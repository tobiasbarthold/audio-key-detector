"""Key detection with Essentia's KeyExtractor (HPCP + key profiles).

The pipeline is exactly the one of Essentia's ``KeyExtractor`` with default settings
(FrameCutter 4096/4096, Hann window, Spectrum, SpectralPeaks, SpectralWhitening, HPCP, Key);
it is run step by step in Essentia's standard mode so the averaged HPCP is available. From it
all 24 key correlations are computed (same formula as Essentia's ``Key``), which gives the
second-best key. The key and strength themselves come from Essentia's ``Key`` algorithm.

The C++ plugin (plugin/Source/KeyEngine.cpp) implements the same steps with Essentia's C++
library; see docs/ALGORITHM.md.
"""

from dataclasses import asdict, dataclass
from typing import Dict, Optional

import numpy as np

import essentia

essentia.log.infoActive = False
essentia.log.warningActive = False
import essentia.standard as es  # noqa: E402

from .audio import load_audio  # noqa: E402

SR = 44100                 # Essentia's KeyExtractor defaults
FRAME_SIZE = 4096
HOP_SIZE = 4096
MIN_FREQ = 25.0
MAX_FREQ = 3500.0
MAX_PEAKS = 60
PEAK_THRESHOLD = 1e-4
PCP_THRESHOLD = 0.2
MAX_SECONDS = 240.0        # analyse the middle 4 minutes of very long files

DEFAULT_PROFILE = "shaath"
# Strength (Pearson correlation of the best key profile) -> confidence 0..1, linear
CONF_LO = 0.5
CONF_HI = 0.9
# "No clear key" gate (see docs/ALGORITHM.md): tonality = share of the 50 Hz - 5 kHz energy
# that sits in clear spectral peaks (16384-sample frames, peak > 10x local average power)
TONAL_FRAME = 16384
TONAL_FMIN = 50.0
TONAL_FMAX = 5000.0
TONAL_PEAK_RATIO = 10.0
TONAL_AVG_BINS = 41
NOISE_TONALITY = 0.025     # below: "mostly noise"
SILENCE_PEAK = 1e-5        # max |sample| below: "silence"

NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
MINOR_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]
# Essentia's key names -> pitch class (Essentia's HPCP bin 0 is A, reference 440 Hz)
_ESS_PC = {"A": 9, "Bb": 10, "B": 11, "C": 0, "C#": 1, "D": 2, "Eb": 3, "E": 4, "F": 5,
           "F#": 6, "G": 7, "Ab": 8, "G#": 8, "Db": 1, "D#": 3, "A#": 10, "Gb": 6}

# Key profiles as in Essentia's key.cpp (index 0 = tonic). Only used to rank all 24 keys
# for the second-best key; the best key itself comes from essentia.standard.Key.
PROFILES = {
    "shaath": ([6.6, 2.0, 3.5, 2.3, 4.6, 4.0, 2.5, 5.2, 2.4, 3.7, 2.3, 3.4],
               [6.5, 2.7, 3.5, 5.4, 2.6, 3.5, 2.5, 5.2, 4.0, 2.7, 4.3, 3.2]),
    "edma": ([1.00, 0.29, 0.50, 0.40, 0.60, 0.56, 0.32, 0.80, 0.31, 0.45, 0.42, 0.39],
             [1.00, 0.31, 0.44, 0.58, 0.33, 0.49, 0.29, 0.78, 0.43, 0.29, 0.53, 0.32]),
    "bgate": ([1.00, 0.00, 0.42, 0.00, 0.53, 0.37, 0.00, 0.77, 0.00, 0.38, 0.21, 0.30],
              [1.00, 0.00, 0.36, 0.39, 0.00, 0.38, 0.00, 0.74, 0.27, 0.00, 0.42, 0.23]),
}


def key_name(pc: int, minor: bool) -> str:
    return "%s %s" % ((MINOR_NAMES if minor else NOTE_NAMES)[pc % 12], "minor" if minor else "major")


def camelot(pc: int, minor: bool) -> str:
    ref = (pc + 3) % 12 if minor else pc % 12          # relative major for minor keys
    return "%d%s" % (((7 * ref + 7) % 12) + 1, "A" if minor else "B")


@dataclass
class KeyResult:
    clear: bool
    key: Optional[str]            # None when no clear key
    camelot: Optional[str]
    confidence: float             # 0..1, mapped from Essentia's strength
    second_key: Optional[str]
    second_camelot: Optional[str]
    strength: float               # Essentia's key strength (correlation of the best key)
    tonality: float               # 0 (noise) .. 1 (purely pitched), see ALGORITHM.md
    reason: str = ""              # why there is no clear key
    best_guess: Optional[str] = None
    profile: str = DEFAULT_PROFILE
    hpcp: list = None             # 12 bins, C..B, averaged and normalised (max 1)
    correlations: list = None     # 24 values: C..B major, then C..B minor

    def to_dict(self) -> Dict:
        d = asdict(self)
        for k in ("confidence", "strength", "tonality"):
            d[k] = round(float(d[k]), 3)
        for k in ("hpcp", "correlations"):
            if d[k] is not None:
                d[k] = [round(float(x), 4) for x in d[k]]
        return d


def confidence_from_strength(strength: float) -> float:
    return float(np.clip((strength - CONF_LO) / (CONF_HI - CONF_LO), 0.0, 1.0))


class _Chain:
    """Essentia standard-mode algorithms of KeyExtractor (created once, reused)."""

    def __init__(self, sr: int = SR):
        self.window = es.Windowing(size=FRAME_SIZE, type="hann")
        self.spectrum = es.Spectrum(size=FRAME_SIZE)
        self.peaks = es.SpectralPeaks(orderBy="magnitude", magnitudeThreshold=PEAK_THRESHOLD,
                                      minFrequency=MIN_FREQ, maxFrequency=MAX_FREQ,
                                      maxPeaks=MAX_PEAKS, sampleRate=sr)
        self.whitening = es.SpectralWhitening(maxFrequency=MAX_FREQ, sampleRate=sr)
        self.hpcp = es.HPCP(bandPreset=False, harmonics=4, maxFrequency=MAX_FREQ,
                            minFrequency=MIN_FREQ, nonLinear=False, normalized="none",
                            referenceFrequency=440, sampleRate=sr, size=12,
                            weightType="cosine", windowSize=1.0, maxShifted=False)
        self.tonal_window = es.Windowing(size=TONAL_FRAME, type="hann")
        self.tonal_spectrum = es.Spectrum(size=TONAL_FRAME)
        self.keys = {}

    def key_algo(self, profile: str):
        if profile not in self.keys:
            self.keys[profile] = es.Key(usePolyphony=False, useThreeChords=False, numHarmonics=4,
                                        slope=0.6, profileType=profile, pcpSize=12)
        return self.keys[profile]


_CHAIN = None


def _chain() -> _Chain:
    global _CHAIN
    if _CHAIN is None:
        _CHAIN = _Chain()
    return _CHAIN


def mean_hpcp(y: np.ndarray) -> np.ndarray:
    """Average HPCP over all frames (Essentia order: bin 0 = A), as KeyExtractor computes it."""
    ch = _chain()
    hp = []
    for frame in _frames(y, FRAME_SIZE, HOP_SIZE):
        spec = ch.spectrum(ch.window(frame))
        freqs, mags = ch.peaks(spec)
        hp.append(ch.hpcp(freqs, ch.whitening(spec, freqs, mags)))
    if not hp:
        return np.zeros(12, dtype=np.float32)
    return np.mean(np.array(hp, dtype=np.float32), axis=0)


def tonality(y: np.ndarray, sr: int = SR) -> float:
    """Share of the 50 Hz - 5 kHz energy that sits in clear spectral peaks (0 = noise)."""
    ch = _chain()
    k = np.arange(TONAL_FRAME // 2 + 1) * (sr / TONAL_FRAME)
    lo = int(np.searchsorted(k, TONAL_FMIN))
    hi = int(np.searchsorted(k, TONAL_FMAX))
    energies, peaks = [], []
    for frame in _frames(y, TONAL_FRAME, TONAL_FRAME, start_from_zero=True):
        mag = ch.tonal_spectrum(ch.tonal_window(frame))
        p = mag[lo - 1:hi + 1].astype(np.float64) ** 2            # one guard bin each side
        local = _moving_average(p, TONAL_AVG_BINS) + 1e-20
        c = p[1:-1]
        is_peak = (c > p[:-2]) & (c >= p[2:]) & (c > TONAL_PEAK_RATIO * local[1:-1])
        energies.append(float(np.sum(p)))
        peaks.append(float(np.sum(c[is_peak])))
    if not energies:
        return 0.0
    e = np.array(energies)
    keep = e > 1e-4 * e.max()
    tot = float(e[keep].sum())
    return float(np.clip(np.array(peaks)[keep].sum() / tot, 0.0, 1.0)) if tot > 0 else 0.0


def _moving_average(p: np.ndarray, n: int) -> np.ndarray:
    """Centred moving average over n bins, edges replicated (scipy uniform_filter1d 'nearest')."""
    h = n // 2
    padded = np.concatenate([np.full(h, p[0]), p, np.full(h, p[-1])])
    c = np.concatenate([[0.0], np.cumsum(padded)])
    return (c[n:] - c[:-n]) / n


def _frames(y: np.ndarray, size: int, hop: int, start_from_zero: bool = False):
    fc = es.FrameCutter(frameSize=size, hopSize=hop, startFromZero=start_from_zero)
    while True:
        frame = fc(y)
        if len(frame) == 0:
            return
        yield frame


def prepare_pcp(mean_hpcp: np.ndarray) -> np.ndarray:
    """Normalise to max 1 and zero bins below PCP_THRESHOLD (as Essentia's streaming Key)."""
    p = np.asarray(mean_hpcp, dtype=np.float32).copy()
    m = float(np.max(p)) if len(p) else 0.0
    if m > 0:
        p = p / np.float32(m)
    p[p < PCP_THRESHOLD] = 0.0
    return p


def correlations(pcp_ess: np.ndarray, profile: str) -> np.ndarray:
    """Pearson correlation of the PCP with all 24 rotated profiles, as in Essentia's Key.

    Returned in this project's order: index 0..11 = C..B major, 12..23 = C..B minor.
    """
    pcp = np.asarray(pcp_ess, dtype=np.float64)
    out = np.zeros(24)
    pc0 = pcp - pcp.mean()
    sp = np.sqrt(np.sum(pc0 ** 2))
    for mode, prof in enumerate(PROFILES[profile]):
        prof = np.asarray(prof, dtype=np.float64)
        pr0 = prof - prof.mean()
        sq = np.sqrt(np.sum(pr0 ** 2))
        for shift in range(12):           # shift = essentia bin of the tonic (0 = A)
            r = 0.0 if sp == 0 or sq == 0 else float(np.dot(pc0, np.roll(pr0, shift)) / (sp * sq))
            out[mode * 12 + (shift + 9) % 12] = r
    return out


def _result_from_hpcp(hpcp: np.ndarray, tonal: float, profile: str) -> KeyResult:
    pcp = prepare_pcp(hpcp)
    if float(np.max(pcp)) <= 0:
        return KeyResult(False, None, None, 0.0, None, None, 0.0, tonal,
                         reason="no pitched content", profile=profile)
    key, scale, strength, _ = _chain().key_algo(profile)(pcp)
    strength = float(strength)
    minor = scale == "minor"
    pc = _ESS_PC[key]
    best = pc + (12 if minor else 0)
    corr = correlations(pcp, profile)
    second = int([i for i in np.argsort(-corr, kind="stable") if i != best][0])
    k1, c1 = key_name(pc, minor), camelot(pc, minor)
    k2, c2 = key_name(second % 12, second >= 12), camelot(second % 12, second >= 12)
    reason = "mostly noise" if tonal < NOISE_TONALITY else ""
    clear = reason == ""
    return KeyResult(clear=clear, key=k1 if clear else None, camelot=c1 if clear else None,
                     confidence=confidence_from_strength(strength), second_key=k2,
                     second_camelot=c2, strength=strength, tonality=tonal, reason=reason,
                     best_guess=k1, profile=profile,
                     hpcp=[float(pcp[(i + 3) % 12]) for i in range(12)],   # A..Ab -> C..B
                     correlations=corr.tolist())


def analyze(y: np.ndarray, sr: int = SR, profile: str = DEFAULT_PROFILE) -> KeyResult:
    """Detect the key of a mono float32 signal sampled at 44.1 kHz."""
    assert sr == SR
    y = np.ascontiguousarray(y, dtype=np.float32)
    if len(y) > MAX_SECONDS * sr:
        mid = len(y) // 2
        half = int(MAX_SECONDS * sr / 2)
        y = y[mid - half:mid + half]
    peak = float(np.max(np.abs(y))) if len(y) else 0.0
    if peak < SILENCE_PEAK:
        return KeyResult(False, None, None, 0.0, None, None, 0.0, 0.0, reason="silence",
                         profile=profile)
    return _result_from_hpcp(mean_hpcp(y), tonality(y, sr), profile)


def detect_key(path: str, profile: str = DEFAULT_PROFILE) -> KeyResult:
    """Load an audio file (any supported format) and detect its key."""
    y, sr = load_audio(path, SR)
    return analyze(y, sr, profile)

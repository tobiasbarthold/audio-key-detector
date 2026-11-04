"""Key detection: peak-based chroma with tuning compensation + key-profile correlation.

Pipeline
--------
1. Mono signal at 22.05 kHz, STFT (8192 window, 2048 hop, Hann).
2. Per frame: pick spectral peaks (local maxima that stand >= 10 dB above the
   local average spectrum, 50 Hz - 5 kHz) and refine them by parabolic
   interpolation. Noise rarely produces such peaks, so noisy material yields
   a low "tonality" score and few/no peaks.
3. Tuning: the deviation of every peak from the nearest 12-TET semitone
   (A4 = 440 Hz) is averaged on the circle (period 100 cents), weighted by
   peak magnitude -> one tuning offset in cents for the whole file.
4. Chroma: peaks are mapped to pitch classes after subtracting the tuning
   offset, weighted by magnitude (compressed), a low-mid register window
   (centred near G3, so rumble and hiss count little) and closeness to the
   semitone centre. Frames are normalised so loud transients do not dominate.
5. The averaged chroma is correlated (Pearson) with 24 rotated key profiles
   from two profile sets - Krumhansl-Kessler and Temperley - and the two
   correlation vectors are averaged. KK is the classic, but is known to favour
   major and the dominant; Temperley is flatter for pop/electronic harmony.
   Averaging reduces the bias of either one.
6. Confidence = (best - second best correlation) / 0.15, clipped to 0..1.
   "No clear key" is reported when tonality is low (mostly noise), when there
   is hardly any pitched content, or when confidence is very low. Exact
   numbers: docs/ALGORITHM.md.
"""

from dataclasses import dataclass, field, asdict
from typing import Dict, Optional

import numpy as np
from scipy.ndimage import uniform_filter1d

from .audio import load_audio

SR = 22050
N_FFT = 8192
HOP = 2048
FMIN = 50.0
FMAX = 5000.0
MAX_SECONDS = 240.0  # analyse the middle 4 minutes of very long files

PEAK_RATIO = 10.0          # peak power vs local average power
NOISE_TONALITY = 0.05     # below: "mostly noise"
MIN_CONFIDENCE = 0.02      # below: "no clear key"
MIN_BEST_CORR = 0.35
REG_CENTER = 55.0          # register weight: Gaussian over MIDI pitch
REG_SIGMA = 20.0
MAG_EXP = 0.5              # peak magnitude compression exponent

NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
MINOR_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]
SHARP_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

KK_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
KK_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]
TEMP_MAJOR = [5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0]
TEMP_MINOR = [5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0]


def key_name(pc: int, minor: bool) -> str:
    return "%s %s" % ((MINOR_NAMES if minor else NOTE_NAMES)[pc], "minor" if minor else "major")


def camelot(pc: int, minor: bool) -> str:
    if minor:
        return "%dA" % ((7 * ((pc + 3) % 12) + 7) % 12 + 1)
    return "%dB" % ((7 * pc + 7) % 12 + 1)


def _profile_matrix() -> np.ndarray:
    """(2, 24, 12): profile sets x keys (12 major then 12 minor) x pitch classes."""
    out = np.zeros((2, 24, 12))
    for s, (maj, mnr) in enumerate([(KK_MAJOR, KK_MINOR), (TEMP_MAJOR, TEMP_MINOR)]):
        for pc in range(12):
            out[s, pc] = np.roll(maj, pc)
            out[s, 12 + pc] = np.roll(mnr, pc)
    out -= out.mean(axis=2, keepdims=True)
    out /= np.linalg.norm(out, axis=2, keepdims=True)
    return out


_PROFILES = _profile_matrix()


def freq_to_note(freq: float) -> str:
    midi = 69 + 12 * np.log2(freq / 440.0)
    n = int(round(midi))
    return "%s%d" % (SHARP_NAMES[n % 12], n // 12 - 1)


@dataclass
class KeyResult:
    clear: bool
    key: Optional[str]            # None when no clear key
    camelot: Optional[str]
    confidence: float
    second_key: Optional[str]
    second_camelot: Optional[str]
    tuning_cents: float
    tonality: float               # 0 (noise) .. 1 (purely pitched)
    reason: str = ""              # why there is no clear key
    best_guess: Optional[str] = None
    strongest_hz: Optional[float] = None
    strongest_note: Optional[str] = None
    chroma: list = field(default_factory=list)
    correlations: list = field(default_factory=list)  # 24 values: 12 major then 12 minor (C..B)
    n_peaks: int = 0

    def to_dict(self) -> Dict:
        d = asdict(self)
        d["confidence"] = round(d["confidence"], 3)
        d["tuning_cents"] = round(d["tuning_cents"], 1)
        d["tonality"] = round(d["tonality"], 3)
        d["chroma"] = [round(float(x), 4) for x in d["chroma"]]
        d["correlations"] = [round(float(x), 4) for x in d["correlations"]]
        if d["strongest_hz"] is not None:
            d["strongest_hz"] = round(d["strongest_hz"], 1)
        return d


def _frames(y: np.ndarray) -> np.ndarray:
    if len(y) < N_FFT:
        y = np.pad(y, (0, N_FFT - len(y)))
    n = 1 + (len(y) - N_FFT) // HOP
    idx = np.arange(N_FFT)[None, :] + HOP * np.arange(n)[:, None]
    return y[idx]


def _spectrum(y: np.ndarray):
    win = np.hanning(N_FFT).astype(np.float32)
    mags = []
    frames = _frames(y)
    for i in range(0, len(frames), 256):  # chunked to bound memory
        mags.append(np.abs(np.fft.rfft(frames[i:i + 256] * win, axis=1)))
    return np.concatenate(mags).astype(np.float32)


def analyze(y: np.ndarray, sr: int = SR) -> KeyResult:
    """Detect the key of a mono signal sampled at `sr` (expected SR)."""
    assert sr == SR
    if len(y) > MAX_SECONDS * sr:
        mid = len(y) // 2
        half = int(MAX_SECONDS * sr / 2)
        y = y[mid - half:mid + half]
    y = y - np.mean(y)
    peak = float(np.max(np.abs(y))) if len(y) else 0.0
    if peak < 1e-5:
        return KeyResult(False, None, None, 0.0, None, None, 0.0, 0.0, reason="silence")
    mag = _spectrum(y)
    freqs = np.fft.rfftfreq(N_FFT, 1.0 / sr)
    lo, hi = int(np.searchsorted(freqs, FMIN)), int(np.searchsorted(freqs, FMAX))
    band = mag[:, lo - 1:hi + 1]                       # one guard bin each side
    power = band.astype(np.float64) ** 2
    local = uniform_filter1d(power, size=41, axis=1, mode="nearest") + 1e-20

    # peaks: local maxima standing well above the local average
    p = power[:, 1:-1]
    is_peak = (p > power[:, :-2]) & (p >= power[:, 2:]) & (p > PEAK_RATIO * local[:, 1:-1])
    frame_energy = power.sum(axis=1)
    keep = frame_energy > 1e-4 * frame_energy.max()      # skip near-silent frames
    is_peak &= keep[:, None]
    fi, bi = np.nonzero(is_peak)
    bi = bi + 1                                          # index into `band`
    a, b, c = (np.log(band[fi, bi - 1] + 1e-12), np.log(band[fi, bi] + 1e-12),
               np.log(band[fi, bi + 1] + 1e-12))
    denom = a - 2 * b + c
    delta = np.where(np.abs(denom) > 1e-9, 0.5 * (a - c) / denom, 0.0)
    delta = np.clip(delta, -0.5, 0.5)
    bin_hz = freqs[1] - freqs[0]
    pf = freqs[lo - 1] + (bi + delta) * bin_hz           # peak frequencies
    pm = band[fi, bi].astype(np.float64)                 # peak magnitudes

    # tonality: share of the in-band energy that sits in peaks
    peak_energy = np.zeros(len(band))
    np.add.at(peak_energy, fi, power[fi, bi])
    w_frame = np.where(keep, frame_energy, 0.0)
    tonality = float((peak_energy * keep).sum() / max(w_frame.sum(), 1e-20))
    tonality = float(np.clip(tonality, 0.0, 1.0))

    # strongest region of the long-term spectrum (reported for noisy material)
    mean_spec = uniform_filter1d(power[keep].mean(axis=0), size=3)
    k = int(np.argmax(mean_spec))
    strongest_hz = float(freqs[lo - 1 + k])
    if 0 < k < len(mean_spec) - 1:
        a_, b_, c_ = np.log(mean_spec[k - 1:k + 2] + 1e-20)
        d_ = a_ - 2 * b_ + c_
        if abs(d_) > 1e-9:
            strongest_hz += float(np.clip(0.5 * (a_ - c_) / d_, -0.5, 0.5)) * float(freqs[1] - freqs[0])
    strongest_note = freq_to_note(strongest_hz)

    def result(**kw):
        base = dict(tonality=tonality, strongest_hz=strongest_hz, strongest_note=strongest_note,
                    n_peaks=int(len(pf)))
        base.update(kw)
        return KeyResult(**base)

    if len(pf) < 8:
        return result(clear=False, key=None, camelot=None, confidence=0.0, second_key=None,
                      second_camelot=None, tuning_cents=0.0,
                      reason="mostly noise" if tonality < NOISE_TONALITY else "no pitched content")

    # tuning estimate: circular mean of semitone deviation, weighted by magnitude
    midi = 69.0 + 12.0 * np.log2(pf / 440.0)
    w = np.sqrt(pm)
    phase = 2j * np.pi * midi
    z = np.sum(w * np.exp(phase)) / np.sum(w)
    tuning = float(np.angle(z) / (2 * np.pi) * 100.0)    # cents, -50..50

    # chroma
    midi_c = midi - tuning / 100.0
    nearest = np.round(midi_c)
    resid = midi_c - nearest
    pc = (nearest.astype(int)) % 12
    reg = np.exp(-0.5 * ((nearest - REG_CENTER) / REG_SIGMA) ** 2)
    wt = np.power(pm, MAG_EXP) * reg * (0.5 + 0.5 * np.cos(2 * np.pi * resid))
    frame_chroma = np.zeros((len(band), 12))
    np.add.at(frame_chroma, (fi, pc), wt)
    tot = frame_chroma.sum(axis=1, keepdims=True)
    frame_chroma = np.where(tot > 0, frame_chroma / np.maximum(tot, 1e-20), 0.0)
    fw = np.sqrt(np.maximum(frame_energy, 0.0)) * keep
    chroma = (frame_chroma * fw[:, None]).sum(axis=0)
    if chroma.sum() <= 0:
        return result(clear=False, key=None, camelot=None, confidence=0.0, second_key=None,
                      second_camelot=None, tuning_cents=tuning, reason="no pitched content")
    chroma = chroma / chroma.sum()

    # correlate with the 24 keys of both profile sets, average
    c = chroma - chroma.mean()
    cn = np.linalg.norm(c)
    if cn < 1e-12:
        corr = np.zeros(24)
    else:
        corr = (_PROFILES @ (c / cn)).mean(axis=0)       # (24,)
    order = np.argsort(corr)[::-1]
    b1, b2 = int(order[0]), int(order[1])
    margin = float(corr[b1] - corr[b2])
    confidence = float(np.clip(margin / 0.15, 0.0, 1.0))

    def name(i):
        return key_name(i % 12, i >= 12), camelot(i % 12, i >= 12)

    k1, c1 = name(b1)
    k2, c2 = name(b2)
    reason = ""
    if tonality < NOISE_TONALITY:
        reason = "mostly noise"
    elif corr[b1] < MIN_BEST_CORR or confidence < MIN_CONFIDENCE:
        reason = "ambiguous / atonal"
    clear = reason == ""
    return KeyResult(
        clear=clear, key=k1 if clear else None, camelot=c1 if clear else None,
        confidence=confidence, second_key=k2, second_camelot=c2, tuning_cents=tuning,
        tonality=tonality, reason=reason, best_guess=k1,
        strongest_hz=strongest_hz, strongest_note=strongest_note, chroma=chroma.tolist(),
        correlations=corr.tolist(), n_peaks=int(len(pf)))


def detect_key(path: str) -> KeyResult:
    """Load an audio file (any supported format) and detect its key."""
    y, sr = load_audio(path, SR)
    return analyze(y, sr)

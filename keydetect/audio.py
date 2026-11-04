"""Audio loading: soundfile first, macOS afconvert / ffmpeg as fallback."""

import math
import os
import shutil
import subprocess
import tempfile
from typing import Tuple

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

AUDIO_EXTENSIONS = (".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".ogg")


class AudioLoadError(Exception):
    pass


def _convert_to_wav(path: str, out_wav: str) -> None:
    """Decode any format to a 16-bit wav using afconvert (macOS) or ffmpeg."""
    errors = []
    afconvert = shutil.which("afconvert")
    if afconvert:
        r = subprocess.run(
            [afconvert, "-f", "WAVE", "-d", "LEI16", path, out_wav],
            capture_output=True,
        )
        if r.returncode == 0 and os.path.exists(out_wav):
            return
        errors.append("afconvert: " + r.stderr.decode(errors="replace").strip()[:200])
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        r = subprocess.run(
            [ffmpeg, "-v", "error", "-y", "-i", path, "-c:a", "pcm_s16le", out_wav],
            capture_output=True,
        )
        if r.returncode == 0 and os.path.exists(out_wav):
            return
        errors.append("ffmpeg: " + r.stderr.decode(errors="replace").strip()[:200])
    if not errors:
        errors.append("neither afconvert nor ffmpeg found")
    raise AudioLoadError("cannot decode %s (%s)" % (path, "; ".join(errors)))


def _read(path: str) -> Tuple[np.ndarray, int]:
    data, sr = sf.read(path, dtype="float32", always_2d=True)
    return data, sr


def load_audio(path: str, target_sr: int = 22050) -> Tuple[np.ndarray, int]:
    """Return (mono float32 signal at target_sr, target_sr)."""
    try:
        data, sr = _read(path)
    except Exception:
        with tempfile.TemporaryDirectory() as tmp:
            wav = os.path.join(tmp, "decoded.wav")
            _convert_to_wav(path, wav)
            try:
                data, sr = _read(wav)
            except Exception as exc:  # pragma: no cover
                raise AudioLoadError("cannot read %s (%s)" % (path, exc))
    if data.shape[0] == 0:
        raise AudioLoadError("%s contains no audio" % path)
    mono = data.mean(axis=1) if data.shape[1] > 1 else data[:, 0]
    mono = np.nan_to_num(mono.astype(np.float32, copy=False))
    if sr != target_sr:
        g = math.gcd(int(sr), int(target_sr))
        mono = resample_poly(mono, target_sr // g, sr // g).astype(np.float32)
    return mono, target_sr

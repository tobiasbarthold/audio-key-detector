#!/usr/bin/env python
"""Generate tests/vectors/*.wav and expected.json (reference output of the Python code).

Run from the repo root:  python scripts/make_test_vectors.py
The WAVs are mono, 44.1 kHz, 16-bit PCM, ~3 s. expected.json is computed from the
WAVs as read back from disk (same samples a C++ port will see).
"""
import json
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "tests"))
from synth import SR, note, progression, scale  # noqa: E402
from keydetect.detect import detect_key  # noqa: E402

OUT = os.path.join(ROOT, "tests", "vectors")


def vectors():
    rs = np.random.RandomState(1234)
    noise = rs.randn(3 * SR) * 0.1
    sos = butter(4, [900 / (SR / 2), 1060 / (SR / 2)], btype="band", output="sos")
    bp = sosfilt(sos, rs.randn(3 * SR))
    bp = 0.5 * bp / np.max(np.abs(bp))
    return {
        "c_major_triads": progression(0, False, dur=0.75),
        "a_minor_triads": progression(9, True, dur=0.75),
        "fsharp_minor_triads_plus30c": progression(6, True, cents=30, dur=0.75),
        "d_major_scale": scale(2, False, dur=0.375),
        "a3_single_note_plus30c": note(57, 3.0, cents=30),
        "white_noise": noise,
        "bandpassed_noise_980hz": bp,
        "silence": np.zeros(SR),
    }


def main():
    os.makedirs(OUT, exist_ok=True)
    expected = {}
    for name, y in vectors().items():
        path = os.path.join(OUT, name + ".wav")
        sf.write(path, y.astype(np.float32), SR, subtype="PCM_16")
        r = detect_key(path).to_dict()
        r["file"] = name + ".wav"
        expected[name] = r
        print(name, r["key"], r["camelot"], r["second_key"], r["confidence"], r["strength"],
              r["tonality"], r["reason"])
    with open(os.path.join(OUT, "expected.json"), "w") as f:
        json.dump(expected, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()

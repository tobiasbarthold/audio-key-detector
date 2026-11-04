#!/usr/bin/env python
"""Check the C++ engine against the Python reference.

    python plugin/tests/compare_vectors.py <keydetect-cpp binary> [audio files or folders...]

Without extra paths it checks tests/vectors/*.wav against tests/vectors/expected.json.
Extra files (e.g. real loops) are analysed by both implementations and compared directly.
Exit code 1 if key, Camelot, second key, best guess, "clear", reason, resonances or fits differ, or if
confidence / strength / tonality differ by more than 0.01 or a resonance frequency by more than 1 Hz.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
from keydetect.cli import collect_files  # noqa: E402
from keydetect.detect import detect_key  # noqa: E402

EXACT = ("key", "camelot", "second_key", "best_guess", "clear", "reason", "resonances", "fits", "fits_partial")
CLOSE = ("confidence", "strength", "tonality")


def cpp(binary, files):
    out = subprocess.run([binary] + files, capture_output=True, text=True, check=False).stdout
    return {os.path.realpath(d["file"]): d for d in json.loads(out)}


def compare(name, got, exp):
    bad = ["%s: C++ %r vs Python %r" % (k, got.get(k), exp.get(k)) for k in EXACT if got.get(k) != exp.get(k)]
    bad += ["%s: C++ %.4f vs Python %.4f" % (k, got[k], exp[k]) for k in CLOSE
            if abs(float(got[k]) - float(exp[k])) > 0.01]
    bad += ["resonance_hz: C++ %r vs Python %r" % (got.get("resonance_hz"), exp.get("resonance_hz"))
            for _ in [0] if len(got.get("resonance_hz", [])) != len(exp.get("resonance_hz", []))
            or any(abs(a - b) > 1.0 for a, b in zip(got.get("resonance_hz", []), exp.get("resonance_hz", [])))]
    print("%-50s %-10s %-4s %s" % (name[:50], exp.get("key") or "(%s)" % exp["reason"], exp.get("camelot") or "",
                                   "OK" if not bad else "DIFF  " + "; ".join(bad)))
    return not bad


def main():
    binary, paths = sys.argv[1], sys.argv[2:]
    ok = True
    if not paths:
        vec = os.path.join(ROOT, "tests", "vectors")
        with open(os.path.join(vec, "expected.json")) as f:
            expected = json.load(f)
        files = [os.path.join(vec, n + ".wav") for n in expected]
        got = cpp(binary, files)
        for n, f in zip(expected, files):
            ok &= compare(n, got[os.path.realpath(f)], expected[n])
    else:
        files = collect_files(paths)
        got = cpp(binary, files)
        for f in files:
            ok &= compare(os.path.basename(f), got[os.path.realpath(f)], detect_key(f).to_dict())
    print("all equal" if ok else "DIFFERENCES FOUND")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python
"""Compare detected keys with keys written in file names (e.g. "Synth Fm 135 bpm.wav").

    python scripts/eval_named_loops.py <folder> [--md]

Samples are only read, never copied. Result classes:
  hit      detected tonic (and mode, if the name states one) equals the name
  related  relative major/minor of the named key (same Camelot number)
  miss     anything else, including "no clear key"
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from keydetect.cli import collect_files  # noqa: E402
from keydetect.detect import camelot, detect_key  # noqa: E402

TOK = re.compile(r"(?:^|\s)([A-G][#b]?)\s?(Minor|minor|min|Maj|maj|Major|m)?(?=\s|\.|\d|$)")
PC = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6,
      "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}
ENH = {"Cb": "B", "Fb": "E", "E#": "F", "B#": "C"}


def expected(name):
    base = os.path.splitext(name)[0]
    base = re.sub(r"\.(wav|aif|aiff)$", "", base)
    base = re.split(r"\s\d+\s?bpm", base)[0]
    ms = list(TOK.finditer(base + " "))
    if not ms:
        return None
    m = ms[-1]
    mode = m.group(2)
    return PC[m.group(1)], (None if mode is None else mode in ("Minor", "minor", "min", "m"))


def parse(key):
    tonic, mode = key.split()
    return {"C": 0, "C#": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "Ab": 8, "G#": 8,
            "A": 9, "Bb": 10, "B": 11}[tonic], mode == "minor"


def main():
    rows = []
    for f in collect_files([sys.argv[1]]):
        exp = expected(os.path.basename(f))
        if exp is None:
            continue
        r = detect_key(f)
        name = os.path.splitext(os.path.basename(f))[0]
        if not r.clear:
            rows.append((name, exp, "no clear key", "miss"))
            continue
        pc, minor = parse(r.key)
        epc, eminor = exp
        if pc == epc and (eminor is None or eminor == minor):
            cls = "hit"
        elif eminor is not None and camelot(pc, minor)[:-1] == camelot(epc, eminor)[:-1] \
                and camelot(pc, minor)[-1] != camelot(epc, eminor)[-1]:
            cls = "related"
        else:
            cls = "miss"
        rows.append((name, exp, "%s %s (%.2f)" % (r.key, r.camelot, r.confidence), cls))
    names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
    for name, (pc, mn), got, cls in rows:
        e = names[pc] + ("" if mn is None else " minor" if mn else " major")
        print("| %s | %s | %s | %s |" % (name, e, got, cls))
    n = len(rows)
    c = {k: sum(1 for r in rows if r[3] == k) for k in ("hit", "related", "miss")}
    print("\n%d files: %d hit, %d related, %d miss  -> hit %.0f%%, hit+related %.0f%%" % (
        n, c["hit"], c["related"], c["miss"], 100.0 * c["hit"] / n, 100.0 * (c["hit"] + c["related"]) / n))


if __name__ == "__main__":
    main()

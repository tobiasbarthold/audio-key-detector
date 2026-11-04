#!/usr/bin/env python
"""Compare detected keys with keys written in file names (e.g. "Synth Fm 135 bpm.wav").

    python scripts/eval_named_loops.py <folder> [--extra <folder> ...] [--profile NAME] [--quiet]

Samples are only read, never copied. The files of <folder> are sorted by name and split
deterministically: even positions (0, 2, ...) are the "tuning" half (the only files that
may be used to choose parameters), odd positions are the "holdout" half. Files under each
--extra folder (searched recursively) whose name states a key *with* a mode, e.g. chord
stabs like "Stab Rit Emin.wav", are reported as a second holdout set ("extra").
Both halves and the total are printed separately. Result classes:
  hit      detected tonic (and mode, if the name states one) equals the name
  related  relative major/minor of the named key (same Camelot number)
  miss     anything else, including "no clear key"
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from keydetect.cli import collect_files  # noqa: E402
from keydetect.detect import DEFAULT_PROFILE, camelot, detect_key  # noqa: E402

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
    return {"C": 0, "C#": 1, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "Ab": 8, "G#": 8,
            "A": 9, "Bb": 10, "B": 11}[tonic], mode == "minor"


def evaluate(path, only_mode=False, profile=DEFAULT_PROFILE):
    rows = []
    files = sorted(collect_files([path]), key=lambda f: os.path.basename(f))
    for f in files:
        exp = expected(os.path.basename(f))
        if exp is None or (only_mode and exp[1] is None):
            continue
        r = detect_key(f, profile)
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
    return rows


def summary(label, rows):
    n = len(rows)
    if n == 0:
        return
    c = {k: sum(1 for r in rows if r[3] == k) for k in ("hit", "related", "miss")}
    print("%-14s %2d files: %2d hit, %2d related, %2d miss  -> hit %.0f%%, hit+related %.0f%%" % (
        label, n, c["hit"], c["related"], c["miss"], 100.0 * c["hit"] / n,
        100.0 * (c["hit"] + c["related"]) / n))


def main():
    args = sys.argv[1:]
    quiet = "--quiet" in args
    extras = [args[i + 1] for i, a in enumerate(args) if a == "--extra"]
    profile = [args[i + 1] for i, a in enumerate(args) if a == "--profile"] or [DEFAULT_PROFILE]
    folder = [a for i, a in enumerate(args)
              if not a.startswith("--") and (i == 0 or args[i - 1] not in ("--extra", "--profile"))][0]
    rows = evaluate(folder, profile=profile[0])
    names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
    halves = {"tuning": rows[0::2], "holdout": rows[1::2]}
    if not quiet:
        for label, part in halves.items():
            for name, (pc, mn), got, cls in part:
                e = names[pc] + ("" if mn is None else " minor" if mn else " major")
                print("| %s | %s | %s | %s | %s |" % (label, name, e, got, cls))
        print()
    for label, part in halves.items():
        summary(label, part)
    summary("total", rows)
    ex = []
    for e in extras:
        ex += evaluate(e, only_mode=True, profile=profile[0])
    summary("extra holdout", ex)


if __name__ == "__main__":
    main()

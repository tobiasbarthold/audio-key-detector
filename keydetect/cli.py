"""Command line interface: keydetect <files or folders...> [--json]"""

import argparse
import json
import os
import sys
from typing import Iterable, List

from . import __version__
from .audio import AUDIO_EXTENSIONS
from .detect import DEFAULT_PROFILE, PROFILES, detect_key


def collect_files(paths: Iterable[str]) -> List[str]:
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, names in os.walk(p):
                dirs[:] = sorted(d for d in dirs if not d.startswith("."))
                for n in sorted(names):
                    if n.lower().endswith(AUDIO_EXTENSIONS) and not n.startswith("."):
                        files.append(os.path.join(root, n))
        else:
            files.append(p)
    return files


def format_result(path: str, r) -> str:
    name = os.path.basename(path)
    if r.clear:
        return "%s\n  %s  (%s)  confidence %.2f  |  2nd: %s (%s)" % (
            name, r.key, r.camelot, r.confidence, r.second_key, r.second_camelot)
    guess = "; best guess %s" % r.best_guess if r.best_guess else ""
    out = "%s\n  no clear key  (%s%s)" % (name, r.reason, guess)
    if r.resonances:
        out += "\n  resonances: %s" % " \u00b7 ".join(r.resonances)
        out += "\n  fits: %s%s" % (" \u00b7 ".join(r.fits), " (partial)" if r.fits_partial else "")
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        prog="keydetect", description="Detect the musical key of audio files (wav/aif/flac/mp3/m4a/ogg).")
    ap.add_argument("paths", nargs="+", help="audio files or folders (scanned recursively)")
    ap.add_argument("--json", action="store_true", help="machine-readable JSON output")
    ap.add_argument("--profile", default=DEFAULT_PROFILE, choices=sorted(PROFILES),
                    help="Essentia key profile (default: %(default)s)")
    ap.add_argument("--version", action="version", version="keydetect " + __version__)
    args = ap.parse_args(argv)

    files = collect_files(args.paths)
    if not files:
        print("keydetect: no audio files found", file=sys.stderr)
        return 1
    status = 0
    out = []
    for f in files:
        try:
            r = detect_key(f, args.profile)
        except Exception as exc:
            status = 1
            if args.json:
                out.append({"file": f, "error": str(exc)})
            else:
                print("%s\n  error: %s" % (os.path.basename(f), exc), file=sys.stderr)
            continue
        if args.json:
            d = r.to_dict()
            d.pop("hpcp", None)
            d.pop("correlations", None)
            d["file"] = f
            out.append(d)
        else:
            print(format_result(f, r))
    if args.json:
        print(json.dumps(out, indent=2))
    return status


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check #43: the grimoire's first highlight (README, Checks against the
original: "Open the grimoire: the first spell highlighted is the last row
shown").

SAVE/'s game A, loaded from the start menu in the original and in eclplay
--start-menu, to the outpost inn's camp; KAL (a cleric/mage, the fifth)
picked with the down arrow, Magic, Memorize: the grimoire (no spell is
marked, so it opens at once), then a down arrow and an up arrow in it. The
screens are compared with eclplay's nearest frames.
"""
import argparse
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-grimoire-"))
    waits = {}
    steps = S.a_camp_steps() + [(r"\v\v\v\vMM", "grimoire"), (r"\v", "down"), (r"\^", "up")]
    status, results, out = S.check(work, [L.SAVE], steps, verbose=args.verbose,
                                   during=lambda box, n: waits.setdefault(n, box.where()))
    for n, w in waits.items():
        print(f"    {n}: original waits in {w}")
    print("AGREES" if status == 0 else "PORT DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

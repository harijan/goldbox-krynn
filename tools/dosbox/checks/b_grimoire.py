#!/usr/bin/env python3
"""Check #43: the grimoire's first highlight (README, Checks against the
original: "Open the grimoire: the first spell highlighted is the last row
shown").

SAVE/'s game A, loaded from the start menu in the original and in eclplay
--start-menu, to the outpost inn's camp; KAL (a cleric/mage, the fifth)
picked with the down arrow, Magic, Memorize: the grimoire (no spell is
marked, so it opens at once), then a down arrow and an up arrow in it. Each
screen is compared with eclplay's at the same point of the keys, and the
original must wait in the grimoire's list (67b5:15d2, as recorded).
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402
import checklib  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-grimoire")
    waits = {}
    steps = S.a_camp_steps() + [(r"\v\v\v\vMM", "grimoire"), (r"\v", "down"), (r"\^", "up")]
    v, _, _, _ = S.check(work, [L.SAVE], steps, verbose=args.verbose,
                         during=lambda box, n: waits.setdefault(n, box.where()))
    for n in ("grimoire", "down", "up"):
        v.check("67b5:15d2" in str(waits[n]), f"the original waits in the grimoire at {n}",
                str(waits[n]))
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

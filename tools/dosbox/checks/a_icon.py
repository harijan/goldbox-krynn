#!/usr/bin/env python3
"""Check: the icon editor's Parts and space (README, Checks against the
original: "In the icon editor, choose `Parts` and press space: no key should
leave the menu that follows").

A human fighter is created (seed 7) up to the icon editor; `P` opens Parts'
menu and a space there opens `Next Prev Keep Exit` with nothing to edit.
Then every key below is typed, one at a time: after each the screen is
compared with the one before and with eclplay's best frame, and where the
original waits (its call chain) is printed.
"""
import argparse
import sys

import a_common as A

TO_EDITOR = r"\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r"
# Escape, the menu's letters, Enter, space, arrows, digits, F10, Alt-X, Ctrl-C.
TRIES = [r"\e", "E", "K", "N", "P", "e", "k", r"\r", " ", r"\v", r"\^", r"\>", r"\<", "2", "8",
         "x", r"\#44", r"\@x", "Y", r"\e"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "icon")
    keys = TO_EDITOR + r"\seditor.P\sparts. \sspace." + "".join(
        f"{k}\\stry{i:02d}." for i, k in enumerate(TRIES))
    pair = A.Pair(keys, work, seed=7, verbose=args.verbose)
    print("port:", " | ".join(pair.out.strip().splitlines()[-4:]))
    bad = 0
    with pair:
        pair.run(TO_EDITOR + r"\seditor.P\sparts. \sspace.")
        for s in ("editor", "parts", "space"):
            bad += pair.compare(s)[0] != 0
        space = A.cokbox.Screen.load(pair.work / "shots" / "space.png")
        where = pair.box.where()
        print(f"  after the space the original waits at {where}")
        for i, k in enumerate(TRIES):
            pair.run(k)
            shot = pair.shot(f"try{i:02d}")
            now = pair.box.where()
            same, _ = shot.diff(space)
            count, _, _ = pair.compare(f"try{i:02d}", quiet=True)
            print(f"  key {k!r}: {same} pixels changed from the space's screen, {count} differ "
                  f"from eclplay's; waits at {now}")
            bad += count != 0 or now[:3] != where[:3]
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS / see above", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

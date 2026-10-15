#!/usr/bin/env python3
"""Check #112: the icon editor's Parts and space (README, Checks against the
original: "In the icon editor, choose `Parts` and press space: no key should
leave the menu that follows").

A human fighter is created (seed 7) up to the icon editor; `P` opens Parts'
menu and a space there opens `Next Prev Keep Exit` with nothing to edit.
Then every key below is typed, one at a time: after each the screen is
compared with the port's at the same key read, and the original must still
wait in the menu it waited in after the space (its call chain's first
three return addresses; the port's staying is its screen).
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

TO_EDITOR = r"\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r"
# Escape, the menu's letters, Enter, space, arrows, digits, F10, Alt-X, Ctrl-C.
TRIES = [r"\e", "E", "K", "N", "P", "e", "k", r"\r", " ", r"\v", r"\^", r"\>", r"\<", "2", "8",
         "x", r"\#44", r"\@x", "Y", r"\e"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "icon")
    keys = TO_EDITOR + r"\seditor.P\sparts. \sspace." + "".join(
        f"{k}\\stry{i:02d}." for i, k in enumerate(TRIES))
    pair = A.Pair(keys, work, seed=7, verbose=args.verbose)
    print("port:", " | ".join(pair.out.strip().splitlines()[-4:]))
    v = checklib.Verdict()
    where = {}
    with pair:
        pair.steps(lambda mark: where.__setitem__(mark, pair.box.where()))
    print(f"  after the space the original waits at {where['space']}")
    for mark in pair.marks():
        pair.judge(v, mark)
        if mark.startswith("try"):
            k = TRIES[int(mark[3:])]
            v.check(where[mark][:3] == where["space"][:3],
                    f"key {k!r}: the original still waits in the menu", f"at {where[mark]}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

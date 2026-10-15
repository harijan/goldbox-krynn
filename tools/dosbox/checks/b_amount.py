#!/usr/bin/env python3
"""Check #52: Backspace in an amount (README, Checks against the original:
"In an amount, type more than the coins there are, then Backspace: the last
digit stays on the screen").

SAVE/'s game A in the outpost inn's camp, in the original and eclplay
--start-menu: View, Trade, ISTAN HORBIN picked (down arrow, Select), the
steel picked (Select): "How much steel will you trade? ", then 9 (SIR
STRONGSWORD has 2: the amount becomes all of it, 2), then Backspace, then
Escape. The screens after each key are compared with eclplay's frames, and
the original's row 24 after Backspace is read: the digit should stay.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402
import checklib  # noqa: E402


def ink(shot):
    """The columns past 30 of row 24 with white ink (the amount's digits)."""
    return sorted({x // 8 for x in range(31 * 8, 320) for y in range(192, 200)
                   if shot.at(x, y) == 15})


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "b-amount")
    steps = S.a_camp_steps() + [(r"VT\vSS", "prompt"), ("9", "typed"), (r"\b", "backspace")]
    v, shots, port, out = S.check(work, [L.SAVE], steps, verbose=args.verbose)
    for name in ("prompt", "typed", "backspace"):
        print(f"  {name}: white ink on row 24 past column 30: original {ink(shots[name])}, "
              f"port {ink(port[name]) if port.get(name) else None}")
    v.check(ink(shots["backspace"]) == ink(shots["typed"]) != [],
            "the digit stays after Backspace in the original")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

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
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-amount-"))
    steps = S.a_camp_steps() + [(r"VT\vSS", "prompt"), ("9", "typed"), (r"\b", "backspace")]
    status, results, out = S.check(work, [L.SAVE], steps, verbose=args.verbose)
    for name in ("prompt", "typed", "backspace"):
        shot = results[name][3]
        ink = sorted({(x // 8) for x in range(320) for y in range(192, 200)
                      if shot.at(x, y) == 15 and x >= 31 * 8})
        print(f"  original's {name}: white ink on row 24 past column 30 in columns {ink}")
    print("AGREES" if status == 0 else "PORT DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check #50: the character sheet (DEVELOPERS.md, Checks against the original:
"Open a character's sheet: compare the layout and colours with the table in
View, a fighter/mage's class line (`Fighter/Red Mage`) and an exceptional
strength (`18(00)`)").

SAVE/'s game A in the outpost inn's camp, in the original and eclplay
--start-menu: View of SIR STRONGSWORD (strength 18/100, `18(00)`), of
ISTAN HORBIN (18/79), and of GARIN (a fighter/mage of order 2), picked with
the down arrow; each screen is compared with eclplay's at the same point of
the keys.
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
    work = checklib.workdir(args.workdir, "b-sheet")
    steps = S.a_camp_steps() + [("V", "sir"), (r"E\vV", "istan"), (r"E\vV", "garin")]
    v, _, _, out = S.check(work, [L.SAVE], steps, verbose=args.verbose)
    lines = [line for line in out.splitlines() if line.startswith("print: ")]
    print("  eclplay's class lines:", [l for l in lines if "/" in l or "Knight" in l][:4])
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

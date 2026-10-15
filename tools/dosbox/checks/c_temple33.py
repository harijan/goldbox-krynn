#!/usr/bin/env python3
"""The cleric round the corner in Throtl's temple (README, Checks against
the original): #55 "In Throtl's temple (ECL1 block 33, the cleric round the
corner), `Wait` should bring him nearer, and the menu should start on the
item chosen in the menu before; parlaying `Sly` should lead to the stack of
papers." Also #54's distance 2: the cleric's sprite at 2 and 1 masked over
the view (ENCOUNTER MENU 35 2 41), which Throtl's wanderers never show.

The port writes a saved game in ECL1 block 33 (as c_catacombs.py), moved in
the file to 7,13 facing east; a step into 8,13 (special 2, 848f): the
mutterings, then ENCOUNTER MENU with the sprite at 2 (`YOU SEE A CLERIC
ROUND THE CORNER.`), Wait (1, `A CLERIC APPROACHES.`), Wait (0, `HE
MOTIONS AND OTHER PEOPLE JOIN HIM.`, PARLAY in place of ADVANCE), Parlay,
Sly: `THE CLERIC NODS AND POINTS YOU TO A STACK OF PAPERS.`
"""
import argparse
import sys

import c_geo
import c_places as cp

ARGS = ["--load", cp.SAVE_A, "--play", "--file", "1", "--set", "4be6=1", "--set", "4c61=ff",
        "--set", "4c09=ff"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("temple33", args.workdir)
    saves, _ = cp.port_save(work / "save", "ESI", ARGS, block=33)
    cp.edit_save(saves, "I", at=(7, 13, 2), var={0x4C61: 0xFF, 0x4C09: 0xFF},
                 geo=c_geo.load(1, 32))
    c = cp.Compare(work / "run", saves, [
        ("loaded", cp.load_keys("I")),
        ("mutterings", r"m\^"),
        ("corner", r"\r"),          # the sprite at 2; COMBAT WAIT FLEE ADVANCE
        ("wait1", "W"),             # at 1, Wait still selected
        ("wait2", "W"),             # at 0: the close-up; COMBAT WAIT FLEE PARLAY
        ("parlay", "P"),            # HAUGHTY SLY NICE MEEK ABUSIVE
        ("sly", "S"),               # THE CLERIC NODS AND POINTS YOU TO A STACK OF PAPERS.
        ("letter", r"\r"),
    ], seed=1, verbose=args.verbose)
    status = c.run()
    print("port:", [l for l in c.port_text.splitlines()
                    if l.startswith(("monster: sprite", "choice:")) or "PAPERS" in l][-10:])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Checks at Throtl's gate (README, Checks against the original):
#53 "Enter Throtl from the south (7, 15 facing north): the guards' close-up
should show at once"; #27 "Wall tiles and sky pictures are masked with
colour 13 in the 3D view ... Throtl's street from 7, 15 facing north";
#93 "In Throtl, choose `Area` at 7, 15 facing north: the map should replace
the view ... Walk north and turn: the map should follow. `Area` again
should show the view"; #94 "With the map on, stand by a door or a wall the
party can pass: it should show as a wall, and only on the square whose side
holds it" (7,15's north side and 7,14's south, a wall type 1 with a way
through; 7,13's doors, type 7, on three sides).

The port writes two saved games on the overland map next to Throtl (game
A's party, as if from the first outpost with the caravan met: vars 0x4bf2
0x11, 0x4bc3 1, 0x4bc4 3, 0x4c2d 1, one step north-east to 2,2, south of
Throtl, then camp's Save): C as the party first comes, D with
the guards beaten (var 0x4c4d bit 2, so that 8437 skips them). The original
and eclplay load each from the title, step north onto Throtl's square and
enter; their screens are compared at each step.
"""
import argparse
import sys

import c_places as cp
import checklib

TO_THROTL = "nm9EES"       # decline the outpost, Move NE (2,2, south of Throtl), Exit, Encamp, Save
OVERLAND = cp.OVERLAND


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--machine", default="tandy")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-throtl")
    v = checklib.Verdict()

    saves, _ = cp.port_save(work / "save-gate", TO_THROTL + "C", OVERLAND, block=16)
    gate = cp.Compare(v, work / "gate", saves, [
        ("loaded", cp.load_keys("C")),     # the overland map, Move Encamp
        ("near", "m8"),                    # YOU ARE NEAR THROTL. DO YOU ENTER?
        ("guards", "y"),                   # #53: the guards' close-up and ATTACK LEAVE
    ], seed=1, machine=args.machine, verbose=args.verbose).run()
    v.check("monster: sprite 12 at 0" in gate.port_text and
            "monster: sprite 12 at 1" not in gate.port_text,
            "#53: the port draws the hobgoblin at distance 0 at once (its log), the screens "
            "of both equal")

    saves, _ = cp.port_save(work / "save-street", TO_THROTL + "D",
                            OVERLAND + ["--set", "4c4d=2"], block=16)
    street = cp.Compare(v, work / "street", saves, [
        ("near", cp.load_keys("D") + "m8"),
        ("street", "y"),                   # #27: the 3D view at 7,15 N (in Move: Exit)
        ("menu", "e"),                     # Exit: the adventure's menu
        ("area", "a"),                     # #93: the map in the view's place
        ("north", r"m\^"),                 # a step north with the map on
        ("doors", r"\^"),                  # #94: 7,13, doors (wall type 7) north, east, west
        ("turn", r"\>"),                   # a turn east
        ("view", "ea"),                    # Exit Move, Area again: the view
    ], seed=1, machine=args.machine, verbose=args.verbose).run()
    print("port log:", [l for l in street.port_text.splitlines()
                        if l.startswith(("area:", "at:"))])
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

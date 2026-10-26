#!/usr/bin/env python3
"""A wall held by one square's side only, on the overhead map (docs/checks.md,
Checks against the original, #94): "With the map on, stand by a door or a
wall the party can pass: it should show as a wall, and only on the square
whose side holds it."

Of the shipped maps only GEO3 record 80 (Sanction, ECL3 block 80) has
sides whose wall the square across does not hold: its east edge, x 15,
whose east sides have walls that the squares at x 0 (across the wrap) do
not; 15,7's (wall type 6) is one the party can pass. The port writes a
saved game in block 80 (game A's party, through the city's welcome), moved
in the file beside it: to 0,7 facing west, 15,6 facing south and 14,7
facing east (quiet squares); in each, Area: the map, compared.
"""
import argparse
import shutil
import sys

import c_geo
import c_places as cp
import checklib

PLACES = {"west-of-0-7": (0, 7, 6), "above-15-7": (15, 6, 4), "beside-15-7": (14, 7, 2)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-map80")
    v = checklib.Verdict()
    saves, _ = cp.port_save(work / "save", r"\r\rESE", [
        "--load", cp.SAVE_A, "--play", "--file", "3", "--set", "4be6=1"], block=80)
    geo = c_geo.load(3, 80)
    for name, at in PLACES.items():
        here = work / f"saves-{name}"
        shutil.rmtree(here, ignore_errors=True)  # a run before in the same folder
        shutil.copytree(saves, here)
        cp.edit_save(here, "E", at=at, geo=geo)
        cp.Compare(v, work / name, here, [
            ("loaded", cp.load_keys("E")),
            ("map", "a"),
            ("turned", r"m\>"),
        ], seed=1, verbose=args.verbose).run()
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

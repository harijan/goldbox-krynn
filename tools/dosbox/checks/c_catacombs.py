#!/usr/bin/env python3
"""The catacombs of Throtl (ECL1 block 34; DEVELOPERS.md, Checks against the
original): #96 "In the catacombs of Throtl, `Area` should show `Not Here` in
yellow on row 24 for a moment, and the status line should have no square.
Start the game with `Helm` as its second argument (`START x Helm`): `Area`
should show the map there, and the next step should turn it off"; #97 "Walk
into the catacombs with the map on: the first view inside should be the 3D
view."

The port writes a saved game in ECL1 block 33 (the second half of Throtl,
the same map GEO1 32) with var 0x4c61 0xff (the stairs' ward gone) and no
wandering monsters, moved in the file to 8,0 facing east. Both games: Area
(map on), Move, a step east onto 9,0 (`THERE ARE STAIRS GOING DOWN IN THE
NORTH WALL.`), turn north and step off the map: block 33's after-move
vector NEWECLs 34. Then Exit and Area: `Not Here` (the game's speed, var
0x4bfc, at 20 in the saved game: the notice stays 2 seconds, long enough
for the screenshots), the original's screens from Area to the next key
read compared in order with the port's (c_places.judge_watch). Again with `START x
Helm` (eclplay --helm): Area shows the map in the catacombs, a step turns
it off.
"""
import argparse
import shutil
import sys

import c_geo
import c_places as cp
import checklib

# It judges what the original shows as it comes: its boxes at their cycles
# (when it runs, not when another check imports it).
if __name__ == "__main__":
    checklib.cokbox.REAL_TIME = True

ARGS = ["--load", cp.SAVE_A, "--play", "--file", "1", "--set", "4be6=1", "--set", "4c61=ff",
        "--set", "4c09=ff"]
TO_STAIRS = [("area", "a"), ("move", "m"), ("stairs", r"\^"), ("turn", r"\r\<"),
             ("inside", r"\^")]     # #97: YOU'VE ENTERED THE DARK, BEFOULED CATACOMBS ...


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="plain or helm")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-catacombs")
    v = checklib.Verdict()
    saves, _ = cp.port_save(work / "save", "ESI", ARGS, block=33)
    cp.edit_save(saves, "I", at=(8, 0, 2), var={0x4C61: 0xFF, 0x4C09: 0xFF},
                 geo=c_geo.load(1, 32))

    if args.only in (None, "plain"):
        # The game's speed (var 0x4bfc, Alter's Speed) at 20, not 4: `Not
        # Here` stays speed x 100 ms, two seconds, long enough for the
        # screenshots (at speed 4 they never caught it).
        slow = work / "save-slow"
        shutil.copytree(saves, slow, dirs_exist_ok=True)
        cp.edit_save(slow, "I", var={0x4BFC: 20})
        c = cp.Compare(v, work / "plain", slow, [("loaded", cp.load_keys("I"))] + TO_STAIRS + [
            ("inside-menu", r"\re"),
            ("nothere", "a", None, 20),     # #96: Not Here on row 24, then the menu again
        ], seed=1, verbose=args.verbose).run()
        c.judge_watch("nothere")
        menu = c.shown["nothere"].pixels
        v.check(any(s.pixels != menu for s in c.watched["nothere"]),
                "#96: the original's screens caught the notice before the menu (so that the "
                "notice is compared)", f"{len(c.watched['nothere'])} distinct screens")
        print("  port:", [l for l in c.port_text.splitlines() if "Not Here" in l or "area" in l])

    if args.only in (None, "helm"):
        h = cp.Compare(v, work / "helm", saves, [("loaded", cp.load_keys("I"))] + TO_STAIRS + [
            ("inside-menu", r"\re"),
            ("map", "a"),                   # with Helm: the map in the catacombs
            ("step", r"m\^"),               # the next step: the map off
        ], seed=1, helm=True, verbose=args.verbose).run()
        print("  port (Helm):", [l for l in h.port_text.splitlines()
                                 if l.startswith(("area:", "at:", "monster:"))])
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

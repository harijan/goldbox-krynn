#!/usr/bin/env python3
"""The catacombs of Throtl (ECL1 block 34; README, Checks against the
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
vector NEWECLs 34. Then Exit and Area: `Not Here`. Again with `START x
Helm` (eclplay --helm): Area shows the map in the catacombs, a step turns
it off.
"""
import argparse
import shutil
import sys

import c_geo
import c_places as cp

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
    cp.need_save_a()
    work = cp.workdir("catacombs", args.workdir)
    saves, _ = cp.port_save(work / "save", "ESI", ARGS, block=33)
    cp.edit_save(saves, "I", at=(8, 0, 2), var={0x4C61: 0xFF, 0x4C09: 0xFF},
                 geo=c_geo.load(1, 32))
    status = 0
    notehere = {}

    def area(box):
        box.keys("a")
        notehere["shots"] = cp.watch(box, 20, work / "plain", "nothere")
        box.wait_input(settle=1.0)
        notehere["end"] = box.screenshot(work / "plain" / "after.png")

    if args.only == "helm":
        return helm(work, saves, args)
    # The game's speed (var 0x4bfc, Alter's Speed) at 20, not 4: `Not Here`
    # stays speed x 100 ms, two seconds, long enough for the screenshots.
    shutil.copytree(saves, work / "save-slow")
    saves = work / "save-slow"
    cp.edit_save(saves, "I", var={0x4BFC: 20})
    c = cp.Compare(work / "plain", saves, [("loaded", cp.load_keys("I"))] + TO_STAIRS +
                   [("inside-menu", r"\re")], seed=1, verbose=args.verbose)
    status |= c.run(box_hook=area)
    frames, _ = cp.cok_frames(work / "cok-plain", cp.load_keys("I") + "".join(
        k for _, k in TO_STAIRS) + r"\rea\r", saves=saves, seed=1, patched=True)
    # The menu the notice gives way to is the screen at the next key read,
    # compared with eclplay's below (cok's script may end before it shows it).
    during = [s for s in notehere["shots"] if s.pixels != notehere["end"].pixels]
    misses = cp.match_frames(during, frames)
    print(f"#96 Area in the catacombs: {len(during)} distinct screens before the menu; pixels "
          f"differing from the nearest frame cok presents: {misses}")
    _, _, shots = cp.cokbox.eclplay(cp.load_keys("I") + "".join(k for _, k in TO_STAIRS) +
                                    r"\rea", work / "eclplay-after", saves=[saves],
                                    args=["--seed", "1"])
    after = min(notehere["end"].diff(cp.cokbox.Screen.load(p))[0] for p in shots)
    print(f"  the screen at the next key read: {after} pixels differ from eclplay's")
    misses.append(after)
    print("  port:", [l for l in c.port_text.splitlines() if "Not Here" in l or "area" in l])
    if any(misses):
        status = 1

    if args.only == "plain":
        return 1 if status else 0
    return 1 if status | helm(work, saves, args) else 0


def helm(work, saves, args):
    status = 0
    h = cp.Compare(work / "helm", saves, [("loaded", cp.load_keys("I"))] + TO_STAIRS + [
        ("inside-menu", r"\re"),
        ("map", "a"),                   # with Helm: the map in the catacombs
        ("step", r"m\^"),               # the next step: the map off
    ], seed=1, helm=True, verbose=args.verbose)
    status |= h.run()
    print("  port (Helm):", [l for l in h.port_text.splitlines()
                             if l.startswith(("area:", "at:", "monster:"))])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

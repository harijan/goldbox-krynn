#!/usr/bin/env python3
"""A wandering monster in Throtl (README, Checks against the original):
#54 "Meet a wandering monster in an open corridor: compare the sprite at
distances 2 and 1, masked over the view at its header's place + 3 cells,
with colour 13 black, and the close-up at 0, with the original"; #95 "With
the map on, meet a wandering monster: the map should give way to the view
with the monster's sprite."

A saved game inside Throtl at 7,15 N with wandering monsters on (vars
0x4c09 and 0x4c0e 0). Seed 0: the first step north (7,14, special 0) rolls
8320's d100 at 10 or less, `MONSTERS ATTACK!`, the hobgoblins' sprite
(SETUP MONSTER from the table, distance 1, all a wanderer of Throtl
gets), APPROACH to 0 and the close-up. The original's screens during the
approach are compared with every frame cok presents for the same keys
(C_PATCHED_COK: a cok with #27's fix, so that the walls behind the sprite
agree; without it the 3D view's cells are left out of the comparison), the
screen at the next key read with eclplay's.
"""
import argparse
import os
import sys

import c_places as cp


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("wander", args.workdir)
    saves, _ = cp.throtl_save(work / "save", "J", var={0x4C09: 0, 0x4C0E: 0})
    patched = bool(os.environ.get("C_PATCHED_COK"))
    mask = None if patched else cp.VIEW
    status = 0
    for name, before in (("view", ""), ("map", "a")):
        keys = cp.load_keys("J") + before + "m"
        frames, _ = cp.cok_frames(work / f"cok-{name}", keys + r"\^\r", saves=saves,
                                  seed=args.seed, patched=True)
        seen = {}

        def meet(box):
            box.keys(r"\^")
            seen["shots"] = cp.watch(box, 25, work / name, "approach")
            box.wait_input(settle=1.0)
            seen["end"] = box.screenshot(work / name / "end.png")

        steps = [("loaded", cp.load_keys("J"))] + ([("area", before)] if before else []) + \
            [("move", "m")]
        # At 2000 cycles (not 20000) each frame of the approach lasts long
        # enough for DOSBox-X's screenshots, about three a second.
        c = cp.Compare(work / name, saves, steps, seed=args.seed, verbose=args.verbose,
                       cycles="fixed 2000", timeout=1800)
        status |= c.run(box_hook=meet)
        misses = cp.match_frames(seen["shots"] + [seen["end"]], frames, mask)
        print(f"#{54 if name == 'view' else 95} ({name}): {len(seen['shots'])} distinct screens "
              "during the approach and the one at the next key read; pixels differing from "
              f"the nearest frame cok presents{'' if patched else ' outside the 3D view'}: "
              f"{misses}")
        text, _, shots = cp.cokbox.eclplay(keys + r"\^", work / f"eclplay-{name}-meet",
                                           saves=[saves], args=["--seed", str(args.seed)])
        best = min(shots, key=lambda p: seen["end"].diff(cp.cokbox.Screen.load(p))[0])
        shot = cp.cokbox.Screen.load(best)
        print(f"  at the next key read against eclplay's {best.name}: {seen['end'].diff(shot)}"
              f"; outside the 3D view: {cp.masked_diff(seen['end'], shot, cp.VIEW)}")
        print("  port:", [l for l in text.splitlines() if l.startswith(("monster:", "area:"))
                          or "ATTACK" in l])
        if any(misses):
            status = 1
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""A wandering monster in Throtl (DEVELOPERS.md, Checks against the original):
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
approach, as fast as DOSBox-X takes them (at 2000 cycles, so that each of
its frames lasts long enough), are compared in order with the port's
screens for the same keys, as drawn (c_places.judge_watch) or as cok
presents them, and 30 seconds of cok's frames at the close-up's wait (its
picture animates there: the original's screens of that wait are frames of
that animation); the screen at the key read after it with eclplay's.
"""
import argparse
import sys

import c_places as cp
import checklib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-wander")
    v = checklib.Verdict()
    saves, _ = cp.throtl_save(work / "save", "J", var={0x4C09: 0, 0x4C0E: 0})
    for name, before in (("view", ""), ("map", "a")):
        steps = [("loaded", cp.load_keys("J"))] + ([("area", before)] if before else []) + \
            [("move", "m"), ("meet", r"\^", None, 25)]
        # At 2000 cycles (not 20000) each frame of the approach lasts long
        # enough for DOSBox-X's screenshots, about three a second.
        c = cp.Compare(v, work / name, saves, steps, seed=args.seed, verbose=args.verbose,
                       cycles="fixed 2000", timeout=1800).run()
        c.judge_watch("meet", pause=3000)
        log = [l for l in c.port_text.splitlines() if l.startswith(("monster:", "area:"))
               or "ATTACK" in l]
        v.check("print: MONSTERS ATTACK!" in log and "monster: sprite 12 at 1" in log and
                "monster: picture 12" in log,
                f"#{54 if name == 'view' else 95}: the step meets the wanderer, its sprite at 1, "
                "then the close-up (the port's log; the screens of both equal)", str(log))
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

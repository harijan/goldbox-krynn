#!/usr/bin/env python3
"""Check #13's battle part (README, Checks against the original: "The debug
mode ...: and, in a battle, the trace of each combatant and `Done*`").

The original runs as `START Woof`, eclplay as --woof, both with seed 7 and
SAVE/'s games: game A loaded, Solace's pages to the outpost, Leave, No,
Move, Ctrl-D (the flag on), then the step north-east that meets the
caravan's four baaz (batch D's route), and Enter at each key read after it
(the trace waits for one after each opcode, the battle's setup after each
record, the combat menu reads one too), a screen each, compared with
eclplay's best frame.
"""
import argparse
import sys

import a_common as A

cokbox = A.cokbox
SAVES = [cokbox.ROOT / "SAVE"]
TO_MOVE = r"P\v\v\rA\v\v\v\v\v\r" + r"\r" * 7 + "Lnm"    # the overland map, in Move
SEED = 7


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--steps", type=int, default=120)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "debug-battle")
    keys = (TO_MOVE + r"\cD\son." + "9" + r"\sstep." +
            "".join(rf"\r\st{i:03d}." for i in range(args.steps)))
    pair = A.Pair(keys, work, seed=SEED, saves=SAVES, box_args="Woof", eclplay_args=["--woof"],
                  verbose=args.verbose)
    lines = pair.out.splitlines()
    # The battle's setup traces each record by name: status, whether it can act.
    loaded = next((i for i, l in enumerate(lines) if l.endswith("| LOAD MONSTER")), len(lines))
    setup = [l for l in lines[loaded + 1:loaded + 11] if l.startswith("debug: ")]
    done = [l for l in lines if l.startswith("menu: ") and "Done*" in l]
    print(f"port: {len([l for l in lines if l.startswith('debug: ')])} trace lines; the "
          f"battle's setup {[l.split('| ')[-1] for l in setup]}; combat menus with Done*: {len(done)} "
          f"{done[:1]}")
    bad = known = 0
    with pair:
        pair.run()
        shots = sorted((pair.work / "shots").glob("*.png"))
        for shot in shots:
            count, best, area = pair.compare(shot.stem, quiet=True)
            if count and area == (272, 192, 279, 199):
                known += 1      # PRESS <ENTER>/<RETURN> TO CONTINUE.'s full stop
            elif count:
                bad += 1
            if count:
                print(f"  {shot.stem}: {count} pixels differ from eclplay's "
                      f"{pair.port_shots[best].name} in {area}")
        print(f"  {len(shots)} screens: {bad} differ, {known} only by the known full stop at "
              f"row 24 column 34; the original waits at {pair.box.where()}; DS:4b51 "
              f"{pair.box.ds_byte(0x4B51)}")
    if not done:
        print("  (no combat menu with Done* reached: more --steps)")
        bad += 1
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS / see above",
          f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

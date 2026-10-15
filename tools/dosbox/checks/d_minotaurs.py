#!/usr/bin/env python3
"""Check #57 (README, Checks against the original): "Try to flee the
minotaurs of ECL3 block 80: it should always fail (CHECKPARTY [7d1b]
stores nothing)."

The port writes game J: game A in ECL3 block 80 (eclplay --load --file 3,
var 0x4cca 0 while saving), camped and saved after the city's pages; then
patched: 0x4cca 1 (so that a rest encounter west of x 10 is the minotaurs'
881d), 0x4c0e 5 (the patrol's countdown: the port's location vector at
Begin, see d_firstblock.py, counts it down without a RANDOM) and the square
2, 1 (square byte 0). Both load J, Encamp, Rest an hour: "Your repose is
suddenly interrupted!", "THE MINOTAURS PREPARE TO ATTACK." DEFEND
YOURSELVES / FLEE: Flee. For each seed the screens and memory
(d_lib.compare_state) at the menu and after Flee against the port's, and
the page after Flee read: "NOT EVERYONE IN YOUR PARTY IS FAST ENOUGH TO GET
AWAY!" in both, whatever the dice. (The page's PRESS <ENTER>/<RETURN> TO
CONTINUE has the port's known full stop.)
"""
import argparse
import sys

import checklib
import d_lib


def save_80(work):
    saves = d_lib.make_save("J", work / "save", keys=r"\r\r\r\rE", block=80,
                            args=["--file", "3", "--set", "4cca=0"])
    d_lib.patch_game(saves, "J", square=[2, 1, 0, 0, 0], words={0x4CCA: 1, 0x4C0E: 5})
    return saves


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, action="append")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-minotaurs")
    saves = save_80(work)
    v = checklib.Verdict()
    steps = [("menu", d_lib.load("J") + "ERHAR"), ("fled", "F")]
    for seed in args.seed or [7, 1, 2]:
        out = work / f"seed{seed}"
        port = d_lib.Port(out / "eclplay", d_lib.marked(steps), seed, saves=[saves])
        with d_lib.box(out, seed, verbose=args.verbose, saves=[saves]) as b:
            for name, keys in steps:
                shown = d_lib.step(b, keys, name, out)
                d_lib.screen(v, f"seed {seed}: screen {name}", shown, port, name, out)
                d_lib.compare_state(v, b, port, name)
        text = d_lib.screen_text(shown, range(17, 24))
        said = [line[7:] for line in port.lines if "FAST ENOUGH" in line]
        print(f"seed {seed}: port {said}; original {text}")
        v.check(any("NOT EVERYONE" in t for t in text.values()),
                f"#57 seed {seed}: the flight fails in the original (\"NOT EVERYONE ...\")")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

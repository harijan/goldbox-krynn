#!/usr/bin/env python3
"""Check #78 (DEVELOPERS.md, Checks against the original): "Fight the red dragons
of ECL3 block 97 with a party of low levels: each character of level 3 or
less should flash and be "terrified" as the battle begins, and those of
higher levels "afraid" unless they save."

The port writes game J: game A in ECL3 block 97 (eclplay --load, --file 3,
vars 0x4be6-0x4be8 1 so that its wall sets load, 0x4ce4 0x80 so that the
hostages' dragons stay away while the save is made), camped and saved;
then the saved game's square is patched to 10, 5 (square byte 0x8b, type
11: "HERE LIES AN OLD AND BATTERED DRAGON", 990f) and 0x4ce4 to 0. Begin
Adventuring runs only the block's load vector (d_firstblock.py), so the
keys add Look (`L`: the location vector runs, ten minutes pass). Then ATTACK: one red
dragon of MON3 record 22, whose fear (0x52) acts at setup. Compared: the
party's effects in the original (0x6f terrified, 0x77 afraid) against
eclplay's "is terrified" lines, and the screens and memory
(d_lib.compare_state) at Look and at the first menu against the port's.
Seed 7. Every character of game A is of level 3 or less.
"""
import argparse
import sys

import checklib
import d_lib

STEPS = [("look", d_lib.load("J") + "L"), ("menu", r"\rA")]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-dragonfear")
    saves = d_lib.save_97(work)
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed, saves=[saves])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        for name, keys in STEPS:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            o = d_lib.compare_state(v, b, port, name)
    eff = {f[1].strip(): [e[0] for e in f[7]] for f in o["fighters"][:6]}
    print("original's effects at the first menu:", eff)
    terrified = sorted(n for n, e in eff.items() if 0x6F in e)
    afraid = sorted(n for n, e in eff.items() if 0x77 in e)
    lines = port.lines
    print("port: terrified", sorted(a[7:].strip() for a, b_ in zip(lines, lines[1:])
                                    if b_ == "print: is terrified"))
    v.check(terrified == ["GARIN", "ISTAN HORBIN", "KAL", "SIR STRONGSWORD", "SIRRION"] and
            not afraid, "#78 the original: the old red dragon's fear terrifies (0x6f) the five "
            "of level 3 or less, MOLLY (0x5c) not, none afraid (0x77)",
            f"terrified {terrified}, afraid {afraid}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

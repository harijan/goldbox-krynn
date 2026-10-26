#!/usr/bin/env python3
"""Check #77 (docs/checks.md, Checks against the original): "Fight in the rooms of
Neraka (ECL2 block 64, squares at x 11-15 of y 8-9 and x 8-10 of y 12-13):
tables and chairs should stand only in the room's own block, never in the
block to its east or below it."

The port writes game J: game A in ECL2 block 64 (eclplay --load --file 2),
saved after the arrival page, then patched: the square 12, 8 (square byte
0xc0: a room, 0x40, of type 0, the random encounters of 8456) and var 0x4c07
(steps counted) 9. Both roll after Look (Begin Adventuring runs only the
first block's load vector, d_firstblock.py): seed 9 (found with eclplay) gives
"MONSTROUS FORCES SPOT YOU AND ATTACK." (4 giant rats, 5 giant
centipedes). The original's battlefield (DS:6a2e, its tables 0x1a and
chairs 0x1b among the terrain) and combatants' cells, the rest of the
memory (d_lib.compare_state) and the screens at Look and at the first menu
against the port's.
"""
import argparse
import sys

import checklib
import d_lib

STEPS = [("look", d_lib.load("J") + "L"), ("menu", r"\r")]


def save_64(work):
    saves = d_lib.make_save("J", work / "save", keys=r"\rE", block=64, args=["--file", "2"])
    d_lib.patch_game(saves, "J", square=[12, 8, 0, 0, 0xC0], words={0x4C07: 9})
    return saves


def field(cs, cells):
    return [r.hex() for r in cells], [(c["n"], c["name"], c["x"], c["y"], c["size"]) for c in cs]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=9)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-neraka")
    saves = save_64(work)
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed, saves=[saves])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        for name, keys in STEPS:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            d_lib.compare_state(v, b, port, name)
        cells = d_lib.battle_map(b)
        d_lib.value(v, "#77 the battlefield and every combatant's cell at the first menu",
                    field(d_lib.combatants(b), cells), port, "menu",
                    lambda s: field(d_lib.port_combatants(s), s["cells"]))
    furniture = [(x, y) for y, row in enumerate(cells) for x, t in enumerate(row)
                 if t in (0x1A, 0x1B)]
    print("original's tables and chairs:", furniture)
    v.check(len(furniture) == 20, "#77 the original: 20 tables and chairs on the battlefield, "
            "as batch D saw them", f"{len(furniture)}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #77 (README, Checks against the original): "Fight in the rooms of
Neraka (ECL2 block 64, squares at x 11-15 of y 8-9 and x 8-10 of y 12-13):
tables and chairs should stand only in the room's own block, never in the
block to its east or below it."

The port writes game J: game A in ECL2 block 64 (eclplay --load --file 2),
saved after the arrival page, then patched: the square 12, 8 (square byte
0xc0: a room, 0x40, of type 0, the random encounters of 8456) and var 0x4c07
(steps counted) 9. The port rolls at Begin (its first-block location
vector), the original after Look: seed 9 (found with eclplay) gives
"MONSTROUS FORCES SPOT YOU AND ATTACK." (4 giant rats, 5 giant
centipedes). The original's battlefield (DS:6a2e, its tables 0x1a and
chairs 0x1b among the terrain) and combatants' cells at the first menu
against eclplay's --combat-map, and the screen.
"""
import argparse
import struct
import sys

import d_lib
from d_gate import last_battle


def save_64(work):
    saves = d_lib.make_save("J", work / "save", keys=r"\rE", block=64, args=["--file", "2"])
    path = saves / "SAVGAMJ.DAT"
    data = bytearray(path.read_bytes())
    struct.pack_into("<H", data, 1 + (0x4C07 - 0x4B00) * 2, 9)
    data[0x1401:0x1406] = bytes([12, 8, 0, 0, 0xC0])
    path.write_bytes(bytes(data))
    return saves


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=9)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("neraka", args.workdir)
    saves = save_64(work)
    cmap = work / "combat-map.txt"
    if cmap.exists():
        cmap.unlink()
    lines, shots = d_lib.port(d_lib.load("J") + r"\r", work / "port", args.seed, saves=[saves],
                              args=["--combat-map", cmap])
    rows, cells, facings = last_battle(cmap)
    furniture = sum(r.count("1a") + r.count("1b") for r in
                    [[row[i:i + 2] for i in range(0, 100, 2)] for row in rows])
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        res = d_lib.run_steps(b, [("look", d_lib.load("J") + "L"), ("menu", r"\r")], shots,
                              work)
        mine = [r.hex() for r in d_lib.battle_map(b)]
        mycells = {c["n"]: (c["x"], c["y"]) for c in d_lib.combatants(b)}
    found = sum(1 for r in mine for i in range(0, 100, 2) if r[i:i + 2] in ("1a", "1b"))
    print(f"port: {furniture} tables and chairs on the battlefield; original: {found}")
    print("map rows equal:", sum(a == b_ for a, b_ in zip(mine, rows)), "of", len(rows))
    ok = mine == rows and {n: c for n, c in mycells.items() if n in cells} == cells and \
        res["menu"][0] == 0
    status = d_lib.verdict(ok, "#77 a fight in Neraka's room at 12,8: the original's "
                           "battlefield, its tables and chairs among it, and the combatants' "
                           "cells equal eclplay's --combat-map; the first menu's screen equal")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

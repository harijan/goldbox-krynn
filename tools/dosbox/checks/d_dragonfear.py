#!/usr/bin/env python3
"""Check #78 (README, Checks against the original): "Fight the red dragons
of ECL3 block 97 with a party of low levels: each character of level 3 or
less should flash and be "terrified" as the battle begins, and those of
higher levels "afraid" unless they save."

The port writes game J: game A in ECL3 block 97 (eclplay --load, --file 3,
vars 0x4be6-0x4be8 1 so that its wall sets load, 0x4ce4 0x80 so that the
hostages' dragons stay away while the save is made), camped and saved;
then the saved game's square is patched to 10, 5 (square byte 0x8b, type
11: "HERE LIES AN OLD AND BATTERED DRAGON", 990f) and 0x4ce4 to 0. The
port, at Begin Adventuring, runs the block's location vector at once (see
d_firstblock.py); the original does not, so its keys add Look (`L`: the
location vector runs once more, ten minutes pass). Then ATTACK: one red
dragon of MON3 record 22, whose fear (0x52) acts at setup. Compared: the
party's effects in the original (0x6f terrified, 0x77 afraid) against
eclplay's "is terrified" lines, and the first menu's screen. Seed 7. Every
character of game A is of level 3 or less.
"""
import argparse
import struct
import sys

import d_lib


def save_97(work):
    saves = d_lib.make_save("J", work / "save", keys="E", block=97, args=[
        "--file", "3", "--set", "4be7=1", "--set", "4be8=1", "--set", "4be6=1",
        "--set", "4ce4=80"])
    path = saves / "SAVGAMJ.DAT"
    data = bytearray(path.read_bytes())
    struct.pack_into("<H", data, 1 + (0x4CE4 - 0x4B00) * 2, 0)
    data[0x1401:0x1406] = bytes([10, 5, 0, 0, 0x8B])  # x, y, facing, wall, square
    path.write_bytes(bytes(data))
    return saves


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("dragonfear", args.workdir)
    saves = save_97(work)
    lines, shots = d_lib.port(d_lib.load("J") + r"\rA", work / "port", args.seed,
                              saves=[saves])
    terrified = sorted(a[7:].strip() for a, b in zip(lines, lines[1:])
                       if b == "print: is terrified")
    afraid = sorted(a[7:].strip() for a, b in zip(lines, lines[1:]) if b == "print: is afraid")
    print("port: terrified", terrified, "afraid", afraid,
          [l for l in lines if l.startswith("round: 1")])
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        res = d_lib.run_steps(b, [("look", d_lib.load("J") + "L"), ("menu", r"\rA")],
                              shots, work)
        cs = d_lib.combatants(b)
        eff = {c["name"].strip(): [e[0] for e in d_lib.effects(b, c["rec"])] for c in cs[:6]}
        levels = {c["name"].strip(): c["raw"][0xD6] for c in cs[:6]}
    o_terrified = sorted(n for n, e in eff.items() if 0x6F in e)
    o_afraid = sorted(n for n, e in eff.items() if 0x77 in e)
    print("original: levels (+0xd6)", levels, "; 0x6f on", o_terrified, "; 0x77 on", o_afraid)
    print("original's effects:", eff)
    ok = o_terrified == terrified and o_afraid == afraid and res["menu"][0] == 0
    status = d_lib.verdict(ok, f"#78 the old red dragon's fear: terrified {o_terrified} in "
                           "both (MOLLY, with 0x5c, untouched); the first menu's screen equal")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

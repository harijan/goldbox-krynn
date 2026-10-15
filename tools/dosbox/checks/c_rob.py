#!/usr/bin/env python3
"""ROB's arithmetic (README, Checks against the original, #56): "Have a
character with 530 of a coin robbed of 10% (`ROB 0 10 0`, ECL1 block 17):
it should keep 476" (Turbo Pascal's Real multiply's short way, where exact
arithmetic gives 477).

The port writes a saved game on the overland map at 2,2, a step north-east
of the first outpost (game A, the overland vars of c_throtl.py); in the file
var 0x4c27 is set to 1 (block 17's load vector then has each knight give
his tithe, 9cfc's ROB 0 10 0) and SIR STRONGSWORD, the knight, is given
530 copper (+0xed) and 530 steel (+0xf3). Both games load it, step south-
west onto the outpost and enter. The original's coins are read in memory at
the tithe's message; the port's from the game it saves at the inn after.
"""
import argparse
import struct
import sys

import c_places as cp

OVERLAND = ["--load", cp.SAVE_A, "--play", "--set", "4bf2=11", "--set", "4bc3=1",
            "--set", "4bc4=3", "--set", "4c2d=1"]


def coins(box):
    return [(cp.name_of(raw), cp.money(raw)) for _, raw in cp.party_records(box)][:1]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("rob", args.workdir)
    saves, _ = cp.port_save(work / "save", "nm9EESC", OVERLAND, block=16)
    cp.edit_save(saves, "C", var={0x4C27: 1})
    path = saves / "CHRDATC1.SAV"
    rec = bytearray(path.read_bytes())
    struct.pack_into("<H", rec, 0xED, 530)
    struct.pack_into("<H", rec, 0xF3, 530)
    path.write_bytes(bytes(rec))
    c = cp.Compare(work / "run", saves, [
        ("loaded", cp.load_keys("C"), coins),
        ("near", "m1"),                    # YOU ARE NEAR AN OUTPOST. DO YOU ENTER?
        ("tithe", "y", coins),             # SIR STRONGSWORD GIVES HIS TITHE ...
    ], seed=1, verbose=args.verbose)
    status = c.run()
    print("original, SIR STRONGSWORD's coins before:", c.probes["loaded"])
    print("original, after the tithe:", c.probes["tithe"])
    out, psaves, _ = cp.cokbox.eclplay(cp.load_keys("C") + r"m1y\rI\rSD", work / "port-save",
                                       saves=[saves], shots=False, args=["--seed", "1"])
    raw = (psaves / "CHRDATD1.SAV").read_bytes()
    print("port, saved after the tithe:", cp.name_of(raw), cp.money(raw))
    print([l for l in out.splitlines() if "robbed" in l or "TITHE" in l])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

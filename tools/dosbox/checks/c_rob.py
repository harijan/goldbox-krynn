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
west onto the outpost and enter. SIR STRONGSWORD's coins at the tithe's
message, read in the original's memory, are compared with the port's at
the same key read (and the original's must be 476: the README's value).
"""
import argparse
import struct
import sys

import c_places as cp
import checklib


def coins(state):
    knight = state.members[0]
    return {f"{knight.name}'s coins (silver, copper, bronze, platinum, steel, gems, jewelry)":
            cp.money(knight.record)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-rob")
    v = checklib.Verdict()
    saves, _ = cp.port_save(work / "save", "nm9EESC", cp.OVERLAND, block=16)
    cp.edit_save(saves, "C", var={0x4C27: 1})
    cp.edit_record(saves / "CHRDATC1.SAV", lambda r: (struct.pack_into("<H", r, 0xED, 530),
                                                      struct.pack_into("<H", r, 0xF3, 530)))
    c = cp.Compare(v, work / "run", saves, [
        ("loaded", cp.load_keys("C"), coins),
        ("near", "m1"),                    # YOU ARE NEAR AN OUTPOST. DO YOU ENTER?
        ("tithe", "y", coins),             # SIR STRONGSWORD GIVES HIS TITHE ...
    ], seed=1, verbose=args.verbose).run()
    (before,), (after,) = c.probes["loaded"].values(), c.probes["tithe"].values()
    v.check(before[1] == before[4] == 530 and after[1] == after[4] == 476,
            "the original's copper and steel: 530 before the tithe, 476 after",
            f"{before[1]}, {before[4]} -> {after[1]}, {after[4]}")
    print("port:", [l for l in c.port_text.splitlines() if "TITHE" in l])
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

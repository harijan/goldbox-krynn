#!/usr/bin/env python3
"""Not a numbered check (found while setting up #90): which of the first
block's vectors run at Begin Adventuring.

The original's adventure loop (2fd3:3c28) runs only the first block's load
vector (DS:4b41, the call at 3cc7) before its first command; the after-move
and location vectors (DS:4b39, 4b3b) run after a command. The port's
enter_block (src/adventure.c) runs all three for the first block.

The port writes game I: game A in ECL1 block 16 (the overland map) at 8, 12
with var 0x4c06 (steps in the wild) 8, camped and saved there (the block's
entry counts one: 9 is saved). Both load I, seed 10, Begin Adventuring, then
step east. In the original the ECL variables are found in memory (the
saved game's block of 0x4b00-0x4eff) and read with RandSeed: at the map,
and after the step. eclplay --trace shows which vectors and RANDOMs run.
"""
import argparse
import re
import struct
import sys

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("firstblock", args.workdir)
    saves = d_lib.make_save("I", work / "save", keys="e", block=16, args=[
        "--set", "4bf2=10", "--set", "4bc3=8", "--set", "4bc4=c", "--set", "4c2d=1",
        "--set", "4c06=8"])
    saved = (saves / "SAVGAMI.DAT").read_bytes()[1:1 + 0x800]
    lines, _ = d_lib.port(d_lib.load("I") + "m6", work / "port", 10, saves=[saves],
                         shots=False, args=["--trace"])
    k = lines.index("choice: Begin Adventuring")
    first_cmd = next(i for i in range(k, len(lines)) if lines[i].startswith("menu: Move"))
    at_begin = [l.strip() for l in lines[k:first_cmd] if re.match(r"\s+[0-9a-f]{4} ", l)]
    print("port, before the first command:", len(at_begin), "ECL instructions,",
          sum(1 for l in at_begin if l.endswith("RANDOM")), "RANDOM; vectors entered at",
          [l.split()[0] for l in at_begin if l.split()[0] in ("8014", "8155", "82be")])
    met = any("HOSTILE" in l for l in lines)
    with d_lib.box(work, 10, verbose=args.verbose, saves=[saves]) as b:
        b.keys(d_lib.load("I"))
        b.wait_input(settle=0.5)
        mem = b.read(0, 0xA0000)
        base = mem.find(saved[0x180:0x1a0]) - 0x180
        def var(m, v):
            return struct.unpack_from("<H", m, base + (v - 0x4b00) * 2)[0]
        seed0, c0 = b.ds_dword(0x43AE), var(mem, 0x4C06)
        b.keys("m6")
        b.wait_input(settle=0.5)
        mem = b.read(0, 0xA0000)
        seed1, c1 = b.ds_dword(0x43AE), var(mem, 0x4C06)
        text = d_lib.screen_text(b.screenshot(work / "after-step.png"), [24])
    print(f"original: at the map RandSeed {seed0:#x}, 0x4c06 {c0}; after the step "
          f"{seed1:#x}, 0x4c06 {c1}; row 24 {text}")
    print("port: the step meets hostile creatures" if met else "port: no encounter")
    differs = seed0 == 10 and c0 == 9 and any(l.endswith("RANDOM") for l in at_begin)
    status = d_lib.verdict(not differs, "the first block at Begin Adventuring: the original "
                           "runs no vector but the load vector (RandSeed and 0x4c06 "
                           "untouched); the port runs the after-move and location vectors "
                           "too (a RANDOM and a step counted)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

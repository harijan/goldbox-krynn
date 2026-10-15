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
and after the step; against the port's at the same points, and the screens
too. The port as it is differs (an extra step counted, a RANDOM drawn: the
step meets hostile creatures); the port with the known fixes runs only the
load vector, and the original against it decides (known: resume). eclplay
--trace shows which vectors and RANDOMs run.
"""
import argparse
import re
import struct
import sys

import checklib
import d_lib

STEPS = [("map", d_lib.load("I")), ("step", "m6")]


def steps_counted(vars_):
    return struct.unpack_from("<H", vars_, (0x4C06 - 0x4B00) * 2)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-firstblock")
    saves = d_lib.make_save("I", work / "save", keys="e", block=16, args=[
        "--set", "4bf2=10", "--set", "4bc3=8", "--set", "4bc4=c", "--set", "4c2d=1",
        "--set", "4c06=8"])
    saved = (saves / "SAVGAMI.DAT").read_bytes()[1:1 + 0x800]
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), 10, saves=[saves],
                      log_args=["--trace"])
    lines = port.lines
    k = lines.index("choice: Begin Adventuring")
    first_cmd = next(i for i in range(k, len(lines)) if lines[i].startswith("menu: Move"))
    at_begin = [line.strip() for line in lines[k:first_cmd] if re.match(r"\s+[0-9a-f]{4} ", line)]
    print("port, before the first command:", len(at_begin), "ECL instructions,",
          sum(1 for line in at_begin if line.endswith("RANDOM")), "RANDOM; vectors entered at",
          [line.split()[0] for line in at_begin if line.split()[0] in ("8014", "8155", "82be")])
    v = checklib.Verdict()
    seen = {}
    with d_lib.box(work, 10, verbose=args.verbose, saves=[saves]) as b:
        base = None
        for name, keys in STEPS:
            shown = d_lib.step(b, keys, name, work, settle=0.5)
            mem = b.read(0, 0xA0000)
            if base is None:
                base = mem.find(saved[0x180:0x1A0]) - 0x180
                if base < 0:
                    raise checklib.cokbox.BoxError("the saved game's variables not found")
            seen[name] = (b.ds_dword(checklib.cokbox.RANDSEED),
                          steps_counted(mem[base:base + 0x800]))
            d_lib.screen(v, f"screen {name}", shown, port, name, work, known="resume")
            d_lib.value(v, f"RandSeed and 0x4c06 (steps counted) at {name}", seen[name], port,
                        name, lambda s: (s["seed"], steps_counted(s["vars"])), "resume")
            d_lib.compare_state(v, b, port, name, known="resume")
    print(f"original: RandSeed and 0x4c06 at the map {seen['map']}, after the step "
          f"{seen['step']}")
    v.check(seen["map"] == (10, 9) and seen["step"][1] == 10,
            "the original at Begin: only the load vector ran (RandSeed 10 and 0x4c06 9 as "
            "saved); the step counts one (10)", f"{seen}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

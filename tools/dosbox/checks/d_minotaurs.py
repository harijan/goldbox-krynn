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
YOURSELVES / FLEE: Flee. For each seed the screen after it is compared
with eclplay's and read: "NOT EVERYONE IN YOUR PARTY IS FAST ENOUGH TO GET
AWAY!" in both, whatever the dice.
"""
import argparse
import struct
import sys

import d_lib


def save_80(work):
    saves = d_lib.make_save("J", work / "save", keys=r"\r\r\r\rE", block=80,
                            args=["--file", "3", "--set", "4cca=0"])
    path = saves / "SAVGAMJ.DAT"
    data = bytearray(path.read_bytes())
    struct.pack_into("<H", data, 1 + (0x4CCA - 0x4B00) * 2, 1)
    struct.pack_into("<H", data, 1 + (0x4C0E - 0x4B00) * 2, 5)
    data[0x1401:0x1406] = bytes([2, 1, 0, 0, 0])
    path.write_bytes(bytes(data))
    return saves


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, action="append")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("minotaurs", args.workdir)
    saves = save_80(work)
    status = 0
    for seed in args.seed or [7, 1, 2]:
        out = work / f"seed{seed}"
        keys = d_lib.load("J") + "ERHAR"
        lines, shots = d_lib.port(keys + "F", out / "port", seed, saves=[saves])
        said = [l[7:] for l in lines if "FAST ENOUGH" in l or "AVOID THEM" in l]
        with d_lib.box(out, seed, verbose=args.verbose, saves=[saves]) as b:
            res = d_lib.run_steps(b, [("menu", keys), ("fled", "F")], shots, out)
            _, area, _ = d_lib.best(res["fled"][1], shots)
        text = d_lib.screen_text(res["fled"][1], range(17, 24))
        print(f"seed {seed}: port {said}; original {text}")
        # The page's single-item menu: the port's full stop after CONTINUE (row
        # 24, column 34), the bug load_game_screen.py found, is the only
        # difference allowed.
        stop = area is None or area == (272, 192, 279, 199)
        status |= d_lib.verdict(res["menu"][0] == 0 and stop and
                                any("NOT EVERYONE" in t for t in text.values()),
                                f"#57 seed {seed}: the flight fails in both (screens equal but "
                                "the port's known full stop on row 24)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

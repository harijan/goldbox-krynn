#!/usr/bin/env python3
"""Check #75 (README, Checks against the original): "With the second of
four characters unconscious, fight anything: its body should lie where the
second would stand, right of the first, and the third and fourth stand a
rank back."

The port writes game J: game A in the outpost inn's camp, then patched to a
party of four (the saved game's party size, byte 0x1414, 6 -> 4) with the
second, ISTAN HORBIN, unconscious (.SAV +0x188 status 4, +0x189 can-act 0,
+0x197 hit points 0). Both load J and meet the caravan's baaz (seed 7, the
first step north-east; open ground) and, in a second run, Throtl's guards
(the gate route of d_gate.py, a 3D street, with Helm). At the first menu the
original's battlefield (DS:6a2e), the bodies (DS:69ee + 7k: x, y, terrain
under) and the combatants' cells are compared with eclplay's --combat-map.
"""
import argparse
import re
import sys

import d_lib
from d_gate import last_battle


def save_four(work):
    saves = d_lib.make_save("J", work / "save")
    path = saves / "SAVGAMJ.DAT"
    data = bytearray(path.read_bytes())
    data[0x1414] = 4
    path.write_bytes(bytes(data))
    rec = saves / "CHRDATJ2.SAV"
    raw = bytearray(rec.read_bytes())
    raw[0x188], raw[0x189], raw[0x197] = 4, 0, 0
    rec.write_bytes(bytes(raw))
    return saves


def bodies(b):
    """The fallen (DS:69ee + 7k, k from 1, count DS:6a2d): the record (a far
    pointer), then x, y and the terrain under the body."""
    count = b.ds_byte(0x6A2D)
    return [tuple(b.ds_read(0x69EE + 7 * k + 4, 3)) for k in range(1, min(count, 8) + 1)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("ranks", args.workdir)
    saves = save_four(work)
    status = 0
    for name, keys, extra, box_args in [
            ("caravan", d_lib.load("J") + r"Lnm9\r", [], ""),
            ("gate", d_lib.load("J") + r"Lnm9\r\@x\r\rEN\r\rY\r\rLnm98yA", ["--helm"], "x Helm")]:
        out = work / name
        cmap = out / "combat-map.txt"
        out.mkdir(parents=True, exist_ok=True)
        if cmap.exists():
            cmap.unlink()
        lines, shots = d_lib.port(keys, out / "port", args.seed, saves=[saves],
                                  args=[*extra, "--combat-map", cmap])
        rows, cells, _ = last_battle(cmap)
        text = open(cmap).read().split("battle in block")[-1]
        port_bodies = re.findall(r"body at (\d+),(\d+) on ([0-9a-f]+)", text)
        print(f"{name}: port: cells {cells}; bodies {port_bodies}")
        with d_lib.box(out, args.seed, verbose=args.verbose, saves=[saves], args=box_args) as b:
            res = d_lib.run_steps(b, [("menu", keys)], shots, out)
            mine = [r.hex() for r in d_lib.battle_map(b)]
            cs = d_lib.combatants(b)
            mycells = {c["n"]: (c["x"], c["y"]) for c in cs if c["size"]}
            mybodies = bodies(b)
        print(f"{name}: original: cells {mycells}; bodies (x, y, terrain) {mybodies}; map "
              f"rows equal {sum(a == b_ for a, b_ in zip(mine, rows))} of {len(rows)}")
        same_bodies = [(int(x), int(y), int(t, 16)) for x, y, t in port_bodies] == mybodies
        status |= d_lib.verdict(mine == rows and same_bodies and res["menu"][0] == 0 and
                                {n: c for n, c in cells.items() if n != 2} ==
                                {n: c for n, c in mycells.items() if n != 2},
                                f"#75 {name}: four with the second unconscious: the "
                                "battlefield, ISTAN's body and the others' cells equal "
                                "eclplay's; the first menu's screen equal")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

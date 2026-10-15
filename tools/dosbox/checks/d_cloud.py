#!/usr/bin/env python3
"""Check #6 (README, Checks against the original), in part: "Clouds and
pools: cast Stinking Cloud with a wall among its four cells, ... over half
of another cloud; compare the tiles while it lasts and after "The air
clears a little..." (its caster's level in rounds), and who coughs or
chokes. ... Compare too that the tiles are not redrawn when a cloud
clears." (The fallen body under a cloud and the kapak's pool are not
reached here; the kapak's pool raised early is a port stop, batch E.)

The port writes game J: game A in the outpost inn's camp, GARIN's last four
spells patched to Magic Missile, Stinking Cloud, Lightning Bolt and Fear
(+0x54: 0f 22 33 54) and SIRRION's third Sleep to Stinking Cloud (+0x56).
Both (Helm, seed 7) take the gate route (d_gate.py) to Throtl's guards. In
round 1 SIRRION casts Stinking Cloud on 26, 11 (Manual: up 3, left 1), its
four cells taking in the wall of row 10; in round 2 GARIN casts one on 27,
11, half over the first; every other turn is Done/Quit, four rounds. The
screen at every turn's menu is compared with eclplay's, and the battle
map's cloud cells (terrain 0x1c, 0x1e) read in memory at each.
"""
import argparse
import re
import sys

import d_lib

ROUTE = d_lib.load("J") + r"Lnm9\r\@x\r\rEN\r\rY\r\rLnm98yA"
TURNS = r"DQDQC\rM8884TDQDQDQC\^\^\rM886TDQDQDQDQDQDQDQDQDQDQ"


def spells_save(work):
    saves = d_lib.make_save("J", work / "save")
    d_lib.patch_record(saves, "J", 3, 0x54, [0x0F, 0x22, 0x33, 0x54])
    d_lib.patch_record(saves, "J", 6, 0x56, [0x22])
    return saves


def clouds(b):
    m = d_lib.battle_map(b)
    return sorted((x, y, v) for y, row in enumerate(m) for x, v in enumerate(row)
                  if v in (0x1C, 0x1E))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("cloud", args.workdir)
    saves = spells_save(work)
    lines, shots = d_lib.port(ROUTE + TURNS, work / "port", args.seed, saves=[saves],
                              args=["--helm"])
    said = [l[7:] for l in lines if l.startswith("print: ") and re.search(
        r"cloud|cough|chokes|clears", l)]
    print("port:", said)
    steps = [("gate", ROUTE)] + [(f"t{i:02d}", k) for i, k in enumerate(
        re.findall(r"DQ|C\\r|C\\\^\\\^\\r|M\d+T", TURNS))]
    # A cast is its list key and its aim: one step.
    merged = []
    for name, k in steps:
        if merged and merged[-1][1].startswith("C") and k.startswith("M"):
            merged[-1] = (merged[-1][0], merged[-1][1] + k)
        else:
            merged.append((name, k))
    seen = []
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves], args="x Helm") as b:
        res = d_lib.run_steps(b, merged[:1], shots, work)
        for step in merged[1:]:
            res.update(d_lib.run_steps(b, [step], shots, work))
            seen.append((step[0], clouds(b)))
    for name, c in seen:
        print(f"original's cloud cells after {name}: {c}")
    ok = all(c == 0 for n, (c, _) in res.items() if n != "gate")
    status = d_lib.verdict(ok, f"#6 two Stinking Clouds by the wall, the second half over the "
                           f"first: every screen at the {len(merged) - 1} turns' menus, "
                           "through both clouds' clearing, equals eclplay's")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

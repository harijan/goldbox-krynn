#!/usr/bin/env python3
"""Check #6 (docs/checks.md, Checks against the original), in part: "Clouds and
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
screen at every turn's menu, the battle map's cloud cells (terrain 0x1c,
0x1e) and the rest of the memory (d_lib.compare_state: who coughs and
chokes is in the effects and hit points) against the port's at the same
point.
"""
import argparse
import re
import sys

import checklib
import d_lib

TURNS = r"DQDQC\rM8884TDQDQDQC\^\^\rM886TDQDQDQDQDQDQDQDQDQDQ"


def clouds(cells):
    """The cloud cells (x, y, terrain 0x1c or 0x1e) of the map's rows CELLS."""
    return sorted((x, y, v) for y, row in enumerate(cells) for x, v in enumerate(row)
                  if v in (0x1C, 0x1E))


def steps():
    """The gate, then a step for each turn (a cast is its list key and its aim)."""
    out = [("gate", d_lib.GATE_J)]
    for k in re.findall(r"DQ|C\\r|C\\\^\\\^\\r|M\d+T", TURNS):
        if out[-1][1].startswith("C") and k.startswith("M"):
            out[-1] = (out[-1][0], out[-1][1] + k)
        else:
            out.append((f"t{len(out) - 1:02d}", k))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-cloud")
    saves = d_lib.spells_save(work)
    plan = steps()
    port = d_lib.Port(work / "eclplay", d_lib.marked(plan), args.seed, saves=[saves],
                      args=["--helm"])
    v = checklib.Verdict()
    seen = []
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves], args="x Helm") as b:
        for name, keys in plan:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            d_lib.compare_state(v, b, port, name)
            seen.append((name, clouds(d_lib.battle_map(b))))
            d_lib.value(v, f"the cloud cells at {name}", seen[-1][1], port, name,
                        lambda s: clouds(s["cells"]))
    said = [line[7:] for line in port.lines if line.startswith("print: ") and re.search(
        r"cloud|cough|chokes|clears", line)]
    print("port:", said)
    for name, c in seen:
        print(f"original's cloud cells at {name}: {c}")
    cells = [{(x, y) for x, y, _ in c} for _, c in seen]
    first = next((c for c in cells if c), set())
    most = max(cells, key=len)
    v.check(first == {(26, 11), (27, 11), (26, 12), (27, 12)} and
            {(27, 11), (27, 12), (28, 12)} <= most and (28, 11) not in most and not cells[-1],
            "#6 the original: SIRRION's cloud on 26-27 x 11-12, GARIN's half over it with the "
            "wall at 28,11 left out, all cleared by the last turn")
    v.check(said.count("The air clears a little...") >= 2,
            "#6 the port says \"The air clears a little...\" as each cloud goes")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

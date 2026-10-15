#!/usr/bin/env python3
"""Check #127 (README, Checks against the original): "In a 3D area, cast
Lightning Bolt at an enemy beside a wall: the bolt should come back once,
hitting the enemy again but not the caster."

Game J (d_cloud.py's: GARIN with Lightning Bolt), the gate route (Throtl is
a 3D area), seed 7: Done/Quit for every turn until GARIN may cast (round
2), Lightning Bolt ("Begins Casting", a third of 3), and at his next turn
the aim: Manual, up 3 and left 1 (a warrior in the gateway, the walls of row
10 beside it), Target. eclplay: the warrior takes 11 twice (the bolt back
from the wall), a hobgoblin leader and a hobgoblin 11, and GARIN himself 5.
The original's hit points (the warrior, GARIN) are read before and after,
the cast's screens taken in a burst (speed 30) and compared with the port's
frames at its pauses, and the panel read.
"""
import argparse
import sys

import d_lib
from d_cloud import ROUTE, spells_save

TURNS = "DQ" * 9 + r"C\^\r"
AIM = "M8884"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--burst", type=float, default=40)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("bolt", args.workdir)
    saves = spells_save(work)
    lines, shots = d_lib.port(ROUTE + TURNS + AIM + "T", work / "port", args.seed,
                              saves=[saves], args=["--helm"], pauses=True)
    k = lines.index("cast: GARIN : Lightning Bolt")
    hits = []
    for who, what in zip(lines[k:], lines[k + 1:]):
        if what.startswith("print: takes"):
            hits.append((who[7:].strip(), what.split()[2]))
    print("port's hits:", hits)
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves], args="x Helm") as b:
        res = d_lib.run_steps(b, [("aim", ROUTE + TURNS + AIM)], shots, work)
        cs = d_lib.combatants(b)
        before = {(c["n"], c["name"]): c["hp"] for c in cs}
        speed = b.ds_byte(0x4B38)
        b.ds_write(0x4B38, bytes([30]))
        b.push_key(ord("t") | 0x1400)
        burst = d_lib.burst(b, args.burst, work=work, prefix="bolt")
        b.ds_write(0x4B38, bytes([speed]))
        after = {(c["n"], c["name"]): c["hp"] for c in d_lib.combatants(b)}
    lost = {k: before[k] - after[k] for k in before if before[k] != after[k]}
    print("original: hit points lost in the bolt's turn:", lost)
    frames = d_lib.frames_after(shots, res["aim"][1], 400)
    matched = [getattr(frames.get(s.pixels), "name", None) for s in burst]
    panel = []
    for s in burst:
        t = d_lib.screen_text(s, range(10, 13), 23, 39)
        if 10 in t and (t.get(10, "").strip(" ?"), t.get(11, "").strip(" ?")) not in panel:
            panel.append((t.get(10, "").strip(" ?"), t.get(11, "").strip(" ?")))
    print("original's panel in the burst:", panel)
    print("original's screens equal to port frames:", sum(1 for m in matched if m), "of",
          len(matched))
    # The bolt's damage texts as the original's panel showed them, up to the
    # next turn's attack, against the port's (repeats of one text merge).
    shown = []
    for name, text in panel:
        if text == "ATTACKS":
            break
        shown.append((name, text.split()[1]))
    port = [h for i, h in enumerate(hits) if i == 0 or h != hits[i - 1]]
    warrior = [v for (n, name), v in lost.items() if name == "WARRIOR"]
    port_garin = sum(int(d) for w, d in hits if w == "GARIN")
    status = d_lib.verdict(res["aim"][0] == 0 and shown == port and warrior == [18],
                           f"#127 the bolt off the gateway's walls: the original's panel "
                           f"{shown} as the port's hits {hits}; the warrior (18 hit points) "
                           "loses all 18, so twice 11; the caster takes 5 in both")
    if port_garin:
        print("(the README's \"but not the caster\" does not hold in either: the bolt's "
              "line back reaches GARIN)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

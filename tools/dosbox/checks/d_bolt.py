#!/usr/bin/env python3
"""Check #127 (DEVELOPERS.md, Checks against the original): "In a 3D area, cast
Lightning Bolt at an enemy beside a wall: the bolt should come back once,
hitting the enemy again but not the caster."

Game J (d_lib.spells_save: GARIN with Lightning Bolt), the gate route (Throtl is
a 3D area), seed 7: Done/Quit for every turn until GARIN may cast (round
2), Lightning Bolt ("Begins Casting", a third of 3), and at his next turn
the aim: Manual, up 3 and left 1 (a warrior in the gateway, the walls of row
10 beside it), Target. eclplay: the warrior takes 11 twice (the bolt back
from the wall), a hobgoblin leader and a hobgoblin 11, and GARIN himself 5.
The memory (d_lib.compare_state: every combatant's hit points among it)
and the screen at the aim and after the cast against the port's; the
cast's screens taken in a burst (speed 30) against the port's frames in
order, and the panel read: its damage texts against the port's log.
"""
import argparse
import sys

import checklib
import d_lib

TURNS = "DQ" * 9 + r"C\^\r"
AIM = "M8884"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--burst", type=float, default=60)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-bolt")
    saves = d_lib.spells_save(work)
    steps = [("aim", d_lib.GATE_J + TURNS + AIM), ("after", "T")]
    port = d_lib.Port(work / "eclplay", d_lib.marked(steps), args.seed, saves=[saves],
                      args=["--helm"])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves], args="x Helm") as b:
        shown = d_lib.step(b, steps[0][1], "aim", work)
        d_lib.screen(v, "screen aim", shown, port, "aim", work)
        before = d_lib.compare_state(v, b, port, "aim")
        burst, _, _ = d_lib.burst_step(v, b, port, "aim", "after", ord("t") | 0x1400, work,
                                       "bolt", args.burst)
        after = d_lib.compare_state(v, b, port, "after")
    hp = [{(f[0], f[1]): f[5] for f in st["fighters"]} for st in (before, after)]
    lost = {k: hp[0][k] - hp[1][k] for k in hp[0] if hp[0][k] != hp[1][k]}
    print("original: hit points lost in the bolt's turn:", lost)
    lines = port.lines
    k = lines.index("cast: GARIN : Lightning Bolt") if "cast: GARIN : Lightning Bolt" in lines \
        else len(lines)
    hits = [(who[7:].strip(), what.split()[2]) for who, what in zip(lines[k:], lines[k + 1:])
            if what.startswith("print: takes")]
    print("port's hits:", hits)
    # The bolt's damage texts as the original's panel showed them, up to the
    # next turn's attack, against the port's (repeats of one text merge).
    shown = []
    for s in burst:
        t = d_lib.screen_text(s, range(10, 12), 23, 39)
        pair = (t.get(10, "").strip(" ?"), t.get(11, "").strip(" ?"))
        if pair[0] and (not shown or shown[-1] != pair):
            shown.append(pair)
    said = []
    for name, text in shown:
        if text == "ATTACKS":
            break
        if text.startswith("TAKES"):
            said.append((name, text.split()[1]))
    merged = [h for i, h in enumerate(hits) if i == 0 or h != hits[i - 1]]
    print("original's panel in the burst:", shown)
    v.check(said == merged and said, "#127 the original's panel shows the bolt's damages as "
            "the port's log has them", f"original {said}, port {merged}")
    # The burst runs to the next wait: the monsters' attacks that follow are
    # in the hit points too; the bolt's own damage to GARIN is the panel's.
    warrior = [d for (n, name), d in lost.items() if name == "WARRIOR"]
    v.check(warrior == [18] and ("GARIN", "5") in said,
            "#127 the original: the warrior (18 hit points) loses all 18, twice 11: the bolt "
            "came back off the wall; GARIN, the caster, loses 5 (\"but not the caster\" does "
            "not hold, in either)", f"{lost}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

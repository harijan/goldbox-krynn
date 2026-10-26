#!/usr/bin/env python3
"""Checks #92 and #17 (docs/checks.md, Checks against the original): "Step away from
a hobgoblin beside a character: it should attack once, (from behind)." and
"Escape in Move after a step that drew an attack of opportunity: the
combatant goes back with the wounds kept."

Game A, the caravan's baaz (seed 7). Four turns end with Done Quit; at SIR
STRONGSWORD's turn two baaz stand beside him; Move, a step south-west (1):
each baaz attacks once "(from behind)" (one hits for 3); then Escape. The
screens at his menu, after the step and after Escape, and the memory there
(d_lib.compare_state: his cell DS:623f and hit points among it) and his
movement left (combat record +0x06), against the port's at the same point.
"""
import argparse
import sys

import checklib
import d_lib

STEPS = [("sir", d_lib.LOAD_A + d_lib.TO_CARAVAN + "DQ" * 4), ("step", "M1"), ("escape", r"\e")]


def movement(cs):
    return d_lib.by_name(cs, "SIR STRONGSWORD")[0]["combat"][6]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-move")
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed)
    v = checklib.Verdict()
    seen = {}
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        for name, keys in STEPS:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            o = d_lib.compare_state(v, b, port, name)
            sir = [f for f in o["fighters"] if f[1] == "SIR STRONGSWORD"][0]
            seen[name] = ((sir[2], sir[3]), sir[5], movement(d_lib.combatants(b)))
            d_lib.value(v, f"SIR's movement left (+0x06) at {name}", seen[name][2], port, name,
                        lambda s: movement(d_lib.port_combatants(s)))
            print(f"  original at {name}: SIR's cell, hit points, movement left {seen[name]}")
    i = port.lines.index("turn: SIR STRONGSWORD (initiative 4)")
    attacks = [line for line in port.lines[i:] if line.startswith("attack:")]
    print("port, SIR's step:", attacks)
    v.check(len(attacks) == 2 and all("(from behind)" in a for a in attacks),
            "#92 the port: each of the two baaz beside him attacks once \"(from behind)\"")
    v.check(seen["step"][1] == seen["sir"][1] - 3 and seen["step"][0] != seen["sir"][0],
            "#92 the original: the step taken and 3 points of damage (one baaz hits)")
    v.check(seen["escape"][0] == seen["sir"][0] and seen["escape"][2] == seen["sir"][2] and
            seen["escape"][1] == seen["step"][1],
            "#17 Escape in the original: back on his cell with his movement, the damage kept")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

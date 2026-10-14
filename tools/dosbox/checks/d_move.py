#!/usr/bin/env python3
"""Checks #92 and #17 (README, Checks against the original): "Step away from
a hobgoblin beside a character: it should attack once, (from behind)." and
"Escape in Move after a step that drew an attack of opportunity: the
combatant goes back with the wounds kept."

Game A, the caravan's baaz (seed 7). Four turns end with Done Quit; at SIR
STRONGSWORD's turn two baaz stand beside him; Move, a step south-west (1):
each baaz attacks once "(from behind)" (one hits for 3); then Escape. The
screens at his menu, after the step and after Escape are compared with
eclplay's; in the original's memory his cell (DS:623f), hit points and
movement left (combat record +0x06).
"""
import argparse
import sys

import d_lib


def state(b, name):
    c = d_lib.by_name(d_lib.combatants(b), name)[0]
    cr = d_lib.combat_record(b, c["rec"])
    return (c["x"], c["y"]), c["hp"], cr[6]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("move", args.workdir)
    steps = [("sir", d_lib.LOAD_A + d_lib.TO_CARAVAN + "DQ" * 4), ("step", "M1"),
             ("escape", r"\e")]
    lines, shots = d_lib.port("".join(k for _, k in steps), work / "port", args.seed)
    i = lines.index("turn: SIR STRONGSWORD (initiative 4)")
    attacks = [l for l in lines[i:] if l.startswith("attack:")]
    print("port, SIR's step:", attacks)
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        res = {}
        seen = []
        for step in steps:
            res.update(d_lib.run_steps(b, [step], shots, work))
            seen.append(state(b, "SIR STRONGSWORD"))
            print(f"original after {step[0]}: cell, hit points, movement left", seen[-1])
    once = len(attacks) == 2 and all("(from behind)" in a for a in attacks)
    status = d_lib.verdict(once and res["step"][0] == 0 and seen[1][1] == seen[0][1] - 3,
                           "#92 the step away draws one attack \"(from behind)\" from each "
                           "baaz beside him (screen after the step equal; 3 damage in "
                           "both)")
    status |= d_lib.verdict(res["escape"][0] == 0 and seen[2][0] == seen[0][0]
                            and seen[2][1] == seen[1][1] and seen[2][2] == seen[0][2],
                            "#17 Escape puts him back on his cell with his movement, the "
                            "3 points of damage kept (screen equal)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

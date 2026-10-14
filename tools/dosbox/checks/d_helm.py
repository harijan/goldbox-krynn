#!/usr/bin/env python3
"""Checks #82 and #81 (README, Checks against the original): "Start the game
with a second argument of Helm (START x Helm) and press Alt-X at a
character's turn in combat: "The Gods intervene!", every monster dead, the
round ending." and "Win a battle that the party can still fight: "Continue
Battle:" should ask at the round's end; Yes should go on with another round
and ask again."

Game A from the start menu, Solace's pages, the caravan's four baaz (the
first step north-east of the outpost); at ISTAN HORBIN's turn, the first,
Alt-X. The original runs with `x Helm`, eclplay with --helm, both seed 7.
Compared: the screen at "Continue Battle:", in the second round after Yes
(KAL's menu), at "Continue Battle:" again after the six party members'
Done Quit, and at the results after No; in the original's memory the baaz'
status and footprint.
"""
import argparse
import sys

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("helm", args.workdir)
    steps = [("turn", d_lib.LOAD_A + d_lib.TO_CARAVAN), ("gods", r"\@x"),
             ("round2", "Y"), ("again", "DQ" * 6), ("results", "N")]
    lines, shots = d_lib.port("".join(k for _, k in steps), work / "port", args.seed,
                              args=["--helm"])
    asked = sum(1 for l in lines if l == "menu: Continue Battle:")
    print("port: Continue Battle: asked", asked, "times;",
          [l for l in lines if l.startswith("round: 2")])
    log = [l for l in lines if l.startswith(("print:", "menu:", "combat:", "turn:"))]
    gods = "print: The Gods intervene!" in lines
    print("port:", " | ".join(log[log.index("print: The Gods intervene!") - 2:][:8]
                               if gods else log[-8:]))
    status = 0
    with d_lib.box(work, args.seed, verbose=args.verbose, args="x Helm") as b:
        res = d_lib.run_steps(b, steps[:2], shots, work)
        monsters = [c for c in d_lib.combatants(b) if c["n"] > 6]
        print("original after Alt-X:", [(c["name"], "status", c["status"], "size", c["size"])
                                        for c in monsters])
        dead = all(c["status"] == 6 and c["size"] == 0 for c in monsters)
        res.update(d_lib.run_steps(b, steps[2:], shots, work))
        print("original: round", b.ds_byte(0x714B), "(DS:714b)")
    status |= d_lib.verdict(gods and dead and all(res[k][0] == 0 for k in ("turn", "gods")),
                            "#82 Alt-X with Helm: the screens at the turn and at Continue "
                            "Battle: equal eclplay's; every baaz dead "
                            "(status 6) and off the map (size 0) in the original")
    status |= d_lib.verdict(asked == 2 and all(res[k][0] == 0 for k in ("round2", "again",
                                                                         "results")),
                            "#81 Continue Battle: Yes gives round 2 (KAL's menu), asked "
                            "again at its end, No ends with the results: screens equal")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

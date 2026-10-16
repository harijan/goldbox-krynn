#!/usr/bin/env python3
"""Checks #82 and #81 (DEVELOPERS.md, Checks against the original): "Start the game
with a second argument of Helm (START x Helm) and press Alt-X at a
character's turn in combat: "The Gods intervene!", every monster dead, the
round ending." and "Win a battle that the party can still fight: "Continue
Battle:" should ask at the round's end; Yes should go on with another round
and ask again."

Game A from the start menu, Solace's pages, the caravan's four baaz (the
first step north-east of the outpost); at ISTAN HORBIN's turn, the first,
Alt-X. The original runs with `x Helm`, eclplay with --helm, both seed 7.
Compared, at ISTAN's turn, at "Continue Battle:", in the second round after
Yes (KAL's menu), at "Continue Battle:" again after the six party members'
Done Quit, and at the results after No: the screen and the memory
(d_lib.compare_state: the baaz' status and footprint, the round, ...)
against the port's at the same point.
"""
import argparse
import sys

import checklib
import d_lib

STEPS = [("turn", d_lib.LOAD_A + d_lib.TO_CARAVAN), ("gods", r"\@x"), ("round2", "Y"),
         ("again", "DQ" * 6), ("results", "N")]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-helm")
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed, args=["--helm"])
    v = checklib.Verdict()
    seen, text = {}, {}
    with d_lib.box(work, args.seed, verbose=args.verbose, args="x Helm") as b:
        for name, keys in STEPS:
            shown = d_lib.step(b, keys, name, work)
            text[name] = d_lib.screen_text(shown, [24])
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            seen[name] = d_lib.compare_state(v, b, port, name)
    # DEVELOPERS.md's points, as the original showed them (batch D).
    monsters = [f for f in seen["gods"]["fighters"] if f[0] > 6]
    print("original after Alt-X: the baaz (n, status, size)",
          [(f[0], f[6], f[4]) for f in monsters])
    v.check(len(monsters) == 4 and all(f[6] == 6 and f[4] == 0 for f in monsters),
            "#82 the Gods: every baaz dead (status 6) and off the map (size 0) in the original")
    v.check("print: The Gods intervene!" in port.lines, "#82 the port says \"The Gods intervene!\"")
    asked = [n for n in ("gods", "again") if "CONTINUE BATTLE" in text[n].get(24, "")]
    v.check(asked == ["gods", "again"], "#81 the original asks \"Continue Battle:\" at the end "
            "of round 1 and again at the end of round 2", f"asked at {asked}")
    # DS:714b counts a round at its end, before "Continue Battle:".
    v.check(seen["round2"]["turn"] == "KAL" and
            seen["again"]["round"] == seen["gods"]["round"] + 1,
            "#81 Yes: another round in the original (KAL's menu first; DS:714b one more at "
            "its end)", f"rounds counted {seen['gods']['round']} -> {seen['again']['round']}")
    v.check(port.lines.count("menu: Continue Battle:") == 2, "#81 the port asks twice too")
    v.check(not seen["results"]["battle"], "#81 No ends the battle in the original")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

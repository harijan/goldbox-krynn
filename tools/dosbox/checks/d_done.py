#!/usr/bin/env python3
"""Check #16 (docs/checks.md, Checks against the original): "In the Done menu, F8
(scan 0x42, B) with no one dying ends the turn as Bandage would; Home (0x47)
guards; in Speed, Del slows past 9."

Game A, the caravan's four baaz (seed 7): ISTAN HORBIN's turn, Done, F8; KAL's
turn, Done, Home; GARIN's turn, Done, Speed, Del twelve times (from speed 4).
At each step the original's screen against the port's at the same point,
and the original's memory against the port's state there (d_lib.compare_state:
whose turn it is, the speed DS:4b38, the combatants, ...) and KAL's guard
(combat record +0x07).
"""
import argparse
import sys

import checklib
import d_lib

STEPS = [("istan", d_lib.LOAD_A + d_lib.TO_CARAVAN), ("istan-done", "D"), ("kal", r"\#42"),
         ("kal-done", "D"), ("garin", r"\#47"), ("speed", "DS"), ("del5", r"\#53" * 5),
         ("del12", r"\#53" * 7)]


def guard(cs):
    return d_lib.by_name(cs, "KAL")[0]["combat"][7]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-done")
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed)
    v = checklib.Verdict()
    seen = {}
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        for name, keys in STEPS:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            o = d_lib.compare_state(v, b, port, name)
            seen[name] = (o["turn"], guard(d_lib.combatants(b)), o["speed"])
            d_lib.value(v, f"KAL's guard (+0x07) at {name}", seen[name][1], port, name,
                        lambda s: guard(d_lib.port_combatants(s)))
    # What docs/checks.md's check says, as the original showed it (batch D).
    v.check(seen["kal"][0] == "KAL", "F8 ended ISTAN's turn in the original: KAL's next")
    v.check(seen["garin"][1] == 1, "Home: KAL guards in the original (+0x07 1)")
    v.check("print: Guarding" in port.lines, "the port says \"Guarding\" at KAL's place")
    v.check((seen["del5"][2], seen["del12"][2]) == (9, 16),
            "Del slows past 9 in the original: speed 4 -> 9 after 5, 16 after 12")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #63 (README, Checks against the original): "Flee a fight: the
results should say `The party has fled.` and `Each character receives 0`."

Game A started with Helm (eclplay --helm), seed 7, the caravan's baaz: the
Gods at the first turn, "Continue Battle:" Yes, and the six walk south and
off the map, round after round ("Flee:" Yes, "Got Away"; the keys were
found with eclplay, each step tried until the map refuses it). The results'
screen is compared with eclplay's, and the original's party's statuses read
there (0 again: the end of combat puts back those that ran).
"""
import argparse
import sys

import d_lib

FLIGHT = (r"M222221111346\rDQM2222113344666\rDQM22222213446\rDQM22222N111346\rDQ"
          r"M221111N33346\rDQM22222111346\rDQYM2222YDQM222222YDQM2222211346\rDQ"
          r"M2211N333YYDQM22YDQYM21333333YDQYM22222Y")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("flee", args.workdir)
    steps = [("turn", d_lib.LOAD_A + d_lib.TO_CARAVAN), ("gods", r"\@xY"),
             ("results", FLIGHT)]
    lines, shots = d_lib.port("".join(k for _, k in steps), work / "port", args.seed,
                              args=["--helm"])
    i = lines.index("print: The party has fled.")
    print("port:", lines[i:i + 3])
    with d_lib.box(work, args.seed, verbose=args.verbose, args="x Helm") as b:
        res = d_lib.run_steps(b, steps, shots, work)
        party = [(c["name"], c["status"]) for c in d_lib.combatants(b)[:6]]
        print("original's party (status):", party)
    ok = lines[i + 1] == "print: Each character receives 0" and \
        all(c == 0 for c, _ in res.values())
    status = d_lib.verdict(ok, "#63 the party's flight: \"The party has fled.\", \"Each "
                           "character receives 0\"; the results' screen equal to eclplay's")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

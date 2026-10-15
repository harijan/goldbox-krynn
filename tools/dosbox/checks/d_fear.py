#!/usr/bin/env python3
"""Check #122 (README, Checks against the original): "Cast Fear and press
`T` at once (the aim on the caster's own cell), in Tandy mode with the
caster at a few places on the view: the cone should go as the port's does
(the directions drawing the caster's icon left). Repeat in EGA mode (the
same) and CGA mode (other directions ...)."

Game J (d_cloud.py's: GARIN with Fear), the gate route, seed 7, in each
display mode (machine tandy, ega, cga; eclplay --display the same): every
turn Done/Quit until GARIN may cast (round 2), Fear ("Begins Casting"), and
at his next turn Target at once. Compared: who "runs in terror" (effect
0x6f in the original's records, the port's log) and the screen at the next
menu. One caster place only (GARIN at 26,13 where the battle put him).
"""
import argparse
import sys

import d_lib
from d_cloud import ROUTE, spells_save

TURNS = "DQ" * 9 + r"C\r"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--machine", action="append", choices=["tandy", "ega", "cga"])
    parser.add_argument("--psp", type=lambda v: int(v, 16), default=0x08F0,
                        help="the game's PSP (hex; default 08f0, DS 14c6: the low byte "
                        "0xc6 the port assumes; 0 for where DOS puts it, 087b)")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("fear", args.workdir)
    saves = spells_save(work)
    status = 0
    for machine in args.machine or ["tandy", "ega", "cga"]:
        out = work / machine
        lines, shots = d_lib.port(ROUTE + TURNS + "T", out / "port", args.seed, saves=[saves],
                                  args=["--helm"], machine=machine)
        k = lines.index("cast: GARIN : Fear")
        terror = sorted(a[7:].strip() for a, b in zip(lines[k:], lines[k + 1:])
                        if b == "print: runs in terror")
        unaffected = sorted(a[7:].strip() for a, b in zip(lines[k:], lines[k + 1:])
                            if b == "print: is unaffected")
        with d_lib.box(out, args.seed, machine=machine, verbose=args.verbose, saves=[saves],
                       args="x Helm", want_psp=args.psp or None) as b:
            print(f"{machine}: the game's PSP {b.psp:04x}, DS {b.ds:04x}")
            res = d_lib.run_steps(b, [("aim", ROUTE + TURNS), ("after", "T")], shots, out)
            cs = d_lib.combatants(b)
            fled = sorted(c["name"].strip() for c in cs
                          if any(e[0] == 0x6F for e in d_lib.effects(b, c["rec"])))
            print("  0x6f on", [(c["n"], c["name"], c["x"], c["y"]) for c in cs
                                if any(e[0] == 0x6F for e in d_lib.effects(b, c["rec"]))],
                  "; GARIN at", [(c["x"], c["y"]) for c in cs if c["name"] == "GARIN "])
        print(f"{machine}: port: runs in terror {terror}, unaffected {unaffected}; "
              f"original: 0x6f on {fled}")
        status |= d_lib.verdict(fled == terror and all(c == 0 for c, _ in res.values()),
                                f"#122 {machine}: Fear with T at once: the same terrified "
                                f"({terror}); screens equal")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

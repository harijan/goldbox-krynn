#!/usr/bin/env python3
"""Check #122 (README, Checks against the original): "Cast Fear and press
`T` at once (the aim on the caster's own cell), in Tandy mode with the
caster at a few places on the view: the cone should go as the port's does
(the directions drawing the caster's icon left). Repeat in EGA mode (the
same) and CGA mode (other directions ...)."

Game J (d_lib.spells_save: GARIN with Fear), the gate route, seed 7, in each
display mode (machine tandy, ega, cga; eclplay --display the same): every
turn Done/Quit until GARIN may cast (round 2), Fear ("Begins Casting"), and
at his next turn Target at once. Compared: the screen and the memory
(d_lib.compare_state: who "runs in terror", effect 0x6f, among it) at the
aim and at the next menu against the port's. One caster place only (GARIN
at 26,13 where the battle put him).

The cone at the caster's own cell depends on where DOS loaded the game: at a
PSP of 0xnnf0 (DS low byte 0xc6, what the port assumes; --psp 8f0, the
default) the original agrees in all three modes; at the PSP the harness
gives without a pad (087b, --psp 0) it terrifies a hobgoblin instead and
no party member: the port, which models 0xc6 only (README), is not
compared then, and the original's recorded behaviour is asserted.
"""
import argparse
import sys

import checklib
import d_lib

TURNS = "DQ" * 9 + r"C\r"


def terrified(fighters):
    return sorted(f[1].strip() for f in fighters if any(e[0] == 0x6F for e in f[7]))


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
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-fear")
    saves = d_lib.spells_save(work)
    steps = [("aim", d_lib.GATE_J + TURNS), ("after", "T")]
    v = checklib.Verdict()
    for machine in args.machine or ["tandy", "ega", "cga"]:
        out = work / machine
        port = d_lib.Port(out / "eclplay", d_lib.marked(steps), args.seed, saves=[saves],
                          args=["--helm"], machine=machine)
        with d_lib.box(out, args.seed, machine=machine, verbose=args.verbose, saves=[saves],
                       args="x Helm", want_psp=args.psp or None) as b:
            print(f"{machine}: the game's PSP {b.psp:04x}, DS {b.ds:04x}")
            low = b.ds & 0xFF
            for name, keys in steps:
                shown = d_lib.step(b, keys, name, out)
                if low == 0xC6:
                    d_lib.screen(v, f"{machine}: screen {name}", shown, port, name, out)
                    o = d_lib.compare_state(v, b, port, name)
                else:
                    o = d_lib.original_view(b)
        fled = terrified(o["fighters"])
        print(f"{machine}: original: 0x6f on {fled}")
        if low == 0xC6:
            v.check(fled == ["KAL", "SIR STRONGSWORD"],
                    f"#122 {machine}: the original terrifies SIR STRONGSWORD and KAL, as batch "
                    "D saw it at PSP 08f0", f"{fled}")
        else:
            print(f"{machine}: DS's low byte {low:#x}, not the 0xc6 the port models: the port "
                  "is not compared; the original's recorded cone asserted")
            v.check(fled and all(n.startswith("HOBGOBLIN") for n in fled),
                    f"#122 {machine}: at DS low byte {low:#x} the original terrifies a "
                    "hobgoblin and no party member, as batch D saw it at PSP 087b", f"{fled}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

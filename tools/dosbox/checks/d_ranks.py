#!/usr/bin/env python3
"""Check #75 (README, Checks against the original): "With the second of
four characters unconscious, fight anything: its body should lie where the
second would stand, right of the first, and the third and fourth stand a
rank back."

The port writes game J: game A in the outpost inn's camp, then patched to a
party of four (the saved game's party size, byte 0x1414, 6 -> 4) with the
second, ISTAN HORBIN, unconscious (.SAV +0x188 status 4, +0x189 can-act 0,
+0x197 hit points 0). Both load J and meet the caravan's baaz (seed 7, the
first step north-east; open ground) and, in a second run, Throtl's guards
(the gate route of d_gate.py, a 3D street, with Helm). At the first menu the
original's battlefield (DS:6a2e), the bodies (DS:69ee + 7k: x, y, terrain
under), the combatants' cells, the rest of the memory (d_lib.compare_state)
and the screen against the port's.
"""
import argparse
import sys

import checklib
import d_lib

# Where batch D saw ISTAN's body and the others (name, x, y) in each fight.
SEEN = {"caravan": ((28, 14), {"SIR STRONGSWORD": (27, 13), "GARIN ": (29, 14),
                               "MOLLY": (27, 14)}),
        "gate": ((28, 13), {"SIR STRONGSWORD": (27, 13), "GARIN ": (28, 14),
                            "MOLLY": (29, 14)})}


def save_four(work):
    saves = d_lib.make_save("J", work / "save")
    path = saves / "SAVGAMJ.DAT"
    data = bytearray(path.read_bytes())
    data[0x1414] = 4
    path.write_bytes(bytes(data))
    d_lib.patch_record(saves, "J", 2, 0x188, [4, 0])
    d_lib.patch_record(saves, "J", 2, 0x197, [0])
    return saves


def field(cs, cells, bodies):
    return ([r.hex() for r in cells], [(c["n"], c["name"], c["x"], c["y"], c["size"]) for c in cs],
            bodies)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-ranks")
    saves = save_four(work)
    v = checklib.Verdict()
    for name, keys, extra, box_args in [
            ("caravan", d_lib.load("J") + r"Lnm9\r", [], ""),
            ("gate", d_lib.GATE_J, ["--helm"], "x Helm")]:
        out = work / name
        port = d_lib.Port(out / "eclplay", d_lib.marked([("menu", keys)]), args.seed,
                          saves=[saves], args=extra)
        with d_lib.box(out, args.seed, verbose=args.verbose, saves=[saves], args=box_args) as b:
            shown = d_lib.step(b, keys, "menu", out)
            d_lib.screen(v, f"{name}: screen menu", shown, port, "menu", out)
            d_lib.compare_state(v, b, port, "menu")
            cs = d_lib.combatants(b)
            mine = field(cs, d_lib.battle_map(b), d_lib.bodies(b))
        d_lib.value(v, f"#75 {name}: the battlefield, the combatants' cells and the bodies "
                    "(name, x, y, terrain under)", mine, port, "menu",
                    lambda s: field(d_lib.port_combatants(s), s["cells"], d_lib.port_bodies(s)))
        body, others = SEEN[name]
        where = {c["name"]: (c["x"], c["y"]) for c in cs if c["size"] and c["n"] <= 4}
        print(f"{name}: original: bodies {mine[2]}; the party's cells {where}")
        v.check([b_[:3] for b_ in mine[2]] == [("ISTAN HORBIN", *body)] and
                {n: where.get(n) for n in others} == others,
                f"#75 {name}: the original: ISTAN's body where the second stands, the third "
                "and fourth a rank back, as batch D saw them")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

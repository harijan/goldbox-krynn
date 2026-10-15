#!/usr/bin/env python3
"""Checks #74, #83, #84 and #88 (DEVELOPERS.md, Checks against the original): the
fight with the guards at Throtl's gate (7, 15 facing north, ATTACK) with
saved game A's six: the battlefield and its placement (#74), the combat
screen as the battle is set up (#83), the characters' icons (#84), and the
panel's text when ISTAN HORBIN attacks a hobgoblin (#88).

Route, the same keys in eclplay (--helm) and the original (x Helm), seed 7:
game A, the caravan (the Gods at the first turn, Continue No, the results,
Exit, No treasure, the survivors helped back to the outpost), Leave, decline
the outpost, Move, north-east, north, enter Throtl, ATTACK. The original is
watched from ATTACK to MOLLY's menu
(the first turn) and through ISTAN's attack (Move, 8 into the hobgoblin
north of him). The battle as set up (the original stopped as its first turn
starts) against the port's frame of the set-up battle; the screens at each
wait against the port's at the same point; the attack's burst against the
port's frames in order, and the panel's text read with the game's font.
The map (DS:6a2e), the combatants' cells (DS:623f) and facings (combat
record +0x09) and the rest of d_lib.compare_state against the port's.
"""
import argparse
import sys

import checklib
import d_lib

ROUTE = d_lib.LOAD_A + d_lib.TO_CARAVAN + d_lib.CARAVAN_WON + "Lnm98y"
STEPS = [("gate", ROUTE), ("molly", "A"), ("istan", "DQ"), ("move", "M"), ("sirrion", "8")]
# The gate's ATTACK/LEAVE menu: the guards' picture in the view animates
# while the menu waits, so which of its frames the screenshot shows depends
# on time (eclplay's shot has the first).
ANIMATED = [(*checklib.VIEW_AREA, "the guards' picture, animated while the menu waits")]
# The panel as batch D read it in ISTAN's attack (rows 10-18).
PANEL = [(10, "ISTAN HORBIN"), (11, "ATTACKS"), (12, "HOBGOBLIN"), (13, "HITTING FOR 11"),
         (14, "POINTS OF DAMAGE"), (16, "HOBGOBLIN"), (17, "GOES DOWN"), (18, "AND IS DYING")]


def field(cs, cells):
    """The battlefield: its terrain rows (hex) and each combatant's (n,
    name, x, y, size, facing)."""
    return ([r.hex() for r in cells],
            [(c["n"], c["name"], c["x"], c["y"], c["size"], c["combat"][9]) for c in cs])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--burst", type=float, default=60,
                        help="seconds of screenshots at most after ISTAN's step")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-gate")
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed, args=["--helm"])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, args="x Helm") as b:
        for (before, _), (name, keys) in zip([(None, None)] + STEPS, STEPS):
            if name == "molly":
                b.push_key(ord("a") | 0x1E00)
                # The battle's first turn (3995:040b) made to spin as it starts:
                # the screen is the set-up battle's.
                handle = d_lib.trap(b, 0x3995, 0x040B, lambda: b.ds_byte(d_lib.DS_ENTRY) == 1, 30)
                setup = d_lib.stable(b)
                setup.save_png(work / "setup.png")
                d_lib.untrap(b, handle)
                print("  the set-up battle's text:", d_lib.screen_text(setup))
                frame = d_lib.load_bmp(port.at["molly"].setup)
                v.screen("#83 the battle as set up (the first turn's start) against the port's "
                         "frame of the set-up battle", setup, frame, work=work)
                b.wait_input(settle=1.0)
                shown = b.screenshot(work / "molly.png")
            elif name == "sirrion":
                d_lib.burst_step(v, b, port, before, name, ord("8") | 0x0900, work, "attack",
                                 args.burst)
                attack = sorted(work.glob("attack-[0-9][0-9].png"))
            else:
                shown = d_lib.step(b, keys, name, work, settle=0.5 if name == "move" else 1.0)
            if name != "sirrion":
                d_lib.screen(v, f"screen {name}", shown, port, name, work,
                             masks=ANIMATED if name == "gate" else ())
            d_lib.compare_state(v, b, port, name)
            if name == "molly":
                mine = field(d_lib.combatants(b), d_lib.battle_map(b))
                d_lib.value(v, "#74 the battlefield (DS:6a2e's terrain) and every combatant's "
                            "cell, size and facing at MOLLY's menu", mine, port, "molly",
                            lambda s: field(d_lib.port_combatants(s), s["cells"]))
    shots = [d_lib.cokbox.Screen.load(p) for p in attack]
    panel = d_lib.panel(shots, range(9, 21))
    print("original's panel rows 9-20 during the attack, in order:", panel)
    seen = [x for x in panel if x in PANEL]
    v.check(seen == PANEL, "#88 the original's panel in ISTAN's attack, as batch D read it",
            f"{seen}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

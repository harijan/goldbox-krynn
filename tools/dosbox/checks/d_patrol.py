#!/usr/bin/env python3
"""Checks #76 and #79 (DEVELOPERS.md, Checks against the original): the huge patrol
of ECL1 block 16 at 8cda (KERNEN, 31, 3 on the overland map): 9 red dragons,
9 bozaks and 9 sivaks, which surprise the party (var 0x7ecb 1).
#76 "Stay to fight the huge patrol ...: count how many stand on the
battlefield; those that find no place are removed before the fight and
bring no experience."
#79 "Meet the patrol ... which surprises the party: in the first round only
characters of dexterity 16 and up, with a high roll, should get a turn."

The port writes game H: game A on the overland map at 30, 2 (eclplay
--load with var 0x4bf2 16, 0x4bc3 30, 0x4bc4 2, 0x4c2d 1, block 16), camped
and saved there. Both (Helm, seed 7) load H, Move south-east onto 31, 3
(the battle faces south-east, and 14 of the 27 find no place: from 30, 3
east all 27 do): "A HUGE
PATROL SALLIES FORTH FROM THE CITY. DO YOU FLEE?" No. The original is
stopped as the first turn starts (3995:040b made to spin) and read: the
combatants on the map (DS:623f, size above 0) and their cells, against
eclplay's log of the placement; every combatant's initiative (combat record
+0x03) against eclplay's round 1; the screen against the port's frame of
the set-up battle. Then ISTAN's menu, the Gods and the results (the
experience): the screens and the memory (d_lib.compare_state) against the
port's at each.
"""
import argparse
import re
import sys

import checklib
import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-patrol")
    saves = d_lib.make_save("H", work / "save", keys="e", block=16, args=[
        "--set", "4bf2=10", "--set", "4bc3=1e", "--set", "4bc4=2", "--set", "4c2d=1"])
    steps = [("flee", d_lib.load("H") + "m3"), ("istan", "N"), ("gods", r"\@x"),
             ("results", r"\r")]
    port = d_lib.Port(work / "eclplay", d_lib.marked(steps), args.seed, saves=[saves],
                      args=["--helm"])
    cells = {}
    for line in port.lines:
        m = re.match(r"combat: (\d+) (.*) at (\d+),(\d+)$", line)
        if m:
            cells[int(m.group(1))] = (m.group(2).rstrip(), int(m.group(3)), int(m.group(4)))
    first = [line for line in port.lines if line.startswith("round: 1:")]
    port_init = [(e.rsplit(" ", 1)[0], int(e.rsplit(" ", 1)[1]))
                 for e in first[0].split(": ", 2)[2].split(", ")] if first else None
    no_place = sum(1 for line in port.lines if line.endswith("has no place and is removed"))
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves], args="x Helm") as b:
        shown = d_lib.step(b, steps[0][1], "flee", work)
        d_lib.screen(v, "screen flee", shown, port, "flee", work)
        d_lib.compare_state(v, b, port, "flee")
        print(f"  DS:72ce before the battle: {b.ds_byte(d_lib.DS_ENTRY)}", flush=True)
        b.push_key(ord("n") | 0x3100)
        # The combatants and the order read once they stay as they are (the
        # game spinning in the trap): DS:72ce may be 1 before the battle.
        handle = d_lib.trap(b, 0x3995, 0x040B, lambda: b.ds_byte(d_lib.DS_ENTRY) == 1, 60,
                            settle=lambda: (d_lib.initiatives(b), [
                                (c["n"], c["x"], c["y"], c["size"], c["raw"])
                                for c in d_lib.combatants(b)]))
        setup = d_lib.stable(b)
        setup.save_png(work / "setup.png")
        cs = d_lib.combatants(b)
        mine = {c["n"]: (c["name"].rstrip(), c["x"], c["y"]) for c in cs if c["size"]}
        inits = d_lib.initiatives(b)
        party = {c["name"]: c["raw"][0x17] for c in cs[:6]}
        d_lib.untrap(b, handle)
        frame = port.at["istan"].setup
        v.screen("#76 the battle as set up (the first turn's start) against the port's frame "
                 "of the set-up battle", setup, d_lib.load_bmp(frame) if frame else None,
                 work=work)
        v.value("#76 the combatants on the battlefield and their cells (DS:623f, size above 0) "
                "against the port's log of the placement", mine, cells)
        v.value("#79 round 1's order and initiatives (+0x03) against the port's log", inits,
                port_init)
        for name, keys in steps[1:]:
            if name != "istan":  # N went in above
                b.keys(keys)
            b.wait_input(settle=1.0)
            shown = b.screenshot(work / f"{name}.png")
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            d_lib.compare_state(v, b, port, name)
    text = " ".join(d_lib.screen_text(shown).values())
    print(f"original: {len(mine)} on the map; the party's dexterity {party}; results {text!r}")
    print(f"port: {len(cells)} placed, {no_place} with no place")
    v.check(len(mine) == 19 and "EACH CHARACTER RECEIVES 5033" in text,
            "#76 the original: 19 of the 33 on the battlefield, each character receives 5033 "
            "(the removed bring no experience), as batch D saw it")
    party_inits = {n: i for n, i in inits if n in party}
    v.check({n: i for n, i in party_inits.items() if i} == {"ISTAN HORBIN": 3, "KAL": 1},
            "#79 the original: surprised, only ISTAN (3) and KAL (1) of the party get a turn",
            f"{party_inits}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

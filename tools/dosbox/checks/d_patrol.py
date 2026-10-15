#!/usr/bin/env python3
"""Checks #76 and #79 (README, Checks against the original): the huge patrol
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
eclplay's log; every combatant's initiative (combat record +0x03) against
eclplay's round 1; the screen. Then the Gods at ISTAN's turn and the
results' screen (the experience).
"""
import argparse
import re
import sys

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("patrol", args.workdir)
    saves = d_lib.make_save("H", work / "save", keys="e", block=16, args=[
        "--set", "4bf2=10", "--set", "4bc3=1e", "--set", "4bc4=2", "--set", "4c2d=1"])
    route = d_lib.load("H") + "m3"
    lines, shots = d_lib.port(route + r"N\@x\r\r", work / "port", args.seed, saves=[saves],
                              args=["--helm"])
    cells = {}
    for l in lines:
        m = re.match(r"combat: (\d+) (.*) at (\d+),(\d+)$", l)
        if m:
            cells[int(m.group(1))] = (m.group(2).rstrip(), int(m.group(3)), int(m.group(4)))
    first = [l for l in lines if l.startswith("round: 1:")][0]
    port_init = [(e.rsplit(" ", 1)[0], int(e.rsplit(" ", 1)[1]))
                 for e in first.split(": ", 2)[2].split(", ")]
    removed = [l for l in lines if "removed" in l]
    no_place = sum(1 for l in lines if l.endswith("has no place and is removed"))
    got = [l for l in lines if l.startswith("print: Each character receives")]
    print(f"port: {len(cells)} combatants placed, {no_place} with no place; {removed}; {got}")
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves], args="x Helm") as b:
        res = d_lib.run_steps(b, [("flee", route)], shots, work)
        b.push_key(ord("n") | 0x3100)
        handle = d_lib.trap(b, 0x3995, 0x040B, lambda: b.ds_byte(0x72CE) == 1, 60)
        setup = d_lib.stable(b)
        cs = d_lib.combatants(b)
        mine = {c["n"]: (c["name"].rstrip(), c["x"], c["y"]) for c in cs if c["size"]}
        idx, order = d_lib.turn_entry(b)
        inits = []
        for i in range(len(order)):
            rec = b.far(b.ds * 16 + 0x71AE + 4 * i)
            inits.append((order[i], d_lib.combat_record(b, rec)[3]))
        party = {c["name"]: c["raw"][0x17] for c in cs[:6]}
        d_lib.untrap(b, handle)
        count, area, frame = d_lib.best(setup, shots)
        print(f"original at the first turn: {count} pixels differ from eclplay's {frame.name}")
        print(f"original: {len(mine)} on the map; the party's dexterity {party}")
        print("original's round 1 (name, initiative):", inits)
        res.update(d_lib.run_steps(b, [("istan", ""), ("gods", r"\@x"), ("results", r"\r")],
                                   shots, work))
    ok76 = mine == cells and count == 0 and res["results"][0] == 0
    status = d_lib.verdict(ok76, f"#76 the huge patrol: {len(mine)} combatants on the "
                           f"battlefield, each on eclplay's cell ({no_place} removed); "
                           f"the set-up screen and the results ({got}) equal")
    status |= d_lib.verdict(inits == port_init,
                            "#79 surprise: round 1's order and initiatives equal eclplay's: "
                            + ", ".join(f"{n} {i}" for n, i in inits if n in party))
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

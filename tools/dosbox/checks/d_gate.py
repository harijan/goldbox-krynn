#!/usr/bin/env python3
"""Checks #74, #83, #84 and #88 (README, Checks against the original): the
fight with the guards at Throtl's gate (7, 15 facing north, ATTACK) with
saved game A's six: the battlefield and its placement (#74), the combat
screen as the battle is set up (#83), the characters' icons (#84), and the
panel's text when ISTAN HORBIN attacks a hobgoblin (#88).

Route, the same keys in eclplay (--helm) and the original (x Helm), seed 7:
game A, the caravan (the Gods at the first turn, Continue No, the results,
Exit, No treasure, the survivors helped back to the outpost), Leave, decline
the outpost, Move, north-east, north, enter Throtl, ATTACK. The original is
watched with screenshots as fast as they come from ATTACK to MOLLY's menu
(the first turn) and through ISTAN's attack (Move, 8 into the hobgoblin
north of him); every screen is compared with eclplay's frames where eclplay
has one (the set-up battle, each key read), and the panel's text read with
the game's font. The map (DS:6a2e) and the combatants' cells (DS:623f) and
facings (combat record +0x09) are read in memory and compared with eclplay's
--combat-map.
"""
import argparse
import re
import sys

import d_lib

ROUTE = (d_lib.LOAD_A + d_lib.TO_CARAVAN + r"\@x\r\rEN\r\rY\r\r" + r"Lnm98y")


def last_battle(path):
    """eclplay --combat-map's last battle: (hex rows, {n: (x, y)}, {n: facing})."""
    text = open(path).read().split("battle in block")[-1].splitlines()
    rows = [l for l in text if re.fullmatch(r"[0-9a-f]{100}", l)]
    cells, facings = {}, {}
    for l in text:
        m = re.match(r"\S+ (\d+) .*: at (\d+),(\d+) size \d+ side \d+ facing (\d+)$", l)
        if m:
            cells[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
            facings[int(m.group(1))] = int(m.group(4))
    return rows, cells, facings


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--cycles", default="fixed 20000")
    parser.add_argument("--burst", type=float, default=20,
                        help="seconds of screenshots after ISTAN's step")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("gate", args.workdir)
    cmap = work / "combat-map.txt"
    if cmap.exists():
        cmap.unlink()
    lines, shots = d_lib.port(ROUTE + "A" + "DQ" + "M8", work / "port", args.seed,
                              args=["--helm", "--combat-map", cmap])
    rows, cells, facings = last_battle(cmap)
    print("port's battle:", [l for l in lines if l.startswith("combat: ") and " at " in l][-21:-15])
    # The port's frames at each of its pauses too, for ISTAN's attack.
    _, pshots = d_lib.port(ROUTE + "A" + "DQ" + "M8", work / "port-pauses", args.seed,
                           args=["--helm"], pauses=True)
    i = lines.index("turn: ISTAN HORBIN (initiative 7)")
    print("port, ISTAN's attack:", [l for l in lines[i:i + 10] if not l.startswith("sound")])
    status = 0
    with d_lib.box(work, args.seed, verbose=args.verbose, args="x Helm",
                   cycles=args.cycles) as b:
        res = d_lib.run_steps(b, [("gate", ROUTE)], shots, work)
        b.push_key(ord("A") | 0x1E00)
        # The battle's first turn (3995:040b) made to spin as it starts: the
        # screen is the set-up battle's.
        handle = d_lib.trap(b, 0x3995, 0x040B, lambda: b.ds_byte(0x72CE) == 1, 30)
        print(f"  trapped {len(handle)} copies")
        setup = d_lib.stable(b)
        setup.save_png(work / "setup.png")
        entry = b.ds_byte(0x72CE)
        d_lib.untrap(b, handle)
        count, area, setup_frame = d_lib.best(setup, shots)
        print(f"original at the first turn's start (DS:72ce {entry}): {count} pixels differ "
              f"from eclplay's {setup_frame.name}" + (f" in {area}" if area else ""))
        caught = count == 0
        print("  its text:", d_lib.screen_text(setup))
        res.update(d_lib.run_steps(b, [("molly", "")], shots, work))
        mine = [r.hex() for r in d_lib.battle_map(b)]
        cs = d_lib.combatants(b)
        mycells = {c["n"]: (c["x"], c["y"]) for c in cs}
        facing = {c["n"]: d_lib.combat_record(b, c["rec"])[9] for c in cs}
        print("original's cells:", mycells)
        print("original's facings:", facing)
        res.update(d_lib.run_steps(b, [("istan", "DQ")], shots, work))
        b.push_key(ord("m") | 0x3200)
        b._until(lambda: not b.buffer(), 30, "M taken")
        b.wait_input(settle=0.5)
        # The game's speed (DS:4b38, 4) made 30 for the attack, so that its
        # pauses (speed x 100 ms) last for screenshots; then 4 again.
        speed = b.ds_byte(0x4B38)
        b.ds_write(0x4B38, bytes([30]))
        b.push_key(ord("8") | 0x0900)
        attack = d_lib.burst(b, args.burst, work=work, prefix="attack")
        b.ds_write(0x4B38, bytes([speed]))
        pause_frames = d_lib.frames_after(pshots, res["istan"][1], 150)
        matched = [getattr(pause_frames.get(s.pixels), "name", None) for s in attack]
        print("  the burst's screens equal to the port's frames at its pauses:", matched)
        texts = [d_lib.screen_text(s, range(9, 21), 23, 39) for s in attack]
        panel = []
        for t in texts:
            for r, line in sorted(t.items()):
                line = line.strip(" ?")
                if (r, line) not in panel:
                    panel.append((r, line))
        colours = {r: d_lib.read_text(s, r, 23, 39)[1] for s in attack for r in range(10, 21)
                   if d_lib.read_text(s, r, 23, 39)[0].strip(" ?")}
        print("original's panel rows 9-16 during the attack, in order:", panel)
        print("  their colours:", colours)
        res.update(d_lib.run_steps(b, [("sirrion", "")], shots, work))
    print("map rows equal:", sum(a == b_ for a, b_ in zip(mine, rows)), "of", len(rows))
    status |= d_lib.verdict(mine == rows and mycells == cells and facing == facings,
                            "#74 the battlefield (DS:6a2e's 50x25 terrain) and every "
                            "combatant's cell and facing equal eclplay's --combat-map")
    status |= d_lib.verdict(bool(caught) and res["molly"][0] == 0,
                            f"#83 the screen as the battle is set up equals eclplay's "
                            f"{setup_frame.name} (the original stopped as the first turn "
                            f"starts); MOLLY's menu too")
    status |= d_lib.verdict(res["molly"][0] == 0 and res["istan"][0] == 0,
                            "#84 the six characters' icons: the whole screen at MOLLY's "
                            "and ISTAN's menus equal eclplay's")
    status |= d_lib.verdict(any(matched) and all(m for m, t in zip(matched, texts)
                                                 if 10 in t and "ISTAN" in t[10]),
                            "#88 the original's screens during ISTAN's attack (those "
                            "showing it) equal the port's frames at its pauses; the panel "
                            "read: " + "; ".join(f"row {r} {t!r}" for r, t in panel))
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

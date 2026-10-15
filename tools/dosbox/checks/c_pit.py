#!/usr/bin/env python3
"""The pit of Throtl (ECL1 block 32, special 26 at 2,5; README, Checks
against the original): #30 "Fall into the pit in Throtl and compare the
damage messages and how they page"; #35 "With a dwarf or kender of
constitution 19 in the party, compare the saving throws against a trap's
DAMAGE with those of another race: the port adds 5 (effect 0x5e) to throws
of types 0, 2 and 4"; #105 "With Find Traps memorized by the second
character only, step on the trap of ECL1 block 32 that asks for it, and
choose CAST SPELL: the spell should go from the second character's memory."

Saved games the port writes inside Throtl (c_places.throtl_save), moved in
the file to 2,4 facing south, north of the pit: E as game A's party is
(the dwarves ISTAN HORBIN, constitution 18, and GARIN, 17, and the kender
MOLLY, 18, carry 0x5e from their .SFX: +5, +4, +5 to throws of type 0);
F with ISTAN HORBIN, the second, given Find Traps (22) in his first spell
byte (+0x1e). Seed 14: no one finds the trap (9b63's d100 above the best
skill), so the pit opens with E (DAMAGE 192 1 8 0 0: each character saves
against throw 0 or takes 1d8) and SPELL 22 finds ISTAN with F. With seed
14 the port without 0x5e's bonus hits MOLLY too: the throws tell the bonus.
The original's screens during the fall are compared with every frame cok
presents for the same keys (eclplay's shots are only at key reads).
"""
import argparse
import sys

import c_places as cp

STEP = r"m\^"


def hp_of(box):
    return [(cp.name_of(raw), raw[0x197], raw[0x188]) for _, raw in cp.party_records(box)]


def spells_of(box):
    return [(cp.name_of(raw), list(raw[0x1E:0x22])) for _, raw in cp.party_records(box)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=14)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("pit", args.workdir)
    status = 0

    # #30 and #35: the fall.
    saves, _ = cp.throtl_save(work / "save-e", "E", at=(2, 4, 4))
    keys = cp.load_keys("E") + "m"
    frames, _ = cp.cok_frames(work / "cok-e", keys + r"\^\r", saves=saves, seed=args.seed)
    fall = {}

    def the_fall(box):
        box.keys(r"\^")
        fall["shots"] = cp.watch(box, 10, work / "fall", "fall")
        box.wait_input(settle=1.0)
        fall["end"] = box.screenshot(work / "fall" / "end.png")
        fall["hp"] = hp_of(box)

    c = cp.Compare(work / "fall", saves, [("loaded", cp.load_keys("E")), ("move", "m")],
                   seed=args.seed, verbose=args.verbose)
    status |= c.run(box_hook=the_fall)
    # The screen at the key read is eclplay's to compare (below): cok may
    # present no frame of a wait its script's next key ends at once.
    during = [s for s in fall["shots"] if s.pixels != fall["end"].pixels]
    misses = cp.match_frames(during, frames, cp.VIEW)
    print(f"#30: {len(during)} distinct screens of the original during the fall; pixels "
          "differing from the nearest frame cok presents (or two in a row: caught "
          f"drawing), outside the 3D view's cells: {misses}")
    pfall, _, pshots = cp.cokbox.eclplay(keys + r"\^\r", work / "eclplay-fall", saves=[saves],
                                         args=["--seed", str(args.seed)])
    best = min(pshots, key=lambda p: fall["end"].diff(cp.cokbox.Screen.load(p))[0])
    shot = cp.cokbox.Screen.load(best)
    print("#30: the original's screen at the next key read against eclplay's", best.name,
          fall["end"].diff(shot), "; outside the 3D view:",
          cp.masked_diff(fall["end"], shot, cp.VIEW))
    misses.append(cp.masked_diff(fall["end"], shot, cp.VIEW))
    port_hits = [l for l in pfall.splitlines() if " is hit " in l or l.endswith(" dies.")]
    print("#35 port's hits:", port_hits)
    print("#35 original's party after the fall (name, hp, status):", fall["hp"])
    if any(misses):
        status = 1

    # #105: Find Traps from the second character.
    saves, _ = cp.throtl_save(work / "save-f", "F", at=(2, 4, 4))
    path = saves / "CHRDATF2.SAV"
    rec = bytearray(path.read_bytes())
    rec[0x1E] = 22
    path.write_bytes(bytes(rec))
    c = cp.Compare(work / "trap", saves, [
        ("loaded", cp.load_keys("F")),
        ("click", STEP, spells_of),      # SOFT CLICK ... CAST SPELL LEAVE ANYWAY
        ("found", "C", spells_of),       # YOU HAVE FOUND A TRAP!
    ], seed=args.seed, verbose=args.verbose)
    status |= c.run()
    print("#105 original's spell bytes at the menu:", c.probes["click"])
    print("#105 original's spell bytes after CAST SPELL:", c.probes["found"])
    # The port's: on, through the battle (won) to camp's Save as G.
    out, psaves, _ = cp.cokbox.eclplay(
        cp.load_keys("F") + STEP + r"C\r\r\r\rEeESG", work / "trap-port-save", saves=[saves],
        shots=False, args=["--combat", "won", "--seed", str(args.seed)])
    after = [(cp.name_of(d), list(d[0x1E:0x22])) for d in
             (open(psaves / f"CHRDATG{i}.SAV", "rb").read() for i in range(1, 7))]
    print("#105 port's spell bytes saved after the trap and the fight:", after)
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

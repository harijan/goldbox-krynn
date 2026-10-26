#!/usr/bin/env python3
"""The pit of Throtl (ECL1 block 32, special 26 at 2,5; docs/checks.md, Checks
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

The original's screens during the fall are compared, in order, with the
port's for the same keys as drawn or as cok presents them
(c_places.judge_watch; eclplay's shots are only at key reads); the screen at the wait after it, and each character's hit points
and status there (memory), with the port's at that read. For #105, the
party's spell bytes at the trap's menu and after CAST SPELL.
"""
import argparse
import sys

import c_places as cp
import checklib

# It judges what the original shows as it comes: its boxes at their cycles
# (when it runs, not when another check imports it).
if __name__ == "__main__":
    checklib.cokbox.REAL_TIME = True

STEP = r"m\^"


def hp_of(state):
    return {"each one's hit points and status": [(m.name, m.record[0x197], m.record[0x188])
                                                  for m in state.members]}


def spells_of(state):
    return {"each one's first four spell bytes (+0x1e)": [(m.name, list(m.record[0x1E:0x22]))
                                                          for m in state.members]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=14)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-pit")
    v = checklib.Verdict()

    # #30 and #35: the fall.
    saves, _ = cp.throtl_save(work / "save-e", "E", at=(2, 4, 4))
    c = cp.Compare(v, work / "fall", saves, [
        ("loaded", cp.load_keys("E"), hp_of),
        ("move", "m"),
        ("fall", r"\^", hp_of, 10),     # A PIT OPENS UP ..., the hits, PRESS <ENTER>
    ], seed=args.seed, verbose=args.verbose).run()
    c.judge_watch("fall")
    (before,), (after,) = c.probes["loaded"].values(), c.probes["fall"].values()
    hit = [a[0] for a, b in zip(after, before) if a[1] < b[1]]
    v.check(hit and "MOLLY" not in hit,
            "#35: the fall hits some and not MOLLY (a kender of constitution 18: her +5 saves "
            "her with this seed, where the port without 0x5e's bonus hits her)",
            f"the original's hit: {hit}")
    print("#35 port's hits:", [l for l in c.port_text.splitlines() if " is hit " in l])

    # #105: Find Traps from the second character.
    saves, _ = cp.throtl_save(work / "save-f", "F", at=(2, 4, 4))
    cp.edit_record(saves / "CHRDATF2.SAV", lambda r: r.__setitem__(0x1E, 22))
    c = cp.Compare(v, work / "trap", saves, [
        ("loaded", cp.load_keys("F")),
        ("click", STEP, spells_of),      # SOFT CLICK ... CAST SPELL LEAVE ANYWAY
        ("found", "C", spells_of),       # YOU HAVE FOUND A TRAP!
    ], seed=args.seed, verbose=args.verbose).run()
    (at_menu,), (cast,) = c.probes["click"].values(), c.probes["found"].values()
    v.check(at_menu[1][1][0] == 22 and cast[1][1][0] == 0 and
            [s for i, s in enumerate(at_menu) if i != 1] == [s for i, s in enumerate(cast) if i != 1],
            "#105: in the original Find Traps (22) goes from ISTAN HORBIN's memory, no one "
            "else's changes", f"{at_menu[1]} -> {cast[1]}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

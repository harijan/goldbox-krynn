#!/usr/bin/env python3
"""Checks #119, #22 and #128 (DEVELOPERS.md, Checks against the original), casting
in the caravan's battle (game A's KAL, cleric and mage):

#119 "In a battle, cast Magic Missile with Cast: the caster should turn
toward the target in its attacking pose, the spell's missile fly to it,
"Casts a Spell" (not "casts") show in the side panel with "Spell:Magic
Missile" on row 23, and the target flash. Cast while under 0x4a (a curse
that miscasts): still "Casts a Spell"."
#22 "A cleric's long spell ("Begins Casting") moves it down the round's
order with a d2 at each tie: compare the order after it."
#128 "Let a cleric begin casting a spell that takes time, then hit it:
"lost a spell", and its memorized spell gone."

#119: seed 7, ISTAN Done/Quit, KAL Cast Magic Missile, Next six times (a
baaz), Target, the game's speed poked to 30 for the spell's choice and for
the cast, and screenshots taken in bursts: each against the port's frames
at its pauses, map shows and text lines in order (d_lib.burst_step), the
panel and row 23 read with the game's font. Then the same with the port's
game E (game A with KAL given 0x4a): the same dice, and KAL miscasts (no
aim), "Casts a Spell" all the same. #22: seed 7, KAL casts Bless (casting
time 10, a third of it 3): initiative 7 -> 4, past the 6s and a d2 against MOLLY and
SIR STRONGSWORD at 4; the original's round order (DS:71ae) at GARIN's menu
against the port's there. #128: seed 12 (found with eclplay), KAL begins
Bless, a baaz hits him: his memorized Blesses (+0x1f-+0x57) and the spell
kept (combat record +0x00) before and after against the port's, and his
spell list at his next turn. At every step the screen and the memory
(d_lib.compare_state) against the port's at the same point.
"""
import argparse
import sys

import checklib
import d_lib

# It judges what the original shows as it comes: its boxes at their cycles
# (when it runs, not when another check imports it).
if __name__ == "__main__":
    checklib.cokbox.REAL_TIME = True

ROUTE = d_lib.LOAD_A + d_lib.TO_CARAVAN
BLESS = r"C\^\^\^\^\r"
# The order batch D saw at GARIN's menu after KAL began Bless (seed 7).
ORDER = ["ISTAN HORBIN", "BAAZ", "BAAZ", "GARIN ", "BAAZ", "MOLLY", "KAL", "SIR STRONGSWORD",
         "SIRRION", "BAAZ"]


def kal(cs):
    """KAL's Blesses (id 1) memorized (+0x1f-+0x57) and spell kept (+0x00)."""
    c = d_lib.by_name(cs, "KAL")[0]
    return c["raw"][0x1F:0x58].count(1), c["combat"][0]


def texts(shots):
    """Each screen's (row 10, row 11, row 23) as the game's font reads them."""
    return [tuple(d_lib.read_text(s, r, *cols)[0].strip(" ?") for r, cols in
                  ((10, (23, 39)), (11, (23, 39)), (23, (0, 40)))) for s in shots]


def missile(v, args, work, name, to_kal, n, seed, saves=()):
    """KAL's Magic Missile (TO_KAL the keys to his menu, N the Nexts to a
    baaz; 0: the cast ends with the choice, a miscast): the original's
    screens from the spell's choice to the end of the cast against the
    port's, in order. Returns the texts read on them."""
    out = work / name
    steps = [("list", to_kal + r"C\^"), ("chosen", r"\r")]
    if n:
        steps += [("aimed", "N" * n), ("after", "T")]
    port = d_lib.Port(out / "eclplay", d_lib.marked(steps), seed, saves=saves)
    shots = []
    with d_lib.box(out, seed, verbose=args.verbose, saves=saves) as b:
        for (before, _), (mark, keys) in zip([(None, None)] + steps, steps):
            if mark in ("chosen", "after"):
                key = ord("t") | 0x1400 if mark == "after" else 0x1C0D  # T, Enter
                shots += d_lib.burst_step(v, b, port, before, mark, key, out, mark,
                                          args.burst)[0]
            else:
                shown = d_lib.step(b, keys, mark, out)
                d_lib.screen(v, f"{name}: screen {mark}", shown, port, mark, out)
            d_lib.compare_state(v, b, port, mark)
    said = sorted(set(texts(shots)))
    print(f"port ({name}):", [line for line in port.lines if line.startswith(
        ("cast:", "print: Casts", "print: Spell"))][-3:])
    print(f"original ({name}): texts (row 10, 11, 23): {said}")
    return said, port.lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--burst", type=float, default=30)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-cast")
    v = checklib.Verdict()

    # #119, then under 0x4a.
    said, _ = missile(v, args, work, "missile", ROUTE + "DQ", 6, 7)
    v.check(any(t[:2] == ("KAL", "CASTS A SPELL") for t in said) and
            any(t[2] == "SPELL:MAGIC MISSILE" for t in said) and
            any(t[1].startswith("TAKES") for t in said),
            "#119 the original: \"CASTS A SPELL\" on row 11 under KAL, \"SPELL:MAGIC "
            "MISSILE\" on row 23, the target's \"TAKES\"")
    saves = d_lib.make_save("E", work / "save", args=["--effect", "5,4a,0,0"])
    # The same dice: KAL miscasts (a d2 of 1), no aim, the turn over.
    said, lines = missile(v, args, work, "cursed", d_lib.load("E") + r"Lnm9\r" + "DQ", 0, 7,
                          saves=[saves])
    k = lines.index("cast: KAL: Magic Missile") if "cast: KAL: Magic Missile" in lines else None
    print("port (cursed), after the cast:", lines[k + 1:k + 5] if k is not None else None)
    v.check(any(t[:2] == ("KAL", "CASTS A SPELL") for t in said),
            "#119 under 0x4a the original still says \"CASTS A SPELL\"")

    # #22
    port = d_lib.Port(work / "bless" / "eclplay", d_lib.marked([("garin", ROUTE + "DQ" + BLESS)]),
                      7)
    with d_lib.box(work / "bless", 7, verbose=args.verbose) as b:
        shown = d_lib.step(b, ROUTE + "DQ" + BLESS, "garin", work / "bless")
        d_lib.screen(v, "bless: screen garin", shown, port, "garin", work / "bless")
        d_lib.compare_state(v, b, port, "garin")
        order = d_lib.turn_entry(b)
    d_lib.value(v, "#22 the round's order and whose turn at GARIN's menu", order, port, "garin",
                d_lib.port_turn)
    print("port: turns after Begins Casting:", [line for line in port.lines
                                                 if line.startswith("turn:")][2:])
    v.check(order[1] == ORDER and d_lib.whose(order) == "GARIN ",
            "#22 the original: KAL (initiative 7, Bless begun: 4) after MOLLY, before SIR "
            "STRONGSWORD, as batch D saw it", f"{order}")

    # #128
    steps = [("molly", ROUTE + "DQDQ" + BLESS), ("sir", "DQ"), ("kal-list", "DQ" * 5 + "C")]
    port = d_lib.Port(work / "lost" / "eclplay", d_lib.marked(steps), 12)
    seen = {}
    with d_lib.box(work / "lost", 12, verbose=args.verbose) as b:
        for name, keys in steps:
            shown = d_lib.step(b, keys, name, work / "lost")
            d_lib.screen(v, f"lost: screen {name}", shown, port, name, work / "lost")
            d_lib.compare_state(v, b, port, name)
            seen[name] = kal(d_lib.combatants(b))
            d_lib.value(v, f"#128 KAL's Blesses memorized and spell kept at {name}", seen[name],
                        port, name, lambda s: kal(d_lib.port_combatants(s)))
    v.check(any("lost a spell" in line for line in port.lines), "#128 the port: \"lost a spell\"")
    v.check(seen["molly"] == (1, 1) and seen["sir"] == (0, 0),
            "#128 the original: Bless begun (one memorized, kept), then hit: none, none kept",
            f"{seen}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

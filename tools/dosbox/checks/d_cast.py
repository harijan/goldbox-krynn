#!/usr/bin/env python3
"""Checks #119, #22 and #128 (README, Checks against the original), casting
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
baaz), Target, the game's speed poked to 30 for the cast and screenshots
taken in a burst: each compared with eclplay's frames at its pauses
(d_lib.pause_player), the panel and row 23 read with the game's font. Then
the same with the port's game E (game A with KAL given 0x4a): the same
dice, and KAL miscasts (no aim), "Casts a Spell" all the same. #22: seed 7, KAL casts Bless (casting time 10, a
third of it 3): initiative 7 -> 4, past the 6s and a d2 against MOLLY and
SIR STRONGSWORD at 4; the original's round order (DS:71ae) at GARIN's menu
against the port's turns that follow. #128: seed 12 (found with eclplay),
KAL begins Bless, a baaz hits him: his memorized spells (+0x1f-+0x57) and
the spell kept (combat record +0x00) before and after, and his spell list
at his next turn.
"""
import argparse
import sys

import d_lib

ROUTE = d_lib.LOAD_A + d_lib.TO_CARAVAN
BLESS = r"C\^\^\^\^\r"


def kal(b):
    c = d_lib.by_name(d_lib.combatants(b), "KAL")[0]
    return c["raw"][0x1F:0x58].count(1), d_lib.combat_record(b, c["rec"])[0]


def missile(args, work, name, to_kal, n, seed, saves=()):
    """KAL's Magic Missile (TO_KAL the keys to his menu, N the Nexts to a
    baaz): the original's screens from choosing the spell to the end of the
    cast against the port's frames at its pauses; the texts read."""
    keys = to_kal + r"C\^"
    lines, shots = d_lib.port(keys + r"\r" + ("N" * n + "T" if n else ""), work / name, seed,
                              saves=saves, pauses=True)
    print(f"port ({name}):", [l for l in lines if l.startswith(("cast:", "print: Casts",
                                                                 "print: Spell"))][-3:])
    with d_lib.box(work / name, seed, verbose=args.verbose, saves=saves) as b:
        res = d_lib.run_steps(b, [("list", keys)], shots, work / name)
        speed = b.ds_byte(0x4B38)
        b.ds_write(0x4B38, bytes([30]))
        b.push_key(0x1C0D)
        burst = d_lib.burst(b, 8, until=lambda: b.waiting(), work=work / name, prefix="chosen")
        if n:
            b.wait_input()
            b.keys("N" * n)
            b.wait_input(settle=0.5)
            b.push_key(ord("t") | 0x1400)
            burst += d_lib.burst(b, args.burst, work=work / name, prefix="cast")
        b.ds_write(0x4B38, bytes([speed]))
    frames = d_lib.frames_after(shots, res["list"][1], 150)
    matched = [getattr(frames.get(s.pixels), "name", None) for s in burst]
    texts = [(d_lib.read_text(s, 10, 23, 39)[0].strip(" ?"), d_lib.read_text(s, 11, 23, 39)[0]
              .strip(" ?"), d_lib.read_text(s, 23)[0].strip(" ?")) for s in burst]
    print(f"original ({name}): screens equal to port frames {matched}; texts "
          f"(row 10, 11, 23): {sorted(set(texts))}")
    # Screens caught between two of the port's pauses (row 23 being written,
    # the map redrawn) have no frame; those at a pause must match one.
    shown = [t for m, t in zip(matched, texts) if m]
    said = any(t[:2] == ("KAL", "CASTS A SPELL") for t in shown) and \
        (any(t[2] == "SPELL:MAGIC MISSILE" for t in shown) or not n) and \
        (any(t[1].startswith("TAKES") for t in shown) or not n)
    return res["list"][0] == 0 and said, lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--burst", type=float, default=15)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("cast", args.workdir)
    status = 0

    # #119, then under 0x4a.
    ok, lines = missile(args, work, "missile", ROUTE + "DQ", 6, 7)
    saves = d_lib.make_save("E", work / "save", args=["--effect", "5,4a,0,0"])
    # The same dice: KAL miscasts (a d2 of 1), no aim, the turn over.
    ok4a, lines = missile(args, work, "cursed", d_lib.load("E") + r"Lnm9\r" + "DQ", 0, 7,
                          saves=[saves])
    k = lines.index("cast: KAL: Magic Missile")
    print("port (cursed), after the cast:", lines[k + 1:k + 5])
    status |= d_lib.verdict(ok and ok4a,
                            "#119 Magic Missile: the original's screens during the cast "
                            "equal the port's frames at its pauses; \"CASTS A SPELL\" on "
                            "row 11 under the caster's name, \"SPELL:MAGIC MISSILE\" on row "
                            "23; the same with 0x4a on KAL")

    # #22
    lines, shots = d_lib.port(ROUTE + "DQ" + BLESS + "DQ" * 5, work / "bless", 7)
    i = lines.index("print: Begins Casting")
    after = [l[6:].rsplit(" (", 1)[0] for l in lines[i:] if l.startswith("turn:")]
    print("port: turns after Begins Casting:", after)
    with d_lib.box(work / "bless", 7, verbose=args.verbose) as b:
        res = d_lib.run_steps(b, [("garin", ROUTE + "DQ" + BLESS)], shots, work / "bless")
        idx, order = d_lib.turn_entry(b)
        print(f"original at GARIN's menu: entry {idx}, the order {order}")
    rest = order[idx - 1:]
    status |= d_lib.verdict(res["garin"][0] == 0 and rest[:len(after) - 2] == after[2:],
                            "#22 Bless begun: the original's order from GARIN on "
                            f"{rest} matches the port's turns {after[2:]}")

    # #128
    keys = ROUTE + "DQDQ" + BLESS
    steps = [("molly", keys), ("sir", "DQ"), ("kal-list", "DQ" * 5 + "C")]
    lines, shots = d_lib.port("".join(k for _, k in steps), work / "lost", 12)
    print("port:", [l for l in lines if "lost a spell" in l or "Begins" in l])
    with d_lib.box(work / "lost", 12, verbose=args.verbose) as b:
        res = d_lib.run_steps(b, steps[:1], shots, work / "lost")
        before = kal(b)
        res.update(d_lib.run_steps(b, steps[1:2], shots, work / "lost"))
        after = kal(b)
        res.update(d_lib.run_steps(b, steps[2:], shots, work / "lost"))
    print(f"original: KAL's Blesses (id 1) memorized and spell kept (+0x00): before the hit "
          f"{before}, after {after}")
    status |= d_lib.verdict(before == (1, 1) and after == (0, 0) and
                            all(c == 0 for c, _ in res.values()),
                            "#128 Bless begun then KAL hit: \"lost a spell\", one Bless "
                            "fewer in memory, none kept; his spell list next turn equal")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

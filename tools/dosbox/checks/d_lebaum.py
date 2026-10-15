#!/usr/bin/env python3
"""Checks #9 and #23 (README, Checks against the original), in part:
#9 "Sir Lebaum (ECL3 block 82): at each of his turns 0x4a casts Dispel
Magic on a d20 of 1, with no text unless an effect of value other than 0xff
goes ("is affected"), on whatever his last fireball or spell aimed at."
#23 "A cleric on Auto against undead: 3afb:024b takes the highest kind to
turn from a stack byte its calls leave ... Put a cleric of level 5 or more
on Auto (Alt-Q) against undead only of kinds (+0xda) above 4: the port's
never tries to turn them, where the player's Turn would. Then against
undead of kind 1 or 2, which it should try to turn."

The port writes game J: game A in ECL3 block 82 (the dungeons of
Duerghast), saved after its first page, then patched: the square 0, 13
(square byte 0x9e, type 30: "A DEATH KNIGHT TURNS FROM HIS TASK OF RAISING
THE DEAD ... SIR LEBAUM", 9a07), every character's hit points and most 250
(+0x197, +0x62) so that the battle lasts, and KAL a cleric of level 7
(+0xf9). The port meets him at Begin (its first-block vectors), the
original after Look. Two pages, then Alt-Q: the computer plays every turn,
seed 7: 3 minotaurs, Sir Lebaum and 4 ghasts, the ghasts finding no place.
Compared: the screen where the keys end (SIRRION's menu, round 6), the
party's hit points and effects, Sir Lebaum's kind (+0xda) and effects
(0x4a), KAL's turning attempts (combat record +0x11) in the original's
memory, against eclplay's log.
"""
import argparse
import sys
import time

import d_lib


def save_82(work):
    saves = d_lib.make_save("J", work / "save", keys=r"\rE", block=82, args=["--file", "3"])
    path = saves / "SAVGAMJ.DAT"
    data = bytearray(path.read_bytes())
    data[0x1401:0x1406] = bytes([0, 13, 0, 0, 0x9E])
    path.write_bytes(bytes(data))
    for m in range(1, 7):
        rec = saves / f"CHRDATJ{m}.SAV"
        raw = bytearray(rec.read_bytes())
        raw[0x62] = raw[0x197] = 250
        if m == 5:
            raw[0xF9] = 7
        rec.write_bytes(bytes(raw))
    return saves


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("lebaum", args.workdir)
    saves = save_82(work)
    lines, shots = d_lib.port(d_lib.load("J") + r"\r\r\@q", work / "port", args.seed,
                              saves=[saves])
    lines = [l for l in lines if not l.startswith("sound:")]
    turned = [l for l in lines if l in ("print: turns", "print: Turns Undead")
              or l.startswith("print: is turned") or "destroyed" in l]
    k = lines.index("print: is affected") if "print: is affected" in lines else None
    dispel = lines[k - 1:k + 1] if k else []
    print("port: KAL's turning texts", turned, "; dispels", dispel)
    print("port: the last turns", [l for l in lines if l.startswith("turn:")][-3:])
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        res = d_lib.run_steps(b, [("pages", d_lib.load("J") + "L"), ("turn", r"\r\r")],
                              shots, work)
        cs = d_lib.combatants(b)
        lebaum = [c for c in cs if c["name"] == "SIR LEBAUM"][0]
        print("original: Sir Lebaum's kind (+0xda)", lebaum["raw"][0xDA], "effects",
              d_lib.effects(b, lebaum["rec"]))
        kal = [c for c in cs if c["name"] == "KAL"][0]
        b.push_key(0x1000)  # Alt-Q
        attempts = set()
        end = time.monotonic() + 900
        b._last_polls, b._streak = b.hook("polls"), 0
        while time.monotonic() < end and not b.waiting():
            cr = d_lib.combat_record(b, kal["rec"])
            if cr is not None:
                attempts.add(cr[0x11])
            time.sleep(0.05)
        b.wait_input(settle=1.0)
        shown = b.screenshot(work / "end.png")
        count = d_lib.compare(shown, shots, "end", work)
        cs = d_lib.combatants(b)
        party = {c["name"].strip(): (c["hp"], [e[0] for e in d_lib.effects(b, c["rec"])])
                 for c in cs[:6]}
        print("original at the end: KAL's +0x11 seen", sorted(attempts), "; party", party)
    # #9: in eclplay's log a d20 of 1 at one of Sir Lebaum's turns dispels
    # SIRRION's 0x6f (terrified, 3 copies): "SIRRION / is affected".
    sirrion = party["SIRRION"][1]
    terrified = sorted(n for n, (_, e) in party.items() if 0x6F in e)
    status = d_lib.verdict(count == 0 and dispel == ["print: SIRRION", "print: is affected"]
                           and 0x6F not in sirrion,
                           f"#9 Sir Lebaum's 0x4a: eclplay's Dispel ({dispel}) took "
                           f"SIRRION's 0x6f; the original's SIRRION has none at the end "
                           f"(effects {sirrion}) while {terrified} keep theirs, and the "
                           "screen where the keys end is equal")
    print(f"#23 (in part): KAL (cleric 7) on Auto never tried to turn in the original "
          f"(+0x11 seen {sorted(attempts)}) nor the port; but Sir Lebaum's kind is "
          f"{lebaum['raw'][0xDA]} and the ghasts found no place: no undead of a kind above 4 "
          "or of 1-2 was on the map, so the check is NOT RUN")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

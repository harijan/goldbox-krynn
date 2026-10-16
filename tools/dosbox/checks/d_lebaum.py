#!/usr/bin/env python3
"""Checks #9 and #23 (DEVELOPERS.md, Checks against the original), in part:
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
(+0xf9). Both meet him after Look (Begin Adventuring runs only the
first block's load vector, d_firstblock.py). Two pages, then Alt-Q: the computer plays every turn,
seed 7: 3 minotaurs, Sir Lebaum and 4 ghasts, the ghasts finding no place.
Compared: the screens and the memory (d_lib.compare_state: the party's
hit points and effects among it) after the pages and where the keys end
(SIRRION's menu) against the port's; Sir Lebaum's kind (+0xda) and KAL's
turning attempts (combat record +0x11) against the port's; and KAL's
attempts sampled through the battle (the port's log has no turning).
"""
import argparse
import sys

import checklib
import d_lib

STEPS = [("pages", d_lib.load("J") + "L"), ("turn", r"\r\r"), ("end", r"\@q")]


def save_82(work):
    saves = d_lib.make_save("J", work / "save", keys=r"\rE", block=82, args=["--file", "3"])
    d_lib.patch_game(saves, "J", square=[0, 13, 0, 0, 0x9E])
    for m in range(1, 7):
        d_lib.patch_record(saves, "J", m, 0x62, [250])
        d_lib.patch_record(saves, "J", m, 0x197, [250])
    d_lib.patch_record(saves, "J", 5, 0xF9, [7])
    return saves


def lebaum(cs):
    """Sir Lebaum's kind (+0xda) and KAL's turning attempts (+0x11)."""
    return (d_lib.by_name(cs, "SIR LEBAUM")[0]["raw"][0xDA],
            d_lib.by_name(cs, "KAL")[0]["combat"][0x11])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-lebaum")
    saves = save_82(work)
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed, saves=[saves])
    v = checklib.Verdict()
    attempts = set()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        for name, keys in STEPS[:2]:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            d_lib.compare_state(v, b, port, name)
        cs = d_lib.combatants(b)
        d_lib.value(v, "Sir Lebaum's kind and KAL's turning attempts at the first menu",
                    lebaum(cs), port, "turn", lambda s: lebaum(d_lib.port_combatants(s)))
        kal = d_lib.by_name(cs, "KAL")[0]["rec"]

        def sample():
            cr = d_lib.combat_record(b, kal)
            if cr is not None:
                attempts.add(cr[0x11])

        b.push_key(0x1000)  # Alt-Q: the computer plays every turn
        b._last_polls, b._streak = b.hook("polls"), 0
        b._until(lambda: sample() or b.waiting(), 900, "the battle on Auto", 0.05)
        b.wait_input(settle=1.0)
        shown = b.screenshot(work / "end.png")
        d_lib.screen(v, "screen end", shown, port, "end", work)
        o = d_lib.compare_state(v, b, port, "end")
        cs = d_lib.combatants(b)
        d_lib.value(v, "Sir Lebaum's kind and KAL's turning attempts at the end", lebaum(cs),
                    port, "end", lambda s: lebaum(d_lib.port_combatants(s)))
    lines = [line for line in port.lines if not line.startswith("sound:")]
    k = lines.index("print: is affected") if "print: is affected" in lines else None
    dispel = lines[k - 1:k + 1] if k else []
    print("port: dispels", dispel, "; the last turns",
          [line for line in lines if line.startswith("turn:")][-3:])
    party = {f[1].strip(): [e[0] for e in f[7]] for f in o["fighters"][:6]}
    print("original at the end: the party's effects", party, "; KAL's +0x11 seen",
          sorted(attempts))
    v.check(dispel == ["print: SIRRION", "print: is affected"],
            "#9 the port: Sir Lebaum's 0x4a dispels SIRRION's 0x6f (\"is affected\")")
    v.check(0x6F not in party["SIRRION"] and
            all(0x6F in party[n] for n in ("SIR STRONGSWORD", "ISTAN HORBIN", "KAL")),
            "#9 the original at the end: SIRRION's 0x6f gone, the others' kept", f"{party}")
    v.check(attempts == {0}, "#23 (in part) the original: KAL (cleric 7) on Auto never tried "
            "to turn (+0x11 0 throughout)", f"{sorted(attempts)}")
    print(f"#23: Sir Lebaum's kind is {lebaum(cs)[0]} and the ghasts found no place: no undead "
          "of a kind above 4 or of 1-2 was on the map, so the check is NOT RUN")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

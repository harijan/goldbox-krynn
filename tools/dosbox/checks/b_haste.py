#!/usr/bin/env python3
"""Check #49: Haste ages each member hasted a year, once (DEVELOPERS.md, Checks
against the original: "Cast Haste with a mage of level 4: each member
hasted should 'age' a year, once (its value's bit 4)").

Game A, GARIN (fighter/mage) made a level 4 mage who knows Haste (0x30,
+0x92) and has it memorized (+0x1e), in the outpost inn's camp: Magic, Cast,
Haste (Enter: the list's highlighted row), Exit, Exit, Save as F. The
original's ages (+0x60) and effects are read in memory after the cast:
each member hasted (effect 0x27 with bit 4 of its value) must be a year
older, once, the others not; the ages and effects must be the port's (its
game saved after Exit, Exit), and the saved game the port's, masked.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_magic_lib as M  # noqa: E402
import checklib  # noqa: E402

KEYS = r"\v\vMC\r"          # GARIN selected, Magic, Cast, the highlighted spell


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-haste")
    game = L.copy_game(L.SAVE, "A", work / "game")
    L.patch(game / "CHRDATA3.SAV", {0xFE: 4, 0x62 + 0x30: 1, 0x1E: 0x30})

    def ages(directory, letter):
        return {L.name_of(L.sav(directory, letter, n)):
                struct.unpack_from("<H", L.sav(directory, letter, n), 0x60)[0] for n in range(1, 7)}
    ages0 = ages(game, "A")
    print("ages before:", ages0)
    seen, effects = {}, {}

    def probe(box, i):
        if i == 0:
            for addr, raw in L.members(box):
                seen[L.name_of(raw)] = struct.unpack_from("<H", raw, 0x60)[0]
                effects[L.name_of(raw)] = [e[:4] for e in L.effects_at(box, addr + 0xE3)]
            print("original after the cast, ages:", seen)
            for name, e in effects.items():
                print("  ", name, e)

    out, psaves, odir, _ = M.run_both(work, game, KEYS + "EE", steps=[KEYS, "EE"],
                                      probe=probe, verbose=args.verbose)
    v = checklib.Verdict()
    print("port:", " / ".join(ln[7:] for ln in M.lines(out, ("print",))
                                 if "ages" in ln or "Hasted" in ln or "casts" in ln))
    hasted = {n for n, e in effects.items() if any(x[0] == 0x27 and x[2] & 0x10 for x in e)}
    v.check(len(hasted) == 4 and all(seen[n] - ages0[n] == (n in hasted) for n in seen),
            "in the original each member hasted is a year older, the others not",
            f"hasted {sorted(hasted)}, ages {seen}")
    v.value("the ages after the cast", seen, ages(psaves, "F"))
    v.value("the effects after the cast", effects,
            {L.name_of(L.sav(psaves, "F", n)): list(L.sfx(psaves, "F", n)) for n in range(1, 7)})
    L.judge_games(v, "saved F", odir, "F", psaves, "F")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

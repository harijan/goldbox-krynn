#!/usr/bin/env python3
"""Check #49: Haste ages each member hasted a year, once (README, Checks
against the original: "Cast Haste with a mage of level 4: each member
hasted should 'age' a year, once (its value's bit 4)").

Game A, GARIN (fighter/mage) made a level 4 mage who knows Haste (0x30,
+0x92) and has it memorized (+0x1e), in the outpost inn's camp: Magic, Cast,
Haste (Enter: the list's highlighted row), Exit, Exit, Save as F. The
original's ages (+0x60) and effects are read in memory after the cast, and
its saved game compared with the port's.
"""
import argparse
import pathlib
import struct
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_magic_lib as M  # noqa: E402

KEYS = r"\v\vMC\r"          # GARIN selected, Magic, Cast, the highlighted spell


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-haste-"))
    game = L.copy_game(L.SAVE, "A", work / "game")
    L.patch(game / "CHRDATA3.SAV", {0xFE: 4, 0x62 + 0x30: 1, 0x1E: 0x30})
    ages0 = {L.name_of(L.sav(game, "A", n)): struct.unpack_from("<H", L.sav(game, "A", n), 0x60)[0]
             for n in range(1, 7)}
    print("ages before:", ages0)
    seen = {}

    def probe(box, i):
        if i == 0:
            for _, raw in L.members(box):
                seen[L.name_of(raw)] = struct.unpack_from("<H", raw, 0x60)[0]
            print("original after the cast, ages:", seen)
            for addr, raw in L.members(box):
                print("  ", L.name_of(raw), [e[:4] for e in L.effects_at(box, addr + 0xE3)])

    out, psaves, odir, _ = M.run_both(work, game, KEYS + "EE", steps=[KEYS, "EE"], probe=probe,
                                      verbose=args.verbose)
    print("port:", " / ".join(ln[7:] for ln in M.lines(out, ("print",))
                                 if "ages" in ln or "Hasted" in ln or "casts" in ln))
    port_ages = {L.name_of(L.sav(psaves, "F", n)): struct.unpack_from("<H", L.sav(psaves, "F", n), 0x60)[0]
                 for n in range(1, 7)}
    print("port's saved ages:", port_ages)
    print("saved F compared with the port's F:")
    differ = L.compare_games(odir, "F", psaves, "F", report=lambda s: print("  " + s))
    status = 1 if differ or seen != port_ages else 0
    print("AGREES" if status == 0 else "PORT DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

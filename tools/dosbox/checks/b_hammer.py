#!/usr/bin/env python3
"""Check #51: the Spiritual Hammer readied and unreadied (README, Checks
against the original: "Ready the Spiritual Hammer a cleric conjured, then
unready it: it should vanish (power 0 removes effect 0x17)").

Game A, KAL (cleric/mage) made to know Spiritual Hammer (0x1c, +0x7e) and
have it memorized (+0x1e), his mace unreadied in the file so the weapon
hand is free, in the outpost inn's camp: Magic, Cast, Spiritual Hammer
(the list's highlighted row), Exit, Exit; View, Items, the fifth item (the
hammer), Ready, Ready again, Escape, Exit; Save as F. KAL's items and
effects are read in the original's memory after each step, and the saved
game compared with the port's.
"""
import argparse
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_magic_lib as M  # noqa: E402

STEPS = [("cast", r"\v\v\v\vMC\rEE"), ("ready", r"VI\v\v\v\vR"), ("unready", "R"),
         ("leave View", r"\eE")]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-hammer-"))
    game = L.copy_game(L.SAVE, "A", work / "game")
    L.patch(game / "CHRDATA5.SAV", {0x62 + 0x1C: 1, 0x1E: 0x1C})
    L.patch(game / "CHRDATA5.STF", {2 * 63 + 0x34: 0})
    seen = []

    def probe(box, i):
        addr, raw = L.member(box, "KAL")
        items = [(it[1:1 + min(it[0], 40)].decode("latin-1").strip(), it[0x34], it[0x3d], it[0x3e])
                 for _, it in L.items_of(box, addr)]
        effects = [e[:4] for e in L.effects_at(box, addr + 0xE3)]
        what = STEPS[i][0] if i >= 0 else "in camp"
        seen.append((what, items, effects))
        print(f"original after {what}: KAL's items {items}\n    effects {effects}")

    out, psaves, odir, _ = M.run_both(work, game, "".join(k for _, k in STEPS),
                                      steps=[k for _, k in STEPS], probe=probe,
                                      verbose=args.verbose)
    items = [ln for ln in out.splitlines() if "Hammer" in ln or "Gains" in ln]
    print("port:", items)
    print("port's saved KAL: items", len((psaves / "CHRDATF5.STF").read_bytes()) // 63,
          "effects", L.sfx(psaves, "F", 5))
    print("saved F compared with the port's F:")
    differ = L.compare_games(odir, "F", psaves, "F", report=lambda s: print("  " + s))
    gone = not any("Hammer" in name for name, *_ in seen[-1][1])
    print(f"the hammer {'vanished' if gone else 'stayed'} in the original")
    status = 1 if differ or not gone else 0
    print("AGREES" if status == 0 else "PORT DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

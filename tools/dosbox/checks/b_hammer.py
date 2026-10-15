#!/usr/bin/env python3
"""Check #51: the Spiritual Hammer readied and unreadied (README, Checks
against the original: "Ready the Spiritual Hammer a cleric conjured, then
unready it: it should vanish (power 0 removes effect 0x17)").

Game A, KAL (cleric/mage) made to know Spiritual Hammer (0x1c, +0x7e) and
have it memorized (+0x1e), his mace unreadied in the file so the weapon
hand is free, in the outpost inn's camp: Magic, Cast, Spiritual Hammer
(the list's highlighted row), Exit, Exit; View, Items, the fifth item (the
hammer), Ready, Ready again, Escape, Exit; Save as F. KAL's items (name,
+0x34, +0x3d, +0x3e) and effects (id, minutes, value, on removal) are read
in the original's memory after each step. After the cast (in camp) and at
the end they must be the port's (its game saved there); within View, where
the port cannot save, the original's must be those recorded: the hammer
readied and a permanent 0x17 added, then the hammer gone. The saved game
must be the port's, masked.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_magic_lib as M  # noqa: E402
import checklib  # noqa: E402

STEPS = [("cast", r"\v\v\v\vMC\rEE"), ("ready", r"VI\v\v\v\vR"), ("unready", "R"),
         ("leave View", r"\eE")]


def is_hammer(item):
    """The conjured hammer: +0x3d 0x17 (its effect), +0x3e 0x80."""
    return item[2:] == (0x17, 0x80)


def port_state(saves):
    """KAL's items and effects as the port saved them, as probe() reads them."""
    st = (saves / "CHRDATF5.STF").read_bytes()
    items = [(st[i + 1:i + 1 + min(st[i], 40)].decode("latin-1").strip(), st[i + 0x34],
              st[i + 0x3D], st[i + 0x3E]) for i in range(0, len(st), 63)]
    return items, list(L.sfx(saves, "F", 5))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-hammer")
    game = L.copy_game(L.SAVE, "A", work / "game")
    L.patch(game / "CHRDATA5.SAV", {0x62 + 0x1C: 1, 0x1E: 0x1C})
    L.patch(game / "CHRDATA5.STF", {2 * 63 + 0x34: 0})
    seen = {}

    def probe(box, i):
        addr, raw = L.member(box, "KAL")
        items = [(it[1:1 + min(it[0], 40)].decode("latin-1").strip(), it[0x34], it[0x3d], it[0x3e])
                 for _, it in L.items_of(box, addr)]
        effects = [e[:4] for e in L.effects_at(box, addr + 0xE3)]
        what = STEPS[i][0] if i >= 0 else "in camp"
        seen[what] = (items, effects)
        print(f"original after {what}: KAL's items {items}\n    effects {effects}")

    keys = "".join(k for _, k in STEPS)
    out, psaves, odir, _, fixed = M.run_both(work, game, keys, steps=[k for _, k in STEPS],
                                             probe=probe, verbose=args.verbose)
    v = checklib.Verdict()
    print("port:", [ln for ln in out.splitlines() if "Hammer" in ln or "Gains" in ln])
    v.check(any(is_hammer(it) for it in seen["cast"][0]), "the original's cast adds the hammer",
            str(seen["cast"][0]))
    cast_saves, _ = L.saved_by_port(game, "A", STEPS[0][1], work / "port-cast")
    v.value("KAL's items and effects after the cast", seen["cast"], port_state(cast_saves))
    items, effects = seen["ready"]
    hammer = [it for it in items if is_hammer(it)]
    v.check(len(hammer) == 1 and hammer[0][1] != 0 and
            sum(1 for e in effects if e[0] == 0x17 and e[2] == 0xFF) == 1,
            "the original readies the hammer and adds a permanent 0x17, as recorded (the port "
            "is not compared within View)", f"{hammer}, {effects}")
    v.check(not any(is_hammer(it) for it in seen["unready"][0]),
            "the hammer vanishes when unreadied in the original, as recorded (the port: the "
            "end below)", str(seen["unready"][0]))
    v.value("KAL's items and effects at the end", seen["leave View"], port_state(psaves))
    L.judge_games(v, "saved F", odir, "F", psaves, "F", fixed=fixed)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

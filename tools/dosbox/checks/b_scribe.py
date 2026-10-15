#!/usr/bin/env python3
"""Check #41: Scribe marks the first item holding the spell's id, scroll or
not (DEVELOPERS.md, Checks against the original: "With a magic-user of an order
carrying a scroll and another item whose +0x3c-+0x3e hold the scroll's
spell id, scribe it: the other item is marked, and the scroll is not
used").

Game A, GARIN (fighter/mage of order 2, mage level 2, who does not know
Friends, 0x0e) given a Red MU scroll of Friends (type 0x27, +0x35 0x10,
+0x3c 0x0e) as a sixth item, and his Long Sword (the third) +0x3c 0x0e,
in the files. In the outpost inn's camp: Magic, Scribe, Friends (Enter),
Exit; Rest, Hours, Add six times, Rest; Exit; Save as F. GARIN's items'
+0x3c-+0x3e and his spell 0x0e (+0x70) are read in the original's memory
after each step: the sword must be marked (0x8e) and the scroll keep its
0x0e, spell 0x0e still unknown, and each step's state the port's (its game
saved there); the saved game must be the port's, masked.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_magic_lib as M  # noqa: E402
import checklib  # noqa: E402

SPELL = 0x0E
STEPS = [("Scribe", r"\v\vMS\rE"), ("a rest of 6 hours", "RHAAAAAAR"), ("Exit", "E")]


def scroll():
    item = bytearray(63)
    name = b"Red MU Scroll 1 Spell"
    item[0] = len(name)
    item[1:1 + len(name)] = name
    item[0x2E] = 0x27            # a magic-user's scroll
    item[0x2F:0x33] = bytes([0x64, 0x28, 0x78, 1])   # "1 Spell" "MU Scroll" "Red", bonus 1
    item[0x35] = 0x10            # order 2, nothing hidden
    struct.pack_into("<H", item, 0x37, 25)
    struct.pack_into("<H", item, 0x3A, 150)
    item[0x3C] = SPELL
    return bytes(item)


def port_state(saves, letter="F"):
    """GARIN's items (type, +0x3c-3e) and spell 0x0e in a game the port saved."""
    st = (saves / f"CHRDAT{letter}3.STF").read_bytes()
    return ([(st[i + 0x2E], st[i + 0x3C:i + 0x3F].hex()) for i in range(0, len(st), 63)],
            L.sav(saves, letter, 3)[0x62 + SPELL])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-scribe")
    game = L.copy_game(L.SAVE, "A", work / "game")
    stf = game / "CHRDATA3.STF"
    stf.write_bytes(stf.read_bytes() + scroll())
    L.patch(stf, {2 * 63 + 0x3C: SPELL})
    L.patch(game / "CHRDATA3.SAV", {0x142: 6})
    seen = []

    def probe(box, i):
        addr, raw = L.member(box, "GARIN ")
        items = [(it[0x2E], it[0x3C:0x3F].hex()) for _, it in L.items_of(box, addr)]
        what = STEPS[i][0] if i >= 0 else "in camp"
        seen.append((items, raw[0x62 + SPELL]))
        print(f"original after {what}: GARIN's items (type, +0x3c-3e) {items}, knows 0x0e: "
              f"{raw[0x62 + SPELL]}, clock {L.clock(box)}")

    out, psaves, odir, _ = M.run_both(work, game, "".join(k for _, k in STEPS),
                                      steps=[k for _, k in STEPS], probe=probe,
                                      rests={1}, verbose=args.verbose)
    v = checklib.Verdict()
    for i, (what, _) in enumerate(STEPS):
        items, knows = seen[i + 1]
        v.check(items[2][1] == "8e0000" and items[5][1] == "0e0000" and knows == 0,
                f"after {what}: the sword marked, the scroll unused and 0x0e unknown in the "
                "original", f"{items}, knows {knows}")
        # The first two steps end in the Magic menu: Exit to the camp's to save.
        saves = L.saved_by_port(game, "A", "".join(k for _, k in STEPS[:i + 1]) +
                                ("E" if i < 2 else ""), work / f"port-{i}")
        v.value(f"GARIN's items and spell 0x0e after {what}", seen[i + 1], port_state(saves))
    L.judge_games(v, "saved F", odir, "F", psaves, "F")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

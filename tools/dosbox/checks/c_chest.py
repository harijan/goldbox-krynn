#!/usr/bin/env python3
"""The treasure menu (README, Checks against the original): #58 "With a
party of six, open the chest of ECL1 block 32 (250 steel, 5 gems and 3
jewels): the results should say each character receives 1391 experience
points ... Compare the results screen and the treasure menu"; #59 "In that
menu, Take: the coins should list jewelry first, and the question read `How
much Jewelry  will you take? ` with two spaces"; #60 "Share right away: of
the 5 gems, the first five characters should get one each, and the sixth
none"; #61 "Leave with coins in the pool: `There is still treasure left.`
and the question should show, and `~Yes ~No` should start on the item the
menu before had selected, not on No"; #62 "With a cleric who has Detect
Magic memorized, `Detect` in the treasure menu: no "casts" message, and the
items with a bonus should show `* `".

Game A's party of six in a saved game the port writes inside Throtl, moved
in the file to 12,4 facing south (special 4, quiet with var 0x4c4a bit 4),
north of the chest's room (special 12 at 12,5: 8b86, TREASURE 0 0 0 0 250
5 3 255 and COMBAT with no monsters). For #62, a second save at 4,7 facing
west of the scroll (special 20 at 3,7: TREASURE ... 32, a Red Scroll) with
KAL (fifth, a cleric) given Detect Magic (5) in his first spell byte; four
down arrows select him in the treasure menu.
"""
import argparse
import sys

import c_places as cp

TO_CHEST = r"m\^"


def coins(box):
    return [(cp.name_of(raw), cp.money(raw)) for _, raw in cp.party_records(box)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("chest", args.workdir)
    status = 0
    saves, _ = cp.throtl_save(work / "save-g", "G", at=(12, 4, 4), var={0x4C4A: 4})

    # #58, #59, #61: the results, the menu, Take, then Exit with money left.
    a = cp.Compare(work / "take", saves, [
        ("loaded", cp.load_keys("G")),
        ("enter", TO_CHEST),        # AS YOU ENTER SOMEONE IS LEAVING ...
        ("results", r"\r"),         # #58: found Treasure, 1391 each
        ("menu", r"\r"),            # #58: View Take Pool Share Exit
        ("take", "T"),              # #59: the coins, jewelry first
        ("howmuch", r"\r"),         # #59: How much Jewelry  will you take?
        ("back", r"\e"),            # 0 taken: the menu again
        ("leave", "E"),             # #61: There is still treasure left. ~Yes ~No
    ], seed=1, verbose=args.verbose)
    status |= a.run()
    print("#58 port:", [l for l in a.port_text.splitlines() if "receives" in l])

    # #60: Share at once.
    b = cp.Compare(work / "share", saves, [
        ("loaded", cp.load_keys("G")),
        ("menu", TO_CHEST + r"\r\r", coins),
        ("share", "S", coins),
    ], seed=1, verbose=args.verbose)
    status |= b.run()
    print("#60 original's coins before Share:", b.probes["menu"])
    print("#60 original's coins after Share:", b.probes["share"])
    _, psaves, _ = cp.cokbox.eclplay(cp.load_keys("G") + TO_CHEST + r"\r\rSEeESI",
                                     work / "share-port-save", saves=[saves], shots=False,
                                     args=["--seed", "1"])
    print("#60 port's coins after Share (saved in camp):",
          [(cp.name_of(d), cp.money(d)) for d in
           ((psaves / f"CHRDATI{i}.SAV").read_bytes() for i in range(1, 7))])

    # #62: Detect with KAL.
    saves, _ = cp.throtl_save(work / "save-h", "H", at=(4, 7, 6))
    path = saves / "CHRDATH5.SAV"
    rec = bytearray(path.read_bytes())
    rec[0x1E] = 5
    path.write_bytes(bytes(rec))
    c = cp.Compare(work / "detect", saves, [
        ("loaded", cp.load_keys("H")),
        ("menu", r"m\^\r"),
        ("kal", r"\v\v\v\v"),       # View Take Pool Detect Exit
        ("detect", "D"),
        ("items", "T"),             # the pool's items, a bonus marked "* "
    ], seed=1, verbose=args.verbose)
    status |= c.run()
    print("#62 port:", [l for l in c.port_text.splitlines()[-12:]])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

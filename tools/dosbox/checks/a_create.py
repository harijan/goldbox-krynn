#!/usr/bin/env python3
"""Check: character creation (README, Checks against the original: "Create
characters: no score should ever show a racial adjustment ..." and "Create a
knight: its Plate Mail, Shield and Long Sword should be carried but not
readied ...").

For each character below the original in DOSBox-X and eclplay create it
from the title with the same seed (RandSeed set at the first wait, as
eclplay --seed): every sheet drawn (each roll and reroll) is compared with
eclplay's best frame, and the roster files it is saved to (NAME.WHO,
.STF, .SFX) byte for byte, the far pointers masked. The knight is then
added to the party and the game saved: the record loaded again carries
its items' weight (+0x17d) and the items stay unreadied (+0x34).
"""
import argparse
import struct
import sys

import a_common as A

# Pick Race's rows, and Pick Class's for each race (eclplay's lists).
RACES = ["Silvanesti Elf", "Qualinesti Elf", "Half-elf", "Mountain Dwarf", "Hill Dwarf",
         "Kender", "Human"]
# (name, race, class row, god row or None, alignment row, seed, rerolls, what)
CASES = [
    ("DWARF", 3, 1, None, 0, 7, 2, "a mountain dwarf fighter: constitution not raised"),
    ("ELF", 0, 2, None, 0, 11, 2, "a Silvanesti elf mage: dexterity not raised"),
    ("TRIPLE", 1, 11, None, 0, 5, 1, "a Qualinesti fighter/mage/thief: money not divided"),
    ("YOUNG", 6, 0, 0, 0, 0, 0, "a human cleric of 19: wisdom less one"),
    ("OLDER", 6, 0, 0, 0, 2, 0, "a human cleric of 21: wisdom as rolled"),
    ("THIEF", 6, 3, None, 0, 3, 1, "a human thief: thief skills"),
    ("KNIGHT", 6, 5, None, 0, 9, 0, "a human knight: items carried, not readied"),
    ("KNIGHT2", 6, 5, None, 0, 9, 0, "the same knight added to the party and saved"),
]


def create_keys(name, race, cls, god, align, rerolls):
    down = lambda n: r"\v" * n  # noqa: E731
    keys = r"\r" + down(race) + r"\r\r" + down(cls) + r"\r"
    if god is not None:
        keys += down(god) + r"\r"
    keys += down(align) + r"\r" + rf"\s{name}-0."
    for k in range(rerolls):
        keys += rf"Y\s{name}-{k + 1}."
    return keys + "N" + name + r"\r" + rf"\s{name}-named." + r"\eYY"


def fields(who):
    scores = [who[0x10 + 2 * i] for i in range(6)]
    age, = struct.unpack_from("<H", who, 0x60)
    coins = struct.unpack_from("<6H", who, 0xED)
    weight, = struct.unpack_from("<H", who, 0x17D)
    return (f"scores (base) {scores}, exceptional {who[0x1c]}, age {age}, coins {list(coins)}, "
            f"thief skills {list(who[0xdb:0xe3])}, items {who[0x142]}, weight {weight}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", help="only these names")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "create")
    bad = 0
    for name, race, cls, god, align, seed, rerolls, what in CASES:
        if args.case and name not in args.case:
            continue
        print(f"{name} ({what}), seed {seed}:")
        keys = r"\kxP" + create_keys(name, race, cls, god, align, rerolls) + r"\sback."
        if name == "KNIGHT2":
            # Add it to the party (Add, the first row), View it, save as game A.
            keys += r"\v\r\r\e\sadded." + r"\v\v\v\v\v\rA\ssaved."
        pair = A.Pair(keys, work / name, seed=seed, verbose=args.verbose)
        with pair:
            pair.run()
            screens = sorted(p.stem for p in (pair.work / "shots").glob("*.png"))
            differ = sum(1 for s in screens if pair.compare(s)[0])
            orig, port = pair.box_files(), pair.port_files()
            if name == "KNIGHT2":
                box = pair.box
                rec = box.far(box.ds * 16 + 0x609A)
                raw = box.read(rec, 409)
                items = []
                item = box.far(rec + 0x143)
                while item is not None and len(items) < 10:
                    it = box.read(item, 63)
                    items.append((it[0x2e], it[0x34], struct.unpack_from("<H", it, 0x37)[0]))
                    item = box.far(item + 0x2a)
                print(f"  in memory after Add: items (type, readied, weight) {items}; "
                      f"weight carried +0x17d {struct.unpack_from('<H', raw, 0x17d)[0]}")
        who = orig.get(f"{name}.WHO")
        if who:
            print("  original's", fields(who))
        if port.get(f"{name}.WHO"):
            print("  port's    ", fields(port[f"{name}.WHO"]))
        if name.startswith("KNIGHT"):
            for label, files in (("original", orig), ("port", port)):
                stf = files.get("KNIGHT.STF") or files.get("CHRDATA1.STF", b"")
                print(f"  {label}'s KNIGHT.STF items (type, readied +0x34, weight): " +
                      str([(stf[i + 0x2e], stf[i + 0x34], struct.unpack_from("<H", stf, i + 0x37)[0])
                           for i in range(0, len(stf), 63)]))
                sav = files.get("CHRDATA1.SAV")
                if sav:
                    print(f"  {label}'s CHRDATA1.SAV (loaded again by Add): weight carried "
                          f"{struct.unpack_from('<H', sav, 0x17d)[0]}, items {sav[0x142]}")
        fdiff = A.compare_files(orig, port, A.mask_saved)
        print(f"  {len(screens)} screens, {differ} differ; {fdiff} files differ")
        bad += differ + fdiff
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS (see above)", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

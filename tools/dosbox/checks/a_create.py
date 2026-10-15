#!/usr/bin/env python3
"""Checks #110 and #111: character creation (DEVELOPERS.md, Checks against the original: "Create
characters: no score should ever show a racial adjustment ..." and "Create a
knight: its Plate Mail, Shield and Long Sword should be carried but not
readied ...").

For each character below the original in DOSBox-X and eclplay create it
from the title with the same seed (RandSeed set at the first wait, as
eclplay --seed): every sheet drawn (each roll and reroll) is compared with
the port's at the same key read, and the roster files it is saved to
(NAME.WHO, .STF, .SFX) byte for byte, the far pointers masked. The knight's
items (type, readied +0x34, weight) are compared, and must be three and
unreadied; it is then added to the party: the record in memory, its weight
carried (+0x17d) and its items against the port's at the same key read,
and the game saved and its files compared.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

# Pick Race's rows: 0 Silvanesti Elf, 1 Qualinesti Elf, 2 Half-elf, 3 Mountain
# Dwarf, 4 Hill Dwarf, 5 Kender, 6 Human; Pick Class's for each race.
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


def items(raw_items):
    """(type +0x2e, readied +0x34, weight +0x37) of each item."""
    return [(it[0x2E], it[0x34], struct.unpack_from("<H", it, 0x37)[0]) for it in raw_items]


def stf_items(stf):
    return items([stf[i:i + 63] for i in range(0, len(stf), 63)])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", help="only these names")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "create")
    v = checklib.Verdict()
    for name, race, cls, god, align, seed, rerolls, what in CASES:
        if args.case and name not in args.case:
            continue
        print(f"{name} ({what}), seed {seed}:")
        keys = r"\kxP" + create_keys(name, race, cls, god, align, rerolls) + r"\sback."
        if name == "KNIGHT2":
            # Add it to the party (Add, the first row), View it, save as game A.
            keys += r"\v\r\r\e\sadded." + r"\v\v\v\v\v\rA\ssaved."
        pair = A.Pair(keys, work / name, seed=seed, verbose=args.verbose)
        added = {}
        with pair:
            pair.steps(lambda mark: added.__setitem__(mark, A.party_records(pair.box))
                       if mark == "added" else None)
            orig, port = pair.box_files(), pair.port_files()
        for mark in pair.marks():
            pair.judge(v, mark, what=f"{name}, screen {mark}")
        for label, files in (("original", orig), ("port", port)):
            if files.get(f"{name}.WHO"):
                print(f"  {label}'s", fields(files[f"{name}.WHO"]))
        if name.startswith("KNIGHT"):
            stf = {label: stf_items(files.get("KNIGHT.STF") or files.get("CHRDATA1.STF", b""))
                   for label, files in (("original", orig), ("port", port))}
            v.value(f"{name}: the knight's items (type, readied +0x34, weight)", stf["original"],
                    stf["port"])
            v.check(len(stf["original"]) == 3 and not any(r for _, r, _ in stf["original"]),
                    f"{name}: three items, none readied, in the original")
        if name == "KNIGHT2":
            probe = pair.probes()["added"]
            (raw, its), = added["added"]
            (praw, pits), = probe.members
            v.value("KNIGHT2 in memory after Add: weight carried +0x17d",
                    struct.unpack_from("<H", raw, 0x17D)[0], struct.unpack_from("<H", praw, 0x17D)[0])
            v.value("KNIGHT2 in memory after Add: its items (type, readied, weight)",
                    items(its), items(pits))
        A.judge_files(v, orig, port, A.mask_saved, what=f"{name}: file")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

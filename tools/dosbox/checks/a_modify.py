#!/usr/bin/env python3
"""Check: Modify Character (README, Checks against the original: "Modify a
character's name typing an upper-case `K`: Modify should keep and end.
Change the wisdom of a cleric with two spells a day and exit: it should keep
one until it is loaded again.").

A human cleric of Paladine is created with seed 1 (wisdom 13, two spells a
day, +0x11c 2), added to the party and modified twice: the name field
(six fields down), `K`; then wisdom (two down), Add, Exit. After each step
the record is read in the original's memory and the screen compared with
eclplay's; the game is then saved as A in both and the files compared.
"""
import argparse
import sys

import a_common as A
from a_create import create_keys

STEPS = [
    ("created", r"\kxP" + create_keys("ABE", 6, 0, 0, 0, 0) + r"\v\r\r\e"),  # create, Add, Escape
    ("name", r"\v\r\r\v\v\v\v\v\v"),      # Modify (row 3), the cleric, down to the name
    ("typed-K", "K"),
    ("wisdom", r"\r\r\v\v"),              # Modify again, the cleric, down to wisdom
    ("wisdom-added", "A"),
    ("exited", "E"),
    ("saved", r"\v\v\v\v\rA"),            # Save Current Game (row 7) as A
]


def record(box):
    rec = box.far(box.ds * 16 + 0x609A)
    raw = box.read(rec, 409)
    return (f"name {raw[1:1 + raw[0]].decode('latin-1')!r}, wisdom {raw[0x14]}/{raw[0x15]}, "
            f"spells a day +0x11c {raw[0x11c]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "modify")
    keys = "".join(k + rf"\s{name}." for name, k in STEPS)
    pair = A.Pair(keys, work, seed=1, verbose=args.verbose)
    print("port: " + " | ".join(line for line in pair.out.strip().splitlines()
                                if line.startswith(("menu: Modify", "menu: Add", "choice", "input",
                                                    "roster", "party"))))
    bad = 0
    with pair:
        for name, k in STEPS:
            pair.run(k)
            pair.shot(name)
            print(f"  {name}: the original waits at {pair.box.where()}; {record(pair.box)}")
            bad += pair.compare(name)[0] != 0
        orig, port = pair.box_files(), pair.port_files()
    for label, files in (("original", orig), ("port", port)):
        sav = files.get("CHRDATA1.SAV")
        if sav:
            print(f"  {label}'s CHRDATA1.SAV: name {sav[1:1 + sav[0]].decode('latin-1')!r}, "
                  f"wisdom {sav[0x14]}/{sav[0x15]}, +0x11c {sav[0x11c]}")
    bad += A.compare_files(orig, port, A.mask_saved)
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS / see above", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

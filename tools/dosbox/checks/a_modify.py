#!/usr/bin/env python3
"""Check #118: Modify Character (README, Checks against the original:
"Modify a character's name typing an upper-case `K`: Modify should keep and
end. Change the wisdom of a cleric with two spells a day and exit: it should
keep one until it is loaded again.").

A human cleric of Paladine is created with seed 1 (wisdom 13, two spells a
day, +0x11c 2), added to the party and modified twice: the name field
(six fields down), `K`; then wisdom (two down), Add, Exit. After each step
the screen, and the record's name, wisdom and +0x11c read in the
original's memory, are compared with the port's at the same key read; the
game is then saved as A in both and the files compared.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402
from a_create import create_keys  # noqa: E402

STEPS = [
    ("created", r"\kxP" + create_keys("ABE", 6, 0, 0, 0, 0) + r"\v\r\r\e"),  # create, Add, Escape
    ("name", r"\v\r\r\v\v\v\v\v\v"),      # Modify (row 3), the cleric, down to the name
    ("typed-K", "K"),
    ("wisdom", r"\r\r\v\v"),              # Modify again, the cleric, down to wisdom
    ("wisdom-added", "A"),
    ("exited", "E"),
    ("saved", r"\v\v\v\v\rA"),            # Save Current Game (row 7) as A
]


def fields(raw):
    """What #118 rests on: the name, the wisdom (+0x14 and +0x15) and the
    spells a day's byte +0x11c."""
    return (A.name_of(raw), raw[0x14], raw[0x15], raw[0x11C])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "modify")
    keys = "".join(k + rf"\s{name}." for name, k in STEPS)
    pair = A.Pair(keys, work, seed=1, verbose=args.verbose)
    probes = pair.probes()
    v = checklib.Verdict()
    records = {}
    with pair:
        pair.steps(lambda mark: records.__setitem__(mark, A.party_records(pair.box)))
        orig = pair.box_files()
    for name, _ in STEPS:
        pair.judge(v, name)
        mine = [fields(raw) for raw, _ in records[name]]
        port = [fields(raw) for raw, _ in probes[name].members]
        v.value(f"{name}: the party's (name, wisdom, wisdom, +0x11c)", mine, port)
    A.judge_files(v, orig, pair.port_files(), A.mask_saved, pair.fixed_files)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

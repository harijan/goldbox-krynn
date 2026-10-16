#!/usr/bin/env python3
"""Checks #116 and #117: the training hall (DEVELOPERS.md, Checks against the
original: "In a training hall, train a character with experience past the
second next level, and answer No: the experience should be cut all the
same." and "At Knight Change Classes, press F9 for a knight who is too
inexperienced: the order should change.").

SAVE/'s game A, with ISTAN HORBIN's experience (CHRDATA2.SAV +0x116, a
level 2 ranger) set to 200,000, is loaded from the start menu and begun;
the outpost's HALL (ECL1 block 17, PROGRAM 0) opens the start menu with var
0x7ea8 set. Train ISTAN and answer No; Knight Change Classes for SIR
STRONGSWORD (a level 1 knight of the Crown, 2,500 experience) and F9. At
each step the screen, and the member's experience, order and levels read in
the original's memory, are compared with the port's at the same key read;
the game is saved as A in both and the files compared.
"""
import argparse
import pathlib
import shutil
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

TO_HALL = r"\kxP\v\v\rA\v\v\v\v\v\r" + r"\r\r\r\r\r\r\rH\shall."
CASES = {
    # Train (row 4), ISTAN (the second), No; then Save (row 9) as A.
    "train": TO_HALL + r"\v\v\v\r\v\r\swill-become.N\strained." + r"\v\v\v\v\v\rA\ssaved.",
    # Knight Change Classes (row 5), SIR STRONGSWORD, F9; then Save as A.
    "knight": TO_HALL + r"\v\v\v\v\r\r\spetition.\#43\schanged." + r"\v\v\v\v\rA\ssaved.",
}
EXPERIENCE = 200000


def fields(raw):
    """Name, experience (+0x116), order (+0x5c) and levels (+0xf9)."""
    return (A.name_of(raw), struct.unpack_from("<I", raw, 0x116)[0], raw[0x5C],
            list(raw[0xF9:0x101]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "hall")
    saves = work / "saves"
    shutil.rmtree(saves, ignore_errors=True)
    saves.mkdir(parents=True)
    for f in A.SAVES[0].iterdir():
        if f.is_file() and not f.is_symlink():
            shutil.copyfile(f, saves / f.name)
    sav = bytearray((saves / "CHRDATA2.SAV").read_bytes())
    struct.pack_into("<I", sav, 0x116, EXPERIENCE)
    (saves / "CHRDATA2.SAV").write_bytes(sav)
    v = checklib.Verdict()
    for name, keys in CASES.items():
        if args.case and name not in args.case:
            continue
        print(f"{name}: keys {keys}")
        pair = A.Pair(keys, work / name, saves=[saves], verbose=args.verbose)
        probes = pair.probes()
        member = 1 if name == "train" else 0
        records = {}
        with pair:
            pair.steps(lambda mark: records.__setitem__(mark, A.party_records(pair.box)))
            orig = pair.box_files()
        for mark in pair.marks():
            pair.judge(v, mark, what=f"{name}, screen {mark}")
            v.value(f"{name}, {mark}: member {member + 1}'s (name, experience, order, levels)",
                    fields(records[mark][member][0]), fields(probes[mark].members[member][0]))
        A.judge_files(v, orig, pair.port_files(), A.mask_saved,
                      what=f"{name}: file")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

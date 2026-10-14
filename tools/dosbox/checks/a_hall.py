#!/usr/bin/env python3
"""Check: the training hall (README, Checks against the original: "In a
training hall, train a character with experience past the second next
level, and answer No: the experience should be cut all the same." and "At
Knight Change Classes, press F9 for a knight who is too inexperienced: the
order should change.").

SAVE/'s game A, with ISTAN HORBIN's experience (CHRDATA2.SAV +0x116, a
level 2 ranger) set to 200,000, is loaded from the start menu and begun;
the outpost's HALL (ECL1 block 17, PROGRAM 0) opens the start menu with var
0x7ea8 set. Train ISTAN and answer No; Knight Change Classes for SIR
STRONGSWORD (a level 1 knight of the Crown, 2,500 experience) and F9. The
records are read in the original's memory after each, the screens compared
with eclplay's, and the game saved as A in both and the files compared.
"""
import argparse
import shutil
import struct
import sys

import a_common as A

TO_HALL = r"\kxP\v\v\rA\v\v\v\v\v\r" + r"\r\r\r\r\r\r\rH\shall."
CASES = {
    # Train (row 4), ISTAN (the second), No; then Save (row 9) as A.
    "train": TO_HALL + r"\v\v\v\r\v\r\swill-become.N\strained." + r"\v\v\v\v\v\rA\ssaved.",
    # Knight Change Classes (row 5), SIR STRONGSWORD, F9; then Save as A.
    "knight": TO_HALL + r"\v\v\v\v\r\r\spetition.\#43\schanged." + r"\v\v\v\v\rA\ssaved.",
}
EXPERIENCE = 200000


def record(box, n):
    """Party member N's (0 the first) name, experience, order and levels."""
    rec = box.far(box.ds * 16 + 0x609A)
    for _ in range(n):
        rec = box.far(rec + 0x17F)
    raw = box.read(rec, 409)
    return (raw[1:1 + raw[0]].decode("latin-1"), struct.unpack_from("<I", raw, 0x116)[0],
            raw[0x5C], list(raw[0xF9:0x101]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "hall")
    saves = work / "saves"
    shutil.rmtree(saves, ignore_errors=True)
    saves.mkdir(parents=True)
    for f in (A.cokbox.ROOT / "SAVE").iterdir():
        if f.is_file() and not f.is_symlink():
            shutil.copyfile(f, saves / f.name)
    sav = bytearray((saves / "CHRDATA2.SAV").read_bytes())
    struct.pack_into("<I", sav, 0x116, EXPERIENCE)
    (saves / "CHRDATA2.SAV").write_bytes(sav)
    bad = 0
    for name, keys in CASES.items():
        if args.case and name not in args.case:
            continue
        print(f"{name}: keys {keys}")
        pair = A.Pair(keys, work / name, saves=[saves], verbose=args.verbose)
        print("  port: " + " | ".join(line for line in pair.out.strip().splitlines()[-40:]
                                      if not line.startswith(("item: ", "list: Choose"))))
        member = 1 if name == "train" else 0
        with pair:
            rest = keys
            while r"\s" in rest:
                head, _, tail = rest.partition(r"\s")
                shot, _, rest = tail.partition(".")
                pair.run(head)
                pair.shot(shot)
                print(f"  {shot}: the original waits at {pair.box.where()}; member {member + 1}: "
                      f"{record(pair.box, member)}; var 0x7ea8 "
                      f"{pair.box.word(pair.box.far(pair.box.ds * 16 + 0x4B2C) + 0x2A8 * 2):#x}")
                bad += pair.compare(shot)[0] != 0
            orig, port = pair.box_files(), pair.port_files()
        for label, files in (("original", orig), ("port", port)):
            f = files[f"CHRDATA{member + 1}.SAV"]
            print(f"  {label}'s saved CHRDATA{member + 1}.SAV: experience "
                  f"{struct.unpack_from('<I', f, 0x116)[0]}, order {f[0x5c]}, levels "
                  f"{list(f[0xf9:0x101])}")
        bad += A.compare_files(orig, port, A.mask_saved)
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS / see above", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

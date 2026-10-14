#!/usr/bin/env python3
"""The shops of ECL2 block 50 (README, Checks against the original): #67
"In a new game, before visiting any other shop, the weapon smith of ECL2
block 50 should list each weapon at its value and charge 15 steel for any of
them (var `0x7f6d` 0, `36d0:0484`)"; #72 "Buy `10 Arrows +1` (worth 120 at
ECL2 block 50's magic shop ...), halve it down to a stack of 2 and a single
one, and sell both: the stack should fetch about a tenth of the single
one's price (2 x 60 / 20 = 6 against 60 for those of ECL2)"; #103 "In ECL2
block 50, visit the magic shop: the shopkeeper's portrait should fill the
view, the head (`HEAD2` record 69) over the body (`BODY2` record 68); after
`Buy` the redraw should show it again beside the party list. The weapon
smith's shows head 68."

The port writes a saved game in block 50 (game A's party, var 0x4c85 1 so
that Skyla's guide at the gate, special 1, has been met; var 0x7f6d is 0 as
in game A, no shop visited), moved in the file to 4,15 facing north, south
of the weapon smith (special 4 at 4,14), as E; and to 5,12 facing east, west
of the magic shop (special 2 at 6,12), as F; SIR STRONGSWORD given 1000
steel. The original's money is read in memory after each purchase or sale.
"""
import argparse
import shutil
import struct
import sys

import c_geo
import c_places as cp

ARGS = ["--load", cp.SAVE_A, "--play", "--file", "2", "--set", "4be6=1", "--set", "4c85=1"]


def steel(box):
    _, raw = cp.party_records(box)[0]
    return cp.name_of(raw), cp.money(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="smith or magic")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("block50", args.workdir)
    saves, _ = cp.port_save(work / "save", "ESE", ARGS, block=50)
    shutil.copyfile(saves / "SAVGAME.DAT", saves / "SAVGAMF.DAT")
    for f in list(saves.glob("CHRDATE*")):
        shutil.copyfile(f, saves / f.name.replace("CHRDATE", "CHRDATF"))
    geo = c_geo.load(2, 50)
    cp.edit_save(saves, "E", at=(4, 15, 0), geo=geo)
    cp.edit_save(saves, "F", at=(5, 12, 2), geo=geo)
    for letter in "EF":
        path = saves / f"CHRDAT{letter}1.SAV"
        rec = bytearray(path.read_bytes())
        struct.pack_into("<H", rec, 0xF3, 1000)
        path.write_bytes(bytes(rec))
    status = 0

    if args.only in (None, "smith"):
        c = cp.Compare(work / "smith", saves, [
            ("loaded", cp.load_keys("E"), steel),
            ("smith", r"m\^"),              # #103: head 68; DO YOU WISH TO PURCHASE WEAPONS?
            ("shop", "y"),
            ("buy", "B"),                   # #67: each weapon at its value
            ("pick", r"\v\v\v\v"),          # Broad Sword, 12
            ("bought", r"\r", steel),       # #67: 15 steel
        ], seed=1, verbose=args.verbose)
        status |= c.run()
        print("#67 original's SIR STRONGSWORD before and after:", c.probes["loaded"],
              c.probes["bought"])
        print("#67 port:", [l for l in c.port_text.splitlines() if l.startswith("shop:")])

    if args.only in (None, "magic"):
        c = cp.Compare(work / "magic", saves, [
            ("loaded", cp.load_keys("F"), steel),
            ("shop", r"m\^"),               # #103: head 69 over body 68
            ("menu", "y"),
            ("buy", "B"),
            ("arrows", r"\v" * 10),         # 10 Arrows +1, 480 (factor 64)
            ("bought", r"\r", steel),
            ("redraw", r"\e"),              # #103: the portrait again beside the list
            ("view", "V"),
            ("items", "I"),
            ("pick", r"\v\v\v"),
            ("halve1", "H"), ("halve2", "H"), ("halve3", "H"),   # 2, 1, 2, 5
            ("sell2", "S"),                 # #72: the offer for the stack of 2
            ("deal2", "y", steel),
            ("sell1", "S"),                 # #72: the offer for the single one
            ("deal1", "y", steel),
        ], seed=1, verbose=args.verbose)
        status |= c.run()
        print("#72 original's SIR STRONGSWORD's coins: before", c.probes["loaded"], "bought",
              c.probes["bought"], "after selling 2", c.probes["deal2"], "after selling 1",
              c.probes["deal1"])
        print("#72 port:", [l for l in c.port_text.splitlines()
                            if l.startswith("shop:") or "give you" in l])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

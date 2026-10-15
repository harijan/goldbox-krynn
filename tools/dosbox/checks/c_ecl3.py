#!/usr/bin/env python3
"""Treasures of ECL3 (README, Checks against the original): #64 "`Share` the
hoard of ECL3 block 98 (95,000 coins): what the party cannot carry should
stay in the pool, with `Share` still offered"; #65 "Take a random potion
from a treasure of ECL3 block 81 (`TREASURE ... 130`) and drink it:
generated potions are of type 0x30 or 0x35, where the shipped ones are 0x2f;
see whether the game can use them."

#64: the port writes a saved game in ECL3 block 97 (vars 0x4be7, 0x4be8 1
so that LOAD PIECES 17 18 255 loads record 17's two sets; 0x4bf2 97 and
0x4ce5 7 so that the block and the square stay quiet), then the file is
changed to block 98 (var 0x4bf2; the same map, GEO3 97), the party at 6,1
facing east and the step counter (0x4c06) past 4. A step onto 7,1 (special
3, 8485): `YOU HAVE REACHED A TREASURE VAULT.`, TREASURE 0 60000 20000
10000 5000 3 2 96, the results, the treasure menu, Share. The original's
records and pool are read in memory (the pool at DS:6b0c, seven longs).

#65: a saved game in ECL3 block 81 (wandering monsters off, 0x4c0a 0xff),
moved to 14,9 facing west; a step into the bed chamber (special 16, 90ba),
Exit, Look: the treasure under the bed (TREASURE 0 0 0 0 0 3 2 130). Seed
22 generates a Potion. Take it (SIR STRONGSWORD), leave the rest, View,
Items, unready the shield, ready the potion, Use, Select: the original's
item record is read in memory before it is drunk.
"""
import argparse
import struct
import sys

import c_geo
import c_places as cp

POOL = 0x6B0C


def pool(box):
    return list(struct.unpack("<7I", box.ds_read(POOL, 28)))


def coins(box):
    return [cp.money(raw) for _, raw in cp.party_records(box)]


def items_of(box, n=8):
    """SIR STRONGSWORD's items (the list his record's +0x143 points to, each
    63 bytes, linked at +0x2a): name, type +0x2e, readied +0x34."""
    rec, _ = cp.party_records(box)[0]
    out = []
    item = box.far(rec + 0x143)
    while item is not None and len(out) < n:
        raw = box.read(item, 63)
        out.append((raw[1:1 + min(raw[0], 41)].decode("latin-1").strip(), "type %#x" % raw[0x2E],
                    "readied %d" % raw[0x34], raw[0x2E:0x3F].hex()))
        item = box.far(item + 0x2A)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="vault or potion")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("ecl3", args.workdir)
    status = 0

    if args.only in (None, "vault"):
        saves, _ = cp.port_save(work / "save-98", "ESE", [
            "--load", cp.SAVE_A, "--play", "--file", "3", "--set", "4be6=1", "--set", "4be7=1",
            "--set", "4be8=1", "--set", "4bf2=61", "--set", "4ce5=7", "--at", "6,1,2"], block=97)
        cp.edit_save(saves, "E", at=(6, 1, 2), var={0x4BF2: 98, 0x4C06: 5},
                     geo=c_geo.load(3, 97))
        c = cp.Compare(work / "vault", saves, [
            ("loaded", cp.load_keys("E")),
            ("vault", r"m\^"),              # YOU HAVE REACHED A TREASURE VAULT.
            ("results", r"\r"),
            ("menu", r"\r", lambda b: (pool(b), coins(b))),
            ("share", "S", lambda b: (pool(b), coins(b))),   # #64
        ], seed=1, verbose=args.verbose)
        status |= c.run()
        print("#64 original's pool and coins before Share:", c.probes["menu"])
        print("#64 after Share:", c.probes["share"])
        print("#64 port:", [l for l in c.port_text.splitlines() if l.startswith("treasure:")])
        _, psaves, _ = cp.cokbox.eclplay(cp.load_keys("E") + r"m\^\r\rSENeESF",
                                         work / "vault-port-save", saves=[saves], shots=False,
                                         args=["--seed", "1"])
        print("#64 port's coins after Share (saved in camp):",
              [cp.money((psaves / f"CHRDATF{i}.SAV").read_bytes()) for i in range(1, 7)])

    if args.only in (None, "potion"):
        saves, _ = cp.port_save(work / "save-81", "ESE", [
            "--load", cp.SAVE_A, "--play", "--file", "3", "--set", "4be6=1", "--set", "4c0a=ff"],
            block=81)
        cp.edit_save(saves, "E", at=(14, 9, 6), var={0x4C0A: 0xFF}, geo=c_geo.load(3, 81))
        c = cp.Compare(work / "potion", saves, [
            ("loaded", cp.load_keys("E")),
            ("chamber", r"m\^"),            # THIS ROOM WAS ONCE A VERY OPULENT BED CHAMBER.
            ("menu", r"\re"),
            ("look", "L"),                  # the treasure under the bed: the results
            ("treasure", r"\r"),
            ("items", "TI"),                # Potion, Red MU Scroll
            ("taken", "T", items_of),
            ("leave", r"\e\eEN"),
            ("view", "VI"),
            ("unready", r"\vR"),
            ("ready", r"\v\vR", items_of),
            ("use", "U"),                   # uses an item / Potion: Select Exit
            ("drunk", "S", items_of),
        ], seed=22, verbose=args.verbose)
        status |= c.run()
        print("#65 original's SIR STRONGSWORD's items (name, type, readied) after Take:",
              c.probes["taken"])
        print("#65 readied:", c.probes["ready"])
        print("#65 after Use and Select:", c.probes["drunk"])
        print("#65 port:", [l for l in c.port_text.splitlines()
                            if l.startswith("treasure:") or "healed" in l or "uses" in l
                            or "Readied" in l])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

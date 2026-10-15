#!/usr/bin/env python3
"""Treasures of ECL3 (DEVELOPERS.md, Checks against the original): #64 "`Share` the
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
Items, unready the shield, ready the potion, Use, Select: SIR
STRONGSWORD's items (name, type +0x2e, readied +0x34, the bytes from
+0x2e) are read in memory after Take, after readying and after Use, and
compared with the port's at the same key reads.
"""
import argparse
import sys

import c_geo
import c_places as cp
import checklib


def pool_and_coins(state):
    return {"the pool's coins": state.pool,
            "each one's coins (silver ... steel, gems, jewelry)":
            [(m.name, cp.money(m.record)) for m in state.members]}


def items_of(state):
    """SIR STRONGSWORD's items: name, type +0x2e, readied +0x34, the bytes
    from +0x2e."""
    return {f"{state.members[0].name}'s items (name, type, readied, +0x2e on)":
            [(cp.item_name(i), "type %#x" % i[0x2E], "readied %d" % i[0x34], i[0x2E:0x3F].hex())
             for i in state.members[0].items]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="vault or potion")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-ecl3")
    v = checklib.Verdict()

    if args.only in (None, "vault"):
        saves, _ = cp.port_save(work / "save-98", "ESE", [
            "--load", cp.SAVE_A, "--play", "--file", "3", "--set", "4be6=1", "--set", "4be7=1",
            "--set", "4be8=1", "--set", "4bf2=61", "--set", "4ce5=7", "--at", "6,1,2"], block=97)
        cp.edit_save(saves, "E", at=(6, 1, 2), var={0x4BF2: 98, 0x4C06: 5},
                     geo=c_geo.load(3, 97))
        c = cp.Compare(v, work / "vault", saves, [
            ("loaded", cp.load_keys("E")),
            ("vault", r"m\^"),              # YOU HAVE REACHED A TREASURE VAULT.
            ("results", r"\r"),
            ("menu", r"\r", pool_and_coins),
            ("share", "S", pool_and_coins),   # #64
        ], seed=1, verbose=args.verbose).run()
        before, after = c.probes["menu"]["the pool's coins"], c.probes["share"]["the pool's coins"]
        v.check(before == [0, 60000, 20000, 10000, 5000, 3, 2] and any(after) and after != before,
                "#64: in the original part of the hoard stays in the pool after Share "
                "(the menu still offering Share is on the screen)", f"{before} -> {after}")

    if args.only in (None, "potion"):
        saves, _ = cp.port_save(work / "save-81", "ESE", [
            "--load", cp.SAVE_A, "--play", "--file", "3", "--set", "4be6=1", "--set", "4c0a=ff"],
            block=81)
        cp.edit_save(saves, "E", at=(14, 9, 6), var={0x4C0A: 0xFF}, geo=c_geo.load(3, 81))
        c = cp.Compare(v, work / "potion", saves, [
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
        ], seed=22, verbose=args.verbose).run()
        (taken,), (drunk,) = c.probes["taken"].values(), c.probes["drunk"].values()
        potions = [i for i in taken if i[0].endswith("Potion")]
        v.check(potions and potions[0][1] in ("type 0x30", "type 0x35") and
                not any(i[0].endswith("Potion") for i in drunk),
                "#65: in the original the generated potion (type 0x30 or 0x35) is taken, and gone "
                "once used", f"{potions} -> {[i[0] for i in drunk]}")
        print("#65 port:", [l for l in c.port_text.splitlines()
                            if l.startswith("treasure:") or "healed" in l or "uses" in l])
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

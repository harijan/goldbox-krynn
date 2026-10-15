#!/usr/bin/env python3
"""The first outpost's armoury and temple (ECL1 block 17; DEVELOPERS.md, Checks
against the original): #66 "Enter Throtl's armoury (ECL1 block 17): the
first screen should keep the view's frame as it was, with the portrait last
shown (if any) in the view's place, and only a redraw after `Buy` draws the
frame. `Buy` should list lines like `Long Sword` and `7` ending at column
30, and buying say `NAME buys a Long Sword` on row 24"; #68 "Appraise a gem
with a character carrying no items and choose `Keep`: the gem should be
gone and no item gained. Press Escape at `You can : ` instead: the gem is
sold"; #69 "Leave a temple with money in the pool: the priest's first
sentence should vanish at once behind `Do you want to go back and retrieve
your money?`"; #70 "In a temple's `Heal`, buy a cure: on the list's return
only the row picked should show, the first time the first cure
highlighted"; #71 "Raise a dead fighter of constitution 15 whose maximum
hit points are above those at full: it should lose the whole difference."

Game A's start (7 Enters from Solace) reaches the outpost's menu. Copies
of SAVE/'s games, edited: for #68 SIR STRONGSWORD given a gem (+0xf5) and
no items (an empty .STF); for #69-#71 GARIN (a dwarf, fighter 3, his mage
level 2 taken away, +0xfe) dead (+0x188 6, 0 hit points), constitution 15 (+0x18, +0x19), maximum hit
points 30 (+0x62) over 16 at full (+0x11b), and 3000 steel (+0xf3).
"""
import argparse
import shutil
import struct
import sys

import c_places as cp
import checklib

OUTPOST = cp.load_keys("A") + r"\r\r\r\r\r\r\r"   # WHERE DO YOU WISH TO GO?


def copy_a(dest):
    dest.mkdir(parents=True, exist_ok=True)
    for f in (cp.cokbox.ROOT / "SAVE").glob("*.*"):
        shutil.copyfile(f, dest / f.name)
    return dest


def sir(state):
    m = state.members[0]
    return {f"{m.name}'s items": [cp.item_name(i) for i in m.items],
            f"{m.name}'s coins (silver ... steel, gems, jewelry)": cp.money(m.record)}


def garin(state):
    r = state.members[2].record
    return {"GARIN's constitution, maximum hit points, at full, hit points, status, steel":
            (r[0x18], r[0x19], r[0x62], r[0x11B], r[0x197], r[0x188], cp.money(r)[4])}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="armoury, appraise or temple")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-outpost")
    v = checklib.Verdict()

    if args.only in (None, "armoury"):
        saves = copy_a(work / "saves-a")
        cp.Compare(v, work / "armoury", saves, [
            ("outpost", OUTPOST),
            ("armoury", "A"),                   # the first screen: Buy View Pool Appraise Exit
            ("buy", "B"),                       # the list
            ("long-sword", r"\v\v\v\v\v\v\v"),  # Long Sword highlighted
            ("bought", r"\r"),                  # SIR STRONGSWORD buys a Long Sword
            ("menu", r"\e"),
        ], seed=1, verbose=args.verbose).run()

    if args.only in (None, "appraise"):
        for name, last in (("keep", "K"), ("escape", r"\e")):
            saves = copy_a(work / f"saves-{name}")
            cp.edit_record(saves / "CHRDATA1.SAV", lambda r: struct.pack_into("<H", r, 0xF5, 1))
            (saves / "CHRDATA1.STF").write_bytes(b"")
            c = cp.Compare(v, work / f"appraise-{name}", saves, [
                ("armoury", OUTPOST + "A", sir),
                ("appraise", "A"),              # You have a fine collection of: 1 Gem
                ("gem", "G"),                   # The Gem is Valued at N steel. You can : Sell Keep
                (name, last, sir),
            ], seed=1, verbose=args.verbose).run()
            items, coins = c.probes[name].values()
            v.check(items == [] and coins[5] == 0 and coins[4] == (2 if name == "keep" else 52),
                    f"#68 {name}: in the original the gem is gone, no item gained, steel "
                    f"{'unchanged' if name == 'keep' else 'up by 50 (sold)'}", f"{items}, {coins}")

    if args.only in (None, "temple"):
        saves = copy_a(work / "saves-t")

        def dead(r):
            r[0x18] = r[0x19] = 15
            r[0x62], r[0x188], r[0x197] = 30, 6, 0
            r[0xFE] = 0     # no mage level: a fighter of level 3 only
            struct.pack_into("<H", r, 0xF3, 3000)
        cp.edit_record(saves / "CHRDATA3.SAV", dead)
        c = cp.Compare(v, work / "temple", saves, [
            ("temple", OUTPOST + "NT", garin),  # Heal View Pool Appraise Exit
            ("garin", r"\v\v"),
            ("heal", "H"),                      # #70: the cures, the first highlighted
            ("raise", r"\v\v\v\v\v\v\v\r"),     # Raise Dead will only cost 2750 ...
            ("paid", "Y", garin),               # #70, #71: the list again
            ("menu", r"\e"),
            ("pool", "P"),
            ("leave", "E"),                     # #69: ... retrieve your money?
        ], seed=1, verbose=args.verbose).run()
        (raised,) = c.probes["paid"].values()
        v.check(raised[2] == raised[3] == 16 and raised[5] == 0,
                "#71: in the original GARIN, raised, keeps 16 maximum hit points (the whole 14 "
                "above those at full lost), status 0", str(raised))
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

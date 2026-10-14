#!/usr/bin/env python3
"""The first outpost's armoury and temple (ECL1 block 17; README, Checks
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
no items (an empty .STF); for #69-#71 GARIN (fighter 3, mage 2, a dwarf)
dead (+0x188 6, 0 hit points), constitution 15 (+0x18, +0x19), maximum hit
points 30 (+0x62) over 16 at full (+0x11b), and 3000 steel (+0xf3).
"""
import argparse
import shutil
import struct
import sys

import c_places as cp

OUTPOST = cp.load_keys("A") + r"\r\r\r\r\r\r\r"   # WHERE DO YOU WISH TO GO?


def copy_a(dest):
    dest.mkdir(parents=True, exist_ok=True)
    for f in (cp.cokbox.ROOT / "SAVE").glob("*.*"):
        shutil.copyfile(f, dest / f.name)
    return dest


def edit(path, fn):
    raw = bytearray(path.read_bytes())
    fn(raw)
    path.write_bytes(bytes(raw))


def sir(box):
    _, raw = cp.party_records(box)[0]
    return cp.name_of(raw), "items", raw[0x142], "coins", cp.money(raw)


def garin(box):
    _, raw = cp.party_records(box)[2]
    return (cp.name_of(raw), "con", raw[0x18], raw[0x19], "max hp", raw[0x62], "full",
            raw[0x11B], "hp", raw[0x197], "status", raw[0x188], "steel", cp.money(raw)[4])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="armoury, appraise or temple")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("outpost", args.workdir)
    status = 0

    if args.only in (None, "armoury"):
        saves = copy_a(work / "saves-a")
        c = cp.Compare(work / "armoury", saves, [
            ("outpost", OUTPOST),
            ("armoury", "A"),                   # the first screen: Buy View Pool Appraise Exit
            ("buy", "B"),                       # the list
            ("long-sword", r"\v\v\v\v\v\v\v"),  # Long Sword highlighted
            ("bought", r"\r"),                  # SIR STRONGSWORD buys a Long Sword
            ("menu", r"\e"),
        ], seed=1, verbose=args.verbose)
        status |= c.run()

    if args.only in (None, "appraise"):
        for name, last in (("keep", "K"), ("escape", r"\e")):
            saves = copy_a(work / f"saves-{name}")
            edit(saves / "CHRDATA1.SAV", lambda r: struct.pack_into("<H", r, 0xF5, 1))
            (saves / "CHRDATA1.STF").write_bytes(b"")
            c = cp.Compare(work / f"appraise-{name}", saves, [
                ("armoury", OUTPOST + "A", sir),
                ("appraise", "A"),              # You have a fine collection of: 1 Gem
                ("gem", "G"),                   # The Gem is Valued at N steel. You can : Sell Keep
                (name, last, sir),
            ], seed=1, verbose=args.verbose)
            status |= c.run()
            print(f"#68 {name}: original's SIR STRONGSWORD before:", c.probes["armoury"])
            print(f"#68 {name}: after:", c.probes[name])
            print("#68 port:", [l for l in c.port_text.splitlines()
                                if l.startswith(("shop:", "print: The Gem"))])

    if args.only in (None, "temple"):
        saves = copy_a(work / "saves-t")

        def dead(r):
            r[0x18] = r[0x19] = 15
            r[0x62], r[0x188], r[0x197] = 30, 6, 0
            struct.pack_into("<H", r, 0xF3, 3000)
        edit(saves / "CHRDATA3.SAV", dead)
        c = cp.Compare(work / "temple", saves, [
            ("temple", OUTPOST + "NT", garin),  # Heal View Pool Appraise Exit
            ("garin", r"\v\v"),
            ("heal", "H"),                      # #70: the cures, the first highlighted
            ("raise", r"\v\v\v\v\v\v\v\r"),     # Raise Dead will only cost 2750 ...
            ("paid", "Y", garin),               # #70, #71: the list again, only the row picked
            ("menu", r"\e"),
            ("pool", "P"),
            ("leave", "E"),                     # #69: ... retrieve your money?
        ], seed=1, verbose=args.verbose)
        status |= c.run()
        print("#71 original's GARIN before:", c.probes["temple"])
        print("#71 original's GARIN raised:", c.probes["paid"])
        # The port's: on to the inn's camp and Save as B.
        _, psaves, _ = cp.cokbox.eclplay(
            OUTPOST + r"NT\v\vH\v\v\v\v\v\v\v\rY\eEI\rSB", work / "temple-port-save",
            saves=[saves], shots=False, args=["--seed", "1"])
        raw = (psaves / "CHRDATB3.SAV").read_bytes()
        print("#71 port's GARIN raised (saved at the inn):", cp.name_of(raw), "con", raw[0x18],
              raw[0x19], "max hp", raw[0x62], "full", raw[0x11B], "hp", raw[0x197], "status",
              raw[0x188])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

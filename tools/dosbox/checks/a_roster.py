#!/usr/bin/env python3
"""Check: the roster (README, Checks against the original: "Add a roster
character with 128 or more jewels: it should not be listed. Make the first
key of Add a space: Add should end. Add a character, leave, and open Add
again with a space: it may stay."; "Add a ranger to a party with three
rangers: `too many rangers in party`."; "Remove a character and save it over
an existing file, answering No to `Overwrite`: the character should take the
new file's name.").

The roster is written by eclplay (Create, seeded: BOB, JEWEL and CARL
fighters, RANGA-RANGD rangers), JEWEL.WHO's +0xf7 (the jewelry's low byte)
then set to 0x80. The original in DOSBox-X and eclplay run the same keys
from the title on copies of it; screens are compared with eclplay's best
frame, where the original waits is printed, and the roster files after are
compared.
"""
import argparse
import shutil
import struct
import sys

import a_common as A
from a_create import create_keys

ROSTER = [("BOB", 1), ("JEWEL", 1), ("CARL", 1), ("RANGA", 4), ("RANGB", 4), ("RANGC", 4),
          ("RANGD", 4)]   # human (row 6) fighters (class row 1) and rangers (4)
ADD = r"\v\r"             # from the start menu's first row, with no party

CASES = {
    # 113: JEWEL is not listed.
    "jewels": r"\kxP" + ADD + r"\slist.\e\sback.",
    # 113: a space as Add's first key.
    "space": r"\kxP" + ADD + r"\slist. \safter-space.",
    # 113: Add BOB, leave, Add again, a space.
    "space-again": r"\kxP" + ADD + r"\r\e\sleft." + r"\v\v\v\r\slist2. \safter-space.",
    # 114: three rangers, then a fourth (the rangers are rows 4-7 without JEWEL).
    "rangers": r"\kxP" + ADD + r"\v\v\r\v\r\v\r\srangers3.\v\r\sfourth.\e\sback.",
    # 115: BOB added, removed (row 6) and saved as DAVE over BOB.WHO.
    "remove": r"\kxP" + ADD + r"\r\e\sadded.\v\v\v\v\r\r\soverwrite.N\snew-name.DAVE\r\sremoved.",
}


def make_roster(work):
    """Create the roster with eclplay; return its directory."""
    keys = r"\kxP" + "".join(create_keys(n, 6, c, None, 0, 0) for n, c in ROSTER)
    out, saves, _ = A.cokbox.eclplay(A.port_keys(keys), work / "make-roster", shots=False,
                                     args=["--seed", "3"])
    roster = work / "roster"
    shutil.rmtree(roster, ignore_errors=True)
    roster.mkdir(parents=True)
    for f in saves.iterdir():
        shutil.copyfile(f, roster / f.name)
    who = bytearray((roster / "JEWEL.WHO").read_bytes())
    who[0xF7] = 0x80
    (roster / "JEWEL.WHO").write_bytes(who)
    print("roster:", " ".join(sorted(p.name for p in roster.iterdir())))
    return roster


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "roster")
    roster = make_roster(work)
    bad = 0
    for name, keys in CASES.items():
        if args.case and name not in args.case:
            continue
        print(f"{name}: keys {keys}")
        pair = A.Pair(keys, work / name, saves=[roster], verbose=args.verbose)
        print("  port: " + " | ".join(line for line in pair.out.strip().splitlines()[-30:]
                                      if not line.startswith("item: ")))
        with pair:
            # Type step by step, printing where the original waits at each shot.
            rest = keys
            while r"\s" in rest:
                head, _, tail = rest.partition(r"\s")
                shot, _, rest = tail.partition(".")
                if shot == "fourth":
                    # The message shows for a moment: screens as fast as they come.
                    pair.run(head[:-2])
                    # The original's speed (DS:4b38, 4) made 60 for this one key, so
                    # that the message's 6 seconds can be seen (the port's frames,
                    # taken at key reads, do not show it).
                    speed = pair.box.ds_byte(0x4B38)
                    pair.box.ds_write(0x4B38, bytes([60]))
                    pair.box.wait_input()
                    pair.box.push_key(A.cokbox.key_word("\r"))
                    seen = []
                    for _ in range(14):
                        s = pair.box.screenshot()
                        if not seen or s.pixels != seen[-1].pixels:
                            s.save_png(pair.work / f"message-{len(seen)}.png")
                            seen.append(s)
                    print(f"  {len(seen)} different screens after the fourth ranger's Enter "
                          f"(message-N.png); lit pixels on row 24: "
                          + ", ".join(str(sum(1 for p in s.region(0, 192, 320, 8) if p))
                                      for s in seen))
                    pair.box.ds_write(0x4B38, bytes([speed]))
                    head = ""
                pair.run(head)
                pair.shot(shot)
                print(f"  {shot}: original waits at {pair.box.where()}")
                bad += pair.compare(shot)[0] != 0
            if rest:
                pair.run(rest)
            if name == "remove":
                orig, port = pair.box_files(), pair.port_files()
                for label, files in (("original", orig), ("port", port)):
                    who = files.get("DAVE.WHO")
                    print(f"  {label}: files {sorted(files)}; DAVE.WHO's name "
                          f"{who[1:1 + who[0]].decode() if who else None}")
                bad += A.compare_files(orig, port, A.mask_roster)
            if name == "rangers":
                box = pair.box
                rec, names = box.far(box.ds * 16 + 0x609A), []
                while rec is not None and len(names) < 8:
                    raw = box.read(rec, 32)
                    names.append(raw[1:1 + raw[0]].decode("latin-1"))
                    rec = box.far(rec + 0x17F)
                print(f"  the original's party: {names}")
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS / see above", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

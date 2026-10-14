#!/usr/bin/env python3
"""The overland map (README, Checks against the original): #98 "Start a new
game; Solace leads straight into the first outpost. Choose `LEAVE` there
(ECL1 block 17 at `8d86`): the overland map should show with the party's
mark ... on the outpost (cell 2, 4, over its star), and `YOU ARE NEAR AN
OUTPOST. DO YOU ENTER?`. Answer No, then `Move` and the right arrow: the
mark should move a square east, the outpost's cell show as it was, and
every second step the moons move a day"; #99 "On the overland map in
`Move` mode, press Ctrl-F8: the party should camp, without a step; after
leaving camp the map should show again with the mark"; #100 "At 35, 4, on
the east coast, press keypad 3, south-east: `YOU QUICKLY TIRE OF SWIMMING
AND RETURN TO SHORE.`, and no step. Keypad 5, or F1, then tries the same
way again"; #101 "Win a fight on the overland map: after the treasure menu
the map should show with the mark where the party stood"; #102 "Meet the
knights' camp (ECL1 block 16, `PROGRAM 9`) and leave the camp menu: the map
should show again, and the script end there."

Run 1 is game A from Solace (SAVE/, which plays as a new game does from
there), started with Helm: LEAVE, No, Move, east: the caravan's attack;
Alt-X at the first character's turn (the Gods), No to Continue Battle, the
results and the treasure menu, Exit, No: the caravan's text over the map.
Run 2: a saved game the port writes at 2,2 with the caravan met (c_throtl's
overland vars): four steps east, Ctrl-F8 (BIOS word 0x6500), Exit. Run 3:
the same game moved in the file to 35,4 (vars 0x4bc3, 0x4bc4): 3, Enter, 5,
Enter, F1 (0x3b00). Run 4: the game at 2,2 with the step counter (var
0x4c06) at 10 and seeds that meet the knights a step east (75 for the
port, 211 for the original); Yes, the camp (PROGRAM 9), Exit.
"""
import argparse
import shutil
import sys

import c_places as cp

OVERLAND = ["--load", cp.SAVE_A, "--play", "--set", "4bf2=11", "--set", "4bc3=1",
            "--set", "4bc4=3", "--set", "4c2d=1"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="caravan, steps, coast or knights")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    cp.need_save_a()
    work = cp.workdir("overland", args.workdir)
    status = 0

    if args.only in (None, "caravan"):
        c = cp.Compare(work / "caravan", cp.cokbox.ROOT / "SAVE", [
            ("outpost", cp.load_keys("A") + r"\r\r\r\r\r\r\r"),
            ("leave", "L"),                 # #98: the map, the mark on the outpost, the question
            ("no", "N"),
            ("move", "m"),
            ("east", r"\>"),                # #98: a square east; the caravan
            ("battle", r"\r"),
            ("gods", r"\@x"),               # The Gods intervene! Continue Battle:
            ("won", "N"),
            ("treasure", r"\r"),
            ("left", "E"),
            ("map", "N"),                   # #101: the map with the mark, the caravan's text
        ], seed=1, helm=True, verbose=args.verbose)
        status |= c.run()
        print("port:", [l for l in c.port_text.splitlines() if l.startswith("overland:")])

    saves, _ = cp.port_save(work / "save", "nm9EESC", OVERLAND, block=16)
    if args.only in (None, "steps"):
        c = cp.Compare(work / "steps", saves, [
            ("loaded", cp.load_keys("C")),
            ("move", "m"),
            ("east1", r"\>"), ("east2", r"\>"), ("east3", r"\>"), ("east4", r"\>"),
            ("camp", r"\#65"),              # #99: Ctrl-F8 camps
            ("map", "E"),                   # leaving camp: the map and the mark
        ], seed=1, verbose=args.verbose)
        status |= c.run()
        print("port:", [l for l in c.port_text.splitlines() if l.startswith("overland:")])

    if args.only in (None, "coast"):
        shutil.copyfile(saves / "SAVGAMC.DAT", saves / "SAVGAMD.DAT")
        for f in list(saves.glob("CHRDATC*")):
            shutil.copyfile(f, saves / f.name.replace("CHRDATC", "CHRDATD"))
        cp.edit_save(saves, "D", var={0x4BC3: 35, 0x4BC4: 4})
        c = cp.Compare(work / "coast", saves, [
            ("loaded", cp.load_keys("D")),
            ("move", "m"),
            ("se", "3"),                    # #100: YOU QUICKLY TIRE OF SWIMMING ...
            ("back", r"\r"),
            ("again", "5"),                 # the same way again
            ("back2", r"\r"),
            ("f1", r"\#3b"),
            ("back3", r"\r"),
        ], seed=1, verbose=args.verbose)
        status |= c.run(box_hook=lambda box: print(
            "#100 original's overland square after (vars 0x4bc3, 0x4bc4):",
            cp.var(box, 0x4BC3), cp.var(box, 0x4BC4)))
        print("port:", [l for l in c.port_text.splitlines()
                        if l.startswith("overland:") or "SWIMMING" in l])
    if args.only in (None, "knights"):
        # #102: the step counter (var 0x4c06) at 10, so that the next step
        # rolls for an encounter. The port meets the knights (8890) with seed
        # 75; the original, which takes no roll between the title and that
        # step (RandSeed read in memory: still the seed), with 211 (its
        # d100s, Random(100) = (seed >> 16) mod 100: 8 under the chance of
        # 11, then 92). The port rolls once more, on resuming: it runs the
        # location vector there (8849's RANDOM in --trace before the first
        # menu: 66 from 75, then 11 and 94 at the step).
        shutil.copyfile(saves / "SAVGAMC.DAT", saves / "SAVGAME.DAT")
        for f in list(saves.glob("CHRDATC*")):
            shutil.copyfile(f, saves / f.name.replace("CHRDATC", "CHRDATE"))
        cp.edit_save(saves, "E", var={0x4C06: 10})
        c = cp.Compare(work / "knights", saves, [
            ("loaded", cp.load_keys("E")),
            ("move", "m"),
            ("knights", r"\>"),            # YOU COME UPON A COMPANY OF KNIGHTS ...
            ("camp", "Y"),                  # PROGRAM 9: the camp's menu
            ("map", "E"),                   # #102: the map again, the script ended
            ("step", r"\>"),
        ], seed=75, box_seed=211, verbose=args.verbose)
        status |= c.run()
        print("port:", [l for l in c.port_text.splitlines()[-8:]])
    print(f"files in {work}")
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())

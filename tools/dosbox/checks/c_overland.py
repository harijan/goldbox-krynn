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
0x4c06) at 10 and RandSeed 75 rolled once at the first key read after
loading (c_places.Compare), which meets the knights a step east; Yes, the camp
(PROGRAM 9), Exit. The overland square (vars 0x4bc3, 0x4bc4) is read in
the original's memory after each step of runs 2 and 3 and compared with
the port's.
"""
import argparse
import sys

import c_places as cp
import checklib


def square(state):
    return {"the overland square (vars 0x4bc3, 0x4bc4)": (state.var(0x4BC3), state.var(0x4BC4))}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--only", help="caravan, steps, coast or knights")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "c-overland")
    v = checklib.Verdict()

    if args.only in (None, "caravan"):
        c = cp.Compare(v, work / "caravan", cp.cokbox.ROOT / "SAVE", [
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
        ], seed=1, helm=True, verbose=args.verbose).run()
        print("port:", [l for l in c.port_text.splitlines() if l.startswith("overland:")])

    saves, _ = cp.port_save(work / "save", "nm9EESC", cp.OVERLAND, block=16)
    if args.only in (None, "steps"):
        c = cp.Compare(v, work / "steps", saves, [
            ("loaded", cp.load_keys("C"), square),
            ("move", "m"),
            ("east1", r"\>", square), ("east2", r"\>", square), ("east3", r"\>", square),
            ("east4", r"\>", square),
            ("camp", r"\#65", square),      # #99: Ctrl-F8 camps
            ("map", "E", square),           # leaving camp: the map and the mark
        ], seed=1, verbose=args.verbose).run()
        got = [list(c.probes[n].values())[0] for n in ("east4", "camp", "map")]
        v.check(got[0] == got[1] == got[2], "#99: in the original Ctrl-F8 takes no step", str(got))
        print("port:", [l for l in c.port_text.splitlines() if l.startswith("overland:")])

    if args.only in (None, "coast"):
        cp.copy_game(saves, "C", "D")
        cp.edit_save(saves, "D", var={0x4BC3: 35, 0x4BC4: 4})
        c = cp.Compare(v, work / "coast", saves, [
            ("loaded", cp.load_keys("D"), square),
            ("move", "m"),
            ("se", "3"),                    # #100: YOU QUICKLY TIRE OF SWIMMING ...
            ("back", r"\r", square),
            ("again", "5"),                 # the same way again
            ("back2", r"\r", square),
            ("f1", r"\#3b"),
            ("back3", r"\r", square),
        ], seed=1, verbose=args.verbose).run()
        got = [list(c.probes[n].values())[0] for n in ("loaded", "back", "back2", "back3")]
        v.check(all(g == (35, 4) for g in got), "#100: in the original the party stays at 35,4",
                str(got))
        v.check(c.port_text.count("YOU QUICKLY TIRE OF SWIMMING") == 3,
                "#100: 3, 5 and F1 each tire of swimming (the port's log; the screens of both "
                "equal)")
    if args.only in (None, "knights"):
        # #102: the step counter (var 0x4c06) at 10, so that the next step
        # rolls for an encounter; RandSeed 75 rolled once at the first key
        # read after loading (where the port as it is gets from 75: it rolls
        # once on resuming here) meets the knights (8890) a step east.
        cp.copy_game(saves, "C", "E")
        cp.edit_save(saves, "E", var={0x4C06: 10})
        c = cp.Compare(v, work / "knights", saves, [
            ("loaded", cp.load_keys("E")),
            ("move", "m"),
            ("knights", r"\>"),            # YOU COME UPON A COMPANY OF KNIGHTS ...
            ("camp", "Y"),                  # PROGRAM 9: the camp's menu
            ("map", "E"),                   # #102: the map again, the script ended
            ("step", r"\>"),
        ], seed=cp.roll(75), verbose=args.verbose).run()
        v.check("COMPANY OF KNIGHTS" in c.port_text_of(fixed=True),
                "#102: the step meets the knights' camp (the log of the port with the known "
                "fixes; the original's screens are compared above)")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

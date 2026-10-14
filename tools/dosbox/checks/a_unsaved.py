#!/usr/bin/env python3
"""Check: Escape marks the game unsaved (README, Checks against the original:
"In the start menu, press Escape, then Save: the game should count as
unsaved, so `Exit to DOS` and Yes ask `Game NOT saved.  Quit anyway? `; No
there should open `Save Which Game: ` and stay in the menu").

SAVE/'s game A is loaded in the start menu and saved as game A (Save Current
Game sets DS:5885), then Escape is pressed in the start menu, and Exit to
DOS, Yes, No, Escape. DS:5885 is read after each step; each screen is
compared with eclplay's for the same keys. A second run presses Escape
first and then saves: Exit and Yes then quit to DOS at once.
"""
import argparse
import sys

import a_common as A

# Title, Play, Load A (the list keeps the third row, Modify, selected).
LOAD = r"\kxP\v\v\rA\sloaded."
SAVE = r"\v\v\v\v\rA\ssaved."    # down to Save Current Game (row 7), as game A
ESC = r"\e\sescape."
EXIT = r"\v\v\rY\squit.N\snot-saved.\e\sback."
STEPS_1 = [("load A", LOAD), ("save as A", SAVE), ("Escape", ESC),
           ("Exit, Yes, No, Escape", EXIT)]
STEPS_2 = [("load A", LOAD), ("Escape", ESC), ("save as A", SAVE),
           ("Exit, Yes", r"\v\v\rY")]


def run(name, steps, work, verbose):
    keys = "".join(k for _, k in steps)
    pair = A.Pair(keys, work / name, saves=[A.cokbox.ROOT / "SAVE"], verbose=verbose)
    bad = 0
    print(f"{name}: keys {keys}")
    print("  port's last lines:", " | ".join(pair.out.strip().splitlines()[-6:]))
    with pair:
        for label, k in steps:
            pair.run(k)
            if pair.box.running and label != "Exit, Yes":
                pair.box.wait_input(settle=0.3)
                print(f"  after {label}: DS:5885 = {pair.box.ds_byte(0x5885)}, "
                      f"waits at {pair.box.where()}")
        if steps[-1][0] == "Exit, Yes":
            quit_ = pair.box.wait_exit(20)
            print(f"  after Exit, Yes: the original {'quit to DOS' if quit_ else 'is still running'}"
                  f"; the port: {'quit' if '(quit to DOS)' in pair.out else 'did not quit'}")
            bad += quit_ != ("(quit to DOS)" in pair.out)
        for shot in sorted((pair.work / "shots").glob("*.png")):
            bad += pair.compare(shot.stem)[0] != 0
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "unsaved")
    bad = run("escape-after-save", STEPS_1, work, args.verbose)
    bad += run("escape-before-save", STEPS_2, work, args.verbose)
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

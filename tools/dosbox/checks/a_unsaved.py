#!/usr/bin/env python3
"""Check #109: Escape marks the game unsaved (DEVELOPERS.md, Checks against the
original: "In the start menu, press Escape, then Save: the game should count
as unsaved, so `Exit to DOS` and Yes ask `Game NOT saved.  Quit anyway? `;
No there should open `Save Which Game: ` and stay in the menu").

SAVE/'s game A is loaded in the start menu and saved as game A (Save Current
Game sets DS:5885), then Escape is pressed in the start menu, and Exit to
DOS, Yes, No, Escape. At each step the screen and DS:5885 are compared with
the port's at the same key read. A second run presses Escape first and then
saves: Exit and Yes then quit to DOS at once, in both.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

# Title, Play, Load A (the list keeps the third row, Modify, selected).
LOAD = r"\kxP\v\v\rA\sloaded."
SAVE = r"\v\v\v\v\rA\ssaved."    # down to Save Current Game (row 7), as game A
RUNS = {
    "escape-after-save": LOAD + SAVE + r"\e\sescape.\v\v\rY\squit.N\snot-saved.\e\sback.",
    "escape-before-save": LOAD + r"\e\sescape." + SAVE + r"\v\v\rY",   # quits to DOS
}


def run(v, name, keys, work, verbose):
    print(f"{name}: keys {keys}")
    pair = A.Pair(keys, work / name, saves=A.SAVES, verbose=verbose)
    probes = pair.probes()
    saved = {}
    with pair:
        pair.steps(lambda mark: saved.__setitem__(mark, pair.box.ds_byte(0x5885)))
        quits = keys.endswith("Y")
        if quits:
            gone = pair.box.wait_exit(20)
    for mark in pair.marks():
        pair.judge(v, mark, what=f"{name}, screen {mark}")
        v.value(f"{name}, {mark}: DS:5885 (saved)", saved[mark], probes[mark].get("saved"))
    if quits:
        v.value(f"{name}: Exit, Yes quits to DOS at once", gone, "(quit to DOS)" in pair.out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "unsaved")
    v = checklib.Verdict()
    for name, keys in RUNS.items():
        run(v, name, keys, work, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check: load saved game A and compare the screen (README, Checks against
the original: "Load saved game A and compare the party list and status line
beside the view"; and, in CGA mode, "compare ... with eclplay --display cga
--shots, colour for colour").

In each display mode the original in DOSBox-X and eclplay load SAVE/'s game
A from the start menu and begin adventuring; screens are taken at the start
menu with the party loaded and at Solace's first page of text, each
compared with the port's at the same key read (checklib.frames_at), and a
difference with the port built with the known fixes (Solace's row 24: the
full stop the port adds after PRESS <ENTER>/<RETURN> TO CONTINUE).
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import f_lib  # noqa: E402

cokbox = checklib.cokbox

# Title, Play, Load Saved Game, A; then Begin Adventuring (the eighth row,
# the list keeping the third selected).
STEPS = [("loaded", r"\kxP\v\v\rA"), ("solace", r"\v\v\v\v\v\r")]
# Where a difference lies (printed only).
REGIONS = {"party list": (136, 8, 176, 64), "status line": (136, 112, 176, 8),
           "text": (8, 136, 304, 48), "row 24": (0, 192, 320, 8)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--machine", action="append", choices=sorted(cokbox.MACHINES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "load-screen")
    v = checklib.Verdict()
    saves = [checklib.SAVES]
    keys = f_lib.marked(STEPS)
    for machine in args.machine or ["tandy", "ega", "cga"]:
        w = work / machine
        port, fixed = f_lib.frames(keys, w, machine=machine, saves=saves)
        shots = {}
        with cokbox.Box(workdir=w / "box", machine=machine, saves=saves,
                        verbose=args.verbose) as box:
            for name, step in STEPS:
                box.keys(step)
                box.wait_input(settle=1.0)
                shots[name] = box.screenshot(w / f"{name}.png")
        f_lib.judge(v, machine, shots, port, fixed, w)
        for name, shot in shots.items():
            other = port.get(name)
            parts = [r for r, a in REGIONS.items()
                     if other is not None and shot.region(*a) != other.region(*a)]
            if parts:
                print(f"  {machine} {name}: differs in {', '.join(parts)}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

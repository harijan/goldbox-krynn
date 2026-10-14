#!/usr/bin/env python3
"""Check: load saved game A and compare the screen (README, Checks against
the original: "Load saved game A and compare the party list and status line
beside the view"; and, in CGA mode, "compare ... with eclplay --display cga
--shots, colour for colour").

In each display mode the original in DOSBox-X and eclplay load SAVE/'s game
A from the start menu and begin adventuring; screens are taken at the start
menu with the party loaded and at Solace's first page of text, and each is
compared with the eclplay frame it matches best (eclplay saves one at each
key read).
"""
import argparse
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

# Title, Play, Load Saved Game, A; then Begin Adventuring (the eighth row,
# the list keeping the third selected).
STEPS = [("loaded", r"\kxP\v\v\rA"), ("solace", r"\v\v\v\v\v\r")]
REGIONS = {"party list": (136, 8, 176, 64), "status line": (136, 112, 176, 8),
           "text": (8, 136, 304, 48), "row 24": (0, 192, 320, 8)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--machine", action="append", choices=sorted(cokbox.MACHINES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-load-screen-"))
    saves = cokbox.ROOT / "SAVE"
    if not (saves / "SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    status = 0
    for machine in args.machine or ["tandy", "ega", "cga"]:
        keys = "".join(k for _, k in STEPS)
        _, _, eshots = cokbox.eclplay(keys, work / machine / "eclplay", machine=machine,
                                      saves=[saves])
        expected = [cokbox.Screen.load(p) for p in eshots]
        with cokbox.Box(workdir=work / machine / "box", machine=machine, saves=[saves],
                        verbose=args.verbose) as box:
            for name, step in STEPS:
                box.keys(step)
                box.wait_input(settle=1.0)
                shown = box.screenshot(work / machine / f"{name}.png")
                best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
                count, area = shown.diff(expected[best])
                print(f"{machine} {name}: {count} pixels differ from eclplay's "
                      f"{eshots[best].name}" + (f" in {area}" if area else ""))
                if count:
                    status = 1
                    shown.save_png(work / machine / f"diff-{name}.png", highlight=expected[best])
                    for region, (x, y, w, h) in REGIONS.items():
                        if shown.region(x, y, w, h) != expected[best].region(x, y, w, h):
                            print(f"  {region} differs")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

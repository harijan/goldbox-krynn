#!/usr/bin/env python3
"""Checks #36 and #37: the camp screen in Throtl, and Enter right after
Encamp (DEVELOPERS.md, Checks against the original: "Camp in Throtl and compare
the camp screen ..." and "Camp right after `Encamp` and press Enter: the
original opens `Alter`, the fifth item, as the menu keeps the selection").

The port writes the game: SAVE/'s game A loaded (eclplay --load), entered
into ECL1 block 32 in 3D (0x4be6 = 1), the gate's battle decided won
(--combat won), the treasure left, Encamp at Throtl's gate (7, 15 facing
north) and Save as J. The original and eclplay --start-menu load J from the
start menu and:

#36: Encamp (`e`), then Rest (`R`): the camp screen and the rest menu.
#37: Encamp chosen with the arrows (four rights from Move) and Enter, then
     Enter again; and Encamp chosen by its letter, then Enter.

Each of the original's screens is compared with eclplay's at the same point
of the keys, and the routine the original waits in, with its mode (DS:4b49), must be the one recorded: the camp's
menu (4888:2d03, mode 2), the rest's (57e4:08ed), Save's letters
(4b6d:2327: Enter after Encamp opens Save).
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402
import checklib  # noqa: E402

TO_THROTL_CAMP = r"\r\rEne" + L.save_keys("J")   # ATTACK, won, Exit, No, Encamp, Save J


def make_save(work):
    out, saves, _ = L.port(TO_THROTL_CAMP, work / "make",
                           ["--load", L.SAVE / "SAVGAMA.DAT", "--play", "--combat", "won",
                            "--set", "4be6=1"], block=32)
    game = work / "game"
    L.copy_game(saves, "J", game)
    return game


# Where the original waits after each step (batch B's run), and its mode.
WAITS = {"camp": ("4888:2d03", 2), "rest": ("57e4:08ed", 2), "enter": ("4b6d:2327", 2)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-camp")
    game = make_save(work)
    v = checklib.Verdict()
    runs = {
        "36": [(L.load_keys("J"), "resume"), ("e", "camp"), ("R", "rest")],
        "37-arrows": [(L.load_keys("J"), "resume"), (r"\>\>\>\>\r", "camp"), (r"\r", "enter")],
        "37-letter": [(L.load_keys("J"), "resume"), ("e", "camp"), (r"\r", "enter")],
    }
    for label, steps in runs.items():
        print(f"#{label}:")
        waits = {}

        def during(box, name):
            waits[name] = (box.where(), box.ds_byte(0x4B49))

        shots, port, out = S.run_pair(work / label, [game], steps, verbose=args.verbose,
                                      during=during)
        S.judge(v, shots, port, work / label)
        for name, (where, mode) in waits.items():
            print(f"    {name}: original waits in {where}, mode {mode}")
            if name in WAITS:
                v.check(WAITS[name][0] in str(where) and mode == WAITS[name][1],
                        f"#{label} {name}: the original waits in {WAITS[name][0]}, mode "
                        f"{WAITS[name][1]}", f"{where}, mode {mode}")
        menus = [line for line in out.splitlines() if line.startswith(("menu:", "choice:"))]
        print("  eclplay's last menus:", menus[-3:])
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

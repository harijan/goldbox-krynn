#!/usr/bin/env python3
"""Checks #36 and #37: the camp screen in Throtl, and Enter right after
Encamp (README, Checks against the original: "Camp in Throtl and compare
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

Each of the original's screens is compared with eclplay's nearest frame,
and where() names the routine the original waits in.
"""
import argparse
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402

TO_THROTL_CAMP = r"\r\rEne" + L.save_keys("J")   # ATTACK, won, Exit, No, Encamp, Save J


def make_save(work):
    out, saves, _ = L.port(TO_THROTL_CAMP, work / "make",
                           ["--load", L.SAVE / "SAVGAMA.DAT", "--play", "--combat", "won",
                            "--set", "4be6=1"], block=32)
    game = work / "game"
    L.copy_game(saves, "J", game)
    return game


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-camp-"))
    game = make_save(work)
    waits = {}

    def during(box, name):
        waits[name] = (box.where(), box.ds_byte(0x4B49))

    status = 0
    runs = {
        "36": [(L.load_keys("J"), "resume"), ("e", "camp"), ("R", "rest")],
        "37-arrows": [(L.load_keys("J"), "resume"), (r"\>\>\>\>\r", "camp"), (r"\r", "enter")],
        "37-letter": [(L.load_keys("J"), "resume"), ("e", "camp"), (r"\r", "enter")],
    }
    for label, steps in runs.items():
        print(f"#{label}:")
        results, out = S.run_pair(work / label, [game], steps, verbose=args.verbose,
                                  during=during)
        S.report(results)
        for name in results:
            print(f"    original waits in {waits[name][0]}, mode {waits[name][1]}")
        menus = [line for line in out.splitlines() if line.startswith(("menu:", "choice:"))]
        print("  eclplay's last menus:", menus[-3:])
        # The resumed view differs (see #38: the wall set offsets); the camp's
        # screens must not.
        if any(results[n][0] for n in results if n != "resume"):
            status = 1
    print("AGREES" if status == 0 else "PORT DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check #38: resuming a game saved in camp (README, Checks against the
original: "Save in camp, then load that game: it holds mode 2 (0x1407).
Compare how the original resumes it with how it resumes a game saved from
the party menu").

Three games, each loaded from the start menu (Begin Adventuring) in the
original and in eclplay --start-menu, the screens compared and where() the
original waits named, with its mode (DS:4b49) and last mode (DS:4b4a):

- J, saved by the port in camp at Throtl's gate (3D, modes 4 then 2; see
  b_camp_screen.py), then Encamp and Save as E in both, the files compared;
- D, saved by the port in the outpost inn's camp (game A's start, the inn;
  modes 0 then 2), then the inn, camp and Save as E in both;
- SAVE/'s A (modes 0 and 0: no camp), its first page.

The resumed view at Throtl's gate differs: the original draws doors where
the port draws openings. Built with the wall sets' tiles offset by the slot
the record is loaded at (69ea:1025's DS:1ec2[slot] - DS:1ec4, once for the
record), the port's frame is the original's (--patched ECLPLAY).
"""
import argparse
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402
from b_camp_screen import make_save as make_throtl  # noqa: E402


def make_inn(work):
    out, saves, _ = L.port(L.A_TO_CAMP + L.save_keys("D"), work / "make-inn",
                           ["--load", L.SAVE / "SAVGAMA.DAT", "--play"])
    return L.copy_game(saves, "D", work / "game-inn")


def modes(path):
    d = pathlib.Path(path).read_bytes()
    return d[0x1406], d[0x1407]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--patched", help="an eclplay built with the slot's offset, to show "
                        "its resumed view is the original's")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-resume-"))
    throtl = make_throtl(work)
    inn = make_inn(work)
    status = 0
    cases = [
        ("throtl", throtl, "J", [(L.load_keys("J"), "resume"), ("e", "camp"),
                                 (L.save_keys("E"), "saved")]),
        ("inn", inn, "D", [(L.load_keys("D"), "resume"), (L.INN, "camp"),
                           (L.save_keys("E"), "saved")]),
        ("A", L.SAVE, "A", [(L.load_keys("A"), "resume")]),
    ]
    for label, game, letter, steps in cases:
        print(f"{label}: SAVGAM{letter}.DAT modes (0x1406, 0x1407) = {modes(game / f'SAVGAM{letter}.DAT')}")
        waits = {}
        orig_saves = work / label / "orig-saves"

        def during(box, name):
            waits[name] = (box.where(), box.ds_byte(0x4B49), box.ds_byte(0x4B4A))
            if name == "saved":
                shutil.copytree(box.game / "SAVE", orig_saves, dirs_exist_ok=True)

        results, out = S.run_pair(work / label, [game], steps, verbose=args.verbose,
                                  during=during)
        S.report(results)
        for name in results:
            print(f"    {name}: original waits in {waits[name][0]}, mode {waits[name][1]}, "
                  f"last mode {waits[name][2]}")
        menus = [line for line in out.splitlines() if line.startswith("menu:")]
        print("  eclplay's menus after Begin Adventuring:",
              menus[menus.index("menu: Load Saved Game") + 1:][:4] if "menu: Load Saved Game"
              in menus else menus[-4:])
        if label == "throtl" and args.patched:
            shot = results["resume"][3]
            out2 = work / label / "patched"
            out2.mkdir(parents=True, exist_ok=True)
            import subprocess
            subprocess.run([args.patched, "--load", str(game / "SAVGAMJ.DAT"), "--play",
                            "--screen", str(out2 / "resume.bmp"), "--keys", "",
                            str(L.ROOT / "Assets")], capture_output=True, check=False)
            n, _ = shot.diff(L.cokbox.Screen.load(out2 / "resume.bmp"))
            print(f"  resume: {n} pixels differ from the patched eclplay's view")
        if "saved" in waits:
            port_saves = work / label / "eclplay" / "saves"
            print("  saved again as E: modes original", modes(orig_saves / "SAVGAME.DAT"),
                  "port", modes(port_saves / "SAVGAME.DAT"))
            if L.compare_games(orig_saves, "E", port_saves, "E",
                               report=lambda s: print("  " + s)):
                status = 1
        if any(results[n][0] for n in results):
            status = 1
    print("AGREES" if status == 0 else "PORT DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

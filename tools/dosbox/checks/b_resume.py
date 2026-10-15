#!/usr/bin/env python3
"""Check #38: resuming a game saved in camp (README, Checks against the
original: "Save in camp, then load that game: it holds mode 2 (0x1407).
Compare how the original resumes it with how it resumes a game saved from
the party menu").

Three games, each loaded from the start menu (Begin Adventuring) in the
original and in eclplay --start-menu, each screen compared with the port's
at the same point of the keys, and the original's mode (DS:4b49) and last
mode (DS:4b4a) where it waits against those recorded:

- J, saved by the port in camp at Throtl's gate (3D, modes 4 then 2; see
  b_camp_screen.py), then Encamp and Save as E in both, the files compared
  (masked);
- D, saved by the port in the outpost inn's camp (game A's start, the inn;
  modes 4, 2), then the inn, camp and Save as E in both, the files compared;
- SAVE/'s A (modes 0 and 0: no camp), its first page.

Two differences are known (tools/dosbox/known-fixes.patch), each judged
against the port built with the fixes: the resumed view at Throtl's gate
(walls: the port without them offsets a two-set WALLDEF record's second set
by its own slot and draws openings where the original draws doors), and
J's game saved again, whose last mode (SAVGAM?.DAT 0x1406) the port's first
block sets to 4 where the original keeps 2 (resume). A's first page has the
full stop of PRESS <ENTER>/<RETURN> TO CONTINUE (fullstop). Once the port
has the fixes the script agrees.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402
import checklib  # noqa: E402
from b_camp_screen import make_save as make_throtl  # noqa: E402

cokbox = L.cokbox

# The original's (where it waits, mode, last mode) at each step, as batch B
# recorded them.
WAITS = {
    "throtl": {"resume": ("475c:0a51", 4, 2)},
    "inn": {"resume": ("3775:18ec", 3, 4)},
    "A": {"resume": ("3775:18ec", 4, 0)},
}


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
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-resume")
    throtl = make_throtl(work)
    inn = make_inn(work)
    v = checklib.Verdict()
    cases = [
        ("throtl", throtl, "J", [(L.load_keys("J"), "resume"), ("e", "camp"),
                                 (L.save_keys("E"), "saved")]),
        ("inn", inn, "D", [(L.load_keys("D"), "resume"), (L.INN, "camp"),
                           (L.save_keys("E"), "saved")]),
        ("A", L.SAVE, "A", [(L.load_keys("A"), "resume")]),
    ]
    for label, game, letter, steps in cases:
        print(f"{label}: SAVGAM{letter}.DAT modes (0x1406, 0x1407) = "
              f"{modes(game / f'SAVGAM{letter}.DAT')}")
        waits = {}
        orig_saves = work / label / "orig-saves"

        def during(box, name):
            waits[name] = (box.where(), box.ds_byte(0x4B49), box.ds_byte(0x4B4A))
            if name == "saved":
                L.keep_saves(box, orig_saves)

        shots, port, fixed_frames, out = S.run_pair(work / label, [game], steps,
                                                    verbose=args.verbose, during=during)
        S.judge(v, shots, port, fixed_frames, work / label)
        for name, (where, mode, last) in waits.items():
            print(f"    {name}: original waits in {where}, mode {mode}, last mode {last}")
            want = WAITS[label].get(name)
            if want:
                v.check(want[0] in str(where) and (mode, last) == want[1:],
                        f"{label} {name}: the original waits in {want[0]}, modes {want[1:]}",
                        f"{where}, modes {(mode, last)}")
        menus = [line for line in out.splitlines() if line.startswith("menu:")]
        print("  eclplay's menus after Begin Adventuring:",
              menus[menus.index("menu: Load Saved Game") + 1:][:4] if "menu: Load Saved Game"
              in menus else menus[-4:])
        if "saved" in waits:
            port_saves = work / label / "eclplay" / "saves"
            keys = S.marked(steps)

            def fixed_saves(label=label, keys=keys, game=game):
                _, saves, _ = cokbox.eclplay(checklib.port_keys(keys), work / label /
                                             "eclplay-fixed-saves", saves=[game], shots=False,
                                             binary=checklib.fixed_eclplay())
                return saves, "E"

            print("  saved again as E: modes original", modes(orig_saves / "SAVGAME.DAT"),
                  "port", modes(port_saves / "SAVGAME.DAT"))
            L.judge_games(v, f"{label}: saved again as E", orig_saves, "E", port_saves, "E",
                          fixed=fixed_saves)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

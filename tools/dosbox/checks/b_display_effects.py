#!/usr/bin/env python3
"""Check #44: Display with effect 0x13 (DEVELOPERS.md, Checks against the original:
"`Display` a character with effect 0x13: it shows `Find Traps`").

The port writes the game: SAVE/'s game A with SIR STRONGSWORD given effect
0x13 (no time, value 0; eclplay --effect 1,13,0,0) saved in the outpost
inn's camp as D. The original and eclplay --start-menu load D, go to the
inn's camp, Magic, Display: each screen is compared with eclplay's at the
same point of the keys, and the port's text must name `Find Traps` (0x13).
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402
import checklib  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--effect", default="13", help="the effect id, hex (default 13)")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-display")
    out, saves, _ = L.port(L.A_TO_CAMP + L.save_keys("D"), work / "make",
                           ["--load", L.SAVE / "SAVGAMA.DAT", "--play",
                            "--effect", f"1,{args.effect},0,0"])
    game = L.copy_game(saves, "D", work / "game")
    print("SFX of SIR STRONGSWORD:", L.sfx(game, "D", 1))
    steps = [(L.load_keys("D") + L.INN, "camp"), ("MD", "display")]
    v, _, _, out = S.check(work / "run", [game], steps, verbose=args.verbose)
    shown = [line for line in out.splitlines() if "Find Traps" in line or "Spell Effects" in line]
    print("  eclplay's text:", shown[:4])
    if args.effect.lower() == "13":
        v.check(any("Find Traps" in line for line in shown), "the port's Display names Find Traps")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #32: the thief skills of a human thief (README, Checks against the
original: "The thief skills include the 7 that 66c2:0b9f reads from the
stack ... Load a saved game with a human thief and compare its thief skills
(+0xdb) with the port's").

The port writes the game: eclplay's test party of four (ALDA fighter, BRAM
cleric, CERA mage, DUNN a level 1 human thief) saved in the outpost inn's
camp as D, with DUNN's skills (+0xdb-+0xe2) then zeroed in the file, so
that what the original shows is what it computed on loading (66c2:0b9f).
The original loads D RUNS times (an interrupt between 66c2:08a6 and
66c2:0b9f could change the stack's byte), the skills read in memory, then
saved in camp as E; the port loads D and saves E the same way. In each run
DUNN's skills in memory must be the port's (its E), and the original's E
the port's, masked.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402

TO_SAVE_D = r"\r\r\r\r\r\rSD"     # block 17's start to the inn's camp, Save D


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--runs", type=int, default=2)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "b-thief")
    v = checklib.Verdict()
    out, saves, _ = L.port(TO_SAVE_D, work / "make", ["--test-party", "4", "--play"], block=17)
    game = work / "game"
    L.copy_game(saves, "D", game)
    rec = L.sav(game, "D", 4)
    print(f"port's {L.name_of(rec)} (race {rec[0x5a]}, thief level {rec[0xff]}): "
          f"skills as the port saved them {rec[0xdb:0xe3].hex(' ')}; zeroed in the file")
    L.patch(game / "CHRDATD4.SAV", {0xDB: bytes(8)})

    # The port loads D and saves E.
    pout, psaves, _ = L.port(L.INN + L.save_keys("E"), work / "port",
                             ["--load", game / "SAVGAMD.DAT", "--play"], saves=[game])
    port_skills = L.sav(psaves, "E", 4)[0xDB:0xE3]

    for run in range(args.runs):
        with L.box(work / f"run{run}", [game], verbose=args.verbose, turbo=True) as box:
            box.keys(L.load_keys("D"))
            box.wait_input(settle=0.3)
            _, raw = L.member(box, "DUNN")
            orig = raw[0xDB:0xE3]
            box.keys(L.INN + L.save_keys("E"))
            box.wait_input(settle=0.3)
            odir = L.keep_saves(box, work / f"run{run}" / "saved")
        v.value(f"run {run}: DUNN's thief skills after loading", orig.hex(" "),
                port_skills.hex(" "))
        L.judge_games(v, f"run {run}: saved E", odir, "E", psaves, "E")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #39: the time Fix rests (README, Checks against the original: "Hurt
a character a few points with a cleric of two first-level spells a day in
the party and Fix: the clock should pass the time of 4888:28f9, 270 minutes
divided by 41 over the points lacking, which depends on the 14 it reads
from the stack").

Copies of SAVE/'s game A, KAL and SIRRION (the clerics, level 2) given
wisdom 12 in the files, so that each has two first-level spells a day
(the load recomputes them: +0x11c 2): the estimate is then 14 + 27 + 27 =
68 and the time 270 minutes (240 + 2 x 15), divided by 68 over the points
lacking, as a byte. SIR STRONGSWORD lacks 4, 5, 9 or 20 of his 27 hit
points (games D-G); one case keeps SAVE/'s wisdom (four spells a day: 300
minutes, the same estimate). In the outpost inn's camp, Fix: the clock
before and after and the hit points are read in the original; the game is
saved again as H and compared with the port's, whose .DAT holds the clock.
With a stack byte other than 14 the times differ: 4 lacking gives 270 / 17
= 15 -> 15 minutes with 14, 270 / 13 = 20 with 0.
"""
import argparse
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402

WIS12 = {0x14: 12, 0x15: 12}
CASES = {"D": (4, True), "E": (5, True), "F": (9, True), "G": (20, True), "I": (5, False)}


def minutes(c):
    return c[1] + 10 * c[2] + 60 * c[3] + 1440 * c[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--cases", default="DEFGI")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-fix-"))
    status = 0
    for letter in args.cases:
        lacking, wis12 = CASES[letter]
        game = work / f"game{letter}"
        if game.exists():
            shutil.rmtree(game)
        L.copy_game(L.SAVE, "A", game, letter)
        L.patch(game / f"CHRDAT{letter}1.SAV", {0x197: 27 - lacking})
        if wis12:
            for n in (5, 6):
                L.patch(game / f"CHRDAT{letter}{n}.SAV", WIS12)
        pout, psaves, _ = L.port(L.A_TO_CAMP + "F" + L.save_keys("H"), work / f"port{letter}",
                                 ["--load", game / f"SAVGAM{letter}.DAT", "--play"], saves=[game])
        php = L.sav(psaves, "H", 1)[0x197]
        with L.box(work / f"run{letter}", [game], verbose=args.verbose, turbo=True) as box:
            box.keys(L.load_keys(letter) + L.A_TO_CAMP)
            box.wait_input(settle=0.3)
            addr, _ = L.member(box, "SIR STRONGSWORD")
            spells = [L.member(box, n)[1][0x11C] for n in ("KAL", "SIRRION")]
            c0 = L.clock(box)
            box.keys("F")
            c1 = L.wait_rest(box)
            hp = box.read(addr + 0x197, 1)[0]
            box.keys(L.save_keys("H"))
            box.wait_input(settle=0.3)
            odir = work / f"run{letter}" / "saved"
            shutil.copytree(box.game / "SAVE", odir, dirs_exist_ok=True)
        print(f"game {letter}: {lacking} lacking, clerics' first-level spells a day {spells}: "
              f"original's Fix passed {minutes(c1) - minutes(c0)} minutes, SIR at {hp} hp; "
              f"port's SIR at {php} hp")
        if L.compare_games(odir, "H", psaves, "H", report=lambda s: print("    " + s)):
            status = 1
    print("AGREES" if status == 0 else "DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

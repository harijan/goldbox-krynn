#!/usr/bin/env python3
"""Check #39: the time Fix rests (docs/checks.md, Checks against the original: "Hurt
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

Judged: the minutes the original's Fix passed against the port's (its H's
clock less the game's) and against those recorded with the 14 (15, 20, 40,
90, 25), the clerics' spells a day and SIR STRONGSWORD's hit points against
the port's H, and the whole of H, masked.
"""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402

WIS12 = {0x14: 12, 0x15: 12}
CASES = {"D": (4, True), "E": (5, True), "F": (9, True), "G": (20, True), "I": (5, False)}
# The minutes Fix passed in batch B's run of the original (the stack's 14).
RECORDED = {"D": 15, "E": 20, "F": 40, "G": 90, "I": 25}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--cases", default="DEFGI")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-fix")
    v = checklib.Verdict()
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
        port_passed = L.minutes(L.dat_clock(psaves, "H")) - L.minutes(L.dat_clock(game, letter))
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
            odir = L.keep_saves(box, work / f"run{letter}" / "saved")
        passed = L.minutes(c1) - L.minutes(c0)
        print(f"game {letter}: {lacking} lacking, clerics' first-level spells a day {spells}")
        v.value(f"game {letter}: the minutes Fix passed", passed, port_passed)
        v.check(passed == RECORDED[letter], f"game {letter}: the original's Fix passed "
                f"{RECORDED[letter]} minutes, as recorded (the stack's 14)", f"{passed}")
        v.value(f"game {letter}: KAL and SIRRION's first-level spells a day", spells,
                [L.sav(psaves, "H", n)[0x11C] for n in (5, 6)])
        v.value(f"game {letter}: SIR STRONGSWORD's hit points after Fix", hp,
                L.sav(psaves, "H", 1)[0x197])
        L.judge_games(v, f"game {letter}: saved H", odir, "H", psaves, "H")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

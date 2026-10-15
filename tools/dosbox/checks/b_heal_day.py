#!/usr/bin/env python3
"""Check #40: a day's healing counted in one camp (README, Checks against the
original: "Rest a full day twice in one camp, and once each in two camps:
the party heals a point a day only counting the ticks of one camp
(DS:7136)").

A copy of SAVE/'s game A as D, SIR STRONGSWORD at 20 of 27 hit points in the
file. Four runs in the outpost inn's camp, in the original and the port:
  1  one camp, a day's rest twice;
  2  two camps (Exit, the inn again), a day each;
  3  one camp, twelve hours twice;
  4  two camps, twelve hours each.
The original's DS:7136 (five-minute ticks toward the day) is read after
each rest, the hit points in memory; the game saved again as G is compared
with the port's.
"""
import argparse
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402

DAY, HALF = "RDAR", "RH" + "A" * 12 + "R"
AGAIN = "E" + L.INN    # Exit the camp, then the inn's camp again
RUNS = {1: [DAY, DAY], 2: [DAY, AGAIN, DAY], 3: [HALF, HALF], 4: [HALF, AGAIN, HALF]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--runs", default="1234")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-heal-"))
    game = work / "game"
    if game.exists():
        shutil.rmtree(game)
    L.copy_game(L.SAVE, "A", game, "D")
    L.patch(game / "CHRDATD1.SAV", {0x197: 20})
    status = 0
    for n in map(int, args.runs):
        steps = RUNS[n]
        pout, psaves, _ = L.port(L.A_TO_CAMP + "".join(steps) + L.save_keys("G"), work / f"port{n}",
                                 ["--load", game / "SAVGAMD.DAT", "--play"], saves=[game])
        heals = pout.count("The Whole Party Is Healed")
        php = L.sav(psaves, "G", 1)[0x197]
        print(f"run {n} {steps}: port: healed {heals} times, SIR STRONGSWORD {php} hp")
        with L.box(work / f"run{n}", [game], verbose=args.verbose, turbo=True) as box:
            box.keys(L.load_keys("D") + L.A_TO_CAMP)
            box.wait_input(settle=0.3)
            addr, _ = L.member(box, "SIR STRONGSWORD")
            seen = [f"DS:7136 {box.ds_word(0x7136)}"]
            for step in steps:
                box.keys(step)
                if step == AGAIN:
                    box.wait_input(settle=0.3)
                    seen.append(f"camp again: DS:7136 {box.ds_word(0x7136)}")
                else:
                    c = L.wait_rest(box)
                    seen.append(f"rested to day {c[4]} hour {c[3]}: DS:7136 {box.ds_word(0x7136)}, "
                                f"hp {box.read(addr + 0x197, 1)[0]}")
            print("  original:", "; ".join(seen))
            box.keys(L.save_keys("G"))
            box.wait_input(settle=0.3)
            odir = work / f"run{n}" / "saved"
            shutil.copytree(box.game / "SAVE", odir, dirs_exist_ok=True)
        print("  saved G, the original's against the port's:")
        if L.compare_games(odir, "G", psaves, "G", report=lambda s: print("    " + s)):
            status = 1
    print("AGREES" if status == 0 else "DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

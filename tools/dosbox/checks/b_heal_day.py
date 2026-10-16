#!/usr/bin/env python3
"""Check #40: a day's healing counted in one camp (DEVELOPERS.md, Checks against the
original: "Rest a full day twice in one camp, and once each in two camps:
the party heals a point a day only counting the ticks of one camp
(DS:7136)").

A copy of SAVE/'s game A as D, SIR STRONGSWORD at 20 of 27 hit points in the
file. Four runs in the outpost inn's camp, in the original and the port:
  1  one camp, a day's rest twice;
  2  two camps (Exit, the inn again), a day each;
  3  one camp, twelve hours twice;
  4  two camps, twelve hours each.
The original's DS:7136 (five-minute ticks toward the day) and SIR
STRONGSWORD's hit points are read in memory after each step. The hit points
must be the port's at the same step (its game saved there), the port's
heals as many; DS:7136, which the port keeps (heal_ticks) but neither shows
nor saves, must be what batch B's run recorded (a full day's 288 ticks
reset it to 0, a new camp clears it, twelve hours leave 144). The game
saved again as G must be the port's, masked.
"""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402

DAY, HALF = "RDAR", "RH" + "A" * 12 + "R"
AGAIN = "E" + L.INN    # Exit the camp, then the inn's camp again
RUNS = {1: [DAY, DAY], 2: [DAY, AGAIN, DAY], 3: [HALF, HALF], 4: [HALF, AGAIN, HALF]}
# DS:7136 after each step in the original, as batch B's run recorded it.
TICKS = {1: [0, 0], 2: [0, 0, 0], 3: [144, 0], 4: [144, 0, 144]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--runs", default="1234")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-heal")
    game = work / "game"
    if game.exists():
        shutil.rmtree(game)
    L.copy_game(L.SAVE, "A", game, "D")
    L.patch(game / "CHRDATD1.SAV", {0x197: 20})
    v = checklib.Verdict()
    for n in map(int, args.runs):
        steps = RUNS[n]
        pout, psaves, _ = L.port(L.A_TO_CAMP + "".join(steps) + L.save_keys("G"), work / f"port{n}",
                                 ["--load", game / "SAVGAMD.DAT", "--play"], saves=[game])
        port_hp = [L.sav(L.saved_by_port(game, "D", "".join(steps[:k + 1]),
                                         work / f"port{n}-{k}", "G"), "G", 1)[0x197]
                   for k in range(len(steps))]
        print(f"run {n} {steps}: port: healed {pout.count('The Whole Party Is Healed')} times, "
              f"SIR STRONGSWORD's hit points after each step {port_hp}")
        with L.box(work / f"run{n}", [game], verbose=args.verbose, turbo=True) as box:
            box.keys(L.load_keys("D") + L.A_TO_CAMP)
            box.wait_input(settle=0.3)
            addr, _ = L.member(box, "SIR STRONGSWORD")
            print(f"  original in camp: DS:7136 {box.ds_word(0x7136)}")
            ticks, hps = [], []
            for step in steps:
                box.keys(step)
                if step == AGAIN:
                    box.wait_input(settle=0.3)
                else:
                    L.wait_rest(box)
                ticks.append(box.ds_word(0x7136))
                hps.append(box.read(addr + 0x197, 1)[0])
            print(f"  original after each step: DS:7136 {ticks}, hit points {hps}, the clock "
                  f"{L.clock(box)[1:5]}")
            box.keys(L.save_keys("G"))
            box.wait_input(settle=0.3)
            odir = L.keep_saves(box, work / f"run{n}" / "saved")
        v.value(f"run {n}: SIR STRONGSWORD's hit points after each step", hps, port_hp)
        heals = sum(1 for a, b in zip([20] + hps, hps) if b > a)
        v.value(f"run {n}: the heals", heals, pout.count("The Whole Party Is Healed"))
        v.check(ticks == TICKS[n], f"run {n}: the original's DS:7136 after each step is "
                f"{TICKS[n]}, as recorded (the port's is not shown)", str(ticks))
        L.judge_games(v, f"run {n}: saved G", odir, "G", psaves, "G")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

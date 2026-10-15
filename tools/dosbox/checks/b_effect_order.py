#!/usr/bin/env python3
"""Check #34: the timers' walk restarting (README, Checks against the
original: "Give a character with no effects ... a long timed effect, then a
short one, then any third ... let time pass until the short one ends: in
the ten minutes in which it ends, the long one should lose twenty
(57e4:0171 restarts its walk). With only the two, it loses ten.").

SAVE/'s game A, SIR STRONGSWORD (a human knight with no effects) given a
.SFX in the copies: game D with effects 0x01 for 120 minutes, 0x02 for 15
and 0x08 for 120, in that order (as three spells cast in that order leave
them; no handler on removal); game E with only the first two. Each is
loaded in the original and the port, taken to the outpost inn's camp,
rested 20 minutes (Rest, Mins, Add four times, Rest), the effects read in
memory, and saved as F. The original's effects (id, minutes) after the rest
must be the port's (its F), the long one must lose more where a third
follows (the walk's restart), and the original's F must be the port's,
masked.
"""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402

REST_20 = "RMAAAAR"   # Rest, Mins, Add 5 four times, Rest
GAMES = {"D": [(0x01, 120, 0, 0), (0x02, 15, 0, 0), (0x08, 120, 0, 0)],
         "E": [(0x01, 120, 0, 0), (0x02, 15, 0, 0)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-order")
    v = checklib.Verdict()
    lost = {}
    for letter, effects in GAMES.items():
        game = work / f"game{letter}"
        if game.exists():
            shutil.rmtree(game)
        L.copy_game(L.SAVE, "A", game, letter)
        L.write_sfx(game / f"CHRDAT{letter}1.SFX", effects)
        print(f"game {letter}: SIR STRONGSWORD given {[(hex(i), m) for i, m, _, _ in effects]}")
        keys = L.A_TO_CAMP + REST_20 + L.save_keys("F")
        pout, psaves, _ = L.port(keys, work / f"port{letter}",
                                 ["--load", game / f"SAVGAM{letter}.DAT", "--play"], saves=[game])
        port_after = L.sfx(psaves, "F", 1)
        with L.box(work / f"run{letter}", [game], verbose=args.verbose, turbo=True) as box:
            box.keys(L.load_keys(letter) + L.A_TO_CAMP)
            box.wait_input(settle=0.3)
            addr, _ = L.member(box, "SIR STRONGSWORD")
            c0 = L.clock(box)
            print("  original, in camp:", [(hex(e[0]), e[1]) for e in L.effects_at(box, addr + 0xE3)])
            box.keys(REST_20)
            c1 = L.wait_rest(box)
            orig_after = L.effects_at(box, addr + 0xE3)
            box.keys(L.save_keys("F"))
            box.wait_input(settle=0.3)
            odir = L.keep_saves(box, work / f"run{letter}" / "saved")
        print(f"  the clock {c0[1:5]} -> {c1[1:5]}")
        orig = [(hex(e[0]), e[1]) for e in orig_after]
        v.value(f"game {letter}: SIR STRONGSWORD's effects (id, minutes) after 20 minutes",
                orig, [(hex(e[0]), e[1]) for e in port_after])
        lost[letter] = 120 - dict(orig).get("0x1", 120)
        L.judge_games(v, f"game {letter}: saved F", odir, "F", psaves, "F")
    v.check(lost["D"] > lost["E"], "the long effect loses more in the original where a third "
            "follows (57e4:0171's restart)", f"lost {lost['D']} with three, {lost['E']} with two")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

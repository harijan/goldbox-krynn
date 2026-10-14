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
memory, and saved as F; the original's F is compared with the port's.
"""
import argparse
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402

REST_20 = "RMAAAAR"   # Rest, Mins, Add 5 four times, Rest
GAMES = {"D": [(0x01, 120, 0, 0), (0x02, 15, 0, 0), (0x08, 120, 0, 0)],
         "E": [(0x01, 120, 0, 0), (0x02, 15, 0, 0)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-order-"))
    status = 0
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
        print("  port, after 20 minutes:", [(hex(i), m) for i, m, _, _ in port_after])
        with L.box(work / f"run{letter}", [game], verbose=args.verbose, turbo=True) as box:
            box.keys(L.load_keys(letter) + L.A_TO_CAMP)
            box.wait_input(settle=0.3)
            addr, _ = L.member(box, "SIR STRONGSWORD")
            c0 = L.clock(box)
            print("  original, in camp:", [(hex(e[0]), e[1]) for e in L.effects_at(box, addr + 0xE3)])
            box.keys(REST_20)
            box.wait_input(settle=0.3)
            orig_after = L.effects_at(box, addr + 0xE3)
            print(f"  original, after the rest (clock {c0[1:5]} -> {L.clock(box)[1:5]}):",
                  [(hex(e[0]), e[1]) for e in orig_after])
            box.keys(L.save_keys("F"))
            box.wait_input(settle=0.3)
            odir = work / f"run{letter}" / "saved"
            shutil.copytree(box.game / "SAVE", odir, dirs_exist_ok=True)
        if [(e[0], e[1]) for e in orig_after] != [(e[0], e[1]) for e in port_after]:
            status = 1
            print("  <- the effects differ")
        print("  saved F, the original's against the port's:")
        if L.compare_games(odir, "F", psaves, "F", report=lambda s: print("    " + s)):
            status = 1
    print("AGREES" if status == 0 else "DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

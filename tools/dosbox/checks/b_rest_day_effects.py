#!/usr/bin/env python3
"""Check #33: game B rested a day keeps its effects (README, Checks against
the original: "Load saved game B and rest a day: the effects in the .SFX
files of its characters, all permanent, should be unchanged when it is
saved again").

The original (DOSBox-X) and the port (eclplay) load SAVE/'s game B, go from
its start to the outpost inn's camp, rest a day (Rest, Days, Add, Rest),
and save as C. Each .SFX (effects: id, minutes, value, run-on-removal)
must be SAVE/'s B's in the original and the port, the original's clock a
day on, its screen after the rest the port's at the same point of the
keys, and the whole saved game the port's, masked as save_again.py does.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import b_screens_lib as S  # noqa: E402
import checklib  # noqa: E402

REST_DAY = "RDAR"   # Rest, Days, Add (1), Rest


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-restday")
    v = checklib.Verdict()
    keys_camp = L.load_keys("B") + L.A_TO_CAMP
    after = REST_DAY + L.save_keys("C")
    pout, psaves, _ = L.cokbox.eclplay(keys_camp + after, work / "port", saves=[L.SAVE],
                                       shots=False)
    print("port:", " | ".join(pout.strip().splitlines()[-6:]))
    marked = keys_camp + REST_DAY + r"\srest."
    port = S.frames_at(marked, work / "frames", saves=[L.SAVE])
    with L.box(work, [L.SAVE], verbose=args.verbose, turbo=True) as box:
        box.keys(keys_camp)
        box.wait_input(settle=0.3)
        c0 = L.clock(box)
        box.keys(REST_DAY)
        c1 = L.wait_rest(box)
        box.wait_input(settle=0.3)
        shot = box.screenshot(work / "after-rest.png")
        print(f"original: clock {c0} -> {c1}; waits at {box.where()}")
        box.keys(L.save_keys("C"))
        box.wait_input(settle=0.3)
        odir = L.keep_saves(box, work / "orig-saved")
    v.screen("the screen after the rest", shot, port["rest"],
             fixed=lambda: S.frames_at(marked, work / "frames-fixed", fixed=True,
                                       saves=[L.SAVE])["rest"], work=work)
    v.check(L.minutes(c1) - L.minutes(c0) == 1440, "the original rested a day",
            f"{c0[1:5]} -> {c1[1:5]}")
    for n in range(1, 7):
        before = L.sfx(L.SAVE, "B", n)
        who = L.name_of(L.sav(odir, "C", n))
        v.check(L.sfx(odir, "C", n) == before, f"{who}'s effects in the original's C are B's",
                str(before))
        v.value(f"{who}'s effects saved", L.sfx(odir, "C", n), L.sfx(psaves, "C", n))
    L.judge_games(v, "saved C", odir, "C", psaves, "C")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

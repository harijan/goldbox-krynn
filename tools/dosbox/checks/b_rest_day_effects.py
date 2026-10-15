#!/usr/bin/env python3
"""Check #33: game B rested a day keeps its effects (README, Checks against
the original: "Load saved game B and rest a day: the effects in the .SFX
files of its characters, all permanent, should be unchanged when it is
saved again").

The original (DOSBox-X) and the port (eclplay) load SAVE/'s game B, go from
its start to the outpost inn's camp, rest a day (Rest, Days, Add, Rest),
and save as C. The .SFX files are compared with SAVE/'s B (effects: id,
minutes, value, run-on-removal), and the whole saved games with each
other, masked as save_again.py does.
"""
import argparse
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402

REST_DAY = "RDAR"   # Rest, Days, Add (1), Rest


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-restday-"))
    keys_camp = L.load_keys("B") + L.A_TO_CAMP
    after = REST_DAY + L.save_keys("C")
    pout, psaves, _ = L.port_menu(keys_camp + after, work / "port", saves=[L.SAVE], shots=False)
    print("port:", " | ".join(pout.strip().splitlines()[-6:]))
    with L.box(work, [L.SAVE], verbose=args.verbose, turbo=True) as box:
        box.keys(keys_camp)
        box.wait_input(settle=0.3)
        c0 = L.clock(box)
        box.keys(REST_DAY)
        box.wait_input(settle=0.3)
        c1 = L.clock(box)
        box.screenshot(work / "after-rest.png")
        print(f"original: clock {c0} -> {c1}; waits at {box.where()}")
        box.keys(L.save_keys("C"))
        box.wait_input(settle=0.3)
        odir = work / "orig-saved"
        shutil.copytree(box.game / "SAVE", odir, dirs_exist_ok=True)
    status = 0
    for n in range(1, 7):
        before = L.sfx(L.SAVE, "B", n)
        o, p = L.sfx(odir, "C", n), L.sfx(psaves, "C", n)
        same = before == o == p
        status |= not same
        print(f"{L.name_of(L.sav(odir, 'C', n)):16} SAVE/ {before}  original {o}  port {p}"
              + ("" if same else "  <- differ"))
    print("saved C, the original's against the port's:")
    if L.compare_games(odir, "C", psaves, "C", report=lambda s: print("  " + s)):
        status = 1
    print("AGREES" if status == 0 else "DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

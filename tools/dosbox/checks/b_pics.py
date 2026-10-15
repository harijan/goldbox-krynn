#!/usr/bin/env python3
"""Check #42: Alter's Pics takes special keys by their scan codes' letters
(README, Checks against the original: "In Alter's Pics, press the down
arrow: the pictures flag changes").

Game A in the outpost inn's camp: Alter, Pics, the down arrow (scan 0x50,
P: Pics), F7 (0x41, A: Animation, not offered once the pictures are off),
Exit, Exit, then Save as F. The original's flags (pictures DS:4b4d,
animation DS:4b4f) are read after each key and must be those recorded (1 1,
0 1, 0 0, 0 0: the port's are seen only in its menus and its save); each
screen is compared with the port's at the same point of the keys (eclplay
--load, the same keys); the saved game's 0x4bff (2 x 4b4d + 4b4f) and every
file must be the port's, masked.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402

STEPS = [("Alter, Pics", "AP"), ("down arrow", r"\v"), ("F7", r"\#41"), ("Exit", "E")]
# (pictures, animation) after each step in batch B's run of the original.
FLAGS = [(1, 1), (0, 1), (0, 0), (0, 0)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-pics")
    v = checklib.Verdict()
    load = ["--load", L.SAVE / "SAVGAMA.DAT", "--play"]
    keys = "".join(k for _, k in STEPS) + "E" + L.save_keys("F")
    out, psaves, _ = L.port(L.A_TO_CAMP + keys, work / "port", load)
    print("port's Pics menus:", [line for line in out.splitlines() if line.startswith("menu: Pics")])
    marked = L.A_TO_CAMP + "".join(rf"{k}\s{i}." for i, (_, k) in enumerate(STEPS))
    port = checklib.frames_at(marked, work / "frames", start_menu=False, args=load)
    fixed = {}

    def fixed_frame(name):
        if not fixed:
            fixed.update(checklib.frames_at(marked, work / "frames-fixed", start_menu=False,
                                            args=load, binary=checklib.fixed_eclplay()))
        return fixed.get(name)

    flags = []
    with L.box(work, [L.SAVE], verbose=args.verbose, turbo=True) as box:
        box.keys(L.load_keys("A") + L.A_TO_CAMP)
        box.wait_input(settle=0.3)
        print(f"original in camp: pictures {box.ds_byte(0x4b4d)}, animation {box.ds_byte(0x4b4f)}")
        for i, (what, k) in enumerate(STEPS):
            box.keys(k)
            box.wait_input(settle=0.5)
            shot = box.screenshot(work / f"orig-{i}.png")
            flags.append((box.ds_byte(0x4b4d), box.ds_byte(0x4b4f)))
            v.screen(f"screen after {what}", shot, port.get(str(i)),
                     fixed=lambda n=str(i): fixed_frame(n), work=work)
        box.keys("E" + L.save_keys("F"))
        box.wait_input(settle=0.3)
        odir = L.keep_saves(box, work / "saved")
    v.check(flags == FLAGS, "the original's (pictures, animation) after each step as recorded",
            str(flags))
    v.value("the saved 0x4bff", L.dat_var(odir, "F", 0x4BFF), L.dat_var(psaves, "F", 0x4BFF))
    L.judge_games(v, "saved F", odir, "F", psaves, "F")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #42: Alter's Pics takes special keys by their scan codes' letters
(README, Checks against the original: "In Alter's Pics, press the down
arrow: the pictures flag changes").

Game A in the outpost inn's camp: Alter, Pics, the down arrow (scan 0x50,
P: Pics), F7 (0x41, A: Animation, not offered once the pictures are off),
Exit, Exit, then Save as F. The original's flags (pictures DS:4b4d,
animation DS:4b4f) are read after each key and its Pics menu screenshotted;
the saved game's 0x4bff (2 x 4b4d + 4b4f) and every file are compared with
the port's, which takes the same keys.
"""
import argparse
import pathlib
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402

STEPS = [("Alter, Pics", "AP"), ("down arrow", r"\v"), ("F7", r"\#41"), ("Exit", "E")]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-pics-"))
    keys = "".join(k for _, k in STEPS) + "E" + L.save_keys("F")
    out, psaves, shots = L.port(L.A_TO_CAMP + keys, work / "port",
                                ["--load", L.SAVE / "SAVGAMA.DAT", "--play"], shots=True)
    menus = [line for line in out.splitlines() if line.startswith("menu: Pics")]
    print("port's Pics menus:", menus)
    port_dat = (psaves / "SAVGAMF.DAT").read_bytes()
    print(f"port's saved 0x4bff: {port_dat[0x1ff]} (game A's {(L.SAVE / 'SAVGAMA.DAT').read_bytes()[0x1ff]})")

    status = 0
    with L.box(work, [L.SAVE], verbose=args.verbose, turbo=True) as box:
        box.keys(L.load_keys("A") + L.A_TO_CAMP)
        box.wait_input(settle=0.3)
        print(f"original in camp: pictures {box.ds_byte(0x4b4d)}, animation {box.ds_byte(0x4b4f)}")
        for i, (what, k) in enumerate(STEPS):
            box.keys(k)
            box.wait_input(settle=0.5)
            shot = box.screenshot(work / f"orig-{i}.png")
            n, area, path = L.best_match(shot, shots)
            print(f"original after {what}: pictures {box.ds_byte(0x4b4d)}, animation "
                  f"{box.ds_byte(0x4b4f)}; screen vs port's nearest {path.name}: {n} pixels differ")
        box.keys("E" + L.save_keys("F"))
        box.wait_input(settle=0.3)
        odir = work / "saved"
        shutil.copytree(box.game / "SAVE", odir, dirs_exist_ok=True)
    orig_dat = (odir / "SAVGAMF.DAT").read_bytes()
    print(f"original's saved 0x4bff: {orig_dat[0x1ff]}")
    print("saved F compared with the port's F:")
    if L.compare_games(odir, "F", psaves, "F", report=lambda s: print("  " + s)):
        status = 1
    print("AGREES" if status == 0 else "PORT DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

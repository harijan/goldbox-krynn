#!/usr/bin/env python3
"""Check #48: Use of an item whose +0x3d is 0x80 (spell 0), then a cast
from memory (README, Checks against the original: "Use an item whose
+0x3d is 0x80, then cast a spell from memory: it should not say 'casts',
and the spell should stay memorized").

Game A, KAL's readied Ring Mail given +0x3d 0x80, +0x3e 0 in the file, in
the outpost inn's camp: View, Items, Use (the first item), Escape, Exit;
Magic, Cast, Bless (four rows up from the highlighted last), Exit, Exit;
Save as F. In the original (normal speed, so that the cast's pauses can be
seen) DS:711d is read after the Use, the screen is shot as fast as it comes
during the cast, and KAL's memorized spells (+0x1e-+0x57) read after it.
A second run without the Use is the control: there the cast says "casts"
and Bless is forgotten. Both runs' saved games are compared with the
port's.
"""
import argparse
import pathlib
import shutil
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402

USE = r"\v\v\v\vVIU\eE"
CAST = r"MC\^\^\^\^"
LEAVE = "EE"


def original(work, game, use, verbose):
    """Returns (memorized before, after, DS:711d after the use, frames dir, saves)."""
    with L.box(work, [game], verbose=verbose) as box:
        box.keys(L.load_keys("A") + L.A_TO_CAMP)
        box.wait_input(settle=0.3)
        before = L.member(box, "KAL")[1][0x1E:0x58]
        flag = None
        if use:
            box.keys(USE)
            box.wait_input(settle=0.3)
            flag = box.ds_byte(0x711D)
        else:
            box.keys(r"\v\v\v\v")
        box.keys(CAST)
        box.wait_input(settle=0.3)
        box.key("\r")
        frames = work / "frames"
        frames.mkdir(parents=True, exist_ok=True)
        seen = []
        end = time.monotonic() + 12
        while time.monotonic() < end:
            shot = box.screenshot()
            if not seen or shot.pixels != seen[-1].pixels:
                shot.save_png(frames / f"cast-{len(seen):02d}.png")
                seen.append(shot)
            if box.waiting(reads_only=True):
                break
        box.wait_input(settle=0.3)
        after = L.member(box, "KAL")[1][0x1E:0x58]
        flag_after = box.ds_byte(0x711D)
        box.keys(LEAVE + L.save_keys("F"))
        box.wait_input(settle=0.3)
        odir = work / "saved"
        shutil.copytree(box.game / "SAVE", odir, dirs_exist_ok=True)
    return before, after, flag, flag_after, len(seen), odir


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-spell0-"))
    game = L.copy_game(L.SAVE, "A", work / "game")
    L.patch(game / "CHRDATA5.STF", {0x3D: 0x80, 0x3E: 0})
    status = 0
    for name, use in (("with the Use", True), ("control, no Use", False)):
        keys = (USE if use else r"\v\v\v\v") + CAST + r"\r" + LEAVE + L.save_keys("F")
        out, psaves, _ = L.port(L.A_TO_CAMP + keys, work / name.split(",")[0].replace(" ", "-") / "port",
                                ["--load", game / "SAVGAMA.DAT", "--play"], saves=[game])
        said = [ln[7:] for ln in out.splitlines() if ln.startswith("print: ")][-14:]
        print(f"{name}: port says {' / '.join(said)}")
        print(f"  port's saved KAL memorized: {L.sav(psaves, 'F', 5)[0x1E:0x58].hex()}")
        sub = work / name.split(",")[0].replace(" ", "-")
        before, after, flag, flag_after, nframes, odir = original(sub, game, use, args.verbose)
        print(f"  original: DS:711d after the Use {flag}, after the cast {flag_after}; "
              f"memorized before {before.hex()}\n            after  {after.hex()}; "
              f"{nframes} frames of the cast in {sub / 'frames'}")
        print("  saved F compared with the port's F:")
        if L.compare_games(odir, "F", psaves, "F", report=lambda s: print("    " + s)):
            status = 1
        if use and before != after:
            status = 1
    print("AGREES" if status == 0 else "PORT DIFFERS", f"(files in {work})")
    return status


if __name__ == "__main__":
    sys.exit(main())

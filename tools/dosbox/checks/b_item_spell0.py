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
and Bless is forgotten.

Judged in each run: the original's screens from the cast's Enter to the
next key read against the port's frames there (eclplay with a shot at each
delay, in order); KAL's memorized spells after the cast against the port's
(its game saved after Exit, Exit) and kept (with the Use) or Bless gone
(the control); the port's text with "casts" only in the control; DS:711d,
which the port keeps but neither shows nor saves, as recorded (1 after the
Use and after the cast, 0 in the control); the saved game the port's,
masked.
"""
import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402

USE = r"\v\v\v\vVIU\eE"
CAST = r"MC\^\^\^\^"
LEAVE = "EE"


def original(work, game, use, verbose):
    """Returns (memorized before, after, DS:711d after the use and after the
    cast, the cast's screens, the screenshots running that showed each, the
    saves directory)."""
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
        (work / "frames").mkdir(parents=True, exist_ok=True)
        seen, counts = [], []
        end = time.monotonic() + 12
        while time.monotonic() < end:
            done = box.waiting(reads_only=True)
            shot = box.screenshot()
            if not seen or shot.pixels != seen[-1].pixels:
                shot.save_png(work / "frames" / f"cast-{len(seen):02d}.png")
                seen.append(shot)
                counts.append(0)
            counts[-1] += 1
            if done:
                break
        box.wait_input(settle=0.3)
        after = L.member(box, "KAL")[1][0x1E:0x58]
        flag_after = box.ds_byte(0x711D)
        box.keys(LEAVE + L.save_keys("F"))
        box.wait_input(settle=0.3)
        odir = L.keep_saves(box, work / "saved")
    return before, after, flag, flag_after, seen, counts, odir


def port_cast(work, game, keys):
    """The port's frames from the key read where the cast's Enter is typed to
    the next (a shot at each delay too)."""
    exe = checklib.eclplay_with_pauses()
    args = ["--load", game / "SAVGAMA.DAT", "--play"]
    _, _, upto = L.port(L.A_TO_CAMP + keys, work / "upto", args, saves=[game], shots=True,
                        binary=exe)
    _, _, shots = L.port(L.A_TO_CAMP + keys + "\\r", work / "through", args, saves=[game],
                         shots=True, binary=exe)
    return [L.cokbox.Screen.load(p) for p in shots[len(upto) - 1:]]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-spell0")
    game = L.copy_game(L.SAVE, "A", work / "game")
    L.patch(game / "CHRDATA5.STF", {0x3D: 0x80, 0x3E: 0})
    v = checklib.Verdict()
    for name, use in (("with the Use", True), ("control, no Use", False)):
        sub = work / name.split(",")[0].replace(" ", "-")
        pre = (USE if use else r"\v\v\v\v") + CAST
        keys = pre + r"\r" + LEAVE + L.save_keys("F")
        out, psaves, _ = L.port(L.A_TO_CAMP + keys, sub / "port",
                                ["--load", game / "SAVGAMA.DAT", "--play"], saves=[game])
        said = [ln[7:] for ln in out.splitlines() if ln.startswith("print: ")][-14:]
        print(f"{name}: port says {' / '.join(said)}")
        frames = port_cast(sub / "port-cast", game, pre)
        before, after, flag, flag_after, seen, counts, odir = original(sub, game, use,
                                                                       args.verbose)
        print(f"  original: DS:711d after the Use {flag}, after the cast {flag_after}; "
              f"memorized before {before.hex()}\n            after  {after.hex()}; "
              f"{len(seen)} screens of the cast in {sub / 'frames'}, the port's "
              f"{len(frames)} frames; screenshots running of each screen {counts}")
        L.in_order(v, f"{name}: the cast's screen", seen, frames, counts)
        v.value(f"{name}: KAL's memorized spells after the cast", after.hex(),
                L.sav(psaves, "F", 5)[0x1E:0x58].hex())
        v.check((before == after) == use, f"{name}: the spell " +
                ("stays memorized" if use else "is forgotten") + " in the original")
        casts = any("casts" in line for line in said)
        v.check(casts != use, f"{name}: the port's text " + ("does not say" if use else "says")
                + " casts")
        v.check((flag, flag_after) == ((1, 1) if use else (None, 0)),
                f"{name}: the original's DS:711d as recorded (the port's is not shown)",
                f"after the Use {flag}, after the cast {flag_after}")
        L.judge_games(v, f"{name}: saved F", odir, "F", psaves, "F")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

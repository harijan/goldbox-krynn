#!/usr/bin/env python3
"""The Dark Queen of Krynn's menus in the original against eclplay --game
dark-queen (DARK-QUEEN.md's step 2: the window library, the start;
docs/checks.md, "Checks against the original": dqk_menus), compared in RGB
with the game's own mouse cursor (the port's with --mouse): main's menu,
Play's camp menu without a party (7368:046f), its Load's "load from
where?" (604b:2e87) and Escape back, Exit From Game's "Exit to DOS?" and
No; then, past Begin Adventuring as dqk_views goes (SAVGAMA, block 1, the
copy protection answered), block 1's developers' menus: JUMPER, NXT
twice, BPIC's "WHAT PICTURE #" and its INPUT NUMBER (6c70:2e7c) with 240
typed, and the big picture (63ae:0321: BIGPIC 240, the overland's top
half) with its YES NO; and, in a second run, Hizden's inn (block 18, a
step north): its greeting, its VERTICAL MENU (1bdb:1f25), Down, Return
and the text after it.

The port runs main's menu from --start-menu, block 1 with the keys typed,
and the inn from its event (--start 898b) with the original's facing and
lamps. Compared: the whole screen, but the party's list and status (no
party in the port's block runs) where the adventure's screen shows them,
the field's caret (it blinks, 6431:0001) and the inn's lamps (the port's
inn run draws its frame before its --set: dqk_party compares the lamps). The original's
colour cycles (261b:05be) are stopped in the box (a retf), so that its
pictures with cycles (picture 142, BIGPIC 240) show their colours as set,
as the port's shots do, and its screens settle. About two minutes.

    python3 tools/dosbox/checks/dqk_menus.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ("--game", "dark-queen")

PARTY = (136, 8, 176, 120, "the party's list and status (no party yet)")
CARET = (8, 186, 312, 12, "the input field's caret, blinking")
# The lamps under the view: the original's are its saved game's (variables
# 0x1fa-0x1fc, 2 each all the way), drawn at Begin Adventuring (20b2:00e4);
# the port's inn run draws its frame as eclplay starts, before its --set,
# with 0 (dqk_party compares them from the start menu).
LAMPS = (40, 120, 56, 8, "the lamps under the view (eclplay's frame before its --set)")
DS_VARS = 0xB20F
DS_SQUARE = 0xC9F0


def freeze_cycles(box):
    """261b:05be, the colours' cycles' step, made to return at once
    (retf), in the box only: the original's colours then stay as they are
    set, as the port's are at its shots; each picture with cycles is
    compared so (else the screen never settles)."""
    at = (box.ds - 0x422E + 0x261B) * 16 + 0x05BE
    if box.read(at, 1) != b"\x55":  # push bp
        raise cokbox.BoxError("261b:05be is not where it was expected")
    box.write(at, b"\xcb")


def eclplay(work, name, args, block=None):
    """eclplay's shots (--shots, with the mouse) of a run, and its output."""
    shots = work / f"port-{name}"
    if shots.exists():
        shutil.rmtree(shots)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--mouse", "--shots", str(shots),
                             *map(str, args), str(dqk_lib.ASSETS),
                             *([str(block)] if block else [])],
                            capture_output=True, text=True, cwd=ROOT, timeout=120)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    return [cokbox.RGBScreen.load(p) for p in
            sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))]


def at(shots, i):
    return shots[i] if len(shots) > i else None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_menus")
    out = work / "original"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    verdict = checklib.Verdict()

    # The port's start: the title passed by Escape (shot 0 its first
    # scene), main's menu (1), the camp (2), Load's question (3), the camp
    # (4), Exit's question (5), the camp (6).
    start = eclplay(work, "start", ["--start-menu", "--keys", "\\k\\ePL\\eEN"])

    with cokbox.Box(workdir=work / "box", game="dark-queen", verbose=opts.verbose,
                    timeout=900) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        dqk_lib.escape_title(box)
        freeze_cycles(box)
        steps = [("main's menu", None, 1), ("the camp's menu", "p", 2),
                 ("Load's \"load from where?\"", "l", 3), ("the camp after Escape", "\\e", 4),
                 ("Exit's \"Exit to DOS?\"", "e", 5), ("the camp after No", "n", 6)]
        for what, keys, i in steps:
            if keys is not None:
                box.keys(keys)
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"{checklib.slug(what)}.png")
            verdict.screen(what, shot, at(start, i), work=work)
        # Begin Adventuring in block 1 (dqk_views.begin), a key at a time.
        for key in "lta":
            box.keys(key)
            box.settled(tolerance=64)
        variables = box.far(box.ds * 16 + DS_VARS)
        box.write(variables + 0x16, b"\x01")
        lamps = [a for i, b in enumerate(box.read(variables + 0x1fa, 3))
                 for a in ("--set", f"{0x1fa + i:x}={b:x}")]
        box.keys("b")
        box.settled(tolerance=64)
        _, _, answer = dqk_lib.protection(box)
        box.keys(answer + "\\r")
        box.settled(tolerance=64)
        # Block 1: JUMPER, NXT, NXT, BPIC, 240 typed, Return.
        jumper = eclplay(work, "jumper", ["--set", "22=1", *lamps, "--keys", "JNNB240\\r"], 1)
        steps = [("block 1's developers' menu", "j", 1, [PARTY]),
                 ("its second page", "n", 2, [PARTY]),
                 ("its third page", "n", 3, [PARTY]),
                 ("BPIC's INPUT NUMBER, 240 typed", "b240", 7, [PARTY, CARET]),
                 ("the big picture 240 and YES NO", "\\r", 8, [])]
        for what, keys, i, masks in steps:
            box.keys(keys)
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"{checklib.slug(what)}.png")
            verdict.screen(what, shot, at(jumper, i), masks=masks, work=work)
    # Hizden's inn: block 1's HIZD (block 18, as dqk_views), a step north.
    with cokbox.Box(workdir=work / "box", game="dark-queen", verbose=opts.verbose,
                    timeout=900) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        dqk_lib.escape_title(box)
        freeze_cycles(box)
        dqk_lib.load_saved_game(box)
        variables = box.far(box.ds * 16 + DS_VARS)
        box.write(variables + 0x16, b"\x01")
        box.keys("b")
        box.settled(tolerance=64)
        _, _, answer = dqk_lib.protection(box)
        box.keys(answer + "\\r")
        box.settled(tolerance=64)
        box.keys("h")
        box.settled(tolerance=64)
        box.keys("\\^")
        shot = box.settled(tolerance=64)
        x, y, facing = box.ds_read(DS_SQUARE, 3)
        variables = box.far(box.ds * 16 + DS_VARS)
        lamps = [a for i, b in enumerate(box.read(variables + 0x1fa, 3))
                 for a in ("--set", f"{0x1fa + i:x}={b:x}")]
        inn = eclplay(work, "inn", ["--start", "898b", "--set", f"34={x:x}", "--set",
                                    f"35={y:x}", "--set", f"11={facing // 2:x}", "--set",
                                    "22=1", "--set", "16=1", "--set", "a4=1", *lamps, "--keys",
                                    "\\r\\v\\r"], 18)
        shot.save_png(out / "inn.png")
        verdict.screen("the inn's greeting", shot, at(inn, 0), masks=[PARTY, LAMPS], work=work)
        for what, keys, i in [("the inn's VERTICAL MENU", "\\r", 1),
                              ("the inn's menu after Down", "\\v", 2),
                              ("the inn's answer", "\\r", 3)]:
            box.keys(keys)
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"{checklib.slug(what)}.png")
            verdict.screen(what, shot, at(inn, i), masks=[PARTY, LAMPS], work=work)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

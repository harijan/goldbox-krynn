#!/usr/bin/env python3
"""The Dark Queen of Krynn in the original against eclplay --game dark-queen
(DARK-QUEEN.md's step 1; docs/checks.md, "Checks against the original":
dqk_views): the title's eight scenes and its menu; a script's text and the
3D view in two places of two maps; and the view turned and stepped in the
adventure loop, compared in RGB.

The original loads SAVGAMA.QSV (TDQK's game A) as dqk_boot does; before
Begin Adventuring its block variable (0x16 of the 1 KB at far DS:b20f) is
set to 1, so that the game starts in block 1, the developers' "jumper"
(5f07:10cd: the block 0x16 holds, block 4 for 0): its LOAD FILES 2 1 puts
the party in Palanthas (GEO 2) at 3,3 facing north with its question and
menu; D (DARG) then enters block 14 (NEWECL 14), whose load vector puts it
in the Dargonesti's sea (GEO 14, wall sets 13, 14 and 11) at 11,17 facing
east with a text; Return ends it, and the party turns and steps in the
adventure loop, its square and facing read from DS:c9f0-c9f2 at each, and
is then put on two squares whose views show the map's third wall set
(slot 2, types 11-15), its square and facing written there and turned
left. The title's scenes are taken as the original waits in each (its
loop's scene number, dqk_lib.title_scene), not by time.

The port runs the same blocks headless: block 1 to its menu, then D and
block 14 to its text,
and for each place of the loop block 14's load vector from its LOAD FILES
(--start 806f) with its SAVE of the square and facing (8080, 8086, 808c)
given the original's, and EXIT in place of its MUSIC after the view is drawn
(CALL [0001]). Compared: the whole screen for the title and its menu (but
the mouse's pointer, and the colours the title's scenes cycle, 261b:05be,
which the port does not run); the screen but the party's list and status
(cells 17-38 of rows 1-15, not ported) and the bottom bar for the texts;
the view (cells 3-13) for the loop's places. In run_all.py's Dark Queen set
(make dqk-checks). About two minutes.

    python3 tools/dosbox/checks/dqk_views.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))
import hlib  # noqa: E402  (tools/hlib.py)

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ("--game", "dark-queen")

POINTER = (1, 1, 16, 16, "the mouse's pointer (ALWAYS.TLB 2, the mouse driver's)")
# What the port does not draw yet: the party's list and status, and the
# adventure loop's bar.
PARTY = (136, 8, 176, 120, "the party's list and status (no party yet)")
BAR = (0, 184, 320, 16, "the bottom bar (the adventure's menu not ported)")
VIEW = (24, 24, 88, 88)
# The colours the title's scenes cycle and twinkle (261b:05be runs them;
# the port draws the first step), each where the scene shows them.
CYCLES = {
    1: [(32, 70, 256, 28, "the logo's cycled colours")],
    2: [(20, 101, 280, 13, "the logo's spark")],
    5: [(8, 16, 304, 168, "the letters' cycled fire")],
    4: [(187, 89, 13, 4, "the title's twinkle")],
}
# The variables of the 1 KB at far DS:b20f.
DS_VARS = 0xB20F
# The party's square and facing (8a8b's DS:c9f0-c9f2).
DS_SQUARE = 0xC9F0
# The places of the adventure loop, a box's run each: the jumper's key, the
# block it enters, the port's start there (its LOAD FILES) and what it sets
# there (the square and facing: block 14's own SAVEs of them, 8080, 8086
# and 808c, given the original's, and its MUSIC after the view's CALL
# [0001] an EXIT; block 18's set before its LOAD FILES, which an EXIT then
# ends after its CALL), and the moves made: (name, keys). Block 14: a step
# east (to 12,17, whose event says the party is under water), that text's
# Return, then turns; block 18 (Hizden's house, at night: its hour 20): a
# corridor of doors and windows, turned each way (a step north meets its
# innkeeper).
AREAS = [
    dict(key="d", block=14, start=0x806F, at=lambda x, y, f: {0x8082: x, 0x8088: y,
                                                             0x808E: f // 2, 0x80BE: 0},
         moves=[("ahead", "\\^"), ("read", "\\r"), ("left", "\\<"), ("left-2", "\\<"),
                ("left-3", "\\<"), ("right", "\\>")],
         # Squares whose views draw slot 2's wall set (types 11-15: set 11,
         # which 8a8b:0346 draws from its copy, DS:cd98).
         visits=[("glass", 2, 30, 0), ("doors", 1, 28, 0)]),
    dict(key="h", block=18, start=0x8065, at=lambda x, y, f: {0x34: x, 0x35: y, 0x11: f // 2},
         moves=[("left", "\\<"), ("left-2", "\\<"), ("left-3", "\\<")]),
]


def eclplay(work, name, args, block):
    """eclplay's shots (--shots) of a run, as RGBScreens, and its output."""
    shots = work / f"port-{name}"
    if shots.exists():
        shutil.rmtree(shots)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--shots", str(shots),
                             *map(str, args), str(dqk_lib.ASSETS),
                             *([str(block)] if block else [])],
                            capture_output=True, text=True, cwd=ROOT, timeout=120)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    return [cokbox.RGBScreen.load(p) for p in
            sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))], result


def port_place(work, name, area, x, y, facing):
    """eclplay's screen in AREA's block at X, Y facing FACING (0-7)."""
    bmp = work / f"port-{name}.bmp"
    sets = {**area["at"](x, y, facing), 0x22: 1, 0x16: 1}
    args = ["--start", f"{area['start']:x}"]
    for k, v in sets.items():
        args += ["--set", f"{k:x}={v:x}"]
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--screen", str(bmp), *args,
                             str(dqk_lib.ASSETS), str(area["block"])],
                            capture_output=True, text=True, cwd=ROOT, timeout=120)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    return cokbox.RGBScreen.load(bmp) if bmp.exists() else None


def begin(box, out, verdict):
    """From the title's menu: the camp menu, TDQK's game A, the block
    variable set to 1, Begin Adventuring, the copy protection answered:
    the jumper's screen. Returns (the variables' address, the jumper's
    screen, the --set arguments of the saved game's lamps)."""
    box.keys("plta")
    box.settled(tolerance=64)
    variables = box.far(box.ds * 16 + DS_VARS)
    box.write(variables + 0x16, b"\x01")
    # The lamps under the view (variables 0x1fa-0x1fc), the saved game's.
    lamps = [a for i, b in enumerate(box.read(variables + 0x1fa, 3))
             for a in ("--set", f"{0x1fa + i:x}={b:x}")]
    box.keys("b")
    box.settled(tolerance=64)
    entry, word, answer = dqk_lib.protection(box)
    box.keys(answer + "\\r")
    return variables, box.settled(tolerance=64), lamps


def square_byte(geo, x, y):
    """GEO.GLB record GEO's square byte at X, Y (its third plane)."""
    record = hlib.load(dqk_lib.ASSETS / "DISK3" / "GEO.GLB").record(geo)
    height, width = record[0], record[1]
    return record[8 + 2 * width * height + y * width + x]


def loop(box, work, out, verdict, memory, area, prefix):
    """The adventure loop's places in AREA: its first, then each move's,
    then each square visited: the party put there (DS:c9f0-c9f2, and the
    square's byte, c9f4) facing right of the facing wanted, then turned
    left."""
    visits = [(name, ("visit", x, y, f)) for name, x, y, f in area.get("visits", ())]
    for name, keys in [("loop", None)] + area["moves"] + visits:
        if isinstance(keys, tuple):
            _, x, y, f = keys
            box.write(box.ds * 16 + DS_SQUARE, bytes([x, y, (f + 2) % 8]))
            box.write(box.ds * 16 + DS_SQUARE + 4, bytes([square_byte(area["block"], x, y)]))
            keys = "\\<"
        if keys is not None:
            box.keys(keys)
        name = f"{prefix}-{name}"
        shot = box.settled(tolerance=64)
        shot.save_png(out / f"{name}.png")
        x, y, facing = box.ds_read(DS_SQUARE, 3)
        memory[name] = dict(x=x, y=y, facing=facing)
        mine = port_place(work, name, area, x, y, facing)
        verdict.screen(f"block {area['block']}'s view at {x},{y} facing {facing} ({name})",
                       region(shot, VIEW), region(mine, VIEW) if mine else None, work=work)


def region(screen, box):
    x, y, w, h = box[:4]
    return cokbox.RGBScreen(w, h, screen.region(x, y, w, h))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_views")
    out = work / "original"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    verdict = checklib.Verdict()
    memory = {}

    title, _ = eclplay(work, "title", ["--start-menu", "--keys", "\\k " * 8 + "Q"], None)

    with cokbox.Box(workdir=work / "box", game="dark-queen", verbose=opts.verbose,
                    timeout=600, turbo=False) as box:
        # The title: each scene once the original waits in it (its loop's
        # scene number, dqk_lib.title_scene: drawn and shown), then Space.
        for n in range(1, 9):
            for _ in range(1200):
                if dqk_lib.title_scene(box) == n:
                    break
                box.settle(0.05)
            else:
                raise cokbox.BoxError(f"the title's scene {n} never waited for a key")
            box.settle(0.3)
            shot = box.screenshot()
            shot.save_png(out / f"title-{n}.png")
            verdict.screen(f"title scene {n}", shot, title[n - 1] if len(title) >= n else None,
                           masks=CYCLES.get(n, ()), work=work)
            # Space as it waits (Box.keys would wait for a still screen,
            # which the scenes' cycles and sparks may not give before the
            # wait runs out, 6523:000c's 7.9 s).
            box.push_key(cokbox.key_word(" "))
        menu = box.settled(tolerance=64)
        menu.save_png(out / "menu.png")
        verdict.screen("the title's menu", menu, title[8] if len(title) > 8 else None,
                       masks=[POINTER], work=work)
        variables, shot, lamps = begin(box, out, verdict)
        shot.save_png(out / "jumper.png")
        # Block 1 to its menu, then D: block 14 to its text.
        jumper, _ = eclplay(work, "jumper", ["--set", "22=1", *lamps, "--keys", "D\\r"], 1)
        memory["jumper"] = list(box.ds_read(DS_SQUARE, 3))
        verdict.value("the jumper's square and facing (DS:c9f0)", memory["jumper"], [3, 3, 0])
        verdict.screen("block 1's text, menu and view", shot, jumper[0] if jumper else None,
                       masks=[POINTER, PARTY], work=work)
        box.keys(AREAS[0]["key"])
        shot = box.settled(tolerance=64)
        shot.save_png(out / "arrival.png")
        memory["arrival"] = list(box.ds_read(DS_SQUARE, 3))
        verdict.value("block 14's square and facing", memory["arrival"], [11, 17, 2])
        verdict.screen("block 14's text and view", shot, jumper[1] if len(jumper) > 1 else None,
                       masks=[POINTER, PARTY], work=work)
        box.keys("\\r")
        loop(box, work, out, verdict, memory, AREAS[0], "darg")
    # Hizden's: the title passed by Escape (6523:0345: to the menu).
    with cokbox.Box(workdir=work / "box", game="dark-queen", verbose=opts.verbose,
                    timeout=600) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        dqk_lib.escape_title(box)
        begin(box, out, verdict)
        box.keys(AREAS[1]["key"])
        loop(box, work, out, verdict, memory, AREAS[1], "hizd")
    (out / "memory.json").write_text(json.dumps(memory, indent=1) + "\n")
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

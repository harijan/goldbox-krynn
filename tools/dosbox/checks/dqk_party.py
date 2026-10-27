#!/usr/bin/env python3
"""The Dark Queen of Krynn's saved game and its party in the original
against eclplay --game dark-queen --start-menu (DARK-QUEEN.md's step 2:
dqk-saves; docs/checks.md, "Checks against the original": dqk_party),
compared in RGB with the game's own mouse cursor (the port's with
--mouse): Load's TDQK and saved game A (604b:2e87, 28f8: SAVE\\SAVGAMA.QSV)
and the camp with its party (7368:046f, the list 5e22:023b), Down to the
second member, View's sheet (86b8:0000) and its menu, Escape to the camp;
Begin Adventuring past the copy protection (answered in the box; the port
logs and passes it over) to block 4's first text with the party's list and
the status (5e22:03f8); Palanthas, the palace and its leaving to Caergoth's
3D view and the adventure's commands (6fea:0365); steps east and a turn
(6fea:000a, 0753, the clock 1396:044d); then the way to Caergoth's
ruins, the widow, the shadows, and the step after which dragons attack
(SETUP MONSTERS, 6c70:2db1: the first fight, where the port stops).

Compared: the whole screen but the sheet's readied weapon and armour
(rows 21-22: 177e:03fc, items' names not ported), and the mouse's cursor
in the adventure's commands (1bdb:1aef's areas give it other shapes: not
ported); the party's square and facing (DS:c9f0-c9f2) after each step
against the port's log; the variables 0x0001-0x0400 once loaded, and the
clock, the last square and the edge at the widow (eclplay --vars); and the
original's ECL past the dragons' SETUP MONSTERS where the port stops. About a
minute.

    python3 tools/dosbox/checks/dqk_party.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ("--game", "dark-queen")

ITEMS = (8, 168, 120, 16, "the sheet's readied weapon and armour (177e:03fc: not ported)")
CURSOR = (0, 0, 24, 24, "the mouse's cursor over the commands' view areas (1bdb:1aef)")
DS_SQUARE = 0xC9F0
CAERGOTH = 20
DS_VARS = 0xB20F
# Block 4's dragons: their SETUP MONSTERS and COMBAT (ecldump).
SETUP, COMBAT = 0x8B5B, 0x8C1F

def port_vars(work, name, keys):
    """The port's variables 0x0001-0x0400 (eclplay --vars) once KEYS have
    run out."""
    path = work / f"port-{name}.vars"
    subprocess.run([str(cokbox.ECLPLAY), *GAME, "--start-menu", "--vars", str(path), "--keys",
                    keys, str(dqk_lib.ASSETS)], capture_output=True, text=True, cwd=ROOT,
                   timeout=300)
    return path.read_bytes() if path.exists() else b""


def differing(original, port, addresses):
    """The variables (address: original, port) that differ."""
    return {f"{a:#x}": (original[a - 1], port[a - 1] if len(port) >= a else None)
            for a in addresses if len(port) < a or original[a - 1] != port[a - 1]}


def port_run(work, keys):
    """eclplay's shots of the start menu with KEYS, and its log."""
    shots = work / "port"
    if shots.exists():
        shutil.rmtree(shots)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--mouse", "--shots", str(shots),
                             "--start-menu", "--keys", keys, str(dqk_lib.ASSETS)],
                            capture_output=True, text=True, cwd=ROOT, timeout=300)
    (work / "port.txt").write_text(result.stdout + result.stderr)
    return ([cokbox.RGBScreen.load(p) for p in
             sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))],
            result.stdout + result.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_party")
    out = work / "original"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    verdict = checklib.Verdict()

    # The port: the title passed by Escape, main's menu (shot 1), the
    # camp (2), "load from where?" (3), "load which game:" (4), the camp
    # with its party (5), after Down (6), the sheet (7), the camp (8), the
    # first text (9), its next two, the city's menu, the palace's five
    # texts, the city's menu again, Leave's text and Caergoth (CAERGOTH),
    # then a shot after each move, the widow's menu after the last; the
    # last before the stop the step before the dragons.
    route = ("\\^\\^\\^\\^\\^\\^\\^\\^\\^\\^\\>\\^\\^\\^\\>\\^\\^\\<\\^\\^\\^\\>\\^\\^L\\r\\v"
             "\\^\\r\\^\\^\\^")
    keys = ("\\k\\ePLTA\\vV\\eB\\r\\r\\rP\\r\\r\\r\\r\\rL\\r" + route)
    shots, log = port_run(work, keys)
    loaded_vars = port_vars(work, "loaded", "\\k\\ePLTA")
    moves = ["\\^"] * 10 + ["\\>", "\\^", "\\^", "\\^", "\\>", "\\^", "\\^", "\\<",
                            "\\^", "\\^", "\\^", "\\>", "\\^", "\\^"]
    widow_vars = port_vars(work, "widow", "\\k\\ePLTA\\vV\\eB\\r\\r\\rP\\r\\r\\r\\r\\rL\\r"
                           + "".join(moves))

    with cokbox.Box(workdir=work / "box", game="dark-queen", verbose=opts.verbose,
                    timeout=900) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        box.keys("\\e")
        box.settled(tolerance=64)
        freeze = (box.ds - 0x422E + 0x261B) * 16 + 0x05BE  # dqk_menus' freeze_cycles
        if box.read(freeze, 1) != b"\x55":
            raise cokbox.BoxError("261b:05be is not where it was expected")
        box.write(freeze, b"\xcb")
        steps = [("loaded", "plta", [], 5), ("down", "\\v", [], 6),
                 ("view", "v", [ITEMS], 7), ("view's escape", "\\e", [], 8)]
        for what, typed, masks, i in steps:
            box.keys(typed)
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"{checklib.slug(what)}.png")
            verdict.screen(what, shot, shots[i] if len(shots) > i else None, masks=masks,
                           work=work)
            if what == "loaded":
                variables = box.far(box.ds * 16 + DS_VARS)
                original = box.read(variables + 1, 0x400)
                verdict.value("the variables 0x0001-0x0400 once loaded (far DS:b20f)",
                              differing(original, loaded_vars, range(1, 0x401)), {})
        box.keys("b")
        box.settled(tolerance=64)
        _, _, answer = dqk_lib.protection(box)
        box.keys(answer + "\\r")
        shot = box.settled(tolerance=64)
        shot.save_png(out / "first-text.png")
        verdict.screen("block 4's first text with the party", shot,
                       shots[9] if len(shots) > 9 else None, work=work)
        box.keys("\\r\\r\\rp\\r\\r\\r\\r\\rl\\r")
        shot = box.settled(tolerance=64)
        shot.save_png(out / "caergoth.png")
        verdict.screen("Caergoth's view and the commands", shot,
                       shots[CAERGOTH] if len(shots) > CAERGOTH else None, masks=[CURSOR],
                       work=work)
        # The route's moves, a shot each after the first ten steps east.
        port_moves = re.findall(r"^(?:step|turn): (-?\d+),(-?\d+) facing (\d)$", log, re.M)
        squares = []
        for n, key in enumerate(moves):
            box.keys(key)
            box.settled(tolerance=64)
            x, y, facing = box.ds_read(DS_SQUARE, 3)
            squares.append((x, y, facing))
        port_squares = [(int(x), int(y), int(f)) for x, y, f in port_moves[:len(moves)]]
        verdict.value("the party's square and facing after each move (DS:c9f0-c9f2)",
                      squares, port_squares)
        variables = box.far(box.ds * 16 + DS_VARS)
        original = box.read(variables + 1, 0x400)
        verdict.value("the clock (variables 5-11), the last square (0x17, 0x18) and the edge "
                      "(0x21) at the widow", differing(original, widow_vars,
                                                       [*range(5, 12), 0x17, 0x18, 0x21]), {})
        shot = box.screenshot()
        shot.save_png(out / "widow.png")
        index = CAERGOTH + len(moves)
        verdict.screen("the widow's menu", shot, shots[index] if len(shots) > index else None,
                       masks=[CURSOR], work=work)
        for typed in ["l", "\\r", "\\v", "\\^", "\\r", "\\^", "\\^"]:
            box.keys(typed)
            box.settled(tolerance=64)
        shot = box.settled(tolerance=64)
        shot.save_png(out / "before-dragons.png")
        verdict.screen("the step before the dragons", shot, shots[-1] if shots else None,
                       masks=[CURSOR], work=work)
        box.keys("\\^")
        shot = box.settled(tolerance=64)
        shot.save_png(out / "dragons.png")
        pc = box.ds_word(dqk_lib.DS_PC)
        verdict.check(SETUP < pc <= COMBAT, "the original past the dragons' SETUP MONSTERS",
                      f"its ECL at {pc:04x}, between {SETUP:04x} and their COMBAT, {COMBAT:04x}")
    verdict.check("SETUP MONSTERS (dqk 6c70:2db1)" in log,
                  "the port stops at the dragons' SETUP MONSTERS, cited",
                  log.strip().splitlines()[-1] if log.strip() else "")
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

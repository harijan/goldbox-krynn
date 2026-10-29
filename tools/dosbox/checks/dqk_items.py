#!/usr/bin/env python3
"""The Dark Queen of Krynn's sheet menu in the original against eclplay
--game dark-queen --start-menu (DARK-QUEEN.md's step 2: dqk-create's items
part; docs/checks.md, "Checks against the original": dqk_items), compared
in RGB with the game's own mouse cursor (the port's with --mouse): SAVGAMA
loaded, View (86b8:0b0c: the sheet with its readied weapon and armour,
177e:03fc) and Items (86b8:0f41) of Roland of Dust: his four items
readied, Trade and Drop refused ("Must be unreadied"), "Can't halve
that", Join; the helm unreadied (Rdy), Drop asked ("Your helm +2 will be
gone forever", "Lose it?") and refused, Trade ("Trade with whom?",
86b8:3ae5: Down, Exit) and then given to Timasalin; the second member's
Items: his arrows (40) halved and joined again, his bow readied and
refused for hands ("Your hands are full!"), the arrows unreadied and the
bow readied. Then, from the camp, Save Current Game: the saved game the
original writes and the port's (the same bytes but the characters' far
pointers) are compared.

Compared: every screen of each step; the saved game. About two minutes.

    python3 tools/dosbox/checks/dqk_items.py [--workdir DIR] [--verbose]

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

# (name, key) one key a step; the port's shot after key n is shots[n + 2].
STEPS = [
    ("loaded", "p"), ("load", "l"), ("tdqk", "t"), ("game a", "a"),
    ("view", "v"), ("items", "i"),
    ("trade of a readied item", "t"), ("drop of a readied item", "d"),
    ("halve", "h"), ("join", "j"),
    ("down", "\\v"), ("unready the helm", "r"),
    ("drop the helm", "d"), ("keep it", "n"),
    ("trade the helm", "t"), ("the picker, down", "\\v"), ("exit", "e"),
    ("trade the helm again", "t"), ("down", "\\v"), ("select", "s"),
    ("exit items", "e"), ("exit view", "e"),
    ("the next member", "\\v"), ("view timasalin", "v"), ("items", "i"),
    ("down", "\\v"), ("ready the bow", "r"), ("up", "\\^"),
    ("halve the arrows", "h"), ("join them", "j"), ("unready the arrows", "r"),
    ("down", "\\v"), ("ready the bow", "r"),
    ("exit items", "e"), ("exit view", "e"),
    ("save", "s"), ("game a", "a"),
]


def port_run(work, keys, saves):
    """eclplay's shots of the start menu with KEYS (it saves to SAVES), and
    its log."""
    shots = work / "port"
    if shots.exists():
        shutil.rmtree(shots)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--mouse", "--shots", str(shots),
                             "--saves", str(saves), "--start-menu", "--keys", keys,
                             str(dqk_lib.ASSETS)], capture_output=True, text=True, cwd=ROOT,
                            timeout=300)
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
    work = checklib.workdir(opts.workdir, "dqk_items")
    out = work / "original"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    verdict = checklib.Verdict()
    saves = work / "saves"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(dqk_lib.ASSETS / "SAVE", saves)
    keys = "\\k\\e" + "".join(key for _, key in STEPS)
    shots, log = port_run(work, keys, saves)

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
        for n, (what, key) in enumerate(STEPS):
            box.keys(key)
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"{n:02d}-{checklib.slug(what)}.png")
            verdict.screen(what, shot, shots[n + 2] if len(shots) > n + 2 else None, work=work)
        box.settle(1.0)
        original = box.cdrive / "DQK" / "SAVE" / "SAVGAMA.QSV"
        written = original.read_bytes() if original.exists() else b""
    port = (saves / "SAVGAMA.QSV").read_bytes() if (saves / "SAVGAMA.QSV").exists() else b""
    diffs = saved_differences(written, port)
    verdict.value("the saved game written (SAVGAMA.QSV, but the characters' far pointers)",
                  (len(written), diffs[:20]), (len(port), []))
    return verdict.finish()


def saved_differences(a, b):
    """The offsets at which two saved games differ but in the characters'
    far pointers (a record's next, effects, items' (its low word counts
    them: the port writes it too), slots and combat record; each effect's
    next): the original's are its heap's addresses."""
    if len(a) != len(b) or len(a) < 0x40f:
        return ["size"]
    mask = set()
    at = 0x40f
    for _ in range(a[0x40d] | a[0x40e] << 8):
        rec = a[at:at + 0x19e]
        if len(rec) < 0x19e:
            return ["size"]
        for o, n in [(0, 4), (4, 4), (10, 2), (0xc, 0x34)]:
            mask.update(range(at + o, at + o + n))
        items = rec[8] | rec[9] << 8
        effects = rec[4:8] != b"\0\0\0\0" or b[at + 4] != 0
        at += 0x19e + items * 17
        while effects:
            mask.update(range(at + 5, at + 9))
            effects = a[at + 5:at + 9] != b"\0\0\0\0"
            at += 9
    return [hex(k) for k in range(len(a)) if a[k] != b[k] and k not in mask]


if __name__ == "__main__":
    sys.exit(checklib.run(main))

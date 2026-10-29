#!/usr/bin/env python3
"""The Dark Queen of Krynn's Train Character (dqk 6be8:0022) and Knight
new Order (7368:10c2) in the original against eclplay --game dark-queen
--start-menu (DARK-QUEEN.md's step 2: dqk-create; docs/checks.md,
"Checks against the original": dqk_train), compared in RGB with the game's
own mouse cursor (the port's with --mouse).

The shipped saved game A is played from a patched copy in both: variable
0x30 (the training hall's classes) 0x7f, and the experience of Roland of Dust
(a knight of the Crown, level 11) 1,200,001, of Roark 'Croaker' (a dwarf
fighter and thief) 1,000,001 and of Walker Keys (a kender cleric and
thief) 700,000: the file is the same, no memory is patched. The random
seed is set at the title in both (DS:ca0e; --seed), so that the hit points
rolled are the same. Played: Load, Knight new Order (Roland: "may become a
knight of the sword"), Change, again (the rose), Exit; Train for Roland
("will become: a level 12 Knight", Yes), for Roark (a level 10 Fighter: the
thief is at the dwarves' top level, and the experience held below the level
after's), for Walker (a level 13 Thief), and for Timasalin ("Not Enough
Experience"); then Save Current Game. Compared: every screen, and the
saved game written (SAVGAMA.QSV, but the characters' far pointers and the
effects' next ones, the heap's). In a second run with the hall's variable
0x40 (knights only): Roark's "we don't train that class here", and the
file unchanged but the variable. A third run patches Walker's cleric level
from 10 to 6 in the file (the same in both) so that a cleric, a caster
that needs no new-spell picker (849a:2080, a mage's or ranger's), trains.
About three minutes.

    python3 tools/dosbox/checks/dqk_train.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ("--game", "dark-queen")
SEED = 5
CURSOR = (0, 0, 24, 24, "the mouse's cursor over the camp's buttons")
DS_SEED = 0xCA0E
HEADER = 0x40D


def characters(data):
    """(offset, size) of each character of a .QSV (604b:28f8): the record,
    its items of 17 bytes (their count the low word at +8) and its effects
    of 9 (a chain: each one's far pointer, +5, not nil, another follows)."""
    count = data[HEADER] | data[HEADER + 1] << 8
    at, out = 0x40F, []
    for _ in range(count):
        size = 0x19E + 17 * struct.unpack_from("<H", data, at + 8)[0]
        if data[at + 4:at + 8] != b"\0" * 4:
            e = at + size
            while True:
                size += 9
                if data[e + 5:e + 9] == b"\0" * 4:
                    break
                e += 9
        out.append((at, size))
        at += size
    return out


def patched(source, out, hall, experience, levels=None):
    """SAVGAMA.QSV with variable 0x30 = hall and each character's
    experience (by index) as given, and the levels (+0xa5 + class) of
    those in LEVELS ({index: {class: level}}); the other saved files beside
    it."""
    shutil.copytree(source, out, dirs_exist_ok=True)
    path = out / "SAVGAMA.QSV"
    data = bytearray(path.read_bytes())
    data[0x2F] = hall
    chars = characters(data)
    for index, value in experience.items():
        struct.pack_into("<I", data, chars[index][0] + 0x44, value)
    for index, classes in (levels or {}).items():
        for k, value in classes.items():
            data[chars[index][0] + 0xA5 + k] = value
    path.write_bytes(data)


def pointer_mask(data):
    """The offsets of a .QSV that are the heap's: each character's far
    pointers (next, effects, items but its count, the slots, combat) and
    each effect's next."""
    mask = set()
    for at, size in characters(data):
        for off, n in [(0, 4), (4, 4), (10, 2), (0x40, 4)] + [(0xC + 4 * i, 4) for i in range(13)]:
            mask.update(range(at + off, at + off + n))
        for e in range(at + 0x19E + 17 * struct.unpack_from("<H", data, at + 8)[0], at + size, 9):
            mask.update(range(e + 5, e + 9))
    return mask


def port_run(work, name, saves, keys, seed):
    shots = work / f"port-{name}"
    if shots.exists():
        shutil.rmtree(shots)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--mouse", "--seed", str(seed),
                             "--saves", str(saves), "--shots", str(shots), "--start-menu",
                             "--keys", keys, str(dqk_lib.ASSETS)],
                            capture_output=True, text=True, cwd=ROOT, timeout=300)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    return ([cokbox.RGBScreen.load(p) for p in
             sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))],
            result.stdout + result.stderr)


def freeze_cycles(box):
    at = (box.ds - 0x422E + 0x261B) * 16 + 0x05BE
    if box.read(at, 1) != b"\x55":
        raise cokbox.BoxError("261b:05be is not where it was expected")
    box.write(at, b"\xcb")


def play(work, name, source, hall, experience, steps, verdict, verbose, levels=None):
    """One run of STEPS (what, typed, message the port logs or None) in
    both; returns the two saved files."""
    saves = work / f"saves-{name}"
    patched(source, saves, hall, experience, levels)
    own = work / f"port-saves-{name}"
    shutil.copytree(saves, own, dirs_exist_ok=True)
    keys = "\\k\\e" + "".join("\\v" if t == "v" else "\\^" if t == "^" else t
                              for _, t, _ in steps)
    shots, log = port_run(work, name, own, keys, SEED)
    original = work / f"original-{name}"
    original.mkdir(exist_ok=True)
    with cokbox.Box(workdir=work / f"box-{name}", game="dark-queen", verbose=verbose,
                    saves=[saves], seed=SEED, timeout=900) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        box.keys("\\e")
        box.settled(tolerance=64)
        freeze = (box.ds - 0x422E + 0x261B) * 16 + 0x05BE
        if box.read(freeze, 1) != b"\x55":
            raise cokbox.BoxError("261b:05be is not where it was expected")
        box.write(freeze, b"\xcb")
        for i, (what, key, message) in enumerate(steps):
            box.keys("\\v" if key == "v" else "\\^" if key == "^" else key)
            shot = box.settled(tolerance=64)
            shot.save_png(original / f"{i:02d}-{checklib.slug(what)}.png")
            verdict.screen(what, shot, shots[i + 2] if len(shots) > i + 2 else None,
                           masks=[CURSOR], work=work)
            if message is not None:
                verdict.check(message in log, f"the port says {message!r}")
    return (work / f"box-{name}" / "c" / "DQK" / "SAVE" / "SAVGAMA.QSV", own / "SAVGAMA.QSV")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_train")
    verdict = checklib.Verdict()
    source = dqk_lib.ASSETS / "SAVE"
    steps = [
        ("the camp", "p", None), ("load from where", "l", None), ("which game", "t", None),
        ("the camp with the party", "a", None),
        ("Train Roland", "t", "a level 12 Knight"), ("Yes", "y", "Congratulations..."),
        ("Knight new Order", "k", "knight of the sword"), ("Change", "c", None),
        ("Knight new Order again", "k", "knight of the rose"), ("Exit", "e", None),
        ("down 1", "v", None), ("down 2", "v", None), ("down 3", "v", None), ("down 4", "v", None),
        ("Train Roark", "t", "a level 10 Fighter"), ("Yes for Roark", "y", None),
        ("down to Walker", "v", None),
        ("Train Walker", "t", "a level 13 Thief"), ("Yes for Walker", "y", None),
        ("up 1", "^", None), ("up 2", "^", None), ("up 3", "^", None), ("up 4", "^", None),
        ("Train Timasalin", "t", "Not Enough Experience"),
        ("Save Current Game", "s", None), ("save A", "a", None),
    ]
    written = play(work, "a", source, 0x7F, {0: 1200001, 4: 1000001, 5: 700000}, steps,
                   verdict, opts.verbose)
    original, port = (p.read_bytes() if p.exists() else b"" for p in written)
    mask = pointer_mask(port) if port else set()
    differing = [i for i in range(min(len(original), len(port)))
                 if i not in mask and original[i] != port[i]]
    verdict.check(len(original) == len(port) == 7965 and not differing,
                  "the saved game written (SAVGAMA.QSV)",
                  f"{len(original)} and {len(port)} bytes, differing at "
                  f"{[hex(i) for i in differing[:12]]}")
    steps2 = steps[:4] + [("down to Roark", "v", None)] * 4 + [
        ("Train Roark, the hall of knights", "t", "we don't train that class here")]
    play(work, "b", source, 0x40, {0: 1200001, 4: 1000001, 5: 700000}, steps2, verdict,
         opts.verbose)
    # A caster that trains without the new-spell picker (849a:2080, a mage's
    # or ranger's): Walker's cleric (a kender cleric and thief, the level 10
    # made 6 here by the file: the same in both) takes levels, its spells a day
    # and known recomputed (8bcc:01e7).
    steps3 = steps[:4] + [("down 1", "v", None)] * 5 + [
        ("Train Walker's cleric", "t", None), ("Yes for Walker's cleric", "y", None),
        ("Save Current Game", "s", None), ("save A", "a", None)]
    written = play(work, "c", source, 0x7F, {5: 700000}, steps3, verdict, opts.verbose,
                   levels={5: {0: 6}})
    original, port = (p.read_bytes() if p.exists() else b"" for p in written)
    mask = pointer_mask(port) if port else set()
    differing = [i for i in range(min(len(original), len(port)))
                 if i not in mask and original[i] != port[i]]
    verdict.check(len(original) == len(port) and not differing,
                  "the saved game written after a cleric's training",
                  f"{len(original)} and {len(port)} bytes, differing at "
                  f"{[hex(i) for i in differing[:12]]}")
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

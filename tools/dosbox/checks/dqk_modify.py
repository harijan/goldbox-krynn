#!/usr/bin/env python3
"""The Dark Queen of Krynn's Modify Character in the original against
eclplay --game dark-queen --start-menu (DARK-QUEEN.md's step 2: dqk-create;
docs/checks.md, "Checks against the original": dqk_modify), compared in RGB
with the game's own mouse cursor (the port's with --mouse): the camp with
saved game A loaded (Load, Tdqk, A), Modify (M: 6a87:1293) on its first
member, and a key at a time its Add (Right), Sub (Left), Next (Down) and
Previous (Up) over the scores, the hit points and the name (edited in place:
a letter typed, Backspace, Left, Delete, Return), Exit (6a87:01e5); then
Remove Character (Y at "Update ...?") writes the member's .QCH, whose bytes
are compared but for the original's far pointers. Further runs: the scores
at the limits of their race and class (a saved game A of the check's own, its
first member's scores changed: the same file in both), a name taken by
another of the party and typed past its 15 characters, a character of an
experience that creation does not give ("can't be modified."), a click on a
score's row (the box's mouse, DOSBox-X's sdl.mouse_emulation=always), and
Keep (6a87:025e: the knight's, a cleric, fighter and mage's and a cleric and
ranger's, each Removed to a .QCH compared).

Compared: the whole screen, the mouse's cursor, and the
name's cursor (a block the original toggles by the timer's ticks, 6a87:0d30)
while the name is edited. About two minutes.

    python3 tools/dosbox/checks/dqk_modify.py [--workdir DIR] [--verbose]

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

CURSOR = (0, 0, 24, 24, "the mouse's cursor over the sheet's name")


BASE = 0x40f  # the first member's record in SAVGAMA.QSV


def record_at(data, member):
    """Where the member's record starts in a saved game: each is its 0x19e
    bytes, 17 an item, 9 an effect while the next pointer is not nil."""
    at = BASE
    for _ in range(member):
        items = data[at + 8] | data[at + 9] << 8
        more = any(data[at + 4:at + 8])
        at += 0x19e + 17 * items
        while more:
            more = any(data[at + 5:at + 9])
            at += 9
    return at


def patched(changes, member=0):
    """SAVGAMA.QSV with a member's bytes changed (offset: byte)."""
    data = bytearray((dqk_lib.ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
    at = record_at(data, member)
    for offset, value in changes.items():
        data[at + offset] = value
    return bytes(data)


def scores(values, base=None):
    """The record's scores (+0x79 + 2k) and base scores (+0x78 + 2k)."""
    out = {}
    for k, v in enumerate(values):
        out[0x79 + 2 * k] = v
        out[0x78 + 2 * k] = v if base is None else base[k]
    return out


# Each: (name, the saved game's changes or None, steps, what it writes).
# A step is (what, keys[, extra masks]); "PLTA" loads the party.
ABILITIES = [
    ("Modify", "m"), ("Add strength", "\\>"), ("Sub strength", "\\<"),
    ("Sub strength again", "\\<"), ("Next: intelligence", "\\v"),
    ("Add intelligence", "\\>"), ("Next by n: wisdom", "n"), ("Sub wisdom", "\\<"),
    ("Next by End: dexterity", "\\#4f"), ("Add dexterity", "\\>"),
    ("Next: constitution", "n"), ("Add constitution", "\\>"), ("Sub constitution", "\\<"),
    ("Sub constitution again", "\\<"), ("Next: charisma", "n"), ("Add charisma", "\\>"),
    ("Next: name", "n"), ("Name: a letter", "x"), ("Name: Left", "\\<"),
    ("Name: Backspace", "\\b"), ("Name: Delete", "\\#53"), ("Name: Right", "\\>"),
    ("Name: Return, hit points", "\\r"), ("Add hit points", "\\>"),
    ("Sub hit points", "\\<"), ("Previous: charisma", "p"),
    ("Previous by Up: constitution", "\\^"), ("Exit", "e"),
]
REMOVE = [("Remove: update?", "r"), ("Remove: yes", "y")]

# Roland at the limits: strength 18/99 (the human's most, 100), intelligence
# 18 (most), wisdom 10 (the knight's least), dexterity 8 (its least),
# constitution 10 (its least), charisma 3 (the human's least).
LIMITS = [
    ("Modify", "m"), ("Add strength to 18/100", "\\>"), ("Add strength at its most", "\\>"),
    ("Sub strength", "\\<"), ("Next: intelligence", "n"), ("Add intelligence at its most", "\\>"),
    ("Next: wisdom", "n"), ("Sub wisdom at the class's least", "\\<"), ("Next: dexterity", "n"),
    ("Sub dexterity at the class's least", "\\<"), ("Next: constitution", "n"),
    ("Sub constitution at the class's least", "\\<"), ("Add constitution", "\\>"),
    ("Next: charisma", "n"), ("Sub charisma at the race's least", "\\<"),
    ("Add charisma", "\\>"), ("Previous: constitution", "p"), ("Previous: dexterity", "p"),
    ("Up to the hit points", "\\^\\^\\^\\^"),
    ("Add hit points", "\\>"), ("Add hit points at their most", "\\>"),
    ("Sub hit points", "\\<"), ("Sub hit points again", "\\<"), ("Exit", "e"),
]

# A name another member has, typed over the whole of Roland's.
NAMES = [("Modify", "m"), ("Previous: hit points", "p"), ("Next: name", "p")] + \
    [(f"Name: Delete {k}", "\\#53") for k in range(14)] + \
    [(f"Name: {c}", c) for c in "TIMASALIN"] + \
    [("Name: Return, a name taken", "\\r"), ("Name: Backspace", "\\b"),
     ("Name: Backspace again", "\\b")] + \
    [(f"Name: {c} again", c) for c in "XYZABCDEFGHIJKLM"] + \
    [("Name: Home to charisma", "\\#47")]


# A mountain dwarf paladin (race 3, class 3: its charisma held at 17) with
# 17 charisma, three first level cleric spells a day (wisdom's Add and Sub set
# one) and 10 hit points (constitution's Add raises them to the levels' least).
DWARF = [
    ("Modify", "m"), ("Next: intelligence", "n"), ("Next: wisdom", "n"),
    ("Add wisdom", "\\>"), ("Next: dexterity", "n"), ("Next: constitution", "n"),
    ("Add constitution", "\\>"), ("Next: charisma", "n"), ("Sub charisma", "\\<"),
    ("Sub charisma again", "\\<"), ("Sub charisma once more", "\\<"),
    ("Add charisma", "\\>"), ("Add charisma again", "\\>"), ("Exit", "e"),
]
# Cinder, a female Qualinesti (strength 16 at most, a male's 18): at 15.
FEMALE = [("Down", "\\v"), ("Down again", "\\v"), ("Modify", "m"), ("Add strength", "\\>"),
          ("Add strength at her most", "\\>"), ("Add strength again", "\\>"), ("Exit", "e")]
# A name's cursor wrapping at either end.
WRAP = [("Modify", "m"), ("Previous: hit points", "p"), ("Next: name", "p"),
        ("Name: Left", "\\<"), ("Name: z", "z"), ("Name: Right", "\\>"), ("Name: y", "y"),
        ("Name: Right", "\\>"), ("Name: Return", "\\r"), ("Exit", "e")]
# The third and fourth members (333,333 and 500,000 experience) modified too.
MEMBERS = [("Down", "\\v"), ("Modify", "m"), ("Add strength", "\\>"), ("Exit", "e"),
           ("Down", "\\v"), ("Down again", "\\v"), ("Modify", "m"),
           ("Add strength", "\\>"), ("Exit", "e")]
# A cursed readied item stays readied.
CURSED = [("Modify", "m"), ("Add strength", "\\>"), ("Exit", "e")]


def mouse(x, y):
    return f"\\m{x},{y},1."


# The camp's second member, then its own Modify of a character past the
# three experiences.
SCENARIOS = [
    ("abilities", None, ABILITIES + REMOVE, "ROLANDOF"),
    ("limits", patched({**scores([18, 18, 10, 8, 10, 3]), 0x84: 99, 0x85: 99, 0x89: 134,
                        0x198: 134}), LIMITS + REMOVE, "ROLANDOF"),
    ("names", None, NAMES + [("Exit", "e")], None),
    ("unmodifiable", patched({0x44: 1, 0x45: 0, 0x46: 0, 0x47: 0}),
     [("Modify", "m"), ("Remove: update?", "r"), ("Remove: yes", "y")], "ROLANDOF"),
    ("dwarf", patched({0x58: 3, 0x5a: 3, 0x83: 17, 0x82: 17, 0x167: 3, 0x89: 10, 0x198: 10}),
     DWARF + REMOVE, "ROLANDOF"),
    ("female", patched({0x79: 15, 0x78: 15}, member=2), FEMALE + REMOVE, "'CINDER'"),
    ("wrap", None, WRAP, None),
    ("members", None, MEMBERS, None),
    ("cursed", patched({0x19e + 0x0c: 1}), CURSED + REMOVE, "ROLANDOF"),
    ("mouse", None, [("Modify", "m"), ("Click the dexterity's row", mouse(40, 100)),
                     ("Add dexterity", "\\>"), ("Click the hit points", mouse(200, 27)),
                     ("Sub hit points", "\\<"), ("Click the strength's row", mouse(20, 76)),
                     ("Add strength", "\\>"), ("Exit", "e")], None),
    ("Keep", None, [("Modify", "m"), ("Add strength", "\\>"), ("Keep", "k")] + REMOVE,
     "ROLANDOF"),
    # A cleric, fighter and mage's, and a cleric and ranger's spells a day and
    # known, the hit points' base (8bcc:01e7, 6a87:1599, 66e5:30d3).
    ("Keep mage", None, [("Down", "\\v"), ("Modify", "m"), ("Next: intelligence", "n"),
                         ("Add intelligence", "\\>"), ("Keep", "k")] + REMOVE, "TIMASALI"),
    ("Keep ranger", None, [("Down", "\\v"), ("Down again", "\\v"), ("Down once more", "\\v"),
                           ("Modify", "m"), ("Next: intelligence", "n"), ("Next: wisdom", "n"),
                           ("Sub wisdom", "\\<"), ("Keep", "k")] + REMOVE, "BALTHIRN"),
]


def port_run(work, name, keys, qsv, saves):
    shots = work / name
    if shots.exists():
        shutil.rmtree(shots)
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(dqk_lib.ASSETS / "SAVE", saves)
    if qsv is not None:
        (saves / "SAVGAMA.QSV").write_bytes(qsv)
    args = [str(cokbox.ECLPLAY), *GAME, "--mouse", "--shots", str(shots), "--start-menu",
            "--saves", str(saves), "--keys", "\\k\\e" + keys, str(dqk_lib.ASSETS)]
    result = subprocess.run(args, capture_output=True, text=True, cwd=ROOT, timeout=300)
    (work / f"{name}.txt").write_text(result.stdout + result.stderr)
    return ([cokbox.RGBScreen.load(p) for p in
             sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))],
            result.stdout + result.stderr)


def file_diff(original, port):
    """The bytes of two .QCH files that differ but for the original's far
    pointers (the record's: +0..+0x43 but the items' count; each effect's
    next, 5-8)."""
    if len(original) != len(port):
        return f"sizes {len(original)} and {len(port)}"
    effects_at = len(original)
    if original[4:8] != bytes(4):
        count = (len(original) - 0x19e - 17 * (original[8] | original[9] << 8)) // 9
        effects_at = len(original) - 9 * count
    out = []
    for k, (a, b) in enumerate(zip(original, port)):
        pointer = (k < 0x44 and not 8 <= k < 10) or (k >= effects_at and (k - effects_at) % 9 >= 5)
        if a != b and not pointer:
            out.append(f"+{k:#x}: {a:#04x} {b:#04x}")
    return ", ".join(out[:12]) or None


def name_cursor(n):
    """The name's cursor at its nth character: a block the original toggles
    by the clock (6a87:0d30)."""
    return (8 + 8 * n, 8, 8, 8, "the name's cursor, a block the original toggles by the clock")


def edited(text, cursor, typed):
    """The name and the cursor after a key at the name (6a87:0d30)."""
    if len(typed) == 1 and 0x20 <= ord(typed) <= 0x7a:
        if len(text) < 15:
            text = text[:cursor] + typed + text[cursor:]
            cursor = min(cursor + 1, 15)
    elif typed == "\\b":
        if cursor > 0:
            cursor -= 1
            text = text[:cursor] + text[cursor + 1:]
    elif typed == "\\#53":
        text = text[:cursor] + text[cursor + 1:]
    elif typed == "\\<":
        cursor = len(text) if cursor == 0 else cursor - 1
    elif typed == "\\>":
        cursor = cursor + 1 if len(text) > cursor else 0
    return text, cursor


def click(box, x, y):
    """A click of the left button at pixel x, y of the game's screen (the
    window shows it twice the size), taken once the game waits."""
    box.wait_input()
    box._xdo("mousemove", "--window", box.window, 2 * x, 2 * y)
    box.settle(0.3)
    box._xdo("click", "1")
    box.settle(0.5)


def run_scenario(opts, work, verdict, name, qsv, steps, written):
    out = work / f"original-{name}"
    out.mkdir(exist_ok=True)
    masks = [CURSOR]
    steps = [("loaded", "PLTA")] + steps
    keys = "".join(k for _, k in steps)
    shots, log = port_run(work, name, keys, qsv, work / f"port-saves-{name}")
    saves = []
    if qsv is not None:
        folder = work / f"saves-{name}"
        folder.mkdir(exist_ok=True)
        (folder / "SAVGAMA.QSV").write_bytes(qsv)
        saves = [folder]
    cursor, text, pointer = 0, "ROLAND OF DUST", None
    conf = {"sdl.mouse_emulation": "always"} if name == "mouse" else {}
    with cokbox.Box(workdir=work / f"box-{name}", game="dark-queen", verbose=opts.verbose,
                    saves=saves, extra_conf=conf, timeout=900) as box:
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
        consumed = 1  # the title's Escape
        for what, typed in steps:
            if typed.startswith("\\m"):
                pointer = tuple(map(int, typed[2:-1].split(",")[:2]))
                click(box, *pointer)
            else:
                box.keys(typed)
            n = 4 if typed == "PLTA" else 1
            if typed.startswith("\\^\\^"):
                n = typed.count("\\^")
            consumed += n
            if "taken" in what:
                box.settle(2.5)  # the message on the bar, for the speed's time
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"{checklib.slug(what)}.png")
            if what.startswith("Remove"):
                continue
            more = []
            if what == "Next: name":
                more = [name_cursor(cursor)]
            elif what.startswith("Name: "):
                text, cursor = edited(text, cursor, typed)
                more = [name_cursor(cursor)]
            if pointer is not None:
                more.append((pointer[0] - 16, pointer[1] - 16, 32, 32,
                             "the pointer's cursor, DOSBox-X's mouse a pixel off"))
            verdict.screen(f"{name}: {what}", shot, shots[consumed] if len(shots) > consumed
                           else None, masks=masks + more, work=work)
        saved = box.game / "SAVE" / f"{written}.QCH" if written else None
        original = saved.read_bytes() if saved is not None and saved.exists() else b""
    if written:
        port = work / f"port-saves-{name}" / f"{written}.QCH"
        port_bytes = port.read_bytes() if port.exists() else b""
        verdict.check(original != b"" and port_bytes != b"",
                      f"{name}: both wrote the .QCH {written}")
        if original and port_bytes:
            diff = file_diff(original, port_bytes)
            verdict.check(diff is None, f"{name}: the written .QCH's bytes", diff or "")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--only", help="one scenario by name")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_modify")
    verdict = checklib.Verdict()
    for name, qsv, steps, written in SCENARIOS:
        if opts.only in (None, name):
            run_scenario(opts, work, verdict, name, qsv, steps, written)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

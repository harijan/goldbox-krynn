#!/usr/bin/env python3
"""Death Knights of Krynn's Create New Character (49cb:05c7), its icon
editor (49cb:3cac) and Modify Character (49cb:25d4) in the original against
eclplay --game death-knights --start-menu (docs/start-menu.md, "Death Knights'
creation"). Each case creates a character from the title with the same seed
(Box seed, eclplay --seed), the same keys typed to both: the lists, the
sheet at each roll and reroll, the name, the icon editor's menus and edits,
the questions; the original's screen at each wait against the port's at
the key read the same keys leave it in, whole screens. The roster files it
is saved to (NAME.PC, .WIZ, .JNK, .EFF) are compared byte for byte, the far
pointers masked (masked). Cases with a modify step then add it
to the party (Add, Death), Modify it (scores, the name, the hit points,
Keep or Exit) and Remove it to the roster, whose files are compared again.

Seeded; in run_all.py's Death Knights set (make dkk-checks).
About three minutes (turbo).

    python3 tools/dosbox/checks/dkk_create.py [--workdir DIR] [--case NAME ...]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_lib as K  # noqa: E402

cokbox = checklib.cokbox

# The title's two keys, Play; the start menu's first row, Create.
TITLE_STEPS = ["x", "x"]
PLAY_CREATE = r"\r\r"

# Pick Race's rows: 0 Silvanesti Elf, 1 Qualinesti Elf, 2 Half-elf, 3
# Mountain Dwarf, 4 Hill Dwarf, 5 Kender, 6 Human; Pick Class's rows the
# race's (DS:7423). Each case: (name, seed, race, gender, class row, deity
# row or None, alignment row, rerolls, icon keys, modify keys or None,
# what).
DOWN = r"\v"
# Menus by index flush the keyboard once they have their key (6a69:0f1b):
# one key a step there, as the original would lose a key typed ahead.
# The icon editor: Parts, Head, Next twice, Keep, Exit; Size, the other
# size, Keep; 1st-color, Body, Next, Keep, Exit; 2nd-color, Weapon, Prev,
# Exit (put back), Exit; Exit: is this icon ok?
ICON_EDITS = [
    ("parts", "P"), ("head", "H"), ("next", "N"), ("next-2", "N"), ("kept", "K"), ("parts-back", "E"),
    ("size", "S"), ("resized", r"\r"), ("size-kept", "K"),
    ("first", "1"), ("body", "B"), ("recoloured", "N"), ("body-kept", "K"), ("first-back", "E"),
    ("second", "2"), ("weapon", "W"), ("prev", "P"), ("put-back", "E"), ("second-back", "E"),
    ("ok", "E"),
]
CASES = [
    ("CLERIC", 3, 6, 0, 0, 0, 0, 1, ICON_EDITS, "modify",
     "a human cleric of Paladine: its spells, kit 4, Modify"),
    ("PALADIN", 5, 3, 1, 3, None, 0, 0, [("ok", "E")], "paladin",
     "a mountain dwarf paladin: effect 0x2d, charisma held at 17"),
    ("TRIPLE", 7, 1, 0, 11, None, 0, 1, [("ok", "E")], None,
     "a Qualinesti fighter/mage/thief, lawful neutral: 70001, the Red Robes' spells, kit 2"),
    ("WHITE", 11, 0, 1, 2, None, 0, 0, [("ok", "E")], None,
     "a Silvanesti mage of the White Robes: its spells by order and level, kit 5"),
    ("KENDER", 13, 5, 0, 6, 4, 2, 0, [("ok", "E")], None,
     "a kender cleric/thief of Sirrion: the thief's kit with the cleric's first item"),
    ("KNIGHT", 17, 6, 0, 5, None, 0, 0, [("ok", "E")], None,
     "a human knight: kit 1"),
    ("RANGER", 19, 2, 1, 7, 1, 1, 0, [("ok", "E")], None,
     "a half-elf cleric/ranger of Majere: effect 0x69, kit 2"),
]
# Modify, after the character is added to the party: Add (the second row),
# Death, its row, Exit; Modify, Select (the start menu keeps the row picked
# last: Add's second, now Drop's, Modify the next); then each step; then
# Remove (three rows on), Select.
ADD = [("add-from", r"\v\r"), ("add-list", r"\r"), ("added", r"\r"), ("add-done", r"\e")]
MODIFY = {
    # Strength up twice, down once; Down to intelligence, Subtract; Down
    # to the name (field 6 is past wisdom ... charisma), its keys; the hit
    # points (Down), Add, Subtract; Keep.
    "modify": [
        ("modify", r"\v\r\r"), ("strength", "A"), ("strength-2", "A"), ("strength-less", "S"),
        ("intelligence", r"\v"), ("intelligence-less", "S"), ("wisdom", r"\v"),
        ("dexterity", r"\v"), ("constitution", r"\v"), ("charisma", r"\v"),
        ("charisma-more", "A"), ("name", r"\v"), ("typed", "ASKE"), ("name-back", r"\b\b"),
        ("hit-points", r"\v"), ("hit-points-more", "A"), ("hit-points-less", "S"),
        ("kept", "K"),
        # Removed: saved to the roster by its new name.
        ("remove", r"\v\v\v\r"), ("removed", r"\r"),
    ],
    "paladin": [
        ("modify", r"\v\r\r"), ("hit-points", r"\^"), ("name", r"\^"), ("charisma", r"\^"),
        ("charisma-less", "S"), ("charisma-more", "A"), ("charisma-more-2", "A"),
        ("exited", "E"),
        # Removed: overwrite PALADIN? Yes.
        ("remove", r"\v\v\v\r"), ("overwrite", r"\r"), ("removed", "Y"),
    ],
}


def create_steps(race, gender, cls, deity, align, rerolls, name, icon):
    steps = [("race", PLAY_CREATE), ("gender", DOWN * race + r"\r"),
             ("class", DOWN * gender + r"\r")]
    if deity is not None:
        steps += [("deity", DOWN * cls + r"\r"), ("alignment", DOWN * deity + r"\r")]
    else:
        steps += [("alignment", DOWN * cls + r"\r")]
    steps += [("rolled-0", DOWN * align + r"\r")]
    steps += [(f"rolled-{k + 1}", "Y") for k in range(rerolls)]
    steps += [("name", "N"), ("icon", name + r"\r")]
    steps += icon
    steps += [("save", "Y"), ("saved", "Y")]
    return steps


def files_of(folder):
    """The roster's files in FOLDER (not a saved game's): {file name:
    bytes}."""
    out = {}
    for path in sorted(folder.iterdir()):
        if path.suffix.upper() in (".PC", ".WIZ", ".JNK", ".EFF") and \
                not path.name.upper().startswith("CHRDAT"):
            out[path.name] = path.read_bytes()
    return out


def masked(name, data):
    """A character's file with its far pointers 0 (the original writes them
    as they are in memory, the port 0): .PC's effects (+0x3a), items and
    slots (+0x7d-+0xb4; dkk_start's mask runs on over +0xb5-+0xb8, the
    hands, save bonus and weight, compared here), next, combat record and
    spells' block (+0xb9, +0xbd, +0xc2); .JNK's +0x2a and .EFF's +5."""
    data = bytearray(data)
    ext = name.rsplit(".", 1)[-1].upper()
    if ext == "PC":
        for at, n in ((0x3A, 4), (0x7D, 0xB5 - 0x7D), (0xB9, 4), (0xBD, 4), (0xC2, 4)):
            data[at:at + n] = bytes(n)
    elif ext == "JNK":
        for k in range(0, len(data) - 62, 63):
            data[k + 0x2A:k + 0x2E] = bytes(4)
    elif ext == "EFF":
        for k in range(0, len(data) - 8, 9):
            data[k + 5:k + 9] = bytes(4)
    return bytes(data)


def compare_files(v, what, original, port):
    v.value(f"{what}: the files", sorted(original), sorted(port))
    for n in sorted(original):
        if n not in port:
            continue
        x = masked(n, original[n])
        y = masked(n, port[n])
        diff = [k for k in range(min(len(x), len(y))) if x[k] != y[k]][:8]
        v.check(x == y, f"{what}: {n}" + ("" if x == y else
                                          f" differs at {[hex(k) for k in diff]} "
                                          f"(sizes {len(x)}, {len(y)}): original "
                                          f"{[hex(x[k]) for k in diff]}, port "
                                          f"{[hex(y[k]) for k in diff]}"))


def run_case(v, work, opts, case):
    name, seed, race, gender, cls, deity, align, rerolls, icon, modify, what = case
    print(f"{name} ({what}), seed {seed}:", flush=True)
    saves = work / f"saves-{name}"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    steps = create_steps(race, gender, cls, deity, align, rerolls, name, icon)
    if modify is not None:
        steps += ADD + MODIFY[modify]
    box_dir = work / f"box-{name}"
    keys = ""
    port_saves = None
    created = None
    with cokbox.Box(workdir=box_dir, game="death-knights", machine="ega", seed=seed,
                    saves=[saves], verbose=opts.verbose, timeout=1800, keep=True) as box:
        v.check(K.title(box), f"{name}: the title's four pictures")
        K.type_steps(box, TITLE_STEPS, settle=2.0)
        for step, typed in steps:
            keys += typed
            K.type_steps(box, [typed], settle=2.0)
            shown = box.screenshot(work / f"dosbox-{name}-{step}.png")
            _, shot, _, _ = K.port(work, f"{name}-{step}", keys, seed, saves=saves)
            port_saves = work / f"port-{name}-{step}" / "saves"
            v.screen(f"{name}: {step}", shown, cokbox.Screen.load(shot) if shot else None,
                     work=work)
            if step == "saved":
                created = (files_of(box_dir / "c" / "DKK" / "SAVE"), files_of(port_saves))
    if created is not None:
        compare_files(v, f"{name} created", *created)
    if modify is not None:
        compare_files(v, f"{name} modified", files_of(box_dir / "c" / "DKK" / "SAVE"),
                      files_of(port_saves))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_create"))
    parser.add_argument("--case", action="append", help="only these names")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    for case in CASES:
        if opts.case and case[0] not in opts.case:
            continue
        run_case(v, work, opts, case)
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())

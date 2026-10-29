#!/usr/bin/env python3
"""The Dark Queen of Krynn's Create Character in the original against
eclplay --game dark-queen --start-menu --mouse (DARK-QUEEN.md's step 2:
dqk-create; docs/checks.md, "Checks against the original": dqk_create),
compared in RGB: the camp's Create (7368:046f, 66e5:2f95), the form of race,
gender, class and alignment (66e5:2a6c: radio buttons the mouse clicks, the
box's pointer moved by xdotool), a cleric's deity (66e5:36b0), the rolls
and the sheet with Reroll Stats and Done (66e5:1f25), the name (66e5:193f),
the icon's picker (66e5:0cf1: a cell clicked, Done), the question "Save
...?" and the file written (604b:23a7).

The random numbers: the generator is the game's own (2102:0234, DS:ca0e), so
that the port's seed where the form opens (its "seed:" line) is written to
the original's DS:ca0e as the form opens, and every roll then is the same.
The cases make characters of each class family (a human fighter, a
Silvanesti cleric of the neutral gods, a dwarf, a kender thief, a half-elf
multi-class mage); the files they write are compared byte for byte but the
far pointers (the record's, the items' and each effect's).

    python3 tools/dosbox/checks/dqk_create.py [--workdir DIR] [--verbose] [--only CASE ...]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import re
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

# The form's buttons: a click at the pixel of each (66e5:2a6c: rows from 24
# by 8, columns 12 and 148, the text 12 on).
RACE = {name: (30, 27 + 8 * i) for i, name in enumerate(
    ["silvanesti", "qualinesti", "half-elf", "mountain-dwarf", "hill-dwarf", "kender", "human"])}
CLASS = {"cleric": (170, 27), "knight": (170, 35), "fighter": (170, 43), "paladin": (170, 51),
         "ranger": (170, 59), "mage": (170, 67), "thief": (170, 75),
         "cleric-fighter": (170, 83), "cleric-fighter-mage": (170, 91),
         "cleric-ranger": (170, 99), "cleric-mage": (170, 107), "cleric-thief": (170, 115),
         "fighter-mage": (170, 123), "fighter-thief": (170, 131),
         "fighter-mage-thief": (170, 139), "mage-thief": (170, 147)}
GENDER = {"male": (30, 167), "female": (30, 175)}
LAW = {"lawful": (30, 103), "neutral": (30, 111), "chaotic": (30, 119)}
GOOD = {"good": (30, 131), "neutral": (30, 139)}

# case: (name, [steps]); a step is ("click", x, y), ("key", keys) or
# ("name", text); the form opens at the first step and "seed" is written
# to the original there.
CASES = {
    "fighter": [("form", "human", "fighter", "male", None, None)],
    "cleric": [("form", "silvanesti", "cleric", "female", "lawful", "good"), ("deity", 1)],
    "dwarf": [("form", "hill-dwarf", "cleric", "male", None, None)],
    "kender": [("form", "kender", "thief", "male", None, None)],
    "multi": [("form", "half-elf", "fighter-mage", "female", None, None)],
}


def port_keys(case):
    """eclplay's keys for the case, and what each one is (for the shots)."""
    keys, steps = "\\k\\ePC", []
    kind, race, klass, gender, law, good = case[0]
    for table, value in ((RACE, race), (CLASS, klass), (GENDER, gender), (LAW, law),
                         (GOOD, good)):
        if value is not None:
            x, y = table[value]
            keys += f"\\m{x},{y},1."
            steps.append(("click", x, y))
    keys += "d"
    steps.append(("key", "d"))
    return keys, steps


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--only", nargs="*")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_create")
    verdict = checklib.Verdict()
    verdict.check(False, "not written yet")
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

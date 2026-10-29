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

    python3 tools/dosbox/checks/dqk_create.py [--workdir DIR] [--verbose] [--flows CASE ...]

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


def click(x, y):
    return ("click", x, y)


def form(race, klass, gender, law, good):
    return [(f"{race}", click(*RACE[race])), (f"{klass}", click(*CLASS[klass])),
            (f"{gender}", click(*GENDER[gender])), (f"{law}", click(*LAW[law])),
            (f"{good}", click(*GOOD[good])), ("form done", ("key", "d"))]


def finish(name, rights, reroll=0, around=False, click_at=None):
    """The roll (REROLL times Reroll Stats), the name, the icon moved RIGHTS
    cells right (AROUND: seven cells left and seven up, past each edge),
    and the question answered yes."""
    steps = [(f"reroll {n + 1}", ("key", "r")) for n in range(reroll)]
    steps.append(("stats done", ("key", "d")))
    steps += [(f"name {ch}", ("key", ch)) for ch in name]
    steps.append(("name entered", ("key", "\\r")))
    steps += [(f"icon right {n + 1}", ("key", "\\>")) for n in range(rights)]
    if click_at is not None:
        steps.append(("icon click", click(*click_at)))
    if around:
        steps += [(f"icon left {n + 1}", ("key", "\\<")) for n in range(7)]
        steps += [(f"icon up {n + 1}", ("key", "\\^")) for n in range(7)]
    steps += [("icon done", ("key", "d")), ("saved", ("key", "y")),
              ("pointer away", click(300, 60)), ("pointer on a button", click(40, 123))]
    return steps


# case: (file, steps); the form opens at the first step and the port's seed
# there is written to the original.
CASES = {
    "fighter": ("ZARTH", form("human", "fighter", "male", "lawful", "good")
                + finish("Zarth", 1)),
    "cleric": ("ELARA", form("silvanesti", "cleric", "female", "lawful", "good")
               + [("deity down", ("key", "\\v")), ("deity select", ("key", "s"))]
               + finish("Elara", 0, reroll=1)),
    "dwarf": ("DORN", form("hill-dwarf", "cleric", "male", "lawful", "neutral")
              + finish("Dorn", 2)),
    "kender": ("KIP", form("kender", "thief", "male", "neutral", "good")
               + finish("Kip", 3, around=True)),
    "multi": ("MIRA", form("half-elf", "fighter-mage", "female", "lawful", "good")
              + finish("Mira", 1, click_at=(8 + 24 * 3 - 1, 8 + 24 * 3 + 5))),
    # The sheet's Modify (6a87:1293 with creation's flag: no name to edit;
    # Next from charisma goes to the hit points): a strength and an
    # intelligence added and kept, then every field in turn, the hit points
    # added to and Exit.
    "modify": ("QUIN", form("human", "fighter", "male", "lawful", "good")
               + [("modify", ("key", "m")), ("add strength", ("key", "\\>")),
                  ("next", ("key", "n")), ("add intelligence", ("key", "\\>")),
                  ("keep", ("key", "k")), ("modify again", ("key", "m"))]
               + [(f"next {k}", ("key", "n")) for k in
                  ("intelligence", "wisdom", "dexterity", "constitution", "charisma",
                   "hit points", "strength")]
               + [("previous", ("key", "p")), ("add hit points", ("key", "\\>")),
                  ("exit", ("key", "e"))]
               + finish("Quin", 0)),
    # What ends a creation: Escape from the sheet's bar, Exit from the form,
    # Escape from the name, Exit from the icons and No to the save, each
    # back at the camp; the next begins with the generator where it was left.
    "exits": (None, form("human", "fighter", "male", "lawful", "good")
              + [("sheet escape", ("key", "\\e")), ("again", ("key", "C"))]
              + [("form exit", ("key", "e")), ("again", ("key", "C"))]
              + form("kender", "thief", "female", "neutral", "good")
              + [("sheet exit", ("key", "e")), ("again", ("key", "C"))]
              + form("hill-dwarf", "fighter", "male", "lawful", "neutral")
              + [("stats done", ("key", "d")), ("name Q", ("key", "Q")),
                 ("name escape", ("key", "\\e")), ("again", ("key", "C"))]
              + form("half-elf", "mage-thief", "male", "chaotic", "neutral")
              + [("stats done", ("key", "d")), ("name R", ("key", "R")),
                 ("name entered", ("key", "\\r")), ("icon exit", ("key", "e")),
                 ("again", ("key", "C"))]
              + form("silvanesti", "ranger", "female", "lawful", "good")
              + [("stats done", ("key", "d")), ("name S", ("key", "S")),
                 ("name entered", ("key", "\\r")), ("icon done", ("key", "d")),
                 ("save no", ("key", "n"))]),
}


# The steps that end at the camp.
BACK_AT_THE_CAMP = {"saved", "sheet escape", "form exit", "sheet exit", "name escape",
                    "icon exit", "save no"}


def key_text(step):
    kind = step[0]
    if kind == "click":
        return f"\\m{step[1]},{step[2]},1."
    return step[1]


def port_run(work, case, saves):
    """eclplay's shots with the case's steps (saving to SAVES), and its log."""
    keys = "\\k\\ePC" + "".join(key_text(s) for _, s in CASES[case][1])
    shots = work / f"port-{case}"
    if shots.exists():
        shutil.rmtree(shots)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--mouse", "--shots", str(shots),
                             "--saves", str(saves), "--start-menu", "--keys", keys,
                             str(dqk_lib.ASSETS)], capture_output=True, text=True, cwd=ROOT,
                            timeout=300)
    log = result.stdout + result.stderr
    (work / f"port-{case}.txt").write_text(log)
    return ([cokbox.RGBScreen.load(p) for p in
             sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))], log)


def far_pointers(data):
    """The offsets of a saved character's far pointers (a record's next,
    effects, items and slots; each effect's next): the original's are its
    heap's addresses, the port's are not."""
    mask = set()
    for o, n in [(0, 4), (4, 4), (10, 2), (0xc, 0x34)]:
        mask.update(range(o, o + n))
    items = data[8] | data[9] << 8 if len(data) > 9 else 0
    at = 0x19e + items * 17
    effects = data[4:8] != b"\0\0\0\0"
    while effects and at + 9 <= len(data):
        mask.update(range(at + 5, at + 9))
        effects = data[at + 5:at + 9] != b"\0\0\0\0"
        at += 9
    return mask


def character_differences(a, b):
    if len(a) != len(b):
        return ["size"]
    mask = far_pointers(a)
    return [hex(k) for k in range(len(a)) if a[k] != b[k] and k not in mask]


def run_case(case, opts, work, verdict):
    file_name, steps = CASES[case]
    saves = work / f"saves-{case}"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(dqk_lib.ASSETS / "SAVE", saves)
    shots, log = port_run(work, case, saves)
    # The generator where each creation starts (the original's, between the
    # creations, is moved by what the camp's idle frames draw).
    seeds = [struct.pack("<I", int(m, 16)) for m in re.findall(r"seed: ([0-9a-f]{8})", log)]
    if not seeds:
        verdict.differ(case, "the port logged no seed")
        return
    begun = 0
    out = work / f"original-{case}"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    with cokbox.Box(workdir=work / f"box-{case}", game="dark-queen", verbose=opts.verbose,
                    timeout=900, extra_conf={"sdl.mouse_emulation": "always"}) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        for key in ("\\e", "P", "C"):
            box.keys(key)
            box.settled(tolerance=64)
        box.ds_write(0xCA0E, seeds[0])
        begun = 1
        last = (0, 0)
        for n, (what, step) in enumerate(steps):
            if step[0] == "click":
                box.click(step[1], step[2])
                last = (step[1], step[2])
            else:
                box.keys(step[1])
            shot = box.settled(tolerance=64)
            if step == ("key", "C") and begun < len(seeds):
                box.ds_write(0xCA0E, seeds[begun])
                begun += 1
            shot.save_png(out / f"{n:02d}-{checklib.slug(what)}.png")
            masks = ()
            if what in BACK_AT_THE_CAMP:
                x, y = last
                masks = ((x - 8, y - 8, 24, 24, "the camp's cursor: the original keeps the "
                          "shield the form's pointer had until it moves (the next steps "
                          "agree)"),)
            elif what.startswith("name ") or what == "stats done":
                masks = ((0, 184, 320, 16, "the name's blinking caret"),)
            verdict.screen(f"{case}: {what}", shot, shots[n + 4] if len(shots) > n + 4 else None,
                           masks=masks, work=work)
        box.settle(1.0)
        written = b""
        if file_name is not None:
            original = box.cdrive / "DQK" / "SAVE" / (file_name + ".QCH")
            written = original.read_bytes() if original.exists() else b""
    if file_name is None:
        return
    ported = saves / (file_name + ".QCH")
    port = ported.read_bytes() if ported.exists() else b""
    verdict.value(f"{case}: the file written ({file_name}.QCH, but the far pointers)",
                  (len(written), character_differences(written, port)[:20] if port else ["none"]),
                  (len(port), []))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--flows", "--only", dest="only", nargs="*", metavar="CASE")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_create")
    verdict = checklib.Verdict()
    for case in CASES:
        if not opts.only or case in opts.only:
            run_case(case, opts, work, verdict)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

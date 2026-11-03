#!/usr/bin/env python3
"""The Dark Queen of Krynn's adventure camp (Encamp: 6fea:0365's key 4, then
5f07:0b3b, the camp's menu 7070:2d3c, "View Magic Rest Alt Fix Load Save Exit")
in the original and in eclplay (DARK-QUEEN.md's step 4, part 44: dqk-encamp;
docs/checks.md, "Checks against the original": dqk_camp). Both sides start as
dqk_party's walk does: saved game A loaded from the start menu (Load, Tdqk, A;
the original with plta), Begin Adventuring past the copy protection (answered
in the box; the port logs and passes it over), the palace, the leaving and
Caergoth's 3D view with the adventure's commands (the same keys as
dqk_party's route, three steps west of Caergoth's first text).

Then, in the adventure's commands, Encamp (e): the camp's menu is shown (the
screen compared in RGB, the mouse's pointer masked, the variables 0x0001-0x0400
compared); View (v) and the sheet's Escape back to the menu; Escape to leave the
menu (the adventure's screen and variables). Then Encamp again, Save (s) to slot
C, "Exit to DOS?" answered No (n), Escape: the saved game (SAVGAMC.QSV) compared
(the far pointers masked, the slots and the effects' pointer by nil-ness), and
the screen. Then Encamp and Load: "Game not saved. Load anyway? " is asked (the
game was saved, then the camp's Exit cleared that: DS:ae5b), its screen compared.

Then the Alt menu (a, 7070:1ce2), after Load's question is answered No (Escape): Speed
(s: Slower, Exit), Level (l: Champion), Order (o: Select, then Down moves the selected
member, Place, Exit), Drop (d: Yes, Yes: one member goes) and Exit, and the camp's Save to
slot D (s, d, n, Escape): each step's screen in RGB and the variables compared, and the
saved game D compared as the Save above is (five members now; the bytes the save pads past
the item table are masked, 604b:3352-3366).

Not compared: Magic, Rest, Fix, Icon and Load's yes (they stop eclplay: their routines
are not ported). About a minute.

    python3 tools/dosbox/checks/dqk_camp.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_create  # noqa: E402
import dqk_fight  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_modify  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ("--game", "dark-queen")
DS_VARS = 0xB20F
VARIABLES = 0x400
C2EF = 0xC2EF  # DS:c2ef: the camping picture animates while it is above 0 (1bdb:1127)
SLOT = "C"
SAVED = f"SAVGAM{SLOT}.QSV"
# The mouse's pointer at the corner (ALWAYS 2, drawn by the port's --mouse and by DOSBox-X's cursor).
POINTER = (8, 8, 8, 9, "the mouse pointer at the corner")
# The camping picture (small picture 0x80 at cell 3, 3): the original animates it while the
# camp's menu waits (DS:c2ef above 0, checked at each menu, 1bdb:1127), so its frame at a
# shot is the animation's and the screens that show it mask it.
PICTURE = (24, 24, 88, 90, "the camping picture, animating (DS:c2ef above 0)")
PARTY = 6  # saved game A's members
# The walk to the commands: the start menu's Load (PLTA: the original's plta, its
# Tdqk and letter A), Begin (B; the original's b, then the copy protection answered and
# its Enter, which the port passes over), the first texts, Leave, and three steps west.
START = "\\k\\ePLTAB\\r\\r\\rP\\r\\r\\r\\r\\rL\\r\\^\\^\\^"
ORIGINAL_WALK = "\\r\\r\\rp\\r\\r\\r\\r\\rl\\r\\^\\^\\^"
# The camp's keys: Encamp, View, Escape (the menu again), Escape (the adventure), Save C,
# No, Escape, then Encamp and Load (its question, "Game not saved. Load anyway? ").
MENU = START + "E"
SHEET = START + "Ev"
AGAIN = START + "Ev\\e"
LEAVE = START + "Ev\\e\\e"
SAVE = START + "Ev\\e\\eESCN\\e"
QUESTION = START + "Ev\\e\\eESCN\\eEl"
# The Alt menu after the question's No (Escape): each step's keys, typed after the last.
ALT_SLOT = "D"
ALT_SAVED = f"SAVGAM{ALT_SLOT}.QSV"
ALT_STEPS = [
    ("the camp's Alt menu", "a"),
    ("Alt's Speed, Slower and Exit", "sse"),
    ("Alt's Level, Champion", "lc"),
    ("Alt's Order: Select", "os"),
    ("Alt's Order: a member moved down and placed, Exit", "\\vpe"),
    ("the camp's menu after Alt's Drop of the selected member and Exit", "dyye"),
    ("the adventure after Save D, No and Escape", "sdn\\e"),
]
MEMBERS_AFTER_DROP = 5


def port_run(work, name, keys, saved=SAVED):
    """eclplay's last shot, its log, its saved game (bytes or b"") and its
    variables 0x0001-0x0400 at the end (bytes or b"")."""
    saves = work / f"saves-{name}"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(dqk_lib.ASSETS / "SAVE", saves)
    shots = work / f"shots-{name}"
    if shots.exists():
        shutil.rmtree(shots)
    vars_path = work / f"vars-{name}.bin"
    args = [str(cokbox.ECLPLAY), *GAME, "--mouse", "--start-menu", "--saves", str(saves),
            "--shots", str(shots), "--keys", keys, "--vars", str(vars_path)]
    result = subprocess.run([*args, str(dqk_lib.ASSETS)], capture_output=True, text=True,
                            cwd=ROOT, timeout=300)
    log = result.stdout + result.stderr
    (work / f"port-{name}.txt").write_text(log)
    files = sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))
    shot = cokbox.RGBScreen.load(files[-1]) if files else None
    game = (saves / saved).read_bytes() if (saves / saved).exists() else b""
    variables = vars_path.read_bytes() if vars_path.exists() else b""
    return shot, log, game, variables


def nil(data):
    return data == bytes(len(data))


def saved_differences(original, port, members=PARTY):
    """The offsets where the two saved games differ, and the pointers' nil-ness.
    Masked: each character's record next (0-3), the items' pointer's high word (10-11)
    and each effect's next (the original's are its heap's addresses, as
    dqk_create.far_pointers masks them), and the bytes from the item table's end on
    (the save pads from DS:c6de past its 0x10ef bytes, 604b:3352-3366, which the original
    fills with its heap: the port writes 0 there, party.c). Compared by nil-ness, not
    masked: the effects' pointer (+4-7) and the 13 slots (+0x0c-0x3f): the original's
    slots are the readied items' (dqk_create.original_slots, model_slots: the port's model
    of them)."""
    if len(original) != len(port):
        return {"size": (len(original), len(port))}
    mask, nil_area, diffs = set(), set(), {}
    pad = dqk_modify.record_at(original, members)  # the characters' end, where the pad starts
    mask.update(range(pad + 0x10EF, len(original)))
    for member in range(members):
        at = dqk_modify.record_at(original, member)
        mask.update(at + o for o in dqk_create.far_pointers(original[at:])
                    if not (4 <= o < 8 or 0x0C <= o < 0x40))
        nil_area.update(range(at + 4, at + 8), range(at + 0x0C, at + 0x40))
        slots = dqk_create.original_slots(original[at:])
        model = dqk_create.model_slots(port[at:])
        if slots != model:
            diffs[f"member {member + 1}: readied slots"] = (slots, model)
        effects = (nil(original[at + 4:at + 8]), nil(port[at + 4:at + 8]))
        if effects[0] != effects[1]:
            diffs[f"member {member + 1}: effects pointer nil"] = effects
    for k in range(len(original)):
        if original[k] != port[k] and k not in mask and k not in nil_area:
            diffs[f"{k:#06x}"] = (original[k], port[k])
    return diffs


def differing(original, port, addresses):
    """The variables (address: original, port) that differ."""
    return {f"{a:#x}": (original[a - 1], port[a - 1] if len(port) >= a else None)
            for a in addresses if len(port) < a or original[a - 1] != port[a - 1]}


def wait_key(box, seconds=120):
    """The screen once the game waits for a key (Box.waiting: a menu's, a sheet's
    or a question's wait, or a ReadKey): a state wait, not a settling of the screen."""
    end = time.monotonic() + seconds
    while not box.waiting():
        if time.monotonic() > end:
            raise cokbox.BoxError(f"the game did not wait for a key in {seconds} s")
        time.sleep(0.05)
    return box.screenshot()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_camp")
    out = work / "original"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    verdict = checklib.Verdict()

    # The port's screens and variables at each step, and its saved game C and question.
    menu, log_menu, _, port_menu = port_run(work, "menu", MENU)
    sheet, _, _, port_sheet = port_run(work, "view", SHEET)
    again, _, _, port_again = port_run(work, "again", AGAIN)
    leave, _, _, port_leave = port_run(work, "leave", LEAVE)
    saved_shot, log_save, port_saved, port_saved_vars = port_run(work, "save", SAVE)
    question, log_question, _, port_question_vars = port_run(work, "question", QUESTION)

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
        box.keys("plta")
        box.settled(tolerance=64)
        box.keys("b")
        box.settled(tolerance=64)
        _, _, answer = dqk_lib.protection(box)
        box.keys(answer + "\\r")
        box.settled(tolerance=64)
        box.keys(ORIGINAL_WALK)
        box.settled(tolerance=64)

        def original_variables():
            return dqk_fight.block_vars(box)

        def compare_variables(what, port_vars):
            verdict.value(f"the variables 0x0001-0x0400 at {what} (far DS:b20f)",
                          differing(original_variables(), port_vars, range(1, VARIABLES + 1)), {})

        steps = [("the camp's menu", "e", menu, port_menu),
                 ("the view's sheet", "v", sheet, port_sheet),
                 ("the camp's menu again", "\\e", again, port_again),
                 ("the adventure after Exit", "\\e", leave, port_leave)]
        for what, typed, port_shot, port_vars in steps:
            box.keys(typed)
            shot = wait_key(box)
            shot.save_png(out / f"{checklib.slug(what)}.png")
            masks = [POINTER]
            if what.startswith("the camp's menu"):
                animating = box.read(box.ds * 16 + C2EF, 1)
                verdict.check(animating != b"\0", f"the camping picture animates at {what} "
                              "(DS:c2ef above 0): masked", animating.hex())
                masks.append(PICTURE)
            verdict.screen(what, shot, port_shot, masks=masks, work=work)
            compare_variables(what, port_vars)

        box.keys("e")
        wait_key(box)
        box.keys("s" + SLOT.lower() + "n\\e")
        shot = wait_key(box)
        shot.save_png(out / "after-save.png")
        verdict.screen("the adventure after Save C, No and Escape", shot, saved_shot,
                       masks=[POINTER], work=work)
        compare_variables("the adventure after Save C, No and Escape", port_saved_vars)
        original_saved = (box.game / "SAVE" / SAVED).read_bytes() \
            if (box.game / "SAVE" / SAVED).exists() else b""
        verdict.value(f"the saved game {SLOT} the original and the port write (byte for byte, "
                      "the far pointers masked, the slots and the effects' pointer by nil-ness)",
                      saved_differences(original_saved, port_saved) if original_saved else
                      {"missing": True}, {})

        # Save, Exit, Encamp, Load: the game was saved, but Exit cleared that (5f07:0ba6),
        # so Load asks "Game not saved. Load anyway? " (7070:2f07).
        box.keys("e")
        wait_key(box)
        box.keys("l")
        shot = wait_key(box)
        shot.save_png(out / "load-question.png")
        verdict.screen("the camp's Load after Save and Exit asks whether to load anyway", shot,
                       question, masks=[POINTER, PICTURE], work=work)
        compare_variables("the camp's Load question", port_question_vars)

        # Alt after the question's No (Escape): Speed, Level, Order, Drop, Exit, and Save D.
        box.keys("\\e")
        wait_key(box)
        typed = ""
        for what, keys in ALT_STEPS:
            typed += keys
            name = f"alt-{len(typed)}"
            port_shot, log_alt, port_alt_saved, port_alt_vars = port_run(
                work, name, QUESTION + "\\e" + typed, saved=ALT_SAVED)
            if "not yet ported" in log_alt:
                verdict.check(False, f"the port runs {what} (no stop)", log_alt.strip().splitlines()[-1])
            box.keys(keys)
            shot = wait_key(box)
            shot.save_png(out / f"{checklib.slug(what)}.png")
            # Drop's farewell is a timed message: the step after its keys (Exit) is where the
            # state is compared, once the list is drawn again.
            if what.startswith("the adventure"):
                verdict.screen(what, shot, port_shot, masks=[POINTER], work=work)
            else:
                verdict.screen(what, shot, port_shot, masks=[POINTER, PICTURE], work=work)
            compare_variables(what, port_alt_vars)
        original_alt_saved = (box.game / "SAVE" / ALT_SAVED).read_bytes() \
            if (box.game / "SAVE" / ALT_SAVED).exists() else b""
        verdict.value(f"the saved game {ALT_SLOT} after Alt the original and the port write "
                      "(byte for byte, the far pointers masked, the slots and the effects' "
                      "pointer by nil-ness)",
                      saved_differences(original_alt_saved, port_alt_saved, MEMBERS_AFTER_DROP)
                      if original_alt_saved else {"missing": True}, {})
    menu_line = "menu: View Magic Rest Alt Fix Load Save Exit"
    verdict.check(menu_line in log_menu and menu_line in log_save and "save: " in log_save,
                  "the port's Encamp menu shows its words, and Save writes the game",
                  log_save.strip().splitlines()[-1] if log_save.strip() else "")
    verdict.check("not yet ported" not in log_question,
                  "the port asks Load's question (no stop at Load)",
                  log_question.strip().splitlines()[-1] if log_question.strip() else "")
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

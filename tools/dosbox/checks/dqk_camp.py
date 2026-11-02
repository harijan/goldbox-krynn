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
screen compared in RGB, the mouse's pointer masked); View (v) and the sheet's
Escape back to the menu (compared); Escape to leave the menu (the adventure's
screen compared, and the variables 0x0001-0x0400 against eclplay's --vars).
Then Encamp again, Save (s) to slot C, "Exit to DOS?" answered No (n), Escape:
the saved game (SAVGAMC.QSV) compared byte for byte, and the screen.

Not compared: Magic, Rest, Alt, Fix and Load, which stop eclplay (their routines
are not ported). About a minute.

    python3 tools/dosbox/checks/dqk_camp.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_create  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_modify  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ("--game", "dark-queen")
DS_VARS = 0xB20F
VARIABLES = 0x400
SLOT = "C"
SAVED = f"SAVGAM{SLOT}.QSV"
# The mouse's pointer at the corner (ALWAYS 2, drawn by the port's --mouse and by DOSBox-X's cursor).
POINTER = (8, 8, 8, 9, "the mouse pointer at the corner")
# The camping picture (small picture 0x80 at cell 3, 3), which animates while the menu waits:
# its frame at a shot is the animation's, so the menu's screen after the sheet masks it.
PICTURE = (24, 24, 88, 90, "the camping picture's frame, animating while the menu waits")
PARTY = 6  # saved game A's members
# The walk to the commands: the start menu's Load (PLTA: the original's plta, its
# Tdqk and letter A), Begin (B; the original's b, then the copy protection answered and
# its Enter, which the port passes over), the first texts, Leave, and three steps west.
START = "\\k\\ePLTAB\\r\\r\\rP\\r\\r\\r\\r\\rL\\r\\^\\^\\^"
ORIGINAL_WALK = "\\r\\r\\rp\\r\\r\\r\\r\\rl\\r\\^\\^\\^"


def port_run(work, name, keys, variables=False):
    """eclplay's last shot, its log, its saved game C (bytes or b"") and, with
    variables, its variables 0x0001-0x0400 at the end (bytes or b"")."""
    saves = work / f"saves-{name}"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(dqk_lib.ASSETS / "SAVE", saves)
    shots = work / f"shots-{name}"
    if shots.exists():
        shutil.rmtree(shots)
    args = [str(cokbox.ECLPLAY), *GAME, "--mouse", "--start-menu", "--saves", str(saves),
            "--shots", str(shots), "--keys", keys]
    vars_path = work / f"vars-{name}.bin"
    if variables:
        args += ["--vars", str(vars_path)]
    result = subprocess.run([*args, str(dqk_lib.ASSETS)], capture_output=True, text=True,
                            cwd=ROOT, timeout=300)
    log = result.stdout + result.stderr
    (work / f"port-{name}.txt").write_text(log)
    files = sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))
    shot = cokbox.RGBScreen.load(files[-1]) if files else None
    saved = (saves / SAVED).read_bytes() if (saves / SAVED).exists() else b""
    variables_read = vars_path.read_bytes() if variables and vars_path.exists() else b""
    return shot, log, saved, variables_read


def saved_differences(original, port):
    """The offsets where the two saved games differ, but the far pointers (the
    records' and items' and effects' next, the slots: the original's are its heap's
    addresses, the port's are not, as dqk_create.py masks them) and the size."""
    if len(original) != len(port):
        return {"size": (len(original), len(port))}
    mask = set()
    for member in range(PARTY):
        at = dqk_modify.record_at(original, member)
        mask.update(at + o for o in dqk_create.far_pointers(original[at:]))
    return {f"{k:#06x}": (original[k], port[k]) for k in range(len(original))
            if original[k] != port[k] and k not in mask}


def differing(original, port, addresses):
    """The variables (address: original, port) that differ."""
    return {f"{a:#x}": (original[a - 1], port[a - 1] if len(port) >= a else None)
            for a in addresses if len(port) < a or original[a - 1] != port[a - 1]}


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

    # The port: the camp's menu (after E), the sheet (v), the menu again (Escape), the
    # adventure (Escape), with the variables; then Save C, No, Escape.
    menu, log_menu, _, _ = port_run(work, "menu", START + "E")
    sheet, _, _, _ = port_run(work, "view", START + "Ev")
    again, _, _, _ = port_run(work, "again", START + "Ev\\e")
    leave, log_leave, _, port_vars = port_run(work, "leave", START + "Ev\\e\\e", variables=True)
    saved_shot, log_save, port_saved, _ = port_run(work, "save", START + "Ev\\e\\eESCN\\e")

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
        steps = [("the camp's menu", "e", menu), ("the view's sheet", "v", sheet),
                 ("the camp's menu again", "\\e", again), ("the adventure after Exit", "\\e", leave)]
        for what, typed, port_shot in steps:
            box.keys(typed)
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"{checklib.slug(what)}.png")
            masks = [POINTER, PICTURE] if what == "the camp's menu again" else [POINTER]
            verdict.screen(what, shot, port_shot, masks=masks, work=work)
            if what == "the adventure after Exit":
                variables = box.far(box.ds * 16 + DS_VARS)
                original = box.read(variables + 1, VARIABLES)
                verdict.value("the variables 0x0001-0x0400 after Encamp and Exit (far DS:b20f)",
                              differing(original, port_vars, range(1, VARIABLES + 1)), {})
        box.keys("e")
        box.settled(tolerance=64)
        box.keys("s" + SLOT.lower() + "n\\e")
        shot = box.settled(tolerance=64)
        shot.save_png(out / "after-save.png")
        verdict.screen("the adventure after Save C, No and Escape", shot, saved_shot,
                       masks=[POINTER], work=work)
        original_saved = (box.game / "SAVE" / SAVED).read_bytes() \
            if (box.game / "SAVE" / SAVED).exists() else b""
        verdict.value(f"the saved game {SLOT} the original and the port write (byte for byte, "
                      "the far pointers masked)",
                      saved_differences(original_saved, port_saved) if original_saved else
                      {"missing": True}, {})
    menu_line = "menu: View Magic Rest Alt Fix Load Save Exit"
    verdict.check(menu_line in log_menu and menu_line in log_save and "save: " in log_save,
                  "the port's Encamp menu shows its words, and Save writes the game",
                  log_save.strip().splitlines()[-1] if log_save.strip() else "")
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""The Dark Queen of Krynn's text, picture and wait opcodes run in the
original and in eclplay (DARK-QUEEN.md's step 3d: dqk_ecl_text; docs/checks.md,
"Checks against the original"): five test blocks (tools/dqkecl.py, with the
text operands packed as src/ecl.c's unpack reads them) that replace block 1
of a copy of DISK3\\ECL.GLB, as dqk_ecl.py does. Each starts with the jumper's
LOAD FILES 2 1 and a PRESS KEY, as dqk_views' jumper does, with the saved
game's lamps: the original runs it typing a Return at every wait and taking
the screen there; eclplay runs the same block with --shots (a BMP at each
wait) and as many Returns. A block ending in CLEAR EXIT (no EXIT: its handler
ends the block) is compared at the screen after it, the bottom bar left out.

Checked, at every wait: the whole screen, but the mouse's pointer, the
party's list (no party in eclplay) and the three lamps under the view (see
LAMPS), as dqk_views compares it; and the variables 0x100-0x17f, byte for
byte. The blocks:

- text: PRINTCLEAR, PRINT (a text that wraps and waits at the window's end,
  and a number: an immediate, a wide word, a variable), PRINT RETURN, DELAY;
- colour_journal: JOURNAL in colour 3 (variable 0x39 set), then in colour 7
  with a wide number;
- pictures: PICTURE none (0xff), a small picture of each library (PIC A's
  113, PIC C's 239) and a big one (240);
- view_after_picture: the view again (PICTURE 0xff) after a small picture;
- box_exit: CLEAR BOX between PRINTs, then CLEAR EXIT at the end.

The text speed (variable 0x12) is 0 in both, as eclplay's default. Not in
run_all's set (make dqk-checks): view_after_picture and box_exit's last
screen differ (see docs/games.md, The Dark Queen's ECL).

    python3 tools/dosbox/checks/dqk_ecl_text.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_views  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import OPCODES, Block, imm, var, word  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ["--game", "dark-queen"]
JUMP = dqk_ecl.JUMP
JUMP_VAR = 0x16
DS_VARS = dqk_ecl.DS_VARS
VARIABLES = dqk_ecl.VARIABLES
# The variables compared: the blocks' own (0x100-0x17f), zeroed by each prelude.
WINDOW = list(range(0x100, 0x180))
TEXT_SPEED = 0x12  # 3 ms times this per character while a text prints
COLOUR = 0x39      # PRINT's colour 3 where set
MAX_WAITS = 40
# The screen's areas not compared: the mouse's pointer (the mouse driver's),
# the party's list and status (eclplay has no party in these blocks), and the
# three lamps under the view, which the port's LOAD FILES leaves dark where the
# original lights them (the menus' frame draw does light them: dqk_views'
# jumper, not in this flow's screens). The bottom bar's menu is not ported
# (dqk_views' BAR), so the screen after a CLEAR EXIT leaves it out too.
LAMPS = (40, 119, 56, 10, "the lamps under the view (LOAD FILES leaves them dark)")
MASKS = [dqk_views.POINTER, dqk_views.PARTY, LAMPS]
END_MASKS = MASKS + [dqk_views.BAR]
DELAY_OP = 0x3A
WAIT_NAMES = {0x1D: "PRESS KEY", 0x11: "PRINT", 0x12: "PRINTCLEAR", 0x1F: "JOURNAL"}

# The opcodes this flow writes (dqkecl's table has the arithmetic and jumps).
dqkecl.OPCODES.update({"LOAD_FILES": 0x21, "PICTURE": 0x0E, "PRINT": 0x11, "PRINTCLEAR": 0x12,
                       "JOURNAL": 0x1F, "CLEAR_EXIT": 0x23, "PRINT_RETURN": 0x33,
                       "CLEAR_BOX": 0x3D, "DELAY": 0x3A})

LONG = ("THE PARTY WALKS ON INTO THE DARK WOOD, WHERE THE TREES CLOSE OVERHEAD AND THE PATH "
        "IS LOST IN THE FERN. THEY STOP TO LISTEN. SOMETHING IS MOVING AHEAD, SLOW AND HEAVY, "
        "AND IT IS NOT ALONE.")


def text(s):
    """A string operand (src/ecl.c's cok_ecl_operands): type 0x80, the count of
    packed bytes, then the 6-bit codes, four to three bytes (unpack). A letter
    is its code less 0x40, a space or punctuation or digit its own byte."""
    codes = []
    for c in s:
        n = ord(c)
        if 0x40 < n < 0x60:
            codes.append(n - 0x40)
        elif 0x20 <= n < 0x40:
            codes.append(n)
        else:
            raise ValueError(f"no code for {c!r}")
    codes += [0] * (-len(codes) % 4)  # code 0 is skipped
    packed = bytearray()
    for i in range(0, len(codes), 4):
        c0, c1, c2, c3 = codes[i:i + 4]
        packed += bytes([c0 << 2 | c1 >> 4, (c1 & 15) << 4 | c2 >> 2, (c2 & 3) << 6 | c3])
    return bytes([0x80, len(packed)]) + bytes(packed)


def start(b):
    """The jumper's first instruction (block 1's LOAD FILES 2 1, dqk_views): the
    view drawn as the game draws it, for the screens to compare."""
    b.op("LOAD_FILES", imm(2), imm(1))
    b.op("PRESS_KEY")


def text_block():
    b = Block()
    start(b)
    b.op("PRINTCLEAR", text("ONE TWO THREE"))
    b.op("PRINT_RETURN")
    b.op("PRINT", imm(7))
    b.op("PRINT", word(300))
    b.op("SAVE", imm(42), var(0x100))
    b.op("PRINT", var(0x100))
    b.op("PRINT_RETURN")
    b.op("PRINT", text(LONG))
    b.op("DELAY")
    b.op("PRINTCLEAR", text("THE LAST LINE"))
    b.op("PRESS_KEY")
    b.op("EXIT")
    return b


def colour_block():
    """JOURNAL in colour 3 (variable 0x39 set), then in colour 7 with a wide
    number: each alone against its own difference."""
    b = Block()
    start(b)
    b.op("SAVE", imm(1), var(COLOUR))
    b.op("PRINTCLEAR", text("COLOUR THREE"))
    b.op("PRESS_KEY")
    b.op("JOURNAL", imm(9))
    b.op("SAVE", imm(0), var(COLOUR))
    b.op("JOURNAL", word(300))
    b.op("PRINTCLEAR", text("COLOUR SEVEN"))
    b.op("PRESS_KEY")
    b.op("EXIT")
    return b


def picture_block():
    b = Block()
    start(b)
    b.op("PICTURE", imm(0xFF))   # none shown yet
    b.op("PICTURE", imm(113))    # PIC A
    b.op("PRESS_KEY")
    b.op("PICTURE", imm(239))    # PIC C
    b.op("PRESS_KEY")
    b.op("PICTURE", imm(240))    # a big picture
    b.op("PRESS_KEY")
    b.op("EXIT")
    return b


def view_block():
    """The view again after a picture (PICTURE 0xff): the 3D walls drawn again
    as the original draws them."""
    b = Block()
    start(b)
    b.op("PICTURE", imm(113))
    b.op("PRESS_KEY")
    b.op("PICTURE", imm(0xFF))
    b.op("PRESS_KEY")
    b.op("EXIT")
    return b


def box_block():
    b = Block()
    start(b)
    b.op("PRINTCLEAR", text("BEFORE CLEAR BOX"))
    b.op("PRESS_KEY")
    b.op("CLEAR_BOX")
    b.op("PRESS_KEY")
    b.op("PRINT", text("AFTER CLEAR BOX"))
    b.op("PRESS_KEY")
    b.op("PRINTCLEAR", text("THEN CLEAR EXIT"))
    b.op("CLEAR_EXIT")
    b.ends = "CLEAR_EXIT"  # no EXIT: the handler ends the block (5f07:1402)
    return b


def stop_of(block):
    """Where the block's ECL stops (dqk_ecl's stop_at); a block ending in CLEAR
    EXIT stops past it, with that opcode (its handler ends the block)."""
    if getattr(block, "ends", None) == "CLEAR_EXIT":
        return dqkecl.CODE_START + block.size, OPCODES["CLEAR_EXIT"]
    return dqk_ecl.stop_at(block)


BLOCKS = {"text": text_block, "colour_journal": colour_block, "pictures": picture_block,
          "view_after_picture": view_block, "box_exit": box_block}


def original(work, name, block, patched, verbose):
    """The original's run of the block from its PRESS KEY to its end, a
    Return at each wait. Returns (the screens at the waits, their names, the
    screen at the end, the state there, the variables after, the lamps)."""
    stop = stop_of(block)
    with cokbox.Box(workdir=work / f"box-{name}", game="dark-queen", verbose=verbose,
                    timeout=900, files={"DISK3/ECL.GLB": patched}) as box:
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
        variables = box.far(box.ds * 16 + DS_VARS)
        box.write(variables + JUMP_VAR, bytes([JUMP]))
        box.write(variables + TEXT_SPEED, b"\x00")
        lamps = box.read(variables + 0x1FA, 3)
        box.keys("b")
        box.settled(tolerance=64)
        _, _, answer = dqk_lib.protection(box)
        box.keys(answer + "\\r")
        screens, names, trail = [], [], []
        for _ in range(MAX_WAITS):
            shot = box.settled(tolerance=64)
            state = dqk_lib.ecl_state(box)
            if state["opcode"] == DELAY_OP:
                continue  # the DELAY's 600 ms: the screen still, not a wait
            trail.append(f"{state['pc']:#x}/{state['opcode']:#x}")
            if (state["pc"], state["opcode"]) == stop:
                break
            screens.append(shot)
            names.append(WAIT_NAMES.get(state["opcode"], f"opcode {state['opcode']:#x}"))
            box.keys("\\r")
        else:
            raise cokbox.BoxError(f"{name}: the block did not end in {MAX_WAITS} waits: "
                                  + " ".join(trail[-8:]))
        after = box.read(variables + 1, VARIABLES)
        return screens, names, shot, (state["pc"], state["opcode"]), after, lamps


def port(work, name, patched, lamps, waits):
    """eclplay's run of the block with the saved game's lamps and the jumper's
    variable, a BMP at each wait and Returns for them, and its final screen
    (eclplay ends at the block's EXIT). Returns (the screens at the waits, the
    final screen, the variables, eclplay's last line)."""
    assets = work / f"assets-{name}"
    if assets.exists():
        shutil.rmtree(assets)
    assets.mkdir(parents=True)
    for entry in dqk_lib.ASSETS.iterdir():
        if entry.name != "DISK3":
            (assets / entry.name).symlink_to(entry)
            continue
        (assets / "DISK3").mkdir()
        for f in entry.iterdir():
            if f.name != "ECL.GLB":
                (assets / "DISK3" / f.name).symlink_to(f)
    (assets / "DISK3" / "ECL.GLB").write_bytes(patched)
    shots = work / f"port-{name}-shots"
    if shots.exists():
        shutil.rmtree(shots)
    final = work / f"port-{name}-final.bmp"
    vars_path = work / f"port-{name}.vars"
    args = ["--set", "22=1"]
    for i, value in enumerate(lamps):
        args += ["--set", f"{0x1FA + i:x}={value:x}"]
    # One Return per wait: the block's last key is taken at its last wait, and
    # the run ends at the block's EXIT.
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, *args, "--shots", str(shots),
                             "--screen", str(final), "--vars", str(vars_path),
                             "--keys", "\\r" * waits, str(assets), str(JUMP)],
                            capture_output=True, text=True, cwd=ROOT, timeout=300)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    bmps = sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))
    last = (result.stdout.strip().splitlines() or [""])[-1]
    return ([cokbox.RGBScreen.load(p) for p in bmps],
            cokbox.RGBScreen.load(final) if final.exists() else None,
            vars_path.read_bytes() if vars_path.exists() else b"", last)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_ecl_text")
    verdict = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    for name, make in BLOCKS.items():
        block = make()
        patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
        screens, names, final, end, after, lamps = original(work, name, block, patched,
                                                             opts.verbose)
        for i, o in enumerate(screens):
            o.save_png(work / f"original-{name}-{i + 1}.png")
        final.save_png(work / f"original-{name}-end.png")
        stop = stop_of(block)
        verdict.check(end == stop, f"{name}: the original's ECL stopped at the block's last "
                      "instruction", f"pc {end[0]:#x}, opcode {end[1]:#x}, not "
                      f"{stop[0]:#x}, {stop[1]:#x}")
        mine, mine_final, port_vars, port_last = port(work, name, patched, lamps, len(screens))
        verdict.check(len(mine) == len(screens), f"{name}: eclplay waits where the original "
                      f"does ({len(screens)} waits)", f"{len(mine)} waits; {port_last}")
        for i, (o, p) in enumerate(zip(screens, mine)):
            verdict.screen(f"{name}: wait {i + 1} ({names[i]})", o, p, masks=MASKS, work=work)
        if getattr(block, "ends", None) == "CLEAR_EXIT":  # eclplay ends there, the game's screen
            verdict.screen(f"{name}: the screen after CLEAR EXIT", final, mine_final,
                           masks=END_MASKS, work=work)
        if len(port_vars) != VARIABLES:
            verdict.check(False, f"{name}: eclplay saved the variables", port_last)
            continue
        differing = [a for a in WINDOW if after[a - 1] != port_vars[a - 1]]
        verdict.check(not differing, f"{name}: the variables 0x100-0x17f agree",
                      ", ".join(f"{a:x}: original {after[a - 1]:02x}, port {port_vars[a - 1]:02x}"
                                for a in differing[:8]))
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

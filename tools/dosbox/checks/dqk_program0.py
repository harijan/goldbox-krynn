#!/usr/bin/env python3
"""The Dark Queen of Krynn's PROGRAM 0 (5f07:0c15: the camp from the adventure,
7368:046f with 0) run in the original and in eclplay (DARK-QUEEN.md's step 3i:
dqk_program0; docs/checks.md, "Checks against the original"), with dqk_ecl.py's
flow: a test block replaces block 1 of a copy of DISK3\\ECL.GLB, PRESS KEY,
SAVE 1 to variable 0x30, PROGRAM 0, SAVE 7 to variable 0x130, EXIT. Both sides
have saved game A's party (dqk_ecl_misc.py's six members: the original loads it
with plta, eclplay gets the six .QCH files with --character). After the PRESS KEY's Enter the
camp opens; then View Character (v) and Escape back to the camp's menu, then Begin
Adventuring (b): the block goes on to its end, the adventure's screen shown.

A second flow is on the overland: the block first sets variables 0x22 0 (after 1, so the
store makes mode 3), 0x24 1, 0x3e 0xf0 (the big picture) and the party's place at 5, 5; then
PROGRAM 0 with a PRESS KEY after it, so the screen is compared before the block's EXIT. Begin
(b) draws the overland's frame, and 177e:1ad1 the screen as it was in mode 3 (the overland map
again, and the place).

Compared, in each flow: the screen at the PRESS KEY and at each wait of the camp (the first
flow's view's sheet and the camp again too); the adventure after Begin (the hall's, after the
block's EXIT, its bottom bar masked); the variables the original's run changed (0x30 and the
marker, and 0x22, 0x24, 0x3e, 0x25, 0x26 in the overland) against eclplay's; the original's
ECL stopping at the block's last instruction. About half a minute.

    python3 tools/dosbox/checks/dqk_program0.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_ecl_misc  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_views  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, imm, var  # noqa: E402

cokbox = checklib.cokbox
GAME = ["--game", "dark-queen"]
JUMP = dqk_ecl.JUMP
VARIABLES = dqk_ecl.VARIABLES
DS_VARS = dqk_ecl.DS_VARS
MARKER = 0x130  # SAVE 7 here once the camp has been left (the block went on)
# The keys the camp reads after the PRESS KEY's Enter: View Character, Escape
# back to the camp's menu, Begin Adventuring.
TYPED = ["v", "\\e", "b"]
# The hall's screens: the camp, the view's sheet, the camp again, the adventure's screen
# after Begin (taken after the block's EXIT, the extra masks are for that one).
HALL = [
    ("the camp's menu", []),
    ("the view's sheet", []),
    ("the camp again", []),
    ("the adventure after Begin", ["BAR", "FOOT"]),
]
# The overland: Begin, then the block's second PRESS KEY (before its EXIT), where the
# screen is compared, and the Enter that ends the block.
OVERLAND_TYPED = ["b", "\\r"]
OVERLAND = [
    ("the camp's menu", []),
    ("the overland after Begin, at the block's second PRESS KEY", []),
]
# Not compared: the status line and the view's three foot icons (x 40-93, y 120-127), which
# eclplay's block entry does not draw (the adventure's start, dqk_party's Begin, does), and
# the bottom bar after the EXIT (the adventure's menu is not ported; the game's own DISK
# READ ERROR, which its adventure loop shows once the block has ended). Nothing the EXIT or
# the loop would draw after that is compared (the view's redraw at CALL 1, b7f8, is not
# reached in the original).
STATUS = (208, 104, 104, 24, "the status line, drawn by the adventure's start, not eclplay's")
FOOT = (40, 120, 56, 8, "the view's foot icons, drawn by the adventure's start, not eclplay's")
BAR = (0, 184, 320, 16, "the bottom bar after the EXIT (the game's own DISK READ ERROR)")
MASKS = {"STATUS": STATUS, "FOOT": FOOT, "BAR": BAR}


def changed(before, after):
    """The variables (1 to 0x400) whose values differ."""
    return [a for a in range(1, VARIABLES + 1) if before[a - 1] != after[a - 1]]


def block(overland):
    b = Block()
    b.op("PRESS_KEY")
    if overland:
        b.op("SAVE", imm(1), var(0x22))  # 1 then 0: the store to 0x22 makes mode 3
        b.op("SAVE", imm(0), var(0x22))
        b.op("SAVE", imm(1), var(0x24))
        b.op("SAVE", imm(0xf0), var(0x3e))
        b.op("SAVE", imm(5), var(0x25))
        b.op("SAVE", imm(5), var(0x26))
    b.op("SAVE", imm(1), var(0x30))  # Begin Adventuring sets it 0 (7368:07af's 0x30)
    b.op("PROGRAM", imm(0))
    if overland:
        b.op("PRESS_KEY")
    b.op("SAVE", imm(7), var(MARKER))
    b.op("EXIT")
    return b


def original(work, patched, verbose, typed):
    """The original's run of the block from its PRESS KEY: the state where it waits
    and where it ends (pc, opcode), the variables at the PRESS KEY and at the end,
    and the screens after each of the keys (the first is the PRESS KEY's Enter).
    Booted as dqk_ecl.original: saved game A (plta), the protection answered."""
    shots = []
    with cokbox.Box(workdir=work / "box", game="dark-queen", verbose=verbose, timeout=900,
                    files={"DISK3/ECL.GLB": patched}) as box:
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
        box.write(variables + 0x16, bytes([JUMP]))
        box.keys("b")
        box.settled(tolerance=64)
        _, _, answer = dqk_lib.protection(box)
        box.keys(answer + "\\r")
        box.settled(tolerance=64)
        wait = dqk_lib.ecl_state(box)
        box.set_seed(dqk_ecl.SEED)
        before = box.read(variables + 1, VARIABLES)
        pre = box.screenshot()  # the PRESS KEY's screen, before its Enter
        for key in ["\\r"] + typed:
            box.keys(key)
            shots.append(box.settled(tolerance=64))
        end = dqk_lib.ecl_state(box)
        after = box.read(variables + 1, VARIABLES)
    return ((wait["pc"], wait["opcode"]), before, (end["pc"], end["opcode"]), after, pre, shots)


def port(work, patched, typed):
    """eclplay's run of the block with saved game A's party: its variables (--vars),
    its shots at the waits (the PRESS KEY's first), its final screen, its output's
    last line, and whether keys are left unread. The assets are a copy with the
    block replacing block 1 (as dqk_ecl.port)."""
    assets = work / "assets-program0"
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
    vars_path, shots, screen = work / "port.vars", work / "port-shots", work / "port-end.bmp"
    if shots.exists():
        shutil.rmtree(shots)
    keys = "\\r" + "".join(typed)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, *dqk_ecl_misc.party_args(),
                             "--seed", str(dqk_ecl.SEED), "--vars", str(vars_path),
                             "--shots", str(shots), "--screen", str(screen), "--keys", keys,
                             str(assets), str(JUMP)],
                            capture_output=True, text=True, cwd=cokbox.ROOT, timeout=300)
    (work / "port.txt").write_text(result.stdout + result.stderr)
    last = (result.stdout.strip().splitlines() or [""])[-1]
    unread = "(keys left unread" in result.stderr
    frames = [cokbox.RGBScreen.load(p) for p in
              sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))] if shots.exists() else []
    end = cokbox.RGBScreen.load(screen) if screen.exists() else None
    return (vars_path.read_bytes() if vars_path.exists() else b""), frames, end, last, unread


def flow(verdict, work, verbose, name, overland):
    """One flow: the original and eclplay on the block, compared as the module says."""
    work = work / name
    work.mkdir(parents=True, exist_ok=True)
    typed, what = (OVERLAND_TYPED, OVERLAND) if overland else (TYPED, HALL)
    b = block(overland)
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(b))
    port_vars, frames, port_end, port_last, port_unread = port(work, patched, typed)
    verdict.check(not port_unread, f"{name}: eclplay leaves no keys unread",
                  "the camp took fewer keys than were typed")
    verdict.check("(done in block 1)" in port_last and len(port_vars) == VARIABLES,
                  f"{name}: eclplay runs the block to its EXIT", port_last)
    wait, before, end, after, pre, shots = original(work, patched, verbose, typed)
    verdict.check(wait == (b.address("entry") + 1, dqkecl.OPCODES["PRESS_KEY"]),
                  f"{name}: the original waits in the block's PRESS KEY",
                  f"pc {wait[0]:#x}, opcode {wait[1]:#x}")
    stop = dqk_ecl.stop_at(b)
    verdict.check(end == stop, f"{name}: the original's ECL stopped at the block's last instruction",
                  f"pc {end[0]:#x}, opcode {end[1]:#x}, not {stop[0]:#x}, {stop[1]:#x}")
    # eclplay's shots: the PRESS KEY's (the first), then the camp's and the rest of the
    # waits; the screen after the block's end is its final screen.
    ported = frames[1:] + [port_end]
    pre.save_png(work / "original-pre.png")
    verdict.screen(f"{name}: the PRESS KEY's screen", pre, frames[0] if frames else None,
                   masks=[dqk_views.POINTER, STATUS, FOOT], work=work)
    for i, (what_i, extra) in enumerate(what):
        shots[i].save_png(work / f"original-{i}.png")
        masks = [dqk_views.POINTER] + [MASKS[m] for m in extra]
        verdict.screen(f"{name}: {what_i}", shots[i], ported[i] if len(ported) > i else None,
                       masks=masks, work=work)
    if len(port_vars) != VARIABLES:
        verdict.check(False, f"{name}: eclplay saved the variables", port_last)
        return
    # The variables the original's run changed (the camp, Begin's) and the marker: eclplay
    # has the same values there. Its start is a fresh game, not saved game A (eclplay loads
    # no saved game here), so the others are not compared.
    moved = sorted(set(changed(before, after)) | {MARKER, 0x30})
    print(f"{name}: variables the original changed: {', '.join(f'{a:x}' for a in moved)}",
          flush=True)
    differing = [a for a in moved if after[a - 1] != port_vars[a - 1]]
    verdict.check(not differing, f"{name}: the variables the camp changes agree",
                  ", ".join(f"{a:x}: original {after[a - 1]:02x}, port {port_vars[a - 1]:02x}"
                            for a in differing[:8]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_program0")
    verdict = checklib.Verdict()
    flow(verdict, work, opts.verbose, "hall", overland=False)
    flow(verdict, work, opts.verbose, "overland", overland=True)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

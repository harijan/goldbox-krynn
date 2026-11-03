#!/usr/bin/env python3
"""The Dark Queen of Krynn's temple in the original and in eclplay (DARK-QUEEN.md's step 4,
part 42: dqk-temple; docs/checks.md, "Checks against the original": dqk_shops). Flow `temple`
(--flows temple):

A test block (tools/dqkecl.py) replaces block 1 of a copy of DISK3\\ECL.GLB, and the original
runs it from saved game A (dqk_fight's enter: plta, Begin Adventuring, the copy protection
answered), with the seed set after the block's entry PRESS KEY:

    PRESS KEY; SAVE 1 -> var 0x14 (the temple: 5f07:0861); COMBAT; EXIT

COMBAT with no monsters opens the temple (7825:0af6): "Heal View Repair Exit". Then, by keys,
View (v: the sheet, then its menu), the sheet's Escape back to the temple's menu, and Exit (e):
the block's EXIT returns to the adventure. Each step is compared with eclplay (--load of a copy
of saved game A, --play, the same keys typed; dqk_fight.port): the screen in RGB, the block's
1 KB of variables (DS:b20f, 1 to 0x400) and the party's records (pointers masked).

Heal (h, 7825:0615) and Repair (r, 7825:08fa) are not compared: their options cure through the
effects engine (148f) and the spells, not ported, so eclplay stops there citing them; the flow
checks that it does (the transcript names 7825:0615 and 7825:08fa, each in its own run).

    python3 tools/dosbox/checks/dqk_shops.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import shutil  # noqa: E402

import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_fight  # noqa: E402
import dqk_fight_lib as L  # noqa: E402
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, imm, var  # noqa: E402

cokbox = checklib.cokbox
JUMP = dqk_ecl.JUMP
SEED = dqk_ecl.SEED
VARIABLES = dqk_ecl.VARIABLES
TEMPLE = 0x14  # the variable COMBAT reads for the temple (5f07:0861)

# The steps: the keys typed after the block's entry PRESS KEY, one at a time for the original
# (STEP_KEYS: each step's new keys) and in full for eclplay's run that ends at each step.
STEPS = [("the temple's menu", ""), ("View's sheet", "v"),
         ("the temple's menu after the sheet", "\\e"), ("the adventure after Exit", "e")]
TYPED = ["", "v", "v\\e", "v\\ee"]
# The mouse pointer at the corner (the original's; eclplay has none), as dqk_fight masks it.
MASKS = [(2, 2, 16, 18, "the mouse cursor (the shield at the corner)")]
# The adventure after the temple's Exit: the original's box shows DISK READ ERROR in the bar and
# its redraw stops before the party's list (as after any COMBAT with no monsters that returns:
# the treasure's too, in the same box; a box artifact, not compared). The party's records and the
# variables are compared at this step, and the frame's pixels.
EXIT_MASKS = MASKS + [(0, 184, 320, 16, "the bottom bar: DISK READ ERROR in the original's box "
                       "after COMBAT returns (the treasure's too), eclplay's command bar"),
                      (136, 16, 176, 112, "the party's list and status: not drawn by the "
                       "original's redraw that the error stops (records compared apart)")]
# Heal and Repair, each typed alone: eclplay stops at the cited routine.
STOPS = [("h", "dqk 7825:0615", "Heal"), ("r", "dqk 7825:08fa", "Repair")]


def temple_block():
    """Block 1: the entry PRESS KEY, the temple's variable set, COMBAT (no monsters), EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    b.op("SAVE", imm(1), var(TEMPLE))
    b.op("COMBAT")
    b.op("EXIT")
    return b


def block_vars(box):
    """The block's 1 KB of variables (dqk_fight's read at DS:b20f)."""
    return dqk_fight.block_vars(box)


def original(work, patched, verbose):
    """The original's state after the entry PRESS KEY's key and each later step's key: the
    screen, the variables, the party's records (raw), and whether the game waits for a key."""
    out = []
    with cokbox.Box(workdir=work / "box-temple", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        dqk_fight.enter(box)
        box.set_seed(SEED)
        box.keys("\\r")  # the entry PRESS KEY: SAVE, COMBAT, the temple's menu
        for i, (what, keys) in enumerate(STEPS):
            if i > 0:
                box.keys(keys)
            shot = box.settled(tolerance=64)
            out.append(dict(what=what, screen=shot, vars=block_vars(box),
                            records=[raw for _, raw in L.records(box)],
                            waiting=box.waiting()))
    return out


def port(work, patched, i, typed):
    """eclplay's run of the block from saved game A's copy, the keys TYPED after the entry
    (dqk_fight.saved_entry, --load, --play). The screen is the last of its shots (--shots: one
    at each key wait), the one where the keys ran out: a menu's, not the screen after its
    return (eclplay ends a menu by returning from it). Returns (variables, the screen's RGB or
    None, the party's records as raw bytes, the transcript, its last line)."""
    records = work / f"port-temple{i}.records"
    shots = work / f"port-temple{i}-shots"
    for path in (records, shots):
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()
    extra = ["--load", str(dqk_fight.saved_entry(work)), "--play", "--records", str(records),
             "--shots", str(shots)]
    variables, last, _ = dqk_ecl.port(work, f"temple{i}", patched, typed=typed, extra=extra,
                                      block=None)
    data = records.read_bytes() if records.exists() else b""
    size = L.RECORD
    ported = [data[k:k + size] for k in range(0, max(len(data) - 3, 0), size)]
    waits = sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem)) if shots.is_dir() else []
    shot = cokbox.RGBScreen.load(waits[-1]) if waits else None
    transcript = (work / f"port-temple{i}.txt").read_text()
    return variables, shot, ported, transcript, last


def masked(raw):
    return dqk_fight.masked(raw.hex())


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows", default="temple", help="which flows: temple")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_shops")
    v = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(temple_block()))
    orig = original(work, patched, opts.verbose)
    (work / "original").mkdir(exist_ok=True)
    for o in orig:
        o["screen"].save_png(work / "original" / f"{checklib.slug(o['what'])}.png")
    for i, ((what, _), typed) in enumerate(zip(STEPS, TYPED)):
        o = orig[i]
        v.check(o["waiting"], f"the original waits for a key at {what}")
        variables, shot, ported, transcript, last = port(work, patched, i, typed)
        v.check(len(variables) == VARIABLES and "error:" not in transcript,
                f"eclplay runs the temple to {what} with no error", transcript.strip()[-200:])
        if shot is not None:
            masks = EXIT_MASKS if i == len(STEPS) - 1 else MASKS
            v.screen(f"the screen at {what}", o["screen"], shot, masks=masks, work=work)
        else:
            v.check(False, f"eclplay saves the screen at {what}")
        # The temple's variable reads 1 while its menu waits (cleared after the call returns);
        # eclplay's run ends by the menu's return, so it is not compared there (steps 0-2).
        ignore = {TEMPLE - 1} if i < len(STEPS) - 1 else set()
        differing = [a + 1 for a in range(VARIABLES)
                     if a not in ignore and o["vars"][a] != variables[a]]
        v.check(not differing, f"the variables at {what} agree (1 KB)",
                ", ".join(f"{a:x}: original {o['vars'][a - 1]:02x}, port {variables[a - 1]:02x}"
                          for a in differing[:8]))
        want = [masked(raw) for raw in o["records"]]
        have = [masked(raw) for raw in ported]
        v.check(len(want) == len(have) and want == have,
                f"the party's records at {what} agree (pointers masked)",
                f"{len(have)} records, the original {len(want)}")
    # Heal and Repair: eclplay's run stops at each, cited (the original is not compared here).
    for key, cite, name in STOPS:
        _, _, _, transcript, _ = port(work, patched, f"stop-{name.lower()}", key)
        v.check(cite in transcript,
                f"eclplay stops at the temple's {name} (not ported), citing {cite}",
                transcript.strip()[-200:])
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

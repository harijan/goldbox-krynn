#!/usr/bin/env python3
"""The Dark Queen of Krynn's block, map and call opcodes run in the original and
in eclplay (DARK-QUEEN.md's step 3e: dqk_ecl_flow; docs/checks.md, "Checks
against the original"): test blocks (tools/dqkecl.py) that replace block 1 of a
copy of DISK3\\ECL.GLB (block 2 too, for NEWECL), each started as dqk_ecl_text's
blocks are: the jumper's LOAD FILES and its SAVEs of the party's square to 3,3,
then a PRESS KEY. The original and eclplay (entered at the saved game's square)
are compared as dqk_ecl_text.py compares them: the screen at every wait, the
variables 0x100-0x17f byte for byte. The blocks:

- clear_nothing: CLEARMONSTERS (flow only: no monster is loaded, 0x0c is
  unported), then NOTHING with its six operands (CHECKPARTY's handler,
  5f07:0812: six operands loaded, nothing done) and NOTHING with none (5f07:1449:
  the opcode alone); the flow goes on after each;
- steps: CALL 2 (a step forward, 6c70:1186) twice, the square, facing, wall ahead
  and the square's byte read after each: two steps, each its own CALL word(2);
- redraw: CALL 1 (the view again, 6c70:1685) recomputes the saved square byte
  (DS:c9f4, from DS:c9f0-1, at 6c70:1694); a store to 0x34 or 0x35 changes nothing
  until a CALL 1 does. Squares 2,3 (0x81), 5,3 (0x80), and 5,2 after a store
  (still 0x80) and then a CALL 1 (0x81);
- others: a CALL whose operand is an immediate reads the high byte last set by an
  operand of type 1-3 at that index (dqk 5f07:0ac8; ae6a+i): at a block's start
  that is 0x80, the vectors' words, so CALL imm(1) first does nothing. CALL 4,
  CALL word 0x0101 and CALL 0 do nothing; a word with high byte 3 is the next
  CALL's operand, so CALL imm(1) after it does nothing either;
- walls: LOAD FILES 2 0 and 2 0xff: the screen at the wait. The second operand
  matters only through DS:ca03 (the wall set, saved as the header's +9), which no
  block here reads: test_adventure's test_dark_queen_wall_set checks it;
- music: MUSIC with an immediate and with a variable (5f07:1420: the song's
  number, no sound compared);
- frames: PICTURE 14 (PICA 14: the coral base, then its creature overlays), then
  CALL word(3) eight times (63ae:000c: the next frame drawn), and a PRINT whose wait
  shows the frame (a PRESS KEY would animate it in the original); eclplay does not
  report its frame, so the frame is compared through the screen;
- newecl: NEWECL 2 from block 1 into block 2 (a test block): the original's run
  rests at 0x8015, opcode 0, after block 2's EXIT, which vectors 1 and 2 run; that
  is the block's stop, with two waits (block 1's PRESS KEY and block 2's), and
  variable 0x16 (the block number) 2 at the end.

Not in this flow, and open (docs/games.md): the redraw after the picture (the
view at 6c70:1e5a-1e88, when mode 3 and DS:beea), and mode 3 (5f07:105e-107a,
not ported).

    python3 tools/dosbox/checks/dqk_ecl_flow.py [--workdir DIR] [--verbose] [--only NAME]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_ecl_text  # noqa: E402
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, imm, var, wvar, word  # noqa: E402

cokbox = checklib.cokbox
JUMP = dqk_ecl.JUMP
VARIABLES = dqk_ecl.VARIABLES
WINDOW = dqk_ecl_text.WINDOW  # 0x100-0x17f, the blocks' own variables
MASKS = dqk_ecl_text.MASKS
# The ECL's resting state with no block running: a stop there is a block whose
# keys were lost, or NEWECL's block 2 after its EXIT (see newecl).
RESTING = ("0x8000/0x0", "0x8015/0x0")

# The opcodes this flow writes (dqkecl's and dqk_ecl_text's tables have the rest).
dqkecl.OPCODES.update({"CLEARMONSTERS": 0x1C, "NOTHING6": 0x1E, "NOTHING": 0x29,
                       "MUSIC": 0x2C, "CALL": 0x2D, "NEWECL": 0x20})


def start_files(b, wall):
    """The jumper's LOAD FILES 2 (the map) and WALL, its SAVEs of the square to 3,3
    and a PRESS KEY (dqk_ecl_text's start, with the wall set given)."""
    b.op("LOAD_FILES", imm(2), imm(wall))
    b.op("SAVE", imm(3), var(0x34))
    b.op("SAVE", imm(3), var(0x35))
    b.op("PRESS_KEY")


def call(b, value):
    """CALL with a word operand: its value is the word, whatever high byte the
    operand before it left (see others for the immediate's)."""
    b.op("CALL", word(value))


def clear_nothing():
    b = Block()
    dqk_ecl_text.start(b)
    b.op("SAVE", imm(0x21), var(0x100))
    b.op("CLEARMONSTERS")
    b.op("NOTHING6", imm(1), var(0x100), word(0x1234), wvar(0x101), imm(2), var(0x34))
    b.op("SAVE", imm(0x33), var(0x102))
    b.op("NOTHING")
    b.op("SAVE", imm(0x44), var(0x103))
    b.op("PRESS_KEY")
    b.op("EXIT")
    return {1: b}, 1, {0x100: 0x21, 0x101: 0, 0x102: 0x33, 0x103: 0x44}


def steps():
    """Two steps (CALL 2 each, a word): the square, facing and wall ahead read
    after each, and the square's byte."""
    b = Block()
    dqk_ecl_text.start(b)
    for base in (0x100, 0x105):
        call(b, 2)
        b.op("SAVE", var(0x34), var(base))
        b.op("SAVE", var(0x35), var(base + 1))
        b.op("SAVE", var(0x23), var(base + 2))   # the facing
        b.op("SAVE", var(0x32), var(base + 3))   # the wall ahead
        b.op("SAVE", var(0x2D), var(base + 4))   # the square's byte
    b.op("PRESS_KEY")
    b.op("EXIT")
    return {1: b}, 1, {}


def redraw():
    """CALL 1 recomputes the saved square byte (6c70:1685's 1694, from DS:c9f0-1),
    and a store to 0x34 or 0x35 changes nothing until then: 2,3 (0x81), 5,3 (0x80),
    then 5,2 (0x81) after a store that was not recomputed (0x80 still). Read after
    each."""
    b = Block()
    dqk_ecl_text.start(b)
    b.op("SAVE", imm(2), var(0x34))
    call(b, 1)
    b.op("SAVE", var(0x2D), var(0x100))      # 0x81
    b.op("SAVE", imm(5), var(0x34))
    call(b, 1)
    b.op("SAVE", var(0x2D), var(0x101))      # 0x80
    b.op("SAVE", imm(2), var(0x35))
    b.op("SAVE", var(0x2D), var(0x102))      # no CALL: still 0x80
    call(b, 1)
    b.op("SAVE", var(0x2D), var(0x103))      # 0x81
    b.op("PRESS_KEY")
    b.op("EXIT")
    return {1: b}, 1, {0x100: 0x81, 0x101: 0x80, 0x102: 0x80, 0x103: 0x81}


def others():
    """CALL's other values do nothing (5f07:0aba's compare chain ends at 0b37). An
    immediate's high byte is the block's stale 0x80 (the vectors' words) until an
    operand of type 1-3 at its index sets it: CALL imm(1) first is 0x8001, nothing,
    and the square byte is still the block's (0). A word with high byte 3 (SAVE word
    0x0301) is then CALL imm(1)'s value, 0x0301: nothing, and 5,3 is not recomputed
    (0x81 from 2,3 stays)."""
    b = Block()
    dqk_ecl_text.start(b)
    b.op("CALL", imm(1))                     # 0x8001: nothing
    b.op("SAVE", var(0x2D), var(0x104))      # 0x00
    b.op("SAVE", imm(2), var(0x34))
    call(b, 1)                               # 2,3: 0x81
    b.op("SAVE", var(0x2D), var(0x100))      # 0x81
    b.op("SAVE", imm(5), var(0x34))          # 5,3 stored, not recomputed
    call(b, 4)
    b.op("SAVE", var(0x2D), var(0x101))      # 0x81
    call(b, 0x0101)
    b.op("SAVE", var(0x2D), var(0x102))      # 0x81
    call(b, 0)
    b.op("SAVE", var(0x2D), var(0x103))      # 0x81
    b.op("SAVE", word(0x0301), var(0x110))   # its low byte 1 stored; high byte 3 kept
    b.op("CALL", imm(1))                     # 0x0301: nothing
    b.op("SAVE", var(0x2D), var(0x105))      # 0x81
    b.op("PRESS_KEY")
    b.op("EXIT")
    return {1: b}, 1, {0x104: 0x00, 0x100: 0x81, 0x101: 0x81, 0x102: 0x81, 0x103: 0x81,
                       0x105: 0x81, 0x110: 0x01}


def walls_zero():
    b = Block()
    start_files(b, 0)
    b.op("EXIT")
    return {1: b}, 1, {}


def walls_none():
    b = Block()
    start_files(b, 0xFF)
    b.op("EXIT")
    return {1: b}, 1, {}


def music():
    b = Block()
    dqk_ecl_text.start(b)
    b.op("SAVE", imm(0x44), var(0x100))
    b.op("MUSIC", imm(3))
    b.op("MUSIC", var(0x100))
    b.op("SAVE", imm(0x55), var(0x101))
    b.op("PRESS_KEY")
    b.op("EXIT")
    return {1: b}, 1, {0x100: 0x44, 0x101: 0x55}


def frames():
    """PICTURE 14 (a small picture with frames), then eight CALL 3s (the picture's
    next frame each, past the loop's last back to its first), then a text whose
    wait shows the frame: a PRESS KEY would animate it in the original (1bdb:1008),
    and the block's EXIT redraws the view. eclplay reports no frame, so the frame
    is compared through the screen at that wait."""
    b = Block()
    dqk_ecl_text.start(b)
    b.op("PICTURE", imm(14))
    for _ in range(8):
        call(b, 3)
    b.op("PRINTCLEAR", dqk_ecl_text.text(dqk_ecl_text.LONG))
    b.op("PRINT", dqk_ecl_text.text(dqk_ecl_text.LONG))
    b.op("EXIT")
    return {1: b}, 1, {}


def newecl():
    """NEWECL 2 from block 1 into block 2: SAVE [16] reads 1 in block 2's body and
    2 after its run (the new block number; where in the entry it is stored is not
    traced here). Block 1's SAVE after NEWECL does not run."""
    b1 = Block()
    dqk_ecl_text.start(b1)
    b1.op("SAVE", imm(0x11), var(0x100))
    b1.op("NEWECL", imm(2))
    b1.op("SAVE", imm(0x22), var(0x101))
    b1.op("PRESS_KEY")
    b1.op("EXIT")
    b2 = Block()
    b2.op("SAVE", var(0x16), var(0x102))
    b2.op("SAVE", imm(0x33), var(0x103))
    b2.op("PRESS_KEY")
    b2.op("SAVE", var(0x16), var(0x104))
    b2.op("EXIT")
    b2.stop = (0x8015, 0)  # the resting state after its EXIT (vectors 1 and 2 run it)
    b2.waits = 2           # block 1's PRESS KEY, block 2's
    return {1: b1, 2: b2}, 2, {0x100: 0x11, 0x101: 0, 0x102: 1, 0x103: 0x33, 0x104: 1,
                                0x16: 2}


BLOCKS = {"clear_nothing": clear_nothing, "steps": steps, "redraw": redraw, "others": others,
          "walls_zero": walls_zero, "walls_none": walls_none, "music": music,
          "frames": frames, "newecl": newecl}


def lost_keys(trail):
    """True where the original's run never got past the resting state: its keys were
    lost before its block started (retried once)."""
    return bool(trail) and all(state in RESTING for state in trail)


def run(verdict, work, name, glb, blocks, last, expect, verbose):
    """One flow in the original and in eclplay: BLOCKS (index: block) replace
    the ECL.GLB's blocks, LAST is the block whose EXIT ends the run."""
    patched = glb
    for index, block in blocks.items():
        patched = hlib.replace(patched, index, dqkecl.assemble(block))
    for attempt in (1, 2):
        try:
            screens, names, _, end, after, lamps, square = dqk_ecl_text.original(
                work, name, blocks[last], patched, verbose)
            break
        except dqk_ecl_text.Unfinished as err:
            if attempt == 2 or not lost_keys(err.trail):
                raise
            print(f"  retry: {name}: the original's keys were lost ({err})", flush=True)
    for i, o in enumerate(screens):
        o.save_png(work / f"original-{name}-{i + 1}.png")
    stop = dqk_ecl_text.stop_of(blocks[last])
    verdict.check(end == stop, f"{name}: the original's ECL stopped at the last block's EXIT",
                  f"pc {end[0]:#x}, opcode {end[1]:#x}, not {stop[0]:#x}, {stop[1]:#x}")
    waits = getattr(blocks[last], "waits", None)
    if waits is not None:
        verdict.check(len(screens) == waits, f"{name}: the original waits {waits} times",
                      f"{len(screens)} waits")
    mine, _, port_vars, port_last = dqk_ecl_text.port(work, name, patched, lamps,
                                                               square, len(screens))
    verdict.check(len(mine) == len(screens), f"{name}: eclplay waits where the original does "
                  f"({len(screens)} waits)", f"{len(mine)} waits; {port_last}")
    for i, (o, p) in enumerate(zip(screens, mine)):
        verdict.screen(f"{name}: wait {i + 1} ({names[i]})", o, p, masks=MASKS, work=work)
    if len(port_vars) != VARIABLES:
        verdict.check(False, f"{name}: eclplay saved the variables", port_last)
        return
    differing = [a for a in WINDOW if after[a - 1] != port_vars[a - 1]]
    verdict.check(not differing, f"{name}: the variables 0x100-0x17f agree",
                  ", ".join(f"{a:x}: original {after[a - 1]:02x}, port {port_vars[a - 1]:02x}"
                            for a in differing[:8]))
    for side, vars_ in (("eclplay", port_vars), ("the original", after)):
        wrong = [a for a, v in sorted(expect.items()) if vars_[a - 1] != v]
        verdict.check(not wrong, f"{name}: {side} leaves the values the block computes",
                      ", ".join(f"{a:x}: {vars_[a - 1]:x} not {expect[a]:x}" for a in wrong[:8]))
    if verbose:
        print("  vars 0x100-0x104:",
              " ".join(f"{after[a - 1]:02x}" for a in range(0x100, 0x105)), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--only", metavar="NAME", help="run one flow (" + ", ".join(BLOCKS) + ")")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_ecl_flow")
    verdict = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    for name, make in BLOCKS.items():
        if opts.only and name != opts.only:
            continue
        blocks, last, expect = make()
        run(verdict, work, name, glb, blocks, last, expect, opts.verbose)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

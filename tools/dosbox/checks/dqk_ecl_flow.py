#!/usr/bin/env python3
"""The Dark Queen of Krynn's block, map and call opcodes run in the original and
in eclplay (DARK-QUEEN.md's step 3e: dqk_ecl_flow; docs/checks.md, "Checks
against the original"): test blocks (tools/dqkecl.py) that replace block 1 of a
copy of DISK3\\ECL.GLB, each started as dqk_ecl_text's blocks are: the jumper's
LOAD FILES and its SAVEs of the party's square to 3,3, then a PRESS KEY. The
original and eclplay (entered at the saved game's square) are compared as
dqk_ecl_text.py compares them: the screen at every wait, and the variables
0x100-0x17f byte for byte. The blocks:

- clear_nothing: CLEARMONSTERS, then NOTHING with its six operands (CHECKPARTY's
  handler, 5f07:0812: six operands loaded, nothing done) and NOTHING with none
  (5f07:1449: the opcode alone); the flow goes on after each;
- steps: CALL 2 (a step forward, 6c70:1186), twice; the square, facing, wall ahead
  and party's square read after each;
- redraw: CALL 1 (the view again, 6c70:1685) recomputes the saved square byte
  (DS:c9f4, from DS:c9f0-1, at 6c70:1694) after a store to 0x34 or 0x35, and not
  before it;
- others: CALL 4, CALL 0x0101 (a word), CALL 0 do nothing, and a word store with a
  high byte stands in the address, so a CALL 1 after it does nothing;
- walls: LOAD FILES 2 0 and 2 0xff (the wall set's id, which the original stores at
  DS:ca03, 6c70:1e57): the screen at the wait;
- music: MUSIC with an immediate and with a variable (5f07:1420: the song's
  number, no sound compared).

Not in this flow, and open (see docs/games.md): PICTURE 14 (a small picture with
frames) is a different picture in the original, and its frames do not advance in
eclplay; NEWECL 2 from block 1 runs in eclplay (block 2 reads the old block, 1,
and block 1's SAVE after NEWECL does not run), but the original's run does not
reach a block's wait.

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
END_MASKS = dqk_ecl_text.END_MASKS

# The opcodes this flow writes (dqkecl's and dqk_ecl_text's tables have the rest).
dqkecl.OPCODES.update({"CLEARMONSTERS": 0x1C, "NOTHING6": 0x1E, "NOTHING": 0x29,
                       "MUSIC": 0x2C, "CALL": 0x2D})


def start_files(b, wall):
    """The jumper's LOAD FILES 2 (the map) and WALL, its SAVEs of the square to 3,3
    and a PRESS KEY (dqk_ecl_text's start, with the wall set given)."""
    b.op("LOAD_FILES", imm(2), imm(wall))
    b.op("SAVE", imm(3), var(0x34))
    b.op("SAVE", imm(3), var(0x35))
    b.op("PRESS_KEY")


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
    b = Block()
    dqk_ecl_text.start(b)
    for call, base in ((2, 0x100), (2, 0x105)):
        b.op("CALL", imm(call))
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
    and a store to 0x34 or 0x35 changes nothing until then: squares 2,3 (0x81),
    5,3 (0x80) and 5,2 (0x81), as eclplay's probe reads them. Read after each."""
    b = Block()
    dqk_ecl_text.start(b)
    b.op("SAVE", imm(2), var(0x34))
    b.op("CALL", imm(1))
    b.op("SAVE", var(0x2D), var(0x100))      # 0x81
    b.op("SAVE", imm(5), var(0x34))
    b.op("CALL", imm(1))
    b.op("SAVE", var(0x2D), var(0x101))      # 0x80
    b.op("SAVE", imm(2), var(0x35))
    b.op("SAVE", var(0x2D), var(0x102))      # no CALL: still 0x80
    b.op("CALL", imm(1))
    b.op("SAVE", var(0x2D), var(0x103))      # 0x81
    b.op("PRESS_KEY")
    b.op("EXIT")
    return {1: b}, 1, {0x101: 0x80, 0x102: 0x80, 0x103: 0x81}


def others():
    """CALL's other values do nothing (5f07:0aba's compare chain ends at 0b37): a
    CALL 4, a CALL with the word 0x0101 and CALL 0 leave the square byte alone, and
    a word whose high byte is not 0 (SAVE word 0x0301) does not make CALL 1 another
    value: CALL 1 after it still recomputes (0x80 at 5,3)."""
    b = Block()
    dqk_ecl_text.start(b)
    b.op("SAVE", imm(2), var(0x34))
    b.op("CALL", imm(1))                     # 2,3: 0x81
    b.op("SAVE", imm(5), var(0x34))          # 5,3 stored, not recomputed
    b.op("CALL", imm(4))
    b.op("SAVE", var(0x2D), var(0x100))      # 0x81
    b.op("CALL", word(0x0101))
    b.op("SAVE", var(0x2D), var(0x101))      # 0x81
    b.op("CALL", imm(0))
    b.op("SAVE", var(0x2D), var(0x102))      # 0x81
    b.op("SAVE", word(0x0301), var(0x110))
    b.op("CALL", imm(1))                     # 5,3: 0x80
    b.op("SAVE", var(0x2D), var(0x103))
    b.op("PRESS_KEY")
    b.op("EXIT")
    return {1: b}, 1, {0x103: 0x00, 0x110: 0x01}  # CALL 1 after the word: not 1 (high byte 3)


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


BLOCKS = {"clear_nothing": clear_nothing, "steps": steps, "redraw": redraw, "others": others,
          "walls_zero": walls_zero, "walls_none": walls_none, "music": music}


def run(verdict, work, name, glb, blocks, last, expect, verbose):
    """One flow in the original and in eclplay: BLOCKS (index: block) replace
    the ECL.GLB's blocks, LAST is the block whose EXIT ends the run."""
    patched = glb
    for index, block in blocks.items():
        patched = hlib.replace(patched, index, dqkecl.assemble(block))
    # The original's run can lose its keys before the block starts (its ECL state
    # reads 0x8000 with no block running, and the Returns go to the menu): one retry.
    for attempt in (1, 2):
        try:
            screens, names, final, end, after, lamps, square = dqk_ecl_text.original(
                work, name, blocks[last], patched, verbose)
            break
        except cokbox.BoxError as err:
            if attempt == 2:
                raise
            print(f"  retry: {name}: the original's run failed ({err})", flush=True)
    for i, o in enumerate(screens):
        o.save_png(work / f"original-{name}-{i + 1}.png")
    stop = dqk_ecl_text.stop_of(blocks[last])
    verdict.check(end == stop, f"{name}: the original's ECL stopped at the last block's EXIT",
                  f"pc {end[0]:#x}, opcode {end[1]:#x}, not {stop[0]:#x}, {stop[1]:#x}")
    mine, mine_final, port_vars, port_last = dqk_ecl_text.port(work, name, patched, lamps,
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

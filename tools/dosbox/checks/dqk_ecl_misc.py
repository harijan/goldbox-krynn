#!/usr/bin/env python3
"""The Dark Queen of Krynn's ECL CLOCK run in the original and in eclplay
(DARK-QUEEN.md's step 3f: dqk_ecl_misc; docs/checks.md, "Checks against the
original"), with dqk_ecl.py's flow: a test block (tools/dqkecl.py) replaces
block 1 of a copy of DISK3\\ECL.GLB, and each side's run is checked as
dqk_ecl.py checks its blocks: the variables 0x100-0x17f and 0x29 agree, the
clock's variables 5-11 and the moons' 0x1fa-0x1ff are compared too (the block
changes them on purpose), nothing else changes, and the values the block
computes are the expected ones.

The block (ECL CLOCK, 5f07:0a35 -> 1396:044d and 1396:0368): the years at 99
and one more year (100 come off, not stay full); twelve of unit 0 (two of
10 carry into unit 1); and a day (24 of unit 3: unit 4 and each moon's days
and phases, 1396:00f8). Not here: the selected member's age, which needs the
party on both sides (eclplay has no --party for Dark Queen yet), and DUMP and
FIND SPECIAL, which need the same party too.

    python3 tools/dosbox/checks/dqk_ecl_misc.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, imm, var  # noqa: E402

JUMP = dqk_ecl.JUMP
# The variables compared: 0x100-0x17f and 0x29 (dqk_ecl's), the clock's and the moons'.
WINDOW = list(range(0x100, 0x180)) + [0x29] + list(range(5, 12)) + list(range(0x1FA, 0x200))


def clock():
    b, expect = Block(), {}
    b.op("PRESS_KEY")
    for a in WINDOW:  # the same starting values (the original's saved game's, eclplay's none)
        b.op("SAVE", imm(0), var(a))
    # One year: the years (variable 0x0b) at 99, one more makes 100, which comes off.
    b.op("SAVE", imm(99), var(0x0B))
    b.op("ECL_CLOCK", imm(1), imm(6))
    b.op("SAVE", var(0x0B), var(0x124))
    # Twelve of unit 0 (10 a unit): unit 0 is 2 and unit 1 is 1.
    b.op("ECL_CLOCK", imm(12), imm(0))
    b.op("SAVE", var(5), var(0x12C))
    b.op("SAVE", var(6), var(0x12D))
    # A day: 24 of unit 3, unit 4 one more, and the moons' days and phases.
    b.op("ECL_CLOCK", imm(24), imm(3))
    b.op("SAVE", var(8), var(0x12E))
    b.op("SAVE", var(9), var(0x12B))
    for i, (day, phase) in enumerate(((0x1FD, 0x1FA), (0x1FE, 0x1FB), (0x1FF, 0x1FC))):
        b.op("SAVE", var(day), var(0x125 + i))
        b.op("SAVE", var(phase), var(0x128 + i))
    b.op("EXIT")
    # 100 years come off a full year (1396:0368); unit 0 is 2 and unit 1 is 1 (10 a unit);
    # a day gives unit 4 one and each moon one day in its phase (limits 8, 1 and 6).
    expect.update({0x124: 0, 0x12C: 2, 0x12D: 1, 0x12E: 0, 0x12B: 1, 0x125: 1, 0x126: 1,
                   0x127: 1, 0x128: 0, 0x129: 0, 0x12A: 0})
    return b, expect


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_ecl_misc")
    verdict = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    # The baseline of the variables for eclplay: an empty block's end.
    empty = Block()
    empty.op("PRESS_KEY")
    base_vars, base_last, base_unread = dqk_ecl.port(
        work, "empty", hlib.replace(glb, JUMP, dqkecl.assemble(empty)))
    verdict.check(len(base_vars) == dqk_ecl.VARIABLES and "(done in block 1)" in base_last
                  and not base_unread, "eclplay runs an empty block to its EXIT (the baseline)",
                  base_last)
    block, expect = clock()
    dqk_ecl.run_block(verdict, work, "clock", glb, block, expect, "", base_vars, opts.verbose,
                      window=WINDOW)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""The Dark Queen of Krynn's item opcodes run in the original and in eclplay
(DARK-QUEEN.md's step 3h: dqk_ecl_items; docs/checks.md, "Checks against the
original"), with dqk_ecl_misc.py's party: a test block (tools/dqkecl.py)
replaces block 1 of a copy of DISK3\\ECL.GLB, and each side's run is checked as
dqk_ecl.py checks its blocks (the variables 0x100-0x17f and 0x29 agree between
the original and eclplay, nothing else changes, and the values the block
computes are the expected ones). Both sides have the party of saved game A
(ROLANDOF, TIMASALI, 'CINDER', BALTHIRN, ROARK'CR, WALKERKE, in that order).

Two blocks:

- items (FIND ITEM 6c70:2f3b, DESTROY ITEMS 6c70:30ec): each type the party
  holds, and one it does not (0x7e), found (= or <>, as the flags); then
  DESTROY ITEMS 0x80 | 0x44 takes only the items of type 0x44 with a bonus,
  so ROLANDOF's bonus-less one stays; DESTROY 0x44 takes it (readied, unreadied
  first); 0x80 | 0x1e takes the bonus items of type 0x1e in three members, and
  leaves the type 0x16 that the same members hold; DESTROY 0x25 and 0x12 take
  their readied items (unreadied, no power) in every member that holds them.
  The types found after each step are saved as variables.
- rob (ROB 6c70:28f8): ROARK'CR's money at 30 percent kept of 200 (the coin
  at 0x54b, an integer: 140; as Reals it could fall to 139), and the chance
  of 100 takes ROARK's items of weight up to 0x18 (the type 0x1c, which only
  ROARK holds, is then gone); the items above it (readied, unreadied first)
  and their chances are the original's d100 draws, compared as they fall.

WALKERKE's readied item of type 0x43 has a power (+0x38 0x80), whose routine
(86b8:16f7) is not ported: no block takes it, so neither side meets it.

    python3 tools/dosbox/checks/dqk_ecl_items.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_ecl_misc  # noqa: E402
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import OPCODES, Block, imm, var, wvar  # noqa: E402

JUMP = dqk_ecl.JUMP
# The window the blocks compare: 0x100-0x17f and 0x29 (dqk_ecl's WINDOW); each
# block zeroes it first (the prelude), so the starting values agree.
WINDOW = dqk_ecl.WINDOW
# The coins of the selected member as the ECL reads them (wide): the gold of ROARK'CR at
# 0x54d (the field table's coin at 0x14d, 0x400 the selected character's fields add),
# with the coins before and after it at 0x54b and 0x54f.
COINS = (0x54B, 0x54D, 0x54F)
# The types the party holds (ROLANDOF, TIMASALI, 'CINDER', BALTHIRN, ROARK'CR,
# WALKERKE) and one it does not.
HELD = [0x12, 0x13, 0x16, 0x1C, 0x1E, 0x1F, 0x23, 0x25, 0x29, 0x3D, 0x43, 0x44, 0x4B]
ABSENT = 0x7E
# DESTROY ITEMS' operand: the type, its high bit (0x80) for items with a bonus only.
MAGIC = 0x80


def prelude(b):
    for a in WINDOW:  # the same starting values (the original's saved game's, eclplay's none)
        b.op("SAVE", imm(0), var(a))


def find(b, expect, at, item_type, present):
    """FIND ITEM item_type: the flag saved as variable AT (1 if found); PRESENT None
    where the dice decide it (not expected, only compared)."""
    b.op("FIND_ITEM", imm(item_type))
    b.op("IF_EQUAL")
    b.op("SAVE", imm(1), var(at))
    if present is not None:
        expect[at] = 1 if present else 0


def items():
    b, expect = dqkecl.Block(), {}
    b.op("PRESS_KEY")
    prelude(b)
    # Every held type and one not held, before anything is destroyed.
    at = 0x100
    for t in HELD + [ABSENT]:
        find(b, expect, at, t, t != ABSENT)
        at += 1
    # 0x80 | 0x44: ROLANDOF's 0x44 has no bonus, so it stays (0x120).
    b.op("DESTROY_ITEMS", imm(MAGIC | 0x44))
    find(b, expect, 0x120, 0x44, True)
    # 0x44 with no flag: it goes (its readied item unreadied first).
    b.op("DESTROY_ITEMS", imm(0x44))
    find(b, expect, 0x121, 0x44, False)
    # 0x80 | 0x1e: the bonus items of 0x1e in TIMASALI, 'CINDER' and BALTHIRN go; 0x16,
    # which those hold too, stays.
    b.op("DESTROY_ITEMS", imm(MAGIC | 0x1E))
    find(b, expect, 0x122, 0x1E, False)
    find(b, expect, 0x123, 0x16, True)
    # The readied 0x25 and 0x12 (no power), in every member holding them.
    b.op("DESTROY_ITEMS", imm(0x25))
    find(b, expect, 0x124, 0x25, False)
    b.op("DESTROY_ITEMS", imm(0x12))
    find(b, expect, 0x125, 0x12, False)
    # The types nobody destroyed stay: ROARK's 0x13 and WALKERKE's 0x23.
    find(b, expect, 0x126, 0x13, True)
    find(b, expect, 0x127, 0x23, True)
    b.op("EXIT")
    return b, expect


def rob():
    b, expect = dqkecl.Block(), {}
    b.op("PRESS_KEY")
    prelude(b)
    # ROARK'CR selected, robbed: 30 percent of the money kept of 200 (the first coin),
    # every item at chance 100 (the d100 draws decide the rest).
    b.op("LOAD_CHARACTER", imm(4))
    b.op("ROB", imm(0), imm(30), imm(100))
    # The three coins, as words: 200 of gold at 0x54d (none at the others) and 140 after.
    for coin, at in zip(COINS, (0x130, 0x132, 0x134)):
        b.op("SAVE", wvar(coin), wvar(at))
    expect.update({0x130: 0, 0x131: 0, 0x132: 140, 0x133: 0, 0x134: 0, 0x135: 0})
    # The type 0x1c (weight 10, not readied) is taken at chance 100: gone.
    find(b, expect, 0x136, 0x1C, False)
    # ROARK's readied 0x13 and 0x1f (no power) may go, the d100 decides it.
    find(b, expect, 0x137, 0x13, None)
    find(b, expect, 0x138, 0x1F, None)
    b.op("EXIT")
    return b, expect


def run_block(verdict, work, name, glb, block, expect, base_vars, verbose):
    """One block run in the original (saved game A) and in eclplay (its party), judged as
    dqk_ecl_misc.run_block judges its blocks, over WINDOW."""
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
    port_vars, port_last, port_unread = dqk_ecl_misc.port_party(work, name, patched)
    verdict.check(not port_unread, f"{name}: eclplay leaves no keys unread",
                  "the block took fewer keys than were typed")
    wait, before, end, after = dqk_ecl.original(work, name, patched, verbose)
    verdict.check(wait == (block.address("entry") + 1, OPCODES["PRESS_KEY"]),
                  f"{name}: the original waits in the block's PRESS KEY",
                  f"pc {wait[0]:#x}, opcode {wait[1]:#x}")
    stop = dqk_ecl.stop_at(block)
    verdict.check(end == stop, f"{name}: the original's ECL stopped at the block's last "
                  "instruction", f"pc {end[0]:#x}, opcode {end[1]:#x}, not "
                  f"{stop[0]:#x}, {stop[1]:#x}")
    dqk_ecl.compare(verdict, name, (before, after), (base_vars, port_vars), port_last, expect,
                    WINDOW)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_ecl_items")
    verdict = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    # The baseline of the variables for eclplay: an empty block's end, with the party.
    empty = Block()
    empty.op("PRESS_KEY")
    base_vars, base_last, base_unread = dqk_ecl_misc.port_party(
        work, "empty", hlib.replace(glb, JUMP, dqkecl.assemble(empty)))
    verdict.check(len(base_vars) == dqk_ecl.VARIABLES and "(done in block 1)" in base_last
                  and not base_unread, "eclplay runs an empty block to its EXIT (the baseline)",
                  base_last)
    for name, make in (("items", items), ("rob", rob)):
        block, expect = make()
        run_block(verdict, work, name, glb, block, expect, base_vars, opts.verbose)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

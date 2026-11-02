#!/usr/bin/env python3
"""The Dark Queen of Krynn's ECL CLOCK, DUMP and FIND SPECIAL run in the
original and in eclplay (DARK-QUEEN.md's step 3f: dqk_ecl_misc; docs/checks.md,
"Checks against the original"), with dqk_ecl.py's flow: a test block
(tools/dqkecl.py) replaces block 1 of a copy of DISK3\\ECL.GLB, and each side's
run is checked as dqk_ecl.py checks its blocks: the variables compared (the
window below) agree, nothing else changes, and the values the block computes
are the expected ones. Both sides have the party of saved game A (the original
loads it with plta; eclplay gets the six .QCH files of DQKAssets\\SAVE, in its
order, with --character).

Two blocks:

- clock (5f07:0a35 -> 1396:044d, 0368, 019f): the years at 99 and one more
  year (100 come off, not stay full), the member's age (the word at +0x52,
  variables 0x04b1-0x04b2) before and after the year (its difference, 1); twelve
  of unit 0 (two of 10 carry into unit 1); a day (24 of unit 3: unit 4 and each
  moon's days and phases, 1396:00f8), and a second day, on which Lunitari (one
  day) turns to phase 1.
- party (FIND SPECIAL, 5f07:0cc5, and DUMP, 5f07:0c9b): each member in turn
  (LOAD CHARACTER) is asked FIND SPECIAL with each id of IDS (the shipped ids
  and those of the .QCH files), the flags saved as variables; then the middle
  member (index 2) is DUMPed, and the first, saving variable 0x20 (the party
  count) and the new selection's age after each.

    python3 tools/dosbox/checks/dqk_ecl_misc.py [--workdir DIR] [--verbose]

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
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import OPCODES, Block, imm, var, wvar  # noqa: E402

JUMP = dqk_ecl.JUMP
GAME = ["--game", "dark-queen"]
# Saved game A's members, in its order (the .QCH files beside it).
PARTY = ["ROLANDOF", "TIMASALI", "'CINDER'", "BALTHIRN", "ROARK'CR", "WALKERKE"]
# The effects each .QCH holds (ids): the shipped ids and those of the files (the
# effect lists dumped with the port's cok_character_read_file).
EFFECTS = {"ROLANDOF": [], "TIMASALI": [95], "'CINDER'": [95], "BALTHIRN": [95, 105],
           "ROARK'CR": [94, 26, 47], "WALKERKE": [92, 94]}
IDS = [20, 24, 50, 57, 61, 63, 119, 168, 92, 94, 95, 105, 26, 47]
# The age, the word at +0x52 of the selected member (variable 0x04b1, a word).
AGE = 0x04B1
# Cleared before each block: 0x100-0x17f, 0x29 (DIVIDE's remainder), the clock's
# variables 5-11 and the moons' 0x1fa-0x1ff. The party size (0x20) is not: the
# blocks start from the party's, which the flow compares.
PRELUDE = list(range(0x100, 0x180)) + [0x29] + list(range(5, 12)) + list(range(0x1FA, 0x200))
WINDOW = PRELUDE + [0x20]
# The party block's flags: member k's id j at 0x100 + k * len(IDS) + j.
FLAG_BASE = 0x100
DUMP_BASE = 0x160  # the party count, the age after DUMP of index 2, then of index 0


def flag(k, j):
    return FLAG_BASE + k * len(IDS) + j


def prelude(b):
    for a in PRELUDE:  # the same starting values (the original's saved game's, eclplay's none)
        b.op("SAVE", imm(0), var(a))


def clock():
    b, expect = Block(), {}
    b.op("PRESS_KEY")
    prelude(b)
    # The years (variable 0x0b) at 99, and the first member (ROLANDOF) selected for
    # the age, read before and after the year.
    b.op("SAVE", imm(99), var(0x0B))
    b.op("LOAD_CHARACTER", imm(0))
    b.op("SAVE", wvar(AGE), wvar(0x130))
    # One year: one more makes 100, which comes off (the member ages a year).
    b.op("ECL_CLOCK", imm(1), imm(6))
    b.op("SAVE", var(0x0B), var(0x124))
    b.op("SAVE", wvar(AGE), wvar(0x132))
    b.op("SUBTRACT", wvar(0x130), wvar(0x132), wvar(0x134))  # the age, one more: 1
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
    # A second day: Lunitari (one day in its phase) turns to phase 1.
    b.op("ECL_CLOCK", imm(24), imm(3))
    b.op("SAVE", var(9), var(0x140))
    for i, (day, phase) in enumerate(((0x1FD, 0x1FA), (0x1FE, 0x1FB), (0x1FF, 0x1FC))):
        b.op("SAVE", var(day), var(0x13A + i))
        b.op("SAVE", var(phase), var(0x13D + i))
    b.op("EXIT")
    # 100 years come off a full year (1396:0368); unit 0 is 2 and unit 1 is 1 (10 a unit);
    # a day gives unit 4 one and each moon one day in its phase (limits 8, 1 and 6);
    # the second day gives unit 4 two, the days 2, 0 and 2, and Lunitari phase 1.
    expect.update({0x124: 0, 0x12C: 2, 0x12D: 1, 0x12E: 0, 0x12B: 1, 0x125: 1, 0x126: 1,
                   0x127: 1, 0x128: 0, 0x129: 0, 0x12A: 0,
                   0x134: 1, 0x135: 0,
                   0x140: 2, 0x13A: 2, 0x13B: 0, 0x13C: 2, 0x13D: 0, 0x13E: 1, 0x13F: 0})
    return b, expect


def party():
    """FIND SPECIAL per member and id (the flags: expected from the .QCH files'
    effect lists), then DUMP of the middle member and of the first, with the party
    count and the new selection's age after each."""
    b, expect = Block(), {}
    b.op("PRESS_KEY")
    prelude(b)
    for k, name in enumerate(PARTY):
        b.op("LOAD_CHARACTER", imm(k))
        for j, effect in enumerate(IDS):
            b.op("FIND_SPECIAL", imm(effect))
            b.op("IF_EQUAL")
            b.op("SAVE", imm(1), var(flag(k, j)))
            expect[flag(k, j)] = 1 if effect in EFFECTS[name] else 0
    # DUMP the middle member (CINDER): the party is five, ROLAND first, and the
    # selection goes back one, to TIMASALI.
    b.op("LOAD_CHARACTER", imm(2))
    b.op("DUMP")
    b.op("SAVE", var(0x20), var(DUMP_BASE))
    b.op("SAVE", wvar(AGE), wvar(DUMP_BASE + 1))
    # DUMP the first (ROLAND): TIMASALI, now first, is selected, the party four.
    b.op("LOAD_CHARACTER", imm(0))
    b.op("DUMP")
    b.op("SAVE", var(0x20), var(DUMP_BASE + 3))
    b.op("SAVE", wvar(AGE), wvar(DUMP_BASE + 4))
    b.op("EXIT")
    expect.update({DUMP_BASE: 5, DUMP_BASE + 3: 4})
    return b, expect


def party_args():
    """eclplay's --character for each member of saved game A, in its order."""
    args = []
    for name in PARTY:
        args += ["--character", str(dqk_lib.ASSETS / "SAVE" / f"{name}.QCH")]
    return args


def port_party(work, name, patched):
    """eclplay's run of a block with saved game A's party (dqk_ecl.port with the
    party's --character options): its variables (--vars) and its output's last line,
    and whether it leaves keys unread."""
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
    vars_path = work / f"port-{name}.vars"
    result = subprocess.run([str(checklib.cokbox.ECLPLAY), *GAME, *party_args(), "--seed",
                             str(dqk_ecl.SEED), "--vars", str(vars_path), "--keys", "\\r",
                             str(assets), str(JUMP)],
                            capture_output=True, text=True, cwd=checklib.cokbox.ROOT, timeout=300)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    last = (result.stdout.strip().splitlines() or [""])[-1]
    unread = "(keys left unread" in result.stderr
    return (vars_path.read_bytes() if vars_path.exists() else b""), last, unread


def run_block(verdict, work, name, glb, block, expect, base_vars, verbose):
    """One block run in the original (with saved game A) and in eclplay (with its party),
    judged as dqk_ecl.run_block judges it, against BASE_VARS (the empty block's end)."""
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
    port_vars, port_last, port_unread = port_party(work, name, patched)
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
    work = checklib.workdir(opts.workdir, "dqk_ecl_misc")
    verdict = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    # The baseline of the variables for eclplay: an empty block's end, with the party.
    empty = Block()
    empty.op("PRESS_KEY")
    base_vars, base_last, base_unread = port_party(work, "empty",
                                                   hlib.replace(glb, JUMP, dqkecl.assemble(empty)))
    verdict.check(len(base_vars) == dqk_ecl.VARIABLES and "(done in block 1)" in base_last
                  and not base_unread, "eclplay runs an empty block to its EXIT (the baseline)",
                  base_last)
    for name, make in (("clock", clock), ("party", party)):
        block, expect = make()
        run_block(verdict, work, name, glb, block, expect, base_vars, opts.verbose)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

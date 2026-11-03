#!/usr/bin/env python3
"""The Dark Queen of Krynn's end of combat with no monsters, its start (DARK-QUEEN.md's step 4,
part 19a: dqk-end-of-combat; docs/checks.md, "Checks against the original": dqk_treasure).

A test block (tools/dqkecl.py) replaces block 1 of a copy of DISK3\\ECL.GLB, and the original
runs it from saved game A (dqk_fight's enter: plta, Begin Adventuring, the copy protection
answered), with a PRESS KEY before COMBAT:

    PRESS KEY; LOAD FILES 2 1 (as dqk_shops's temple block); PRESS KEY; COMBAT; EXIT

The original waits at the second PRESS KEY, before COMBAT: its 1 KB of variables and the party's
records are read there. COMBAT with no monsters, no vault and no temple wanted runs 75cc:135c.
That routine's start walks the weapons lost in combat (1297, empty here), clears the surprise
byte (var 0x2e), and calls 1093:0015, which eclplay stops at, cited (dqk 75cc:1093:0015). The
original is not run past COMBAT: its routine is loaded by COMBAT and no trap can be placed before
it runs (dqk_fight_lib.trap spins a routine that is already loaded), so the comparison is of the
state before COMBAT, with var 0x2e (cleared by 135c's start, after this state) left out.

- `start`: the variables and the party's records at the second PRESS KEY agree with eclplay's
  state at its stop, except var 0x2e; the stop is cited; the transcript has no other error.

    python3 tools/dosbox/checks/dqk_treasure.py [--workdir DIR] [--flows start] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_fight  # noqa: E402
import dqk_fight_lib as L  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_shops  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, imm  # noqa: E402

cokbox = checklib.cokbox
JUMP = dqk_ecl.JUMP
VARIABLES = dqk_ecl.VARIABLES
SURPRISE = 0x2e  # var 0x2e: 75cc:135c's start clears it (COK_VAR_SURPRISE in rules_dark_queen.c)
STOP = "dqk 75cc:1093:0015"  # the cited stop (rules_dark_queen.c's no_monsters_treasure_unported)
FLOWS = {"start": "the treasure's start (75cc:135c, before its overlay call)"}


def start_block():
    """Block 1: the entry PRESS KEY, the 3D view's files, a PRESS KEY before COMBAT, COMBAT, EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    b.op("LOAD_FILES", imm(2), imm(1))  # the jumper's (block 1, ECL 8025), as dqk_shops
    b.op("PRESS_KEY")
    b.op("COMBAT")
    b.op("EXIT")
    return b


def original(work, patched, verbose):
    """The original's state at the second PRESS KEY (before COMBAT): the variables, the party's
    records (raw), and whether the game waits for a key there."""
    with cokbox.Box(workdir=work / "box-start", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        dqk_fight.enter(box)
        box.keys("\\r")  # the entry PRESS KEY: the files load, the block waits before COMBAT
        box.settled(tolerance=64)
        return dict(vars=dqk_fight.block_vars(box),
                    records=[raw for _, raw in L.records(box)], waiting=box.waiting())


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows", default="start", help="which flows: start")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_treasure")
    v = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(start_block()))
    for flow in opts.flows.split(","):
        if flow != "start":
            parser.error(f"flow {flow}: one of {', '.join(FLOWS)}")
        orig = original(work, patched, opts.verbose)
        v.check(orig["waiting"], "the original waits at the PRESS KEY before COMBAT")
        entry = dqk_shops.entry_file(work, "start", None)
        variables, _, ported, transcript, _ = dqk_shops.port(
            work, "start", patched, entry, "\\r")
        v.check(STOP in transcript and "error:" in transcript,
                f"eclplay stops at the treasure's overlay call, cited ({STOP})",
                transcript.strip()[-200:])
        v.check(len(variables) == VARIABLES, "eclplay runs the block to the stop")
        differing = [a for a in range(1, VARIABLES + 1)
                     if a != SURPRISE and orig["vars"][a - 1] != variables[a - 1]]
        v.check(not differing, "the variables before COMBAT agree with eclplay's stop "
                "(1 KB, var 0x2e left out)",
                ", ".join(f"{a:x}: original {orig['vars'][a - 1]:02x}, port "
                          f"{variables[a - 1]:02x}" for a in differing[:8]))
        want = [dqk_shops.masked(raw) for raw in orig["records"]]
        have = [dqk_shops.masked(raw) for raw in ported]
        v.check(len(want) == len(have) and want == have,
                "the party's records before COMBAT agree (pointers masked)",
                f"{len(have)} records, the original {len(want)}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

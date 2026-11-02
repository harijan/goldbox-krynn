#!/usr/bin/env python3
"""The Dark Queen of Krynn's fight, from the first state of a battle, in the
original (DARK-QUEEN.md's step 4, part 1a: the fight fixture; docs/checks.md,
"Checks against the original": dqk_fight). Flow `setup` (--flows setup):

A test block (tools/dqkecl.py's fight) replaces block 1 of a copy of
DISK3\\ECL.GLB, and the original runs it from saved game A (plta, Begin
Adventuring, the copy protection answered, as dqk_ecl does) with the seed
DS:ca0e set at the block's first PRESS KEY:

    CLEARMONSTERS; LOAD MONSTER 64 (SHARKMAN) x2; PRESS KEY (labelled
    'loaded'); SETUP MONSTERS, APPROACH, COMBAT; EXIT

The original's state (tools/dosbox/checks/dqk_fight_lib.py: the party's list
with the monsters appended, each record's combat record, the combatants, the
battlefield and its occupants, the turn, the sides, the round, the mode, the
monsters' flag and count, RandSeed) is read at the first PRESS KEY (before),
at the second (after LOAD MONSTER), and at the first turn: the battle's turn
routine 5045:078b made to spin where it starts (dqk_fight_lib.trap), as the
overlay loads. Checked:

- the original waits in each PRESS KEY, the first where the block starts, the
  second after LOAD MONSTER;
- LOAD MONSTER only registers the monsters (the flag set, one icon slot more,
  no record appended: COMBAT's monsters branch appends them, 6c70:2d82);
- at the first turn the two SHARKMEN are on the list, every record has its
  combat record, the monsters are combatants on the map, the turn is 1, the mode
  5, both sides can act, and the readings agree (dqk_fight_lib.consistent: the
  occupants hold each combatant's index at its cell);
- eclplay --game dark-queen, with the saved game's six members (--character),
  runs the same block and stops at LOAD MONSTER (opcode 0x0b, not yet ported).

The original's states go to build/dqk-fight/setup.json (later parts compare the
port's state with them). About a minute.

    python3 tools/dosbox/checks/dqk_fight.py [--workdir DIR] [--verbose]
        [--flows setup]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_ecl_party  # noqa: E402
import dqk_fight_lib  # noqa: E402
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import OPCODES, Block  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
JUMP = dqk_ecl.JUMP
SEED = dqk_ecl.SEED
# The fight: two SHARKMEN (MONCHA 64, a plain melee monster of size 1; its
# icon is CPIC 64, the monster's id, as block 7 of the shipped ECL writes it).
# SETUP MONSTERS' sprite is SPRIT.TLB's record 82, the encounter's, as block 7
# has it (a monster's id is no sprite: SPRIT has no 64, and the game's DISK READ
# ERROR is what a missing record shows).
MONSTER, NAME, COUNT = 64, "SHARKMAN", 2
SPRITE = 82
# The first turn's routine (5045:078b, a turn starts) and the keys a new
# battle's text may wait for (Return).
TURN = (0x5045, 0x078B)
RETURN = 0x1C0D
STATE = ROOT / "build" / "dqk-fight" / "setup.json"


def fight_block():
    """Block 1 of the test: the entry PRESS KEY, the fight's LOAD MONSTER, a
    PRESS KEY after the loads (labelled 'loaded'), then SETUP MONSTERS,
    APPROACH, COMBAT and the block's EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    dqkecl.fight(b, [(MONSTER, COUNT)], sprite=SPRITE, pause=True)
    b.op("EXIT")
    return b


def cstr(raw):
    return raw.split(b"\0", 1)[0].decode("latin-1")


def original(work, patched, verbose, block):
    """The original's states: (before, after LOAD MONSTER, the first turn), each
    dqk_fight_lib.state, and the (pc, opcode) where it waits at the entry and
    after the loads."""
    with cokbox.Box(workdir=work / "box-setup", game="dark-queen", verbose=verbose,
                    timeout=900, files={"DISK3/ECL.GLB": patched}) as box:
        dqk_ecl.enter_block(box)
        entry = dqk_lib.ecl_state(box)
        before = dqk_fight_lib.state(box)
        box.set_seed(SEED)
        box.keys("\\r")                      # LOAD MONSTER runs; the block waits in the loaded PRESS KEY
        box.settled(tolerance=64)
        loaded = dqk_lib.ecl_state(box)
        after = dqk_fight_lib.state(box)
        box.keys("\\r")                      # SETUP MONSTERS, APPROACH, COMBAT: the battle begins
        handle = dqk_fight_lib.trap(box, *TURN, seconds=180)
        box.settled(tolerance=64)            # the turn routine spins: the state holds
        turn = dqk_fight_lib.state(box)
        return ((entry["pc"], entry["opcode"]), (loaded["pc"], loaded["opcode"]),
                len(handle)), before, after, turn


def port(work, patched):
    """eclplay's run of the block (saved game's six members as the port's
    --character records): its last output line."""
    _, last, _ = dqk_ecl.port(work, "fight", patched, extra=dqk_ecl_party.party_args())
    return last


def setup(v, work, opts):
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    block = fight_block()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
    (entry, loaded, traps), before, after, turn = original(work, patched, opts.verbose, block)
    v.check(entry == (block.address("entry") + 1, OPCODES["PRESS_KEY"]),
            "the original waits in the block's entry PRESS KEY",
            f"pc {entry[0]:#x}, opcode {entry[1]:#x}")
    v.check(loaded == (block.address("loaded") + 1, OPCODES["PRESS_KEY"]),
            "the original waits in the PRESS KEY after LOAD MONSTER",
            f"pc {loaded[0]:#x}, opcode {loaded[1]:#x}")
    v.check(traps >= 1, "the turn routine (5045:078b) is trapped in the overlay buffer",
            f"{traps} copies")
    v.check(len(after["records"]) == len(before["records"]),
            "LOAD MONSTER appends no record (COMBAT does)",
            f"{len(before['records'])} before, {len(after['records'])} after")
    v.check(after["monsters_loaded"] == 1 and after["icon_slot"] == before["icon_slot"] + 1,
            "LOAD MONSTER sets the monsters' flag and registers one icon slot",
            f"flag {after['monsters_loaded']}, slot {before['icon_slot']} -> "
            f"{after['icon_slot']}")
    added = turn["records"][len(before["records"]):]
    names = [cstr(bytes.fromhex(r["raw"])[0x68:0x78]) for r in added]
    v.check(names.count(NAME) >= COUNT, f"COMBAT appends {COUNT} {NAME} records",
            ", ".join(names))
    v.check(turn["turn"] == 1, "the first turn is combatant 1", f"turn {turn['turn']}")
    v.check(turn["mode"] == 5, "the battle's mode is 5 (5045:059d)", f"mode {turn['mode']}")
    v.check(all(turn["sides"]), "both sides can act at the first turn", f"sides {turn['sides']}")
    v.check(all(r["combat"] is not None for r in turn["records"]),
            "every record has its combat record at the first turn")
    monsters = {r["at"] for r in added if cstr(bytes.fromhex(r["raw"])[0x68:0x78]) == NAME}
    placed = [c for c in turn["combatants"] if c["record"] in monsters]
    v.check(len(placed) == COUNT and all(c["size"] for c in placed),
            "both monsters are combatants on the map at the first turn",
            ", ".join(f"{c['n']}: {c['x']},{c['y']} size {c['size']}" for c in placed))
    problems = dqk_fight_lib.consistent(turn)
    v.check(not problems, "the readings agree (the list, the combatants, the occupants)",
            "; ".join(problems[:4]))
    port_last = port(work, patched)
    v.check(port_last.startswith("error: not yet ported") and "LOAD MONSTER" in port_last,
            "eclplay stops at LOAD MONSTER (not yet ported)", port_last)
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps({"block": "fight", "monster": MONSTER, "count": COUNT,
                                 "before": before, "after_load": after, "first_turn": turn,
                                 "port_stop": port_last}, indent=1) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows", default="setup", help="which flows: setup")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_fight")
    v = checklib.Verdict()
    flows = opts.flows.split(",")
    if "setup" in flows:
        setup(v, work, opts)
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

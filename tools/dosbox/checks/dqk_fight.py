#!/usr/bin/env python3
"""The Dark Queen of Krynn's fight, from the first state of a battle, in the
original (DARK-QUEEN.md's step 4, part 1a: the fight fixture; docs/checks.md,
"Checks against the original": dqk_fight). Flow `setup` (--flows setup):

A test block (tools/dqkecl.py's fight) replaces block 1 of a copy of
DISK3\\ECL.GLB, and the original runs it from saved game A (plta, Begin
Adventuring, the copy protection answered, as dqk_ecl does) with the seed
DS:ca0e set after the block's entry PRESS KEY, and the sound off (DQK.CFG's
sound word is none: dqk_fight_lib.files):

    LOAD FILES 4 4; CLEARMONSTERS; PRESS KEY (labelled 'monsters');
    LOAD MONSTER 64 (SHARKMAN) x2; PRESS KEY (labelled 'loaded');
    SETUP MONSTERS, APPROACH, COMBAT; EXIT

The original's state (tools/dosbox/checks/dqk_fight_lib.py: the party's list
with the monsters appended, each record's combat record, the combatants, the
battlefield and its occupants, the turn, the sides, the round, the mode, the
monsters' flag and count, RandSeed) is read at the entry PRESS KEY (before), at
the 'loaded' one (after LOAD MONSTER) and at the first turn: the battle's turn
routine 5045:078b made to spin where it starts (dqk_fight_lib.trap), as the
overlay loads; the spin is checked to hold, and then removed to see the game go
on. The block's variables (1 KB, through the far pointer at DS:b20f) are read at
the entry and at the 'monsters' PRESS KEY; eclplay's must equal the latter.

Checked:

- the original waits in each PRESS KEY (entry, monsters, loaded);
- LOAD MONSTER only registers the monsters (the flag set, one icon slot more,
  no record appended: COMBAT's monsters branch appends them, 6c70:2d82);
- at the first turn the monsters appended are exactly the two SHARKMEN (the
  ten GORGONs of 6c70:170e are dropped by placement: c6f1 counts 12, ad6b 10),
  every record has its combat record, the monsters are combatants on the map,
  the turn is 1, the mode 5, both sides can act, the readings agree
  (dqk_fight_lib.consistent), and the RandSeed is the first turn's (11 draws);
- the spin holds (EB FE in place, the keyboard polls still over a second), and
  untrapped the game goes on (the polls move, the routine's bytes are back);
- eclplay --game dark-queen --load --play, from a copy of saved game A (its six
  members, its square, its locals kept as a Load keeps them), enters the block as
  Begin Adventuring does and has the same 1 KB of variables at the 'monsters' PRESS
  KEY as the original (PORT_GAPS below: none);
- eclplay waits at the 'monsters' PRESS KEY (out of keys there), the same pc as the
  original's wait.

Flow `load` (step 4, part 5): LOAD FILES, CLEARMONSTERS, LOAD MONSTER 64 x2 and 49 x1,
then COMBAT (no SETUP MONSTERS or APPROACH). Where COMBAT calls the battle (5f07:08f0,
after the appends: 19 records, c6f1 13, ad6b 11) the original's party list is read and
eclplay's, started as setup's (--load --play) with --records, must agree: the monsters'
records byte for byte (pointers masked), the party's too (PARTY_GAPS: none). Run twice,
with difficulty 3 and with 1 (var 0x27 set at the entry in the original, and by --set
after the load in eclplay): the hostile monsters' hit points halve for difficulty 1.

The original's states go to build/dqk-fight/setup.json, with the heap pointers
masked (dqk_fight_lib.portable): later parts compare the port's state with them.
About 20 seconds.

    python3 tools/dosbox/checks/dqk_fight.py [--workdir DIR] [--verbose]
        [--flows setup,load]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_fight_lib as L  # noqa: E402
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import OPCODES, Block, imm  # noqa: E402

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
# The monsters COMBAT appends: COUNT SHARKMEN (6c70:2d82) and the ten GORGONs
# 6c70:170e appends, which placement drops (c6f1 counts all twelve; the next icon
# slot, ad6b, is 10 at the first turn: the review's values).
MONSTERS_APPENDED, ICON_SLOTS_AT_TURN = 12, 10
# The first turn's routine (5045:078b, a turn starts) and the keys a new
# battle's text may wait for (Return).
TURN = (0x5045, 0x078B)
RETURN = 0x1C0D
# The variables at the 'monsters' PRESS KEY where eclplay differs from the
# original: none. Both are the block's variables, read through the far pointer at
# DS:b20f (dqk_ecl's). The block's entry has saved game A's variables plus 0x16 = 1
# (the jumper's), LOAD FILES 4 4 sets 0x13 and 0xc6 to 4 in both, and eclplay's
# --load --play gives the same 1 KB. An earlier read at DS:b210 took the far
# pointer's bytes for variables and showed gaps that were not there. The check
# asserts the exact set, so a new difference fails it.
PORT_GAPS = ()
SQUARE = 0xC9F0                # DS:c9f0-c9f2: x, y, facing (saved game A: 7, 13, 0)
STATE = ROOT / "build" / "dqk-fight" / "setup.json"


def fight_block():
    """Block 1 of the test: the entry PRESS KEY, the fight (dqkecl.fight: LOAD
    FILES, CLEARMONSTERS, a PRESS KEY labelled 'monsters', LOAD MONSTER, a PRESS
    KEY labelled 'loaded', SETUP MONSTERS, APPROACH, COMBAT), then the block's EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    dqkecl.fight(b, [(MONSTER, COUNT)], sprite=SPRITE, pause=True)
    b.op("EXIT")
    return b


def cstr(raw):
    return raw.split(b"\0", 1)[0].decode("latin-1")


def names_of(records):
    return [cstr(bytes.fromhex(r["raw"])[0x68:0x78]) for r in records]


def block_vars(box):
    """The block's 1 KB of variables (1 to 0x400): dqk_ecl's far pointer at DS:b20f
    (dqk_party's and dqk_ecl's read), not the bytes at DS:b210 that follow it."""
    return box.read(box.far(box.ds * 16 + dqk_ecl.DS_VARS) + 1, dqk_ecl.VARIABLES)


def original(work, patched, verbose):
    """The original's run: its PRESS KEYs' states (entry, monsters, loaded), the
    variables at the entry and at the monsters' PRESS KEY, the square at the
    entry, the states (before, after LOAD MONSTER, the first turn), the trap's
    handle, whether the spin holds (held), whether untrapped the game goes on
    (resumed), and the polls around it."""
    with cokbox.Box(workdir=work / "box-setup", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        dqk_ecl.enter_block(box)
        entry = dqk_lib.ecl_state(box)
        square = box.ds_read(SQUARE, 3).hex()
        entry_vars = block_vars(box)
        box.set_seed(SEED)
        before = L.state(box)
        # LOAD FILES and CLEARMONSTERS run; the block waits in the 'monsters' PRESS KEY
        box.keys("\\r")
        box.settled(tolerance=64)
        monsters = dqk_lib.ecl_state(box)
        monsters_vars = block_vars(box)
        # LOAD MONSTER runs; the block waits in the 'loaded' PRESS KEY
        box.keys("\\r")
        box.settled(tolerance=64)
        loaded = dqk_lib.ecl_state(box)
        after = L.state(box)
        box.keys("\\r")                      # SETUP MONSTERS, APPROACH, COMBAT: the battle begins
        handle = L.trap(box, *TURN, until=lambda: box.ds_byte(L.DS_TURN) == 1 and
                        box.ds_word(L.DS_MODE) == 5, seconds=180)
        box.settled(tolerance=64)            # the turn routine spins: the state holds
        held, polls, polls_held = L.held(box, handle)
        turn = L.state(box)
        L.untrap(box, handle)
        box.settle(1.0)
        resumed = box.hook("polls") != polls_held and \
            all(box.read(addr, 2) == orig for addr, orig in handle)
        return dict(entry=(entry["pc"], entry["opcode"]),
                    monsters=(monsters["pc"], monsters["opcode"]),
                    loaded=(loaded["pc"], loaded["opcode"]), traps=len(handle),
                    held=held, polls=(polls, polls_held), resumed=resumed, square=square,
                    entry_vars=entry_vars, monsters_vars=monsters_vars,
                    before=before, after=after, turn=turn)


def saved_entry(work):
    """Saved game A as the original's Begin Adventuring enters it: a copy in work
    (the SAVE folder's files beside it, so its characters are read as the
    original's are), with variable 0x16 (the block entered) set to block 1: the
    copy is what eclplay --load takes, with no block named (the load vector alone
    runs, as the original's entry does)."""
    folder = work / "fight-save"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for entry in (dqk_lib.ASSETS / "SAVE").iterdir():
        if entry.name != "SAVGAMA.QSV":
            (folder / entry.name).symlink_to(entry)
    data = bytearray((dqk_lib.ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
    data[0x16 - 1] = dqk_ecl.JUMP
    (folder / "SAVGAMA.QSV").write_bytes(bytes(data))
    return folder / "SAVGAMA.QSV"


def port(work, patched, name="fight", typed="", records=None, difficulty=None):
    """eclplay's run of the block as the original's Load then Begin Adventuring
    gives it: saved game A's copy (--load) and --play, which is Begin Adventuring's
    entry (without it the block is entered in load_saved's mode 0, not the mode the
    original's Begin leaves). No --set for the variables: the save gives them, and
    DIFFICULTY, if not None, is var 0x27 set after the load. TYPED are the keys after
    the entry's; with RECORDS, the party list written there (--records). Returns its
    variables where it stops (--vars) and its last output line."""
    extra = ["--load", str(saved_entry(work)), "--play"]
    if records is not None:
        extra += ["--records", str(records)]
    if difficulty is not None:
        extra += ["--set", f"27={difficulty:x}"]
    variables, last, _ = dqk_ecl.port(work, name, patched, typed=typed, extra=extra,
                                      block=None)
    return variables, last
    return variables, last


def setup(v, work, opts):
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    block = fight_block()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
    r = original(work, patched, opts.verbose)
    before, after, turn = r["before"], r["after"], r["turn"]
    v.check(r["entry"] == (block.address("entry") + 1, OPCODES["PRESS_KEY"]),
            "the original waits in the block's entry PRESS KEY",
            f"pc {r['entry'][0]:#x}, opcode {r['entry'][1]:#x}")
    v.check(r["monsters"] == (block.address("monsters") + 1, OPCODES["PRESS_KEY"]),
            "the original waits in the PRESS KEY after CLEARMONSTERS (before the loads)",
            f"pc {r['monsters'][0]:#x}, opcode {r['monsters'][1]:#x}")
    v.check(r["loaded"] == (block.address("loaded") + 1, OPCODES["PRESS_KEY"]),
            "the original waits in the PRESS KEY after LOAD MONSTER",
            f"pc {r['loaded'][0]:#x}, opcode {r['loaded'][1]:#x}")
    v.check(r["traps"] >= 1, "the turn routine (5045:078b) is trapped in the overlay buffer",
            f"{r['traps']} copies")
    v.check(r["held"], "the spin holds: EB FE in place, the keyboard polls still over a second",
            f"polls {r['polls'][0]} -> {r['polls'][1]}")
    v.check(r["resumed"], "untrapped, the game goes on (the polls move, the routine is back)")
    v.check(len(after["records"]) == len(before["records"]),
            "LOAD MONSTER appends no record (COMBAT does)",
            f"{len(before['records'])} before, {len(after['records'])} after")
    v.check(after["monsters_loaded"] == 1 and after["icon_slot"] == before["icon_slot"] + 1,
            "LOAD MONSTER sets the monsters' flag and registers one icon slot",
            f"flag {after['monsters_loaded']}, slot {before['icon_slot']} -> "
            f"{after['icon_slot']}")
    added = turn["records"][len(before["records"]):]
    names = names_of(added)
    v.check(names == [NAME] * COUNT, f"COMBAT appends exactly {COUNT} {NAME} records",
            ", ".join(names))
    v.check(turn["monster_count"] == MONSTERS_APPENDED and turn["icon_slot"] == ICON_SLOTS_AT_TURN,
            "COMBAT appended 12 monsters (the 2 SHARKMEN and 10 GORGONs of 170e: placement "
            "drops the GORGONs) and 10 icon slots", f"c6f1 {turn['monster_count']}, "
            f"ad6b {turn['icon_slot']}")
    v.check(turn["turn"] == 1, "the first turn is combatant 1", f"turn {turn['turn']}")
    v.check(turn["mode"] == 5, "the battle's mode is 5 (5045:059d)", f"mode {turn['mode']}")
    v.check(all(turn["sides"]), "both sides can act at the first turn", f"sides {turn['sides']}")
    v.check(all(rec["combat"] is not None for rec in turn["records"]),
            "every record has its combat record at the first turn")
    monsters_at = {rec["at"] for rec, name in zip(added, names) if name == NAME}
    placed = [c for c in turn["combatants"] if c["record"] in monsters_at]
    v.check(len(placed) == COUNT and all(c["size"] for c in placed),
            "both monsters are combatants on the map at the first turn",
            ", ".join(f"{c['n']}: {c['x']},{c['y']} size {c['size']}" for c in placed))
    problems = L.consistent(turn)
    v.check(not problems, "the readings agree (the list, the combatants, the occupants)",
            "; ".join(problems[:4]))
    draws = L.draws(SEED, turn["seed"])
    v.check(turn["seed"] == L.FIRST_TURN_SEED and draws == L.FIRST_TURN_DRAWS,
            "the first turn's RandSeed: 11 draws from the seed (sound off: the same every run)",
            f"seed {turn['seed']:#x}, {draws} draws")
    # No key after the entry's: eclplay waits at the 'monsters' PRESS KEY, where the
    # original does (LOAD MONSTER is ported, so the block goes no further unkeyed).
    port_vars, port_last = port(work, patched)
    diff = [a for a in range(1, dqk_ecl.VARIABLES + 1)
            if port_vars[a - 1] != r["monsters_vars"][a - 1]] if len(port_vars) == \
        dqk_ecl.VARIABLES else []
    v.check(len(port_vars) == dqk_ecl.VARIABLES and tuple(diff) == PORT_GAPS,
            "eclplay's variables at the 'monsters' PRESS KEY differ from the original's at "
            "exactly PORT_GAPS", f"differ: {' '.join(f'{a:#x}' for a in diff)}")
    wait = f"(out of keys in block 1 at {r['monsters'][0]:x})"
    v.check(port_last == wait, "eclplay waits at the 'monsters' PRESS KEY, where the original does",
            port_last)
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps({
        "block": "fight", "monster": MONSTER, "count": COUNT, "sound": "none (DQK.CFG 6)",
        "square": r["square"], "entry_vars": r["entry_vars"].hex(),
        "monsters_vars": r["monsters_vars"].hex(),
        "before": L.portable(before), "after_load": L.portable(after),
        "first_turn": L.portable(turn), "port_stop": port_last},
        indent=1) + "\n")


# The load flow (step 4, part 5): LOAD MONSTER only registers the monsters, and
# COMBAT's monsters branch appends them (6c70:2d82 per slot, then 170e's ten
# GORGONs). Two SHARKMEN (id and icon 64) and one MINOTAUR (49), then COMBAT
# with no SETUP MONSTERS or APPROACH (the original allows it after LOAD FILES).
# The party list is read where the battle's routine 5045:059d starts, the appends
# done and placement not yet. Run twice: with the entry's difficulty (3) and with
# difficulty 1 (hit points by (d + 1) / 4: halved for the hostile monsters).
LOADS = ((MONSTER, COUNT), (49, 1))
DIFFICULTIES = (None, 1)
# COMBAT's call of the battle (5f07:08f0, a lcall to 5045:059d through its stub):
# the state is read where it spins, after the appends and before the battle's
# routine. The trap is set while the block waits for its PRESS KEY, as COMBAT's
# code is resident in the ECL's overlay then: the battle starts at once after
# the key, faster than a trap set after it (the loads are not waited for).
CALL_BATTLE = (0x5F07, 0x08F0)
BATTLE_STOP = ("error: not yet ported: The Dark Queen of Krynn's battle "
               "(dqk 5045:059d)")
PARTY = 6                      # the party's records come first in the list
# The party's records as eclplay imports them from the saved game (--load) are the
# original's, byte for byte: none differs. With --character they differed for four
# members (the known spells at 0x159, the saving throws and thief skills at 0x8b and
# 0x93-0x97), which was the character import's, not the battle's. Kept as the exact
# set (empty): a new difference fails the check.
PARTY_GAPS = {}
# The appended monsters: each slot's copies (the SHARKMEN, the MINOTAUR) and the
# ten GORGONs of 6c70:170e, which placement drops later.
APPENDED = sum(count for _, count in LOADS) + 10
ICON_SLOT_AT_BATTLE = 8 + len(LOADS) + 1   # one per LOAD MONSTER, and 170e's
# A record's pointer fields, all masked: the next, effects and items (0-0xb),
# the readied items' slots (0xc-0x3f) and the combat record (0x40-0x43).
POINTER_BYTES = range(0, 0x44)
HIT_POINTS = 0x198             # a record's hit points (dqk_fight_lib's layout)


def load_block(loads):
    """Block 1 of the load test: the entry PRESS KEY, LOAD FILES 4 4, CLEARMONSTERS,
    a PRESS KEY labelled 'monsters', one LOAD MONSTER per (id, count) with the icon
    the id, COMBAT, and the block's EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    b.op("LOAD_FILES", imm(4), imm(4))
    b.op("CLEARMONSTERS")
    b.label("monsters")
    b.op("PRESS_KEY")
    for monster, count in loads:
        b.op("LOAD_MONSTER", imm(monster), imm(count), imm(monster))
    b.op("COMBAT")
    b.op("EXIT")
    return b


def load_original(work, patched, difficulty, verbose):
    """The original's run of the load block: where COMBAT calls the battle (trapped,
    not run on), the block's variables and the party list's records with the
    monsters' count and the next icon slot. DIFFICULTY, if not None, is var 0x27
    set at the entry (var a is the byte at the far pointer plus a)."""
    with cokbox.Box(workdir=work / f"box-load-{difficulty}", game="dark-queen",
                    verbose=verbose, timeout=900, files=L.files(patched)) as box:
        dqk_ecl.enter_block(box)
        if difficulty is not None:
            box.write(box.far(box.ds * 16 + dqk_ecl.DS_VARS) + 0x27, bytes([difficulty]))
        box.set_seed(SEED)
        box.keys("\\r")                 # LOAD FILES, CLEARMONSTERS; waits at 'monsters'
        box.settled(tolerance=64)
        handle = L.trap(box, *CALL_BATTLE, until=lambda: True, seconds=60)
        box.keys("\\r")                 # LOAD MONSTERs, COMBAT: the appends, the call
        for _ in range(180):             # the spin holds once COMBAT reaches it
            if L.held(box, handle)[0]:
                break
        else:
            raise cokbox.BoxError("COMBAT did not reach the battle's call (5f07:08f0)")
        records = L.records(box)
        return dict(variables=block_vars(box),
                    records=[raw.hex() for _, raw in records],
                    count=box.ds_byte(L.DS_MONSTER_COUNT),
                    icon_slot=box.ds_byte(L.DS_ICON_SLOT), traps=len(handle),
                    names=names_of([{"raw": raw.hex()} for _, raw in records]))


def masked(raw_hex):
    """A record's bytes with its pointer fields zeroed (the port has none)."""
    raw = bytearray.fromhex(raw_hex)
    for i in POINTER_BYTES:
        raw[i] = 0
    return bytes(raw)


def load_run(v, work, opts, difficulty):
    """One run of the load block: the original's (load_original), eclplay's from
    the same start (port, --records), and the checks against it. Returns the
    original's readings."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(load_block(LOADS)))
    tag = "" if difficulty is None else f" (difficulty {difficulty})"
    r = load_original(work, patched, difficulty, opts.verbose)
    v.check(r["traps"] >= 1, f"the battle's routine (5045:059d) is trapped{tag}",
            f"{r['traps']} copies")
    v.check(len(r["records"]) == PARTY + APPENDED and r["count"] == APPENDED and
            r["icon_slot"] == ICON_SLOT_AT_BATTLE,
            f"at the battle's start the list holds the party and {APPENDED} monsters, "
            f"c6f1 {APPENDED} and ad6b {ICON_SLOT_AT_BATTLE}{tag}",
            f"{len(r['records'])} records, c6f1 {r['count']}, ad6b {r['icon_slot']}: "
            + " ".join(r["names"][PARTY:]))
    records = work / f"port-load-{difficulty}.records"
    port_vars, port_last = port(work, patched, name=f"load{difficulty}", typed="\\r",
                                records=records, difficulty=difficulty)
    diff = [a for a in range(1, dqk_ecl.VARIABLES + 1)
            if port_vars[a - 1] != r["variables"][a - 1]] \
        if len(port_vars) == dqk_ecl.VARIABLES else []
    v.check(len(port_vars) == dqk_ecl.VARIABLES and tuple(diff) == PORT_GAPS,
            f"eclplay's variables at the battle's start agree with the original's "
            f"(PORT_GAPS){tag}", f"differ: {' '.join(f'{a:#x}' for a in diff)}")
    v.check(port_last == BATTLE_STOP, f"eclplay stops at the battle (not yet ported){tag}",
            port_last)
    data = records.read_bytes() if records.exists() else b""
    size = L.RECORD
    count, icon_slot = (data[-2], data[-1]) if len(data) >= 2 else (-1, -1)
    ported = [data[i:i + size].hex() for i in range(0, len(data) - 2, size)]
    v.check(len(data) >= 2 and len(ported) == len(r["records"]) and
            count == r["count"] and icon_slot == r["icon_slot"],
            f"eclplay's party list has the original's records, c6f1 and ad6b{tag}",
            f"{len(ported)} records, c6f1 {count}, ad6b {icon_slot}")
    # The records' bytes, pointers masked: the monsters' must agree, and the party's
    # must agree at PARTY_GAPS (the import's bytes, before any monster is looked at).
    where = {i: tuple(k for k in range(L.RECORD)
                      if masked(ported[i])[k] != masked(r["records"][i])[k])
             for i in range(min(len(ported), len(r["records"])))}
    where = {i: ks for i, ks in where.items() if ks}
    v.check(len(ported) == len(r["records"]) and
            all(not ks for i, ks in where.items() if i >= PARTY) and
            where == PARTY_GAPS,
            f"every monster's record agrees at the battle's start (pointers masked), and the "
            f"party's agree at PARTY_GAPS (none){tag}",
            "; ".join(f"{i}: " + " ".join(f"{k:#x}" for k in ks) for i, ks in sorted(where.items())))
    return r, ported


def load(v, work, opts):
    """The load flow: the appends at the battle's start, in two runs (the entry's
    difficulty, and difficulty 1), each checked against the original and eclplay;
    and the difficulty changes the hostile monsters' hit points (the three
    LOAD MONSTER records, their copies included) and no GORGON's."""
    runs = {d: load_run(v, work, opts, d) for d in DIFFICULTIES}
    base, low = runs[None][0]["records"], runs[1][0]["records"]
    monsters = range(PARTY, PARTY + APPENDED)
    hit = [i for i in monsters if masked(base[i]) != masked(low[i])]
    loaded = range(PARTY, PARTY + sum(count for _, count in LOADS))
    hp = [(bytes.fromhex(base[i])[HIT_POINTS], bytes.fromhex(low[i])[HIT_POINTS])
          for i in loaded]
    # (d + 1) / 4 of the hit points, as bytes, at least 1 (6c70:29fc's 2a54-2ac6).
    v.check(hit == list(loaded) and all(b == max(1, a * 2 // 4) for a, b in hp),
            "difficulty 1 halves the hit points of the hostile monsters the LOAD MONSTERs "
            "appended (copies too) and changes no GORGON's record",
            f"changed {' '.join(str(i) for i in hit)}; hit points (difficulty 3, 1): "
            + ", ".join(f"{a}/{b}" for a, b in hp))


# The ground flow (step 4, part 4): the first turn's battlefield as the original draws
# it, its view's 7 by 7 cells at 8, 8 and the cells the combatants stand on masked
# (their icons: parts 11-12), against eclplay --ground's drawing of the same map. The
# set is DUNGCOM 4 (the fixture's dungeon: other sets differ; 59c7:0e70, part 7, chooses
# it). The frame's shield at the corner, which the port's ground does not draw, is
# masked too (8 by 9 pixels at 8, 8).
GROUND = (8, 8, 168, 168)                   # the view: x, y, w, h (queen_arena.c)
GROUND_CELL = 24
GROUND_SET = "dungcom4"
GROUND_SHIELD = (8, 8, 8, 9, "the frame's shield over the ground's corner (not the ground)")


def ground_original(work, patched, verbose):
    """The original at the first turn (the setup flow's trap, the same keys): its
    screenshot (320 by 200 in colours) and the battlefield (L.battlefield)."""
    with cokbox.Box(workdir=work / "box-ground", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        dqk_ecl.enter_block(box)
        box.set_seed(SEED)
        box.keys("\\r")
        box.settled(tolerance=64)
        box.keys("\\r")
        box.settled(tolerance=64)
        box.keys("\\r")
        handle = L.trap(box, *TURN, until=lambda: box.ds_byte(L.DS_TURN) == 1 and
                        box.ds_word(L.DS_MODE) == 5, seconds=180)
        box.settled(tolerance=64)
        screen = box.screenshot(path=str(work / "ground-original.png"))
        battlefield = L.battlefield(box)
        L.untrap(box, handle)
        return screen, battlefield


def ground(v, work, opts):
    """The ground flow: the original's first turn against eclplay --ground's drawing
    of the same battlefield. The unmasked ground (GROUND, minus the combatants' cells
    and the shield) must agree pixel for pixel."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    screen, bf = ground_original(work, patched, opts.verbose)
    view_x, view_y = bf["view"]
    cells = bytes.fromhex(bf["cells"])
    occupants = bytes.fromhex(bf["occupants"])
    width = 50
    mapfile = work / "ground.txt"
    rows = [f"view {view_x} {view_y}"] + [cells[r * width:(r + 1) * width].hex()
                                          for r in range(25)]
    mapfile.write_text("\n".join(rows) + "\n")
    bmp = work / "ground-port.bmp"
    result = subprocess.run(
        [str(ROOT / "build" / "eclplay"), "--game", "dark-queen", "--ground", str(mapfile),
         "--ground-set", GROUND_SET, "--screen", str(bmp), str(dqk_lib.ASSETS), "4"],
        capture_output=True, text=True, cwd=ROOT)
    v.check(result.returncode == 0, "eclplay --ground draws the battlefield",
            result.stderr.strip() or f"exit {result.returncode}")
    if result.returncode != 0:
        return
    port = cokbox.RGBScreen.load(bmp, (320, 200))
    gx, gy, gw, gh = GROUND
    masks = []
    for j in range(7):
        for i in range(7):
            if occupants[(view_y + j) * width + view_x + i]:
                masks.append((gx + GROUND_CELL * i, gy + GROUND_CELL * j, GROUND_CELL,
                              GROUND_CELL, "a combatant's icon"))
    masks.append(GROUND_SHIELD)
    # Everything outside the ground (the frame, the panel, the text) is masked: only the
    # ground's rectangle is compared.
    outside = [(0, 0, 320, gy, "outside the ground"), (0, gy + gh, 320, 200 - gy - gh,
               "outside the ground"), (0, gy, gx, gh, "outside the ground"),
               (gx + gw, gy, 320 - gx - gw, gh, "outside the ground")]
    screen_masks = outside + masks
    covered = sum(1 for y in range(gy, gy + gh) for x in range(gx, gx + gw)
                  if any(a <= x < a + w and b <= y < b + h for a, b, w, h, *_ in masks))
    count, box = checklib.masked_diff(screen, port, screen_masks)
    detail = (f"{count} pixels differ in {box}" if count else
              f"{covered} of {gw * gh} ground pixels masked ({len(masks) - 1} cells and the "
              f"shield), {gw * gh - covered} compared")
    v.check(count == 0, "the first turn's ground agrees with eclplay's (masked: the "
            "combatants' cells and the shield)", detail)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows", default="setup,load,ground",
                        help="which flows: setup, load, ground")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_fight")
    v = checklib.Verdict()
    flows = opts.flows.split(",")
    if "setup" in flows:
        setup(v, work, opts)
    if "load" in flows:
        load(v, work, opts)
    if "ground" in flows:
        ground(v, work, opts)
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

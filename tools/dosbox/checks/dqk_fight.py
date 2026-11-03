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

Flows `load`, `clear` and `six` (step 4, part 5): LOAD FILES, CLEARMONSTERS, a PRESS KEY,
their LOAD MONSTERs (and, for `clear`, a CLEARMONSTERS between them: LOAD MONSTER 49 x1,
CLEARMONSTERS, LOAD MONSTER 64 x1; for `six`, six LOAD MONSTERs x1, the seventh icon taking
slot 14), then COMBAT (no SETUP MONSTERS or APPROACH). Where COMBAT calls the battle
(5f07:08f0, after the appends) the original's party list, c6f1, ad6b and the undead count
DS:c2ed are read and eclplay's, started as setup's (--load --play) with --records, must
agree: the monsters' records byte for byte (pointers masked), the party's too (PARTY_GAPS:
none). `load`: 19 records, c6f1 13, ad6b 11, undead 0; `clear`: 17, c6f1 11, ad6b 10, undead
0; `six`: 22, c6f1 16, ad6b 15, undead 2. `load` runs twice, with difficulty 3 and with 1
(var 0x27 set at the entry in the original, and by --set after the load in eclplay): the
hostile monsters' hit points halve for difficulty 1. No transcript of eclplay has an icon
error (the icons are not loaded yet).

Flow `encounter` (step 4, parts 6 and 7): LOAD FILES 4 4, CLEARMONSTERS, a SHARKMAN x2 LOAD MONSTER,
SETUP MONSTERS (sprite 82, distance wanted 16: 2 squares, 2db1's 2e1c, and the close-up PICA 21, one
frame), a PRESS KEY ('setup'), APPROACH, a PRESS KEY ('near'), APPROACH, a PRESS KEY ('nearer': the
close-up at distance 0), PICTURE 0xff (the view again, the sprite and close-up forgotten), SETUP MONSTERS
(sprite 2, distance wanted 3: the open squares, 0 to 2, are var 0x37), a PRESS KEY ('wall'), ADD 9 to var
0x37, a PRESS KEY ('raised'), COMBAT (which lowers 0x37 to the open squares), from saved game A. At each
wait the original's pc, the 1 KB of variables and the screen are read, and eclplay's (--vars, --screen,
with that many keys) must equal them in RGB except the mouse cursor (the shield at the corner, masked); at
the battle's call the party's records and c6f1, ad6b and undead agree as in `load`, var 0x37 there is the
open squares in both, and eclplay stops at 5045:059d.

Flow `ground` (step 4, part 4): the same block to the first turn, where the original's
screenshot and its battlefield (dqk_fight_lib.battlefield: the view at +2 and +4, the
50 by 25 cells at +9) are read, with the set the battle drew: DS:ca03 (the wall set)
of DungCom1 where var 0x22 is not 0, else of WildCom1 (59c7:0e70). eclplay's --ground
draws the same map and set (--ground-set), and the ground (168 by 168 at 8, 8) must agree
pixel for pixel in RGB, masked: the cells the combatants stand on (their icons: parts
11-12, each 24 by 24 exactly) and the mouse pointer at the corner (8 by 9, not the
ground). The set is asserted to be DUNGCOM 4 (LOAD FILES 4 4).

Every flow's box freezes the memory cache's purge draw (dqk_fight_lib.freeze_purges, a box-only
change at 27fa:0f2c), so the original's RandSeed moves only with its own draws, as eclplay's does;
the field flow (below) checks it at each square and records the purges.

The original's states go to build/dqk-fight/setup.json, with the heap pointers
masked (dqk_fight_lib.portable): later parts compare the port's state with them.
About 20 seconds.

    python3 tools/dosbox/checks/dqk_fight.py [--workdir DIR] [--verbose]
        [--flows setup,load,clear,six,ground,encounter,field]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_fight_lib as L  # noqa: E402
import dqk_lib  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import OPCODES, Block, imm, var  # noqa: E402

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
        enter(box)
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
        L.frozen_at_trap(box)
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


def enter(box):
    """The block's entry as dqk_ecl.enter_block gives it, then the memory cache's purges frozen
    (dqk_fight_lib.freeze_purges): every fight flow enters through here."""
    dqk_ecl.enter_block(box)
    L.freeze_purges(box)


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


def port(work, patched, name="fight", typed="", records=None, difficulty=None, screen=None,
         field=None, at=None, blocks=None):
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
    if blocks is not None:
        extra += ["--blocks", str(blocks)]
    if difficulty is not None:
        extra += ["--set", f"27={difficulty:x}"]
    if screen is not None:
        extra += ["--screen", str(screen)]
    if field is not None:
        extra += ["--field", str(field)]
    if at is not None:
        extra += ["--at", "%d,%d,%d" % at]
    variables, last, _ = dqk_ecl.port(work, name, patched, typed=typed, extra=extra,
                                      block=None)
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


# The load flows (step 4, part 5): LOAD MONSTER only registers the monsters, and
# COMBAT's monsters branch appends them (6c70:2d82 per slot, then 170e's ten
# GORGONs). Each block runs LOAD FILES 4 4, CLEARMONSTERS, a PRESS KEY labelled
# 'monsters', its steps (a LOAD MONSTER (id, count), or CLEARMONSTERS), then COMBAT
# with no SETUP MONSTERS or APPROACH (the original allows it after LOAD FILES). The
# party list is read where the battle's routine 5045:059d starts, the appends done
# and placement not yet. A flow's monsters are those its steps leave after the last
# CLEARMONSTERS; the expected c6f1, ad6b, undead count and records follow from them.
CLEAR = "CLEAR"
PARTY = 6                      # the party's records come first in the list
# The MONCHA ids of the six-loads flow: each its own slot, and 170e's icon takes
# the seventh (slot 14). The ZOMBIE (37) and the WRAITH (36) are undead.
SIX_IDS = (64, 49, 41, 37, 36, 64)
# COMBAT's call of the battle (5f07:08f0, a lcall to 5045:059d through its stub):
# the state is read where it spins, after the appends and before the battle's
# routine. The trap is set while the block waits for its PRESS KEY, as COMBAT's
# code is resident in the ECL's overlay then: the battle starts at once after
# the key, faster than a trap set after it (the loads are not waited for).
CALL_BATTLE = (0x5F07, 0x08F0)
# How long COMBAT may take to reach that call after the LOAD MONSTERs (wall clock).
COMBAT_SECONDS = 300
# The battle's stop (step 4, part 9a): the battle starts, builds its battlefield and makes its
# combat records (59c7:0f7b, ported), then stops at the placement (59c7:1864), not ported yet.
PLACEMENT = (0x59C7, 0x1864)
BATTLE_STOP = ("error: not yet ported: The Dark Queen of Krynn's battle's placement "
               "(dqk 59c7:1864)")
# The record bytes the combat records recompute between the battle's call (5f07:08f0, read by the
# battle-start checks) and the stop (59c7:1864, which the port stops at after 0f7b): 0f7b gives
# every record its stats (177e:0be2, cok_character_stats), which rewrite these bytes (the load,
# the sheet's and the combat's values, with their hands and encumbrance). The placement check
# (field) reads the records whole at 1864 and agrees with the port there, so the battle-start
# checks allow these bytes to differ and no others.
RECOMPUTED = frozenset((0x56, 0x57, 0xCA, 0x18D, 0x18E, 0x18F, 0x192, 0x194, 0x196, 0x199))
# The party's records as eclplay imports them from the saved game (--load) are the
# original's, byte for byte: none differs. With --character they differed for four
# members (the known spells at 0x159, the saving throws and thief skills at 0x8b and
# 0x93-0x97), which was the character import's, not the battle's. Kept as the exact
# set (empty): a new difference fails the check.
PARTY_GAPS = {}
# 170e's extra monsters: ten of them, each appended (6c70:170e's movb $0xa).
EXTRAS = 10
# A record's pointer fields, all masked: the next, effects and items (0-0xb),
# the readied items' slots (0xc-0x3f) and the combat record (0x40-0x43).
POINTER_BYTES = range(0, 0x44)
HIT_POINTS = 0x198             # a record's hit points (dqk_fight_lib's layout)
COMBAT_BLOCK = 0x18            # a record's combat block (59c7:0f7b; dqk_fight_lib.COMBAT_RECORD)


class Flow:
    """A load block: its steps, the LOAD MONSTERs that reach COMBAT (after the last
    CLEARMONSTERS), the undead count those make (29fc counts one per undead slot),
    and the difficulties it runs at (None: the entry's)."""

    def __init__(self, name, steps, undead, difficulties=(None,)):
        self.name, self.steps, self.undead = name, steps, undead
        self.difficulties = difficulties
        self.kept = []
        for step in steps:
            if step == CLEAR:
                self.kept = []
            else:
                self.kept.append((step[1], step[2]))

    @property
    def appended(self):
        """The records COMBAT appends: each slot's count (a count of 0 is 1, which
        the flows do not use), then 170e's extras."""
        return sum(count for _, count in self.kept) + EXTRAS

    @property
    def icon_slot(self):
        """DS:ad6b at the battle: the slots 8 up to the loads, then 170e's."""
        return 8 + len(self.kept) + 1


LOAD = Flow("load", (("LOAD", MONSTER, COUNT), ("LOAD", 49, 1)), undead=0,
            difficulties=(None, 1))
CLEARED = Flow("clear", (("LOAD", 49, 1), CLEAR, ("LOAD", MONSTER, 1)), undead=0)
SIX = Flow("six", tuple(("LOAD", monster, 1) for monster in SIX_IDS), undead=2)


def load_block(steps):
    """Block 1 of the load test: the entry PRESS KEY, LOAD FILES 4 4, CLEARMONSTERS,
    a PRESS KEY labelled 'monsters', the steps (each LOAD MONSTER with the icon the
    id, or CLEARMONSTERS), COMBAT, and the block's EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    b.op("LOAD_FILES", imm(4), imm(4))
    b.op("CLEARMONSTERS")
    b.label("monsters")
    b.op("PRESS_KEY")
    for step in steps:
        if step == CLEAR:
            b.op("CLEARMONSTERS")
        else:
            _, monster, count = step
            b.op("LOAD_MONSTER", imm(monster), imm(count), imm(monster))
    b.op("COMBAT")
    b.op("EXIT")
    return b


def load_original(work, patched, flow, difficulty, verbose):
    """The original's run of a load block: where COMBAT calls the battle (trapped,
    not run on), the block's variables and the party list's records with the
    monsters' count, the next icon slot and the undead count. DIFFICULTY, if not
    None, is var 0x27 set at the entry (var a is the byte at the far pointer plus a)."""
    tag = f"{flow.name}-{difficulty}"
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen",
                    verbose=verbose, timeout=900, files=L.files(patched)) as box:
        enter(box)
        if difficulty is not None:
            box.write(box.far(box.ds * 16 + dqk_ecl.DS_VARS) + 0x27, bytes([difficulty]))
        box.set_seed(SEED)
        box.keys("\\r")                 # LOAD FILES, CLEARMONSTERS; waits at 'monsters'
        # Waited for by its state, not the screen: the block at its 'monsters' PRESS
        # KEY with the game waiting for a key there (a settled screen may come before it).
        dqk_ecl.wait_press_key(box, pc=load_block(flow.steps).address("monsters") + 1)
        handle = L.trap(box, *CALL_BATTLE, until=lambda: True, seconds=60)
        L.frozen_at_trap(box)
        box.keys("\\r")                 # the LOAD MONSTERs, COMBAT: the appends, the call
        # The spin holds once COMBAT reaches it. Waited for by a time limit, not a
        # count of checks: a count of 180 ran out once under make test's load (each
        # check settles a second of the game's time, which a loaded host gives in
        # less host time, so the count covers less of COMBAT than it did idle).
        end, checks = time.monotonic() + COMBAT_SECONDS, 0
        while not L.held(box, handle)[0]:
            checks += 1
            if time.monotonic() > end:
                state = dqk_lib.ecl_state(box)
                raise cokbox.BoxError(
                    f"COMBAT did not reach the battle's call (5f07:08f0) in {COMBAT_SECONDS} s "
                    f"({checks} checks): ECL pc {state['pc']:#x}, opcode {state['opcode']:#x}, "
                    f"keyboard polls {box.hook('polls')}")
        records = L.records(box)
        return dict(variables=block_vars(box),
                    records=[raw.hex() for _, raw in records],
                    count=box.ds_byte(L.DS_MONSTER_COUNT),
                    icon_slot=box.ds_byte(L.DS_ICON_SLOT),
                    undead=box.ds_byte(L.DS_UNDEAD), traps=len(handle),
                    names=names_of([{"raw": raw.hex()} for _, raw in records]))


def masked(raw_hex):
    """A record's bytes with its pointer fields zeroed (the port has none)."""
    raw = bytearray.fromhex(raw_hex)
    for i in POINTER_BYTES:
        raw[i] = 0
    return bytes(raw)


# The computer flag (+0x18c): 52db:4dd2 (the leadership, before 1dd7) sets it, after COMBAT's call
# (5f07:08f0) where the battle's start is read, so the start compares it as 0 (at_start). The field
# flow compares it at the placement, with the rest of the record.
COMPUTER = 0x18C


def at_start(raw_hex):
    raw = bytearray(masked(raw_hex))
    raw[COMPUTER] = 0
    return bytes(raw)


def load_run(v, work, opts, flow, difficulty):
    """One run of a load block: the original's (load_original), eclplay's from the
    same start (port, --records), and the checks against it. Returns the original's
    readings and eclplay's records."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(load_block(flow.steps)))
    tag = "" if flow is LOAD and difficulty is None else f" ({flow.name}"
    if difficulty is not None:
        tag += f", difficulty {difficulty}"
    tag += ")" if tag else ""
    r = load_original(work, patched, flow, difficulty, opts.verbose)
    v.check(r["traps"] >= 1, f"the battle's routine (5045:059d) is trapped{tag}",
            f"{r['traps']} copies")
    v.check(len(r["records"]) == PARTY + flow.appended and r["count"] == flow.appended and
            r["icon_slot"] == flow.icon_slot and r["undead"] == flow.undead,
            f"at the battle's start the list holds the party and {flow.appended} monsters, "
            f"c6f1 {flow.appended}, ad6b {flow.icon_slot} and undead {flow.undead}{tag}",
            f"{len(r['records'])} records, c6f1 {r['count']}, ad6b {r['icon_slot']}, "
            f"undead {r['undead']}: " + " ".join(r["names"][PARTY:]))
    name = f"{flow.name}-{difficulty}"
    records = work / f"port-{name}.records"
    port_vars, port_last = port(work, patched, name=name, typed="\\r",
                                records=records, difficulty=difficulty)
    diff = [a for a in range(1, dqk_ecl.VARIABLES + 1)
            if port_vars[a - 1] != r["variables"][a - 1]] \
        if len(port_vars) == dqk_ecl.VARIABLES else []
    v.check(len(port_vars) == dqk_ecl.VARIABLES and tuple(diff) == PORT_GAPS,
            f"eclplay's variables at the battle's start agree with the original's "
            f"(PORT_GAPS){tag}", f"differ: {' '.join(f'{a:#x}' for a in diff)}")
    v.check(port_last == BATTLE_STOP, f"eclplay stops at the battle (not yet ported){tag}",
            port_last)
    # No icon error: the monsters' icons are not loaded yet (monster_icons_unported),
    # so the transcript's only error line is the battle's stop.
    transcript = (work / f"port-{name}.txt").read_text().splitlines()
    errors = [line for line in transcript if line.startswith("error:")]
    v.check(errors == [BATTLE_STOP], f"eclplay's transcript has no error but the battle's{tag}",
            "; ".join(errors))
    data = records.read_bytes() if records.exists() else b""
    size = L.RECORD
    count, icon_slot, undead = (data[-3], data[-2], data[-1]) if len(data) >= 3 else (-1, -1, -1)
    ported = [data[i:i + size].hex() for i in range(0, len(data) - 3, size)]
    v.check(len(data) >= 3 and len(ported) == len(r["records"]) and
            count == r["count"] and icon_slot == r["icon_slot"] and undead == r["undead"],
            f"eclplay's party list has the original's records, c6f1, ad6b and undead{tag}",
            f"{len(ported)} records, c6f1 {count}, ad6b {icon_slot}, undead {undead}")
    # The records' bytes, pointers masked: the monsters' must agree, and the party's
    # must agree at PARTY_GAPS (the import's bytes, before any monster is looked at).
    where = {i: tuple(k for k in range(L.RECORD)
                      if at_start(ported[i])[k] != at_start(r["records"][i])[k] and
                      k not in RECOMPUTED)
             for i in range(min(len(ported), len(r["records"])))}
    where = {i: ks for i, ks in where.items() if ks}
    v.check(len(ported) == len(r["records"]) and
            all(not ks for i, ks in where.items() if i >= PARTY) and
            where == PARTY_GAPS,
            f"every monster's record agrees at the battle's start (pointers masked; RECOMPUTED "
            f"aside), and the party's agree at PARTY_GAPS (none){tag}",
            "; ".join(f"{i}: " + " ".join(f"{k:#x}" for k in ks) for i, ks in sorted(where.items())))
    return r, ported


def load(v, work, opts):
    """The load flow: the appends at the battle's start, in two runs (the entry's
    difficulty, and difficulty 1), each checked against the original and eclplay;
    and the difficulty changes the hostile monsters' hit points (the three
    LOAD MONSTER records, their copies included) and no GORGON's."""
    runs = {d: load_run(v, work, opts, LOAD, d) for d in LOAD.difficulties}
    base, low = runs[None][0]["records"], runs[1][0]["records"]
    monsters = range(PARTY, PARTY + LOAD.appended)
    hit = [i for i in monsters if masked(base[i]) != masked(low[i])]
    loaded = range(PARTY, PARTY + sum(count for _, count in LOAD.kept))
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
# set is the one 59c7:0e70 loads at the battle's start: DungCom1 with record DS:ca03
# (LOAD FILES' second operand, the wall set) where var 0x22 is not 0, else WildCom1
# with the same record. The fixture's is DUNGCOM 4 (LOAD FILES 4 4), asserted. The
# mouse pointer at the corner, which the port's ground does not draw (ALWAYS record 2,
# hotspot 7, 7, at 8, 8: dark_queen.c's pointer; 5837:0846 hides and shows it around
# the redraw), is masked too (its 8 by 9 pixels of the ground at 8, 8).
GROUND = (8, 8, 168, 168)                   # the view: x, y, w, h (queen_arena.c)
GROUND_CELL = 24
GROUND_SET_RECORD = 0xCA03                  # DS:ca03, the wall set (59c7:0e70's operand)
GROUND_POINTER = (8, 8, 8, 9, "the mouse pointer at the corner (ALWAYS 2, not the ground)")


def ground_original(work, patched, verbose):
    """The original at the first turn (the setup flow's trap, the same keys): its
    screenshot (320 by 200 in colours), the battlefield (L.battlefield) and the set
    the battle drew (59c7:0e70's choice, as eclplay's --ground-set names it)."""
    with cokbox.Box(workdir=work / "box-ground", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        box.keys("\\r")
        box.settled(tolerance=64)
        box.keys("\\r")
        box.settled(tolerance=64)
        box.keys("\\r")
        handle = L.trap(box, *TURN, until=lambda: box.ds_byte(L.DS_TURN) == 1 and
                        box.ds_word(L.DS_MODE) == 5, seconds=180)
        L.frozen_at_trap(box)
        box.settled(tolerance=64)
        screen = box.screenshot(path=str(work / "ground-original.png"))
        battlefield = L.battlefield(box)
        wall = box.ds_byte(GROUND_SET_RECORD)
        three_d = block_vars(box)[0x22 - 1]  # var 0x22 (dqk_ecl's variables from 1)
        dungeon = three_d != 0
        set_name = f"{'dungcom' if dungeon else 'wildcom'}{wall}"
        L.untrap(box, handle)
        return screen, battlefield, set_name


def ground(v, work, opts):
    """The ground flow: the original's first turn against eclplay --ground's drawing
    of the same battlefield. The unmasked ground (GROUND, minus the combatants' cells
    and the pointer) must agree pixel for pixel."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    screen, bf, set_name = ground_original(work, patched, opts.verbose)
    v.check(set_name == "dungcom4", "the battle's set is DUNGCOM 4 (59c7:0e70: DS:ca03 "
            "and var 0x22 of the fixture)", set_name)
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
         "--ground-set", set_name, "--screen", str(bmp), str(dqk_lib.ASSETS), "4"],
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
    masks.append(GROUND_POINTER)
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
              f"pointer), {gw * gh - covered} compared")
    v.check(count == 0, "the first turn's ground agrees with eclplay's (masked: the "
            "combatants' cells and the pointer)", detail)


# The encounter (steps 6 and 7 of part 6's SETUP MONSTERS and APPROACH): SETUP MONSTERS with the
# sprite (SPRIT.TLB's record 82), the distance wanted 16 (which 6c70:2e1c makes 2 squares, in both
# vars, whatever the open squares) and the close-up PICA 21 (one frame: PICTURE 14 animates, and
# its screen at a wait is a later frame), then APPROACH, a PRESS KEY, APPROACH, a PRESS KEY, and
# the PRESS KEY at distance 0 ('nearer', the close-up shown). PICTURE 0xff forgets the sprite and
# close-up (5f07:0511-054b); a second SETUP MONSTERS with sprite 2 and the distance wanted 3 (the
# open squares, 0 to 2, are var 0x37: min(open, 3)) draws its sprite at the 'wall' wait; ADD puts
# 9 in var 0x37 (above the open squares), the 'raised' wait; then COMBAT lowers it to the open
# squares before the battle's text (5f07:088f-08ba). The original waits at each PRESS KEY; there
# the variables (the whole 1 KB) and the screen are read, and eclplay must agree with both. At
# COMBAT the battle's call is trapped (5f07:08f0) and the monsters and party read, as the load
# flows do, with var 0x37.
ENC_SPRITE, ENC_DISTANCE, ENC_CLOSEUP = 82, 16, 21
ENC_SPRITE2, ENC_WALL_DISTANCE, ENC_RAISED = 2, 3, 9  # the open squares are 0 here: a wall at once
ENC_WAITS = ("setup", "near", "nearer", "wall", "raised")
# var 0x37 at each wait: the approaches' 2, 1, 0; then the open squares (0 to 2: checked as a
# range, the value is the original's, and eclplay's must equal it); then ENC_RAISED.
ENC_DISTANCES = (2, 1, 0, None, ENC_RAISED)
# The mouse cursor: the shield at the corner (ALWAYS record 2 at 2, 2; 2aa6:15e3's shield
# elsewhere), drawn by the front end, not on the screen the port's BMP holds.
ENC_SCREEN_MASKS = [(2, 2, 16, 18, "the mouse cursor (the shield at the corner)")]
# The battle's start: one LOAD MONSTER (SHARKMAN x COUNT) and COMBAT's ten GORGONs.
ENCOUNTER = Flow("encounter", (("LOAD", MONSTER, COUNT),), undead=0)


def encounter_block():
    """Block 1 of the encounter test (see above), with the entry PRESS KEY first."""
    b = Block()
    b.op("PRESS_KEY")
    b.op("LOAD_FILES", imm(4), imm(4))
    b.op("CLEARMONSTERS")
    b.op("LOAD_MONSTER", imm(MONSTER), imm(COUNT), imm(MONSTER))
    b.op("SETUP_MONSTERS", imm(ENC_SPRITE), imm(ENC_DISTANCE), imm(ENC_CLOSEUP), imm(0))
    b.label("setup")
    b.op("PRESS_KEY")
    b.op("APPROACH")
    b.label("near")
    b.op("PRESS_KEY")
    b.op("APPROACH")
    b.label("nearer")
    b.op("PRESS_KEY")
    b.op("PICTURE", imm(0xFF))
    b.op("SETUP_MONSTERS", imm(ENC_SPRITE2), imm(ENC_WALL_DISTANCE), imm(0xFF), imm(0))
    b.label("wall")
    b.op("PRESS_KEY")
    b.op("ADD", imm(0), imm(ENC_RAISED), var(0x37))
    b.label("raised")
    b.op("PRESS_KEY")
    b.op("COMBAT")
    b.op("EXIT")
    return b


def encounter_original(work, patched, verbose):
    """The original's run of the encounter block: at each of its three waits, the wait's
    pc and opcode, the block's variables and the screen (in colours); at 'nearer' the
    battle's call is trapped (where COMBAT spins) and the party's records and the
    monsters' count, icon slot and undead count are read there."""
    waits = []
    with cokbox.Box(workdir=work / "box-encounter", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        for index, name in enumerate(ENC_WAITS):
            box.keys("\\r")               # the entry's key, then each wait's
            box.settled(tolerance=64)
            state = dqk_lib.ecl_state(box)
            screen = box.screenshot(path=str(work / f"enc-original-{index}.png"))
            waits.append(dict(name=name, pc=state["pc"], opcode=state["opcode"],
                              vars=block_vars(box), screen=screen))
        handle = L.trap(box, *CALL_BATTLE, until=lambda: True, seconds=60)
        L.frozen_at_trap(box)
        box.keys("\\r")                   # COMBAT: the lowering, the appends, the call
        for _ in range(180):
            if L.held(box, handle)[0]:
                break
        else:
            raise cokbox.BoxError("COMBAT did not reach the battle's call (5f07:08f0)")
        records = L.records(box)
        battle = dict(records=[raw.hex() for _, raw in records],
                      count=box.ds_byte(L.DS_MONSTER_COUNT),
                      icon_slot=box.ds_byte(L.DS_ICON_SLOT),
                      undead=box.ds_byte(L.DS_UNDEAD), traps=len(handle),
                      var37=block_vars(box)[0x37 - 1],
                      names=names_of([{"raw": raw.hex()} for _, raw in records]))
        L.untrap(box, handle)
    return waits, battle


def wall_dist(waits):
    """var 0x37 at the 'wall' wait: the open squares ahead (the SETUP's min(open, 3))."""
    return next(w["vars"][0x37 - 1] for w in waits if w["name"] == "wall")


def encounter(v, work, opts):
    """The encounter flow (step 4, part 6): the original's three waits and the battle's
    start against eclplay's from the same saved game. At each wait, eclplay's variables
    (--vars) equal the original's 1 KB (PORT_GAPS: none), its screen (--screen) agrees
    in RGB with the original's except the mouse pointer, and var 0x37 is the distance
    the approaches leave. At the battle's start the party's and monsters' records,
    c6f1, ad6b and the undead count are the load flow's (the two SHARKMEN and 170e's ten
    GORGONs), and eclplay stops at the battle."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    block = encounter_block()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
    waits, battle = encounter_original(work, patched, opts.verbose)
    for index, wait in enumerate(waits):
        k = index + 1
        v.check(wait["pc"] == block.address(wait["name"]) + 1 and
                wait["opcode"] == OPCODES["PRESS_KEY"],
                f"the original waits at PRESS KEY '{wait['name']}'",
                f"pc {wait['pc']:#x}, opcode {wait['opcode']:#x}")
        bmp = work / f"enc-port-{k}.bmp"
        port_vars, port_last = port(work, patched, name=f"enc-{k}",
                                    typed="\\r" * (k - 1), screen=bmp)
        diff = [a for a in range(1, dqk_ecl.VARIABLES + 1)
                if port_vars[a - 1] != wait["vars"][a - 1]] \
            if len(port_vars) == dqk_ecl.VARIABLES else []
        v.check(len(port_vars) == dqk_ecl.VARIABLES and tuple(diff) == PORT_GAPS,
                f"eclplay's variables at '{wait['name']}' agree with the original's "
                f"(PORT_GAPS)", f"differ: {' '.join(f'{a:#x}' for a in diff)}")
        dist = wait["vars"][0x37 - 1]
        ported_dist = port_vars[0x37 - 1] if len(port_vars) == dqk_ecl.VARIABLES else -1
        expected = ENC_DISTANCES[index]
        if expected is None:   # the open squares: 2 at most, whatever the wall
            v.check(0 <= dist <= 2 and ported_dist == dist,
                    f"var 0x37 at '{wait['name']}' is the open squares (0 to 2)",
                    f"original {dist}, eclplay {ported_dist}")
        else:
            v.check(dist == expected and ported_dist == dist,
                    f"var 0x37 at '{wait['name']}' is {expected}",
                    f"original {dist}, eclplay {ported_dist}")
        if not bmp.exists():
            v.check(False, f"eclplay writes its screen at '{wait['name']}'", port_last)
            continue
        port_screen = cokbox.RGBScreen.load(bmp, (320, 200))
        count, box = checklib.masked_diff(wait["screen"], port_screen, ENC_SCREEN_MASKS)
        v.check(count == 0, f"the screen at '{wait['name']}' agrees in RGB (the pointer "
                f"masked)", f"{count} pixels differ in {box}" if count else "all pixels agree")
    records = work / "port-encounter.records"
    port_vars, port_last = port(work, patched, name="encounter",
                                typed="\\r" * len(ENC_WAITS), records=records)
    v.check(port_last == BATTLE_STOP, "eclplay stops at the battle (not yet ported)", port_last)
    ported37 = port_vars[0x37 - 1] if len(port_vars) == dqk_ecl.VARIABLES else -1
    v.check(ported37 == battle["var37"] and battle["var37"] == wall_dist(waits),
            "COMBAT lowers var 0x37 to the open squares before the battle, in both",
            f"original {battle['var37']}, eclplay {ported37}, the wall's {wall_dist(waits)}")
    data = records.read_bytes() if records.exists() else b""
    count, icon_slot, undead = (data[-3], data[-2], data[-1]) if len(data) >= 3 else (-1, -1, -1)
    ported = [data[i:i + L.RECORD].hex() for i in range(0, len(data) - 3, L.RECORD)]
    v.check(len(ported) == len(battle["records"]) and count == battle["count"] and
            icon_slot == battle["icon_slot"] and undead == battle["undead"],
            "eclplay's party list at the battle's start has the original's records, c6f1, ad6b "
            "and undead", f"{len(ported)} records, c6f1 {count}, ad6b {icon_slot}, undead {undead}")
    where = [i for i in range(min(len(ported), len(battle["records"])))
             if any(at_start(ported[i])[k] != at_start(battle["records"][i])[k] and
                    k not in RECOMPUTED for k in range(L.RECORD))]
    v.check(not where and len(ported) == len(battle["records"]),
            "every record agrees at the battle's start (pointers masked, the computer flag as 0, RECOMPUTED aside; "
            "the party's too)",
            " ".join(str(i) for i in where))
    v.check(battle["count"] == ENCOUNTER.appended and battle["icon_slot"] == ENCOUNTER.icon_slot
            and battle["undead"] == 0 and len(battle["records"]) == PARTY + ENCOUNTER.appended,
            f"the battle starts with the party and {ENCOUNTER.appended} monsters (the SHARKMEN "
            f"and 170e's ten GORGONs), "
            f"c6f1 {battle['count']}, ad6b {battle['icon_slot']}, undead {battle['undead']}",
            " ".join(battle["names"][PARTY:]))


# The battlefield (step 4, parts 7 and 9a): the battle's setup builds the battlefield (59c7:1dd7:
# the tiles, then 06a7's dungeon from the 3D map; a battle outside 3D stops at 0e3b), makes the
# combat records (59c7:0f7b: each record's combat block, checked at every offset) and stops at
# the placement, 59c7:1864. The original is trapped at its entry, after those: the view's origin
# (far DS:bedb +2, +4), the header (+6, +7, +8), the 50 by 25 cells (+9), each record's combat
# block (0x18 bytes at +0x40) and RandSeed; eclplay --field and --blocks, at its stop, must hold
# the same. The trap moved from 0f7b to 1864 when 0f7b was ported (9a): the stop is after it.
# The dungeon draws no dice, and the memory cache's purges draw none in the box
# (dqk_fight_lib.freeze_purges), so RandSeed is the seed the battle began with, as in eclplay. The fixture's battle is in 3D (var 0x22 set), asserted; the
# origin is written after the placement (1dd7's 4837:0aea), so it is what the battle before left,
# and is compared as read.
FIELD_CELLS = 50 * 25
# The party's squares: the fixture's (saved game A's, 7, 13, facing 0), one on the map's west
# edge (0, 3, facing 2), where the squares off the map are walls but for the party's row (the
# open side the builder's off-map rule gives, 59c7:0072), and two that catch what the first
# two do not: (12, 12), 049e's door corner and the 17th column, and (4, 14), the 17th row and
# 0285's door again (each mutation of those rules shows only there). The builders ignore the
# facing. The square is DS:c9f0-c9f2 (x, y, facing), which the original is given before the
# block enters the battle, and eclplay --at the same.
FIELD_SQUARES = (None, (0, 3, 2), (12, 12, 0), (4, 14, 0))
# The memory cache's purge counters (27fa:0f14 adds one to DS:d9ba for an argument of 0, to
# DS:da8c otherwise). They are read and recorded, not subtracted: the purges still happen and
# evict a block, each drawing nothing (the freeze). The count varies from run to run with the
# heap's state (0 to 11 past the entry here, the same inputs), so the verdict only records it.
PURGE_COUNTERS = (0xD9BA, 0xDA8C)


def purges(box):
    return sum(box.ds_word(off) for off in PURGE_COUNTERS)


def field_original(work, patched, verbose, square=None, tag="field"):
    """The original's entry to the placement (the setup flow's keys, then the trap), read
    there: the battlefield, RandSeed, the purges of its memory cache since SEED was set, the
    1 KB of variables, the mode and whether the spin holds. SQUARE, if given, is the party's
    (x, y, facing) written at DS:c9f0."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        if square is not None:
            box.ds_write(SQUARE, bytes(square))
        box.set_seed(SEED)
        purges_at_seed = purges(box)
        box.keys("\\r")
        box.settled(tolerance=64)
        box.keys("\\r")
        box.settled(tolerance=64)
        box.keys("\\r")                 # SETUP MONSTERS, APPROACH, COMBAT: the battle begins
        handle = L.trap(box, *PLACEMENT, until=lambda: True, seconds=180)
        L.frozen_at_trap(box)
        box.settled(tolerance=64)
        held, polls, polls_held = L.held(box, handle)
        listed = L.records(box)
        result = dict(traps=len(handle), held=held, polls=(polls, polls_held),
                      field=L.battlefield(box), seed=box.ds_dword(L.DS_SEED),
                      purges=purges(box) - purges_at_seed,
                      records=[raw.hex() for _, raw in listed],
                      blocks=[None if cb is None else cb[1].hex()
                              for cb in (L.combat_record(box, raw) for _, raw in listed)],
                      mode=box.ds_word(L.DS_MODE), variables=block_vars(box))
        L.untrap(box, handle)
        return result


def parse_field(path):
    """eclplay --field's file: RandSeed, the view's origin (signed), the header's three bytes
    (cursor, cursor size, see-all) and the cells in hex."""
    lines = path.read_text().splitlines()
    seed = int(lines[0].split()[1], 16)
    view = [int(v) for v in lines[1].split()[1:]]
    header = [int(v) for v in lines[2].split()[1:]]
    cells = "".join(lines[3:3 + 25])
    return seed, view, header, cells


def field(v, work, opts):
    """The battlefield flow: each square's original entry to the placement against eclplay's
    stop (the fixture's dungeon first, then the west edge's)."""
    for k, square in enumerate(FIELD_SQUARES):
        field_one(v, work, opts, square, f"field{k}" if square else "field")


def field_one(v, work, opts, square, tag):
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    where = "the fixture's square" if square is None else f"the square {square}"
    r = field_original(work, patched, opts.verbose, square, tag)
    v.check(r["traps"] >= 1 and r["held"], "the original spins at the placement (59c7:1864)",
            f"{r['traps']} copies, polls {r['polls'][0]} -> {r['polls'][1]}")
    v.check(r["mode"] == 5, "the battle's mode is 5 at the placement", f"mode {r['mode']}")
    three_d = r["variables"][0x22 - 1]
    v.check(three_d != 0, f"the battle at {where} is in 3D (var 0x22 set): the dungeon builder ran",
            f"var 0x22 {three_d}")
    original = r["field"]
    cells = bytes.fromhex(original["cells"])
    path = work / f"port-{tag}.field"
    records = work / f"port-{tag}.records"
    blocks = work / f"port-{tag}.blocks"
    port_vars, port_last = port(work, patched, name=tag, typed="\\r\\r", field=path, at=square,
                                records=records, blocks=blocks)
    v.check(port_last == BATTLE_STOP, "eclplay stops at the placement (not yet ported)", port_last)
    if not path.exists():
        v.check(False, "eclplay writes its battlefield at the stop", port_last)
        return
    # Var 0x3c gates the records' effect 0x95 (59c7:0f7b:10cf): its value agrees, and the
    # records' effects (masked in the record compare) follow it; 0 at every square here.
    gate = r["variables"][0x3C - 1]
    got = port_vars[0x3C - 1] if len(port_vars) == dqk_ecl.VARIABLES else -1
    v.check(got == gate, f"var 0x3c (the records' effect gate) agrees at the placement at {where}",
            f"eclplay {got}, the original {gate}")
    seed, view, header, ported = parse_field(path)
    draws = L.draws(SEED, r["seed"], 1000)
    v.check(seed == SEED and draws == 0,
            f"RandSeed at the placement: the original draws nothing past SEED, as eclplay does, "
            f"at {where} (its memory cache purged {r['purges']} times, each drawing nothing: "
            f"dqk_fight_lib.freeze_purges)",
            f"original {r['seed']:#x} ({draws} draws past SEED, {r['purges']} purges), "
            f"eclplay {seed:#x}")
    text = (work / f"port-{tag}.txt").read_text().splitlines()
    begun = sum(1 for line in text if line == "print: A battle begins...")
    data = records.read_bytes() if records.exists() else b""
    ported_records = [data[i:i + L.RECORD].hex() for i in range(0, len(data) - 3, L.RECORD)]
    where_records = [i for i in range(min(len(ported_records), len(r["records"])))
                     if masked(ported_records[i]) != masked(r["records"][i])]
    v.check(not where_records and len(ported_records) == len(r["records"]),
            "every record agrees at the placement (pointers masked; the computer flag set by "
            "the leadership)", f"{len(ported_records)} against {len(r['records'])}; differ: "
            f"{' '.join(map(str, where_records))}")
    # Each record's combat block (0x18 bytes at +0x40, 59c7:0f7b), in the layout's named bytes
    # (facing +0x0a, not_party +0x14, +0x17) and the rest, which the port reads as 0: the
    # original's must be 0 there too (eclplay --blocks dumps what the port holds).
    data = blocks.read_bytes() if blocks.exists() else b""
    ported_blocks = [data[i:i + COMBAT_BLOCK].hex() for i in range(0, len(data), COMBAT_BLOCK)]
    where_blocks = [i for i, b in enumerate(r["blocks"])
                    if i >= len(ported_blocks) or b is None or ported_blocks[i] != b]
    v.check(not where_blocks and len(ported_blocks) == len(r["blocks"]) and
            len(data) % COMBAT_BLOCK == 0,
            f"every record's combat block agrees at the placement (0x18 bytes at +0x40, the "
            f"unnamed bytes 0 in both) at {where}",
            f"{len(ported_blocks)} against {len(r['blocks'])}; differ: "
            f"{' '.join(map(str, where_blocks))}")
    v.check(begun == 1, "eclplay prints \"A battle begins...\" once (no setup prelude: 59c7:1dd7 "
            "prints nothing)", f"{begun} times")
    v.check(header == original["header"],
            "the header's cursor, cursor size and see-all (+6, +7, +8) agree at the placement",
            f"eclplay {header}, the original's {original['header']}")
    expect = cells.hex()
    diff = [i for i in range(FIELD_CELLS) if ported[2 * i:2 * i + 2] != expect[2 * i:2 * i + 2]]
    v.check(not diff, f"the 50 by 25 cells agree (the dungeon's tiles, as the builders leave them) "
            f"at {where}",
            f"{len(diff)} cells differ, first {diff[:6]}" if diff else
            f"all {FIELD_CELLS} cells agree")
    # The origin (far DS:bedb +2, +4) is written after the placement (1dd7's 4837:0aea and
    # 0b19), so at its entry the original holds what was there before: 0, 0 for the fixture,
    # 117, 118 for the west edge (a leftover). Not compared; eclplay's stays as setup left it.
    v.check(view == [0, 0], "eclplay's view origin at the stop is as setup left it (0, 0)",
            f"eclplay {view}; the original's {[original['view'][0], original['view'][1]]} "
            "(a leftover: written after the placement)")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows", default="setup,load,clear,six,ground,encounter,field",
                        help="which flows: setup, load, clear, six, ground, encounter, field")
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
    if "clear" in flows:
        load_run(v, work, opts, CLEARED, None)
    if "six" in flows:
        load_run(v, work, opts, SIX, None)
    if "ground" in flows:
        ground(v, work, opts)
    if "encounter" in flows:
        encounter(v, work, opts)
    if "field" in flows:
        field(v, work, opts)
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

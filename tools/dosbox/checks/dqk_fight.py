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
slot 14), then COMBAT (no SETUP MONSTERS or APPROACH). Where the battle's placement is entered
(5045:078b, the first turn, after the appends, the combat records and the round's start) the original's party list, each
record's combat block, c6f1, ad6b and the undead count DS:c2ed are read, and eclplay's, started as
setup's (--load --play) with --records and --blocks, must agree: every record whole (pointers
masked; PARTY_GAPS: none) and every combat block (0x18 bytes). `load`: 19 records, c6f1 13,
ad6b 11, undead 0; `clear`: 17, c6f1 11, ad6b 10, undead 0; `six`: 22, c6f1 16, ad6b 15,
undead 2. `load` runs twice, with difficulty 3 and with 1 (var 0x27 set at the entry in the
original, and by --set after the load in eclplay): the hostile monsters' hit points halve for
difficulty 1. No transcript of eclplay has an icon error (the icons load: part 11a).

Flow `encounter` (step 4, parts 6 and 7): LOAD FILES 4 4, CLEARMONSTERS, a SHARKMAN x2 LOAD MONSTER,
SETUP MONSTERS (sprite 82, distance wanted 16: 2 squares, 2db1's 2e1c, and the close-up PICA 21, one
frame), a PRESS KEY ('setup'), APPROACH, a PRESS KEY ('near'), APPROACH, a PRESS KEY ('nearer': the
close-up at distance 0), PICTURE 0xff (the view again, the sprite and close-up forgotten), SETUP MONSTERS
(sprite 2, distance wanted 3: the open squares, 0 to 2, are var 0x37), a PRESS KEY ('wall'), ADD 9 to var
0x37, a PRESS KEY ('raised'), COMBAT (which lowers 0x37 to the open squares), from saved game A. At each
wait the original's pc, the 1 KB of variables and the screen are read, and eclplay's (--vars, --screen,
with that many keys) must equal them in RGB except the mouse cursor (the shield at the corner, masked); at
at the first turn (5045:078b) the party's records, their combat blocks, the turn list and c6f1, ad6b and undead
agree as in `load`, var 0x37 there is the open squares in both, and eclplay stops at the placement.

Flow `ground` (step 4, part 4): the same block to the first turn, where the original's
screenshot and its battlefield (dqk_fight_lib.battlefield: the view at +2 and +4, the
50 by 25 cells at +9) are read, with the set the battle drew: DS:ca03 (the wall set)
of DungCom1 where var 0x22 is not 0, else of WildCom1 (59c7:0e70). eclplay's --ground
draws the same map and set (--ground-set), and the ground (168 by 168 at 8, 8) must agree
pixel for pixel in RGB, masked: the cells the combatants stand on (their icons: parts
11-12, each 24 by 24 exactly) and the mouse pointer at the corner (8 by 9, not the
ground). The set is asserted to be DUNGCOM 4 (LOAD FILES 4 4).

Flow `wild` (step 4, part 8): the same fight outside 3D (dqkecl.fight's `ground`: var 0x22 0, the
flags index at var 0x1a2, the river at var 0x33), two grounds from one seed, each read at the placement
(5045:078b, the same trap as `field`) and compared with eclplay's --field: the cells, header, records and RandSeed, which here is
the original's after its draws past SEED (the open ground's rolls).

Flow `slots` (step 4, part 11a: the combatants' icons loaded): the same two blocks (the fixture's dungeon, and
WILD_GROUNDS[0]) to the side panel's end (PANEL, 5045:084b: the turn's cursor and the panel drawn); the original's head layers (DS:a848, all 0), its party records' bodies and slots, each
combatant's slot, and the table's images are read, and eclplay's --icons and --screen must agree: the table byte for
byte against hlib's records (CBODY, CPIC, COMSPR), and the drawn combatants' icons composed over eclplay's screen equal the
original's view (pointer masked).

Flow `icons` (step 4, part 11b: the combatants' icons drawn): the runs of `slots` and the dungeon with the party on
FIELD_SQUARES[1] ((0, 3, 2), where a SHARKMAN stands in the view at (5, 4): Q2), to the same stop (PANEL). eclplay's --screen,
its icons drawn by the port, must equal the original's screenshot over the view (8, 8, 168 by 168) in RGB, the combatants'
cells not masked and only the mouse pointer (dqk_views.POINTER) masked, over the whole screen: 0 pixels differing (the
combat frame, 20b2:02fa, included). Gated: at least PARTY combatants drawn in the view in every run, no combatant at cell 0, 0,
and a monster (CPIC) drawn in the view in the square's run.

Every flow's box freezes the memory cache's purge draw (dqk_fight_lib.freeze_purges, a box-only
change at 27fa:0f2c), so the original's RandSeed moves only with its own draws, as eclplay's does;
the field flow (below) checks it at each square and records the purges.

The original's states go to build/dqk-fight/setup.json, with the heap pointers
masked (dqk_fight_lib.portable): later parts compare the port's state with them.
About 20 seconds.

    python3 tools/dosbox/checks/dqk_fight.py [--workdir DIR] [--verbose]
        [--flows setup,load,clear,six,ground,encounter,field,wild,slots,icons]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import json
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_ecl_text  # noqa: E402
import dqk_fight_lib as L  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_views  # noqa: E402
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
# The side panel's end (5045:084b, the return of 177e:085f that 5045:0846 calls after 5045:0827's
# 5837:12f5 has drawn the cursor's box, 5837:00f4): the screen the icons and slots flows compare, with
# the panel drawn (cok_arena_panel's hook, dark_queen.c's combat_panel). Trapping at the panel's entry
# (177e:085f) was the screen before the panel; the turn's own drawing comes between TURN and this.
# The Champions' panel address 6346:0af6 is not the Dark Queen's: traps at it (and at 6825:0af6, the
# map's stub) never held in 300 s.
PANEL = (0x5045, 0x084B)
# The command menu (step 4, part 10b: the party's dispatch, cok_combat_act's 5045:08cf): the
# same overlay copies as TURN, its offset 0x144 further on.
MENU = (0x5045, 0x08CF)
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
         field=None, at=None, blocks=None, turns=None, combat=None, state=None, sets=()):
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
    if turns is not None:
        extra += ["--turns", str(turns)]
    if difficulty is not None:
        extra += ["--set", f"27={difficulty:x}"]
    for assign in sets:
        extra += ["--set", assign]
    if combat is not None:
        extra += ["--combat", combat]
    if state is not None:
        extra += ["--state", str(state)]
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
# party list and the combat blocks are read where the port stops (5045:078b, the first turn,
# after the placement), the appends, the combat records and the placement done. A flow's monsters are
# those its steps leave after the last CLEARMONSTERS; the expected c6f1, ad6b, undead count
# and records follow from them.
CLEAR = "CLEAR"
PARTY = 6                      # the party's records come first in the list
# The MONCHA ids of the six-loads flow: each its own slot, and 170e's icon takes
# the seventh (slot 14). The ZOMBIE (37) and the WRAITH (36) are undead.
SIX_IDS = (64, 49, 41, 37, 36, 64)
# How long COMBAT may take to reach the placement after the LOAD MONSTERs (wall clock).
COMBAT_SECONDS = 300
# The battle's stop (step 4, part 10a): the battle starts, builds its battlefield, makes its combat
# records (59c7:0f7b), places them (59c7:1864), sets the view's origin and redraws the arena (51c2:0c94),
# runs 1dd7's tail (59c7:1e99), and then the round's start: 5045:059d's sides, 52db:0004 for each
# combatant (initiative), 5045:0447's turn list. It stops at the first turn's cursor (BATTLE_STOP, below;
# the dispatch 5045:078b comes after it). The original is trapped at 5045:078b too (TURN), so the whole state is final: the records,
# blocks, combatants, occupants, the view, the turn list and RandSeed compare. The trap is set after
# the block's last PRESS KEY, as 59c7 is loaded only once the battle starts.
# The first turn's stop (step 4, part 10b): 5045:078b's arena turn (5837:12f5, cursor on) draws the
# cursor's box (5837:00f4, ported: part 11b), then the side panel (177e:085f, dark_queen.c's
# combat_panel, ported), then the turn's dispatch (5045:08cf: the first combatant is a party
# member (TIMASALIN or BALTHIR NOMOON in these flows; CINDER in the repo test's walk), so its
# command menu is next, cited as part 22; the computer's turn 5bb8:0000 is not reached). eclplay stops there: the `dispatch` flow compares the original's state at 08cf with it.
# The first turn's command menu (5045:08cf, queen_commands.c) is read; the keys run out there, so
# eclplay ends the battle with the out-of-keys line, at the block-1 IP the menu's read stops at (it
# differs per flow: 802c, 8030, 8048, ...). No error line is written.
BATTLE_STOP = re.compile(r"\(out of keys in block 1 at [0-9a-f]{4}\)")
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
    def placed(self):
        """The monsters the placement keeps on the list (59c7:1864): the loaded ones. 170e's
        extras find no place there and the placement takes them off the list (59c7:1d57, which
        the battle's walk makes), so the list holds the party and these."""
        return sum(count for _, count in self.kept)

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


def turns_agree(v, original, path, tag):
    """eclplay's --turns file (n, index, initiative a line each: 5045:0447's list) against the
    original's turn list (dqk_fight_lib.turn_order), every entry in order and none missing."""
    ported = []
    if path.exists():
        for line in path.read_text().splitlines():
            ported.append(tuple(int(x) for x in line.split()))
    v.check(bool(original) and [tuple(t) for t in original] == ported,
            f"the turn list agrees at the first turn: each entry's record (its place in the list) "
            f"and initiative, in order (5045:0447){tag}",
            f"eclplay {ported}, the original {original}")


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
        box.keys("\\r")                 # the LOAD MONSTERs, COMBAT: appends, records, placement
        handle = at_placement(box)
        records = L.records(box)
        return dict(variables=block_vars(box),
                    turns=L.turn_order(box, records),
                    records=[raw.hex() for _, raw in records],
                    blocks=combat_blocks(box, records),
                    count=box.ds_byte(L.DS_MONSTER_COUNT),
                    icon_slot=box.ds_byte(L.DS_ICON_SLOT),
                    undead=box.ds_byte(L.DS_UNDEAD), traps=len(handle),
                    names=names_of([{"raw": raw.hex()} for _, raw in records]))


def placed(box):
    """The placement is done, not a false stop: the count (DS:c3ef) is at least 2 (the party's
    entry and one more) and the party's list has its head (DS:c6e5). A spin held where the
    placement is not yet made (a disk load, a copy of the overlay that is not the one executing)
    is waited out."""
    return box.ds_byte(L.DS_COMBATANT_COUNT) >= 2 and L.far(box, L.DS_PARTY) is not None


def at_placement(box, where=TURN):
    """The battle's stop (5045:078b, the first turn, or where: PANEL) trapped once the
    battle has its code (after the block's last key: the battle's overlay is loaded when it
    starts), and the spin held with the placement done (placed): COMBAT has made the records.
    Returns the trap's handle. Used by the load flows (load, clear, six, ground's and encounter's
    blocks). The wait is by a time limit, not a count of checks: a count of 180 ran out once under
    make test's load (each check settles a second of the game's time, which a loaded host gives
    in less host time, so the count covers less of COMBAT than it did idle). The records and
    blocks read after it are the placement's."""
    handle = L.trap(box, *where, until=lambda: True, seconds=COMBAT_SECONDS)
    L.frozen_at_trap(box)
    end, checks = time.monotonic() + COMBAT_SECONDS, 0
    while True:
        if L.held(box, handle)[0] and placed(box):
            return handle
        checks += 1
        if time.monotonic() > end:
            state = dqk_lib.ecl_state(box)
            raise cokbox.BoxError(
                f"COMBAT did not reach the battle's stop ({where[0]:04x}:{where[1]:04x}) in "
                f"{COMBAT_SECONDS} s "
                f"({checks} checks): ECL pc {state['pc']:#x}, opcode {state['opcode']:#x}, "
                f"keyboard polls {box.hook('polls')}")


def combat_blocks(box, records):
    """Each record's combat block as hex (0x18 bytes at +0x40, 59c7:0f7b), None for a NULL
    pointer; records are L.records' (pointer, raw) pairs."""
    return [None if cb is None else cb[1].hex()
            for cb in (L.combat_record(box, raw) for _, raw in records)]


def block_diffs(original, ported):
    """The bytes that differ between the original's blocks and eclplay's (list order): (record,
    offset, the original's, eclplay's), for the first few."""
    out = []
    for i, b in enumerate(original):
        if b is None or i >= len(ported):
            out.append((i, None, b, None))
            continue
        for k in range(COMBAT_BLOCK):
            if bytes.fromhex(b)[k] != bytes.fromhex(ported[i])[k]:
                out.append((i, k, bytes.fromhex(b)[k], bytes.fromhex(ported[i])[k]))
    return out


def blocks_agree(v, original, path, tag):
    """eclplay's --blocks file (the 0x18-byte blocks in list order) against the original's blocks
    at the placement: every block, the unnamed bytes 0 in both."""
    data = path.read_bytes() if path.exists() else b""
    ported = [data[i:i + COMBAT_BLOCK].hex() for i in range(0, len(data), COMBAT_BLOCK)]
    diffs = block_diffs(original, ported)
    v.check(not diffs and len(ported) == len(original) and len(data) % COMBAT_BLOCK == 0,
            f"every record's combat block agrees at the first turn (0x18 bytes at +0x40, the "
            f"unnamed bytes 0 in both){tag}",
            f"{len(ported)} against {len(original)}; (record, offset, original, eclplay): "
            + " ".join(f"({i}, {k}, {a}, {b})" for i, k, a, b in diffs[:12]))


def masked(raw_hex):
    """A record's bytes with its pointer fields zeroed (the port has none)."""
    raw = bytearray.fromhex(raw_hex)
    for i in POINTER_BYTES:
        raw[i] = 0
    return bytes(raw)


def load_run(v, work, opts, flow, difficulty):
    """One run of a load block: the original's (load_original), eclplay's from the
    same start (port, --records, --blocks), and the checks against it. Returns the original's
    readings and eclplay's records."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(load_block(flow.steps)))
    tag = "" if flow is LOAD and difficulty is None else f" ({flow.name}"
    if difficulty is not None:
        tag += f", difficulty {difficulty}"
    tag += ")" if tag else ""
    r = load_original(work, patched, flow, difficulty, opts.verbose)
    v.check(r["traps"] >= 1, f"the battle's first turn (5045:078b) is trapped{tag}",
            f"{r['traps']} copies")
    v.check(len(r["records"]) == PARTY + flow.placed and r["count"] == flow.appended and
            r["icon_slot"] == flow.icon_slot and r["undead"] == flow.undead,
            f"at the arena's redraw the list holds the party and {flow.placed} monsters (170e's "
            f"GORGONs taken off), c6f1 {flow.appended}, ad6b {flow.icon_slot} and undead "
            f"{flow.undead}{tag}",
            f"{len(r['records'])} records, c6f1 {r['count']}, ad6b {r['icon_slot']}, "
            f"undead {r['undead']}: " + " ".join(r["names"][PARTY:]))
    name = f"{flow.name}-{difficulty}"
    records = work / f"port-{name}.records"
    blocks = work / f"port-{name}.blocks"
    turns = work / f"port-{name}.turns"
    port_vars, port_last = port(work, patched, name=name, typed="\\r",
                                records=records, blocks=blocks, difficulty=difficulty,
                                turns=turns)
    diff = [a for a in range(1, dqk_ecl.VARIABLES + 1)
            if port_vars[a - 1] != r["variables"][a - 1]] \
        if len(port_vars) == dqk_ecl.VARIABLES else []
    v.check(len(port_vars) == dqk_ecl.VARIABLES and tuple(diff) == PORT_GAPS,
            f"eclplay's variables at the placement agree with the original's "
            f"(PORT_GAPS){tag}", f"differ: {' '.join(f'{a:#x}' for a in diff)}")
    v.check(BATTLE_STOP.fullmatch(port_last or "") is not None,
            f"eclplay ends at the battle's command menu (out of keys){tag}",
            port_last)
    # No icon error: the monsters' icons load as they register (CPIC, dark_queen.c's
    # icon_load), so the transcript's only error line is the battle's stop.
    transcript = (work / f"port-{name}.txt").read_text().splitlines()
    errors = [line for line in transcript if line.startswith("error:")]
    v.check(errors == [], f"eclplay's transcript has no error at the battle's menu{tag}",
            "; ".join(errors))
    data = records.read_bytes() if records.exists() else b""
    size = L.RECORD
    count, icon_slot, undead = (data[-3], data[-2], data[-1]) if len(data) >= 3 else (-1, -1, -1)
    ported = [data[i:i + size].hex() for i in range(0, len(data) - 3, size)]
    v.check(len(data) >= 3 and len(ported) == len(r["records"]) and
            count == r["count"] and icon_slot == r["icon_slot"] and undead == r["undead"],
            f"eclplay's party list has the original's records, c6f1, ad6b and undead{tag}",
            f"{len(ported)} records, c6f1 {count}, ad6b {icon_slot}, undead {undead}")
    # The records, whole (pointers masked): the monsters' must agree, and the party's must
    # agree at PARTY_GAPS (the import's bytes, before any monster is looked at).
    where = {i: tuple(k for k in range(L.RECORD)
                      if masked(ported[i])[k] != masked(r["records"][i])[k])
             for i in range(min(len(ported), len(r["records"])))}
    where = {i: ks for i, ks in where.items() if ks}
    values = {(i, k): (masked(r["records"][i])[k], masked(ported[i])[k])
              for i, ks in where.items() for k in ks}
    v.check(len(ported) == len(r["records"]) and
            all(not ks for i, ks in where.items() if i >= PARTY) and
            where == PARTY_GAPS,
            f"every monster's record agrees at the placement (pointers masked), and the party's "
            f"agree at PARTY_GAPS (none){tag}",
            "; ".join(f"{i}: " + " ".join(f"{k:#x} ({values[i, k][0]:#x}, eclplay {values[i, k][1]:#x})"
                                          for k in ks) for i, ks in sorted(where.items())))
    blocks_agree(v, r["blocks"], blocks, tag)
    turns_agree(v, r["turns"], turns, tag)
    return r, ported


def load(v, work, opts):
    """The load flow: the appends at the battle's start, in two runs (the entry's
    difficulty, and difficulty 1), each checked against the original and eclplay;
    and the difficulty changes the hostile monsters' hit points (the three
    LOAD MONSTER records, their copies included) and no GORGON's."""
    runs = {d: load_run(v, work, opts, LOAD, d) for d in LOAD.difficulties}
    base, low = runs[None][0]["records"], runs[1][0]["records"]
    monsters = range(PARTY, PARTY + LOAD.placed)
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
# The bottom bar's rows, where eclplay's command menu is drawn at the first turn (rows 176-199;
# the original's PANEL has none): masked in the screen compares at PANEL.
MENU_ROWS = (0, 176, 320, 24, "the command menu's rows (eclplay only at the first turn)")


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
    screen_masks = outside + masks + [MENU_ROWS]
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
# COMBAT the placement is trapped (5045:078b) and the monsters and party read, as the load
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
    """The original's run of the encounter block: at each of its five waits, the wait's
    pc and opcode, the block's variables and the screen (in colours); after COMBAT, the
    placement is trapped (5045:078b, the first turn, where COMBAT spins) and the party's records, their
    combat blocks and the monsters' count, icon slot and undead count are read there."""
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
        box.keys("\\r")                   # COMBAT: the lowering, the appends, the records
        handle = at_placement(box)
        records = L.records(box)
        battle = dict(records=[raw.hex() for _, raw in records],
                      blocks=combat_blocks(box, records),
                      turns=L.turn_order(box, records),
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
    the approaches leave. At the placement the party's and monsters' records, their combat
    blocks, c6f1, ad6b and the undead count are the load flow's (the two SHARKMEN and 170e's
    ten GORGONs), and eclplay stops at the placement."""
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
    blocks = work / "port-encounter.blocks"
    turns = work / "port-encounter.turns"
    port_vars, port_last = port(work, patched, name="encounter",
                                typed="\\r" * len(ENC_WAITS), records=records, blocks=blocks,
                                turns=turns)
    v.check(BATTLE_STOP.fullmatch(port_last or "") is not None,
            "eclplay ends at the battle's command menu (out of keys)", port_last)
    ported37 = port_vars[0x37 - 1] if len(port_vars) == dqk_ecl.VARIABLES else -1
    v.check(ported37 == battle["var37"] and battle["var37"] == wall_dist(waits),
            "COMBAT lowers var 0x37 to the open squares before the battle, in both",
            f"original {battle['var37']}, eclplay {ported37}, the wall's {wall_dist(waits)}")
    data = records.read_bytes() if records.exists() else b""
    count, icon_slot, undead = (data[-3], data[-2], data[-1]) if len(data) >= 3 else (-1, -1, -1)
    ported = [data[i:i + L.RECORD].hex() for i in range(0, len(data) - 3, L.RECORD)]
    v.check(len(ported) == len(battle["records"]) and count == battle["count"] and
            icon_slot == battle["icon_slot"] and undead == battle["undead"],
            "eclplay's party list at the placement has the original's records, c6f1, ad6b "
            "and undead", f"{len(ported)} records, c6f1 {count}, ad6b {icon_slot}, undead {undead}")
    where = [i for i in range(min(len(ported), len(battle["records"])))
             if masked(ported[i]) != masked(battle["records"][i])]
    v.check(not where and len(ported) == len(battle["records"]),
            "every record agrees at the placement (pointers masked; the party's too)",
            " ".join(str(i) for i in where))
    blocks_agree(v, battle["blocks"], blocks, "")
    turns_agree(v, battle["turns"], turns, " (encounter)")
    v.check(battle["count"] == ENCOUNTER.appended and battle["icon_slot"] == ENCOUNTER.icon_slot
            and battle["undead"] == 0 and len(battle["records"]) == PARTY + ENCOUNTER.placed,
            f"the arena's redraw has the party and {ENCOUNTER.placed} monsters (the SHARKMEN; "
            f"170e's ten GORGONs taken off), "
            f"c6f1 {battle['count']}, ad6b {battle['icon_slot']}, undead {battle['undead']}",
            " ".join(battle["names"][PARTY:]))


# The battlefield (step 4, parts 7 and 9a): the battle's setup builds the battlefield (59c7:1dd7:
# the tiles, then 06a7's dungeon from the 3D map; a battle outside 3D stops at 0e3b), makes the
# combat records (59c7:0f7b: each record's combat block, checked at every offset) and stops at
# the placement, 59c7:1864, and the view's origin (1dd7's 5837:0aea, 0b19), then after the arena's
# redraw (59c7:1e99) stops before the first turn (5045:078b). The original is trapped there: the
# view's origin (far DS:bedb +2, +4), the header (+6, +7, +8), the 50 by 25 cells (+9), the
# occupants (DS:b7fd), the combatants (DS:c3f1 + 6n: x, y, size), each record's combat block
# (0x18 bytes at +0x40) and
# RandSeed; eclplay --field and --blocks, at its stop, must hold the same. The trap moved from
# 0f7b (9a), to 1864's entry (9b-9c), to 1e99 once 1864 was ported (9c), and to 078b once the
# round's start was ported (part 10a: the battle's stop before the first turn).
# The dungeon draws no dice, and the memory cache's purges draw none in the box
# (dqk_fight_lib.freeze_purges), so RandSeed is the seed the battle began with, as in eclplay. The fixture's battle is in 3D (var 0x22 set), asserted; the
# origin is written after the placement (1dd7's 5837:0aea), so it is what the battle before left,
# and is compared as read.
FIELD_CELLS = 50 * 25
# The wild flow's grounds: (var 0x1a2, var 0x33) as the block stores them, values the shipped
# ECL stores in var 0x1a2 (blocks 64 and 66: 0, 1, 2, 4). Index 1 is flags 0x18 (tree chance 10,
# scatter's density 60) with no river; index 2 is flags 0x11 with the river on. Index 0 and 2
# have the same flags (0x00 and 0x11 build alike), so the second ground differs by its river.
WILD_GROUNDS = ((0x01, 0), (0x02, 1))
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
        handle = at_placement(box)      # the placement made (placed), not a false stop
        box.settled(tolerance=64)
        held, polls, polls_held = L.held(box, handle)
        listed = L.records(box)
        result = dict(traps=len(handle), held=held, polls=(polls, polls_held),
                      field=L.battlefield(box), seed=box.ds_dword(L.DS_SEED),
                      combatants=combatants_at(box),
                      purges=purges(box) - purges_at_seed,
                      records=[raw.hex() for _, raw in listed],
                      turns=L.turn_order(box, listed),
                      blocks=[None if cb is None else cb[1].hex()
                              for cb in (L.combat_record(box, raw) for _, raw in listed)],
                      mode=box.ds_word(L.DS_MODE), variables=block_vars(box))
        L.untrap(box, handle)
        return result


def combatants_at(box):
    """The combatants 1 to count - 1 (DS:c3ef is the count): (n, x, y, size) each, from
    DS:c3f1 + 6n. The entry at the count is the next one, not yet placed, and is not compared."""
    count = box.ds_byte(L.DS_COMBATANT_COUNT)
    return [(c["n"], c["x"], c["y"], c["size"]) for c in L.combatants(box) if c["n"] < count]


def s8(value):
    """A byte as the signed value the port keeps."""
    return value - 256 if value >= 128 else value


def parse_field(path):
    """eclplay --field's file: RandSeed, the view's origin (signed), the header's three bytes
    (cursor, cursor size, see-all), the cells in hex, the occupants in hex (25 rows), and the
    combatants (a count, then n x y size a line each)."""
    lines = path.read_text().splitlines()
    seed = int(lines[0].split()[1], 16)
    view = [int(v) for v in lines[1].split()[1:]]
    header = [int(v) for v in lines[2].split()[1:]]
    cells = "".join(lines[3:3 + 25])
    occupants = "".join(lines[28:28 + 25])
    count = int(lines[53].split()[1])
    combatants = [tuple(int(v) for v in line.split()) for line in lines[54:54 + count]]
    return seed, view, header, cells, occupants, combatants


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
    spins_at_placement(v, r)
    three_d = r["variables"][0x22 - 1]
    v.check(three_d != 0, f"the battle at {where} is in 3D (var 0x22 set): the dungeon builder ran",
            f"var 0x22 {three_d}")
    placement(v, work, patched, tag, where, square, r)


def wild_block(state, river):
    """Block 1 of the wild flow: the fight block, outside 3D (see dqkecl.fight's GROUND)."""
    b = Block()
    b.op("PRESS_KEY")
    dqkecl.fight(b, [(MONSTER, COUNT)], sprite=SPRITE, pause=True, ground=(state, river))
    b.op("EXIT")
    return b


def wild(v, work, opts):
    """The open ground flow (step 4, part 8): battles outside 3D, each (var 0x1a2, river) of
    WILD_GROUNDS, from the same saved game and seed, compared at the placement as `field`
    compares the dungeon's."""
    for k, (state, river) in enumerate(WILD_GROUNDS):
        wild_one(v, work, opts, state, river, f"wild{k}")


def wild_one(v, work, opts, state, river, tag):
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(state, river)))
    where = f"var 0x1a2 {state:#x}, river {river}"
    r = field_original(work, patched, opts.verbose, None, tag)
    spins_at_placement(v, r)
    variables = r["variables"]
    v.check(variables[0x22 - 1] == 0 and variables[0x1A2 - 1] == state and
            variables[0x33 - 1] == river,
            f"the battle at {where} is outside 3D (var 0x22 0) with the block's var 0x1a2 "
            f"and river (var 0x33): the open ground's builder runs (59c7:0e3b)",
            f"var 0x22 {variables[0x22 - 1]}, 0x1a2 {variables[0x1A2 - 1]}, "
            f"0x33 {variables[0x33 - 1]}")
    placement(v, work, patched, tag, where, None, r)


def spins_at_placement(v, r):
    v.check(r["traps"] >= 1 and r["held"],
            "the original spins at the battle's stop (5045:078b, or PANEL, 5045:084b, for the icons)",
            f"{r['traps']} copies, polls {r['polls'][0]} -> {r['polls'][1]}")
    v.check(r["mode"] == 5, "the battle's mode is 5 at the placement", f"mode {r['mode']}")


def menu_screen_agrees(v, original, path, where):
    """The command menu's whole screen (the original's at its wait, 08cf given back and settled)
    against eclplay's --screen at the menu's stop: only the mouse pointer masked."""
    if not path.exists():
        v.check(False, f"eclplay writes its screen at the menu at {where}")
        return
    port = cokbox.RGBScreen.load(path, SLOT_SCREEN)
    count, box = checklib.masked_diff(original, port, [MENU_POINTER])
    v.check(count == 0, f"the command menu's whole screen agrees with the original's at {where} "
            "(the menu's rows, the frame, the bar; the mouse pointer masked)",
            f"{count} pixels differ in {box}")


def placement(v, work, patched, tag, where, square, r):
    """eclplay's run to the first turn (5045:078b) against the original's state r: the battlefield,
    RandSeed (the original's, after the draws the battle made: the builders' and the initiative's),
    the records, blocks, turn list, the header and the view."""
    original = r["field"]
    cells = bytes.fromhex(original["cells"])
    path = work / f"port-{tag}.field"
    records = work / f"port-{tag}.records"
    blocks = work / f"port-{tag}.blocks"
    turns = work / f"port-{tag}.turns"
    menu = r.get("menu_screen")
    screen = work / f"port-{tag}.bmp" if menu is not None else None
    port_vars, port_last = port(work, patched, name=tag, typed="\\r\\r", field=path, at=square,
                                records=records, blocks=blocks, turns=turns, screen=screen)
    v.check(BATTLE_STOP.fullmatch(port_last or "") is not None,
            "eclplay ends at the battle's command menu (out of keys)", port_last)
    if not path.exists():
        v.check(False, "eclplay writes its battlefield at the stop", port_last)
        return
    if screen is not None:
        menu_screen_agrees(v, menu, screen, where)
    # Var 0x3c gates the records' effect 0x95 (59c7:0f7b:10cf): its value agrees, and the
    # records' effects (masked in the record compare) follow it; 0 at every square here.
    gate = r["variables"][0x3C - 1]
    got = port_vars[0x3C - 1] if len(port_vars) == dqk_ecl.VARIABLES else -1
    v.check(got == gate, f"var 0x3c (the records' effect gate) agrees at the placement at {where}",
            f"eclplay {got}, the original {gate}")
    seed, view, header, ported, occupants, combatants = parse_field(path)
    # RandSeed at the first turn: the round's start drew the initiative (one for one, and the
    # turn list's ties); the purges draw nothing (freeze_purges). Both must be the original's.
    n = L.draws(SEED, r["seed"])
    v.check(seed == r["seed"] and n is not None,
            f"RandSeed at the first turn is the original's, after {n} draws past SEED (the "
            f"initiative rolls and the turn list's ties; its memory cache purged {r['purges']} "
            f"times, each drawing nothing: dqk_fight_lib.freeze_purges) at {where}",
            f"original {r['seed']:#x} ({n} draws past SEED, {r['purges']} purges), "
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
    diffs = block_diffs(r["blocks"], ported_blocks)
    v.check(not diffs and len(ported_blocks) == len(r["blocks"]) and
            len(data) % COMBAT_BLOCK == 0,
            f"every record's combat block agrees at the first turn (0x18 bytes at +0x40, the "
            f"unnamed bytes 0 in both) at {where}",
            f"{len(ported_blocks)} against {len(r['blocks'])}; (record, offset, original, "
            f"eclplay): " + " ".join(f"({i}, {k}, {a}, {b})" for i, k, a, b in diffs[:12]))
    turns_agree(v, r["turns"], turns, f" at {where}")
    v.check(begun == 1, "eclplay prints \"A battle begins...\" once (no setup prelude: 59c7:1dd7 "
            "prints nothing)", f"{begun} times")
    v.check(header == original["header"],
            "the header's cursor, cursor size and see-all (+6, +7, +8) agree at the placement",
            f"eclplay {header}, the original's {original['header']}")
    expect = cells.hex()
    diff = [i for i in range(FIELD_CELLS) if ported[2 * i:2 * i + 2] != expect[2 * i:2 * i + 2]]
    v.check(not diff, f"the 50 by 25 cells agree (the builders' output) "
            f"at {where}",
            f"{len(diff)} cells differ, first {diff[:6]}" if diff else
            f"all {FIELD_CELLS} cells agree")
    # The origin (far DS:bedb +2, +4): 1dd7's 5837:0aea and 0b19 write it after the placement,
    # from the first record (the port's view_x, view_y), before the arena's redraw reads it.
    origin = [s8(original["view"][0]), s8(original["view"][1])]
    v.check(view == origin, f"the view's origin agrees at the arena's redraw at {where}",
            f"eclplay {view}, the original {origin}")
    # The combatants (DS:c3f1 + 6n: x, y, size, 59c7:1864) and the occupants (DS:b7fd, 0330's
    # footprints): every one, and the occupants at the same cells.
    # Every entry is compared, the size-0 ones (the GORGONs taken off the map, the failures) too:
    # the placement of each side's formations is per record (the cells a record takes are not
    # free for the next, 59c7:16b3), so their x and y agree as well.
    v.check(combatants == r["combatants"],
            f"every combatant's position and size agree (59c7:1864, the entries of size 0 too) "
            f"at {where}",
            f"eclplay {combatants}, the original {r['combatants']}")
    v.check(occupants == original["occupants"],
            f"the occupants agree (59c7:0330's footprints, the table DS:b7fd) at {where}",
            f"{sum(1 for i in range(0, len(occupants), 2) if occupants[i:i+2] != original['occupants'][i:i+2])} bytes differ")



# The combatants' icons (step 4, part 11a: dark_queen.c's icon table, loaded by the party's and
# the monsters' loads, and COMSPR's slots 0x0f-0x25 at the battle's set): at the placement the
# original's screenshot, the head layers (DS:a848: none in Dark Queen, 204c:00e5), each party
# member's and combatant's record, and the table's images are read; eclplay's --icons dump and
# --screen must agree: the table byte for byte against hlib's records, and the drawn combatants'
# icons composed over eclplay's screen (the combatants' cells as the original draws them, the
# pointer masked) equal the original's view. The original's screen is read here, not composed
# by eclplay's blit, so the composition is this check's own (the mirror of facing 4 to 7, the
# palette of the set and ALWAYS for colours 0-0xf, as scope.md's note).
ICON_SLOTS = 0x26                       # DS:b17d: 0x26 slots, two images each
ICON_MISSILES = range(0x0F, 0x26)      # the battle's COMSPR slots (59c7:0e80-0f06)
ICON_BODY_HIGH = 0x80                  # image 1 is record + 0x80 (004b:0140-0191)
ICON_CLIP = (8, 8, 176, 176)           # the view's ends, 5837:1367 (the blit's clip)
SLOT_SHARKMEN, SLOT_GORGON = 8, 9      # LOAD MONSTER 64 x2's slot, 170e's extra monsters
GORGON_VAR = 0x3A                      # var 0x3a: the index DS:75b8 takes (170e:17d3)
GORGON_TABLE = 0x75B8                  # DS:75b8, words by var 0x3a * 2
COMBAT_BLOCK_FACING = 0xA             # the combat record's facing (+0xa)
SLOT_SCREEN = (320, 200)


# The screen outside the view (the ground's rectangle, GROUND), which no icon is drawn over.
GX, GY, GW, GH = GROUND
OUTSIDE = [(0, 0, 320, GY, "outside the ground"),
           (0, GY + GH, 320, 200 - GY - GH, "outside the ground"),
           (0, GY, GX, GH, "outside the ground"),
           (GX + GW, GY, 320 - GX - GW, GH, "outside the ground")]


def shown_in_view(e):
    """Whether combatant e is drawn in the view (5837:0846's test, as the redraw makes it: size
    above 0, on the view, active (+0x18b) or status 10)."""
    raw = e["raw"]
    if raw is None or e["size"] == 0 or not (0 <= e["sx"] < 7 and 0 <= e["sy"] < 7):
        return False
    return bool(raw[0x18B] or struct.unpack_from("<H", raw, 0x64)[0] == 10)


def slots_original(work, patched, tag, verbose, square=None, where=TURN):
    """The original at the placement (field_original's keys and trap): the screenshot, DS:a848
    (0x26 bytes), the party's records (+0xc4, +0xc5), each combatant 1..count-1 (its record's
    +0x9b, +0xbd, +0xc4, +0xc5, +0x18b, status +0x64 and combat facing; sx, sy at DS:aeac/aee0 +
    n; size at DS:c3f1 + 6n + 5), the set the battle drew (DS:ca03, var 0x22) and the GORGON's
    CPIC id (DS:75b8 by var 0x3a). SQUARE, if given, is the party's (x, y, facing) at DS:c9f0."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        if square is not None:
            box.ds_write(SQUARE, bytes(square))
        box.set_seed(SEED)
        box.keys("\\r")
        box.settled(tolerance=64)
        box.keys("\\r")
        box.settled(tolerance=64)
        box.keys("\\r")                 # SETUP MONSTERS, APPROACH, COMBAT: the battle begins
        handle = at_placement(box, where)   # the placement made (placed), not a false stop
        box.settled(tolerance=64)
        held, polls, polls_held = L.held(box, handle)
        screen = box.screenshot(path=str(work / f"{tag}-original.png"))
        variables = block_vars(box)
        party = [raw for _, raw in L.records(box)[:PARTY]]
        count = box.ds_byte(L.DS_COMBATANT_COUNT)
        combatants = []
        for n in range(1, count):
            off, seg = struct.unpack("<HH", box.ds_read(0xB2F8 + 4 * n, 4))
            raw = None if (off, seg) == (0, 0) else box.read(seg * 16 + off, L.RECORD)
            facing = None
            if raw is not None:
                cb = L.combat_record(box, raw)
                facing = None if cb is None else cb[1][COMBAT_BLOCK_FACING]
            combatants.append(dict(
                n=n, raw=raw, facing=facing,
                sx=L.signed(box.ds_byte(0xAEAC + n)), sy=L.signed(box.ds_byte(0xAEE0 + n)),
                size=box.ds_byte(L.DS_POSITIONS + 6 * n + 5)))
        result = dict(traps=len(handle), held=held, polls=(polls, polls_held),
                      mode=box.ds_word(L.DS_MODE), screen=screen,
                      a848=box.ds_read(0xA848, ICON_SLOTS),
                      party=party, combatants=combatants,
                      wall=box.ds_byte(GROUND_SET_RECORD),
                      three_d=variables[0x22 - 1],
                      gorgon=box.ds_word(GORGON_TABLE + 2 * variables[GORGON_VAR - 1]))
        L.untrap(box, handle)
        return result


def read_icons(path):
    """eclplay --icons: {(slot, image): record bytes, or None where the slot is empty}."""
    icons = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        slot, image = int(parts[0]), int(parts[1])
        empty = len(parts) < 3 or parts[2] == "-"
        icons[(slot, image)] = None if empty else bytes.fromhex(parts[2])
    return icons


def slots_palette(set_name):
    """RGB of each palette index as scope.md's note: the set's record 0 (DUNGCOM or WILDCOM set 4)
    for 0x20-0xff, ALWAYS's record 0 for 0-0xf; each 8-bit value v is ((v>>2)<<2) | ((v>>2)>>4)."""
    def rgb_of(record):
        first, colours, _ = hlib.palette(record)
        return {first + i: tuple(((v >> 2) << 2) | ((v >> 2) >> 4) for v in c)
                for i, c in enumerate(colours)}
    table = dict(rgb_of(dict(hlib.load(dqk_lib.ASSETS / "DISK1" / "ALWAYS.TLB").entries())[0]))
    library = hlib.load(dqk_lib.ASSETS / "DISK2" / f"{set_name.upper()}.TLB")
    table.update(rgb_of(dict(hlib.Library(dict(library.entries())[4]).entries())[0]))
    return table


def slots_port(work, patched, tag, square=None):
    """eclplay's run to the placement as `placement` runs it, with --icons and --screen (and
    --at, the party's square, if given)."""
    icons, screen = work / f"port-{tag}.icons", work / f"port-{tag}.bmp"
    extra = ["--load", str(saved_entry(work)), "--play", "--icons", str(icons),
             "--screen", str(screen)]
    if square is not None:
        extra += ["--at", "%d,%d,%d" % square]
    variables, last, _ = dqk_ecl.port(work, f"port-{tag}", patched, typed="\\r\\r", extra=extra,
                                      block=None)
    return variables, last, icons, screen


def slots(v, work, opts):
    """The combatants' icons flow (step 4, part 11a) at the battle's placement, for the
    fixture's dungeon and WILD_GROUNDS[0] (the open ground), each run against eclplay."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    cbody = dict(hlib.load(dqk_lib.ASSETS / "DISK3" / "CBODY.TLB").entries())
    cpic = dict(hlib.load(dqk_lib.ASSETS / "DISK2" / "CPIC.TLB").entries())
    comspr = dict(hlib.load(dqk_lib.ASSETS / "DISK2" / "COMSPR.TLB").entries())
    runs = (("slots0", fight_block(), "the fixture's dungeon"),
            ("slots1", wild_block(*WILD_GROUNDS[0]), f"the open ground {WILD_GROUNDS[0]}"))
    for tag, block, where in runs:
        patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
        r = slots_original(work, patched, tag, opts.verbose, where=PANEL)
        spins_at_placement(v, r)
        set_name = "dungcom" if r["three_d"] != 0 else "wildcom"
        v.check(r["wall"] == 4, f"the battle's set at {where} is set 4 (DS:ca03)",
                f"set {r['wall']}")
        v.check(all(b == 0 for b in r["a848"]),
                f"the head layers DS:a848 are all 0 at {where} (no CHEAD in Dark Queen: "
                f"204c:00e5)", r["a848"].hex())
        v.check(len(r["party"]) == PARTY, f"the original's party list holds {PARTY} records at "
                f"{where}", f"{len(r['party'])}")
        _, last, icons_path, screen_path = slots_port(work, patched, tag)
        v.check(BATTLE_STOP.fullmatch(last or "") is not None,
                f"eclplay ends at the first turn's command menu at {where}", last)
        if not icons_path.exists() or not screen_path.exists():
            v.check(False, f"eclplay writes its icons and screen at {where}", last)
            continue
        icons = read_icons(icons_path)
        # (b) the table: each party member's body, the monsters' and 170e's, and COMSPR.
        bad = []
        for k, raw in enumerate(r["party"]):
            body, slot = raw[0xC4], raw[0xC5]
            for image, record in ((0, body), (1, body + ICON_BODY_HIGH)):
                if icons.get((slot, image)) != cbody.get(record):
                    bad.append(f"member {k} slot {slot} image {image} (CBODY {record})")
        v.check(not bad, f"each party member's slot holds CBODY +0xc4 and +0xc4 + 0x80 at {where}",
                "; ".join(bad))
        want = {(SLOT_SHARKMEN, 0): cpic.get(64), (SLOT_SHARKMEN, 1): cpic.get(64 + ICON_BODY_HIGH),
                (SLOT_GORGON, 0): cpic.get(r["gorgon"]),
                (SLOT_GORGON, 1): cpic.get(r["gorgon"] + ICON_BODY_HIGH)}
        differ = [f"{key}" for key, rec in want.items() if icons.get(key) != rec]
        v.check(not differ,
                f"the SHARKMEN's slot 8 holds CPIC 64 and 192, 170e's slot 9 the GORGON's CPIC id "
                f"{r['gorgon']} and +0x80 at {where}", ", ".join(differ))
        missiles = [k for k in ICON_MISSILES for image in (0, 1)
                    if icons.get((k, image)) != comspr.get(k - 15 + image * ICON_BODY_HIGH)]
        v.check(not missiles, f"slots 0x0f-0x25 hold COMSPR k-15 and +0x80 at {where}",
                " ".join(map(str, missiles)))
        # (c) the view: each drawn combatant's icon composed over eclplay's screen.
        port = cokbox.RGBScreen.load(screen_path, SLOT_SCREEN)
        palette = slots_palette(set_name)
        pixels = bytearray(port.pixels)
        drawn = []
        for e in r["combatants"]:
            raw = e["raw"]
            if not shown_in_view(e):
                continue
            drawn.append(e["n"])
            record = icons.get((raw[0xC5], 0))
            if record is None:
                continue
            _, _, _, _, _, rows = hlib.picture(record, None)
            flip = e["facing"] is not None and e["facing"] > 3
            for j, row in enumerate(rows):
                if flip:
                    row = row[::-1]
                for i, value in enumerate(row):
                    x, y = 8 + 24 * e["sx"] + i, 8 + 24 * e["sy"] + j
                    if value is None or x >= ICON_CLIP[2] or y >= ICON_CLIP[3]:
                        continue
                    at = 3 * (y * SLOT_SCREEN[0] + x)
                    pixels[at:at + 3] = bytes(palette.get(value, (255, 0, 255)))
        v.check(len(drawn) >= PARTY, f"at least {PARTY} combatants are drawn in the view at "
                f"{where}", f"{len(drawn)}: {drawn}")
        composed = cokbox.RGBScreen(SLOT_SCREEN[0], SLOT_SCREEN[1], bytes(pixels))
        count, box = checklib.masked_diff(r["screen"], composed, OUTSIDE + [GROUND_POINTER, MENU_ROWS])
        v.check(count == 0,
                f"the view agrees with the original's, the combatants' icons composed over "
                f"eclplay's screen (the pointer masked) at {where} ({len(drawn)} combatants drawn: "
                f"{drawn})",
                f"{count} pixels differ in {box}")


# The combatants' icons drawn (step 4, part 11b: arena.c's cursor and pose, dark_queen.c's
# arena_icon, queen_arena.c's blit): the same runs as `slots`, to the side panel's end (PANEL). The
# port's own screen (eclplay --screen, its icons drawn by the port, the combat frame too) must
# equal the original's screenshot over the whole screen in RGB, with only the mouse pointer
# masked (dqk_views.POINTER: the DOSBox pointer's arrow at 2-15 by 2-16). The
# COMSPR cursor box (the turn's cursor, 5837:00f4) and the side panel are drawn at PANEL, so both are
# in the compare; the original is trapped at the same point (PANEL), not at TURN, which is before them.
ICON_MONSTERS = range(0x08, 0x0F)       # the monsters' slots (DS:ad6b: 8-0x0e, 170e's and 29fc's)
def icon_runs():
    """(tag, block, where, square): the fixture's dungeon; the open ground WILD_GROUNDS[0]; and
    the dungeon with the party on FIELD_SQUARES[1], where a SHARKMAN (slot 8, CPIC 64) stands in
    the view at (5, 4) (the original, read at DS:aeac/aee0 + n: Q2), so that a CPIC icon is drawn."""
    return (("icons0", fight_block(), "the fixture's dungeon", None),
            ("icons1", wild_block(*WILD_GROUNDS[0]), f"the open ground {WILD_GROUNDS[0]}", None),
            ("icons2", fight_block(), f"the square {FIELD_SQUARES[1]}", FIELD_SQUARES[1]))


def icons(v, work, opts):
    """The combatants' icons drawn (part 11b) at the battle's placement, for the fixture's dungeon
    and WILD_GROUNDS[0]: the original's screenshot against eclplay's --screen over the view."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    for tag, block, where, square in icon_runs():
        patched = hlib.replace(glb, JUMP, dqkecl.assemble(block))
        r = slots_original(work, patched, tag, opts.verbose, square, where=PANEL)
        spins_at_placement(v, r)
        _, last, _, screen_path = slots_port(work, patched, tag, square)
        v.check(BATTLE_STOP.fullmatch(last or "") is not None,
                f"eclplay ends at the first turn's command menu at {where}", last)
        if not screen_path.exists():
            v.check(False, f"eclplay writes its screen at {where}", last)
            continue
        port = cokbox.RGBScreen.load(screen_path, SLOT_SCREEN)
        drawn = [e for e in r["combatants"] if shown_in_view(e)]
        monsters = [e["n"] for e in drawn if e["raw"][0xC5] in ICON_MONSTERS]
        v.check(len(drawn) >= PARTY, f"at least {PARTY} combatants are drawn in the view at "
                f"{where}", f"{len(drawn)}: {[e['n'] for e in drawn]}")
        if square is not None:
            v.check(len(monsters) >= 1, f"a monster (CPIC) is drawn in the view at {where}",
                    f"{len(monsters)}")
        corner = [e["n"] for e in drawn if e["sx"] == 0 and e["sy"] == 0]
        v.check(not corner, f"no combatant stands at cell 0, 0 (the pointer's mask would hide its "
                f"icon) at {where}", f"{corner}")
        count, box = checklib.masked_diff(r["screen"], port, [dqk_views.POINTER, MENU_ROWS])
        v.check(count == 0,
                f"the whole screen agrees with the original's, the combatants' icons and the frame "
                f"not masked (the mouse pointer only) at {where} ({len(drawn)} combatants drawn, "
                f"{len(monsters)} monsters: {monsters})",
                f"{count} pixels differ in {box}")


def snapshot(box):
    """The state the placement comparison reads (field_original's keys) and L.state's, at the
    box's stop: the battlefield, RandSeed, the combatants, the records, blocks and turn list,
    the mode and the variables."""
    listed = L.records(box)
    return dict(field=L.battlefield(box), seed=box.ds_dword(L.DS_SEED),
                combatants=combatants_at(box), records=[raw.hex() for _, raw in listed],
                turns=L.turn_order(box, listed),
                blocks=[None if cb is None else cb[1].hex()
                        for cb in (L.combat_record(box, raw) for _, raw in listed)],
                mode=box.ds_word(L.DS_MODE), variables=block_vars(box), state=L.state(box))


MENU_POINTER = (8, 8, 10, 10, "the mouse pointer at the command menu's wait (the arrow)")
KEYS = ("E", "G", "D")          # End, Guard, Delay: the keys typed at the first turn's menu
COMPUTER = (0x5045, 0x0891)     # the computer's dispatch: push [bp+8] before its lcall 1d99:0ff9 (078b's copies + 0x106)
TAIL_SECONDS = 120


def to_menu(box):
    """The first turn's command menu (step 4, part 10b) in an entered box, RandSeed set: the
    turn's trap (5045:078b, held) is given back, and 5045:08cf, the party's dispatch, is spun in
    the same copies (EB FE over its two bytes at +0x144, the routine's entry). The state is read
    at the turn (078b) and at the menu (08cf), and DS:ad67, the selected record, at the menu. Then
    08cf is given back, the menu drawn and waiting (the emulation paused). The returned keys are
    snapshot's, at the menu (placement's), with the turn's state as `turn`, the menu spin's
    `menu_held`, `menu_polls`, `menu_traps`, `handle` (078b's copies) and `purges`."""
    purges_at_seed = purges(box)
    box.keys("\\r")                 # the 'monsters' PRESS KEY (field_original's keys)
    box.settled(tolerance=64)
    box.keys("\\r")                 # the 'loaded' PRESS KEY
    box.settled(tolerance=64)
    box.keys("\\r")                 # SETUP MONSTERS, APPROACH, COMBAT: the battle begins
    handle = at_placement(box)      # the placement made (placed), the turn's spin held
    box.settled(tolerance=64)
    held, polls, polls_held = L.held(box, handle)
    turn = snapshot(box)
    menu = [(addr + MENU[1] - TURN[1], None) for addr, _ in handle]
    menu = [(addr, box.read(addr, 2)) for addr, _ in menu]
    for addr, _ in menu:
        box.write(addr, b"\xeb\xfe")
    L.untrap(box, handle)           # 078b given back: the turn runs on to 08cf
    menu_held, menu_polls, menu_polls_held = L.held(box, menu, seconds=1.5)
    at_menu = snapshot(box)
    selected = "%04x:%04x" % (box.ds_word(0xAD69), box.ds_word(0xAD67))
    box.write_paused(menu)
    box.settled(quiet=1.0, tolerance=0)   # the menu drawn, waiting for a key
    return dict(traps=len(handle), held=held, polls=(polls, polls_held),
                menu_traps=len(menu), menu_held=menu_held,
                menu_polls=(menu_polls, menu_polls_held), selected=selected,
                purges=purges(box) - purges_at_seed, turn=turn, handle=handle, **at_menu)


def dispatch_original(work, patched, verbose, tag="dispatch"):
    """The original's run to the first turn's dispatch (step 4, part 10b): to_menu, with the
    menu's screen kept (`menu_screen`, the PNG under work)."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        r = to_menu(box)
        r["menu_screen"] = box.screenshot(path=str(work / f"{tag}-menu.png"))
        return r


def arm(box, handle, routine):
    """The routine at SEG:OFF of 5045 made to spin where the copies of the turn's trap (to_menu's
    handle, 5045:078b) put it: EB FE over its two bytes, written paused (the CPU stands at the
    spin's jump). Returns the copies (address, the two bytes it replaces)."""
    t = [(addr + routine[1] - TURN[1], box.read(addr + routine[1] - TURN[1], 2)) for addr, _ in handle]
    box.write_paused([(a, b"\xeb\xfe") for a, _ in t])
    return t


def tail_state(box):
    """The turn's tail and the round's end bytes the flows read: DS:bebd (the damage type), bd4c,
    ad6e and ad6f (the pending counts), c2d6 (the cloud list's head), ae4c (the round count), 1cb5
    (the back page)."""
    return dict(bebd=box.ds_word(0xBEBD), bd4c=box.ds_byte(0xBD4C), ad6e=box.ds_byte(0xAD6E),
                ad6f=box.ds_byte(0xAD6F), c2d6="%04x:%04x" % (box.ds_word(0xC2D8), box.ds_word(0xC2D6)),
                ae4c=box.ds_byte(0xAE4C), begun=box.ds_byte(L.DS_BEGUN))


def selected(box):
    """DS:ad67, the selected record (the turn's combatant), as snapshot's records name them."""
    return "%04x:%04x" % (box.ds_word(0xAD69), box.ds_word(0xAD67))


def keys_original(work, patched, key, verbose):
    """The original at the first turn's menu (to_menu), the key typed there (step 4, part 22a: End,
    Guard or Delay) and the next combatant's command menu reached: 5045:08cf, trapped where the
    dispatch's copies start. The state there (`stop`, with DS:ad67 as `selected`) and the trap given
    back, the menu drawn and waiting (`menu_screen2`), as the placement's menu."""
    tag = f"keys{key}"
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        menu["menu_screen"] = box.screenshot(path=str(work / f"{tag}-menu.png"))
        t = arm(box, menu["handle"], MENU)
        box.keys(key)
        wait_trap(box, t, "5045:08cf")
        stop = snapshot(box)
        stop.update(tail_state(box), selected=selected(box))
        stop_screen = box.screenshot(path=str(work / f"{tag}-stop.png"))
        L.untrap(box, t)
        box.settled(quiet=1.0, tolerance=0)
        menu_screen2 = box.screenshot(path=str(work / f"{tag}-menu2.png"))
        return dict(menu=menu, stop=stop, stop_screen=stop_screen, menu_screen2=menu_screen2)


def dispatch(v, work, opts):
    """The first turn's dispatch (step 4, part 10b): the original's state at the command menu
    (5045:08cf, after 078b is given back) against eclplay's stop, which is the command menu too
    (BATTLE_STOP): the same records, blocks, combatants, battlefield, turn list and RandSeed as at
    the turn. The selected record (DS:ad67) is the turn list's first combatant. The computer's
    dispatch (5045:0891, 5bb8:0000) is not reached: the first turn is the party's."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    r = dispatch_original(work, patched, opts.verbose)
    spins_at_placement(v, r)
    v.check(r["menu_traps"] >= 1 and r["menu_held"],
            "the original's dispatch spins at the command menu (5045:08cf) once 078b is given "
            "back: EB FE in the same copies, the polls hold over 1.5 s",
            f"{r['menu_traps']} copies, polls {r['menu_polls'][0]} -> {r['menu_polls'][1]}")
    same = [k for k in r["turn"]["state"] if r["turn"]["state"][k] != r["state"][k]]
    v.check(not same, "the state at the command menu is the state at the first turn (records, "
            "blocks, combatants, battlefield, turn list, RandSeed)",
            f"differ: {', '.join(same) or 'none'}")
    first = r["state"]["first"]
    v.check(r["selected"] == first and first is not None,
            "the selected record (DS:ad67) at the command menu is the turn list's first combatant "
            "(DS:bd9d)", f"selected {r['selected']}, first {first}")
    placement(v, work, patched, "dispatch", "the first turn's dispatch (5045:08cf)", None, r)


KEY_TURN = {"E": 2, "G": 2, "D": 1}   # DS:bd2d at the next combatant (the turn list's entry n)
KEY_DRAWS = {"E": 0, "G": 1, "D": 0}  # RandSeed draws past the menu's (Guard's message's wait)
NEXT_TURN = "turn: ROARK 'CROAKER' (initiative 7)"  # the turn list's last line: the next combatant


def turn_record(stop):
    """The record of the turn list's entry for DS:bd2d (n == the index, snapshot's turns), as the
    state names it: its place in the party list (the state's records, in that order)."""
    entry = [e for e in stop["turns"] if e[0] == stop["state"]["turn"]]
    if not entry:
        return None
    return stop["state"]["records"][entry[0][1]]["at"]


def keys(v, work, opts):
    """The command menu's keys (step 4, part 22a): End, Guard and Delay each typed at the first
    turn's menu. The original's menu (the placement's comparison, then the next combatant's menu:
    5045:08cf, after the key), against eclplay's run to the same menu (BATTLE_STOP)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    for key in KEYS:
        tag = f"keys{key}"
        r = keys_original(work, patched, key, opts.verbose)
        spins_at_placement(v, r["menu"])
        placement(v, work, patched, tag, f"the first turn's menu before {key}", None, r["menu"])
        text = (work / f"port-{tag}.txt").read_text().splitlines()
        v.check("menu: Aim Use Guard Quick@Delay View Speed End" in text,
                f"the menu's built text is \"Aim Use Guard Quick@Delay View Speed End\" (before {key})",
                [line for line in text if line.startswith("menu:")][:1])
        t = r["stop"]
        draws = L.draws(r["menu"]["seed"], t["seed"])
        v.check(draws == KEY_DRAWS[key], f"the original's RandSeed past the menu after {key} is "
                f"{KEY_DRAWS[key]} draw(s) (Guard's message's wait, 1df9:052a)",
                f"{draws} draws")
        v.check(t["state"]["turn"] == KEY_TURN[key],
                f"the original's turn index (DS:bd2d) at the next combatant after {key} is "
                f"{KEY_TURN[key]}", f"{t['state']['turn']}")
        record = turn_record(t)
        v.check(record is not None and record == t["selected"],
                f"the selected record (DS:ad67) after {key} is the turn list's entry for DS:bd2d",
                f"selected {t['selected']}, entry {record}")
        base = work / f"port-{tag}-next"
        variables, port_last = port(work, patched, name=f"{tag}-next", typed="\\r\\r" + key,
                                    field=base.with_suffix(".field"),
                                    records=base.with_suffix(".records"),
                                    blocks=base.with_suffix(".blocks"),
                                    turns=base.with_suffix(".turns"),
                                    screen=base.with_suffix(".bmp"))
        v.check(BATTLE_STOP.fullmatch(port_last or "") is not None,
                f"eclplay stops at the next combatant's menu after {key}", port_last)
        lines = (work / f"port-{tag}-next.txt").read_text().splitlines()
        v.check([line for line in lines if line.startswith("turn:")][-1:] == [NEXT_TURN],
                f"eclplay's last turn after {key} is ROARK (the turn list's next entry)",
                [line for line in lines if line.startswith("turn:")][-1:])
        command_state(v, tag, t, dict(base=base, last=port_last), r["menu"]["seed"])
        v.check(variables == t["variables"], f"the variables agree at the next combatant after {key}"
                " (the 1 KB at DS:b20f)", "differ" if variables != t["variables"] else "")
        command_screen(v, f"after the menu is given back after {key}", r["menu_screen2"],
                       base.with_suffix(".bmp"), MENU_POINTER)
    commands(v, work, patched, opts)


# The command menu's commands (step 4, part 22b: Quick, Alt-A and Alt-Q, Alt-M, Speed). Each run
# is the first turn's menu (to_menu), the flow's keys typed there, and the stop where the routine
# hands the turn on: Quick at the computer's turn (5045:0a56, the lcall 2d99:0ff9), Alt-Q at the
# tail (5045:066a, flipped as keys_original's), Alt-M and Speed where the menu is built again
# (5045:0cd7, after c8c's arena). The values are the review's (dqk-commands-b): the original and
# eclplay agree on each.
QUICK_TRAP = 0x0A56
MENU_TRAP = 0x0CD7
MAGIC = 0xB1CB                          # DS:b1cb: the magic (Alt-M toggles it, 0bd7)
SPEED_VAR = 0x12                        # var 0x12: the game speed (Faster and Slower, 166c)
QUICK_AUTO = (0, 1, 0, 0, 0, 0, 1, 1)   # +0x18c of the eight records after Quick (record 1 is the turn's)
ALT_Q_AUTO = (1, 1, 1, 1, 1, 1, 1, 1)   # after Alt-Q every record of the list, the monsters too
TURN_INITIATIVE = 3                     # the combat block's initiative (+3)
QUICK_INITIATIVE = 9                    # record 1's initiative, kept by Quick
ALT_Q_INITIATIVE = 0x13                 # record 1's initiative after Alt-Q (0c0f, then 078b's 0x14 -> 0x13)
COMPUTER_STOP = re.compile(r".*5bb8:0000.*")


def wait_trap(box, t, where):
    """Waits until the trap at WHERE (its copies T, written paused) is reached: the keyboard
    polls hold over 0.6 s (as keys_original's tail)."""
    end = time.monotonic() + TAIL_SECONDS
    while time.monotonic() < end:
        box.settle(0.5)
        if L.held(box, t, seconds=0.6)[0]:
            return
    raise cokbox.BoxError(f"the original's trap at {where} is not reached after {TAIL_SECONDS} s")


def var12(box):
    return block_vars(box)[SPEED_VAR - 1]


def command_original(work, patched, flow, verbose):
    """The original at the first turn's menu (to_menu, the menu's screen kept), FLOW's keys typed
    there, and its stop: Quick (Q) at 5045:0a56, Alt-Q (\\@Q) at the tail 5045:066a, flipped as
    keys_original's tail, Alt-M (\\@M) and Speed (S, F, E) at 5045:0cd7, the menu built again. The
    stop's state and screen; Speed's bar screens after S and after F; the menu's wait screen after
    the trap is given back (Alt-M, Speed)."""
    tag = f"cmd-{flow}"
    trap = {"Q": QUICK_TRAP, "AltQ": COMPUTER[1]}.get(flow, MENU_TRAP)
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        menu["menu_screen"] = box.screenshot(path=str(work / f"{tag}-menu.png"))
        menu["magic"] = box.ds_byte(MAGIC)
        menu["var12"] = menu["variables"][SPEED_VAR - 1]
        t = [(addr + trap - TURN[1], box.read(addr + trap - TURN[1], 2)) for addr, _ in menu["handle"]]
        box.write_paused([(addr, b"\xeb\xfe") for addr, _ in t])
        out = dict(menu=menu)
        if flow == "SFE":
            box.keys("S")
            box.settled(quiet=1.0, tolerance=0)
            out["speed"] = dict(screen=box.screenshot(path=str(work / f"{tag}-speed.png")),
                                var12=var12(box))
            box.keys("F")
            box.settled(quiet=1.0, tolerance=0)
            out["speedF"] = dict(screen=box.screenshot(path=str(work / f"{tag}-speedF.png")),
                                 var12=var12(box))
            box.keys("E")
        else:
            box.keys({"Q": "Q", "AltQ": "\\@Q", "AltM": "\\@M"}[flow])
        wait_trap(box, t, f"5045:{trap:04x}")
        out["stop"] = snapshot(box)
        out["stop_magic"] = box.ds_byte(MAGIC)
        out["stop_var12"] = var12(box)
        out["stop_begun"] = box.ds_byte(L.DS_BEGUN)
        out["stop_screen"] = box.screenshot(path=str(work / f"{tag}-stop.png"))
        if flow in ("Q", "AltQ"):
            out["flipped"] = L.flip(box, t)
            out["flip_screen"] = box.screenshot(path=str(work / f"{tag}-flip.png"))
        else:
            L.untrap(box, t)
            box.settled(quiet=1.0, tolerance=0)
            out["menu_again"] = dict(screen=box.screenshot(path=str(work / f"{tag}-menu2.png")),
                                     seed=box.ds_dword(L.DS_SEED))
    return out


def command_port(work, patched, name, typed):
    """eclplay's run to the stop of NAME (the first turn's menu, then TYPED): its variables, its
    last line, its text, and the files its state is read from (base: field, records, blocks,
    turns, bmp)."""
    base = work / f"cmd-{name}"
    variables, last = port(work, patched, name=f"cmd-{name}", typed="\\r\\r" + typed,
                           field=base.with_suffix(".field"), records=base.with_suffix(".records"),
                           blocks=base.with_suffix(".blocks"), turns=base.with_suffix(".turns"),
                           screen=base.with_suffix(".bmp"))
    text = (work / f"port-cmd-{name}.txt").read_text().splitlines()
    return dict(base=base, vars=variables, last=last, text=text)


def port_var12(p):
    return p["vars"][SPEED_VAR - 1] if len(p["vars"]) == dqk_ecl.VARIABLES else -1


def command_state(v, tag, s, p, m_seed):
    """The stop's state against eclplay's: the RandSeed, the records (pointers masked), the
    combat blocks and the turn list. Returns the original's RandSeed draws past the menu's."""
    draws = L.draws(m_seed, s["seed"])
    field = p["base"].with_suffix(".field")
    if not field.exists():
        v.check(False, f"eclplay writes its battlefield at the stop after {tag}", p["last"])
    else:
        seed = parse_field(field)[0]
        v.check(seed == s["seed"], f"RandSeed at the stop after {tag} is the original's "
                f"({s['seed']:#x})", f"eclplay {seed:#x}")
    data = p["base"].with_suffix(".records").read_bytes() if p["base"].with_suffix(".records").exists() else b""
    ported = [data[i:i + L.RECORD].hex() for i in range(0, len(data) - 3, L.RECORD)]
    differ = [i for i in range(min(len(ported), len(s["records"])))
              if masked(ported[i]) != masked(s["records"][i])]
    v.check(not differ and len(ported) == len(s["records"]),
            f"every record agrees at the stop after {tag} (pointers masked)",
            f"{len(ported)} against {len(s['records'])}; differ: {' '.join(map(str, differ))}")
    path = p["base"].with_suffix(".blocks")
    data = path.read_bytes() if path.exists() else b""
    blocks = [data[i:i + COMBAT_BLOCK].hex() for i in range(0, len(data), COMBAT_BLOCK)]
    diffs = block_diffs(s["blocks"], blocks)
    v.check(not diffs and len(blocks) == len(s["blocks"]),
            f"every record's combat block agrees at the stop after {tag} (0x18 bytes)",
            f"{len(blocks)} against {len(s['blocks'])}; (record, offset, original, eclplay): " +
            " ".join(f"({i}, {k}, {a}, {b})" for i, k, a, b in diffs[:12]))
    turns_agree(v, s["turns"], p["base"].with_suffix(".turns"), f" at the stop after {tag}")
    return draws


def command_screen(v, what, o_screen, png, mask):
    """The original's whole screen at WHAT against eclplay's --screen (MASK masked: a mask, or a list)."""
    if not png.exists():
        v.check(False, f"eclplay writes its screen {what}")
        return
    port_screen = cokbox.RGBScreen.load(png, SLOT_SCREEN)
    masks = mask if isinstance(mask, list) else [mask]
    count, box = checklib.masked_diff(o_screen, port_screen, masks)
    v.check(count == 0, f"the whole screen agrees with the original's {what} (the mouse pointer "
            "masked)", f"{count} pixels differ in {box}")


def command_init(blocks, record):
    return bytes.fromhex(blocks[record])[TURN_INITIATIVE] if blocks[record] is not None else None


def quick_check(v, work, patched, opts):
    o = command_original(work, patched, "Q", opts.verbose)
    p = command_port(work, patched, "Q", "Q")
    s, m = o["stop"], o["menu"]
    v.check(COMPUTER_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay stops at the computer's turn (5bb8:0000, computer_unported) after Q", p["last"])
    draws = command_state(v, "Q", s, p, m["seed"])
    v.check(draws == 1, "the original's RandSeed past the menu after Q is one draw (the bar "
            "message's wait, 1df9:052a)", f"{draws} draws")
    v.check(s["state"]["turn"] == 1, "the original's turn index (DS:bd2d) at the stop after Q is 1",
            f"{s['state']['turn']}")
    auto = tuple(bytes.fromhex(r)[0x18C] for r in s["records"])
    v.check(auto == QUICK_AUTO, "the original's COMPUTER (+0x18c) of the records after Q: record 1 "
            "on Auto (cok_combat_auto), the others as at the menu", f"{auto}")
    initiative = command_init(s["blocks"], 1)
    v.check(initiative == QUICK_INITIATIVE, f"record 1's initiative (block +3) is kept at "
            f"{QUICK_INITIATIVE} by Q", f"{initiative}")
    v.check(o["stop_begun"] == 0 and o["flipped"] == 0, "the original's DS:1cb5 at the stop after Q "
            "is 0 (no tail redraw on the back page: the flip changes nothing)",
            f"{o['stop_begun']} at the stop, {o['flipped']} after the flip")
    bmp = p["base"].with_suffix(".bmp")
    command_screen(v, "at the stop after Q", o["stop_screen"], bmp, MENU_POINTER)
    command_screen(v, "after the flip after Q", o["flip_screen"], bmp, MENU_POINTER)


def alt_q_check(v, work, patched, opts):
    o = command_original(work, patched, "AltQ", opts.verbose)
    p = command_port(work, patched, "AltQ", "\\@Q")
    s, m = o["stop"], o["menu"]
    v.check(COMPUTER_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay stops at the computer's turn (5bb8:0000, computer_unported) after Alt-Q",
            p["last"])
    draws = command_state(v, "AltQ", s, p, m["seed"])
    v.check(draws == 1, "the original's RandSeed past the menu after Alt-Q is one draw (the bar "
            "message's wait, 1df9:052a)", f"{draws} draws")
    v.check(s["state"]["turn"] == 1, "the original's turn index (DS:bd2d) at the computer's dispatch "
            "after Alt-Q is 1 (record 1's entry)", f"{s['state']['turn']}")
    auto = tuple(bytes.fromhex(r)[0x18C] for r in s["records"])
    v.check(auto == ALT_Q_AUTO, "the original's COMPUTER (+0x18c) of the records after Alt-Q: every "
            "record of the list on Auto, the monsters too (0c0f, 5045:1791 over DS:c6e5)",
            f"{auto}")
    initiative = command_init(s["blocks"], 1)
    v.check(initiative == ALT_Q_INITIATIVE, f"record 1's initiative (block +3) is "
            f"{ALT_Q_INITIATIVE:#x} after Alt-Q (0c0f)", f"{initiative}")
    v.check(o["stop_begun"] == 0 and o["flipped"] == 0, "the original's DS:1cb5 at the computer's "
            "dispatch after Alt-Q is 0, and 0 after its flip (19a4:06d3)",
            f"{o['stop_begun']} at the stop, {o['flipped']} after the flip")
    command_screen(v, "at the stop after Alt-Q", o["stop_screen"], p["base"].with_suffix(".bmp"),
                   MENU_POINTER)
    command_screen(v, "after the flip after Alt-Q", o["flip_screen"], p["base"].with_suffix(".bmp"),
                   MENU_POINTER)


def alt_m_check(v, work, patched, opts):
    o = command_original(work, patched, "AltM", opts.verbose)
    p = command_port(work, patched, "AltM", "\\@M")
    m = o["menu"]
    v.check(m["magic"] == 0 and o["stop_magic"] == 1, "the original's DS:b1cb is 0 at the menu and "
            "1 after Alt-M (0bd7 toggles the magic)", f"{m['magic']} then {o['stop_magic']}")
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay ends at the battle's command menu (out of keys) after Alt-M", p["last"])
    v.check("message: magic on" in p["text"], "eclplay says \"magic on\" (queen_bar_message) "
            "after Alt-M", [line for line in p["text"] if line.startswith("message:")])
    menus = sum(1 for line in p["text"] if line.startswith("menu: Aim"))
    v.check(menus == 2, "eclplay builds the command menu again after the message (its built text "
            "logged twice)", f"{menus} times")
    draws = command_state(v, "AltM", o["stop"], p, m["seed"])
    v.check(draws == 1, "the original's RandSeed past the menu after Alt-M is one draw (the bar "
            "message's wait, 1df9:052a)", f"{draws} draws")
    command_screen(v, "with the command menu again after Alt-M", o["menu_again"]["screen"],
                   p["base"].with_suffix(".bmp"), MENU_POINTER)


def speed_check(v, work, patched, opts):
    o = command_original(work, patched, "SFE", opts.verbose)
    m = o["menu"]
    runs = {name: command_port(work, patched, name, keys)
            for name, keys in (("S", "S"), ("SF", "SF"), ("SFE", "SFE"))}
    for name, p in runs.items():
        v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
                f"eclplay ends in the battle at the out of keys after {name}", p["last"])
    v.check(m["var12"] == 4 and o["speed"]["var12"] == 4 and o["speedF"]["var12"] == 3
            and o["stop_var12"] == 3,
            "the original's game speed (var 0x12) is 4 at the menu, 4 on the speed bar, 3 after F "
            "(Faster), 3 after Exit", f"{m['var12']}, {o['speed']['var12']}, {o['speedF']['var12']}, "
            f"{o['stop_var12']}")
    v.check(port_var12(runs["S"]) == 4 and port_var12(runs["SF"]) == 3
            and port_var12(runs["SFE"]) == 3,
            "eclplay's var 0x12 is 4 on the speed bar, 3 after F and after Exit",
            f"{port_var12(runs['S'])}, {port_var12(runs['SF'])}, {port_var12(runs['SFE'])}")
    draws = command_state(v, "SFE", o["stop"], runs["SFE"], m["seed"])
    v.check(draws == 0, "the original's RandSeed past the menu after S, F and E is unchanged (no "
            "draw: the speed menu takes no random number)", f"{draws} draws")
    command_screen(v, "on the speed bar after S", o["speed"]["screen"],
                   runs["S"]["base"].with_suffix(".bmp"), dqk_views.POINTER)
    command_screen(v, "on the speed bar after F", o["speedF"]["screen"],
                   runs["SF"]["base"].with_suffix(".bmp"), dqk_views.POINTER)
    command_screen(v, "with the command menu again after Exit", o["menu_again"]["screen"],
                   runs["SFE"]["base"].with_suffix(".bmp"), MENU_POINTER)



# The turns, the round and the won flows (step 4, part 27a: dqk-round-end). Each is the original's run
# to a stop the port cites, the port's run to the same stop, and their state compared.
TURNS_KEYS = 5                  # the keys typed at the first turn's menu before SHARKMAN's turn (turn 6)
TURNS_SEED = 0x1756582C         # RandSeed at SHARKMAN's computer turn (0 draws past the menu)
ROUND_SEED = 0x427245C1         # RandSeed at round 2's first menu (fixture 1)
ROUND_TURN = 1                  # DS:bd2d at round 2's first menu (CINDER's entry)
TURNS_LINE = "turn: SHARKMAN (initiative 5)"
# won: the treasure menu's key wait (75cc:0e93, ported in part 20a): the keys run out there
COMBAT_END = re.compile(r".*\(out of keys in block 1 at 8030\).*")
WON_CHAINS_OFF = (0xB0, 0xD0, 0x0B)             # won: effects the battle's end and 1b66 read (none held)
WON_STATE = ["mode 6", "seed 00003039", "flags 1 0 0 0 0 0", "standing 1", "fled 0", "monsters 1",
             "out 0", "b7a7 0", "share 1336", "pool 8 0 0"]
# The results' wait: the mouse pointer, and the bar's key cell (RETURN blinks: its phase differs)
WON_SCREEN_MASK = dqk_ecl_text.POINTER_WIDE
WON_WAIT_MASKS = [dqk_ecl_text.POINTER_WIDE, dqk_views.BAR]


def sorted_chains(box):
    """Each record's effect chain (the effect ids of its nodes, from 0x04 of the record's head):
    what the flows compare: the kept effects, none 0xb0, 0xd0 or 0x0b."""
    out = []
    for _, raw in L.records(box):
        off, seg = struct.unpack_from("<HH", raw, 4)
        ids = []
        while (off, seg) != (0, 0) and len(ids) < 60:
            node = box.read(seg * 16 + off, 9)
            ids.append(node[0])
            off, seg = struct.unpack_from("<HH", node, 5)
        out.append(ids)
    return out


def turns_original(work, patched, verbose):
    """The original at the first turn's menu, five E keys typed there (settled between them), and
    SHARKMAN's computer turn: 5045:0891 made to spin in to_menu's copies (the turn 6's dispatch).
    The state there and the screen, then the page flip (19a4:06d3, the spin's own) and its screen."""
    with cokbox.Box(workdir=work / "box-turns", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        menu["menu_screen"] = box.screenshot(path=str(work / "turns-menu.png"))
        t = arm(box, menu["handle"], COMPUTER)
        for i in range(TURNS_KEYS):
            box.keys("E")
            if i < TURNS_KEYS - 1:
                box.settled(quiet=1.0, tolerance=0)
        wait_trap(box, t, "5045:0891")
        stop = snapshot(box)
        stop.update(tail_state(box), sides=[box.ds_byte(0xB7F2), box.ds_byte(0xB7F3)],
                    c2d6="%04x:%04x" % (box.ds_word(0xC2D8), box.ds_word(0xC2D6)))
        stop_screen = box.screenshot(path=str(work / "turns-stop.png"))
        flipped = L.flip(box, t)
        flip_screen = box.screenshot(path=str(work / "turns-flip.png"))
        return dict(menu=menu, stop=stop, stop_screen=stop_screen, flipped=flipped,
                    flip_screen=flip_screen)


def turns(v, work, opts):
    """The turns flow: five E keys from the first turn's menu, to SHARKMAN's computer turn (5045:0891,
    5bb8:0000 for eclplay): the records, blocks, turn list (DS:bd2d 6), RandSeed (0x1756582c, no draw
    past the menu), the sides [6, 2], DS:bebd 0, the pending counts 0, DS:c2d6 NULL, DS:1cb5 0 and the
    screen at the stop and after the flip."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    o = turns_original(work, patched, opts.verbose)
    spins_at_placement(v, o["menu"])
    s = o["stop"]
    base = work / "port-turns"
    variables, last = port(work, patched, name="turns", typed="\\r\\r" + "E" * TURNS_KEYS,
                           field=base.with_suffix(".field"), records=base.with_suffix(".records"),
                           blocks=base.with_suffix(".blocks"), turns=base.with_suffix(".turns"),
                           screen=base.with_suffix(".bmp"))
    v.check(COMPUTER_STOP.fullmatch(last or "") is not None,
            "eclplay stops at the computer's turn (5bb8:0000) after five E", last)
    lines = (work / "port-turns.txt").read_text().splitlines()
    v.check([line for line in lines if line.startswith("turn:")][-1:] == [TURNS_LINE],
            "eclplay's last turn is SHARKMAN (initiative 5)",
            [line for line in lines if line.startswith("turn:")][-1:])
    draws = command_state(v, "turns", s, dict(base=base, last=last), o["menu"]["seed"])
    v.check(s["seed"] == TURNS_SEED and draws == 0, f"RandSeed at SHARKMAN's turn is "
            f"{TURNS_SEED:#x}, no draw past the menu", f"{s['seed']:#x}, {draws} draws")
    v.check(s["state"]["turn"] == 6, "the original's turn index (DS:bd2d) at SHARKMAN's turn is 6",
            f"{s['state']['turn']}")
    v.check(s["sides"] == [6, 2], "the sides at SHARKMAN's turn are [6, 2]", f"{s['sides']}")
    v.check(s["bebd"] == 0 and s["bd4c"] == 0 and s["ad6e"] == 0 and s["ad6f"] == 0,
            "DS:bebd and the pending counts (bd4c, ad6e, ad6f) are 0 at SHARKMAN's turn",
            f"{s['bebd']:#x} {s['bd4c']} {s['ad6e']} {s['ad6f']}")
    v.check(s["c2d6"] == "0000:0000" and s["begun"] == 0,
            "DS:c2d6 is NULL and DS:1cb5 is 0 at SHARKMAN's turn", f"{s['c2d6']} {s['begun']}")
    v.check(variables == s["variables"], "the variables agree at SHARKMAN's turn (the 1 KB)")
    bmp = base.with_suffix(".bmp")
    command_screen(v, "at SHARKMAN's turn", o["stop_screen"], bmp, MENU_POINTER)
    command_screen(v, "after the flip at SHARKMAN's turn", o["flip_screen"], bmp, MENU_POINTER)


def round_original(work, patched, verbose):
    """Fixture 1: DS:b20f's var 0x2e set to 2 at the entry (before set_seed). The first turn's menu,
    five E keys (settled between), the computer's dispatch (5045:0891) armed, the state before the
    sixth key, then the sixth key with the menu (5045:08cf) armed too: round 2's first menu."""
    with cokbox.Box(workdir=work / "box-round", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.write(box.far(box.ds * 16 + dqk_ecl.DS_VARS) + 0x2E, b"\x02")
        box.set_seed(SEED)
        menu = to_menu(box)
        t91 = arm(box, menu["handle"], COMPUTER)
        for i in range(5):
            box.keys("E")
            box.settled(quiet=1.0, tolerance=0)
        before6 = block_vars(box)
        t08 = arm(box, menu["handle"], MENU)
        box.keys("E")
        wait_trap(box, t91 + t08, "5045:08cf (round 2's first menu)")
        stop = snapshot(box)
        stop.update(tail_state(box), sides=[box.ds_byte(0xB7F2), box.ds_byte(0xB7F3)],
                    selected=selected(box))
        seg, off = (int(x, 16) for x in stop["selected"].split(":"))
        computer = box.read(seg * 16 + off + 0x18C, 1)[0]
        if computer != 0:
            raise cokbox.BoxError("round 2's first stop is a computer's turn, not a menu")
        L.untrap(box, t91 + t08)
        box.settled(quiet=1.0, tolerance=0)
        return dict(menu=menu, before6=before6, stop=stop,
                    menu_screen2=box.screenshot(path=str(work / "round-menu2.png")))


def round_flow(v, work, opts):
    """The round flow: round 2's first menu (CINDER, DS:bd2d 1, RandSeed 0x427245c1 after 12 draws
    from the first menu's), against eclplay's stop (BATTLE_STOP) with --set 2e=2: the records,
    blocks, turn list, the variables (var 6 0 -> 1, DS:ae4c 1), the sides, DS:bebd 0, the pending
    counts 0, and the screen after the menu is given back."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    o = round_original(work, patched, opts.verbose)
    spins_at_placement(v, o["menu"])
    s = o["stop"]
    base = work / "port-round"
    variables, last = port(work, patched, name="round", typed="\\r\\r" + "E" * 6,
                           sets=["2e=2"], field=base.with_suffix(".field"),
                           records=base.with_suffix(".records"), blocks=base.with_suffix(".blocks"),
                           turns=base.with_suffix(".turns"), screen=base.with_suffix(".bmp"))
    v.check(BATTLE_STOP.fullmatch(last or "") is not None,
            "eclplay stops at round 2's first menu after six E", last)
    draws = command_state(v, "round", s, dict(base=base, last=last), o["menu"]["seed"])
    v.check(s["seed"] == ROUND_SEED, f"RandSeed at round 2's first menu is {ROUND_SEED:#x}",
            f"{s['seed']:#x} ({draws} draws past the first menu)")
    v.check(s["state"]["turn"] == ROUND_TURN and s["sides"] == [6, 2],
            "the original's DS:bd2d is 1 and the sides [6, 2] at round 2's first menu",
            f"{s['state']['turn']} {s['sides']}")
    before, after = o["before6"], s["variables"]
    v.check(before[5] == 0 and after[5] == 1, "var 6 is 0 before round 2 and 1 after it (one minute)",
            f"{before[5]} -> {after[5]}")
    v.check(s["ae4c"] == 1 and s["bebd"] == 0 and s["bd4c"] == 0 and s["ad6e"] == 0 and
            s["ad6f"] == 0, "DS:ae4c is 1, DS:bebd and the pending counts 0 at round 2's first menu",
            f"{s['ae4c']} {s['bebd']:#x} {s['bd4c']} {s['ad6e']} {s['ad6f']}")
    v.check(variables == after, "the variables agree at round 2's first menu (the 1 KB)")
    command_screen(v, "after round 2's first menu is given back", o["menu_screen2"],
                   base.with_suffix(".bmp"), MENU_POINTER)


def won_original(work, patched, verbose):
    """Fixture 2 (won): the original's run to the battle's end. Root 148f:0512 spins at the 'loaded'
    PRESS KEY (the setup's pass, 59c7:1eb9's event 8), each SHARKMAN +0x64 6 and +0x18b 0, DS:b7f3 0,
    var 0x2e 1 (so 135c's clear shows); 2093:0015 spins (var 0x2e is 0, the sides [6, 0], DS:c6ed =
    1d99:0fd1, c2d6 NULL), then 75cc:0723 and 0736, then the treasure menu's 75cc:0e93 (after the
    results' key): the state and the screens at the results' key wait and at the stop."""
    with cokbox.Box(workdir=work / "box-won", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        v = box.far(box.ds * 16 + dqk_ecl.DS_VARS)
        box.set_seed(SEED)
        box.keys("\\r")
        box.settled(tolerance=64)                       # 'monsters'
        box.keys("\\r")
        box.settled(tolerance=64)                       # 'loaded'
        out = dict(waiting_loaded=box.waiting())
        code = L.code(box, 0x148F, 0x0512)
        orig = box.read(code, 2)
        if orig != b"\x55\x8b":
            raise cokbox.BoxError(f"148f:0512 is not where it was expected: {orig.hex()}")
        box.write(code, b"\xeb\xfe")
        hA = L.Trap([(code, orig)], 0x148F, 0x0512)
        box.keys("\\r")                                 # SETUP MONSTERS, APPROACH, COMBAT
        wait_held_at(box, hA, "148f:0512")
        out["A"] = dict(mode=box.ds_word(L.DS_MODE), sides=[box.ds_byte(0xB7F2), box.ds_byte(0xB7F3)],
                        seed=box.ds_dword(L.DS_SEED))
        for ptr, raw in L.records(box):
            if raw[0x68:0x70] == b"SHARKMAN":
                a = ptr[0] * 16 + ptr[1]
                box.write(a + 0x64, b"\x06\x00")
                box.write(a + 0x18B, b"\x00")
        box.ds_write(0xB7F3, b"\x00")
        box.write(v + 0x2E, b"\x01")                    # so 135c's clear is seen
        c2 = L.code(box, 0x2093, 0x0015)
        o2 = box.read(c2, 2)
        if o2 != b"\x55\x8b":
            raise cokbox.BoxError(f"2093:0015 is not where it was expected: {o2.hex()}")
        box.write(c2, b"\xeb\xfe")
        hB = L.Trap([(c2, o2)], 0x2093, 0x0015)
        L.untrap(box, hA)
        wait_held_at(box, hB, "2093:0015", seconds=90)
        out["B"] = dict(snapshot(box), var2e=box.byte(v + 0x2E), sides=[box.ds_byte(0xB7F2), box.ds_byte(0xB7F3)],
                        c6ed="%04x:%04x" % (box.ds_word(0xC6EF), box.ds_word(0xC6ED)),
                        c2d6="%04x:%04x" % (box.ds_word(0xC2D8), box.ds_word(0xC2D6)),
                        c6ed_at=in_map(box, 0x1D99, 0x0FD1),
                        chains=sorted_chains(box))
        hC = L.trap(box, 0x75CC, 0x0723, seconds=10)
        hC2 = L.trap(box, 0x75CC, 0x0736, seconds=10)
        L.untrap(box, hB)
        both = L.Trap(list(hC) + list(hC2), 0x75CC, 0x0723)
        wait_held_at(box, both, "75cc:0723/0736")
        out["C"] = dict(af3f=box.ds_byte(0xAF3F), mode=box.ds_word(L.DS_MODE), chains=sorted_chains(box))
        hE = L.trap(box, 0x75CC, 0x0E93, seconds=10)
        hE2 = L.trap(box, 0x5F07, 0x0987, seconds=10)
        L.untrap(box, both)
        t0 = time.monotonic()
        while not box.waiting():
            if time.monotonic() - t0 > 60:
                raise cokbox.BoxError("no key wait after the battle's end")
        out["wait_screen"] = box.settled(tolerance=64)  # the key wait's screen (dqk_treasure's)
        out["wait_screen"].save_png(str(work / "won-wait.png"))
        box.keys("\\r")
        hold = L.Trap(list(hE) + list(hE2), 0x75CC, 0x0E93)
        wait_held_at(box, hold, "75cc:0e93/5f07:0987")
        out["E"] = dict(snapshot(box), standing=box.ds_byte(0xAF3F), fled=box.ds_byte(0xC7DE),
                        monsters=box.ds_byte(0xC7DF), out=box.ds_byte(0xB1DA),
                        b7a7=box.ds_byte(0xB7A7), share=int.from_bytes(box.ds_read(0xAE4F, 4), "little"),
                        pool=tuple(int.from_bytes(box.ds_read(at, 4), "little") for at in (0xB1DB, 0xB1DF, 0xB1E3)),
                        flags=list(box.ds_read(0xB2F5, 6)), chains=sorted_chains(box))
        out["end_screen"] = box.screenshot(path=str(work / "won-end.png"))
        return out


def in_map(box, seg, off):
    """DS:c6ed (the far pointer 1d99:0fd1 in the map): its offset is OFF and its segment is the map's
    load segment in the box (2624 measured, 1d99 relocated; not checked against the image's bytes,
    which code_at reads from START_FULL.EXE). Returns the pointer, or None when its offset is not OFF."""
    ptr = (box.ds_word(0xC6EF), box.ds_word(0xC6ED))
    return "%04x:%04x" % ptr if ptr[1] == off and ptr[0] != 0 else None


def wait_held_at(box, t, where, seconds=120):
    """Waits until the spin at WHERE holds (its copies T, EB FE, the polls steady)."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        box.settle(0.5)
        if L.held(box, t, seconds=0.6)[0]:
            return
    raise cokbox.BoxError(f"the original's trap at {where} is not reached after {seconds} s")


def won(v, work, opts):
    """The won flow: the original's state at 75cc:0723 (after the flags), the chains kept from there to
    the treasure menu (none 0xb0, 0xd0 or 0x0b), and at 75cc:0e93 the records, the variables, the
    --state lines and the screens (the results' key wait and the stop), against eclplay --combat won."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    o = won_original(work, patched, opts.verbose)
    v.check(o["waiting_loaded"], "the original waits at the 'loaded' PRESS KEY before COMBAT")
    v.check(o["A"]["mode"] == 5 and o["A"]["sides"] == [6, 2],
            "the battle's mode is 5 and the sides [6, 2] at the setup's end (148f:0512)",
            f"{o['A']['mode']} {o['A']['sides']}")
    # DS:c6ed is 1d99:0fd1 in the map's own segment (the box relocates it, from_ghidra)
    v.check(o["B"]["var2e"] == 0 and o["B"]["sides"] == [6, 0] and o["B"]["c6ed_at"] is not None
            and o["B"]["c2d6"] == "0000:0000",
            "at 2093:0015 var 0x2e is 0 (135c's clear), the sides [6, 0], DS:c6ed is 1d99:0fd1 (the "
            "map's copy in the box), DS:c2d6 NULL",
            f"{o['B']['var2e']} {o['B']['sides']} {o['B']['c6ed_at']} {o['B']['c2d6']}")
    # the party's six records (the two SHARKMEN are removed by the battle's end, 11b0)
    kept = o["B"]["chains"][:6] == o["C"]["chains"][:6] == o["E"]["chains"]
    held = [i for i, ids in enumerate(o["E"]["chains"]) if set(ids) & set(WON_CHAINS_OFF)]
    v.check(kept and not held, "the records' effect chains are kept to the treasure menu (none 0xb0, "
            "0xd0 or 0x0b)", f"kept {kept}, held by {held}: B {o['B']['chains']} C {o['C']['chains']} "
            f"E {o['E']['chains']}")
    e = o["E"]
    v.check(e["mode"] == 6 and e["monsters"] == 1 and e["standing"] == 1 and e["fled"] == 0
            and e["out"] == 0 and e["b7a7"] == 0 and e["share"] == 1336 and e["pool"] == (8, 0, 0)
            and e["flags"] == [1, 0, 0, 0, 0, 0] and e["seed"] == 0x3039,
            "at the treasure menu: mode 6, the flags 1 0 0 0 0 0, standing 1, fled 0, monsters 1, out 0, "
            "b7a7 0, share 1336, pool 8 0 0, RandSeed 0x3039 (the won path draws nothing)",
            f"mode {e['mode']} share {e['share']} pool {e['pool']} seed {e['seed']:#x}")
    base = work / "port-won"
    port(work, patched, name="won-wait", typed="\\r\\r\\r\\r", combat="won",
         screen=work / "port-won-wait.bmp")
    end_v, end_last = port(work, patched, name="won", typed="\\r\\r\\r\\r\\r", combat="won",
                           records=base.with_suffix(".records"), state=base.with_suffix(".state"),
                           screen=base.with_suffix(".bmp"))
    v.check(COMBAT_END.fullmatch(end_last or "") is not None,
            "eclplay waits at the treasure menu (75cc:0e93, part 20a) after the results' key", end_last)
    got = base.with_suffix(".state").read_text().splitlines() if base.with_suffix(".state").exists() else []
    v.check(got == WON_STATE, "eclplay's --state at the treasure menu is the original's", got)
    data = base.with_suffix(".records").read_bytes() if base.with_suffix(".records").exists() else b""
    ported = [data[i:i + L.RECORD].hex() for i in range(0, len(data) - 3, L.RECORD)]
    differ = [i for i in range(min(len(ported), len(e["records"])))
              if masked(ported[i]) != masked(e["records"][i])]
    v.check(not differ and len(ported) == len(e["records"]),
            "every record agrees at the treasure menu (pointers masked)",
            f"{len(ported)} against {len(e['records'])}; differ: {' '.join(map(str, differ))}")
    v.check(end_v == e["variables"], "the variables agree at the treasure menu (the 1 KB)")
    command_screen(v, "at the results' key wait", o["wait_screen"], work / "port-won-wait.bmp",
                   WON_WAIT_MASKS)
    command_screen(v, "at the treasure menu", o["end_screen"], base.with_suffix(".bmp"), WON_SCREEN_MASK)

def commands(v, work, patched, opts):
    """The command menu's four commands (step 4, part 22b) from the first turn's menu: Quick, Alt-Q,
    Alt-M and Speed (S, F, E), each run against eclplay's stop after its keys."""
    quick_check(v, work, patched, opts)
    alt_q_check(v, work, patched, opts)
    alt_m_check(v, work, patched, opts)
    speed_check(v, work, patched, opts)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows",
                        default="setup,load,clear,six,ground,encounter,field,wild,slots,icons,dispatch,"
                                "keys,turns,round,won",
                        help="which flows: setup, load, clear, six, ground, encounter, field, wild, "
                             "slots, icons, dispatch, keys, turns, round, won")
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
    if "wild" in flows:
        wild(v, work, opts)
    if "slots" in flows:
        slots(v, work, opts)
    if "icons" in flows:
        icons(v, work, opts)
    if "dispatch" in flows:
        dispatch(v, work, opts)
    if "keys" in flows:
        keys(v, work, opts)
    if "turns" in flows:
        turns(v, work, opts)
    if "round" in flows:
        round_flow(v, work, opts)
    if "won" in flows:
        won(v, work, opts)
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

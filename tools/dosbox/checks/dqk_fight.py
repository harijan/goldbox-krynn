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

Flow `melee` (step 4, part 15c): one round of the computer's fight in the original, from the first
turn's menu: five E keys, SHARKMAN 1's computer turn (turn 6: the walk, the stuck steps and the pattern
6, and the computer's target), ROLAND OF DUST's menu (turn 7), E, SHARKMAN 2's turn (turn 8) and the round's
end, round 2's first menu (TIMASALIN). eclplay's --combat is the port's own (no --combat): its runs stop
at the menus, out of keys, after "\\r\\r" + five E and + six E. At each: RandSeed (21 draws, then 29 past
ROLAND's menu), every record and combat block (the target as eclplay writes it: normal_block; the pattern
too), the turn list, the combatants' places (--field), and the screen (MENU_POINTER masked; round 2's once drawn).

Flow `melee-long` (step 4, part 15d): the same fight from the first turn's menu to round 6's first menu (30
party menus, about 100 s in the box): each party menu trapped at 5045:08cf and E typed there, each computer turn
(5045:0891) given back at once. At each round's first menu (DS:bd2d 1, rounds 2 to 6) eclplay's run after
6 * (round - 1) E keys is compared: RandSeed, SHARKMAN 2 and SHARKMAN 1's places, ROLAND OF DUST's hit points
(scope.md section 0's table; the first strike is round 5's turn 8, and it hits ROLAND), the first combatant
(the turn list's), every record and combat block, the turn list, the effect chains, the places and the screen.
See melee_long_original.

Flow `melee-dying` (step 4, part 16b): melee-long's run with ROLAND OF DUST's hit points set to 1 before COMBAT
(fight_block(DYING_MEMBER): LOAD_CHARACTER 0, SAVE 1 into 0x5fd, the ECL field 0x1fc, both sides). The original's
states at SHARKMAN 1's turn (its dispatch 5045:0891), ROLAND's menu after it (five E) and each round's first menu
2 to 6 are compared with eclplay's runs (dying_compare): the chains, RandSeed, records, blocks, turns, cells,
occupants, combatants, the bodies (eclplay's --field now dumps them: part16-17's step 7, DS:c526 and DS:c5d3)
and the screen. The first strike (round 5's turn 8) kills ROLAND: 0 hit points at round 6's first menu. About a
minute (the melee-long run's stops, with the eclplay runs).

Flow `altq_missile` (step 4, part 15c, its stop moved by part 17b): Alt-Q's first missile flight. The original
runs from the first turn's menu with \\@Q typed there: the trap is EB FE in the copies of 52db:2888 in overlay 0
(MISSILE_ENTRY, arm; the root image has no copy there), and the wait holds when the spin does and RandSeed does not
move over a second, twice (the computer's turns run without a key wait, so the polls alone do not show the
flight). Then the flight's strike: 148f:077f (STRIKE_EFFECT, in the root image, so a static trap holds), where
event 4 runs effect 0x69 on the attacker; 78e4:1aa2 is not trapped, as overlay 9 is only loaded by that call.
eclplay's run with the same keys stops there (MISSILE_STOP): since part 18b1 ported 0x69, a check build cites it
unported again (missile_eclplay), so the port stops where it first runs, before its handler, as before 18b1. Its
state is compared at the strike: RandSeed
1044 draws past the menu (MISSILE_STOP_DRAWS; the entry's 1039 is checked too), every record and combat block,
the turn list, the combatants (--field), the selected record (DS:ad67, 57fb:04da), and the whole screen (the
flight's restore is in it; MENU_POINTER masked). About a minute of wall time (0.4 min in the last run); the Box
timeout is 900 s.

Flow `walk`: tests/test_dark_queen.py's test_first_fight in the original, the shipped ECL: the start
menu, plta, Begin, block 4's texts and the steps to the dragons, the first turn's menu (5045:08cf), End,
the next turn's menu; RandSeed set at the start menu to eclplay's there, and not set again: at block 4's
first text it is eclplay's, the copy protection's two draws made in both; then the seed at THEY ATTACK!'s
wait and the turn list and seed at both menus against eclplay's --load --seed route (walk_port; its
--start-menu route's turn lists are the same). See the comment above walk_original.

Flow `auto` (step 4, parts 18a, 18b2 and 18b3: --combat auto's first turn and the whole fight to its end; run alone
with `--flows auto`, its own box with a 1800 s timeout, not in the default flows, about 1.5 min): Alt-Q typed at the first
turn's menu in the original (\\@Q), and each round's end (5045:0f6c, cok_combat_end_round's start) made to spin and read,
until the fight's last round (30: the round limit, DS:ae4c + 1 >= DS:b45a, or a side gone; AUTO_MAX rounds at most).
The screen at each end is the flipped one (flip_continue: the original's own page flip, 19a4:06d3, run at the spin, then
the routine given back), since the unflipped one is out of date where the last turn's redraw is on the back page
(DS:1cb5 set, rounds 20 and 23 of this fight). No trap is armed after the last round: the results and Return are read
after it (18b3, auto_original's tail). eclplay runs the block three times, Alt-Q typed (\\r\\r\\@Q), --combat auto (\\r\\r) and
with one more Return: the first two must write the same round files (the --records, --blocks, --field and --turns writers and the screen, at round.c's mark:
auto_eclplay, a patched build), AUTO_ROUNDS of them. Each round's records (pointers masked), combat blocks (fleeing
included), turn list, combatants, battlefield, RandSeed and screen (MENU_POINTER masked) agree with the original's; and
the round's DS:ae4c, DS:b45a and sides are the fight's (auto_expected). A hold is taken as the round's end only where
5045:059d's loop can have left (0726-0732: the entry at DS:bd2d NULL, or 06aa-06c7's done: a side gone or DS:ae4c >=
DS:b45a) and a second hold sees RandSeed and DS:bd2d unmoved: the keyboard polls alone also stand still for more than
0.6 s inside a turn (round 2's turn 4, SHARKMAN 1's, in every run), with the spin not yet reached. The strict wait
refuses 41 to 48 holds a run (the false holds, counted and printed; none is a round's end).

Every flow's box freezes the memory cache's purge draw (dqk_fight_lib.freeze_purges, a box-only
change at 27fa:0f2c), so the original's RandSeed moves only with its own draws, as eclplay's does;
the field flow (below) checks it at each square and records the purges.

The original's states go to build/dqk-fight/setup.json, with the heap pointers
masked (dqk_fight_lib.portable): later parts compare the port's state with them.
About 20 seconds.

    python3 tools/dosbox/checks/dqk_fight.py [--workdir DIR] [--verbose]
        [--flows setup,load,clear,six,ground,encounter,field,wild,slots,icons,dispatch,keys,
         altq_missile,turns,round,melee,melee-long,melee-dying,won,drop,timer,walk,mouse,move,aim,turn,
         cast,cast_auto,cast_auto_area]
        (auto alone)

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import filecmp
import json
import os
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
import dqk_modify  # noqa: E402
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
# The flows run without --flows (main), in main's order; run_all's DQK_SPLIT runs each as its
# own row (dqk_fight-FLOW) and its self-test checks that the two lists agree. auto runs alone.
DEFAULT_FLOWS = ("setup", "load", "clear", "six", "ground", "encounter", "field", "wild", "slots",
                 "icons", "dispatch", "keys", "altq_missile", "turns", "round", "melee", "melee-long",
                 "melee-dying", "won", "drop", "timer", "walk", "mouse", "move", "aim", "turn", "cast",
                 "cast_auto", "cast_auto_area", "band_a1", "band_a2", "band_a3", "band_b1", "band_b2",
                 "band_b3", "band_b4", "band_b5", "band_c1", "effects_1", "effects_2", "effects_4", "effects_3")


def fight_block(lower=None, monster=MONSTER):
    """Block 1 of the test: the entry PRESS KEY, the fight (dqkecl.fight: LOAD
    FILES, CLEARMONSTERS, a PRESS KEY labelled 'monsters', LOAD MONSTER, a PRESS
    KEY labelled 'loaded', SETUP MONSTERS, APPROACH, COMBAT), then the block's EXIT.
    LOWER, if given, is a party member's index whose hit points are set to 1 before
    COMBAT (lower_hit_points); None is the block every other flow runs. MONSTER is
    the monster loaded (COUNT of them; the turn flow loads ZOMBIE)."""
    b = Block()
    b.op("PRESS_KEY")
    before = None if lower is None else (lambda blk: lower_hit_points(blk, lower))
    dqkecl.fight(b, [(monster, COUNT)], sprite=SPRITE, pause=True, before_combat=before)
    b.op("EXIT")
    return b


# The selected member's hit points: ECL address 0x401 + 0x1fc, the byte at +0x198 (rules_dark_queen.c's
# BOTH(0x1fc, U8, COK_FIELD_HIT_POINTS), the record's 0x198 by dark_queen_character_vars).
HIT_POINTS_ECL = 0x401 + 0x1FC


def lower_hit_points(b, member):
    """LOAD_CHARACTER member (the party's index, 0 for ROLAND OF DUST), then SAVE 1 into the selected
    member's hit points (0x5fd): the block's write before COMBAT, run by both sides."""
    b.op("LOAD_CHARACTER", imm(member))
    b.op("SAVE", imm(1), var(HIT_POINTS_ECL))


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


def saved_entry(work, data=None):
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
    if data is None:
        data = (dqk_lib.ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes()
    data = bytearray(data)
    data[0x16 - 1] = dqk_ecl.JUMP
    (folder / "SAVGAMA.QSV").write_bytes(bytes(data))
    return folder / "SAVGAMA.QSV"


def port(work, patched, name="fight", typed="", records=None, difficulty=None, screen=None,
         field=None, at=None, blocks=None, turns=None, combat=None, state=None, sets=(),
         effects=None, entry=None, exe=None, items=None, mouse=False, shots=None):
    """eclplay's run of the block as the original's Load then Begin Adventuring
    gives it: saved game A's copy (--load) and --play, which is Begin Adventuring's
    entry (without it the block is entered in load_saved's mode 0, not the mode the
    original's Begin leaves). No --set for the variables: the save gives them, and
    DIFFICULTY, if not None, is var 0x27 set after the load. TYPED are the keys after
    the entry's; with RECORDS, the party list written there (--records). Returns its
    variables where it stops (--vars) and its last output line."""
    extra = ["--load", str(saved_entry(work) if entry is None else entry), "--play"]
    if records is not None:
        extra += ["--records", str(records)]
    if blocks is not None:
        extra += ["--blocks", str(blocks)]
    if turns is not None:
        extra += ["--turns", str(turns)]
    if effects is not None:
        extra += ["--effects", str(effects)]
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
    if items is not None:
        extra += ["--items", str(items)]
    if shots is not None:
        extra += ["--shots", str(shots)]  # DIR/NNN.bmp at each key wait (the last is where the keys ran out)
    if mouse:
        extra += ["--mouse"]  # the game's own cursor drawn (the box's mouse driver has it at 8, 8)
    variables, last, _ = dqk_ecl.port(work, name, patched, typed=typed, extra=extra,
                                      block=None, exe=exe)
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
                    chains=L.effect_chains(box),
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


def combat_blocks(box, records, raw=False):
    """Each record's combat block as hex (0x18 bytes at +0x40, 59c7:0f7b), None for a NULL
    pointer; records are L.records' (pointer, raw) pairs. As eclplay's --blocks writes them
    (normal_block: the target at +0x0b as the party index plus 1), unless RAW."""
    places = {ptr: i for i, (ptr, _) in enumerate(records)}
    out = []
    for _, r in records:
        cb = L.combat_record(box, r)
        if cb is None:
            out.append(None)
        else:
            out.append(cb[1].hex() if raw else normal_block(cb[1], places))
    return out


TARGET_AT = 0x0B                        # the combat block's target, a far pointer (offset, segment)


def normal_block(block, places):
    """A combat block as eclplay's --blocks writes it: the target (+0x0b, a far pointer in the
    original, its record's place in the list: places) as the place plus 1, 0 for none and 0xff for
    one not in the list; +0x0c to +0x0e 0 (the port has no pointers). The rest as the original has
    it: the pattern (+0x16) is compared as it is."""
    b = bytearray(block)
    off, seg = struct.unpack_from("<HH", b, TARGET_AT)
    if (off, seg) == (0, 0):
        t = 0
    else:
        t = places[(seg, off)] + 1 if (seg, off) in places else 0xFF
    b[TARGET_AT:TARGET_AT + 4] = bytes((t, 0, 0, 0))
    return b.hex()


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


# The weapon choice's end (5bb8:1396's last call, the side panel, 177e:085f): the stop the weapon
# checks compare at. The panel is in the root image (177e: below 1f4e, always resident), so its entry
# is written directly, with no overlay copy to find. The call is at 5bb8:19a5 (a far call, so its
# return IP is 19aa, the frame the entry's stack holds).
PANEL_CALLEE = (0x177E, 0x085F)
PANEL_CALL = 0x19A5
PANEL_RETURN = 0x19AA


def masked(raw_hex):
    """A record's bytes with its pointer fields zeroed (the port has none)."""
    raw = bytearray.fromhex(raw_hex)
    for i in POINTER_BYTES:
        raw[i] = 0
    return bytes(raw)


def chains_file(path):
    """eclplay's --effects file: per record (list order), its nodes as [id, minutes, value, flag]."""
    out = []
    for line in path.read_text().splitlines():
        fields = line.split()
        out.append([[int(x, 16) for x in node.split(",")] for node in fields[1:]])
    return out


def chains_agree(v, original, path, tag):
    """eclplay's effect chains (--effects) agree with the original's, record for record and node
    for node (id, minutes, value, flag): the GORGONs' 0xcb among them."""
    ported = chains_file(path) if path.exists() else None
    v.check(ported is not None and ported == original,
            f"eclplay's effect chains agree with the original's (id, minutes, value, flag, in "
            f"order){tag}", "no file" if ported is None else
            f"original {original} eclplay {ported}")



# The timer flow (scope fixture 4): saved game A with ROARK's effect nodes 0, 1 and 2 rewritten (0x02
# for 10 minutes, flag 0; 0x05 for 5 minutes, flag 1; 0x26 (strength) for 5 minutes, flag 0, value
# 0x6a) and WALKER's nodes 0 and 1 (0x01 for 15 minutes, flag 0; 0x44 (the stored stats) for 5
# minutes, flag 0), ROARK's current exceptional strength 99 -> 100 and WALKER's current intelligence
# and wisdom (+0x7b, +0x7d) 3, then the block's ECL_CLOCK 1 2 (ten minutes, 1396:044d) between two
# PRESS KEYs. The members are saved game A's list: ROARK is 4 and WALKER 5 (0-based). Ten minutes
# remove ROARK's three nodes (0x26's removal runs 017d: 1dfc(0), strength 18/99) and WALKER's 0x44
# (1dfc(1) and 1dfc(2): intelligence 12, wisdom 16).
TIMER_REWRITE = {4: {0: (0x02, 10, 0xFF, 0), 1: (0x05, 5, 0xFF, 1), 2: (0x26, 5, 0x6A, 0)},
                 5: {0: (0x01, 15, 0xFF, 0), 1: (0x44, 5, 0, 0)}}
TIMER_CHANGES = {4: {0x84: 100}, 5: {0x7B: 3, 0x7D: 3}}
TIMER_BEFORE = {4: [0x02, 0x05, 0x26], 5: [0x01, 0x44]}     # the chains the rewrite leaves
TIMER_AFTER = {4: [], 5: [0x01]}                            # after ten minutes
# The stats after ten minutes (the original's, +0x79 strength, +0x84 its exceptional part; +0x7b
# intelligence, +0x7d wisdom): saved game A's values, as the removals recompute them.
TIMER_STATS = {4: {0x79: 18, 0x84: 99}, 5: {0x7B: 12, 0x7D: 16}}


def timer_block():
    """Block 1: the entry PRESS KEY, ECL_CLOCK 1 2, then the PRESS KEY labelled 'clocked', EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    b.op("ECL_CLOCK", imm(1), imm(2))
    b.label("clocked")
    b.op("PRESS_KEY")
    b.op("EXIT")
    return b


def timer_original(work, glb, qsv, verbose):
    """The original's run of the timer block: its chains before ECL_CLOCK (at the entry's PRESS KEY)
    and after it (at 'clocked'), the variables, RandSeed and the records there."""
    with cokbox.Box(workdir=work / "box-timer", game="dark-queen", verbose=verbose, timeout=900,
                    files={**L.files(glb), "SAVE/SAVGAMA.QSV": qsv}) as box:
        dqk_ecl.enter_block(box)
        L.freeze_purges(box)
        before = L.effect_chains(box)
        box.set_seed(SEED)
        box.keys("\\r")               # the entry's PRESS KEY: ECL_CLOCK runs
        dqk_ecl.wait_press_key(box, pc=timer_block().address("clocked") + 1)
        listed = L.records(box)
        return dict(before=before, chains=L.effect_chains(box), variables=block_vars(box),
                    seed=box.ds_dword(L.DS_SEED), records=[raw.hex() for _, raw in listed])


def ids_of(chains):
    return {member: [node[0] for node in chains[member]] for member in TIMER_BEFORE}


def timer(v, work, opts):
    """The timers' flow (scope fixture 4): ECL_CLOCK 1 2 with the nodes and record bytes TIMER_REWRITE
    and TIMER_CHANGES set. Ten minutes: ROARK's 0x02, 0x05 and 0x26 run out (0x05's handler, flag 1,
    is 1 retf; 0x26's removal recomputes strength), WALKER's 0x44 runs out (the stored stats, 1dfc(1)
    and 1dfc(2)) and its 0x01 has 5 left. The original's chains before and after, the records after
    (members 4 and 5 against eclplay's, pointers masked), clock variables and RandSeed, and eclplay's
    (--effects, --records, --vars, --state) at the same stop, the chains compared record for record."""
    import dqk_modify  # noqa: E402
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(timer_block()))
    qsv = dqk_modify.modified(TIMER_REWRITE, TIMER_CHANGES)
    o = timer_original(work, patched, qsv, opts.verbose)
    before, after = ids_of(o["before"]), ids_of(o["chains"])
    v.check(all(before[m] == TIMER_BEFORE[m] for m in TIMER_BEFORE),
            "the timer's chains before the clock: ROARK 02 05 26, WALKER 01 44",
            f"original {before}")
    v.check(all(after[m] == TIMER_AFTER[m] for m in TIMER_AFTER),
            "the original's chains after ten minutes: ROARK none, WALKER 01 (5 left)",
            f"original {after}")
    base = work / "port-timer"
    variables, last = port(work, patched, name="timer", typed="\\r", entry=saved_entry(work, qsv),
                           records=base.with_suffix(".records"), state=base.with_suffix(".state"),
                           effects=base.with_suffix(".effects"))
    chains_agree(v, o["chains"], base.with_suffix(".effects"), " (timer)")
    v.check(len(variables) == dqk_ecl.VARIABLES and variables[4:11] == o["variables"][4:11],
            "the clock's variables (5-11) agree after ECL_CLOCK 1 2",
            f"eclplay {list(variables[4:11])} the original {list(o['variables'][4:11])}")
    state = base.with_suffix(".state").read_text().splitlines() \
        if base.with_suffix(".state").exists() else []
    v.check(state[1:2] == [f"seed {o['seed']:08x}"], "RandSeed after the clock is the original's",
            f"original {o['seed']:#x}, eclplay {state[1:2]}")
    # The records after the clock, members 4 and 5 (pointers masked): the original's and eclplay's
    # agree, and the original's stats are the ones the removals recompute.
    data = base.with_suffix(".records").read_bytes() if base.with_suffix(".records").exists() else b""
    ported = [data[i:i + L.RECORD].hex() for i in range(0, len(data) - 3, L.RECORD)]
    original = o["records"]
    for m in (4, 5):
        v.check(len(ported) == len(original) and masked(ported[m]) == masked(original[m]),
                f"member {m}'s record after the clock agrees with the original's (pointers masked)",
                f"{len(ported)} records against {len(original)}")
    stats = {m: {k: bytes.fromhex(original[m])[k] for k in TIMER_STATS[m]} for m in TIMER_STATS}
    v.check(all(stats[m][k] == val for m in TIMER_STATS for k, val in TIMER_STATS[m].items()),
            "the original's stats after ten minutes: ROARK strength 18/99, WALKER intelligence 12 "
            "and wisdom 16", " ".join(f"{m}: " + " ".join(f"{k:#x} {val:#x}" for k, val in
                                       stats[m].items()) for m in stats))


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
    effects = work / f"port-{name}.effects"
    port_vars, port_last = port(work, patched, name=name, typed="\\r",
                                records=records, blocks=blocks, difficulty=difficulty,
                                turns=turns, effects=effects)
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
    chains_agree(v, r["chains"], effects, tag)
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
                      chains=L.effect_chains(box),
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
    effects = work / "port-encounter.effects"
    port_vars, port_last = port(work, patched, name="encounter",
                                typed="\\r" * len(ENC_WAITS), records=records, blocks=blocks,
                                turns=turns, effects=effects)
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
    chains_agree(v, battle["chains"], effects, " (encounter)")
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
                      chains=L.effect_chains(box),
                      turns=L.turn_order(box, listed),
                      blocks=combat_blocks(box, listed),
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


def wild_block(state, river, monster=MONSTER):
    """Block 1 of the wild flow: the fight block, outside 3D (see dqkecl.fight's GROUND)."""
    b = Block()
    b.op("PRESS_KEY")
    dqkecl.fight(b, [(monster, COUNT)], sprite=SPRITE, pause=True, ground=(state, river))
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


def placement(v, work, patched, tag, where, square, r, entry=None):
    """eclplay's run to the first turn (5045:078b) against the original's state r: the battlefield,
    RandSeed (the original's, after the draws the battle made: the builders' and the initiative's),
    the records, blocks, turn list, the header and the view. ENTRY: the saved game eclplay loads
    (saved_entry's; the plain one by default)."""
    original = r["field"]
    cells = bytes.fromhex(original["cells"])
    path = work / f"port-{tag}.field"
    records = work / f"port-{tag}.records"
    blocks = work / f"port-{tag}.blocks"
    turns = work / f"port-{tag}.turns"
    effects = work / f"port-{tag}.effects"
    menu = r.get("menu_screen")
    screen = work / f"port-{tag}.bmp" if menu is not None else None
    port_vars, port_last = port(work, patched, name=tag, typed="\\r\\r", field=path, at=square,
                                records=records, blocks=blocks, turns=turns, screen=screen,
                                effects=effects, entry=entry)
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
    chains_agree(v, r["chains"], effects, f" at {where}")
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
                chains=L.effect_chains(box), turns=L.turn_order(box, listed),
                blocks=combat_blocks(box, listed), blocks_raw=combat_blocks(box, listed, raw=True),
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
    return menu_from_turn(box, purges_at_seed)


def menu_from_turn(box, purges_at_seed):
    """to_menu's stop once the key that begins the battle is typed: 5045:078b trapped (at_placement),
    then 08cf in its copies, as to_menu says. Shared with the walk flow."""
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


def keys_original(work, patched, key, verbose, tag=None, data=None):
    """The original at the first turn's menu (to_menu), the key typed there (step 4, part 22a: End,
    Guard or Delay) and the next combatant's command menu reached: 5045:08cf, trapped where the
    dispatch's copies start. The state there (`stop`, with DS:ad67 as `selected`) and the trap given
    back, the menu drawn and waiting (`menu_screen2`), as the placement's menu. DATA, if given, is
    the saved game the box runs (SAVGAMA.QSV; the plain one by default)."""
    tag = tag or f"keys{key}"
    files = L.files(patched) if data is None else {**L.files(patched), "SAVE/SAVGAMA.QSV": data}
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose,
                    timeout=900, files=files) as box:
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
# computer's dispatch (5045:0891, flipped as keys_original's), Alt-M and Speed where the menu is built again
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
MISSILE_STOP = re.compile(r".*effect 0x69 \(dqk 78e4:1aa2\).*")  # the strike after the flight (148f:077f): part 17b
MISSILE_EFFECT_AT = "    [0x69] = {0x1aa2, COK_FX_QUEEN_RANGER},\n"


def missile_stop(name, text):
    """src/rules_dark_queen_magic.c: effect 0x69 (78e4:1aa2, ported by part 18b1) cited unported again, so that
    eclplay stops where it first runs, before its handler: the strike's event 4 on the attacker (148f:077f, the
    original's trap), the stop part 17b compared (MISSILE_STOP)."""
    if MISSILE_EFFECT_AT not in text:
        raise cokbox.BoxError("src/rules_dark_queen_magic.c's effect 0x69 changed: update dqk_fight.missile_stop")
    return text.replace(MISSILE_EFFECT_AT, MISSILE_EFFECT_AT.replace("},", ', "dqk 78e4:1aa2"},'))


def missile_eclplay():
    """eclplay with effect 0x69 cited unported (missile_stop; checklib.build, cached by the sources)."""
    return checklib.build("eclplay", transforms=[("rules_dark_queen_magic.c", missile_stop)])


def wait_trap(box, t, where):
    """Waits until the trap at WHERE (its copies T, written paused) is reached: the keyboard
    polls hold over 0.6 s (as keys_original's tail)."""
    end = time.monotonic() + TAIL_SECONDS
    while time.monotonic() < end:
        box.settle(0.5)
        if L.held(box, t, seconds=0.6)[0]:
            return
    raise cokbox.BoxError(f"the original's trap at {where} is not reached after {TAIL_SECONDS} s")


TURN_HOLD = 4.0                         # the turn flow's hold (game seconds): a timed wait of the game (1df9:052a) stops the polls too


def wait_spin(box, t, where, seconds=TURN_HOLD):
    """wait_trap with a longer hold: the spin's polls must not move over SECONDS of game time (a timed wait of the
    game, such as the message's, stops them for a second or two as a spin does, so the 0.6 s of wait_trap is not
    enough for the turn flow's traps)."""
    end = time.monotonic() + TAIL_SECONDS
    while time.monotonic() < end:
        box.settle(0.5)
        if L.held(box, t, seconds=seconds)[0]:
            return
    raise cokbox.BoxError(f"the original's trap at {where} is not reached after {TAIL_SECONDS} s")


def var12(box):
    return block_vars(box)[SPEED_VAR - 1]


def command_original(work, patched, flow, verbose):
    """The original at the first turn's menu (to_menu, the menu's screen kept), FLOW's keys typed
    there, and its stop: Quick (Q) at 5045:0a56, Alt-Q (\\@Q) at the computer's dispatch 5045:0891, flipped as
    keys_original's, Alt-M (\\@M) and Speed (S, F, E) at 5045:0cd7, the menu built again. The
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
            box.keys({"Q": "Q", "AltQ": "\\@Q", "AltM": "\\@M", "AltX": "\\@X"}[flow])
        wait_trap(box, t, f"5045:{trap:04x}")
        out["stop"] = snapshot(box)
        out["stop"]["sides"] = [box.ds_byte(0xB7F2), box.ds_byte(0xB7F3)]  # weapon_draws
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


def command_port(work, patched, name, typed, entry=None, effects=False, exe=None):
    """eclplay's run to the stop of NAME (the first turn's menu, then TYPED; ENTRY the saved game as
    placement's; EXE a patched eclplay, or None): its variables, its last line, its text, and the files
    its state is read from (base: field, records, blocks, turns, bmp; effects, with EFFECTS, the chains)."""
    base = work / f"cmd-{name}"
    variables, last = port(work, patched, name=f"cmd-{name}", typed="\\r\\r" + typed,
                           field=base.with_suffix(".field"), records=base.with_suffix(".records"),
                           blocks=base.with_suffix(".blocks"), turns=base.with_suffix(".turns"),
                           screen=base.with_suffix(".bmp"), entry=entry,
                           effects=base.with_suffix(".effects") if effects else None, exe=exe)
    text = (work / f"port-cmd-{name}.txt").read_text().splitlines()
    return dict(base=base, vars=variables, last=last, text=text)


def port_var12(p):
    return p["vars"][SPEED_VAR - 1] if len(p["vars"]) == dqk_ecl.VARIABLES else -1


EVENT1_IDS = (0x19, 0x25, 0x45)  # 148f:055c: the handlers event 1 runs on the target (148f:0369)


def weapon_draws(enemies):
    """The RandSeed draws the computer's turn (5bb8:0000) makes from its entry to the panel's call in
    its weapon choice (177e:085f, weapon_original's stop; the keys 5bb8:1050 read none and 1396 draws
    none before it: no message shows, Ready's messages would draw once, 177e:153c), exactly,
    with ENEMIES the other side's count (s["sides"]): the pattern from 0 (5bb8:008d, d8, then d2 or
    d4): 2 draws. The pick at the top of 0000 (52db:3d9e, no target): one d(1, n) unused (3e74), then
    one d(1, n) for the first try (3ed5), which 52db:1030 accepts (it refuses only by event 1's
    handlers, the guard in weapon_entry); none for n < 2 (2102:0234). So 2 + 2 if n >= 2."""
    return 2 + (2 if enemies >= 2 else 0)


def ported_records(p):
    """eclplay's --records file at the stop as hex, one record a line (the list order)."""
    data = p["base"].with_suffix(".records").read_bytes() if p["base"].with_suffix(".records").exists() else b""
    return [data[i:i + L.RECORD].hex() for i in range(0, len(data) - 3, L.RECORD)]


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
    ported = ported_records(p)
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


def panel_frames(box, seg):
    """The stack's frames that call the side panel (the far call lcall SEG:0x085f, SEG the panel's
    real segment) from 5bb8:19a5, so that they return to 19aa: (the frame's offset in the stack
    segment, the caller's segment). The stack segment is the hook's SS (taken at the last key read:
    the game's stack does not move between them); the scan is of the whole 64 KB segment."""
    ss = box.hook("ss")
    stack = box.read(ss * 16, 0x10000)
    call = b"\x9a" + struct.pack("<HH", PANEL_CALLEE[1], seg)
    frames = []
    for i in range(0, len(stack) - 3, 2):
        ip, cs = struct.unpack_from("<HH", stack, i)
        if ip == PANEL_RETURN and box.read(cs * 16 + PANEL_CALL, len(call)) == call:
            frames.append((i, cs))
    return frames


def weapon_original(work, patched, verbose, tag, keys):
    """The original's state at the computer's weapon choice's end, for the weapon checks: the first
    turn's menu (to_menu), KEYS(box) typed there, the computer's dispatch (5045:0891, COMPUTER) spun
    as the entry, then given back once the panel's entry (177e:085f, PANEL_CALLEE) is spun in its
    place, held. Returns the snapshot there (stop) and panel_frames (the calls it is in, weapon_guard)."""
    with cokbox.Box(workdir=work / f"box-{tag}-weapon", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t = arm(box, menu["handle"], COMPUTER)
        keys(box)
        wait_trap(box, t, "5045:0891")
        seg, off = box.from_ghidra(*PANEL_CALLEE)
        panel = [(seg * 16 + off, box.read(seg * 16 + off, 2))]
        box.write_paused([(addr, b"\xeb\xfe") for addr, _ in panel])
        L.untrap(box, t)                       # the panel is armed first: the dispatch runs on
        wait_trap(box, panel, "177e:085f")
        stop = snapshot(box)
        frames = panel_frames(box, seg)
    return dict(stop=stop, frames=frames)


def weapon_guard(v, w, tag):
    """The panel's first call is the weapon choice's: one frame on the stack returns to 5bb8:19aa,
    from the call at 19a5 (panel_frames)."""
    v.check(len(w["frames"]) == 1, f"the panel's entry (177e:085f) is first called from the weapon "
            f"choice's call (5bb8:19a5, return 19aa) after {tag}", f"frames {w['frames']}")


def weapon_entry(v, tag, entry, stop, weapon):
    """The computer's weapon choice (5bb8:1396) checked against the original alone, at the panel's
    call (weapon_original's stop, 177e:085f in the choice): the RandSeed there is the entry's plus
    exactly weapon_draws, the draws of the pattern and of the pick (none in the choice itself). The
    pick's first try is accepted only if 52db:1030 refuses no enemy: it refuses only by event 1's
    handlers, so no chain (of any record: the party's and the enemies') may hold one. The entry's
    pattern (+0x16) and target (+0x0b) are 0 (the pattern draws 2: d8, then d2 or d4). WEAPON is the
    index into entry["sides"] of the enemies' side (0 for the party, 1 for the monsters). eclplay's
    own state is not compared here: its stops come later (altq_missile's flight, for one)."""
    d = L.draws(entry["seed"], stop["seed"])
    expected = weapon_draws(entry["sides"][weapon])
    held = [i for i, nodes in enumerate(entry["chains"]) if any(n[0] in EVENT1_IDS for n in nodes)]
    v.check(not held, f"no record holds an event-1 effect (0x19, 0x25, 0x45) at the entry after {tag}: "
            "52db:1030 would refuse the pick's first try and the count would not be exact",
            f"records {held}")
    v.check(d == expected, f"RandSeed at the panel's call in the weapon choice after {tag} is the "
            f"entry's plus exactly {expected} draws (the pattern's 2, then the pick: one roll unused "
            f"and one for its first try, which finds a target with {entry['sides'][weapon]} enemies, "
            "none if fewer than 2)", f"{stop['seed']:#x}, {d} draws")
    pre = [bytes.fromhex(b) for b in entry["blocks"]]
    v.check(all(b[0x16] == 0 and b[0x0B:0x0F] == bytes(4) for b in pre),
            f"the pattern (+0x16) and target (+0x0b) of every record are 0 at the entry after "
            f"{tag}: the pattern draws 2 (d8, then d2 or d4)",
            [i for i, b in enumerate(pre) if b[0x16] or any(b[0x0B:0x0F])])


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
    """Quick (5045:0a2c): the original's state at its hand-on (5045:0a56, before the computer's turn for c), checked
    as before: the wait's one draw, the turn index, the records' COMPUTER flags, record 1's initiative and the bar's
    DS:1cb5. eclplay now plays that turn (cok_combat_computer, part 18a) at once, so its run goes on to the next
    menu (out of keys, BATTLE_STOP), with the records' COMPUTER flags as Quick leaves them."""
    o = command_original(work, patched, "Q", opts.verbose)
    p = command_port(work, patched, "Q", "Q")
    s, m = o["stop"], o["menu"]
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay plays the computer's turn for c after Q and waits at the next menu (out of keys)",
            p["last"])
    draws = L.draws(m["seed"], s["seed"])
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
    ported = tuple(bytes.fromhex(r)[0x18C] for r in ported_records(p))
    v.check(ported == QUICK_AUTO, "eclplay's COMPUTER (+0x18c) of the records at the next menu after Q "
            "agrees with the original's", f"eclplay {ported}, the original {QUICK_AUTO}")


def alt_q_keys(box):
    box.keys("\\@Q")


def alt_q_check(v, work, patched, opts):
    """Alt-Q (the computer's turn of record 1, the bow chosen): the original's dispatch (command_original:
    the state there, checked alone) and its weapon choice (weapon_original's panel call, weapon_entry).
    The flight that follows is altq_missile's: the state there is compared in that flow, not here."""
    o = command_original(work, patched, "AltQ", opts.verbose)
    w = weapon_original(work, patched, opts.verbose, "AltQ", alt_q_keys)
    s, m = o["stop"], o["menu"]
    weapon_guard(v, w, "Alt-Q")
    weapon_entry(v, "AltQ", s, w["stop"], weapon=1)
    draws = L.draws(m["seed"], s["seed"])
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


MISSILE_ENTRY = (0x5045, (0x52DB - 0x5045) * 16 + 0x2888)  # 52db:2888 in overlay 0's copies (arm)
MISSILE_DRAWS = 1039                    # RandSeed draws past the menu at the flight's entry (1034 past the panel)
STRIKE_EFFECT = (0x148F, 0x077F)        # 148f:077f, mov al, 0x69 (b0 69): event 4 on the attacker, after the flight
MISSILE_STOP_DRAWS = 1044               # at the strike: 1039, the flight's 3 frames (1df9:052a(20)), 148f:1771's d20, 148f:199e's die
MISSILE_RECORD = "57fb:04da"            # DS:ad67 at the flight: the selected record (combatant 4)
MISSILE_SECONDS = 600                   # the entry comes after about 5.5 minutes of computer turns (the strike after it)
MISSILE_STILL = 1.0                     # RandSeed must not move over this much game time, twice


def still(box, seconds=MISSILE_STILL):
    """RandSeed unchanged over SECONDS of the game's time."""
    seed = box.ds_dword(L.DS_SEED)
    box.settle(seconds)
    return seed == box.ds_dword(L.DS_SEED)


def wait_missile(box, t, where):
    """Waits until the trap T (written paused, at WHERE) is reached: the spin holds (the keyboard polls
    still over 0.6 s), and RandSeed does not move over a second, twice. The computer's turns run with no
    key wait, so the polls alone hold between turns too: the RandSeed test is what shows the flight."""
    end = time.monotonic() + MISSILE_SECONDS
    while time.monotonic() < end:
        box.settle(0.5)
        if L.held(box, t, seconds=0.6)[0] and still(box) and still(box):
            return
    raise cokbox.BoxError(f"the original's trap at {where} is not reached after {MISSILE_SECONDS} s")


def missile_original(work, patched, verbose):
    """The original's states at Alt-Q's first missile flight: its entry (52db:2888, MISSILE_ENTRY) and its
    strike (148f:077f, STRIKE_EFFECT, after the flight). The first turn's menu (to_menu), \\@Q typed there,
    and the entry trapped in the copies of its overlay; then the entry given back and the strike trapped in
    the root image (write_paused, the bytes checked first), and the strike's state kept with its screen. Each
    state is a snapshot with DS:ad67 as `selected`; the menu's seed is for the draws."""
    with cokbox.Box(workdir=work / "box-altq-missile", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t = arm(box, menu["handle"], MISSILE_ENTRY)
        alt_q_keys(box)
        wait_missile(box, t, "52db:2888")
        entry = snapshot(box)
        entry["selected"] = selected(box)
        seg, off = box.from_ghidra(*STRIKE_EFFECT)
        addr = seg * 16 + off
        found = box.read(addr, 2)
        if found != b"\xb0\x69":
            raise cokbox.BoxError(f"148f:077f is not mov al, 0x69 (b0 69): {found.hex()}")
        strike = [(addr, b"\xb0\x69")]
        box.write_paused([(addr, b"\xeb\xfe")])
        L.untrap(box, t)                # the entry given back: the flight runs on to the strike
        wait_missile(box, strike, "148f:077f")
        stop = snapshot(box)
        stop["selected"] = selected(box)
        stop["screen"] = box.screenshot(path=str(work / "altq-stop.png"))
        L.untrap(box, [(addr, b"\xb0\x69")])
    return dict(menu=menu, entry=entry, stop=stop)


def altq_missile(v, work, opts):
    """Alt-Q's first missile flight (step 4, part 15c; the stop is the strike's since part 17b, see the module
    docstring): the original's entry (52db:2888) and strike (148f:077f, missile_original) against eclplay's run
    to the strike (MISSILE_STOP, the keys "\\r\\r\\@Q"): RandSeed at the entry (MISSILE_DRAWS past the menu)
    and at the strike (MISSILE_STOP_DRAWS), every record and combat block, the turn list (command_state), the
    combatants (--field), the selected record (DS:ad67) and the whole screen (the flight's restore is in it)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    o = missile_original(work, patched, opts.verbose)
    p = command_port(work, patched, "AltQ", "\\@Q", exe=missile_eclplay())
    m, e, s = o["menu"], o["entry"], o["stop"]
    v.check(MISSILE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay stops at effect 0x69 (78e4:1aa2, the strike after the flight) after Alt-Q", p["last"])
    entry_draws = L.draws(m["seed"], e["seed"])
    v.check(entry_draws == MISSILE_DRAWS, f"the original's RandSeed at the flight's entry (52db:2888) after "
            f"Alt-Q is {MISSILE_DRAWS} draws past the menu (1034 past the panel call)", f"{entry_draws} draws")
    draws = command_state(v, "Alt-Q's strike", s, p, m["seed"])
    v.check(draws == MISSILE_STOP_DRAWS, f"the original's RandSeed at the strike after Alt-Q is "
            f"{MISSILE_STOP_DRAWS} draws past the menu (the entry's, the flight's frames, d20 and a die)",
            f"{draws} draws")
    v.check(s["selected"] == MISSILE_RECORD, f"the selected record (DS:ad67) at the strike after Alt-Q "
            f"is {MISSILE_RECORD}", f"{s['selected']}")
    field = p["base"].with_suffix(".field")
    if field.exists():
        fight = parse_field(field)[5]
        v.check(fight == s["combatants"], "the combatants at the strike after Alt-Q agree (eclplay's "
                "--field, the original's combatants)", f"eclplay {fight}, original {s['combatants']}")
    command_screen(v, "at effect 0x69 after Alt-Q's flight", s["screen"], p["base"].with_suffix(".bmp"),
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
TURNS_LINE = "turn: ROLAND OF DUST (initiative 3)"
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


def turns_keys(box):
    for i in range(TURNS_KEYS):
        box.keys("E")
        if i < TURNS_KEYS - 1:
            box.settled(quiet=1.0, tolerance=0)


def turns(v, work, opts):
    """The turns flow: five E keys from the first turn's menu, to SHARKMAN's computer turn (5045:0891,
    turn 6). The original's entry is checked alone: the RandSeed (0x1756582c, no draw past the menu),
    the sides [6, 2], DS:bd2d 6, DS:bebd and the pending counts 0, DS:c2d6 NULL, DS:1cb5 0, and the
    weapon choice's draws and guard (weapon_original, weapon_entry). eclplay's run of the same keys
    goes on through the whole turn (15c) and stops at ROLAND OF DUST's menu (BATTLE_STOP;
    melee compares that stop whole), so no state of the port is compared at SHARKMAN's entry."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    o = turns_original(work, patched, opts.verbose)
    spins_at_placement(v, o["menu"])
    s = o["stop"]
    _, last = port(work, patched, name="turns", typed="\\r\\r" + "E" * TURNS_KEYS)
    v.check(BATTLE_STOP.fullmatch(last or "") is not None,
            "eclplay stops at ROLAND OF DUST's menu (out of keys) after five E", last)
    lines = (work / "port-turns.txt").read_text().splitlines()
    v.check([line for line in lines if line.startswith("turn:")][-1:] == [TURNS_LINE],
            "eclplay's last turn is ROLAND OF DUST (initiative 3)",
            [line for line in lines if line.startswith("turn:")][-1:])
    w = weapon_original(work, patched, opts.verbose, "turns", turns_keys)
    weapon_guard(v, w, "SHARKMAN's turn")
    weapon_entry(v, "turns", s, w["stop"], weapon=0)
    draws = L.draws(o["menu"]["seed"], s["seed"])
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
                           turns=base.with_suffix(".turns"), screen=base.with_suffix(".bmp"),
                           effects=base.with_suffix(".effects"))
    chains_agree(v, s["chains"], base.with_suffix(".effects"), " (round 2)")
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


# The auto flow (step 4, parts 18a, 18b2 and 18b3): the first turn's menu with Alt-Q typed (\\@Q), then each round's end
# in the original (5045:0f6c) to the fight's last, against eclplay's two runs of the block (typed, and --combat auto).
AUTO_ROUNDS = 30                        # the fight's rounds: the round limit (measured three times)
AUTO_MAX = 40                           # a cap on the original's rounds: past it the flow fails
ROUND_END = (0x5045, 0x0F6C)            # dqk 5045:0f6c: the round's end (cok_combat_end_round's start)
ROUND_COUNT = 0xAE4C                    # DS:ae4c, the round count (moved on past the round's end)
ROUND_SECONDS = 600                     # the wait for a round's end (the computer's turns between)
AUTO_KEYS = "\\r\\r"                  # the keys of --combat auto's run (its first turn's Alt-Q is the stub's)
AUTO_TYPED = "\\r\\r\\@Q"            # the keys of the typed run (Alt-Q typed at the first turn's menu)
AUTO_END = "(out of keys in block 1 at 8030)"       # --combat auto's stop: the results' key wait (18b3)
AUTO_RETURN = "(out of keys in block 1 at 8031)"    # the same run with one more Return: the adventure menu
AUTO_LOG_AT = ("static void log_line(cok_adventure *game, const char *kind, const char *text, "
               "void *context)\n{\n    (void)game;\n    (void)context;\n")
AUTO_LOG_CODE = r"""    /* tools/dosbox/checks/dqk_fight.py's auto flow: at each round's end (round.c's mark, "dqk"),
     * the screen and the state as NN.bmp, .records, .blocks, .field and .turns in COK_DQK_ROUNDS. */
    if (strcmp(kind, "dqk") == 0) {
        player *pp = context;
        static unsigned rounds;
        const char *dir = getenv("COK_DQK_ROUNDS");
        if (pp->game == NULL || dir == NULL) return;
        char base[4000], path[4200];
        snprintf(base, sizeof base, "%s/%02u", dir, ++rounds);
        snprintf(path, sizeof path, "%s.bmp", base);
        save(pp, path);
        snprintf(path, sizeof path, "%s.records", base);
        save_records(pp->game, path);
        snprintf(path, sizeof path, "%s.blocks", base);
        save_blocks(pp->game, path);
        snprintf(path, sizeof path, "%s.field", base);
        save_field(pp->game, path);
        snprintf(path, sizeof path, "%s.turns", base);
        save_turns(pp->game, path);
        snprintf(path, sizeof path, "%s.effects", base);
        save_effects(pp->game, path);
        return;
    }
"""
AUTO_ROUND_AT = "bool cok_combat_end_round(cok_adventure *game, bool *done)\n{\n"


def round_log(name, text):
    """src/eclplay.c: the rounds' files written at round.c's mark (the "dqk" kind, AUTO_LOG_CODE)."""
    if AUTO_LOG_AT not in text:
        raise cokbox.BoxError("src/eclplay.c's log_line changed: update dqk_fight.round_log")
    return text.replace(AUTO_LOG_AT, AUTO_LOG_AT + AUTO_LOG_CODE)


def round_mark(name, text):
    """src/round.c: the "dqk" mark logged as each round ends (cok_combat_end_round's start)."""
    if AUTO_ROUND_AT not in text:
        raise cokbox.BoxError("src/round.c's cok_combat_end_round changed: update dqk_fight.round_mark")
    return text.replace(AUTO_ROUND_AT, AUTO_ROUND_AT + '    cok_adventure_log(game, "dqk", "round");\n')


def auto_eclplay():
    """eclplay built with the rounds' mark and writers (checklib.build; cached by the sources)."""
    return checklib.build("eclplay", transforms=[("eclplay.c", round_log), ("round.c", round_mark)])


def auto_port(work, patched, name, typed, combat=None, end=None):
    """eclplay's run of the block from saved game A (--load --play, as port), TYPED keys after the entry,
    with --combat COMBAT if given: its rounds' files (NN.*) in work/auto-NAME, written at each round's
    end (auto_eclplay). With END (a base path), its stop's state is written there too (--records, --state,
    --screen: the end's files). Returns the rounds' directory, the run's last line and its variables."""
    rounds = work / f"auto-{name}"
    if rounds.exists():
        shutil.rmtree(rounds)
    rounds.mkdir(parents=True)
    extra = ["--load", str(saved_entry(work)), "--play"]
    if combat is not None:
        extra += ["--combat", combat]
    if end is not None:
        extra += ["--records", str(end.with_suffix(".records")), "--state", str(end.with_suffix(".state")),
                  "--screen", str(end.with_suffix(".bmp"))]
    os.environ["COK_DQK_ROUNDS"] = str(rounds)
    try:
        variables, last, _ = dqk_ecl.port(work, f"auto-{name}", patched, typed=typed, extra=extra,
                                          block=None, exe=auto_eclplay())
    finally:
        del os.environ["COK_DQK_ROUNDS"]
    return rounds, last, variables


def round_left(box):
    """Whether 5045:059d's turn loop can have left for the round's end: the entry at DS:bd2d is NULL
    (0726-072c, past 0x48 too: 067d), or a side is gone or DS:ae4c >= DS:b45a (06aa-06c7's done)."""
    n = box.ds_byte(L.DS_TURN)
    entry = L.far(box, L.DS_COMBATANT_LIST + 4 * n) if n <= L.COMBATANTS else None
    return entry is None or box.ds_byte(L.DS_SIDE_PARTY) == 0 or box.ds_byte(L.DS_SIDE_ENEMY) == 0 or \
        box.ds_byte(L.DS_ROUND) >= box.ds_byte(L.DS_ROUND_LIMIT)


def wait_round(box, handle, where):
    """Waits until the spin at WHERE (HANDLE's copies) is reached: the keyboard polls hold, the loop
    can have left (round_left), and a second hold sees RandSeed and DS:bd2d unmoved. The polls alone
    are not enough: inside a turn they stand still for more than 0.6 s (round 2's turn 4, SHARKMAN 1's
    computer turn, in every run; round 1's turn 7 in some), the trap's EB FE not yet reached. Returns
    the false holds (a hold that is not the round's end: 41 to 48 in a whole fight)."""
    end = time.monotonic() + ROUND_SECONDS
    false = 0
    while time.monotonic() < end:
        box.settle(0.5)
        if not L.held(box, handle, seconds=0.6)[0]:
            continue
        seen = (box.ds_dword(L.DS_SEED), box.ds_byte(L.DS_TURN))
        if round_left(box) and L.held(box, handle, seconds=0.6)[0] and \
                (box.ds_dword(L.DS_SEED), box.ds_byte(L.DS_TURN)) == seen:
            return false
        false += 1
    raise cokbox.BoxError(f"the original's trap at {where} is not reached after {ROUND_SECONDS} s")


def round_past_count(box, stopped):
    """Whether DS:ae4c has moved on from STOPPED (the round's end has been let go), within the wait."""
    end = time.monotonic() + ROUND_SECONDS
    while time.monotonic() < end:
        if box.ds_byte(ROUND_COUNT) != stopped:
            return True
        time.sleep(0.005)
    return False


def auto_original(work, patched, verbose):
    """The first turn's menu (to_menu), Alt-Q typed there (\\@Q), then each round's end until the fight's last
    (AUTO_MAX at most): each spun at 5045:0f6c (trapped), its state read, and its screen read flipped
    (flip_continue, which gives the routine back). The round is the last when a side is gone or DS:ae4c + 1 >=
    DS:b45a (0f6c's done, read before its DS:ae4c++): then no trap is armed. Otherwise the round's end is let go
    and DS:ae4c seen to move on, before the next trap. Returns the menu's state, the rounds' (state, screen,
    count, limit, sides, DS:1cb5 before the flip) and the strict wait's false holds."""
    out = dict(rounds=[], false_holds=0)
    with cokbox.Box(workdir=work / "box-auto", game="dark-queen", verbose=verbose, timeout=1800,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        out["menu"] = to_menu(box)
        box.keys("\\@Q")                  # Alt-Q typed at the first turn's menu
        for n in range(1, AUTO_MAX + 1):
            handle = L.trap(box, *ROUND_END, seconds=ROUND_SECONDS)
            out["false_holds"] += wait_round(box, handle, f"round {n}'s end")
            stop = snapshot(box)
            stopped = box.ds_byte(ROUND_COUNT)
            limit = box.ds_byte(L.DS_ROUND_LIMIT)
            sides = [box.ds_byte(L.DS_SIDE_PARTY), box.ds_byte(L.DS_SIDE_ENEMY)]
            begun, shot = L.flip_continue(box, handle, str(work / f"auto-round{n:02}-original.png"))
            out["rounds"].append(dict(stop=stop, screen=shot, count=stopped, limit=limit, sides=sides,
                                      begun=begun))
            if sides[0] == 0 or sides[1] == 0 or stopped + 1 >= limit:
                break                           # the fight's last round: no trap after it
            if not round_past_count(box, stopped):
                raise cokbox.BoxError(f"round {n}'s end is not let go (DS:ae4c stays {stopped})")
        else:
            raise cokbox.BoxError(f"the fight is not over after {AUTO_MAX} rounds")
        # The end (18b3): no trap is armed, and no continue question is asked (b7f3 stays 1): the first key
        # wait is the results'. Then Return: the adventure menu (mode 4). snapshot() is not called here.
        out["end_screen"] = key_wait(box)
        out["end"] = end_state(box)
        box.keys("\\r")
        out["back_screen"] = key_wait(box)
        out["back"] = end_state(box)
    return out


def key_wait(box, seconds=120):
    """The screen at the game's next key wait (won_original's wait: box.waiting, a limit of SECONDS), settled."""
    box._last_polls = box.hook("polls")
    box._streak = 0
    end = time.monotonic() + seconds
    while not box.waiting():
        if time.monotonic() > end:
            raise cokbox.BoxError(f"no key wait after {seconds} s")
        time.sleep(0.05)
    return box.settled(tolerance=64)


def end_state(box):
    """The original's state at a key wait after the fight's last round: the reads of won_original's E (the
    flags, the results' DS:af3f, c7de, c7df, b1da, b7a7, ae4f, the pool's coins at b1db, b1df, b1e3, DS:b2f5),
    the round count and limit, the sides, the party's records (raw) and the 1 KB of variables. The combat
    records are freed after the last round, so the turn list is not read (turn_order raises)."""
    return dict(mode=box.ds_word(L.DS_MODE), seed=box.ds_dword(L.DS_SEED), standing=box.ds_byte(0xAF3F),
                fled=box.ds_byte(0xC7DE), monsters=box.ds_byte(0xC7DF), out=box.ds_byte(0xB1DA),
                b7a7=box.ds_byte(0xB7A7), share=int.from_bytes(box.ds_read(0xAE4F, 4), "little"),
                pool=tuple(int.from_bytes(box.ds_read(at, 4), "little") for at in (0xB1DB, 0xB1DF, 0xB1E3)),
                flags=list(box.ds_read(0xB2F5, 6)), count=box.ds_byte(ROUND_COUNT),
                limit=box.ds_byte(L.DS_ROUND_LIMIT), sides=[box.ds_byte(L.DS_SIDE_PARTY), box.ds_byte(L.DS_SIDE_ENEMY)],
                records=[raw.hex() for _, raw in L.records(box)], variables=block_vars(box))


def state_lines(e):
    """eclplay's --state lines (eclplay.c's save_state) for the original's end_state E."""
    return [f"mode {e['mode']}", f"seed {e['seed']:08x}", "flags " + " ".join(str(b) for b in e["flags"]),
            f"standing {e['standing']}", f"fled {e['fled']}", f"monsters {e['monsters']}", f"out {e['out']}",
            f"b7a7 {e['b7a7']}", f"share {e['share']}", "pool " + " ".join(str(p) for p in e["pool"])]


def auto_expected(n):
    """The fight's values at round N's end (measured three times): DS:b45a (the round limit:
    15 until round 15's attack, 29 at its end, 30 from round 16) and the sides ([6, 2] to round 15, [6, 1]
    from 16: SHARKMAN 1 got away in round 16)."""
    return (15 if n <= 14 else 29 if n == 15 else 30), ([6, 2] if n <= 15 else [6, 1])


def auto(v, work, opts):
    """The auto flow (step 4, parts 18a and 18b2): Alt-Q typed at the first turn's menu in the original, each round's
    end read to the fight's last (auto_original), against eclplay's two runs (AUTO_TYPED and --combat auto with
    AUTO_KEYS), which must write the same round files: AUTO_ROUNDS of them. Each round's records, blocks (fleeing
    included), turn list, combatants, battlefield, RandSeed and screen agree with the original's (MENU_POINTER
    masked); the round count, DS:b45a and the sides are the fight's (auto_expected). The end (18b3): the last round
    is at the round limit with the sides [6, 1] (no continue question); the first key wait after it is the results'
    (eclplay's --combat auto run stops there, block 1 at 8030: its --state, --records, --screen and variables agree
    with the original's reads, end_state), and Return gives the adventure menu, mode 4, RandSeed unchanged (the
    same run with one more Return: 8031; its screen, records and variables agree)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    o = auto_original(work, patched, opts.verbose)
    rounds = o["rounds"]
    flipped = sum(1 for r in rounds if r["begun"])
    print(f"original (auto): {len(rounds)} rounds, {o['false_holds']} false holds of the strict wait, "
          f"DS:1cb5 set before the flip in {flipped} rounds", flush=True)
    typed, typed_last, _ = auto_port(work, patched, "typed", AUTO_TYPED)
    combat, combat_last, end_vars = auto_port(work, patched, "combat", AUTO_KEYS, combat="auto",
                                              end=work / "auto-end")
    _, back_last, back_vars = auto_port(work, patched, "return", AUTO_KEYS + "\\r", combat="auto",
                                           end=work / "auto-back")
    print(f"port (auto): typed ends {typed_last!r}; --combat auto ends {combat_last!r}; with Return "
          f"{back_last!r}", flush=True)
    files = sorted(p.name for p in combat.glob("*.bmp"))
    v.value(f"the original's fight is {AUTO_ROUNDS} rounds (the round limit)", len(rounds), AUTO_ROUNDS)
    v.check(len(files) == AUTO_ROUNDS, f"eclplay writes {AUTO_ROUNDS} rounds' files (--combat auto)",
            f"{len(files)} rounds")
    same = all(filecmp.cmp(typed / f"{n:02}.{ext}", combat / f"{n:02}.{ext}", shallow=False)
               for n in range(1, len(rounds) + 1)
               for ext in ("bmp", "records", "blocks", "field", "turns"))
    v.check(same, "the typed Alt-Q and --combat auto write the same rounds' files (all "
            f"{len(rounds)} rounds: screens, records, blocks, field, turns)")
    for n, r in enumerate(rounds, 1):
        base = combat / f"{n:02}"
        p = dict(base=base, last=combat_last)
        s = r["stop"]
        limit, sides = auto_expected(n)
        v.value(f"DS:ae4c (the round count) before round {n}'s end is {n - 1}", r["count"], n - 1)
        v.value(f"DS:b45a (the round limit) at round {n}'s end is {limit}", r["limit"], limit)
        v.value(f"the sides at round {n}'s end are {sides}", r["sides"], sides)
        v.value(f"record 7's fleeing (block +0x15) at round {n}'s end is {1 if n >= 16 else 0}",
                bytes.fromhex(s["blocks"][7])[0x15], 1 if n >= 16 else 0)
        draws = command_state(v, f"round {n} (auto)", s, p, o["menu"]["seed"])
        field = base.with_suffix(".field")
        if field.exists():
            _, _, _, cells, occupants, fight = parse_field(field)
            bf = s["field"]
            cells_off = sum(1 for i in range(0, len(cells), 2) if cells[i:i + 2] != bf["cells"][i:i + 2])
            occ_off = sum(1 for i in range(0, len(occupants), 2)
                          if occupants[i:i + 2] != bf["occupants"][i:i + 2])
            v.check(cells_off == 0 and occ_off == 0,
                    f"the battlefield's cells and occupants agree at round {n}'s end (eclplay's --field)",
                    f"{cells_off} cells and {occ_off} occupants differ")
            v.check(fight == s["combatants"], f"the combatants agree at round {n}'s end (--field)",
                    f"eclplay {fight} against {s['combatants']}")
        command_screen(v, f"at round {n}'s end (auto)", r["screen"], base.with_suffix(".bmp"),
                       MENU_POINTER)
        print(f"round {n} (auto): {draws} RandSeed draws past the first menu; DS:1cb5 before the flip "
              f"{r['begun']}", flush=True)
    # The end (18b3): the last round at the round limit (no continue question), the results' wait, and Return.
    last = rounds[-1]
    v.check(last["sides"] == [6, 1] and last["count"] + 1 >= last["limit"],
            "the last round ends at the round limit with the sides [6, 1] (DS:ae4c + 1 >= DS:b45a): no continue "
            "question", f"sides {last['sides']}, count {last['count']}, limit {last['limit']}")
    v.check(combat_last == AUTO_END, "eclplay's --combat auto run ends at the results' wait (block 1, 8030)",
            combat_last)
    v.check(back_last == AUTO_RETURN, "eclplay's run with Return ends at the menu (block 1, 8031)", back_last)
    e, b = o["end"], o["back"]
    end_base = work / "auto-end"
    state_file = end_base.with_suffix(".state")
    state = state_file.read_text().splitlines() if state_file.exists() else []
    v.check(state == state_lines(e), "eclplay's --state at the results' wait is the original's (the WON_STATE "
            "format, the pool's coins too)", f"eclplay {state}, the original {state_lines(e)}")
    ported = ported_records(dict(base=end_base))
    differ = [i for i in range(min(len(ported), len(e["records"]))) if masked(ported[i]) != masked(e["records"][i])]
    v.check(not differ and len(ported) == len(e["records"]), "every record agrees at the results' wait (pointers "
            "masked)", f"{len(ported)} against {len(e['records'])}; differ: {' '.join(map(str, differ))}")
    v.check(end_vars == e["variables"], "the variables agree at the results' wait (the 1 KB)")
    command_screen(v, "at the results' wait (auto)", o["end_screen"], end_base.with_suffix(".bmp"), WON_WAIT_MASKS)
    back_base = work / "auto-back"
    back_file = back_base.with_suffix(".state")
    back_state = back_file.read_text().splitlines() if back_file.exists() else []
    v.check(b["mode"] == 4 and b["seed"] == e["seed"] and back_state[:2] == ["mode 4", f"seed {e['seed']:08x}"],
            "after Return: the adventure menu (mode 4), RandSeed unchanged (eclplay's too)",
            f"original mode {b['mode']}, seed {b['seed']:#x} (end {e['seed']:#x}); eclplay {back_state[:2]}")
    ported = ported_records(dict(base=back_base))
    differ = [i for i in range(min(len(ported), len(b["records"]))) if masked(ported[i]) != masked(b["records"][i])]
    v.check(not differ and len(ported) == len(b["records"]), "every record agrees at the menu (pointers masked)",
            f"{len(ported)} against {len(b['records'])}; differ: {' '.join(map(str, differ))}")
    v.check(back_vars == b["variables"], "the variables agree at the menu (the 1 KB)")
    command_screen(v, "after Return (the menu, auto)", o["back_screen"], back_base.with_suffix(".bmp"),
                   WON_SCREEN_MASK)


# The melee flow (step 4, part 15c: one round of the computer's fight, the stops of 15c's loop and
# weapon choice in the original). From the first turn's menu: five E keys (settled between), SHARKMAN
# 1's computer turn (turn 6: 5045:0891 spun in its copies, as `turns`), then the trap given back and
# ROLAND OF DUST's menu (turn 7: 5045:08cf spun, sel 57fb:0000); E for ROLAND, SHARKMAN 2's turn (turn
# 8) and round 2's first menu (TIMASALIN, sel 57fb:019e). eclplay's stops are "\r\r" + "E" * 5 and
# "\r\r" + "E" * 6, out of keys at each menu (BATTLE_STOP).
MELEE_ROLAND = "57fb:0000"               # DS:ad67 at ROLAND OF DUST's menu (turn 7)
MELEE_TIMASALIN = "57fb:019e"            # DS:ad67 at round 2's first menu
MELEE_SEED_ROLAND = 0x48F85199           # RandSeed at ROLAND's menu: 21 draws past the first menu's
MELEE_SEED_ROUND2 = 0x473B4BC2           # RandSeed at round 2's first menu: 29 draws past ROLAND's
MELEE_SHARKMAN1 = "00000100000200000100023c03fb57000000000001000601"  # raw: target 57fb:033c, pattern 6
MELEE_SHARKMAN1_FROM = (32, 18)          # combatant 8 at the first menu
MELEE_SHARKMAN1_TO = (28, 16)            # combatant 8 at ROLAND's menu
MELEE_SHARKMAN2_TO = (30, 11)            # combatant 7 at round 2's first menu (SHARKMAN 1 still at 28, 16)
MELEE_PATTERN = 0x16                     # the combat block's pattern byte (+0x16)


def melee_original(work, patched, verbose):
    """The original's run of one round from the first turn's menu (to_menu): five E keys, SHARKMAN 1's
    turn (5045:0891 spun, then given back), ROLAND OF DUST's menu (5045:08cf spun, then given back,
    its screen kept), E, and round 2's first menu (5045:08cf spun again; given back once drawn, its
    screen kept). Each stop's state is snapshot's, with the selected record and the tail's bytes."""
    with cokbox.Box(workdir=work / "box-melee", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t91 = arm(box, menu["handle"], COMPUTER)
        for i in range(TURNS_KEYS):
            box.keys("E")
            if i < TURNS_KEYS - 1:
                box.settled(quiet=1.0, tolerance=0)
        wait_trap(box, t91, "5045:0891")
        sharkman1 = stop_state(box)
        t08 = arm(box, menu["handle"], MENU)
        L.untrap(box, t91)
        wait_trap(box, t08, "5045:08cf (ROLAND OF DUST's menu)")
        roland = stop_state(box)
        L.untrap(box, t08)
        box.settled(quiet=1.0, tolerance=0)
        roland["screen"] = box.screenshot(path=str(work / "melee-roland.png"))
        box.keys("E")
        t08b = arm(box, menu["handle"], MENU)
        wait_trap(box, t08b, "5045:08cf (round 2's first menu)")
        round2 = stop_state(box)
        L.untrap(box, t08b)
        box.settled(quiet=1.0, tolerance=0)
        round2["screen"] = box.screenshot(path=str(work / "melee-round2.png"))
        return dict(menu=menu, sharkman1=sharkman1, roland=roland, round2=round2)


def stop_state(box):
    """A stop of the melee run: snapshot, the tail's bytes, the sides, the selected record and its
    computer byte (+0x18c: 0 for a menu of the party's)."""
    st = snapshot(box)
    st.update(tail_state(box), sides=[box.ds_byte(0xB7F2), box.ds_byte(0xB7F3)],
              selected=selected(box))
    seg, off = (int(x, 16) for x in st["selected"].split(":"))
    st["computer"] = box.read(seg * 16 + off + 0x18C, 1)[0]
    return st


def melee_port(work, patched, name, keys):
    """eclplay's run of the melee flow to the stop after KEYS E keys (the first menu's "\\r\\r" first):
    the files its state is read from (base: field, records, blocks, turns, effects, bmp) and its last line."""
    base = work / f"port-{name}"
    variables, last = port(work, patched, name=name, typed="\\r\\r" + "E" * keys,
                           field=base.with_suffix(".field"), records=base.with_suffix(".records"),
                           blocks=base.with_suffix(".blocks"), turns=base.with_suffix(".turns"),
                           screen=base.with_suffix(".bmp"), effects=base.with_suffix(".effects"))
    return dict(base=base, vars=variables, last=last)


def block_index(blocks_raw, want):
    """The records whose raw combat block is want (hex): their places in the list."""
    return [i for i, b in enumerate(blocks_raw) if b == want]


def melee(v, work, opts):
    """The melee flow (step 4, part 15c): the round's first two stops of the first fight, the original's
    (melee_original) against eclplay's (melee_port) at each: ROLAND OF DUST's menu after SHARKMAN 1's
    turn (RandSeed 21 draws past the first menu, combatant 8 walked from 32, 18 to 28, 16, its block with
    the pattern 6 and the target 57fb:033c, every record and block, the turn list, the screen), and round
    2's first menu after SHARKMAN 2's turn and the round's end (29 draws past ROLAND's menu, SHARKMAN 2
    at 30, 11, every record and block, the screen once the menu is drawn)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    o = melee_original(work, patched, opts.verbose)
    spins_at_placement(v, o["menu"])
    m, a, r, r2 = o["menu"], o["sharkman1"], o["roland"], o["round2"]
    v.check(r["selected"] == MELEE_ROLAND and r2["selected"] == MELEE_TIMASALIN,
            f"the original's selected records: ROLAND OF DUST "
            f"({MELEE_ROLAND}) and TIMASALIN ({MELEE_TIMASALIN}) at the stops",
            f"{r['selected']} and {r2['selected']}")
    v.check(r["computer"] == 0 and r2["computer"] == 0, "the original's menus are the party's (+0x18c 0)",
            f"{r['computer']} and {r2['computer']}")
    # SHARKMAN 1: the combatant's walk (combatant 8, the first menu's place to ROLAND's), and the
    # block (the raw one; its pattern and target) as the original has them.
    from_m = {n: (x, y) for n, x, y, _ in m["combatants"]}
    to_r = {n: (x, y) for n, x, y, _ in r["combatants"]}
    v.check(from_m.get(8) == MELEE_SHARKMAN1_FROM and to_r.get(8) == MELEE_SHARKMAN1_TO,
            "combatant 8 (SHARKMAN 1) walks from 32, 18 to 28, 16 by ROLAND's menu",
            f"{from_m.get(8)} to {to_r.get(8)}")
    v.check(to_r.get(7) == from_m.get(7), "SHARKMAN 2 (combatant 7) has not moved at ROLAND's menu "
            "(it moves at turn 8)", f"{from_m.get(7)} then {to_r.get(7)}")
    where = block_index(r["blocks_raw"], MELEE_SHARKMAN1)
    v.check(len(where) == 1, "SHARKMAN 1's combat block at ROLAND's menu is the measured one (initiative "
            "0, movement 0, guarding 1, facing 2, target 57fb:033c, pattern 6)", where)
    if len(where) == 1:
        pattern = bytes.fromhex(r["blocks_raw"][where[0]])[MELEE_PATTERN]
        v.check(pattern == 6, "its pattern (+0x16) is 6: the stuck steps' (part15 Correction)", pattern)
    # eclplay's stop after five E, against ROLAND's menu.
    pa = melee_port(work, patched, "melee-roland", 5)
    v.check(BATTLE_STOP.fullmatch(pa["last"] or "") is not None,
            "eclplay stops at ROLAND OF DUST's menu (out of keys) after five E", pa["last"])
    chains_agree(v, r["chains"], pa["base"].with_suffix(".effects"), " (ROLAND's menu)")
    draws = command_state(v, "ROLAND's menu", r, pa, m["seed"])
    v.check(r["seed"] == MELEE_SEED_ROLAND and L.draws(m["seed"], r["seed"]) == 21,
            f"RandSeed at ROLAND's menu is {MELEE_SEED_ROLAND:#x}, 21 draws past the first menu's",
            f"{r['seed']:#x}, {draws} draws")
    ported_field = parse_field(pa["base"].with_suffix(".field"))
    v.check(sorted(ported_field[5]) == sorted(tuple(c) for c in r["combatants"]),
            "every combatant's place and size agree at ROLAND's menu (eclplay's --field)",
            f"{ported_field[5]} against {r['combatants']}")
    command_screen(v, "at ROLAND OF DUST's menu", r["screen"], pa["base"].with_suffix(".bmp"),
                   MENU_POINTER)
    # eclplay's stop after six E: round 2's first menu, after SHARKMAN 2's turn and the round's end.
    pb = melee_port(work, patched, "melee-round2", 6)
    v.check(BATTLE_STOP.fullmatch(pb["last"] or "") is not None,
            "eclplay stops at round 2's first menu (out of keys) after six E", pb["last"])
    chains_agree(v, r2["chains"], pb["base"].with_suffix(".effects"), " (round 2)")
    draws = command_state(v, "round 2's first menu", r2, pb, r["seed"])
    v.check(r2["seed"] == MELEE_SEED_ROUND2 and L.draws(r["seed"], r2["seed"]) == 29,
            f"RandSeed at round 2's first menu is {MELEE_SEED_ROUND2:#x}, 29 draws past ROLAND's menu",
            f"{r2['seed']:#x}, {draws} draws")
    to_2 = {n: (x, y) for n, x, y, _ in r2["combatants"]}
    v.check(to_2.get(7) == MELEE_SHARKMAN2_TO and to_2.get(8) == MELEE_SHARKMAN1_TO,
            "SHARKMAN 2 is at 30, 11 and SHARKMAN 1 still at 28, 16 at round 2's first menu",
            f"{to_2.get(7)}, {to_2.get(8)}")
    command_screen(v, "at round 2's first menu", r2["screen"], pb["base"].with_suffix(".bmp"),
                   MENU_POINTER)


# The melee-long flow (step 4, part 15d: the computer's fight to round 6's first menu). Each party menu
# (5045:08cf) is trapped at its entry, E is typed once the menu is drawn and waiting (the trap is given
# back and armed again before the key); each computer turn (5045:0891) is trapped and given back at once.
# The six party members make six menus a round. At each round's first menu (DS:bd2d 1) the state is read
# (stop_state) and the screen kept; eclplay's stop after 6 * (round - 1) E keys is compared with it.
MELEE_LONG_ROUNDS = 6            # the rounds whose first menus are compared: 2 to 6
MELEE_MENUS = 6                  # the party's menus a round (six party members)
MELEE_LONG_STOPS = 60            # a cap on the menus the original's run stops at (30 are needed)
MELEE_LONG = {                   # round: RandSeed, SHARKMAN 2 (combatant 7), SHARKMAN 1 (combatant 8),
    2: (0x473B4BC2, (30, 11), (28, 16), 134),   # ROLAND OF DUST's hit points (scope.md section 0's
    3: (0x03F33312, (26, 9), (25, 16), 134),    # table says TIMASALIN: record 0 is ROLAND, the one
    4: (0xBD18FE62, (32, 13), (22, 16), 134),   # the first strike hits; TIMASALIN stays 61)
    5: (0xD1724551, (26, 11), (27, 16), 134),
    6: (0xAD8313C7, (28, 12), (20, 16), 126),
}
HIT_POINTS = 0x198               # a record's hit points (rules_dark_queen.c: COK_FIELD_HIT_POINTS)
NAME_AT = (0x68, 0x78)           # a record's name
HIT_NAME = "ROLAND OF DUST"      # the party's first record (its hit points are the table's)


def melee_long_original(work, patched, verbose):
    """The original's run from the first turn's menu (to_menu) to round 6's first menu. Two traps are
    armed in the copies of 078b's (arm): the party's act (5045:08cf, a menu's entry) and the computer's
    dispatch (5045:0891, before its lcall 5bb8:0000). A stop at 0891 (the selected record's +0x18c set) is
    given back at once (its trap armed again after 0.25 s, as scope.md's measurement did) and no key is typed; a
    stop at 08cf is a party menu: its trap given back, the menu drawn (settled), the trap armed again, E
    typed. Each stop reads DS:bd2d and the selected record; at a round's first menu (DS:bd2d 1) the whole
    state (stop_state) and the screen (melee-long-round<r>.png)."""
    with cokbox.Box(workdir=work / "box-melee-long", game="dark-queen", verbose=verbose,
                    timeout=1800, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        handle = menu["handle"]
        t_menu = arm(box, handle, MENU)
        t_comp = arm(box, handle, COMPUTER)
        box.keys("E")
        rounds, stops = {}, 0
        while True:
            if stops >= MELEE_LONG_STOPS:
                raise cokbox.BoxError(f"the original's party menus do not reach round "
                                      f"{MELEE_LONG_ROUNDS}'s first menu in {MELEE_LONG_STOPS} stops")
            stops += 1
            wait_trap(box, t_menu + t_comp, "5045:08cf or 5045:0891 (a party menu or a computer's turn)")
            stop = dict(turn=box.ds_byte(L.DS_TURN), selected=selected(box), seed=box.ds_dword(L.DS_SEED))
            seg, off = (int(x, 16) for x in stop["selected"].split(":"))
            stop["computer"] = box.read(seg * 16 + off + 0x18C, 1)[0]
            stop["name"] = cstr(box.read(seg * 16 + off + NAME_AT[0], NAME_AT[1] - NAME_AT[0]))
            if verbose:
                print(f"  stop {stops}: turn {stop['turn']} selected {stop['selected']} "
                      f"seed {stop['seed']:#x} computer {stop['computer']}", flush=True)
            if stop["computer"]:
                L.untrap(box, t_comp)
                time.sleep(0.25)
                t_comp = arm(box, handle, COMPUTER)
                continue
            first = stop["turn"] == 1
            if first:
                stop.update(stop_state(box), turn=1)
            L.untrap(box, t_menu)
            box.settled(quiet=1.0, tolerance=0)
            if first:
                r = 2 + len(rounds)
                stop["screen"] = box.screenshot(path=str(work / f"melee-long-round{r}.png"))
                rounds[r] = stop
                if len(rounds) == MELEE_LONG_ROUNDS - 1:
                    break
            t_menu = arm(box, handle, MENU)
            box.keys("E")
        return dict(menu=menu, rounds=rounds)


def melee_long(v, work, opts):
    """The melee-long flow (step 4, part 15d): the computer's fight from the first turn's menu to round 6's
    first menu, the original's (melee_long_original) against eclplay's (melee_port with 6 * (round - 1) E
    keys) at each round's first menu, in the table of scope.md's section 0: RandSeed, SHARKMAN 2 (7) and
    SHARKMAN 1 (8) places, ROLAND OF DUST's hit points (round 6: 126, after SHARKMAN 2's first strike in round
    5). At each: the selected record (the turn list's first entry, a party menu), every record and combat block,
    the turn list, the effect chains, the combatants' places (--field) and the screen (MENU_POINTER masked)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    o = melee_long_original(work, patched, opts.verbose)
    spins_at_placement(v, o["menu"])
    rounds = o["rounds"]
    v.check(sorted(rounds) == list(range(2, MELEE_LONG_ROUNDS + 1)),
            f"the original stops at the first menu of rounds 2 to {MELEE_LONG_ROUNDS}", sorted(rounds))
    prev_seed = o["menu"]["seed"]
    for r in sorted(rounds):
        s = rounds[r]
        tag = f"round {r}'s first menu"
        seed, sharkman2, sharkman1, hits = MELEE_LONG[r]
        first = {n: idx for n, idx, _ in s["turns"]}.get(1)
        turn_name = None if first is None else cstr(bytes.fromhex(s["records"][first])[NAME_AT[0]:NAME_AT[1]])
        v.check(s["computer"] == 0 and s["name"] == turn_name,
                f"the original's selected record at {tag} is the turn list's first entry ({turn_name}), "
                f"a party menu", f"{s['name']} at {s['selected']}, +0x18c {s['computer']}")
        v.check(s["seed"] == seed, f"RandSeed at {tag} is {seed:#x} (the table)", f"{s['seed']:#x}")
        to = {n: (x, y) for n, x, y, _ in s["combatants"]}
        v.check(to.get(7) == sharkman2 and to.get(8) == sharkman1,
                f"SHARKMAN 2 is at {sharkman2} and SHARKMAN 1 at {sharkman1} at {tag} (the table)",
                f"{to.get(7)}, {to.get(8)}")
        hit = bytes.fromhex(s["records"][0])
        v.check(cstr(hit[NAME_AT[0]:NAME_AT[1]]) == HIT_NAME and hit[HIT_POINTS] == hits,
                f"{HIT_NAME}'s hit points at {tag} are {hits} (the table)", hit[HIT_POINTS])
        p = melee_port(work, patched, f"melee-long-{r}", MELEE_MENUS * (r - 1))
        v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
                f"eclplay stops at {tag} (out of keys) after {MELEE_MENUS * (r - 1)} E", p["last"])
        chains_agree(v, s["chains"], p["base"].with_suffix(".effects"), f" ({tag})")
        draws = command_state(v, tag, s, p, prev_seed)
        v.ok(f"RandSeed draws from the previous first menu to {tag}, the original's", str(draws))
        ported_field = parse_field(p["base"].with_suffix(".field"))
        v.check(sorted(ported_field[5]) == sorted(tuple(c) for c in s["combatants"]),
                f"every combatant's place and size agree at {tag} (eclplay's --field)",
                f"{ported_field[5]} against {s['combatants']}")
        command_screen(v, f"at {tag}", s["screen"], p["base"].with_suffix(".bmp"), MENU_POINTER)
        prev_seed = s["seed"]


# The melee-dying flow (step 4, part 16b: a member dying in the box). The fixture block of melee-long with
# ROLAND OF DUST's hit points (record 0) set to 1 before COMBAT (fight_block(DYING_MEMBER)), both sides. The
# fixture's first strike is round 5's turn 8 and it hits ROLAND (melee-long's table: 134 to 126 at round 6), so
# the kill is that strike's. The stops are melee-long's: each party menu gets its E (keys typed so far counted),
# each computer turn is given back at once. Taken on the way: SHARKMAN 1's turn (its dispatch, 5045:0891, the
# first computer stop), ROLAND OF DUST's menu after it (turn 7, melee's one-round stop: the keys are five E),
# and each round's first menu, 2 to 6. SHARKMAN 1 aims at record 2 (57fb:033c, melee's measured target), which
# is not in reach in round 1: that member keeps its 1 hit point, and only the strike on ROLAND kills.
DYING_MEMBER = 0                         # ROLAND OF DUST (record 0, 57fb:0000)
DYING_TARGET = "57fb:033c"               # SHARKMAN 1's target at ROLAND's menu: record 2 (melee's measurement)
DYING_ROUND_HIT = 5                      # the round of the fixture's first strike (ROLAND, turn 8)
BODY_TABLE, BODY_COUNT, BODY_SIZE = 0xC526, 0xC5D3, 9   # entry k at DS:c526 + 9k (k 1-8), count DS:c5d3
STATUS_AT = 0x64                         # a record's status word (+0x64)
ACTIVE_AT = 0x18B                        # a record's active byte (+0x18b)


def bodies_at(box):
    """The body table as the original keeps it: the count (DS:c5d3) and entries 1 to count (at most 8): each
    (k, index, x, y, cell) with the record's place in the party list (-1 for none), the words signed, cell the
    terrain under the body. Entry k is at DS:c526 + 9k (5837:11c7: the index from 1, times 9, plus c526; part16-17.md's
    step 7): +0 the record far, +4 x, +6 y, +8 cell."""
    listed = L.records(box)
    places = {ptr: i for i, (ptr, _) in enumerate(listed)}
    count = box.ds_byte(BODY_COUNT)
    entries = []
    for k in range(1, min(count, 8) + 1):
        e = box.ds_read(BODY_TABLE + BODY_SIZE * k, BODY_SIZE)
        off, seg = struct.unpack_from("<HH", e, 0)
        x, y = struct.unpack_from("<hh", e, 4)
        index = -1 if (off, seg) == (0, 0) else places.get((seg, off), -1)
        entries.append((k, index, x, y, e[8]))
    return dict(count=count, entries=entries)


def parse_bodies(path):
    """eclplay --field's bodies: the count, then a line each (k, party index, x, y, cell), in the file after the
    combatants (the 'bodies' line)."""
    lines = path.read_text().splitlines()
    i = next(n for n, line in enumerate(lines) if line.startswith("bodies "))
    count = int(lines[i].split()[1])
    return count, [tuple(int(x) for x in line.split()) for line in lines[i + 1:i + 1 + count]]


def dying_original(work, patched, verbose):
    """The original's run from the first turn's menu (to_menu) to round 6's first menu, as melee_long_original's
    (E typed at each party menu, each computer turn given back at once), with the states the flow compares: the
    first computer stop (SHARKMAN 1's turn, before its dispatch's lcall), ROLAND OF DUST's menu after it (state
    read with the trap held, the screen once given back), and each round's first menu 2 to 6 (state, the screen,
    the E keys typed so far: eclplay's count). Each state has the bodies (bodies_at) and the field (snapshot)."""
    with cokbox.Box(workdir=work / "box-melee-dying", game="dark-queen", verbose=verbose,
                    timeout=1800, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        handle = menu["handle"]
        t_menu = arm(box, handle, MENU)
        t_comp = arm(box, handle, COMPUTER)
        box.keys("E")
        typed, stops = 1, 0
        sharkman1 = roland = None
        rounds = {}
        while True:
            if stops >= MELEE_LONG_STOPS:
                raise cokbox.BoxError(f"the original's party menus do not reach round "
                                      f"{MELEE_LONG_ROUNDS}'s first menu in {MELEE_LONG_STOPS} stops")
            stops += 1
            wait_trap(box, t_menu + t_comp, "5045:08cf or 5045:0891 (a party menu or a computer's turn)")
            stop = dict(turn=box.ds_byte(L.DS_TURN), selected=selected(box), seed=box.ds_dword(L.DS_SEED))
            seg, off = (int(x, 16) for x in stop["selected"].split(":"))
            stop["computer"] = box.read(seg * 16 + off + 0x18C, 1)[0]
            stop["name"] = cstr(box.read(seg * 16 + off + NAME_AT[0], NAME_AT[1] - NAME_AT[0]))
            if verbose:
                print(f"  stop {stops}: turn {stop['turn']} selected {stop['selected']} "
                      f"seed {stop['seed']:#x} computer {stop['computer']} typed {typed}", flush=True)
            if stop["computer"]:
                if sharkman1 is None:
                    sharkman1 = dict(stop_state(box), bodies=bodies_at(box), typed=typed)
                L.untrap(box, t_comp)
                time.sleep(0.25)
                t_comp = arm(box, handle, COMPUTER)
                continue
            first = stop["turn"] == 1
            wanted = first or (sharkman1 is not None and roland is None)
            state = dict(stop_state(box), bodies=bodies_at(box), typed=typed) if wanted else None
            L.untrap(box, t_menu)
            box.settled(quiet=1.0, tolerance=0)
            if state is not None and not first:
                state["screen"] = box.screenshot(path=str(work / "melee-dying-roland.png"))
                roland = state
            if first:
                r = 2 + len(rounds)
                state["screen"] = box.screenshot(path=str(work / f"melee-dying-round{r}.png"))
                rounds[r] = state
                if len(rounds) == MELEE_LONG_ROUNDS - 1:
                    break
            t_menu = arm(box, handle, MENU)
            box.keys("E")
            typed += 1
        return dict(menu=menu, sharkman1=sharkman1, roland=roland, rounds=rounds)


def dying_compare(v, tag, s, p, prev_seed):
    """A stop of the melee-dying flow against eclplay's (melee_port's run): the stop's marks (BATTLE_STOP), the
    effect chains, the RandSeed and records, the blocks and the turn list (command_state), the battlefield's cells
    and occupants, the combatants' places, the bodies (eclplay's --field's; part16-17's step 7) and the screen."""
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            f"eclplay stops at {tag} (out of keys) after {s['typed']} E", p["last"])
    chains_agree(v, s["chains"], p["base"].with_suffix(".effects"), f" ({tag})")
    command_state(v, tag, s, p, prev_seed)
    field = p["base"].with_suffix(".field")
    if not field.exists():
        v.check(False, f"eclplay writes its battlefield at {tag}", p["last"])
        return
    ported = parse_field(field)
    v.check(ported[3] == s["field"]["cells"] and ported[4] == s["field"]["occupants"],
            f"the battlefield's cells and occupants agree at {tag} (the body's cell included)",
            f"cells agree {ported[3] == s['field']['cells']}, occupants agree "
            f"{ported[4] == s['field']['occupants']}")
    v.check(sorted(ported[5]) == sorted(tuple(c) for c in s["combatants"]),
            f"every combatant's place and size agree at {tag} (eclplay's --field)",
            f"{sorted(ported[5])} against {s['combatants']}")
    count, entries = parse_bodies(field)
    want = s["bodies"]["entries"][:s["bodies"]["count"]] if s["bodies"]["count"] <= 8 else None
    v.check(want is not None and count == s["bodies"]["count"] and entries == want,
            f"the bodies agree at {tag}: the count and each entry (party index, x, y, cell)",
            f"eclplay {count} {entries} against the original {s['bodies']['count']} {want}")
    command_screen(v, f"at {tag}", s["screen"], p["base"].with_suffix(".bmp"), MENU_POINTER)


def melee_dying(v, work, opts):
    """The melee-dying flow (step 4, part 16b): the fixture with ROLAND OF DUST's hit points at 1 before COMBAT, the
    original's run (dying_original) against eclplay's (melee_port with the E keys typed so far) at ROLAND's menu
    after SHARKMAN 1's turn (five E) and at each round's first menu, 2 to 6. Checks: the write is in place at
    SHARKMAN 1's turn, SHARKMAN 1's walk and block and RandSeed at ROLAND's menu are melee's (the write changes
    nothing before the strike), ROLAND still at 1 hit point in round 1 and 2 to 4 (no strike), the strike kills
    him in round 5 (0 hit points at round 6's first menu: the status is printed), and eclplay agrees at each stop
    (dying_compare). Round 5's end (5045:0f6c) already finds ROLAND dying: +0x0f goes from 2 to 3 and "Your Teammate is
    Dying" is drawn (one draw), so round 6's RandSeed and blocks compare it; its screen is not (the menus' are taken
    after the message's wait)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block(DYING_MEMBER)))
    o = dying_original(work, patched, opts.verbose)
    spins_at_placement(v, o["menu"])
    s1, r = o["sharkman1"], o["roland"]
    lowered = bytes.fromhex(s1["records"][DYING_MEMBER])
    v.check(lowered[HIT_POINTS] == 1 and lowered[STATUS_AT:STATUS_AT + 2] == b"\x00\x00",
            "ROLAND OF DUST has 1 hit point (status 0) at SHARKMAN 1's turn: the write is before COMBAT",
            f"hit points {lowered[HIT_POINTS]}, status {lowered[STATUS_AT:STATUS_AT + 2].hex()}")
    to_r = {n: (x, y) for n, x, y, _ in r["combatants"]}
    v.check(to_r.get(8) == MELEE_SHARKMAN1_TO and r["selected"] == MELEE_ROLAND,
            "SHARKMAN 1 walks to 28, 16 and ROLAND OF DUST's menu is the turn's (melee's stop)",
            f"{to_r.get(8)} at {r['selected']}")
    where = block_index(r["blocks_raw"], MELEE_SHARKMAN1)
    v.check(len(where) == 1, "SHARKMAN 1's combat block at ROLAND's menu is melee's (target 57fb:033c, "
            "pattern 6): the write changed nothing in its turn", where)
    aim = bytes.fromhex(r["blocks_raw"][where[0]])[TARGET_AT:TARGET_AT + 4] if len(where) == 1 else b""
    off, seg = struct.unpack_from("<HH", aim, 0) if len(aim) == 4 else (0, 0)
    v.check(f"{seg:04x}:{off:04x}" == DYING_TARGET, f"SHARKMAN 1 aims at {DYING_TARGET}: the party's third "
            "member (record 2), not ROLAND", f"{seg:04x}:{off:04x}")
    v.check(r["seed"] == MELEE_SEED_ROLAND and L.draws(o["menu"]["seed"], r["seed"]) == 21,
            f"RandSeed at ROLAND's menu is melee's {MELEE_SEED_ROLAND:#x} (21 draws past the first menu's)",
            f"{r['seed']:#x}")
    hp_roland = bytes.fromhex(r["records"][DYING_MEMBER])[HIT_POINTS]
    v.check(hp_roland == 1, "ROLAND OF DUST still has 1 hit point at ROLAND's menu (no strike in round 1)",
            hp_roland)
    p = melee_port(work, patched, "melee-dying-roland", r["typed"])
    dying_compare(v, "ROLAND OF DUST's menu (turn 7)", r, p, o["menu"]["seed"])
    prev = r["seed"]
    for rnd in sorted(o["rounds"]):
        s = o["rounds"][rnd]
        tag = f"round {rnd}'s first menu"
        hp = bytes.fromhex(s["records"][DYING_MEMBER])[HIT_POINTS]
        status = int.from_bytes(bytes.fromhex(s["records"][DYING_MEMBER])[STATUS_AT:STATUS_AT + 2], "little")
        if rnd <= DYING_ROUND_HIT:
            v.check(hp == 1, f"ROLAND OF DUST has 1 hit point at {tag} (the strike is round {DYING_ROUND_HIT}'s)",
                    hp)
        else:
            active = bytes.fromhex(s["records"][DYING_MEMBER])[ACTIVE_AT]
            v.check(hp == 0 and status == 5 and active == 0,
                    f"ROLAND OF DUST is dying at {tag}: 0 hit points, status word 5, +0x18b 0",
                    f"{hp} hit points, status word {status}, +0x18b {active}")
            v.check(s["bodies"]["count"] == 1 and s["bodies"]["entries"][0][:4] == (1, DYING_MEMBER, 27, 13),
                    f"the original's body table at {tag} holds ROLAND OF DUST's body alone (entry 1, at 27, 13)",
                    s["bodies"])
            size = {n: sz for n, _, _, sz in s["combatants"]}.get(1)
            cell = s["field"]["cells"][2 * (13 * 50 + 27):2 * (13 * 50 + 27) + 2]
            v.check(size == 0 and cell == "1f", f"ROLAND OF DUST (combatant 1) is off the map (size 0) and "
                    f"his body's cell (27, 13) is 0x1f at {tag}", f"size {size}, cell {cell}")
        p = melee_port(work, patched, f"melee-dying-{rnd}", s["typed"])
        dying_compare(v, tag, s, p, prev)
        prev = s["seed"]
    v.ok("the keys each stop was reached with (E typed so far)", " ".join(
        f"round {k}: {o['rounds'][k]['typed']}" for k in sorted(o["rounds"])))


def won_original(work, patched, verbose):
    """Fixture 2 (won): the original's run to the battle's end. Root 148f:0512 spins at the 'loaded'
    PRESS KEY (the setup's pass, 59c7:1eb9's event 8), each SHARKMAN +0x64 6 and +0x18b 0, DS:b7f3 0,
    var 0x2e 1 (so 135c's clear shows); 2093:0015 spins (var 0x2e is 0, the sides [6, 0], DS:c6ed =
    1d99:0fd1, c2d6 NULL), then 75cc:0723 and 0736, then the treasure menu's 75cc:0e93 (after the
    results' key): the state and the screens at the results' key wait and at the stop."""
    with cokbox.Box(workdir=work / "box-won", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        freeze_frame(box)  # the treasure menu's picture is compared exactly (as dqk_treasure's)
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
        # the menu's key wait, after the trap is given back (part 20a: the menu draws past 0e93)
        L.untrap(box, hold)
        t0 = time.monotonic()
        while not box.waiting():
            if time.monotonic() - t0 > 60:
                raise cokbox.BoxError("no key wait at the treasure menu after 75cc:0e93")
        out["end_screen"] = box.settled()
        out["end_screen"].save_png(str(work / "won-end.png"))
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
    port(work, patched, name="won-wait", typed="\\r\\r", combat="won",
         screen=work / "port-won-wait.bmp")
    end_v, end_last = port(work, patched, name="won", typed="\\r\\r\\r\\r", combat="won",
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

# The drop flow (step 4, part 21c: a defeated monster's items go to the pool, 75cc:02cb). The fight is
# THENOL WIZARD x2 (MONCHA 41: coins 16 3 0 each, a readied dagger and 20 darts; the copy's list is
# reversed): won's run to the results' key, the treasure menu, then Take's bar, its item list (the game's
# sword cursor over it: the port runs with --mouse and the step screens are compared whole), ROLAND's
# Return on the darts, Escape, and Exit's question. Measured (scope's probe): the menu's pool 32 6 0,
# share 2260, RandSeed 0x3039 at every wait; each member gains [2260, 753, 753, 1130, 1130, 1130].
DROP_MONSTER, DROP_COUNT, DROP_NAME = 41, 2, b"THENOL WIZARD"
DROP_STEPS = [("Take's bar", "t"), ("the item list", "i"), ("the darts taken by ROLAND", "\\r"),
              ("Take's bar after Escape", "\\e"), ("the menu after Exit", "e")]
DROP_TYPED = "\\r\\r\\r\\r"       # eclplay's keys to the treasure menu (won's, from the scope's probe)
DROP_WAIT_TYPED = "\\r\\r"        # to the results' key wait (won's)
DROP_POOL = (32, 6, 0)
DROP_SHARE = 2260
DROP_SEED = 0x3039
DROP_GAINED = [2260, 753, 753, 1130, 1130, 1130]
DROP_MAY = [0, 0, 0, 0, 1, 0]
DROP_POOL_ITEMS = ["0500000505000000000000000014000000",   # the copy's darts (head)
                   "040000040a000200000000000000000000",   # W2's dagger, unreadied
                   "040000040a000200000000000000000000",   # W1's dagger, unreadied
                   "0500000505000000000000000014000000"]   # W1's darts
DROP_ROLAND = 0                  # the record taking the darts (ROLAND OF DUST)
DROP_WEIGHTS = (910, 1010)       # ROLAND's weight (+0x56) before and after the darts
DROP_OUT = re.compile(r".*\(out of keys in block 1 at [0-9a-f]+\).*")


def drop_block():
    """Block 1 of the drop check: the entry PRESS KEY, the fight with THENOL WIZARD x2 (dqkecl.fight, as
    fight_block's with MONSTER 41), then the block's EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    dqkecl.fight(b, [(DROP_MONSTER, DROP_COUNT)], sprite=SPRITE, pause=True)
    b.op("EXIT")
    return b


def drop_list(box, head):
    """The items of the list whose far pointer lies at linear address HEAD: each node's 17 bytes at +0x28
    (hex), in order; a node is 0x3d bytes with the next far pointer at +0 (dqk_treasure's item_list)."""
    out, node = [], box.far(head)
    while node is not None:
        if len(out) >= 64:
            raise cokbox.BoxError("the item list does not end")
        out.append(box.read(node + 0x28, 17).hex())
        node = box.far(node)
    return out


def drop_state(box, what):
    """The key wait at WHAT (settled): the screen, the variables, the records, the pool's coins (DS:b1db,
    b1df, b1e3), RandSeed, the share (DS:ae4f), the pool's items (DS:b1e7, head first) and each record's."""
    t0 = time.monotonic()
    while not box.waiting():
        if time.monotonic() - t0 > 60:
            raise cokbox.BoxError(f"the game waits for no key at {what}")
    shot = box.settled()
    return dict(what=what, shot=shot, vars=block_vars(box), records=[raw for _, raw in L.records(box)],
                pool=tuple(int.from_bytes(box.ds_read(at, 4), "little") for at in (0xB1DB, 0xB1DF, 0xB1E3)),
                seed=box.ds_dword(L.DS_SEED), share=int.from_bytes(box.ds_read(0xAE4F, 4), "little"),
                pool_items=drop_list(box, box.ds * 16 + 0xB1E7),
                member_items=[drop_list(box, seg * 16 + off + 8) for (seg, off), _ in L.records(box)])


def drop_original(work, patched, verbose):
    """The drop flow's original: won_original's run to the results' key (148f:0512 spun at the 'loaded' PRESS
    KEY; each THENOL WIZARD +0x64 6 and +0x18b 0; DS:b7f3 0; then untrapped, no other trap), then the key
    wait's state, Return to the treasure menu's, and the state after each of DROP_STEPS."""
    with cokbox.Box(workdir=work / "box-drop", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        freeze_frame(box)
        box.set_seed(SEED)
        box.keys("\\r")
        box.settled(tolerance=64)                       # 'monsters'
        box.keys("\\r")
        box.settled(tolerance=64)                       # 'loaded'
        code = L.code(box, 0x148F, 0x0512)
        orig = box.read(code, 2)
        if orig != b"\x55\x8b":
            raise cokbox.BoxError(f"148f:0512 is not where it was expected: {orig.hex()}")
        box.write(code, b"\xeb\xfe")
        hA = L.Trap([(code, orig)], 0x148F, 0x0512)
        box.keys("\\r")                                 # SETUP MONSTERS, APPROACH, COMBAT
        wait_held_at(box, hA, "148f:0512")
        mons = 0
        for ptr, raw in L.records(box):
            if raw[0x68:0x78].split(b"\0")[0] == DROP_NAME:
                a = ptr[0] * 16 + ptr[1]
                box.write(a + 0x64, b"\x06\x00")
                box.write(a + 0x18B, b"\x00")
                mons += 1
        box.ds_write(0xB7F3, b"\x00")
        L.untrap(box, hA)
        results = drop_state(box, "the results' key wait")
        box.keys("\\r")
        states = [drop_state(box, "the treasure menu")]
        for what, keys in DROP_STEPS:
            box.keys(keys)
            states.append(drop_state(box, what))
    return results, states, mons


def drop_saved(work):
    """Saved game A's experience (+0x44) by member, matched by name (the drop's party is saved game A's)."""
    qsv = (dqk_lib.ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes()
    out = []
    for m in range(6):
        at = dqk_modify.record_at(qsv, m)
        out.append((qsv[at + 0x68:at + 0x78], int.from_bytes(qsv[at + 0x44:at + 0x48], "little")))
    return out


def drop(v, work, opts):
    """The drop flow (part 21c): the original's state at the results' key wait, the treasure menu and after
    each of DROP_STEPS; eclplay's run to each of them (--combat won, --mouse, --items, --records, --state,
    --screen) agrees with each: the screen (whole at the steps, the pointer's corner masked at the end, the
    blink's BAR at the results' wait), the variables, the records (pointers masked), the coins, RandSeed,
    the share and the pool's and each record's items. The original's own values are checked against the
    measured ones (DROP_*)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(drop_block()))
    results, states, mons = drop_original(work, patched, opts.verbose)
    menu = states[0]
    v.value("the records poked (THENOL WIZARD) are the two monsters", mons, DROP_COUNT)
    v.check(len(menu["records"]) == 6, "the original's party is six records at the treasure menu",
            f"{len(menu['records'])}")
    v.check(all(s["seed"] == DROP_SEED for s in [results] + states),
            "RandSeed is 0x3039 at the results' wait and at every step (the won path draws nothing)",
            [hex(s["seed"]) for s in [results] + states])
    v.check(menu["pool"] == DROP_POOL and menu["share"] == DROP_SHARE,
            "at the treasure menu the pool's coins are 32 6 0 and the share 2260 (the original)",
            f"pool {menu['pool']}, share {menu['share']}")
    v.check(menu["pool_items"] == DROP_POOL_ITEMS,
            "at the treasure menu the pool holds the copy's darts, two daggers (unreadied) and W1's darts, "
            "head first (the original)", menu["pool_items"])
    saved = drop_saved(work)
    gained, may = [], []
    for raw in menu["records"]:
        before = [e for name, e in saved if name == raw[0x68:0x78]]
        gained.append((int.from_bytes(raw[0x44:0x48], "little") - before[0] + 2 ** 31) % 2 ** 32 - 2 ** 31
                      if before else None)
        may.append(raw[0xcd])
    v.check(gained == DROP_GAINED and may == DROP_MAY,
            "each member gains the experience the flow's arithmetic gives, and may-train is as measured "
            "(the original)", f"gained {gained}, may {may}")
    after_darts = states[3]        # the menu after ROLAND's Return on the darts
    v.check(after_darts["records"][DROP_ROLAND][0x56:0x58] == DROP_WEIGHTS[1].to_bytes(2, "little")
            and menu["records"][DROP_ROLAND][0x56:0x58] == DROP_WEIGHTS[0].to_bytes(2, "little"),
            f"ROLAND's weight goes {DROP_WEIGHTS[0]} -> {DROP_WEIGHTS[1]} with the darts (the original)")
    v.check(len(after_darts["member_items"][DROP_ROLAND]) == 5
            and after_darts["member_items"][DROP_ROLAND][-1] == DROP_POOL_ITEMS[0]
            and after_darts["pool_items"] == DROP_POOL_ITEMS[1:],
            "ROLAND has five items, the darts last; the pool keeps the two daggers and W1's darts (the original)")

    # eclplay: the results' wait, then the menu and each step (each run types the keys so far)
    wait = work / "port-drop-wait"
    port(work, patched, name="drop-wait", typed=DROP_WAIT_TYPED, combat="won", mouse=True,
         screen=wait.with_suffix(".bmp"))
    command_screen(v, "at the results' key wait", results["shot"], wait.with_suffix(".bmp"), [dqk_views.BAR])
    names = ["the treasure menu"] + [what for what, _ in DROP_STEPS]
    for i, state in enumerate(states):
        what = names[i]
        base = work / f"port-drop-{i}"
        shot_dir = work / f"port-drop-{i}-shots"
        if shot_dir.exists():
            shutil.rmtree(shot_dir)
        typed = DROP_TYPED + "".join(keys for _, keys in DROP_STEPS[:i])
        variables, last = port(work, patched, name=f"drop-{i}", typed=typed, combat="won", mouse=True,
                               records=base.with_suffix(".records"), state=base.with_suffix(".state"),
                               items=base.with_suffix(".items"), shots=shot_dir)
        v.check(DROP_OUT.fullmatch(last or "") is not None,
                f"eclplay waits for a key at {what} (keys run out)", last)
        # the screen at the key wait where the keys ran out: its shot, taken before list_menu clears the
        # controls (the cursor's shape is the control's: the sword over the list); whole, the pointer's
        # corner masked at the end only (the original shows no cursor there)
        waits = sorted(shot_dir.glob("*.bmp"), key=lambda p: int(p.stem)) if shot_dir.is_dir() else []
        shot = waits[-1] if waits else base.with_suffix(".bmp")
        command_screen(v, f"at {what}", state["shot"], shot,
                       WON_SCREEN_MASK if i == len(states) - 1 else [])
        v.check(variables == state["vars"], f"the variables agree at {what} (the 1 KB)")
        data = base.with_suffix(".records").read_bytes() if base.with_suffix(".records").exists() else b""
        ported = [data[k:k + L.RECORD].hex() for k in range(0, max(len(data) - 3, 0), L.RECORD)]
        want = [raw.hex() for raw in state["records"]]
        differ = [k for k in range(min(len(ported), len(want))) if masked(ported[k]) != masked(want[k])]
        v.check(not differ and len(ported) == len(want), f"every record agrees at {what} (pointers masked)",
                f"{len(ported)} against {len(want)}; differ: {' '.join(map(str, differ))}")
        lines = base.with_suffix(".state").read_text().splitlines() if base.with_suffix(".state").exists() else []
        got = dict(line.split(" ", 1) for line in lines if " " in line)
        port_pool = tuple(int(x) for x in got.get("pool", "").split())
        v.check(got.get("seed") == f"{state['seed']:08x}" and port_pool == state["pool"]
                and int(got.get("share", -1)) == state["share"],
                f"RandSeed, the pool's coins and the share agree at {what} (eclplay's --state)",
                f"eclplay {got}; the original seed {state['seed']:#x}, pool {state['pool']}, "
                f"share {state['share']}")
        ported_items, ported_members = [], []
        if base.with_suffix(".items").exists():
            for line in base.with_suffix(".items").read_text().splitlines():
                fields = line.split()
                if fields[0] == "pool":
                    ported_items = fields[1:]
                else:
                    ported_members.append(fields[1:])
        v.check(ported_items == state["pool_items"] and ported_members == state["member_items"],
                f"the pool's items and each record's agree at {what} (eclplay's --items)",
                f"original {state['pool_items']}, eclplay {ported_items}")


# The walk flow: tests/test_dark_queen.py's test_first_fight in the original, the shipped block 4 (no
# test block): from the start menu, Load saved game A (plta), Begin Adventuring, block 4's texts to
# Caergoth, the steps along the ruins to the dragons (TO_FIGHT), THEY ATTACK!'s Return, the first
# turn's menu, End, the next turn's menu. The original's RandSeed is not eclplay's at the start (its
# title's cycles), so it is set at the start menu to eclplay's there (WALK_TITLE_SEED: the title's
# seven random(8) from 0, test_load_and_play), and the port is run as --load --play --seed of it, which
# test_load_and_play holds equal to the --start-menu route (its turn list is checked equal too). Begin's
# copy protection draws its entry and word (65cc:00cb random(20), 00d8 random(3): two draws, in the
# original and in the port's adventure), so RandSeed is eclplay's at block 4's first text (81bc), and
# from there the two agree draw for draw. Without those draws the first turn is BALTHIR NOMOON's.
WALK_TITLE_SEED = 0x3E6298EB
WALK_TO_FIGHT = "\\^" * 10 + "\\>\\^\\^\\^\\>\\^\\^\\<\\^\\^\\^\\>\\^\\^L\\r\\v\\^\\r\\^\\^\\^"
WALK_CITY = "\\r\\r\\rP\\r\\r\\r\\r\\rL\\r"     # block 4's texts, the palace and Leave, to Caergoth
WALK_TEXT_PC, WALK_ATTACK_PC = 0x81BC, 0x8C0E   # block 4's first text's wait, THEY ATTACK!'s
WALK_STOP = "(out of keys in block 4 at 8c20)"  # the turn's command menu (5045:08cf)
WALK_TURNS = ("turn: TIMASALIN (initiative 7)", "turn: 'CINDER' (initiative 7)")
WALK_SAVE = ("SAVGAMA.QSV", "VAULTA.DAT")       # test_load_and_play's copy


def walk_save(work):
    """Saved game A as test_load_and_play copies it (the QSV and the vault), for eclplay's --load."""
    folder = work / "walk-save"
    folder.mkdir(parents=True, exist_ok=True)
    for name in WALK_SAVE:
        shutil.copy(dqk_lib.ASSETS / "SAVE" / name, folder / name)
    return folder / "SAVGAMA.QSV"


def walk_port(work, name, typed, start_menu=False):
    """eclplay's run of the walk: --load --play --seed WALK_TITLE_SEED and TYPED (or, START_MENU,
    test_first_fight's --start-menu with \\k\\ePLTAB before TYPED). Returns the stdout's lines, the
    --state's RandSeed (None for the start menu's route, which writes no state) and the --turns list."""
    base = work / f"port-walk-{name}"
    args = [str(cokbox.ECLPLAY), "--game", "dark-queen", "--turns", str(base.with_suffix(".turns"))]
    if start_menu:
        args += ["--start-menu", "--keys", "\\k\\ePLTAB" + typed]
    else:
        args += ["--load", str(walk_save(work)), "--play", "--seed", str(WALK_TITLE_SEED),
                 "--state", str(base.with_suffix(".state")), "--keys", typed]
    for path in (base.with_suffix(".turns"), base.with_suffix(".state")):
        path.unlink(missing_ok=True)
    result = subprocess.run([*args, str(dqk_lib.ASSETS)], capture_output=True, text=True, cwd=ROOT,
                            timeout=300)
    base.with_suffix(".txt").write_text(result.stdout + result.stderr)
    seed = None
    if base.with_suffix(".state").exists():
        for line in base.with_suffix(".state").read_text().splitlines():
            if line.startswith("seed "):
                seed = int(line.split()[1], 16)
    turns = []
    if base.with_suffix(".turns").exists():
        turns = [tuple(int(x) for x in line.split())
                 for line in base.with_suffix(".turns").read_text().splitlines()]
    return result.stdout.splitlines(), seed, turns


def walk_hex(seed):
    return "none" if seed is None else f"{seed:#x}"


def walk_wait(box, pc, seconds=120):
    """Until block 4's ECL (DS:b7f9) is at PC with the game waiting for a key there. Returns the pc
    seen; after SECONDS, the last one seen (the caller's check reports it)."""
    end = time.monotonic() + seconds
    while True:
        here = dqk_lib.ecl_state(box)["pc"]
        if here == pc and box.waiting():
            return here
        if time.monotonic() > end:
            return here
        time.sleep(0.05)


def walk_original(work, verbose):
    """The original along the walk (the shipped ECL, the sound off as every fight's: L.files), the
    title's cycles (dqk_menus.freeze_cycles) and the memory cache's purges (L.freeze_purges) frozen,
    RandSeed WALK_TITLE_SEED at the start menu; Begin and the copy protection answered; at block 4's
    first text the seed read (still while it waits); the walk typed (a key as the game waits for it,
    Box.keys), THEY ATTACK!'s wait read, its Return, to_menu's traps at the first turn
    (menu_from_turn), End, and the next combatant's menu (5045:08cf, arm)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    with cokbox.Box(workdir=work / "box-walk", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(glb)) as box:
        dqk_ecl.wait_screen(box)
        box.keys("\\e")
        box.settled(tolerance=64)
        freeze = L.code(box, 0x261B, 0x05BE)  # dqk_menus' freeze_cycles
        if box.read(freeze, 1) != b"\x55":
            raise cokbox.BoxError("261b:05be is not where it was expected")
        box.write(freeze, b"\xcb")
        L.freeze_purges(box)
        box.set_seed(WALK_TITLE_SEED)
        dqk_lib.load_saved_game(box)
        r = dict(loaded_seed=box.ds_dword(L.DS_SEED))
        box.keys("b")
        box.settled(tolerance=64)
        entry, word, answer = dqk_lib.protection(box)
        r["protection"] = (entry, word, answer)
        box.keys(answer + "\\r")
        r["text_pc"] = walk_wait(box, WALK_TEXT_PC)
        r["text_seed"] = box.ds_dword(L.DS_SEED)
        box.settle(1.0)
        r["text_still"] = box.ds_dword(L.DS_SEED) == r["text_seed"]
        box.keys((WALK_CITY + WALK_TO_FIGHT).lower())
        r["attack_pc"] = walk_wait(box, WALK_ATTACK_PC)
        r["attack_seed"] = box.ds_dword(L.DS_SEED)
        box.keys("\\r")                 # THEY ATTACK!'s Return: COMBAT
        menu = menu_from_turn(box, purges(box))
        menu["menu_screen"] = box.screenshot(path=str(work / "walk-menu.png"))
        t = arm(box, menu["handle"], MENU)
        box.keys("e")
        wait_trap(box, t, "5045:08cf")
        stop = snapshot(box)
        stop["selected"] = selected(box)
        L.untrap(box, t)
        box.settled(quiet=1.0, tolerance=0)
        box.screenshot(path=str(work / "walk-menu2.png"))
        r.update(menu=menu, stop=stop)
        return r


def walk(v, work, opts):
    """The walk flow: test_first_fight's turn order in the original (above)."""
    _, text_seed, _ = walk_port(work, "text", "")
    _, attack_seed, _ = walk_port(work, "attack", WALK_CITY + WALK_TO_FIGHT)
    first_lines, first_seed, first_turns = walk_port(work, "menu", WALK_CITY + WALK_TO_FIGHT + "\\r")
    next_lines, next_seed, next_turns = walk_port(work, "next", WALK_CITY + WALK_TO_FIGHT + "\\rE")
    for name, typed, lines, turns, line in (
            ("menu-start", "\\r", first_lines, first_turns, WALK_TURNS[0]),
            ("next-start", "\\rE", next_lines, next_turns, WALK_TURNS[1])):
        start_lines, _, start_turns = walk_port(work, name, WALK_CITY + WALK_TO_FIGHT + typed,
                                                start_menu=True)
        moves = [x for x in lines if x.startswith("turn: ") and not x[6].isdigit()]
        v.check(lines[-1:] == [WALK_STOP] and moves[-1:] == [line] and start_lines[-1:] == [WALK_STOP]
                and bool(turns) and start_turns == turns,
                f"eclplay's walk ({typed}) stops at the turn's menu, {line[6:]}, its --start-menu "
                "route's turn list the --load --seed route's", f"{lines[-1:]} {moves[-1:]}")
    o = walk_original(work, opts.verbose)
    v.check(o["loaded_seed"] == WALK_TITLE_SEED, "the original's Load draws nothing (RandSeed at the "
            "start menu after plta)", f"{o['loaded_seed']:#x}")
    v.check(o["text_pc"] == WALK_TEXT_PC and o["text_still"] and o["text_seed"] == text_seed,
            f"at block 4's first text ({WALK_TEXT_PC:04x}) the original's RandSeed is eclplay's (the copy "
            "protection's entry and word, 65cc:00cb and 00d8, drawn in both), still while it waits",
            f"pc {o['text_pc']:04x}, the original {o['text_seed']:#x}, eclplay {walk_hex(text_seed)}, "
            f"protection {o['protection']}")
    v.check(o["attack_pc"] == WALK_ATTACK_PC and o["attack_seed"] == attack_seed,
            f"at THEY ATTACK!'s wait ({WALK_ATTACK_PC:04x}) RandSeed agrees (the texts' delays, "
            "the steps)", f"pc {o['attack_pc']:04x}, the original {o['attack_seed']:#x}, eclplay "
            f"{walk_hex(attack_seed)}")
    m, s = o["menu"], o["stop"]
    spins_at_placement(v, m)
    v.check(m["menu_traps"] >= 1 and m["menu_held"], "the original spins at the first turn's command "
            "menu (5045:08cf)", f"{m['menu_traps']} copies")
    v.check([tuple(t) for t in m["turns"]] == first_turns and m["seed"] == first_seed
            and m["state"]["turn"] == 1 and m["selected"] == m["state"]["first"],
            f"at the first turn's menu the turn list (5045:0447: each entry's record and initiative) and "
            f"RandSeed agree, the turn entry 1's ({WALK_TURNS[0][6:]})",
            f"the original {m['turns']} {m['seed']:#x}, eclplay {first_turns} {walk_hex(first_seed)}")
    v.check([tuple(t) for t in s["turns"]] == next_turns and s["seed"] == next_seed
            and s["state"]["turn"] == 2,
            f"after End, at the next turn's menu, the turn list and RandSeed agree, the turn entry 2's "
            f"({WALK_TURNS[1][6:]})",
            f"the original {s['turns']} {s['seed']:#x} turn {s['state']['turn']}, eclplay {next_turns} "
            f"{walk_hex(next_seed)}")


def alt_x_check(v, work, patched, opts):
    """Alt-X without the Helm (dqk 5045:0c5d, 52db:3feb: the password "Gem" is not given): "That
    doesn't work" on the bar, one draw, and the menu built again (the turn not ended)."""
    o = command_original(work, patched, "AltX", opts.verbose)
    p = command_port(work, patched, "AltX", "\\@X")
    m = o["menu"]
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay ends at the battle's command menu (out of keys) after Alt-X", p["last"])
    v.check("message: That doesn't work" in p["text"], "eclplay says \"That doesn't work\" (the "
            "bar message) after Alt-X", [line for line in p["text"] if line.startswith("message:")])
    menus = sum(1 for line in p["text"] if line.startswith("menu: Aim"))
    v.check(menus == 2, "eclplay builds the command menu again after the refusal (its built text "
            "logged twice)", f"{menus} times")
    draws = command_state(v, "AltX", o["stop"], p, m["seed"])
    v.check(draws == 1, "the original's RandSeed past the menu after Alt-X is one draw (the bar "
            "message's wait, 1df9:052a)", f"{draws} draws")
    command_screen(v, "with the command menu again after Alt-X", o["menu_again"]["screen"],
                   p["base"].with_suffix(".bmp"), MENU_POINTER)


BANDAGE_STATUS = 0x64                   # the member's status word (+0x64), 4 once bandaged


def bandage_check(v, work, patched, opts):
    """Bandage (dqk 5045:0adb: 51c2:0d63(1), then End): saved game A with member 0 dying (status 5,
    dqk_modify.modified({}, {0: {0x64: 5}})), so the first menu (TIMASALIN's) has Bandage. B is
    typed there; the original's next combatant's menu (08cf, trapped) and eclplay's run to the same
    menu, its first menu's text and placement, the state there (member 0 at status 4, one draw
    past the menu for the say's wait, the records, blocks and turns), the say logged, the screen."""
    import dqk_modify  # noqa: E402
    qsv = dqk_modify.modified({}, {0: {BANDAGE_STATUS: 5}})
    o = keys_original(work, patched, "B", opts.verbose, tag="bandage", data=qsv)
    spins_at_placement(v, o["menu"])
    placement(v, work, patched, "bandage", "the first turn's menu with member 0 dying", None,
              o["menu"], entry=saved_entry(work, qsv))
    text = (work / "port-bandage.txt").read_text().splitlines()
    v.check("menu: Aim Use Guard Quick@Delay Bandage View Speed End" in text,
            "the first menu shows Bandage (member 0 dying, not hostile)",
            [line for line in text if line.startswith("menu:") and "Aim" in line][:1])
    s = o["stop"]
    status = bytes.fromhex(s["records"][0])[BANDAGE_STATUS:BANDAGE_STATUS + 2]
    v.check(status == (4).to_bytes(2, "little"), "the original's member 0 is bandaged (status word "
            "4) after B", status.hex())
    p = command_port(work, patched, "bandage", "B", entry=saved_entry(work, qsv))
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay ends at the next combatant's menu (out of keys) after B", p["last"])
    v.check("print: is bandaged" in p["text"],
            "eclplay says \"is bandaged\" (the say, logged) after B",
            [line for line in p["text"] if line.startswith("print:")])
    draws = command_state(v, "bandage", s, p, o["menu"]["seed"])
    v.check(draws == 1, "the original's RandSeed past the menu after B is one draw (the say's "
            "wait, 1df9:052a)", f"{draws} draws")
    command_screen(v, "with the command menu at the next combatant after B", o["menu_screen2"],
                   p["base"].with_suffix(".bmp"), MENU_POINTER)


YELL_MENU = "Aim Use Guard Quick@Delay Yell View Speed End"
YELL_SEED = 0x64E44872                  # RandSeed at BALTHIR NOMOON's menu: two draws past WALKER's
YELL_DRAWS = 2                          # the two SHARKMEN's saves (intelligence 10, status 0, side 1)


def yell_original(work, patched, verbose):
    """The original at the first turn's menu (to_menu), E typed there (TIMASALIN's) and at the next
    menu (ROARK's), each as turns' E: 5045:08cf trapped, the key, the trap held, given back, settled.
    Then WALKER KEYS' menu (the trap held there: its record DS:ad67, RandSeed, the built text
    searched once given back), Y typed there (a kender's Yell, 5045:0ae9) and the next menu trapped
    (BALTHIR NOMOON's, 08cf): the state there (DS:b45b, the yelled record, as `yelled`), and its
    screen once given back."""
    with cokbox.Box(workdir=work / "box-yell", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        menu["menu_screen"] = box.screenshot(path=str(work / "yell-menu.png"))
        out = dict(menu=menu)
        for last in (False, True):
            t = arm(box, menu["handle"], MENU)
            box.keys("E")
            wait_trap(box, t, "5045:08cf")
            if last:
                out["walker"] = dict(selected=selected(box), seed=box.ds_dword(L.DS_SEED))
            L.untrap(box, t)
            box.settled(quiet=1.0, tolerance=0)
        # The built text is not at DS:af15 (a far pointer to an empty string there): search the
        # original's first megabyte for it (the Yell line only exists once WALKER's menu is built).
        out["walker"]["text_at"] = box.read(0, 0x100000).find(YELL_MENU.encode("latin-1"))
        t = arm(box, menu["handle"], MENU)
        box.keys("Y")
        wait_trap(box, t, "5045:08cf")
        out["stop"] = snapshot(box)
        out["stop"].update(tail_state(box), selected=selected(box),
                           yelled="%04x:%04x" % (box.ds_word(0xB45D), box.ds_word(0xB45B)))
        out["stop_screen"] = box.screenshot(path=str(work / "yell-stop.png"))
        L.untrap(box, t)
        box.settled(quiet=1.0, tolerance=0)
        out["menu_screen2"] = box.screenshot(path=str(work / "yell-menu2.png"))
        return out


def yell_check(v, work, patched, opts):
    """Yell (step 4, part 22b, dqk 5045:0ae9, 148f:0005): a kender's Y at WALKER KEYS' menu, after E at
    the two before it (TIMASALIN's and ROARK's). Both SHARKMEN's saves are made (two rolls past
    WALKER's menu, no 0x8c: the chains agree and none holds 0x8c), DS:b45b is WALKER's record, and
    the next menu (BALTHIR NOMOON's) is the stop: the records, blocks, turns, variables and the
    screen agree with eclplay's run of EEY."""
    o = yell_original(work, patched, opts.verbose)
    spins_at_placement(v, o["menu"])
    v.check(o["walker"]["seed"] == o["menu"]["seed"], "the two Ends before Yell (TIMASALIN's and ROARK's) "
            "draw nothing: RandSeed at WALKER KEYS' menu is the first menu's",
            f"{o['walker']['seed']:#x} against {o['menu']['seed']:#x}")
    s = o["stop"]
    base = work / "cmd-yell"
    p = command_port(work, patched, "yell", "EEY", effects=True)
    text = (work / "port-cmd-yell.txt").read_text().splitlines()
    menus = [line for line in text if line.startswith("menu:")]
    aims = [line for line in menus if line.startswith("menu: Aim")]
    v.check(len(aims) >= 3 and aims[2] == f"menu: {YELL_MENU}",
            "eclplay's third command menu (WALKER KEYS') has Yell: \"Aim Use Guard Quick@Delay Yell "
            "View Speed End\"", aims[:3])
    v.check(o["walker"]["text_at"] >= 0, "the original's built menu at WALKER KEYS' is \"Aim Use "
            "Guard Quick@Delay Yell View Speed End\" in its memory (a linear address)",
            f"found at {o['walker']['text_at']:#x}" if o["walker"]["text_at"] >= 0 else "not found")
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay ends at BALTHIR NOMOON's command menu (out of keys) after EEY", p["last"])
    draws = command_state(v, "yell", s, p, o["menu"]["seed"])
    v.check(draws == YELL_DRAWS and s["seed"] == YELL_SEED,
            f"the original's RandSeed past the menu after Y is {YELL_DRAWS} draws (the two saves "
            f"of SHARKMEN, both made), {YELL_SEED:#x}", f"{draws} draws, {s['seed']:#x}")
    chains_agree(v, s["chains"], base.with_suffix(".effects"), " (yell)")
    with_8c = [i for i, nodes in enumerate(s["chains"]) if any(n[0] == 0x8C for n in nodes)]
    v.check(not with_8c, "no record holds 0x8c (yelled at) after Y: no save failed", with_8c)
    v.check(s["yelled"] == o["walker"]["selected"], "DS:b45b (the yelled record) is WALKER KEYS' "
            "record (DS:ad67 at its menu)", f"{s['yelled']} against {o['walker']['selected']}")
    v.check(s["variables"] == p["vars"], "the variables agree at BALTHIR NOMOON's menu (the 1 KB)")
    command_screen(v, "after the menu is given back after Y", o["menu_screen2"],
                   base.with_suffix(".bmp"), MENU_POINTER)


VIEW_SHEET = "menu: Items Drop Exit"    # the sheet's bar for TIMASALIN (items, steel; no Trade)
SHEET_POINTER = (2, 2, 14, 15, "the mouse pointer on the sheet (the shield at the corner)")


def view_original(work, patched, verbose):
    """The original at the first turn's menu (to_menu), V typed there (TIMASALIN's View): the sheet
    once settled, its bar's keys run out there. Then the command menu's build (5045:0cd7) trapped,
    Escape typed on the sheet: the sheet's tail (5837:00e4, 177e:1ad1 with DS:ae27), the arena and
    the panel again, and the menu built again. The state there, DS:ae27, and the screen once the
    trap is given back."""
    with cokbox.Box(workdir=work / "box-view", game="dark-queen", verbose=verbose,
                    timeout=900, files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        menu["menu_screen"] = box.screenshot(path=str(work / "view-menu.png"))
        menu_redraw = box.ds_byte(0xAE27)
        box.keys("V")
        box.settled(quiet=1.0, tolerance=0)
        out = dict(menu=menu, menu_redraw=menu_redraw,
                   sheet_screen=box.screenshot(path=str(work / "view-sheet.png")))
        t = arm(box, menu["handle"], (0x5045, MENU_TRAP))
        box.keys("\\e")
        wait_trap(box, t, f"5045:{MENU_TRAP:04x}")
        out["stop"] = snapshot(box)
        out["stop"].update(tail_state(box), selected=selected(box))
        out["stop_redraw"] = box.ds_byte(0xAE27)
        out["stop_screen"] = box.screenshot(path=str(work / "view-stop.png"))
        L.untrap(box, t)
        box.settled(quiet=1.0, tolerance=0)
        out["menu_screen2"] = box.screenshot(path=str(work / "view-menu2.png"))
    return out


def view_check(v, work, patched, opts):
    """View in the battle (step 4, part 22b's leftovers; dqk 5045:0af8, 86b8:0b0c, the mode 5 tail
    86b8:0d8b-0d97): V at TIMASALIN's menu, the sheet's bar "Items Drop Exit" (no Trade in combat),
    its screen and eclplay's out of keys there; then Escape: no draw past the menu (the arena and
    the panel again draw nothing), DS:ae27 1 at the menu before V and after it (177e:1ad1), the
    command menu built again with its screen, and the records, blocks and turns agree with
    eclplay's run of "V" then Escape."""
    o = view_original(work, patched, opts.verbose)
    p = command_port(work, patched, "view", "V")
    text = (work / "port-cmd-view.txt").read_text().splitlines()
    v.check(VIEW_SHEET in text, "eclplay's sheet's bar after V is \"Items Drop Exit\"",
            [line for line in text if line.startswith("menu:")][-1:])
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay runs out of keys at the sheet's bar after V", p["last"])
    command_screen(v, "with the sheet after V", o["sheet_screen"], p["base"].with_suffix(".bmp"),
                   SHEET_POINTER)
    q = command_port(work, patched, "view-esc", "V\\e")
    text = (work / "port-cmd-view-esc.txt").read_text().splitlines()
    menus = sum(1 for line in text if line.startswith("menu: Aim"))
    v.check(menus == 2, "eclplay builds the command menu again after the sheet's Escape (its built "
            "text logged twice)", f"{menus} times")
    v.check(BATTLE_STOP.fullmatch(q["last"] or "") is not None,
            "eclplay ends at the command menu (out of keys) after V and Escape", q["last"])
    draws = command_state(v, "view", o["stop"], q, o["menu"]["seed"])
    v.check(draws == 0, "the original's RandSeed past the menu after V and Escape is 0 draws (the "
            "arena and the panel draw nothing)", f"{draws} draws")
    # DS:ae27 is already 1 at the menu, so the 1 after the Escape does not show 177e:1ad1's store
    # (test_view_combat does, from 0 to 1): both sides are recorded here.
    v.check(o["menu_redraw"] == 1 and o["stop_redraw"] == 1, "the original's DS:ae27 (game->redraw) "
            "is 1 at the command menu before V and after the sheet's Escape (177e:1ad1 leaves it 1)",
            (o["menu_redraw"], o["stop_redraw"]))
    command_screen(v, "with the command menu again after V and Escape", o["menu_screen2"],
                   q["base"].with_suffix(".bmp"), MENU_POINTER)


# The mouse areas of the command menu (step 4, part 22b; dqk 1bdb:1595, scope section 5). The box has
# the mouse on (sdl.mouse_emulation), and so has eclplay with --mouse.
# TIMASALIN's cell is at 3, 4 on the view (P, Q): one spot in each of the eight areas, then the cell and
# the gap above it (no area: the arrow). The original's whole screen with the pointer there, against
# eclplay's (the game draws its cursor, so nothing is masked: the cursor's shape tells the area).
MOUSE_CONF = {"sdl.mouse_emulation": "always"}
MOUSE_SPOTS = [((50, 90), "area 0 (0x86)"), ((110, 30), "area 1 (0x88)"),
               ((150, 90), "area 2 (0x82)"), ((110, 130), "area 3 (0x84)"),
               ((50, 40), "area 4 (0x87)"), ((150, 40), "area 5 (0x81)"),
               ((150, 150), "area 6 (0x83)"), ((50, 150), "area 7 (0x85)"),
               ((110, 90), "the cell (no area)"), ((110, 78), "the gap above the cell (no area)")]
MOUSE_CLICK = (50, 40)                   # area 4 (0x87): Move, which 5045:1099 begins
MOVE = (0x5045, 0x1099)
FRAME_ADVANCE = (0x1BDB, 0x1168)          # freeze_frame, as dqk_treasure's (which imports this module)
FRAME_ADVANCE_BYTES = bytes.fromhex("fe060eb2")  # incb DS:b20e


class MouseBox(cokbox.Box):
    """A box whose pointer moves to pixel x, y of the 320x200 screen (no button), as dqk_view_mouse's."""

    def point(self, x, y):
        self._xdo("windowfocus", "--sync", self.window)
        self._xdo("mousemove", "--window", self.window, 2 * x, 2 * y)
        self.settle(0.4)


def freeze_frame(box):
    """The small picture's frame advance frozen in the box only (1bdb:1168's incb DS:b20e: four nops),
    so the menu's screens are exact (dqk_treasure's freeze_frame). Call after the block's entry."""
    at = L.code(box, *FRAME_ADVANCE)
    if box.read(at, 4) != FRAME_ADVANCE_BYTES:
        raise cokbox.BoxError(f"1bdb:1168 is not incb DS:b20e: {box.read(at, 4).hex()}")
    box.write(at, b"\x90" * 4)


def mouse_original(work, patched, verbose):
    """The original at the first turn's menu (to_menu) with the mouse on: the screen with the pointer at
    each of MOUSE_SPOTS (`spots`, in order), Space at the menu (`space`: 1595 entered again), then the
    click on area 4 with Move's routine (5045:1099) trapped where the turn's copies start (arm):
    `reached`, whether its spin holds. The click reaches Move only if the menu's areas are there."""
    out = dict(spots=[])
    with MouseBox(workdir=work / "box-mouse", game="dark-queen", verbose=verbose, timeout=900,
                  files=L.files(patched), extra_conf=MOUSE_CONF) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        for (x, y), _ in MOUSE_SPOTS:
            box.point(x, y)
            out["spots"].append(box.settled(tolerance=64))
        # Space at the menu: 1595 is entered again (the menu takes Space and goes on). The menu's
        # controls are kept, so the click below is on the areas built at to_menu.
        handle = L.trap(box, 0x1BDB, 0x1595, until=lambda: True, seconds=60)
        box.keys(" ")
        box.settle(3.0)
        out["space"] = L.held(box, handle, seconds=1.5)[0]
        L.untrap(box, handle)
        box.settle(1.0)
        box.point(*MOUSE_CLICK)
        t = arm(box, menu["handle"], MOVE)
        box.click(*MOUSE_CLICK)
        try:
            wait_trap(box, t, "5045:1099")
            out["reached"] = True
        except cokbox.BoxError:
            out["reached"] = False
        if out["reached"]:
            # The spin given back: Move steps her and draws its menu, which waits for a key.
            L.untrap(box, t)
            box.settled(quiet=1.0, tolerance=0)
            out["after"] = box.screenshot(path=str(work / "mouse-click.png"))
    return out


def mouse_port(work, patched, name, typed):
    """eclplay's run with --mouse of the block from saved game A (dqk_fight.port's), TYPED after the
    entry's keys: its last shot (the menu waiting, the pointer where the keys left it), its last output
    line and its output."""
    shots = work / f"mouse-{name}"
    if shots.exists():
        shutil.rmtree(shots)
    extra = ["--load", str(saved_entry(work)), "--play", "--mouse", "--shots", str(shots)]
    _, last, _ = dqk_ecl.port(work, f"mouse-{name}", patched, typed=typed, extra=extra, block=None)
    shot = cokbox.RGBScreen.load(sorted(shots.glob("*.bmp"))[-1]) if shots.exists() else None
    return shot, last, (work / f"port-mouse-{name}.txt").read_text()


def mouse_check(v, work, patched, opts):
    """The mouse areas (dqk_fight's `mouse` flow): the mouse on at the menu in both; each spot's screen
    agrees with eclplay's run to the spot ("\\r\\r" then the pointer's move and an Escape, which
    leaves the menu waiting, its last shot taken after the move); the click on area 4 reaches Move
    (5045:1099) in the original, and eclplay's Move menu after it (its last shot, where the keys run
    out) is the original's screen, with the code 0x87 logged."""
    o = mouse_original(work, patched, opts.verbose)
    v.check(o["space"] is True, "Space at the command menu enters 1595 again (the menu takes it)",
            f"{o['space']}")
    for i, ((x, y), label) in enumerate(MOUSE_SPOTS):
        shot, _, _ = mouse_port(work, patched, f"spot-{i}", f"\\r\\r\\m{x},{y},0.\\e")
        if shot is None:
            v.check(False, f"eclplay writes its screen with the pointer at {x},{y} ({label})")
            continue
        v.screen(f"the command menu with the pointer at {x},{y} ({label}), the whole screen",
                 o["spots"][i], shot, work=work)
    shot, last, text = mouse_port(work, patched, "click", f"\\r\\r\\m{MOUSE_CLICK[0]},"
                                  f"{MOUSE_CLICK[1]},1.")
    v.check(o["reached"], "the original reaches 5045:1099 (Move) after the click on area 4 (the spin "
            "holds)", f"{o['reached']}")
    v.check(BATTLE_STOP.fullmatch(last or "") is not None, "eclplay stops at Move's menu (the keys run "
            "out there) after the click on area 4", last)
    v.check("code: 0x87" in text.splitlines(), "eclplay logs the code 0x87 of the click on area 4",
            [line for line in text.splitlines() if line.startswith("code:")])
    if "after" in o:
        v.screen("the Move menu after the click on area 4 (TIMASALIN at 27, 12, Move Left = 10), the "
                 "whole screen (the pointer at 50, 40)", o["after"], shot, work=work)
    else:
        v.check(False, "the original's Move menu after the click on area 4 is drawn")


def mouse(v, work, opts):
    """The command menu's mouse areas (step 4, part 22b): the pointer over each area of TIMASALIN's menu,
    and a click on one (mouse_check)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    patched = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    mouse_check(v, work, patched, opts)


# Move (step 4, part 23: dqk 5045:1099, queen_commands.c's move): the direction keys at the command menu
# and the Move menu's keys, against the original's stops. The command menu is built again (5045:0cd7) after
# every Move that ends (Undo, Done, a melee's turn end): the first build is Undo's, so the keys are typed in
# two stages (MOVE_FIRST, then MOVE_SECOND, the trap re-armed at the second build).
MOVE_BUILT = (0x5045, 0x0CD7)           # the command menu's build: the menu's keys are read after it
MOVE_TAIL = (0x5045, 0x066A)            # the turn's tail after a melee by a step (the turn ends)
MOVE_FIRST = "\\>U"                   # Right (one square, 29,13), then Undo (back to 28,13)
MOVE_SECOND = "\\>\\>D"             # Right (29,13), Right into a wall ("can't go there"), Done
MOVE_DRAWS = 1                          # RandSeed draws past the menu after MOVE_SECOND: the message's wait
MOVE_SEED = 0x23057A5D                  # the RandSeed then (from the menu's 0x1756582C)
MOVE_FACING, MOVE_MOVEMENT = 2, 0x16    # TIMASALIN after Done: facing 2 (right), movement 0x16 (two squares)
MOVE_WILD_KEYS = "\\#51\\#51\\#51\\v"  # PgDn three times (diagonals to 32,17), then Down into SHARKMAN 2
MOVE_WILD_DRAWS = 4                     # RandSeed draws past the menu up to the turn's tail (066a)
MOVE_WILD_SEED = 0x1AD549DA             # RandSeed at the turn's tail
MOVE_WILD_BLOCK = "000001000001000000010408000000000000000000000001"  # BALTHIR's combat block at it
MOVE_WILD_NEXT = "57fb:0816"            # the next combatant's record at its command menu
MOVE_ALLY_KEYS = "\\>\\v"           # Right, then Down into ROARK (below): "ATTACK ALLY? YES NO"
BLOCK_FACING, BLOCK_MOVEMENT, BLOCK_TARGET = 0x0A, 0x07, 0x0B  # a combat block's bytes
SHIELD_POINTER = (2, 2, 16, 18, "the mouse cursor (the shield at the corner, off the menu's controls)")


def selected_block(stop):
    """The combat block (bytes) of the selected record (DS:ad67) at a stop: its index in the state's
    record list (the blocks' order), as turn_record reads the turn list."""
    at = [r["at"] for r in stop["state"]["records"]]
    return bytes.fromhex(stop["blocks"][at.index(stop["selected"])])


def move_original(work, patched, verbose):
    """The original at the first turn's menu (to_menu) with the command menu's build (5045:0cd7) trapped:
    MOVE_FIRST typed there, the trap reached at the build after Undo, given back; then the trap re-armed and
    MOVE_SECOND typed, reached at the build after Done. `stop` is the state there (the selected record too),
    `again` the menu drawn and waiting (its screen and RandSeed)."""
    with cokbox.Box(workdir=work / "box-move", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t = arm(box, menu["handle"], MOVE_BUILT)
        box.keys(MOVE_FIRST)
        wait_trap(box, t, "5045:0cd7")
        L.untrap(box, t)
        box.settled(quiet=1.0, tolerance=0)
        t = arm(box, menu["handle"], MOVE_BUILT)
        box.keys(MOVE_SECOND)
        wait_trap(box, t, "5045:0cd7")
        stop = snapshot(box)
        stop.update(tail_state(box), selected=selected(box))
        L.untrap(box, t)
        box.settled(quiet=1.0, tolerance=0)
        again = dict(screen=box.screenshot(path=str(work / "move-menu.png")), seed=box.ds_dword(L.DS_SEED))
    return dict(menu=menu, stop=stop, again=again)


def move_wild_original(work, patched, verbose):
    """The original's wild melee by a step (the open ground, wild_block(1, 0)): to_menu, MOVE_WILD_KEYS typed,
    the turn's tail (5045:066a) reached (`tail`), then the next combatant's command menu (5045:08cf, `next`)."""
    with cokbox.Box(workdir=work / "box-move-wild", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t = arm(box, menu["handle"], MOVE_TAIL)
        box.keys(MOVE_WILD_KEYS)
        wait_trap(box, t, "5045:066a")
        tail = snapshot(box)
        tail.update(tail_state(box), selected=selected(box))
        # The next menu's trap is armed while the tail's spin holds: the copies given back later may
        # have been moved by the overlay manager (after the release the turn ran on past 08cf's copies).
        n = arm(box, menu["handle"], MENU)
        L.untrap(box, t)
        wait_trap(box, n, "5045:08cf")
        nxt = snapshot(box)
        nxt.update(selected=selected(box))
        L.untrap(box, n)
    return dict(menu=menu, tail=tail, next=nxt)


def move_ally_original(work, patched, verbose):
    """The original's screen after MOVE_ALLY_KEYS (Right, then Down into the ally below): the question waits."""
    with cokbox.Box(workdir=work / "box-move-ally", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        to_menu(box)
        box.keys(MOVE_ALLY_KEYS)
        box.settled(quiet=1.0, tolerance=0)
        return box.screenshot(path=str(work / "move-ally.png"))


def move(v, work, opts):
    """Move (step 4, part 23): the Move menu's Done after a step into the empty square and into a wall (the
    dungeon: the menu built again, facing, movement, RandSeed, the records and blocks), a melee by a step on
    the open ground (the turn's tail and the next combatant's menu), and the ally question's screen (the
    dungeon)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    dungeon = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    o = move_original(work, dungeon, opts.verbose)
    p = command_port(work, dungeon, "move", MOVE_FIRST + MOVE_SECOND)
    m, s = o["menu"], o["stop"]
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay ends at the battle's command menu (out of keys) after Right, Undo, Right, Right, Done",
            p["last"])
    draws = command_state(v, "Move's Done", s, p, m["seed"])
    v.check(draws == MOVE_DRAWS and s["seed"] == MOVE_SEED, f"the original's RandSeed past the menu after "
            f"Move's Done is {MOVE_DRAWS} draw (the wait of 'can't go there') at {MOVE_SEED:#x}",
            f"{draws} draws, {s['seed']:#x}")
    block = selected_block(s)
    v.check(block[BLOCK_FACING] == MOVE_FACING and block[BLOCK_MOVEMENT] == MOVE_MOVEMENT,
            f"the selected record (TIMASALIN) after Done has facing {MOVE_FACING} and movement "
            f"{MOVE_MOVEMENT:#x} (her combat block at the menu)",
            f"facing {block[BLOCK_FACING]}, movement {block[BLOCK_MOVEMENT]:#x}")
    field = p["base"].with_suffix(".field")
    fight = parse_field(field)[5] if field.exists() else None
    v.check(fight == s["combatants"], "every combatant's position agrees at the menu after Move's Done "
            "(TIMASALIN back on 29, 13)", f"eclplay {fight}, the original {s['combatants']}")
    command_screen(v, "the command menu after Move's Done", o["again"]["screen"],
                   p["base"].with_suffix(".bmp"), MENU_POINTER)
    w = move_wild_original(work, wild, opts.verbose)
    pw = command_port(work, wild, "move-wild", MOVE_WILD_KEYS)
    draws = L.draws(w["menu"]["seed"], w["tail"]["seed"])
    v.check(draws == MOVE_WILD_DRAWS and w["tail"]["seed"] == MOVE_WILD_SEED,
            f"the original's RandSeed at the turn's tail after the melee by a step is {MOVE_WILD_DRAWS} draws "
            f"past the menu, at {MOVE_WILD_SEED:#x}", f"{draws} draws, {w['tail']['seed']:#x}")
    v.check(selected_block(w["tail"]).hex() == MOVE_WILD_BLOCK,
            "BALTHIR's combat block at the turn's tail (initiative 0, movement 0, target 8, facing 4)",
            selected_block(w["tail"]).hex())
    v.check(w["next"]["selected"] == MOVE_WILD_NEXT,
            f"the next combatant's record at its menu is {MOVE_WILD_NEXT}", w["next"]["selected"])
    v.check(BATTLE_STOP.fullmatch(pw["last"] or "") is not None,
            "eclplay ends at the next combatant's command menu (out of keys) after the melee by a step",
            pw["last"])
    command_state(v, "the melee by a step (the next combatant's menu)", w["next"], pw, w["menu"]["seed"])
    shots = work / "move-ally-shots"
    if shots.exists():
        shutil.rmtree(shots)
    port(work, dungeon, name="move-ally", typed="\\r\\r" + MOVE_ALLY_KEYS, shots=shots)
    shot = cokbox.RGBScreen.load(sorted(shots.glob("*.bmp"))[-1]) if shots.exists() else None
    ally = move_ally_original(work, dungeon, opts.verbose)
    v.screen("the ATTACK ALLY? question after Right and Down into ROARK, the whole screen", ally, shot,
             masks=[SHIELD_POINTER], work=work)


# Aim (step 4, part 24a: dqk 52db:37dd, queen_aim.c): the Aim menu's screens and the command menu after its Exit,
# against the original's. The dungeon's self-only list: "A" (the menu waiting, "AIM: RANGE = 0" on row 23), then
# "E" (the command menu built again at 5045:0cd7, no draws). The open ground's list (wild_block(1, 0)): "A", "N"
# (SHARKMAN 2 shown, the view on her, "AIM: RANGE = 5"), "P" and "N" again (back to her: the RandSeed holds), then
# "E" (the menu built again, as the dungeon's).
AIM_BUILT = (0x5045, 0x0CD7)            # the command menu's build: after Aim's Exit (08cf's c8c)


def aim_screen_original(work, patched, verbose, tag, typed):
    """The original at the first turn's menu (to_menu) with TYPED typed there, and the screen the keys leave
    (the Aim menu waiting), the RandSeed then (`seed`); the menu's state at its start is `menu`."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        box.keys(typed)
        box.settled(quiet=1.0, tolerance=0)
        return dict(menu=menu, seed=box.ds_dword(L.DS_SEED),
                    screen=box.screenshot(path=str(work / f"{tag}.png")))


def aim_exit_original(work, patched, verbose, tag, typed):
    """The original's Aim with TYPED, whose last key is Exit: the command menu's build (5045:0cd7) reached after it
    (`stop`, the state there, with DS:ad67 as `selected`), the trap given back, and the menu drawn again (`again`)."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t = arm(box, menu["handle"], AIM_BUILT)
        box.keys(typed)
        wait_trap(box, t, "5045:0cd7")
        stop = snapshot(box)
        stop.update(selected=selected(box), view=view_bytes(box))
        L.untrap(box, t)
        box.settled(quiet=1.0, tolerance=0)
        again = dict(screen=box.screenshot(path=str(work / f"{tag}-menu.png")), seed=box.ds_dword(L.DS_SEED))
    return dict(menu=menu, stop=stop, again=again)


def view_bytes(box):
    """The view's record (far DS:bedb): the origin (+2 word x, +4 word y), the cursor (+6) and its size (+7)."""
    return box.read(box.far(box.ds * 16 + 0xBEDB), 8)


# Aim's Target when attacking (part 24b: 52db:2d46, the strike). On the open ground TIMASALIN's Move steps are
# the same as Move's (PgDn three times, then Done), and Aim's Next then Target strike SHARKMAN 2 two squares off
# (the probe's targetwild): the melee by a step's numbers (MOVE_WILD_*) are the strike's. Its Aim menu after
# A and N shows TARGET. The carry-over, an "attack ally?" answered Yes (queen_attack_ally's Yes path), cannot be
# typed from Aim (52db:36c6's list excludes her side: 52db:1030 is called with allies 0), so it is Move's:
# the dungeon's Right, Down into ROARK, then Y (52db:2aef's Yes, the attack, var 0x36 set).
AIM_TARGET_SCREEN = "\\#51\\#51\\#51DAN"        # Move's three steps, Done, then Aim and Next (TARGET listed)
AIM_TARGET_KEYS = AIM_TARGET_SCREEN + "T"            # then Target: the strike
ALLY_YES_KEYS = MOVE_ALLY_KEYS + "Y"                 # Move into ROARK, the question answered Yes
FLED_VAR = 0x36                                      # var 0x36 (FLED): 52db:2aef's Yes sets it


def strike_original(work, patched, verbose, tag, keys):
    """The original's turn for KEYS typed at the first turn's menu: the turn's tail (5045:066a, `tail`), then the
    next combatant's command menu (5045:08cf, `next`, its screen too: `screen`). The strike's and the ally Yes's
    melee are the same routine's steps (52db:1853, 18d6)."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t = arm(box, menu["handle"], MOVE_TAIL)
        box.keys(keys)
        wait_trap(box, t, "5045:066a")
        tail = snapshot(box)
        tail.update(tail_state(box), selected=selected(box))
        n = arm(box, menu["handle"], MENU)
        L.untrap(box, t)
        wait_trap(box, n, "5045:08cf")
        nxt = snapshot(box)
        nxt.update(selected=selected(box))
        L.untrap(box, n)
        box.settled(quiet=1.0, tolerance=0)
        nxt["screen"] = box.screenshot(path=str(work / f"{tag}-menu.png"))
    return dict(menu=menu, tail=tail, next=nxt)


def aim_target_check(v, work, opts):
    """Aim's Target on the open ground (part 24b): the Aim menu after Move's steps, Done, A and N against eclplay's
    shot (TARGET listed); the strike's turn tail (RandSeed draws, BALTHIR's block) and the next combatant's menu
    (records, blocks, turn list, RandSeed, screen) against eclplay's run to that menu."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    shots = work / "aim-target-shots"
    if shots.exists():
        shutil.rmtree(shots)
    port(work, wild, name="aim-target", typed="\\r\\r" + AIM_TARGET_SCREEN, shots=shots)
    shot = cokbox.RGBScreen.load(sorted(shots.glob("*.bmp"))[-1]) if shots.exists() else None
    a = aim_screen_original(work, wild, opts.verbose, "aim-target-a", AIM_TARGET_SCREEN)
    v.screen("the Aim menu after Move's three steps, Done, A and N on the open ground (TARGET listed: SHARKMAN 2 "
             "two squares off)", a["screen"], shot, masks=[SHIELD_POINTER], work=work)
    s = strike_original(work, wild, opts.verbose, "aim-target", AIM_TARGET_KEYS)
    p = command_port(work, wild, "aim-target", AIM_TARGET_KEYS)
    draws = L.draws(a["menu"]["seed"], s["tail"]["seed"])
    v.check(draws == MOVE_WILD_DRAWS and s["tail"]["seed"] == MOVE_WILD_SEED,
            f"the original's RandSeed at the turn's tail after Aim's Target is {MOVE_WILD_DRAWS} draws past the "
            f"menu, at {MOVE_WILD_SEED:#x}", f"{draws} draws, {s['tail']['seed']:#x}")
    v.check(selected_block(s["tail"]).hex() == MOVE_WILD_BLOCK,
            "BALTHIR's combat block at the turn's tail after Aim's Target is the melee's (initiative 0, movement 0, "
            "target 8, facing 4)", selected_block(s["tail"]).hex())
    v.check(s["next"]["selected"] == MOVE_WILD_NEXT,
            f"the next combatant's record at its menu is {MOVE_WILD_NEXT}", s["next"]["selected"])
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay ends at the next combatant's command menu (out of keys) after Aim's Target", p["last"])
    command_state(v, "Aim's Target (the next combatant's menu)", s["next"], p, a["menu"]["seed"])
    command_screen(v, "the command menu after Aim's Target on the open ground", s["next"]["screen"],
                   p["base"].with_suffix(".bmp"), MENU_POINTER)


def ally_yes_check(v, work, opts):
    """Move's "attack ally?" answered Yes (queen_attack_ally's Yes path, the carry-over, see above) on the dungeon.
    The original's var 0x36 (FLED) is 1 at the turn's tail. eclplay's run (ROARK's effect 0x2f, 78e4:09b2, ported in
    part 34a) runs on to the next combatant's command menu (out of keys), where the state (RandSeed,
    records, blocks, turn list) is compared with the original's next."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    dungeon = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    y = strike_original(work, dungeon, opts.verbose, "ally-yes", ALLY_YES_KEYS)
    p = command_port(work, dungeon, "ally-yes", ALLY_YES_KEYS)
    v.check(y["tail"]["variables"][FLED_VAR - 1] == 1, "the original's var 0x36 (FLED) after Yes is 1 at the turn's "
            "tail", str(y["tail"]["variables"][FLED_VAR - 1]))
    v.check(p["vars"] and p["vars"][FLED_VAR - 1] == 1, "eclplay's var 0x36 (FLED) after Yes is 1 at its stop",
            str(p["vars"][FLED_VAR - 1] if p["vars"] else None))
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay ends at the next combatant's command menu (out of keys) after Yes", p["last"])
    command_state(v, "Move's Yes (the next combatant's menu)", y["next"], p, y["menu"]["seed"])


# Aim's Manual (step 4, part 25: dqk 52db:2e9c, queen_aim.c's manual; keys only). The cursor starts on Aim's
# target, herself on the dungeon (CENTER EXIT: she is no target) and SHARKMAN 2 after N, N, P on the open ground.
# A direction key steps it (the next pass), Exit or Escape leaves to Aim, Center centres on the cell at margin 0,
# and Target on a foe in reach is the strike, as Aim's. The dungeon: A, M, then Right (the cell east: CENTER
# EXIT); then E (Aim), C (her view: origin 0x19, 0x0a) and E (the command menu, no draws). The open ground: A, N,
# N, P, M (SHARKMAN 2 at range 5 against 1: CENTER EXIT), then Escape (Aim again). Target from Manual: SHARKMAN 2
# is one step south of BALTHIR, so after Move's three steps, Done, A, M and one Down (the cursor on her, TARGET
# listed) the T strikes her: the probe's measure (4 draws, RandSeed 1ad549da, the turn's tail), the same as
# Aim's Target (aim_target_check).
MANUAL_DUNGEON = "AM"
MANUAL_RIGHT = "AM\\>"
MANUAL_EXIT = "AM\\>ECE"
MANUAL_WILD = "ANNPM"
MANUAL_WILD_ESC = "ANNPM\\e"
MANUAL_TARGET_SCREEN = "\\#51\\#51\\#51DAM\\v"    # Move's three steps, Done, Aim, Manual, one Down
MANUAL_TARGET_KEYS = MANUAL_TARGET_SCREEN + "T"          # then Target: the strike
MANUAL_VIEW = bytes([0x19, 0, 0x0A, 0])                  # the view after Aim's Center on her: origin 0x19, 0x0a


def port_shot(work, patched, name, typed):
    """eclplay's run of TYPED at the first turn's menu: its last screenshot (None where it wrote none)."""
    shots = work / f"{name}-shots"
    if shots.exists():
        shutil.rmtree(shots)
    port(work, patched, name=name, typed="\\r\\r" + typed, shots=shots)
    return cokbox.RGBScreen.load(sorted(shots.glob("*.bmp"))[-1]) if shots.exists() and any(shots.glob("*.bmp")) else None


def manual_check(v, work, opts):
    """Aim's Manual (part 25): the Manual menu on the dungeon and one cell right, Exit, Center and the command menu
    after Exit (the state, the RandSeed and the screen against eclplay's stop; the view agrees with the original's);
    on the open ground the menu on SHARKMAN 2 (CENTER EXIT) and Escape back to Aim, against eclplay's shots; and
    Target from Manual on SHARKMAN 2, the turn's tail and the next combatant's menu against eclplay's run."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    dungeon = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    shot = port_shot(work, dungeon, "manual", MANUAL_DUNGEON)
    a = aim_screen_original(work, dungeon, opts.verbose, "manual-a", MANUAL_DUNGEON)
    v.screen("the Manual menu after A and M on the dungeon (TIMASALIN's own cell: CENTER EXIT)", a["screen"], shot,
             masks=[SHIELD_POINTER], work=work)
    shot = port_shot(work, dungeon, "manual-right", MANUAL_RIGHT)
    r = aim_screen_original(work, dungeon, opts.verbose, "manual-right", MANUAL_RIGHT)
    v.screen("the Manual menu after Right on the dungeon (the cell east of her: CENTER EXIT)", r["screen"], shot,
             masks=[SHIELD_POINTER], work=work)
    e = aim_exit_original(work, dungeon, opts.verbose, "manual-exit", MANUAL_EXIT)
    p = command_port(work, dungeon, "manual-exit", MANUAL_EXIT)
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None, "eclplay ends at the command menu (out of keys) "
            "after A, M, Right, E, C and E on the dungeon", p["last"])
    draws = command_state(v, "Manual's Exit on the dungeon", e["stop"], p, e["menu"]["seed"])
    v.check(draws == 0, "the original's RandSeed past the command menu after Manual's Exit and Aim's Center is "
            "unchanged (no draws)", f"{draws} draws")
    origin = bytes(e["stop"]["view"][2:6])  # the origin's two words, +2 and +4
    v.check(origin == MANUAL_VIEW and e["stop"]["view"][6] == 0,
            "the original's view after Aim's Center on her is origin 0x19, 0x0a with the cursor off", origin.hex())
    seed, view, header, _, _, _ = parse_field(p["base"].with_suffix(".field"))
    v.check(list(view) == [0x19, 0x0A] and header[0] == 0, "eclplay's view after the same keys is origin 0x19, "
            "0x0a with the cursor off", f"{view} {header}")
    command_screen(v, "the command menu after Manual's Exit on the dungeon", e["again"]["screen"],
                   p["base"].with_suffix(".bmp"), MENU_POINTER)
    shot = port_shot(work, wild, "manual-wild", MANUAL_WILD)
    w = aim_screen_original(work, wild, opts.verbose, "manual-wild", MANUAL_WILD)
    v.screen("the Manual menu after A, N, N, P and M on the open ground (SHARKMAN 2 under the cursor: CENTER EXIT, "
             "range 5 against 1)", w["screen"], shot, masks=[SHIELD_POINTER], work=work)
    shot = port_shot(work, wild, "manual-wild-esc", MANUAL_WILD_ESC)
    x = aim_screen_original(work, wild, opts.verbose, "manual-wild-esc", MANUAL_WILD_ESC)
    v.screen("Aim's menu after Escape from Manual on the open ground (AIM: RANGE = 5, the view on her)", x["screen"],
             shot, masks=[SHIELD_POINTER], work=work)
    shot = port_shot(work, wild, "manual-target-a", MANUAL_TARGET_SCREEN)
    ta = aim_screen_original(work, wild, opts.verbose, "manual-target-a", MANUAL_TARGET_SCREEN)
    v.screen("the Manual menu after Move's three steps, Done, A, M and Down on the open ground (SHARKMAN 2 under "
             "the cursor: TARGET listed)", ta["screen"], shot, masks=[SHIELD_POINTER], work=work)
    s = strike_original(work, wild, opts.verbose, "manual-target", MANUAL_TARGET_KEYS)
    tp = command_port(work, wild, "manual-target", MANUAL_TARGET_KEYS)
    draws = L.draws(ta["menu"]["seed"], s["tail"]["seed"])
    v.check(draws == MOVE_WILD_DRAWS and s["tail"]["seed"] == MOVE_WILD_SEED,
            f"the original's RandSeed at the turn's tail after Manual's Target is {MOVE_WILD_DRAWS} draws past the "
            f"menu, at {MOVE_WILD_SEED:#x}", f"{draws} draws, {s['tail']['seed']:#x}")
    v.check(selected_block(s["tail"]).hex() == MOVE_WILD_BLOCK,
            "BALTHIR's combat block at the turn's tail after Manual's Target is the melee's (initiative 0, movement 0, "
            "target 8, facing 4)", selected_block(s["tail"]).hex())
    v.check(s["next"]["selected"] == MOVE_WILD_NEXT,
            f"the next combatant's record at its menu is {MOVE_WILD_NEXT}", s["next"]["selected"])
    v.check(BATTLE_STOP.fullmatch(tp["last"] or "") is not None,
            "eclplay ends at the next combatant's command menu (out of keys) after Manual's Target", tp["last"])
    command_state(v, "Manual's Target (the next combatant's menu)", s["next"], tp, ta["menu"]["seed"])
    command_screen(v, "the command menu after Manual's Target on the open ground", s["next"]["screen"],
                   tp["base"].with_suffix(".bmp"), MENU_POINTER)


def aim(v, work, opts):
    """Aim (step 4, part 24a; Target's strike, part 24b: aim_target_check and ally_yes_check): the Aim menu on the dungeon's self-only list and on the open ground's two
    targets, each screen against eclplay's last shot, and the command menu after Exit against eclplay's
    stop (the state, the RandSeed, the screen). The dungeon's Exit draws nothing; the open ground's N and P
    hold the RandSeed."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    dungeon = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block()))
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    shots = work / "aim-shots"
    if shots.exists():
        shutil.rmtree(shots)
    port(work, dungeon, name="aim", typed="\\r\\r" + "A", shots=shots)
    shot = cokbox.RGBScreen.load(sorted(shots.glob("*.bmp"))[-1]) if shots.exists() else None
    a = aim_screen_original(work, dungeon, opts.verbose, "aim-a", "A")
    v.screen("the Aim menu after A on the dungeon (self only: AIM: RANGE = 0, NEXT PREV MANUAL CENTER EXIT)",
             a["screen"], shot, masks=[SHIELD_POINTER], work=work)
    e = aim_exit_original(work, dungeon, opts.verbose, "aim-exit", "AE")
    p = command_port(work, dungeon, "aim-exit", "AE")
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None, "eclplay ends at the command menu (out of keys) "
            "after A and Exit", p["last"])
    draws = command_state(v, "Aim's Exit on the dungeon", e["stop"], p, e["menu"]["seed"])
    v.check(draws == 0, "the original's RandSeed past the command menu after Aim's Exit is unchanged (no draws)",
            f"{draws} draws")
    command_screen(v, "the command menu after Aim's Exit on the dungeon", e["again"]["screen"],
                   p["base"].with_suffix(".bmp"), MENU_POINTER)
    wshots = work / "aim-wild-shots"
    if wshots.exists():
        shutil.rmtree(wshots)
    port(work, wild, name="aim-wild", typed="\\r\\r" + "AN", shots=wshots)
    wshot = cokbox.RGBScreen.load(sorted(wshots.glob("*.bmp"))[-1]) if wshots.exists() else None
    w = aim_screen_original(work, wild, opts.verbose, "aim-wild", "AN")
    v.screen("the Aim menu after A and N on the open ground (SHARKMAN 2 shown: AIM: RANGE = 5, the view on her)",
             w["screen"], wshot, masks=[SHIELD_POINTER], work=work)
    h = aim_screen_original(work, wild, opts.verbose, "aim-wild-hold", "ANP" + "N")
    v.check(h["seed"] == w["menu"]["seed"], "the original's RandSeed after N, P and N (back on SHARKMAN 2) holds "
            "at the menu's", f"{h['seed']:#x} against {w['menu']['seed']:#x}")
    x = aim_exit_original(work, wild, opts.verbose, "aim-wild-exit", "ANPNE")
    px = command_port(work, wild, "aim-wild-exit", "ANPNE")
    v.check(BATTLE_STOP.fullmatch(px["last"] or "") is not None, "eclplay ends at the command menu (out of keys) "
            "after A, N, P, N and Exit on the open ground", px["last"])
    draws = command_state(v, "Aim's Exit on the open ground", x["stop"], px, x["menu"]["seed"])
    v.check(draws == 0, "the original's RandSeed past the command menu after Aim's Exit on the open ground is "
            "unchanged", f"{draws} draws")
    command_screen(v, "the command menu after Aim's Exit on the open ground", x["again"]["screen"],
                   px["base"].with_suffix(".bmp"), MENU_POINTER)
    aim_target_check(v, work, opts)
    ally_yes_check(v, work, opts)
    manual_check(v, work, opts)


# Turn Undead (step 4, part 26: dqk 52db:1117 and the undead search 52db:135e, queen_turn_undead and queen_undead).
# The turn flow's undead is ZOMBIE (monster 37, kind 1: its +0x5c word is 1), COUNT of them. T's runs stop at the next
# combatant's command menu (5045:08cf), reached once the turn is over (End, 0b29, done); a trap at the turn's tail
# (5045:066a) is not used: after Turn the state it sees is the middle of 1117 (initiative 9, bd2e 0, 3 draws), where
# wait_trap's 0.6 s false hold fires inside the turn's timed pauses (1df9:052a polls no keys), not at the turn's end.
# Alt-Q (the computer's turn of BALTHIR, the first combatant) runs on to the battle's end: its state is the second
# computer dispatch (5045:0891) of the next combatant, after BALTHIR's computer turn, against the port's second turn
# mark (turn_eclplay).
ZOMBIE = 37
TURN_WILD_KEYS = "T"                  # ZOMBIES: T at BALTHIR's menu (the wild ground, wild_block(1, 0))
TURN_DUNGEON_KEYS = "T"               # ZOMBIE-DUNGEON: T at TIMASALIN's menu (the dungeon, fight_block(monster=37))
TURN_ALTQ_KEYS = "\\@Q"               # ZOMBIES: Alt-Q at BALTHIR's menu (the computer turns her)
TURN_WILD_DRAWS, TURN_WILD_SEED = 13, 0x949F681B        # RandSeed draws past the first menu up to the next menu, and at it
TURN_DUNGEON_DRAWS, TURN_DUNGEON_SEED = 3, 0xC7D7027B   # "Nothing Happens...": 3 draws
TURN_ALTQ_DRAWS, TURN_ALTQ_SEED = 16, 0x3A060FCE        # BALTHIR's computer turn: 16 draws past the menu, at its end
TURN_ALTQ_FIRST_DRAWS = 1                               # Alt-Q's own wait, before BALTHIR's computer turn
TURN_TAIL_AT = "            if (!cok_combat_turn_tail(game)) return false;\n"  # round.c, after each turn's tail
TURN_MARK = '            cok_adventure_log(game, "dqk", "turn");\n'


def turn_mark(name, text):
    """src/round.c: the "dqk" mark logged after each turn's tail (the state the turn's end leaves). round_log writes the
    same files as the rounds' (NN.* in COK_DQK_ROUNDS), one per turn: 01 is the first turn's, 02 the second's."""
    if text.count(TURN_TAIL_AT) != 1:
        raise cokbox.BoxError("src/round.c's turn tail changed: update dqk_fight.turn_mark")
    return text.replace(TURN_TAIL_AT, TURN_TAIL_AT + TURN_MARK)


def turn_eclplay():
    """eclplay built with the turn marks (checklib.build; cached by the sources): round_log's writer for the
    "dqk" kind, the round mark (round_mark) left out."""
    return checklib.build("eclplay", transforms=[("eclplay.c", round_log), ("round.c", turn_mark)])


def turn_port(work, patched, name, typed, entry=None):
    """eclplay's run of TYPED with the turn marks (command_port's, with turn_eclplay; ENTRY the saved game as
    command_port's): the port dict, with `tails` the directory of the turns' marks (NN.*: the state after each turn's
    tail, 01 the first)."""
    tails = work / f"tails-{name}"
    if tails.exists():
        shutil.rmtree(tails)
    tails.mkdir(parents=True)
    os.environ["COK_DQK_ROUNDS"] = str(tails)
    try:
        p = command_port(work, patched, name, typed, entry=entry, exe=turn_eclplay())
    finally:
        del os.environ["COK_DQK_ROUNDS"]
    p["tails"] = tails
    return p


def turn_original(work, patched, keys, tag, verbose):
    """The original at the first turn's menu (to_menu), KEYS typed there, and the next combatant's command menu
    (5045:08cf) reached once the turn is over: `next` (the state there, with DS:bd2e as `attack_over`), and `again` (the
    menu drawn and waiting: its screen and RandSeed)."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t = arm(box, menu["handle"], MENU)
        box.keys(keys)
        wait_spin(box, t, "5045:08cf")
        nxt = snapshot(box)
        nxt.update(tail_state(box), selected=selected(box), attack_over=box.ds_byte(0xBD2E))
        L.untrap(box, t)
        box.settled(quiet=1.0, tolerance=0)
        again = dict(screen=box.screenshot(path=str(work / f"{tag}-menu.png")), seed=box.ds_dword(L.DS_SEED))
    return dict(menu=menu, next=nxt, again=again)


def altq_original(work, patched, verbose):
    """The original at the first turn's menu with Alt-Q typed there: the computer's dispatch (5045:0891) of BALTHIR, after
    Alt-Q's own wait (`first`: 1 draw past the menu), then BALTHIR's computer turn's tail (5045:066a, the trap moved
    from the dispatch's: `second`, the state there, with DS:bd2e as `attack_over`). The battle goes on past her turn, so
    the state is read at the tail, not at the next dispatch (the battle is over in round 1 here)."""
    with cokbox.Box(workdir=work / "box-turn-altq", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched)) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        t = arm(box, menu["handle"], COMPUTER)
        box.keys(TURN_ALTQ_KEYS)
        wait_spin(box, t, "5045:0891")
        first = snapshot(box)
        first.update(tail_state(box), selected=selected(box))
        t2 = arm(box, menu["handle"], MOVE_TAIL)
        L.untrap(box, t)
        wait_spin(box, t2, "5045:066a")
        second = snapshot(box)
        second.update(tail_state(box), selected=selected(box), attack_over=box.ds_byte(0xBD2E))
        L.untrap(box, t2)
    return dict(menu=menu, first=first, second=second)


def turn_run(v, work, patched, name, keys, what, draws, seed, verbose, destroyed):
    """One of T's runs: the original's next combatant's menu (turn_original) against eclplay's stop there (BATTLE_STOP:
    the records, blocks, turn list, RandSeed and screen), the RandSeed draws past the first menu (`draws`, at `seed`),
    the ZOMBIE records at status 8 and inactive (`destroyed`), DS:bd2e 1 and the turn index (DS:bd2d) 2 there."""
    o = turn_original(work, patched, keys, f"turn-{name}", verbose)
    p = command_port(work, patched, f"turn-{name}", "\\r\\r" + keys)
    m, s = o["menu"], o["next"]
    got = L.draws(m["seed"], s["seed"])
    v.check(got == draws and s["seed"] == seed,
            f"the original's RandSeed past the menu after {what} is {draws} draws (the next menu), at {seed:#x}",
            f"{got} draws, {s['seed']:#x}")
    hit = [i for i, r in enumerate(s["records"]) if turn_status(r) == 8]
    v.check(len(hit) == destroyed and all(turn_zombie(s["records"][i]) and
                                          bytes.fromhex(s["records"][i])[0x18B] == 0 for i in hit),
            f"after {what}: {destroyed} ZOMBIE record(s) at status 8 and inactive in the original",
            f"{len(hit)} at status 8")
    v.check(s["attack_over"] == 1, f"the original's attack-over byte (DS:bd2e) after {what} is 1",
            f"{s['attack_over']}")
    v.check(s["state"]["turn"] == 2, f"the original's turn index (DS:bd2d) after {what} is 2 (the next combatant)",
            f"{s['state']['turn']}")
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            f"eclplay ends at the next combatant's command menu (out of keys) after {what}", p["last"])
    command_state(v, f"the next combatant's menu after {what}", s, p, m["seed"])
    command_screen(v, f"the command menu after {what}", o["again"]["screen"],
                   p["base"].with_suffix(".bmp"), MENU_POINTER)
    return o, p


def turn_zombie(record):
    """Whether a record (its hex) is a ZOMBIE (the turn flow's undead)."""
    return cstr(bytes.fromhex(record)[0x68:0x78]).startswith("ZOMBIE")


def turn_status(record):
    raw = bytes.fromhex(record)
    return raw[0x64] | raw[0x65] << 8


def turn_altq(v, work, patched, opts):
    """Alt-Q at BALTHIR's menu on the open ground (the computer turns her): the original's first dispatch (1 draw, Alt-Q's
    own wait) and its second (BALTHIR's computer turn's end: 16 draws past the menu, ZOMBIE 2 destroyed, DS:bd2e 1), against
    eclplay's second turn mark (the state after her computer turn: records, blocks, turn list, RandSeed)."""
    o = altq_original(work, patched, opts.verbose)
    p = turn_port(work, patched, "turn-altq", "\\r\\r" + TURN_ALTQ_KEYS)
    m, f, s = o["menu"], o["first"], o["second"]
    got = L.draws(m["seed"], f["seed"])
    v.check(got == TURN_ALTQ_FIRST_DRAWS, "the original's RandSeed past the menu at BALTHIR's first dispatch after Alt-Q "
            f"is {TURN_ALTQ_FIRST_DRAWS} draw (the wait)", f"{got} draws")
    got = L.draws(m["seed"], s["seed"])
    v.check(got == TURN_ALTQ_DRAWS and s["seed"] == TURN_ALTQ_SEED,
            f"the original's RandSeed past the menu after BALTHIR's computer turn (Alt-Q) is {TURN_ALTQ_DRAWS} draws, "
            f"at {TURN_ALTQ_SEED:#x}", f"{got} draws, {s['seed']:#x}")
    hit = [i for i, r in enumerate(s["records"]) if turn_status(r) == 8]
    v.check(len(hit) == 1 and turn_zombie(s["records"][hit[0]]) and
            bytes.fromhex(s["records"][hit[0]])[0x18B] == 0,
            "after BALTHIR's computer turn (Alt-Q): one ZOMBIE record at status 8 and inactive in the original",
            f"{len(hit)} at status 8")
    v.check(s["attack_over"] == 1, "the original's attack-over byte (DS:bd2e) after BALTHIR's computer turn is 1",
            f"{s['attack_over']}")
    mark = p["tails"] / "02"
    if not mark.with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's computer turn's mark after Alt-Q", p["last"])
    else:
        command_state(v, "BALTHIR's computer turn after Alt-Q", s, dict(base=mark, last=p["last"]), m["seed"])


def turn(v, work, opts):
    """Turn Undead (step 4, part 26): T at the first turn's menu on the open ground (ZOMBIES: BALTHIR, a cleric of level 10:
    ZOMBIE 2 is destroyed, ZOMBIE 1 is out of sight) and in the dungeon (ZOMBIE-DUNGEON: TIMASALIN, nothing in sight: "Nothing
    Happens..."), each against eclplay's next combatant's menu; and Alt-Q at BALTHIR's menu (the computer turns her), against
    eclplay's mark after her computer turn."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0], monster=ZOMBIE)))
    dungeon = hlib.replace(glb, JUMP, dqkecl.assemble(fight_block(monster=ZOMBIE)))
    turn_run(v, work, wild, "wild", TURN_WILD_KEYS, "T at BALTHIR's menu (ZOMBIES)", TURN_WILD_DRAWS,
             TURN_WILD_SEED, opts.verbose, destroyed=1)
    turn_run(v, work, dungeon, "dungeon", TURN_DUNGEON_KEYS, "T at TIMASALIN's menu (ZOMBIE-DUNGEON, nothing in sight)",
             TURN_DUNGEON_DRAWS, TURN_DUNGEON_SEED, opts.verbose, destroyed=0)
    turn_altq(v, work, wild, opts)


# Cast (step 4, part 28c: dqk 52db:2543, 849a:145b, 52db:1f78, 52db:1e78, 849a:21b5; queen_cast.c). The fixture CAST
# (scope.md 2): the open ground's two SHARKMEN (wild_block(1, 0)) and saved game A with BALTHIR (member 3) memorising
# CLW (+0xce 0x03) then MM (+0xcf 0x0f), so the list reads CLW, then MM. CAST-HURT is the same with his hit points at
# 0x32 of 106. Each key is typed alone and waited for (the list, the Aim and a question are key waits); a cast's end is
# the next combatant's menu (5045:08cf, WALKER KEYS) or the turn's tail (5045:066a), read once the flight's RandSeed
# settles (a flight polls no key, so the trap reads held before its draws are done: scope.md 1).
CAST_BALTHIR = 3
CAST_MEMO = 0xCE                        # +0xce, then +0xcf: BALTHIR's memorised bytes
CAST_CLW, CAST_MM = 0x03, 0x0F
CAST_HURT = 0x32                        # CAST-HURT: BALTHIR's hit points (+0x198), of 106
CAST_SHARKMAN_2 = 7                     # the record of SHARKMAN 2 (the second SHARKMAN; 4aba:113b): hit points 0x4e before
CAST_TARGETS = 0xC6F4                   # DS:c6f4, the spell's target count
CAST_FACING_MM = 3                      # BALTHIR's facing after MM at SHARKMAN 2
CAST_FORGOTTEN = 0x80                   # the memorised byte's mark (cok_combat_forget)
# The original's draws and RandSeed, counted from BALTHIR's first menu (m0, the seed there) as the scope counts them.
CAST_LIST_DRAWS = 0                     # C (the picker) then Escape: no draw; the menu again
CAST_MM_AIM = (1, 0xEB831397)           # C, Down, C: MM's Aim, one draw
CAST_MM_TAIL = (19, 0xECF5F155)         # N (no draw), T: the tail (18 draws past the Aim)
CAST_MM_HP = (0x4E, 0x4B)               # SHARKMAN 2 before and after the missile (1d4+1)
CAST_CLW_NEXT = (2, 0xD9491FD4)         # C, C: Begins Casting; BALTHIR's turn ends at WALKER's menu
CAST_CLW_AIM = (3, 0x8A1EDDA5)          # E (WALKER's End): BALTHIR's pending cast at its Aim
CAST_CLW_TAIL = (6, 0x35E90FF0)         # T: the tail (3 draws past the Aim)
CAST_CLW_HP = (0x32, 0x3D)              # CLW's heal, +11
CAST_ABORT_TAIL = (4, 0x1AD549DA)       # C, C, E, E, Y: Spell Aborted, the tail (1 draw past the question)


def cast_save(hurt=False):
    """Saved game A with BALTHIR memorising CLW and MM (CAST); CAST-HURT also has his hit points at 0x32."""
    changes = {CAST_BALTHIR: {CAST_MEMO: CAST_CLW, CAST_MEMO + 1: CAST_MM}}
    if hurt:
        changes[CAST_BALTHIR][HIT_POINTS] = CAST_HURT
    return dqk_modify.modified({}, changes)


def wait_seed(box, t, where, seconds=TURN_HOLD):
    """wait_spin for a trap in a flight or a flash: the spin held over SECONDS, and RandSeed unchanged over 1.5 s more
    (the flight polls no key, so the polls alone read the trap held before its draws are done)."""
    end = time.monotonic() + TAIL_SECONDS
    while time.monotonic() < end:
        box.settle(0.5)
        if L.held(box, t, seconds=seconds)[0]:
            seed = box.ds_dword(L.DS_SEED)
            box.settle(1.5)
            if box.ds_dword(L.DS_SEED) == seed and L.held(box, t, seconds=seconds)[0]:
                return
    raise cokbox.BoxError(f"the original's trap at {where} is not reached after {TAIL_SECONDS} s")


def cast_key(box, work, tag, typed):
    """TYPED at a key wait (the list, the Aim, a question): the box settled, then the state, DS:ad67 and the screen."""
    box.keys(typed)
    box.settled(quiet=1.0, tolerance=0)
    s = snapshot(box)
    s.update(selected=selected(box), screen=box.screenshot(path=str(work / f"{tag}.png")))
    return s


def cast_traps(box, handle, traps):
    """The traps in order, each a (routine, where, typed, shot): TYPED typed once the trap is armed (from the menu's
    handle), the state read once it is held and RandSeed settled (wait_seed: `targets` is DS:c6f4, the screen SHOT, a
    path, if given). The next trap is armed before the one before it is given back, as the other flows do, so the game
    cannot run past the next stop. Returns the states; the box is settled after the last."""
    states = []
    held = None
    for routine, where, typed, shot in traps:
        t = arm(box, handle, routine)
        if held is not None:
            L.untrap(box, held)
        if typed is not None:
            box.keys(typed)
        wait_seed(box, t, where)
        s = snapshot(box)
        s.update(tail_state(box), selected=selected(box), targets=box.ds_byte(CAST_TARGETS),
                 cell=(box.ds_byte(0xBD2C), box.ds_byte(0xBD49)))  # the spell's cell (cast_auto_original's)
        if shot is not None:
            s["screen"] = box.screenshot(path=str(shot))
        states.append(s)
        held = t
    L.untrap(box, held)
    box.settled(quiet=1.0, tolerance=0)
    return states


def cast_run_list_mm(box, menu, work, tag):
    """C (the list), Escape (the menu again), C (the list), Down to Magic Missile, C (its Aim), N, then T: the tail;
    then the next combatant's menu."""
    out = dict(list=cast_key(box, work, f"{tag}-list", "C"), esc=cast_key(box, work, f"{tag}-esc", "\\e"))
    cast_key(box, work, f"{tag}-pick", "C")
    cast_key(box, work, f"{tag}-down", "\\v")
    out["aim"] = cast_key(box, work, f"{tag}-aim", "C")
    out["n"] = cast_key(box, work, f"{tag}-n", "N")
    out["tail"], out["next"] = cast_traps(box, menu["handle"], [
        (MOVE_TAIL, "5045:066a", "T", None), (MENU, "5045:08cf", None, None)])
    out["next"]["screen"] = box.screenshot(path=str(work / f"{tag}-next.png"))  # the menu drawn, waiting
    return out


def cast_run_clw(box, menu, work, tag):
    """C, C (Begins Casting: BALTHIR's turn ends at WALKER's menu), E (WALKER's End: BALTHIR's pending cast at its
    Aim), then T: the tail."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "CC", None)])[0])
    out["aim"] = cast_key(box, work, f"{tag}-aim", "E")
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "T", None)])[0]
    return out


def cast_run_abort(box, menu, work, tag):
    """C, C (WALKER's menu), E (BALTHIR's pending Aim), E (Exit: Abort Spell?), then Y: Spell Aborted and the tail."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "CC", None)])[0])
    out["aim"] = cast_key(box, work, f"{tag}-aim", "E")
    out["question"] = cast_key(box, work, f"{tag}-question", "E")
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "Y", work / f"{tag}-tail.png")])[0]
    return out


def cast_original(work, patched, data, tag, verbose, run):
    """The original's run of the fixture DATA (the saved game) from BALTHIR's first menu (to_menu), then RUN's steps."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose, timeout=900,
                    files={**L.files(patched), "SAVE/SAVGAMA.QSV": data}) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        out = run(box, menu, work, tag)
    out["menu"] = menu
    return out


def rec_byte(record, offset):
    """One byte of a record (its hex, as snapshot and ported_records give it)."""
    return bytes.fromhex(record)[offset]


def ported_blocks(p):
    """eclplay's --blocks file at the stop as hex, one combat block a line (the list order)."""
    path = p["base"].with_suffix(".blocks")
    data = path.read_bytes() if path.exists() else b""
    return [data[i:i + COMBAT_BLOCK].hex() for i in range(0, len(data), COMBAT_BLOCK)]


def cast_screen(v, work, what, o_screen, p, mask):
    """The original's screen against eclplay's --screen at the stop of P (its base's .bmp), MASK masked."""
    path = p["base"].with_suffix(".bmp")
    if not path.exists():
        v.check(False, f"eclplay writes its screen {what}")
        return
    v.screen(what, o_screen, cokbox.RGBScreen.load(path, SLOT_SCREEN), masks=[mask], work=work)


def cast_mark(p, number):
    """The state after turn number NUMBER's tail in eclplay (turn_port's marks), as command_state reads a stop."""
    return dict(base=p["tails"] / number, last=p["last"])


def cast(v, work, opts):
    """Cast (step 4, part 28c): BALTHIR's Cast on the open ground (CAST) against eclplay's stops. C then Escape: the
    menu again, no draws, the list's screen after C compared. C, Down, C: MM's Aim (one draw, row 23 "Spell:Magic
    Missile", the screen compared); N; T: the tail (19 draws past the Aim, SHARKMAN 2 hit by 3, BALTHIR's +0xcf
    forgotten and his facing 3), then the next menu (WALKER KEYS). CAST-HURT: C, C (Begins Casting: the turn ends at
    WALKER's menu), E (WALKER's End: BALTHIR's pending cast at its Aim, the screen compared), T: the tail (6 draws, the
    hit points healed, the byte forgotten). Abort: C, C, E, E (Exit: Abort Spell?), Y: the tail (its draws, the byte
    forgotten). eclplay's runs give the same stops, and its tails are marks (turn_port: 01 the first turn's tail)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save, hurt = cast_save(), cast_save(hurt=True)
    mm = cast_original(work, wild, save, "cast-mm", opts.verbose, cast_run_list_mm)
    m0 = mm["menu"]["seed"]
    entry = saved_entry(work / "cast-save", save)
    p_list = command_port(work, wild, "cast-list", "C", entry=entry)
    cast_screen(v, work, "the list after C on the open ground (BALTHIR NOMOON's Spells in Memory: CLW, then MM)",
                mm["list"]["screen"], p_list, SHIELD_POINTER)
    p_esc = command_port(work, wild, "cast-esc", "C\\e", entry=entry)
    draws = command_state(v, "C and Escape (the menu again)", mm["esc"], p_esc, m0)
    v.check(draws == CAST_LIST_DRAWS and mm["esc"]["seed"] == m0,
            "the original's RandSeed after C and Escape is unchanged (no draws: the menu again)", f"{draws} draws")
    cast_screen(v, work, "the command menu after C and Escape", mm["esc"]["screen"], p_esc, MENU_POINTER)
    p_aim = command_port(work, wild, "cast-aim", "C\\eC\\vC", entry=entry)
    draws, seed = L.draws(m0, mm["aim"]["seed"]), mm["aim"]["seed"]
    v.check((draws, seed) == CAST_MM_AIM, f"the original's RandSeed at MM's Aim (C, Down, C) is {CAST_MM_AIM[0]} draw "
            f"past the menu, at {CAST_MM_AIM[1]:#x}", f"{draws} draws, {seed:#x}")
    cast_screen(v, work, "the Aim after C, Down and C (Magic Missile: row 23 Spell:Magic Missile, Target listed)",
                mm["aim"]["screen"], p_aim, SHIELD_POINTER)
    command_state(v, "MM's Aim (C, Down, C)", mm["aim"], p_aim, m0)
    draws = L.draws(seed, mm["n"]["seed"])
    v.check(draws == 0, "the original's RandSeed after N (SHARKMAN 2 under the cursor) is unchanged",
            f"{draws} draws")
    tail = mm["tail"]
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == CAST_MM_TAIL, f"the original's tail after T (MM) is {CAST_MM_TAIL[0]} draws past "
            f"the menu (1 at the Aim, 18 for T), at {CAST_MM_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    hp = (rec_byte(mm["aim"]["records"][CAST_SHARKMAN_2], HIT_POINTS),
          rec_byte(tail["records"][CAST_SHARKMAN_2], HIT_POINTS))
    v.check(hp == CAST_MM_HP, "SHARKMAN 2's hit points (+0x198) go from 0x4e to 0x4b at the tail (MM's 1d4+1)",
            f"{hp[0]:#x} -> {hp[1]:#x}")
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    v.check(rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO + 1) == CAST_MM | CAST_FORGOTTEN and facing == CAST_FACING_MM
            and tail["targets"] == 1, "at the tail BALTHIR's MM is forgotten (+0xcf 0x8f), he faces 3, and DS:c6f4 is 1",
            f"+0xcf {rec_byte(tail['records'][CAST_BALTHIR], CAST_MEMO + 1):#x}, facing {facing}, "
            f"DS:c6f4 {tail['targets']}")
    p_mm = turn_port(work, wild, "cast-mm", "C\\eC\\vCNT", entry=entry)
    mark = cast_mark(p_mm, "01")
    command_state(v, "MM's tail (the turn's tail, 01)", tail, mark, seed)
    v.check(rec_byte(ported_records(mark)[CAST_SHARKMAN_2], HIT_POINTS) == CAST_MM_HP[1] and
            rec_byte(ported_records(mark)[CAST_BALTHIR], CAST_MEMO + 1) == CAST_MM | CAST_FORGOTTEN,
            "eclplay's tail after MM: SHARKMAN 2 at 0x4b, BALTHIR's MM forgotten (+0xcf 0x8f)",
            f"SHARKMAN 2 {rec_byte(ported_records(mark)[CAST_SHARKMAN_2], HIT_POINTS):#x}, MM "
            f"{rec_byte(ported_records(mark)[CAST_BALTHIR], CAST_MEMO + 1):#x}")
    nxt = mm["next"]
    v.check(nxt["selected"] == MOVE_WILD_NEXT, f"the next combatant at its menu after MM is WALKER KEYS "
            f"({MOVE_WILD_NEXT})", nxt["selected"])
    v.check(BATTLE_STOP.fullmatch(p_mm["last"] or "") is not None,
            "eclplay ends at the next combatant's command menu (out of keys) after MM", p_mm["last"])
    command_state(v, "the next combatant's menu after MM", nxt, p_mm, m0)
    command_screen(v, "the command menu after MM", nxt["screen"], p_mm["base"].with_suffix(".bmp"), MENU_POINTER)
    cast_hurt(v, work, opts, wild, hurt)


def cast_hurt(v, work, opts, wild, hurt):
    """CAST-HURT (BALTHIR at 0x32): C, C (Begins Casting: his turn ends at WALKER's menu), E (WALKER's End: his pending
    cast at its Aim), then T (the tail: CLW heals him, the byte forgotten). Abort: C, C, E, E (Exit: Abort Spell?), then
    Y (Spell Aborted, the tail). eclplay's stops are compared, and its tails (turn_port's marks 01 to 03: BALTHIR, WALKER,
    BALTHIR) with the original's tails."""
    entry = saved_entry(work / "cast-hurt", hurt)
    clw = cast_original(work, wild, hurt, "cast-clw", opts.verbose, cast_run_clw)
    m0 = clw["menu"]["seed"]
    nxt = clw["next"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == CAST_CLW_NEXT, f"the original's RandSeed at WALKER's menu after C, C is "
            f"{CAST_CLW_NEXT[0]} draws past BALTHIR's menu, at {CAST_CLW_NEXT[1]:#x}", f"{draws} draws, "
            f"{nxt['seed']:#x}")
    v.check(nxt["selected"] == MOVE_WILD_NEXT and rec_byte(nxt["blocks"][CAST_BALTHIR], 0) == CAST_CLW,
            f"after C, C the next combatant is WALKER KEYS ({MOVE_WILD_NEXT}) and BALTHIR's block holds CLW (+0 = 3)",
            f"{nxt['selected']}, spell {rec_byte(nxt['blocks'][CAST_BALTHIR], 0)}")
    p_next = command_port(work, wild, "cast-clw-next", "CC", entry=entry)
    command_state(v, "C, C (BALTHIR's Begins Casting: WALKER's menu)", nxt, p_next, m0)
    v.check(BATTLE_STOP.fullmatch(p_next["last"] or "") is not None and
            rec_byte(ported_blocks(p_next)[CAST_BALTHIR], 0) == CAST_CLW,
            "eclplay ends at WALKER's menu after C, C, with BALTHIR's block holding CLW", p_next["last"])
    aim = clw["aim"]
    p_aim = turn_port(work, wild, "cast-clw-aim", "CCE", entry=entry)
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == CAST_CLW_AIM, f"the original's RandSeed at BALTHIR's pending Aim (after WALKER's "
            f"End) is {CAST_CLW_AIM[0]} draws past BALTHIR's menu, at {CAST_CLW_AIM[1]:#x}", f"{draws} draws, "
            f"{aim['seed']:#x}")
    cast_screen(v, work, "BALTHIR's pending Cure Light Wounds at its Aim (row 23 Spell:Cure Light Wounds)",
                aim["screen"], p_aim, SHIELD_POINTER)
    command_state(v, "BALTHIR's pending cast at its Aim", aim, p_aim, nxt["seed"])
    tail = clw["tail"]
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == CAST_CLW_TAIL, f"the original's tail after T (CLW) is {CAST_CLW_TAIL[0]} draws past "
            f"BALTHIR's menu, at {CAST_CLW_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    hp = (rec_byte(aim["records"][CAST_BALTHIR], HIT_POINTS), rec_byte(tail["records"][CAST_BALTHIR], HIT_POINTS))
    v.check(hp == CAST_CLW_HP, "BALTHIR's hit points (+0x198) go from 0x32 to 0x3d at the tail (CLW's heal)",
            f"{hp[0]:#x} -> {hp[1]:#x}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    v.check(forgotten == CAST_CLW | CAST_FORGOTTEN and facing == 0,
            "at the tail BALTHIR's CLW is forgotten (+0xce 0x83) and he faces 0", f"+0xce {forgotten:#x}, facing {facing}")
    p_tail = turn_port(work, wild, "cast-clw-tail", "CCET", entry=entry)
    mark = cast_mark(p_tail, "03")
    command_state(v, "CLW's tail (the turn's tail, 03)", tail, mark, aim["seed"])
    v.check(rec_byte(ported_records(mark)[CAST_BALTHIR], HIT_POINTS) == CAST_CLW_HP[1] and
            rec_byte(ported_records(mark)[CAST_BALTHIR], CAST_MEMO) == CAST_CLW | CAST_FORGOTTEN,
            "eclplay's tail after CLW: BALTHIR at 0x3d, CLW forgotten (+0xce 0x83)", "")
    # Abort: the question at Exit, Yes (Spell Aborted), the tail.
    ab = cast_original(work, wild, hurt, "cast-abort", opts.verbose, cast_run_abort)
    question, abort_tail = ab["question"], ab["tail"]
    draws = L.draws(m0, abort_tail["seed"])
    v.check((draws, abort_tail["seed"]) == CAST_ABORT_TAIL, f"the original's tail after Y (Spell Aborted) is "
            f"{CAST_ABORT_TAIL[0]} draws past BALTHIR's menu, at {CAST_ABORT_TAIL[1]:#x}",
            f"{draws} draws, {abort_tail['seed']:#x}")
    forgotten = rec_byte(abort_tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(forgotten == CAST_CLW | CAST_FORGOTTEN, "the original's aborted CLW is forgotten (+0xce 0x83)",
            f"+0xce {forgotten:#x}")
    p_q = command_port(work, wild, "cast-abort-q", "CCEE", entry=entry)
    cast_screen(v, work, "the Abort Spell question (Yes, No) at BALTHIR's pending Aim's Exit",
                question["screen"], p_q, SHIELD_POINTER)
    p_abort = turn_port(work, wild, "cast-abort", "CCEEY", entry=entry)
    mark = cast_mark(p_abort, "03")
    command_state(v, "the abort's tail (Spell Aborted, 03)", abort_tail, mark, question["seed"])
    v.check(rec_byte(ported_records(mark)[CAST_BALTHIR], CAST_MEMO) == CAST_CLW | CAST_FORGOTTEN and
            "message: Spell Aborted" in p_abort["text"],
            "eclplay's tail after the abort: Spell Aborted (its message), CLW forgotten (+0xce 0x83)", "")


# Cast's computer turn (step 4, part 29a: dqk 5bb8:0306 and 5bb8:0170, queen_ai.c; the flow cast_auto; part 29b's
# computer's aim, queen_cast.c's pick). Saved game A with BALTHIR (CAST's bytes: CLW then MM memorised, the open ground).
# Alt-M at her menu (magic on: one draw, the menu again), then Alt-Q (the computer turns her: her dispatch at 5045:0891,
# then her computer turn's spell choice, which chooses MM: its d100 and d5, the cast at once, the announce, the pick of
# MM's target (52db:1d42, the computer's aim) and the flight), trapped at the turn's tail (5045:066a) with RandSeed
# settled; then the fight to its end, with no trap (the key wait of the results).
CAST_AUTO_MAGIC = (1, 0xEB831397)       # Alt-M: the bar message's wait, one draw past the menu, at its end
CAST_AUTO_DISPATCH = (2, 0xD9491FD4)    # Alt-Q: its own wait, then her computer turn's dispatch (5045:0891)
CAST_AUTO_TAIL = (0x5045, 0x066A)       # the turn's tail after the cast (5045:066a)
CAST_AUTO_TAIL_DRAWS = (25, 0x28390BDF) # RandSeed at the tail: 25 draws past the menu, 28390bdf
CAST_AUTO_CELL = (32, 18)               # the spell's cell (DS:bd2c, bd49: SHARKMAN 2's, the missile's target)
CAST_AUTO_TARGETS = 1                   # DS:c6f4: one target of MM
CAST_AUTO_SHARKMAN = 7                  # the record of SHARKMAN 2 (the list's index)
CAST_AUTO_HP = (0x4E, 0x49)             # SHARKMAN 2's hit points before and after the missile (1d4+1)
CAST_AUTO_MEMO = 0x8F                   # BALTHIR's +0xcf after the cast (MM forgotten; CLW at +0xce 0x03 stays)
CAST_AUTO_END = (438, 0xADD80F40)       # RandSeed at the fight's end: 438 draws past the menu, add80f40
CAST_AUTO_END_STATUS = (6, 7)           # the two SHARKMEN records, both 5 (dead, not yet removed) at the end
CAST_AUTO_DEAD = 5


def cast_auto_original(work, patched, data, verbose, tag="cast-auto", to_end=True):
    """The original at BALTHIR's first menu (CAST's save): Alt-M (the menu again), then Alt-Q: her computer turn's
    dispatch (5045:0891 spun, then given back), then the turn's tail (5045:066a, trapped once loaded, RandSeed settled
    by wait_seed: the cast done), then the trap given back and, if TO_END, the fight run to its end (key_wait,
    end_state). Returns the menu, the Alt-M and the dispatch's states, the tail's and, if TO_END, the end's.
    TAG names the box and the screens (cast_auto_area runs the same keys on its own save and stops at the tail)."""
    with cokbox.Box(workdir=work / f"box-{tag}", game="dark-queen", verbose=verbose, timeout=1800,
                    files={**L.files(patched), "SAVE/SAVGAMA.QSV": data}) as box:
        enter(box)
        box.set_seed(SEED)
        menu = to_menu(box)
        magic = cast_key(box, work, f"{tag}-magic", "\\@M")
        magic["magic"] = box.ds_byte(MAGIC)
        t = arm(box, menu["handle"], COMPUTER)
        box.keys("\\@Q")
        wait_spin(box, t, "5045:0891")
        dispatch = snapshot(box)
        dispatch.update(tail_state(box), selected=selected(box))
        L.untrap(box, t)
        t2 = L.trap(box, *CAST_AUTO_TAIL, seconds=TAIL_SECONDS)
        wait_seed(box, t2, "5045:066a")
        tail = snapshot(box)
        tail.update(selected=selected(box), cell=(box.ds_byte(0xBD2C), box.ds_byte(0xBD49)),
                    targets=box.ds_byte(CAST_TARGETS))
        tail["screen"] = box.screenshot(path=str(work / f"{tag}-tail.png"))
        L.untrap(box, t2)
        if not to_end:
            return dict(menu=menu, magic=magic, dispatch=dispatch, tail=tail)
        end_screen = key_wait(box)
        end = end_state(box)
        end["screen"] = end_screen
        box.settled(quiet=1.0, tolerance=0)
    return dict(menu=menu, magic=magic, dispatch=dispatch, tail=tail, end=end)


def cast_auto(v, work, opts):
    """Cast's computer turn (part 29a, dqk 5bb8:0306 and 5bb8:0170; part 29b's aim): Alt-M at BALTHIR's menu (magic on, one
    draw), then Alt-Q (her computer turn). The original: her dispatch's draws, then at the turn's tail (5045:066a) 25 draws
    past the menu, RandSeed 28390bdf, MM's target SHARKMAN 2 (hit points 0x4e to 0x49), its cell 32,18, DS:c6f4 1, and
    BALTHIR's +0xcf 0x8f (MM forgotten); then the fight to its end (438 draws, RandSeed add80f40, both SHARKMEN status 5).
    eclplay runs the same keys to its own stop (out of keys) and agrees on RandSeed, records and the end's statuses."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save = cast_save()
    o = cast_auto_original(work, wild, save, opts.verbose)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "cast-auto-save", save)
    p = turn_port(work, wild, "cast-auto", "\\@M\\@Q", entry=entry)
    magic, dispatch, tail, end = o["magic"], o["dispatch"], o["tail"], o["end"]
    v.check(magic["magic"] == 1 and (L.draws(m0, magic["seed"]), magic["seed"]) == CAST_AUTO_MAGIC,
            f"the original's Alt-M turns the magic on (DS:b1cb 1) with {CAST_AUTO_MAGIC[0]} draw past the menu, at "
            f"{CAST_AUTO_MAGIC[1]:#x}", f"DS:b1cb {magic['magic']}, {L.draws(m0, magic['seed'])} draws, {magic['seed']:#x}")
    v.check((L.draws(m0, dispatch["seed"]), dispatch["seed"]) == CAST_AUTO_DISPATCH,
            f"the original's RandSeed at BALTHIR's computer dispatch (after Alt-Q) is {CAST_AUTO_DISPATCH[0]} draws past "
            f"the menu, at {CAST_AUTO_DISPATCH[1]:#x}", f"{L.draws(m0, dispatch['seed'])} draws, {dispatch['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == CAST_AUTO_TAIL_DRAWS,
            f"the original's RandSeed at the turn's tail (5045:066a, settled) is {CAST_AUTO_TAIL_DRAWS[0]} draws past the "
            f"menu, at {CAST_AUTO_TAIL_DRAWS[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    v.check(tail["cell"] == CAST_AUTO_CELL and tail["targets"] == CAST_AUTO_TARGETS,
            f"at the tail MM's cell is {CAST_AUTO_CELL} and DS:c6f4 is {CAST_AUTO_TARGETS} (one target)",
            f"cell {tail['cell']}, DS:c6f4 {tail['targets']}")
    hp = (rec_byte(tail["records"][CAST_AUTO_SHARKMAN], HIT_POINTS), rec_byte(dispatch["records"][CAST_AUTO_SHARKMAN], HIT_POINTS))
    v.check(hp == (CAST_AUTO_HP[1], CAST_AUTO_HP[0]),
            f"MM hits SHARKMAN 2 (hit points {CAST_AUTO_HP[0]:#x} to {CAST_AUTO_HP[1]:#x}) and BALTHIR's +0xcf is "
            f"{CAST_AUTO_MEMO:#x} (MM forgotten) at the tail",
            f"hit points {hp[1]:#x} to {hp[0]:#x}, +0xcf {rec_byte(tail['records'][CAST_BALTHIR], CAST_MEMO + 1):#x}")
    v.check(rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO + 1) == CAST_AUTO_MEMO,
            "BALTHIR's +0xcf is 0x8f at the tail", f"{rec_byte(tail['records'][CAST_BALTHIR], CAST_MEMO + 1):#x}")
    end_draws = L.draws(m0, end["seed"])
    v.check((end_draws, end["seed"]) == CAST_AUTO_END,
            f"the original's RandSeed at the fight's end is {CAST_AUTO_END[0]} draws past the menu, at {CAST_AUTO_END[1]:#x}",
            f"{end_draws} draws, {end['seed']:#x}")
    v.check(all(rec_byte(end["records"][i], STATUS_AT) == CAST_AUTO_DEAD for i in CAST_AUTO_END_STATUS),
            "both SHARKMEN are status 5 at the fight's end",
            " ".join(f"{rec_byte(end['records'][i], STATUS_AT)}" for i in CAST_AUTO_END_STATUS))
    # eclplay's mark after BALTHIR's computer turn's tail (turn_port's 02: 01 is the turn Alt-Q gives back) against the
    # original's tail: the computer's aim and the cast compared (RandSeed, records, blocks, turn list).
    mark = cast_mark(p, "02")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's computer turn's mark (02) after Alt-M and Alt-Q", p["last"])
    else:
        command_state(v, "BALTHIR's computer turn's tail (MM at SHARKMAN 2, turn mark 02)", tail, mark, m0)
        marked = ported_records(mark)
        v.check(rec_byte(marked[CAST_AUTO_SHARKMAN], HIT_POINTS) == CAST_AUTO_HP[1] and
                rec_byte(marked[CAST_BALTHIR], CAST_MEMO + 1) == CAST_AUTO_MEMO,
                f"eclplay's tail after the computer's MM: SHARKMAN 2 at {CAST_AUTO_HP[1]:#x}, BALTHIR's +0xcf "
                f"{CAST_AUTO_MEMO:#x}", f"SHARKMAN 2 {rec_byte(marked[CAST_AUTO_SHARKMAN], HIT_POINTS):#x}, +0xcf "
                f"{rec_byte(marked[CAST_BALTHIR], CAST_MEMO + 1):#x}")
    # eclplay runs the same keys past the tail and on through SHARKMAN's turn (effect 0x2f, ROARK's, is ported: 78e4:09b2) to
    # the fight's end (Alt-Q leaves the rest of the fight to the computer, as in the original), then out of keys in block 1
    # after it: its RandSeed there and both SHARKMEN's statuses are the original's end. BALTHIR's +0xcf (MM forgotten, not
    # memorised again) is compared too.
    v.check(BATTLE_STOP.fullmatch(p["last"] or "") is not None,
            "eclplay ends out of keys in block 1 after the fight", p["last"])
    ends = [line for line in p["text"] if line.startswith("combat: seed ")]
    field = p["base"].with_suffix(".field")
    seed = parse_field(field)[0] if field.exists() else None
    v.check(ends == [f"combat: seed {CAST_AUTO_END[1]}"] and seed == CAST_AUTO_END[1],
            f"eclplay's fight ends at the original's RandSeed ({CAST_AUTO_END[1]:#x}), and its stop after it too",
            f"ends {ends}, stop {seed:#x}" if seed is not None else f"ends {ends}, no field")
    ported = ported_records(p)
    v.check(len(ported) > max(CAST_AUTO_END_STATUS) and
            all(rec_byte(ported[i], STATUS_AT) == CAST_AUTO_DEAD for i in CAST_AUTO_END_STATUS),
            "eclplay's SHARKMEN are both status 5 at the fight's end",
            " ".join(str(rec_byte(ported[i], STATUS_AT)) for i in CAST_AUTO_END_STATUS if i < len(ported)))
    memo = rec_byte(ported[CAST_BALTHIR], CAST_MEMO + 1) if len(ported) > CAST_BALTHIR else None
    v.check(memo == CAST_AUTO_MEMO,
            f"eclplay's BALTHIR keeps +0xcf {CAST_AUTO_MEMO:#x} (MM forgotten) at its stop", f"{memo}")
    print(f"eclplay (cast auto) ends {p['last']!r}", flush=True)


# Cast's computer area cast (step 4, part 31f: 5bb8:0306's choice, 52db:2543, the computer's area aim 52db:48aa, the flight
# and the burst, carry-over C3: the handler reads the caster's level through vm.character, set by the computer's cast).
# Saved game A with BALTHIR memorising Delayed Blast Fireball (+0xce 0x73) and nothing else: Alt-M at his menu (the magic on,
# one draw), then Alt-Q (her computer turn: the dispatch's two draws, then the choice: d100 above her p 40, d5 at or below
# the priority 9, the cast at once, the aim, the flight), trapped at the turn's tail (5045:066a) with RandSeed settled. The
# original stops there; eclplay runs the same keys with the turn marks and its stop after the tail is reported.
CAST_AUTO_AREA_TAIL = (171, 0x7DDC25DD)  # RandSeed at the tail: 171 draws past the menu, 7ddc25dd
CAST_AUTO_AREA_CELL = (35, 12)           # the burst's cell (DS:bd2c, bd49)
CAST_AUTO_AREA_TARGET = 6                # the record of SHARKMAN 1 (the list's index: 4aba:0f9d), the burst's one target
CAST_AUTO_AREA_HP = (0x4E, 0x45)         # SHARKMAN 1's hit points before and after the burst
CAST_AUTO_AREA_MEMO = 0xF3               # BALTHIR's +0xce after the cast (Delayed Blast Fireball forgotten)


def cast_auto_area(v, work, opts):
    """Cast's computer area cast (part 31f): Alt-M at BALTHIR's menu (one draw), then Alt-Q (her computer turn). The original,
    at the tail (5045:066a): 171 draws past the menu, RandSeed 7ddc25dd, the burst's cell 35,12 and one target (DS:c6f4 1),
    SHARKMAN 1 (record 6) hit from 0x4e to 0x45, and BALTHIR's +0xce 0xf3 (Delayed Blast Fireball forgotten). eclplay runs the
    same keys: its turn mark 02 (the computer turn's tail) agrees with the original's (RandSeed, records, blocks, effect
    chains); its stop after the tail is reported, not checked."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save = dbf_save()
    o = cast_auto_original(work, wild, save, opts.verbose, tag="cast-auto-area", to_end=False)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "cast-auto-area-save", save)
    p = turn_port(work, wild, "cast-auto-area", "\\@M\\@Q", entry=entry)
    magic, dispatch, tail = o["magic"], o["dispatch"], o["tail"]
    v.check(magic["magic"] == 1 and (L.draws(m0, magic["seed"]), magic["seed"]) == CAST_AUTO_MAGIC,
            f"the original's Alt-M turns the magic on (DS:b1cb 1) with {CAST_AUTO_MAGIC[0]} draw past the menu, at "
            f"{CAST_AUTO_MAGIC[1]:#x}", f"DS:b1cb {magic['magic']}, {L.draws(m0, magic['seed'])} draws, {magic['seed']:#x}")
    v.check((L.draws(m0, dispatch["seed"]), dispatch["seed"]) == CAST_AUTO_DISPATCH,
            f"the original's RandSeed at BALTHIR's computer dispatch (after Alt-Q) is {CAST_AUTO_DISPATCH[0]} draws past "
            f"the menu, at {CAST_AUTO_DISPATCH[1]:#x}", f"{L.draws(m0, dispatch['seed'])} draws, {dispatch['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == CAST_AUTO_AREA_TAIL,
            f"the original's RandSeed at the turn's tail (5045:066a, settled) is {CAST_AUTO_AREA_TAIL[0]} draws past the "
            f"menu, at {CAST_AUTO_AREA_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    v.check(tail["cell"] == CAST_AUTO_AREA_CELL and tail["targets"] == CAST_AUTO_TARGETS,
            f"at the tail the burst's cell is {CAST_AUTO_AREA_CELL} and DS:c6f4 is {CAST_AUTO_TARGETS} (one target)",
            f"cell {tail['cell']}, DS:c6f4 {tail['targets']}")
    hp = (rec_byte(dispatch["records"][CAST_AUTO_AREA_TARGET], HIT_POINTS),
          rec_byte(tail["records"][CAST_AUTO_AREA_TARGET], HIT_POINTS))
    v.check(hp == CAST_AUTO_AREA_HP,
            f"the burst hits SHARKMAN 1 (record {CAST_AUTO_AREA_TARGET}) from {CAST_AUTO_AREA_HP[0]:#x} to {CAST_AUTO_AREA_HP[1]:#x}",
            f"hit points {hp[0]:#x} -> {hp[1]:#x}")
    memo = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(memo == CAST_AUTO_AREA_MEMO,
            f"BALTHIR's +0xce is {CAST_AUTO_AREA_MEMO:#x} (Delayed Blast Fireball forgotten) at the tail", f"{memo:#x}")
    # eclplay's mark after BALTHIR's computer turn's tail (turn_port's 02: 01 is the turn Alt-Q gives back) against the
    # original's tail: the computer's aim, the choice and the burst compared (RandSeed, records, blocks, chains, turn list).
    mark = cast_mark(p, "02")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's computer turn's mark (02) after Alt-M and Alt-Q", p["last"])
    else:
        command_state(v, "BALTHIR's computer area cast's tail (turn mark 02)", tail, mark, m0)
        chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (BALTHIR's computer area cast, turn mark 02)")
        marked = ported_records(mark)
        v.check(rec_byte(marked[CAST_AUTO_AREA_TARGET], HIT_POINTS) == CAST_AUTO_AREA_HP[1] and
                rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == CAST_AUTO_AREA_MEMO,
                f"eclplay's tail after the computer's burst: SHARKMAN 1 at {CAST_AUTO_AREA_HP[1]:#x}, +0xce "
                f"{CAST_AUTO_AREA_MEMO:#x}", f"SHARKMAN 1 {rec_byte(marked[CAST_AUTO_AREA_TARGET], HIT_POINTS):#x}, +0xce "
                f"{rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")
    print(f"eclplay (cast auto area) ends {p['last']!r}", flush=True)


# Band a1's plain apply (step 4, part 30a-2: queen_cast.c's plain table, Silence 0x19, 075a: "is silenced"). Saved game A
# with BALTHIR's memorised byte +0xce 0x19 (Silence, 15 minutes; CAST's CLW and MM are not memorised): C, C (Begins
# Casting: BALTHIR's turn ends at WALKER's menu, 2 draws), E (WALKER's End: his pending Silence at its Aim, 3 draws), N
# (SHARKMAN 2 under the cursor, no draw), then T: the tail. The flight (12 draws, as MM's), SHARKMAN 2's saving throw (its
# d20 and the save, saved: no chain, "is Unaffected") and the say's wait. The Aim's draws are CLW's (the same keys).
BAND_A1_SILENCE = 0x19
BAND_A1_TAIL = (17, 0xF79AFEC7)         # the tail after T: 17 draws past the menu (3 at the Aim, 12 flight, d20, the say's 1)
BAND_A1_CELL = (32, 18)                 # the spell's cell (DS:bd2c, bd49: SHARKMAN 2's)
BAND_A1_FACING = 3                      # BALTHIR's facing after the Silence (at SHARKMAN 2, as MM's)


def silence_save():
    """Saved game A with BALTHIR memorising Silence (+0xce 0x19) and nothing else (the WILD block's fixture)."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_A1_SILENCE}})


def band_a1_run(box, menu, work, tag):
    """C, C (Begins Casting: WALKER's menu), E (WALKER's End: BALTHIR's pending Silence at its Aim), N (SHARKMAN 2 under
    the cursor), then T: the tail."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "CC", None)])[0])
    out["aim"] = cast_key(box, work, f"{tag}-aim", "E")
    out["n"] = cast_key(box, work, f"{tag}-n", "N")
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "T", None)])[0]
    return out


def band_a1(v, work, opts):
    """Band a1's plain apply (part 30a-2), the flow band_a1: BALTHIR's Silence at SHARKMAN 2 (save kind 1, type 4), the
    original against eclplay's stops. The original: RandSeed at WALKER's menu after C, C (CAST_CLW_NEXT), at the pending
    Silence's Aim after E (CAST_CLW_AIM), and at the tail after T 17 draws past the menu, RandSeed f79afec7; DS:c6f4 1, the
    cell 32,18, SHARKMAN 2 (record 7) with no effect chain (it saved: "is Unaffected"), BALTHIR's +0xce 0x99 and facing 3.
    eclplay runs the same keys: the Aim's screen (row 23 "Spell:Silence") compared, and its turn mark 03 (the tail) agrees
    on RandSeed, records, blocks, effect chains and the turn list."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save = silence_save()
    o = cast_original(work, wild, save, "band-a1", opts.verbose, band_a1_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-a1-save", save)
    nxt, aim, tail = o["next"], o["aim"], o["tail"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == CAST_CLW_NEXT, f"the original's RandSeed at WALKER's menu after C, C (Silence: "
            f"Begins Casting) is {CAST_CLW_NEXT[0]} draws past BALTHIR's menu, at {CAST_CLW_NEXT[1]:#x}",
            f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == CAST_CLW_AIM, f"the original's RandSeed at BALTHIR's pending Silence at its Aim "
            f"(after WALKER's End) is {CAST_CLW_AIM[0]} draws past BALTHIR's menu, at {CAST_CLW_AIM[1]:#x}",
            f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_A1_TAIL, f"the original's tail after T (Silence) is {BAND_A1_TAIL[0]} draws past "
            f"BALTHIR's menu, at {BAND_A1_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    v.check(L.draws(aim["seed"], o["n"]["seed"]) == 0, "the original's RandSeed after N (SHARKMAN 2 under the cursor) is "
            "unchanged", f"{L.draws(aim['seed'], o['n']['seed'])} draws")
    v.check(tail["cell"] == BAND_A1_CELL and tail["targets"] == 1, f"at the tail Silence's cell is {BAND_A1_CELL} and "
            f"DS:c6f4 is 1", f"cell {tail['cell']}, DS:c6f4 {tail['targets']}")
    v.check(tail["chains"][CAST_AUTO_SHARKMAN] == [], "SHARKMAN 2 (record 7) has no effect at the tail: it saved "
            "(\"is Unaffected\")", f"{tail['chains'][CAST_AUTO_SHARKMAN]}")
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(forgotten == BAND_A1_SILENCE | CAST_FORGOTTEN and facing == BAND_A1_FACING,
            "at the tail BALTHIR's Silence is forgotten (+0xce 0x99) and he faces 3", f"+0xce {forgotten:#x}, facing {facing}")
    p_aim = turn_port(work, wild, "band-a1-aim", "CCE", entry=entry)
    cast_screen(v, work, "BALTHIR's pending Silence at its Aim (row 23 Spell:Silence)", aim["screen"], p_aim, SHIELD_POINTER)
    command_state(v, "BALTHIR's pending Silence at its Aim", aim, p_aim, nxt["seed"])
    p = turn_port(work, wild, "band-a1", "CCENT", entry=entry)
    mark = cast_mark(p, "03")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's turn mark (03) after C, C, E, N and T", p["last"])
        return
    command_state(v, "Silence's tail (the turn's tail, 03)", tail, mark, aim["seed"])
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (Silence's tail, turn mark 03)")
    marked = ported_records(mark)
    v.check(rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_A1_SILENCE | CAST_FORGOTTEN and
            rec_byte(marked[CAST_AUTO_SHARKMAN], STATUS_AT) == rec_byte(tail["records"][CAST_AUTO_SHARKMAN], STATUS_AT),
            "eclplay's tail after Silence: BALTHIR's Silence forgotten (+0xce 0x99), SHARKMAN 2 unchanged",
            f"+0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")


# Band a2's one-target damage and heals (step 4, part 30b: queen_cast.c's 0162, 01e6, 0588, 1e31, 20df, 1c2e, 2073, 2c32,
# 0ff6). Shocking Grasp (0x14, a touch, the mage's kind 2) is memorised alone: C, C (the Aim at once, one draw), then T
# (himself). The touch's hit roll misses (AC 65 against THAC0 53
# and the d20): no text, no damage, +0xce 0x94, the tail 3 draws past the menu. The druid's Cure Light Wounds (0x62, a
# touch, time 4: CLW's order) at CAST-HURT's 0x32 hit points: C, C (Begins Casting: WALKER's menu), E (WALKER's End: the
# pending Aim), then T: the tail, the heal of 1d8 and "is partially healed".
BAND_A2_SHOCK = 0x14
BAND_A2_CLW = 0x62
BAND_A2_SHOCK_AIM = (1, 0xEB831397)     # C, C: the Aim at once, one draw past the menu
BAND_A2_SHOCK_TAIL = (3, 0x8A1EDDA5)    # the tail after T: the Aim's draw, the handler's 1d8, the touch's d20
BAND_A2_SHOCK_HP = 0x6A                 # BALTHIR's hit points before and at the tail (the touch missed)
BAND_A2_CLW_NEXT = CAST_CLW_NEXT        # the same keys and time as CLW (1): the same draws expected
BAND_A2_CLW_AIM = CAST_CLW_AIM
BAND_A2_CLW_TAIL = (5, 0xB1457E83)      # the tail after T: 5 draws past the menu (the Aim's 3, the heal's d8, the say's wait)
BAND_A2_CLW_HP = 0x36                   # 0x32 plus the heal's d8: 4 (the next draw after RandSeed 8a1edda5, the Aim's)


def shock_save():
    """Saved game A with BALTHIR memorising Shocking Grasp (+0xce 0x14) and nothing else (the WILD block's fixture)."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_A2_SHOCK}})


def clw_hurt_save():
    """Saved game A with BALTHIR memorising the druid's Cure Light Wounds (+0xce 0x62) at CAST-HURT's 0x32 hit points."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_A2_CLW, HIT_POINTS: CAST_HURT}})


def shock_run(box, menu, work, tag):
    """C, C (the Aim at once: Shocking Grasp's Aim, one draw), then T (himself): the tail."""
    out = dict(aim=cast_key(box, work, f"{tag}-aim", "CC"))
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "T", None)])[0]
    return out


def band_a2_shock(v, work, wild, opts):
    """Shocking Grasp on himself (the touch misses): the Aim at once, the tail (RandSeed, hit points, the byte forgotten,
    facing 0), and eclplay's tail (turn mark 01) and its Aim agree with the original's."""
    save = shock_save()
    o = cast_original(work, wild, save, "band-a2-shock", opts.verbose, shock_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-a2-shock-save", save)
    aim, tail = o["aim"], o["tail"]
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == BAND_A2_SHOCK_AIM, f"the original's RandSeed at Shocking Grasp's Aim (C, C: the Aim at "
            f"once) is {BAND_A2_SHOCK_AIM[0]} draw past BALTHIR's menu, at {BAND_A2_SHOCK_AIM[1]:#x}",
            f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_A2_SHOCK_TAIL, f"the original's tail after T (Shocking Grasp on herself) is "
            f"{BAND_A2_SHOCK_TAIL[0]} draws past BALTHIR's menu, at {BAND_A2_SHOCK_TAIL[1]:#x}",
            f"{draws} draws, {tail['seed']:#x}")
    hp = (rec_byte(aim["records"][CAST_BALTHIR], HIT_POINTS), rec_byte(tail["records"][CAST_BALTHIR], HIT_POINTS))
    v.check(hp == (BAND_A2_SHOCK_HP, BAND_A2_SHOCK_HP), f"BALTHIR's hit points (+0x198) stay {BAND_A2_SHOCK_HP:#x} at the "
            "tail (the touch missed: no damage, no text)", f"{hp[0]:#x} -> {hp[1]:#x}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    v.check(forgotten == BAND_A2_SHOCK | CAST_FORGOTTEN and facing == 0,
            "at the tail BALTHIR's Shocking Grasp is forgotten (+0xce 0x94) and he faces 0",
            f"+0xce {forgotten:#x}, facing {facing}")
    p_aim = command_port(work, wild, "band-a2-shock-aim", "CC", entry=entry)
    command_state(v, "Shocking Grasp's Aim (C, C)", aim, p_aim, m0)
    p = turn_port(work, wild, "band-a2-shock", "CCT", entry=entry)
    mark = cast_mark(p, "01")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's turn mark (01) after C, C and T", p["last"])
        return
    command_state(v, "Shocking Grasp's tail (the turn's tail, 01)", tail, mark, aim["seed"])
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (Shocking Grasp's tail, turn mark 01)")
    marked = ported_records(mark)
    v.check(rec_byte(marked[CAST_BALTHIR], HIT_POINTS) == BAND_A2_SHOCK_HP and
            rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_A2_SHOCK | CAST_FORGOTTEN,
            f"eclplay's tail after Shocking Grasp: BALTHIR at {BAND_A2_SHOCK_HP:#x}, +0xce {BAND_A2_SHOCK | CAST_FORGOTTEN:#x}",
            f"{rec_byte(marked[CAST_BALTHIR], HIT_POINTS):#x}, +0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")


def band_a2_clw(v, work, wild, opts):
    """The druid's Cure Light Wounds at CAST-HURT's 0x32 (time 4, CLW's order: C, C, E, then T): the draws at WALKER's menu,
    at the Aim and at the tail, the heal, the byte forgotten, and eclplay's tail (turn mark 03) agreeing with the original's."""
    save = clw_hurt_save()
    o = cast_original(work, wild, save, "band-a2-clw", opts.verbose, cast_run_clw)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-a2-clw-save", save)
    nxt, aim, tail = o["next"], o["aim"], o["tail"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == BAND_A2_CLW_NEXT, f"the original's RandSeed at WALKER's menu after C, C (the druid's "
            f"CLW: Begins Casting) is {BAND_A2_CLW_NEXT[0]} draws past BALTHIR's menu, at {BAND_A2_CLW_NEXT[1]:#x}",
            f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == BAND_A2_CLW_AIM, f"the original's RandSeed at BALTHIR's pending druid's CLW at its Aim "
            f"(after WALKER's End) is {BAND_A2_CLW_AIM[0]} draws past BALTHIR's menu, at {BAND_A2_CLW_AIM[1]:#x}",
            f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_A2_CLW_TAIL, "the original's tail after T (the druid's CLW)",
            f"{draws} draws, {tail['seed']:#x}")
    hp = (rec_byte(aim["records"][CAST_BALTHIR], HIT_POINTS), rec_byte(tail["records"][CAST_BALTHIR], HIT_POINTS))
    v.check(hp == (CAST_HURT, BAND_A2_CLW_HP), f"BALTHIR's hit points (+0x198) go from {CAST_HURT:#x} to "
            f"{BAND_A2_CLW_HP:#x} at the tail (one d8)", f"{hp[0]:#x} -> {hp[1]:#x}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    v.check(forgotten == BAND_A2_CLW | CAST_FORGOTTEN and facing == 0,
            "at the tail BALTHIR's druid's CLW is forgotten (+0xce 0xe2) and he faces 0", f"+0xce {forgotten:#x}, facing {facing}")
    p_aim = turn_port(work, wild, "band-a2-clw-aim", "CCE", entry=entry)
    command_state(v, "the druid's CLW at its Aim", aim, p_aim, nxt["seed"])
    p = turn_port(work, wild, "band-a2-clw", "CCET", entry=entry)
    mark = cast_mark(p, "03")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's turn mark (03) after C, C, E and T", p["last"])
        return
    command_state(v, "the druid's CLW's tail (the turn's tail, 03)", tail, mark, aim["seed"])
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (the druid's CLW's tail, turn mark 03)")
    marked = ported_records(mark)
    v.check(rec_byte(marked[CAST_BALTHIR], HIT_POINTS) == BAND_A2_CLW_HP and
            rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_A2_CLW | CAST_FORGOTTEN,
            f"eclplay's tail after the druid's CLW: BALTHIR at {BAND_A2_CLW_HP:#x}, +0xce {BAND_A2_CLW | CAST_FORGOTTEN:#x}",
            f"{rec_byte(marked[CAST_BALTHIR], HIT_POINTS):#x}, +0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")


def band_a2(v, work, opts):
    """Band a2's one-target damage and heals (part 30b), the flow band_a2: Shocking Grasp on himself (the touch misses)
    and the druid's Cure Light Wounds at 0x32 (the heal), each against eclplay's stops (its Aim and turn marks: RandSeed,
    records, combat blocks, effect chains, turn list) on the open ground (the WILD block)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    band_a2_shock(v, work, wild, opts)
    band_a2_clw(v, work, wild, opts)


# Band a3's flow (step 4, part 30c: Detect Magic, a plain apply, and Mirror Image, the d4 apply): BALTHIR memorises one
# spell (+0xce) and casts it at once on his own turn (kind 0, time 0: C, C, the list's first spell, then the tail; no
# Aim). Detect Magic (0x05: effect 5 with value 10, his level, for 10 minutes) puts effect 5 on him with the flash, and
# his racial 0x5f runs at event 9 (d100, a0). Mirror Image (0x20, kind 2: BALTHIR's mage level is 2, max(class 5
# 0, class 4 10 - 8)) puts effect 0x1c on him with the value d4 << 5 | 2, the d4 drawn first. eclplay's turn mark 01 (the
# tail) agrees with the original's on RandSeed, records, blocks and effect chains. A stop past the tail (0x1c's handler at
# its next event) is not compared.
BAND_A3_DETECT = 0x05
BAND_A3_MIRROR = 0x20
BAND_A3_DETECT_EFFECT, BAND_A3_MIRROR_EFFECT = 5, 0x1C
BAND_A3_DETECT_TAIL = (22, 0xA1C0CEE0)  # RandSeed at the tail after C, C (Detect Magic): (draws past the menu, seed)
BAND_A3_MIRROR_TAIL = (23, 0x78F34661)  # the same for Mirror Image (its d4: one draw more)
BAND_A3_DETECT_MINUTES = 10     # the new effect 5's minutes at the tail (its level 10)
BAND_A3_MIRROR_MINUTES = 4      # the new effect 0x1c's minutes at the tail (level 2)


def band_a3_run(box, menu, work, tag):
    """C, C (the list's first spell, cast at once), then the turn's tail (5045:066a)."""
    return dict(tail=cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "CC", None)])[0])


def band_a3_spell(v, work, wild, opts, spell, name, effect, tail_want, minutes, value_ok):
    """One spell of band a3: the original's tail (draws, RandSeed, the new node, the memorised byte forgotten), then
    eclplay's turn mark 01 against it (RandSeed, records, blocks, effect chains)."""
    save = dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: spell}})
    o = cast_original(work, wild, save, f"band-a3-{name}", opts.verbose, band_a3_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / f"band-a3-{name}-save", save)
    tail = o["tail"]
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == tail_want, f"the original's tail after C, C ({name}: {spell:#x}) is {tail_want} "
            f"(draws past the menu, RandSeed)", f"{draws} draws, {tail['seed']:#x}")
    node = [n for n in tail["chains"][CAST_BALTHIR] if n[0] == effect]
    v.check(len(node) == 1 and node[0][1] == minutes and value_ok(node[0][2]) and node[0][3] == 0,
            f"BALTHIR's new effect {effect:#x} at the tail: {minutes} minutes, flag 0, its value as the spell gives",
            f"{tail['chains'][CAST_BALTHIR]}")
    v.check(rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO) == spell | CAST_FORGOTTEN,
            f"at the tail BALTHIR's {spell:#x} is forgotten (+0xce {spell | CAST_FORGOTTEN:#x})",
            f"+0xce {rec_byte(tail['records'][CAST_BALTHIR], CAST_MEMO):#x}")
    p = turn_port(work, wild, f"band-a3-{name}", "CC", entry=entry)
    mark = cast_mark(p, "01")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, f"eclplay writes BALTHIR's turn mark (01) after C, C ({name})", p["last"])
        return
    command_state(v, f"{name}'s tail (the turn's tail, 01)", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), f" ({name}'s tail, turn mark 01)")
    print(f"eclplay ({name}) ends {p['last']!r}", flush=True)


def band_a3(v, work, opts):
    """Band a3's spells on BALTHIR's own turn (step 4, part 30c), the flow band_a3: Detect Magic (effect 5, 10 for its
    minutes, with his 0x5f at event 9) and Mirror Image (effect 0x1c, d4 << 5 | 2), each against eclplay's turn mark 01."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    band_a3_spell(v, work, wild, opts, BAND_A3_DETECT, "detect", BAND_A3_DETECT_EFFECT, BAND_A3_DETECT_TAIL,
                  BAND_A3_DETECT_MINUTES, lambda value: value == 10)
    band_a3_spell(v, work, wild, opts, BAND_A3_MIRROR, "mirror", BAND_A3_MIRROR_EFFECT, BAND_A3_MIRROR_TAIL,
                  BAND_A3_MIRROR_MINUTES, lambda value: (value & 0x1F) == 2 and 1 <= value >> 5 <= 4)


# Band b1's flight and bursts (step 4, part 31a), run on to the tail by band b2 (part 31b): queen_cast.c's picture by
# direction and bursts, queen_area.c's handler. Saved game A with BALTHIR memorising Delayed Blast Fireball (+0xce 0x73,
# time 0): C, C (the Aim at once, one draw), N (SHARKMAN 2 under the cursor, no draw), then T: the flight (the Fireball
# family's picture, one frame), the burst of radius 2 (var 0x22 0), the handler (nd6 of fire at his level 2, the save
# halves it, the level added to the dice), the byte forgotten, and the turn's tail (5045:066a).
BAND_B1_DBF = 0x73
BAND_B1_AIM = (1, 0xEB831397)           # C, C: the Aim at once, one draw past the menu
BAND_B1_TAIL = (119, 0xAD10BE81)        # T: the tail, 119 draws past the menu
BAND_B1_HP = (0x4E, 0x4B)               # SHARKMAN 2 before and at the tail: (2d6 + 2) halved by his save
BAND_B1_FACING = 3                      # BALTHIR's facing at the tail (at SHARKMAN 2)


def dbf_save():
    """Saved game A with BALTHIR memorising Delayed Blast Fireball (+0xce 0x73) and nothing else (the WILD block's fixture)."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_B1_DBF}})


def band_b1_run(box, menu, work, tag):
    """C, C (the Aim at once), N (SHARKMAN 2 under the cursor), then T: the tail (5045:066a)."""
    out = dict(aim=cast_key(box, work, f"{tag}-aim", "CC"))
    out["n"] = cast_key(box, work, f"{tag}-n", "N")
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "T", None)])[0]
    return out


def band_b1(v, work, opts):
    """Band b1's flight and bursts (part 31a), run on to the tail (band b2's handler, part 31b), the flow band_b1: Delayed
    Blast Fireball at SHARKMAN 2 (32, 18). The original at the tail (RandSeed 119 draws past the menu, ad10be81), against
    eclplay's turn mark 01 (the first turn's tail): RandSeed, records, blocks, effect chains."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save = dbf_save()
    o = cast_original(work, wild, save, "band-b1", opts.verbose, band_b1_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-b1-save", save)
    aim, n, tail = o["aim"], o["n"], o["tail"]
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == BAND_B1_AIM, f"the original's RandSeed at Delayed Blast Fireball's Aim (C, C) is "
            f"{BAND_B1_AIM[0]} draw past BALTHIR's menu, at {BAND_B1_AIM[1]:#x}", f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(aim["seed"], n["seed"])
    v.check(draws == 0, "the original's RandSeed after N (SHARKMAN 2 under the cursor) is unchanged", f"{draws} draws")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_B1_TAIL, f"the original's RandSeed at the tail after T is {BAND_B1_TAIL[0]} "
            f"draws past the menu, at {BAND_B1_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    hp = (rec_byte(o["menu"]["records"][CAST_SHARKMAN_2], HIT_POINTS), rec_byte(tail["records"][CAST_SHARKMAN_2], HIT_POINTS))
    v.check(hp == BAND_B1_HP, f"SHARKMAN 2's hit points go from {BAND_B1_HP[0]:#x} to {BAND_B1_HP[1]:#x} at the tail",
            f"{hp[0]:#x} -> {hp[1]:#x}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    v.check(forgotten == BAND_B1_DBF | CAST_FORGOTTEN and facing == BAND_B1_FACING,
            f"at the tail BALTHIR's Delayed Blast Fireball is forgotten (+0xce 0xf3) and he faces {BAND_B1_FACING}",
            f"+0xce {forgotten:#x}, facing {facing}")
    p = turn_port(work, wild, "band-b1", "CCNT", entry=entry)
    mark = cast_mark(p, "01")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's turn mark (01) after C, C, N and T", p["last"])
        return
    command_state(v, "Delayed Blast Fireball's tail (the turn's tail, 01)", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (Delayed Blast Fireball's tail, turn mark 01)")
    marked = ported_records(mark)
    v.check(rec_byte(marked[CAST_SHARKMAN_2], HIT_POINTS) == BAND_B1_HP[1] and
            rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_B1_DBF | CAST_FORGOTTEN,
            f"eclplay's tail after Delayed Blast Fireball: SHARKMAN 2 at {BAND_B1_HP[1]:#x}, +0xce {BAND_B1_DBF | CAST_FORGOTTEN:#x}",
            f"{rec_byte(marked[CAST_SHARKMAN_2], HIT_POINTS):#x}, +0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")
    print(f"eclplay (band b1) ends {p['last']!r}", flush=True)


# Band b2 (step 4, part 31b, queen_area.c): Ice Storm (0x57, 3d10 once for every target) and Flame Strike (0x4a, 6d8,
# halved by a save), on BALTHIR's own cast, after the flight (band b1's picture and bursts). The Ice Storm (time 1) is
# CLW's keys: C, C (Begins Casting at WALKER's menu), E (WALKER's End: BALTHIR's pending Aim), then T on his own cell.
# Flame Strike (time 2): C, C, E (WALKER's End: SHARKMAN 2 hits WALKER, then TIMASALIN's menu), E (TIMASALIN's End: his
# pending Aim), N (SHARKMAN 2 under the cursor), then T. eclplay's turn marks: the one whose RandSeed is the original's.
BAND_B2_ICE = 0x57
BAND_B2_FLAME = 0x4A
CAST_ROLAND, CAST_TIMASALIN, CAST_WALKER = 0, 1, 5    # the records' list order (57fb:0000 + 0x19e * i; CAST_BALTHIR is 3)
BAND_B2_ICE_TAIL = (124, 0x379F92A2)    # the tail after T: 124 draws past the menu (the Aim's 3, the 3d10 once, the flashes)
BAND_B2_ICE_HP = {                       # hit points at the tail: the four listed targets each 20 down
    CAST_ROLAND: (0x86, 0x72), CAST_TIMASALIN: (0x3D, 0x29), CAST_BALTHIR: (0x6A, 0x56), CAST_WALKER: (0x45, 0x31)}
BAND_B2_FLAME_NEXT = (3, 0x8A1EDDA5)    # C, C (Flame Strike, time 2): WALKER's menu, one draw more than CLW's (2)
BAND_B2_FLAME_MENU = (16, 0x3A060FCE)   # TIMASALIN's menu after WALKER's End (SHARKMAN 2's turn between)
BAND_B2_FLAME_AIM = (17, 0xF79AFEC7)    # BALTHIR's pending Aim after TIMASALIN's End
BAND_B2_FLAME_TAIL = (32, 0x2BDCA33E)   # the tail after T: 32 draws past the menu
BAND_B2_FLAME_HP = (0x4E, 0x40)         # SHARKMAN 2 before and at the tail (6d8 halved by his save)
BAND_B2_FLAME_NAMES = "CCEENT"          # the keys of the flame flow (eclplay)
BAND_B2_FLAME_MARK = "05"               # eclplay's turn mark of the tail (the marks 01 to 04: the turns before it)


def ice_save():
    """Saved game A with BALTHIR memorising Ice Storm (+0xce 0x57) and nothing else."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_B2_ICE}})


def flame_save():
    """Saved game A with BALTHIR memorising Flame Strike (+0xce 0x4a) and nothing else."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_B2_FLAME}})


def cast_run_flame(box, menu, work, tag):
    """C, C (WALKER's menu), E (WALKER's End: SHARKMAN 2's turn, then TIMASALIN's menu), E (TIMASALIN's End: BALTHIR's
    pending Aim), N (SHARKMAN 2 under the cursor), then T: the tail."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "CC", None)])[0])
    out["second"] = cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "E", None)])[0]
    out["aim"] = cast_key(box, work, f"{tag}-aim", "E")
    out["n"] = cast_key(box, work, f"{tag}-n", "N")
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "T", None)])[0]
    return out


def mark_of_seed(p, seed):
    """The turn mark (NN) of eclplay's run P whose RandSeed (its battlefield file) is SEED, as cast_mark's dict, or None."""
    for path in sorted(p["tails"].glob("*.field")):
        if parse_field(path)[0] == seed:
            return dict(base=path.with_suffix(""), last=p["last"], number=path.stem)
    return None


def band_b2_ice(v, work, wild, opts):
    """Ice Storm on the party (time 1, CLW's keys): the draws at WALKER's menu and at the Aim, the tail (124 draws past the
    menu, 379f92a2), the four targets each 20 down, the byte forgotten and facing 0; eclplay's tail (turn mark 03)
    agrees with the original's on RandSeed, records, blocks and effect chains."""
    save = ice_save()
    o = cast_original(work, wild, save, "band-b2-ice", opts.verbose, cast_run_clw)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-b2-ice-save", save)
    nxt, aim, tail = o["next"], o["aim"], o["tail"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == CAST_CLW_NEXT, f"the original's RandSeed at WALKER's menu after C, C (Ice Storm's "
            f"Begins Casting) is {CAST_CLW_NEXT[0]} draws past BALTHIR's menu, at {CAST_CLW_NEXT[1]:#x}",
            f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == CAST_CLW_AIM, f"the original's RandSeed at BALTHIR's pending Ice Storm at its Aim "
            f"(after WALKER's End) is {CAST_CLW_AIM[0]} draws past the menu, at {CAST_CLW_AIM[1]:#x}",
            f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_B2_ICE_TAIL, f"the original's tail after T (Ice Storm on the party) is "
            f"{BAND_B2_ICE_TAIL[0]} draws past the menu, at {BAND_B2_ICE_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    hp = {k: (rec_byte(o["menu"]["records"][k], HIT_POINTS), rec_byte(tail["records"][k], HIT_POINTS)) for k in BAND_B2_ICE_HP}
    v.check(all(hp[k] == BAND_B2_ICE_HP[k] for k in BAND_B2_ICE_HP),
            "the four listed targets (BALTHIR, WALKER, TIMASALIN, ROLAND) each lose 20 hit points at the tail",
            " ".join(f"{k}: {a:#x} -> {b:#x}" for k, (a, b) in hp.items()))
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    v.check(forgotten == BAND_B2_ICE | CAST_FORGOTTEN and facing == 0,
            "at the tail BALTHIR's Ice Storm is forgotten (+0xce 0xd7) and he faces 0", f"+0xce {forgotten:#x}, facing {facing}")
    p = turn_port(work, wild, "band-b2-ice", "CCET", entry=entry)
    mark = cast_mark(p, "03")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's turn mark (03) after C, C, E and T", p["last"])
        return
    command_state(v, "Ice Storm's tail (the turn's tail, 03)", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (Ice Storm's tail, turn mark 03)")
    marked = ported_records(mark)
    v.check(all(rec_byte(marked[k], HIT_POINTS) == BAND_B2_ICE_HP[k][1] for k in BAND_B2_ICE_HP) and
            rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_B2_ICE | CAST_FORGOTTEN,
            "eclplay's tail after Ice Storm: the four targets down by 20, +0xce 0xd7",
            f"+0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")


def band_b2_flame(v, work, wild, opts):
    """Flame Strike on SHARKMAN 2 (time 2): the draws at TIMASALIN's menu and at BALTHIR's pending Aim, the tail (32 draws
    past the menu, 2bdca33e), SHARKMAN 2 halved by his save to 0x40, the byte forgotten. eclplay's turn mark is the one
    with the original's RandSeed (found, its number checked): its records, blocks and effect chains agree."""
    save = flame_save()
    o = cast_original(work, wild, save, "band-b2-flame", opts.verbose, cast_run_flame)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-b2-flame-save", save)
    nxt, second, aim, tail = o["next"], o["second"], o["aim"], o["tail"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == BAND_B2_FLAME_NEXT, f"the original's RandSeed at WALKER's menu after C, C (Flame "
            f"Strike) is {BAND_B2_FLAME_NEXT[0]} draws past the menu, at {BAND_B2_FLAME_NEXT[1]:#x}", f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, second["seed"])
    v.check((draws, second["seed"]) == BAND_B2_FLAME_MENU, f"the original's RandSeed at TIMASALIN's menu after WALKER's End "
            f"(SHARKMAN 2's turn between) is {BAND_B2_FLAME_MENU[0]} draws past the menu, at {BAND_B2_FLAME_MENU[1]:#x}",
            f"{draws} draws, {second['seed']:#x}")
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == BAND_B2_FLAME_AIM, f"the original's RandSeed at BALTHIR's pending Flame Strike at its "
            f"Aim (after TIMASALIN's End) is {BAND_B2_FLAME_AIM[0]} draws past the menu, at {BAND_B2_FLAME_AIM[1]:#x}",
            f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_B2_FLAME_TAIL, f"the original's tail after T (Flame Strike) is "
            f"{BAND_B2_FLAME_TAIL[0]} draws past the menu, at {BAND_B2_FLAME_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    hp = (rec_byte(o["menu"]["records"][CAST_SHARKMAN_2], HIT_POINTS), rec_byte(tail["records"][CAST_SHARKMAN_2], HIT_POINTS))
    v.check(hp == BAND_B2_FLAME_HP, f"SHARKMAN 2's hit points go from {BAND_B2_FLAME_HP[0]:#x} to {BAND_B2_FLAME_HP[1]:#x} "
            "at the tail (6d8 halved by his save)", f"{hp[0]:#x} -> {hp[1]:#x}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(forgotten == BAND_B2_FLAME | CAST_FORGOTTEN, "at the tail BALTHIR's Flame Strike is forgotten (+0xce 0xca)",
            f"+0xce {forgotten:#x}")
    p = turn_port(work, wild, "band-b2-flame", BAND_B2_FLAME_NAMES, entry=entry)
    mark = mark_of_seed(p, tail["seed"])
    if mark is None:
        v.check(False, f"eclplay has a turn mark whose RandSeed is the original's tail ({tail['seed']:#x}) after "
                f"{BAND_B2_FLAME_NAMES}", p["last"])
        return
    v.check(mark["number"] == BAND_B2_FLAME_MARK, f"the turn mark with the original's tail is {BAND_B2_FLAME_MARK} (the "
            "fifth turn: WALKER, SHARKMAN 2, TIMASALIN, then BALTHIR's pending Flame Strike)", mark["number"])
    command_state(v, f"Flame Strike's tail (the turn's tail, {mark['number']})", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), f" (Flame Strike's tail, turn mark {mark['number']})")
    marked = ported_records(mark)
    v.check(rec_byte(marked[CAST_SHARKMAN_2], HIT_POINTS) == BAND_B2_FLAME_HP[1] and
            rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_B2_FLAME | CAST_FORGOTTEN,
            f"eclplay's tail after Flame Strike: SHARKMAN 2 at {BAND_B2_FLAME_HP[1]:#x}, +0xce {BAND_B2_FLAME | CAST_FORGOTTEN:#x}",
            f"{rec_byte(marked[CAST_SHARKMAN_2], HIT_POINTS):#x}, +0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")
    print(f"eclplay (band b2, Flame Strike) ends {p['last']!r} at turn mark {mark['number']}", flush=True)


def band_b2(v, work, opts):
    """Band b2 (part 31b), the flow band_b2: Ice Storm on the party and Flame Strike on SHARKMAN 2 (the damage of queen_area.c,
    the flight and bursts of band b1), each against eclplay's turn mark (RandSeed, records, blocks, effect chains)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    band_b2_ice(v, work, wild, opts)
    band_b2_flame(v, work, wild, opts)


# Band b3 (step 4, part 31d, queen_area.c): Lightning Bolt (0x33, time 1) at SHARKMAN 2 on the open ground: the bolt's first hit
# on the spell's cell, its walk on from there, the caster behind BALTHIR out of line. Keys C, C (WALKER's menu: Begins Casting),
# E (WALKER's End: BALTHIR's pending Aim), N (SHARKMAN 2 under the cursor), then T: the tail (5045:066a). The draws at the
# menu and the Aim are CLW's (the picks draw nothing: the same keys). eclplay's turn mark 03 is the tail's.
BAND_B3_LIGHTNING = 0x33
BAND_B3_TAIL = (38, 0xA31F31D0)         # the tail after T: 38 draws past the menu
BAND_B3_HP = (0x4E, 0x42)               # SHARKMAN 2 before and at the tail: 2d6 = 12, not saved
BAND_B3_FACING = 3                      # BALTHIR's facing at the tail (at SHARKMAN 2)
BAND_B3_MARK = "03"                     # eclplay's turn mark of the tail (band b2's Ice Storm, with the same keys, is 03)


def lightning_save():
    """Saved game A with BALTHIR memorising Lightning Bolt (+0xce 0x33) and nothing else."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_B3_LIGHTNING}})


def cast_run_lightning(box, menu, work, tag):
    """C, C (Begins Casting: BALTHIR's turn ends at WALKER's menu), E (WALKER's End: BALTHIR's pending Aim), N (SHARKMAN 2 under
    the cursor), then T: the tail."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "CC", None)])[0])
    out["aim"] = cast_key(box, work, f"{tag}-aim", "E")
    out["n"] = cast_key(box, work, f"{tag}-n", "N")
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "T", None)])[0]
    return out


def band_b3(v, work, opts):
    """Band b3 (part 31d), the flow band_b3: Lightning Bolt at SHARKMAN 2 (queen_bolt's walk and hits, the Lightning's
    dice): the original's RandSeed at the tail (38 draws past the menu, a31f31d0), SHARKMAN 2 from 0x4e to 0x42 (2d6, not
    saved), BALTHIR's byte forgotten and facing 3; eclplay's turn mark 03 agrees with the original's tail on RandSeed,
    records, blocks and effect chains."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save = lightning_save()
    o = cast_original(work, wild, save, "band-b3", opts.verbose, cast_run_lightning)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-b3-save", save)
    nxt, aim, n, tail = o["next"], o["aim"], o["n"], o["tail"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == CAST_CLW_NEXT, f"the original's RandSeed at WALKER's menu after C, C is {CAST_CLW_NEXT[0]} "
            f"draws past BALTHIR's menu, at {CAST_CLW_NEXT[1]:#x}", f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == CAST_CLW_AIM, f"the original's RandSeed at BALTHIR's pending Lightning Bolt at its Aim "
            f"(after WALKER's End) is {CAST_CLW_AIM[0]} draws past the menu, at {CAST_CLW_AIM[1]:#x}", f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(aim["seed"], n["seed"])
    v.check(draws == 0, "the original's RandSeed after N (SHARKMAN 2 under the cursor) is unchanged", f"{draws} draws")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_B3_TAIL, f"the original's tail after T (Lightning Bolt) is {BAND_B3_TAIL[0]} draws past "
            f"the menu, at {BAND_B3_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    hp = (rec_byte(o["menu"]["records"][CAST_SHARKMAN_2], HIT_POINTS), rec_byte(tail["records"][CAST_SHARKMAN_2], HIT_POINTS))
    v.check(hp == BAND_B3_HP, f"SHARKMAN 2's hit points go from {BAND_B3_HP[0]:#x} to {BAND_B3_HP[1]:#x} at the tail (2d6, "
            "not saved)", f"{hp[0]:#x} -> {hp[1]:#x}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    v.check(forgotten == BAND_B3_LIGHTNING | CAST_FORGOTTEN and facing == BAND_B3_FACING,
            f"at the tail BALTHIR's Lightning Bolt is forgotten (+0xce 0xb3) and he faces {BAND_B3_FACING}",
            f"+0xce {forgotten:#x}, facing {facing}")
    p = turn_port(work, wild, "band-b3", "CCENT", entry=entry)
    mark = cast_mark(p, BAND_B3_MARK)
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, f"eclplay writes BALTHIR's turn mark ({BAND_B3_MARK}) after C, C, E, N and T", p["last"])
        return
    command_state(v, f"Lightning Bolt's tail (the turn's tail, {BAND_B3_MARK})", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), f" (Lightning Bolt's tail, turn mark {BAND_B3_MARK})")
    marked = ported_records(mark)
    v.check(rec_byte(marked[CAST_SHARKMAN_2], HIT_POINTS) == BAND_B3_HP[1] and
            rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_B3_LIGHTNING | CAST_FORGOTTEN,
            f"eclplay's tail after Lightning Bolt: SHARKMAN 2 at {BAND_B3_HP[1]:#x}, +0xce {BAND_B3_LIGHTNING | CAST_FORGOTTEN:#x}",
            f"{rec_byte(marked[CAST_SHARKMAN_2], HIT_POINTS):#x}, +0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")
    print(f"eclplay (band b3) ends {p['last']!r} at turn mark {BAND_B3_MARK}", flush=True)


# Band b4's cone (step 4, part 31c: queen_area.c's queen_cone and Cone of Cold, 7fd8:299c). Saved game A with BALTHIR
# memorising Cone of Cold (+0xce 0x5c, time 0) and his class 5 level at 9 (+0xaa 9: the mage's level, a cone of length
# 5; the poke changes no draw before the Aim): C, C (Begins Casting: WALKER's menu, 2 draws), E (WALKER's End: his pending
# Cone of Cold at its Aim, 3 draws), N (SHARKMAN 2 under the cursor, no draw), then T: the tail.
BAND_B4_COLD = 0x5C
BAND_B4_LEVEL = 0xAA                    # BALTHIR's class 5 level (the mage's: 0xa5 + 5)
BAND_B4_MAGE = 9                        # the level poked: a cone of length 5 (the walk to 2 * 5 = 10)
BAND_B4_TAIL = (30, 0x6EACBBD8)         # the tail after T: 30 draws past the menu (the Aim's 3 first)
BAND_B4_CELL = (32, 18)                 # the spell's cell (DS:bd2c, bd49: SHARKMAN 2's)
BAND_B4_FACING = 3                      # BALTHIR's facing after the cone (at SHARKMAN 2, as MM's)
BAND_B4_HP = 0x3D                       # SHARKMAN 2's hit points at the tail (0x4e, 9d4 + 9 halved: 17 off)


def cone_save():
    """Saved game A with BALTHIR memorising Cone of Cold (+0xce 0x5c) at mage level 9 (+0xaa 9) and nothing else."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_B4_COLD, BAND_B4_LEVEL: BAND_B4_MAGE}})


def band_b4(v, work, opts):
    """Band b4's cone (part 31c), the flow band_b4: Cone of Cold at SHARKMAN 2 (32, 18) by BALTHIR at mage level 9, on the
    original's tail after T (the cone's dice, the save, the say's wait) and eclplay's turn mark 03. The original: RandSeed
    at WALKER's menu after C, C (CAST_CLW_NEXT), at the pending Cone of Cold's Aim after E (CAST_CLW_AIM), and at the tail
    30 draws past the menu, RandSeed 6eacbbd8; DS:c6f4 1, the cell 32, 18, SHARKMAN 2 (record 7) from 0x4e to 0x3d (a
    half on its save), BALTHIR's +0xce 0xdc and facing 3. eclplay runs the same keys: the Aim's screen (row 23 "Spell:
    Cone of Cold") compared, and its turn mark 03 (the tail) agrees on RandSeed, records, blocks, effect chains."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save = cone_save()
    o = cast_original(work, wild, save, "band-b4", opts.verbose, band_a1_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-b4-save", save)
    nxt, aim, tail = o["next"], o["aim"], o["tail"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == CAST_CLW_NEXT, f"the original's RandSeed at WALKER's menu after C, C (Cone of Cold: "
            f"Begins Casting) is {CAST_CLW_NEXT[0]} draws past BALTHIR's menu, at {CAST_CLW_NEXT[1]:#x}",
            f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == CAST_CLW_AIM, f"the original's RandSeed at BALTHIR's pending Cone of Cold at its Aim "
            f"(after WALKER's End) is {CAST_CLW_AIM[0]} draws past BALTHIR's menu, at {CAST_CLW_AIM[1]:#x}",
            f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_B4_TAIL, f"the original's tail after T (Cone of Cold) is {BAND_B4_TAIL[0]} draws "
            f"past BALTHIR's menu, at {BAND_B4_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    v.check(L.draws(aim["seed"], o["n"]["seed"]) == 0, "the original's RandSeed after N (SHARKMAN 2 under the cursor) is "
            "unchanged", f"{L.draws(aim['seed'], o['n']['seed'])} draws")
    v.check(tail["cell"] == BAND_B4_CELL and tail["targets"] == 1, f"at the tail the cone's cell is {BAND_B4_CELL} and "
            f"DS:c6f4 is 1", f"cell {tail['cell']}, DS:c6f4 {tail['targets']}")
    hp = rec_byte(tail["records"][CAST_AUTO_SHARKMAN], HIT_POINTS)
    v.check(hp == BAND_B4_HP, f"at the tail SHARKMAN 2 (record 7) is at {BAND_B4_HP:#x} (halved on its save)", f"{hp:#x}")
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(forgotten == BAND_B4_COLD | CAST_FORGOTTEN and facing == BAND_B4_FACING,
            "at the tail BALTHIR's Cone of Cold is forgotten (+0xce 0xdc) and he faces 3", f"+0xce {forgotten:#x}, facing {facing}")
    p_aim = turn_port(work, wild, "band-b4-aim", "CCE", entry=entry)
    cast_screen(v, work, "BALTHIR's pending Cone of Cold at its Aim (row 23 Spell:Cone of Cold)", aim["screen"], p_aim,
                SHIELD_POINTER)
    command_state(v, "BALTHIR's pending Cone of Cold at its Aim", aim, p_aim, nxt["seed"])
    p = turn_port(work, wild, "band-b4", "CCENT", entry=entry)
    mark = cast_mark(p, "03")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's turn mark (03) after C, C, E, N and T", p["last"])
        return
    command_state(v, "Cone of Cold's tail (the turn's tail, 03)", tail, mark, aim["seed"])
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (Cone of Cold's tail, turn mark 03)")
    marked = ported_records(mark)
    v.check(rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_B4_COLD | CAST_FORGOTTEN and
            rec_byte(marked[CAST_AUTO_SHARKMAN], HIT_POINTS) == BAND_B4_HP,
            "eclplay's tail after Cone of Cold: BALTHIR's Cone of Cold forgotten (+0xce 0xdc), SHARKMAN 2 at 0x3d",
            f"+0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")
    print(f"eclplay (band b4) turn mark 03 after {p['last']!r}", flush=True)


# Band b5's Meteor Swarm (step 4, part 31e: 7fd8:3b99 and its try 3a73, queen_area.c). Saved game A with BALTHIR
# memorising Meteor Swarm (+0xce 0x7c, time 3) and nothing else: C, C (Begins Casting: WALKER's menu, 2 draws), E (WALKER's
# End: SHARKMAN 2's turn, then TIMASALIN's menu), E (TIMASALIN's End: CINDER's menu), E (CINDER's End: BALTHIR's pending
# Meteor Swarm at its Aim, no menu), N (no target in range: the cursor stays on BALTHIR, no draw), then T: the four meteors
# from his own cell (29, 14), and the tail.
BAND_B5_METEOR = 0x7C
BAND_B5_TIMASALIN = 1                   # the record of TIMASALIN (downed by the meteors: status 4)
BAND_B5_CINDER = 2                      # the record of CINDER (no hit)
BAND_B5_SHARKMAN_1 = 6                  # the record of SHARKMAN 1 (no hit)
BAND_B5_TAIL = (358, 0xA1AD7490)        # the tail after T: 358 draws past the menu
BAND_B5_CELL = (30, 15)                 # the spell's cell at the tail: the fourth meteor's (DS:bd2c, bd49)
BAND_B5_TARGETS = 2                     # DS:c6f4 at the tail: WALKER and BALTHIR, the fourth meteor's list (repeats kept)
BAND_B5_HP = {                          # the hit points before and at the tail, by record: ROLAND (0), TIMASALIN (1), BALTHIR
    0: (0x86, 0x6F),                    # (3), ROARK (4), WALKER (5); CINDER and the SHARKMEN (2, 6, 7) are untouched
    1: (0x3D, 0),
    3: (0x6A, 0x2B),
    4: (0x62, 0x3D),
    5: (0x45, 0x1E),                    # WALKER: 0x40 after SHARKMAN 2's hit in its turn (0x45 at the menu)
}
BAND_B5_NAMES = "CCEEENT"              # the keys of the meteor flow (eclplay)
BAND_B5_MARK = "06"                     # eclplay's turn mark of the tail (the marks 01 to 05: the turns before it)


def meteor_save():
    """Saved game A with BALTHIR memorising Meteor Swarm (+0xce 0x7c) and nothing else."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: BAND_B5_METEOR}})


def cast_run_meteor(box, menu, work, tag):
    """C, C (WALKER's menu), E (WALKER's End: SHARKMAN 2's turn, then TIMASALIN's menu), E (TIMASALIN's End: CINDER's menu),
    E (CINDER's End: BALTHIR's pending Aim), N (SHARKMAN 2 is not in range: the cursor stays on him), then T: the tail."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "CC", None)])[0])
    out["second"] = cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "E", None)])[0]
    out["third"] = cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "E", None)])[0]
    out["aim"] = cast_key(box, work, f"{tag}-aim", "E")
    out["n"] = cast_key(box, work, f"{tag}-n", "N")
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "T", None)])[0]
    return out


def band_b5(v, work, opts):
    """Band b5's Meteor Swarm (part 31e), the flow band_b5: the four meteors at BALTHIR's own cell (his pending Aim at 29,
    14), on the original's tail after T (the line's and the area's dice, the saves, the flashes) and eclplay's turn mark 06.
    The original: RandSeed at WALKER's menu after C, C (CAST_CLW_NEXT), at TIMASALIN's and at CINDER's menus (16 draws
    each, from the first: BAND_B2_FLAME_MENU), at BALTHIR's pending Aim (BAND_B2_FLAME_AIM), and at the tail 358 draws past
    the menu, RandSeed a1ad7490; the cell 30, 15 (the fourth meteor's), DS:c6f4 2, the hit points of BAND_B5_HP, TIMASALIN
    downed (status 4), BALTHIR's +0xce 0xfc. eclplay runs the same keys: its Aim's screen and its turn mark (the one with the
    original's RandSeed) agree on RandSeed, records, combat blocks and effect chains."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save = meteor_save()
    o = cast_original(work, wild, save, "band-b5", opts.verbose, cast_run_meteor)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-b5-save", save)
    nxt, second, third, aim, tail = o["next"], o["second"], o["third"], o["aim"], o["tail"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == CAST_CLW_NEXT, f"the original's RandSeed at WALKER's menu after C, C (Meteor Swarm: "
            f"Begins Casting) is {CAST_CLW_NEXT[0]} draws past BALTHIR's menu, at {CAST_CLW_NEXT[1]:#x}",
            f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, second["seed"])
    v.check((draws, second["seed"]) == BAND_B2_FLAME_MENU, f"the original's RandSeed at TIMASALIN's menu after WALKER's End "
            f"(SHARKMAN 2's turn between) is {BAND_B2_FLAME_MENU[0]} draws past the menu, at {BAND_B2_FLAME_MENU[1]:#x}",
            f"{draws} draws, {second['seed']:#x}")
    draws = L.draws(m0, third["seed"])
    v.check(draws == BAND_B2_FLAME_MENU[0], f"the original's RandSeed at CINDER's menu after TIMASALIN's End is "
            f"{BAND_B2_FLAME_MENU[0]} draws past the menu (none drawn by his End)", f"{draws} draws")
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == BAND_B2_FLAME_AIM, f"the original's RandSeed at BALTHIR's pending Meteor Swarm at its "
            f"Aim (after CINDER's End) is {BAND_B2_FLAME_AIM[0]} draws past the menu, at {BAND_B2_FLAME_AIM[1]:#x}",
            f"{draws} draws, {aim['seed']:#x}")
    v.check(L.draws(aim["seed"], o["n"]["seed"]) == 0, "the original's RandSeed after N (no target in range) is unchanged",
            f"{L.draws(aim['seed'], o['n']['seed'])} draws")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_B5_TAIL, f"the original's tail after T (Meteor Swarm) is {BAND_B5_TAIL[0]} draws "
            f"past BALTHIR's menu, at {BAND_B5_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    v.check(tail["cell"] == BAND_B5_CELL and tail["targets"] == BAND_B5_TARGETS, f"at the tail the last meteor's cell is "
            f"{BAND_B5_CELL} and DS:c6f4 is {BAND_B5_TARGETS}", f"cell {tail['cell']}, DS:c6f4 {tail['targets']}")
    for record, (before, after) in BAND_B5_HP.items():
        was = rec_byte(o["menu"]["records"][record], HIT_POINTS)
        now = rec_byte(tail["records"][record], HIT_POINTS)
        v.check((was, now) == (before, after), f"record {record}'s hit points go from {before:#x} to {after:#x} by the meteors",
                f"{was:#x} -> {now:#x}")
    v.check(rec_byte(tail["records"][BAND_B5_TIMASALIN], STATUS_AT) == 4, "TIMASALIN is downed (status 4) at the tail",
            f"status {rec_byte(tail['records'][BAND_B5_TIMASALIN], STATUS_AT)}")
    untouched = [BAND_B5_CINDER, BAND_B5_SHARKMAN_1, CAST_SHARKMAN_2]
    v.check(all(rec_byte(o["menu"]["records"][i], HIT_POINTS) == rec_byte(tail["records"][i], HIT_POINTS) for i in untouched),
            "CINDER and both SHARKMEN are untouched at the tail", "")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(forgotten == BAND_B5_METEOR | CAST_FORGOTTEN, "at the tail BALTHIR's Meteor Swarm is forgotten (+0xce 0xfc)",
            f"+0xce {forgotten:#x}")
    p_aim = turn_port(work, wild, "band-b5-aim", "CCEEE", entry=entry)
    cast_screen(v, work, "BALTHIR's pending Meteor Swarm at its Aim (row 23 Spell:Meteor Swarm)", aim["screen"], p_aim,
                SHIELD_POINTER)
    command_state(v, "BALTHIR's pending Meteor Swarm at its Aim", aim, p_aim, third["seed"])
    p = turn_port(work, wild, "band-b5", BAND_B5_NAMES, entry=entry)
    mark = mark_of_seed(p, tail["seed"])
    if mark is None:
        v.check(False, f"eclplay has a turn mark whose RandSeed is the original's tail ({tail['seed']:#x}) after "
                f"{BAND_B5_NAMES}", p["last"])
        return
    v.check(mark["number"] == BAND_B5_MARK, f"the turn mark with the original's tail is {BAND_B5_MARK} (the sixth turn: "
            "BALTHIR, WALKER, SHARKMAN 2, TIMASALIN, CINDER, then BALTHIR's pending Meteor Swarm)", mark["number"])
    command_state(v, f"Meteor Swarm's tail (the turn's tail, {mark['number']})", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), f" (Meteor Swarm's tail, turn mark {mark['number']})")
    marked = ported_records(mark)
    got = {record: rec_byte(marked[record], HIT_POINTS) for record in BAND_B5_HP}
    want = {record: after for record, (_, after) in BAND_B5_HP.items()}
    v.check(got == want and rec_byte(marked[CAST_BALTHIR], CAST_MEMO) == BAND_B5_METEOR | CAST_FORGOTTEN,
            "eclplay's tail after Meteor Swarm: the hit points of the records above, BALTHIR's +0xce 0xfc",
            f"{got}, +0xce {rec_byte(marked[CAST_BALTHIR], CAST_MEMO):#x}")
    print(f"eclplay (band b5, Meteor Swarm) ends {p['last']!r} at turn mark {mark['number']}", flush=True)


def commands(v, work, patched, opts):
    """The command menu's commands from the first turn's menu: Quick, Alt-Q, Alt-M and Speed (S, F,
    E) (step 4, part 22b), then Alt-X without the Helm and Bandage (its leftovers), each run against
    eclplay's stop after its keys; Yell and View (the same leftovers) last."""
    quick_check(v, work, patched, opts)
    alt_q_check(v, work, patched, opts)
    alt_m_check(v, work, patched, opts)
    speed_check(v, work, patched, opts)
    alt_x_check(v, work, patched, opts)
    bandage_check(v, work, patched, opts)
    yell_check(v, work, patched, opts)
    view_check(v, work, patched, opts)


# Band c1's holds and charms (step 4, part 32a, queen_band_c.c: 849a:1e0d's loop and 7fd8's 022a, 2b3a, 0813, 06e9 and 2396).
# Two runs, BALTHIR memorising one spell each (+0xce, no other memo): Charm Person (0x0a, time 1: the Aim at once) by band
# b1's keys, C, C, N (SHARKMAN 2 under the cursor), then T: SHARKMAN 2 saves (d20 9 against its save 6), "is Unaffected",
# no node; BALTHIR's Charm Person forgotten (+0xce 0x8a), facing 3. Hold Person (0x17, time 1, one pick): C, C (WALKER's
# menu), E (WALKER's End: his pending Aim), \> (Manual one east, onto WALKER), T (the first pick), then E (Exit the second):
# WALKER fails (d20 3) and holds ("is held": node [0x34, 14, 10, 0]); BALTHIR's Hold Person forgotten (+0xce 0x97).
# The handler's dispatch (849a:21b5) is not armed: arm() spins the 5045 overlay's copies only, so the Aims (before the
# flight) and the tails stand for it. eclplay's turn mark at each original's tail agrees on RandSeed, records, blocks and
# effect chains (the marks' numbers differ: mark_of_seed finds the one whose RandSeed is the original's).
BAND_C1_CHARM = 0x0A
BAND_C1_HOLD = 0x17
BAND_C1_AIM = (1, 0xEB831397)           # C, C, N: the charm's Aim at once, one draw past the menu, N none
BAND_C1_CHARM_TAIL = (15, 0xE3A9DB89)   # T: the tail, 15 draws past the menu (the dispatch's 13 and the flight's)
BAND_C1_CHARM_FACING = 3
BAND_C1_HOLD_AIM = CAST_CLW_AIM         # C, C, E: the hold's pending Aim at WALKER's End, 3 draws past the menu
BAND_C1_HOLD_TAIL = (27, 0x719026AD)    # the tail after E: 27 draws past the menu (the Aim's 3 and the holds' d20 and flashes)
BAND_C1_HOLD_NODE = [0x34, 14, 10, 0]   # WALKER's node after the hold: effect 0x34 (HELD), 14 minutes, value 10, flag 0
BAND_C1_HOLD_KEYS = "CCE\\>TE"         # the keys of the hold flow (eclplay)
BAND_C1_CHARM_KEYS = "CCNT"              # the keys of the charm flow (eclplay)


def band_c1_save(spell):
    """Saved game A with BALTHIR memorising SPELL (+0xce) and nothing else."""
    return dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: spell}})


def band_c1_hold_run(box, menu, work, tag):
    """C, C (WALKER's menu, BALTHIR's turn ends), E (WALKER's End: BALTHIR's pending Aim), \\> (Manual one east, onto WALKER),
    T (the first pick), then E (Exit the second pick): the tail (5045:066a)."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "CC", None)])[0])
    out["aim"] = cast_key(box, work, f"{tag}-aim", "E")
    out["manual"] = cast_key(box, work, f"{tag}-manual", "\\>")
    out["pick"] = cast_key(box, work, f"{tag}-pick", "T")
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "E", None)])[0]
    return out


def band_c1_charm(v, work, wild, opts):
    """Charm Person on SHARKMAN 2 (band b1's keys): the Aim, the tail (15 draws), SHARKMAN 2 with no charm node, BALTHIR's byte
    forgotten (0x8a) and facing 3; eclplay's tail (turn mark 01) agrees."""
    save = band_c1_save(BAND_C1_CHARM)
    o = cast_original(work, wild, save, "band-c1-charm", opts.verbose, band_b1_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-c1-charm-save", save)
    aim, n, tail = o["aim"], o["n"], o["tail"]
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == BAND_C1_AIM, f"the original's RandSeed at Charm Person's Aim (C, C) is "
            f"{BAND_C1_AIM[0]} draw past BALTHIR's menu, at {BAND_C1_AIM[1]:#x}", f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(aim["seed"], n["seed"])
    v.check(draws == 0, "the original's RandSeed after N (SHARKMAN 2 under the cursor) is unchanged", f"{draws} draws")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_C1_CHARM_TAIL, f"the original's RandSeed at the tail after T is "
            f"{BAND_C1_CHARM_TAIL[0]} draws past the menu, at {BAND_C1_CHARM_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    charmed = any(node[0] == 0x0B for node in tail["chains"][CAST_SHARKMAN_2])
    v.check(not charmed, "at the tail SHARKMAN 2 holds no charm (d20 9 saves him: \"is Unaffected\", no node)",
            f"nodes {tail['chains'][CAST_SHARKMAN_2]}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    facing = rec_byte(tail["blocks"][CAST_BALTHIR], COMBAT_BLOCK_FACING)
    v.check(forgotten == BAND_C1_CHARM | CAST_FORGOTTEN and facing == BAND_C1_CHARM_FACING,
            f"at the tail BALTHIR's Charm Person is forgotten (+0xce 0x8a) and he faces {BAND_C1_CHARM_FACING}",
            f"+0xce {forgotten:#x}, facing {facing}")
    p = turn_port(work, wild, "band-c1-charm", BAND_C1_CHARM_KEYS, entry=entry)
    mark = cast_mark(p, "01")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's turn mark (01) after C, C, N and T", p["last"])
        return
    command_state(v, "Charm Person's tail (the turn's tail, 01)", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (Charm Person's tail, turn mark 01)")
    print(f"eclplay (band c1, charm) ends {p['last']!r}", flush=True)


def band_c1_hold(v, work, wild, opts):
    """Hold Person on WALKER (the first pick, one target; d20 3 fails): the pending Aim after C, C, E, the tail after E
    (27 draws), WALKER's node [0x34, 14, 10, 0] and BALTHIR's byte 0x97; eclplay's turn mark at the same RandSeed agrees."""
    save = band_c1_save(BAND_C1_HOLD)
    o = cast_original(work, wild, save, "band-c1-hold", opts.verbose, band_c1_hold_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "band-c1-hold-save", save)
    aim, tail = o["aim"], o["tail"]
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == BAND_C1_HOLD_AIM, f"the original's RandSeed at Hold Person's pending Aim (C, C, E) is "
            f"{BAND_C1_HOLD_AIM[0]} draws past BALTHIR's menu, at {BAND_C1_HOLD_AIM[1]:#x}", f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == BAND_C1_HOLD_TAIL, f"the original's RandSeed at the tail after E is "
            f"{BAND_C1_HOLD_TAIL[0]} draws past the menu, at {BAND_C1_HOLD_TAIL[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    walker = [list(node) for node in tail["chains"][CAST_WALKER]]
    v.check(BAND_C1_HOLD_NODE in walker, f"at the tail WALKER holds Hold Person's node {BAND_C1_HOLD_NODE} (d20 3 fails it)",
            f"nodes {walker}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(forgotten == BAND_C1_HOLD | CAST_FORGOTTEN, f"at the tail BALTHIR's Hold Person is forgotten (+0xce 0x97)",
            f"+0xce {forgotten:#x}")
    p = turn_port(work, wild, "band-c1-hold", BAND_C1_HOLD_KEYS, entry=entry)
    mark = mark_of_seed(p, tail["seed"])
    if mark is None:
        v.check(False, "eclplay writes a turn mark at the original's tail after C, C, E, \\>, T and E", p["last"])
        return
    command_state(v, "Hold Person's tail (the turn's tail)", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), f" (Hold Person's tail, turn mark {mark['number']})")
    print(f"eclplay (band c1, hold) turn mark {mark['number']} after {p['last']!r}", flush=True)


def band_c1(v, work, opts):
    """Band c1 (step 4, part 32a): Charm Person (0x0a) on SHARKMAN 2 and Hold Person (0x17) on WALKER, on BALTHIR's own casts
    against eclplay's turns (the flow band_c1)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    band_c1_charm(v, work, wild, opts)
    band_c1_hold(v, work, wild, opts)


# Effects band a's melee (step 4, part 34a, effects-1): BALTHIR's node 1 (his 0x69) rewritten to one of the band's
# effects, with no cast, and SHARKMAN 2 melees him. E at BALTHIR's menu (WALKER's menu, no draws), then E at WALKER's
# (SHARKMAN 2's turn, then TIMASALIN's menu). With 0x69 kept (the base) TIMASALIN's menu is 13 draws past the menu, RandSeed
# 949f681b, and SHARKMAN 2 hits BALTHIR 106 -> 101. Invisibility (0x19, events 1 and 0x10) makes SHARKMAN 2 unable to target
# BALTHIR: he hits WALKER (69 -> 59) instead, 17 draws, RandSeed f79afec7. Cause Blindness (0x21, events 0x0a to 0x0c) makes
# BALTHIR 4 worse in AC: one more hit (106 -> 97), 15 draws, RandSeed e3a9db89. eclplay's turn mark is the one with the
# original's RandSeed at TIMASALIN's menu (its records, blocks and effect chains agree there).
EFFECTS_1_NEXT = (0, 0xCE7F005E)        # WALKER's menu after BALTHIR's E: no draw
EFFECTS_1_NAMES = "EE"                  # the keys of the flow (eclplay)
EFFECTS_1_CASES = {
    "invisible": (0x19, (17, 0xF79AFEC7), {CAST_BALTHIR: (0x6A, 0x6A), CAST_WALKER: (0x45, 0x3B)}),
    "blind": (0x21, (15, 0xE3A9DB89), {CAST_BALTHIR: (0x6A, 0x61)}),
}


def effects_1_run(box, menu, work, tag):
    """E at BALTHIR's menu (WALKER's menu: its trap held, no draws), then E at WALKER's (SHARKMAN 2's turn, then TIMASALIN's
    menu). One MENU trap at a time (cast_traps arms the next after the first is given back)."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "E", None)])[0])
    out["second"] = cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "E", None)])[0]
    return out


def effects_1_case(v, work, wild, opts, name, effect, want, hp):
    """One effect on BALTHIR's node 1: the original's draws and RandSeed at TIMASALIN's menu, the hit points listed, and
    eclplay's turn mark at the same RandSeed (command_state, chains_agree)."""
    save = dqk_modify.modified({CAST_BALTHIR: {1: (effect, 0, 0xFF, 0)}}, {})
    o = cast_original(work, wild, save, f"effects-1-{name}", opts.verbose, effects_1_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / f"effects-1-{name}-save", save)
    nxt, second = o["next"], o["second"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == EFFECTS_1_NEXT, f"the original's RandSeed at WALKER's menu after E (BALTHIR's turn) is "
            f"{EFFECTS_1_NEXT[0]} draws past the menu, at {EFFECTS_1_NEXT[1]:#x}", f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, second["seed"])
    v.check((draws, second["seed"]) == want, f"the original's RandSeed at TIMASALIN's menu after E (SHARKMAN 2's melee, {name}) "
            f"is {want[0]} draws past the menu, at {want[1]:#x}", f"{draws} draws, {second['seed']:#x}")
    got = {k: (rec_byte(o["menu"]["records"][k], HIT_POINTS), rec_byte(second["records"][k], HIT_POINTS)) for k in hp}
    v.check(all(got[k] == hp[k] for k in hp), "the listed hit points after the melee ("
            + ", ".join(f"record {k}: {a:#x} -> {b:#x}" for k, (a, b) in hp.items()) + ")",
            " ".join(f"{k}: {a:#x} -> {b:#x}" for k, (a, b) in got.items()))
    p = turn_port(work, wild, f"effects-1-{name}", EFFECTS_1_NAMES, entry=entry)
    mark = mark_of_seed(p, second["seed"])
    if mark is None:
        v.check(False, f"eclplay has a turn mark whose RandSeed is the original's at TIMASALIN's menu ({want[1]:#x}) after "
                f"{EFFECTS_1_NAMES}", p["last"])
        return
    command_state(v, f"the melee ({name}: TIMASALIN's menu, turn mark {mark['number']})", second, mark, m0)
    chains_agree(v, second["chains"], mark["base"].with_suffix(".effects"), f" ({name}, turn mark {mark['number']})")
    print(f"eclplay (effects 1, {name}) ends {p['last']!r} at turn mark {mark['number']}", flush=True)


def effects_1(v, work, opts):
    """Band a's effects-1 (part 34a), the flow effects_1: the melee at BALTHIR with his node 1 rewritten to Invisibility (0x19)
    and to Cause Blindness (0x21), each against eclplay's turn mark (RandSeed, records, blocks, effect chains)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    for name, (effect, want, hp) in EFFECTS_1_CASES.items():
        effects_1_case(v, work, wild, opts, name, effect, want, hp)


# Effects-2 (step 4, part 34b): Burning Hands on himself (level 2, fire, no save) with BALTHIR's node 1 rewritten to Resist Fire
# (0x14: the fire halved, 2 to 1), to Globe of Invulnerability (0x39: the magic type cancelled, no damage) and kept (0x69, the
# base: 2). C, C (the Aim at once, one draw), then T (himself): the tail. The tail is 6 draws past the menu with 0x14 and with 0x69
# (RandSeed 35e90ff0), and 1 with 0x39 (the Aim's draw, eb831397). BALTHIR's hit points 0x6a at the tail with 0x39, 0x69 with 0x14,
# 0x68 with 0x69. The spell is forgotten (+0xce 0x89) in each. eclplay's turn mark 01 agrees with the original's tail.
EFFECTS_2_SPELL = 0x09                  # Burning Hands (+0xce)
EFFECTS_2_HP = 0x6A                     # BALTHIR's hit points before the spell
EFFECTS_2_AIM = (1, 0xEB831397)         # C, C: the Aim at once, one draw past the menu
EFFECTS_2_CASES = {
    "resist_fire": (0x14, (6, 0x35E90FF0), 0x69),
    "globe": (0x39, (1, 0xEB831397), EFFECTS_2_HP),
    "kept": (0x69, (6, 0x35E90FF0), 0x68),
}


def effects_2_run(box, menu, work, tag):
    """C, C (the Aim at once: Burning Hands' Aim, one draw), then T (himself): the tail."""
    out = dict(aim=cast_key(box, work, f"{tag}-aim", "CC"))
    out["tail"] = cast_traps(box, menu["handle"], [(MOVE_TAIL, "5045:066a", "T", None)])[0]
    return out


def effects_2_case(v, work, wild, opts, name, effect, want, hp):
    """One effect on BALTHIR's node 1: the original's draws and RandSeed at the Aim and at the tail, the hit points and the
    forgotten spell, and eclplay's turn mark 01 (RandSeed, records, blocks, effect chains) against the tail."""
    save = dqk_modify.modified({CAST_BALTHIR: {1: (effect, 0, 0xFF, 0)}}, {CAST_BALTHIR: {CAST_MEMO: EFFECTS_2_SPELL}})
    o = cast_original(work, wild, save, f"effects-2-{name}", opts.verbose, effects_2_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / f"effects-2-{name}-save", save)
    aim, tail = o["aim"], o["tail"]
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == EFFECTS_2_AIM, f"the original's RandSeed at Burning Hands' Aim (C, C) is "
            f"{EFFECTS_2_AIM[0]} draw past BALTHIR's menu, at {EFFECTS_2_AIM[1]:#x}", f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == want, f"the original's tail after T (Burning Hands on himself, node 1 {effect:#x}) is "
            f"{want[0]} draws past BALTHIR's menu, at {want[1]:#x}", f"{draws} draws, {tail['seed']:#x}")
    got = (rec_byte(aim["records"][CAST_BALTHIR], HIT_POINTS), rec_byte(tail["records"][CAST_BALTHIR], HIT_POINTS))
    v.check(got == (EFFECTS_2_HP, hp), f"BALTHIR's hit points (+0x198) at the tail are {hp:#x} (node 1 {effect:#x})",
            f"{got[0]:#x} -> {got[1]:#x}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(forgotten == EFFECTS_2_SPELL | CAST_FORGOTTEN, f"at the tail BALTHIR's Burning Hands is forgotten (+0xce "
            f"{EFFECTS_2_SPELL | CAST_FORGOTTEN:#x})", f"+0xce {forgotten:#x}")
    p = turn_port(work, wild, f"effects-2-{name}", "CCT", entry=entry)
    mark = cast_mark(p, "01")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, f"eclplay writes BALTHIR's turn mark (01) after C, C and T ({name})", p["last"])
        return
    command_state(v, f"Burning Hands' tail ({name}: turn mark 01)", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), f" (Burning Hands' tail, {name}, turn mark 01)")
    print(f"eclplay (effects 2, {name}) ends {p['last']!r}", flush=True)


def effects_2(v, work, opts):
    """Effects-2 (step 4, part 34b), the flow effects_2: Burning Hands on himself with his node 1 rewritten to Resist Fire (0x14),
    to Globe of Invulnerability (0x39) and kept (0x69, the base), each against eclplay's turn mark 01."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    for name, (effect, want, hp) in EFFECTS_2_CASES.items():
        effects_2_case(v, work, wild, opts, name, effect, want, hp)


# Effects-3 (step 4, part 34c): BALTHIR's node 1 rewritten to Mirror Image (0x1c, value 0x62: three images, level 2). Burning
# Hands on himself: the tail is 3 draws past the menu (the Aim's, the d4 of 3 and the say's wait), RandSeed 8a1edda5; one image
# is lost (his node's value 0x42) and BALTHIR's hit points stay 0x6a. The melee (E, E, no memorised change): 19 draws past the
# menu, RandSeed ecf5f155; both blows are taken by images (value 0x22, hit points 0x6a). eclplay's turn mark agrees.
EFFECTS_3_NODE = (0x1C, 0, 0x62, 0)     # BALTHIR's node 1: Mirror Image, minutes 0, value 0x62, flag 0
EFFECTS_3_TAIL = (3, 0x8A1EDDA5)        # Burning Hands' tail on himself, past his menu
EFFECTS_3_MELEE = (19, 0xECF5F155)      # the melee, TIMASALIN's menu past BALTHIR's


def effects_3_bh(v, work, wild, opts):
    """Burning Hands on himself with his node 1 at Mirror Image: the Aim's draw, the tail's draws and RandSeed, BALTHIR's
    hit points, his node 0x1c (value 0x42), the spell forgotten, and eclplay's turn mark 01 against the tail."""
    save = dqk_modify.modified({CAST_BALTHIR: {1: EFFECTS_3_NODE}}, {CAST_BALTHIR: {CAST_MEMO: EFFECTS_2_SPELL}})
    o = cast_original(work, wild, save, "effects-3-bh", opts.verbose, effects_2_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "effects-3-bh-save", save)
    aim, tail = o["aim"], o["tail"]
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == EFFECTS_2_AIM, f"the original's RandSeed at Burning Hands' Aim (C, C) is "
            f"{EFFECTS_2_AIM[0]} draw past BALTHIR's menu, at {EFFECTS_2_AIM[1]:#x}", f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, tail["seed"])
    v.check((draws, tail["seed"]) == EFFECTS_3_TAIL, f"the original's tail after T (Burning Hands on himself, node 1 "
            f"Mirror Image) is {EFFECTS_3_TAIL[0]} draws past BALTHIR's menu, at {EFFECTS_3_TAIL[1]:#x}",
            f"{draws} draws, {tail['seed']:#x}")
    got = (rec_byte(aim["records"][CAST_BALTHIR], HIT_POINTS), rec_byte(tail["records"][CAST_BALTHIR], HIT_POINTS))
    v.check(got == (EFFECTS_2_HP, EFFECTS_2_HP), "BALTHIR's hit points at the tail stay 0x6a (both images, one lost)",
            f"{got[0]:#x} -> {got[1]:#x}")
    nodes = [n for n in tail["chains"][CAST_BALTHIR] if n[0] == EFFECTS_3_NODE[0]]
    v.check(nodes == [[0x1C, 0, 0x42, 0]], "BALTHIR's Mirror Image at the tail is value 0x42 (one image lost)",
            f"{nodes}")
    forgotten = rec_byte(tail["records"][CAST_BALTHIR], CAST_MEMO)
    v.check(forgotten == EFFECTS_2_SPELL | CAST_FORGOTTEN, f"at the tail BALTHIR's Burning Hands is forgotten (+0xce "
            f"{EFFECTS_2_SPELL | CAST_FORGOTTEN:#x})", f"+0xce {forgotten:#x}")
    p = turn_port(work, wild, "effects-3-bh", "CCT", entry=entry)
    mark = cast_mark(p, "01")
    if not mark["base"].with_suffix(".records").exists():
        v.check(False, "eclplay writes BALTHIR's turn mark (01) after C, C and T (Mirror Image)", p["last"])
        return
    command_state(v, "Burning Hands' tail (Mirror Image: turn mark 01)", tail, mark, m0)
    chains_agree(v, tail["chains"], mark["base"].with_suffix(".effects"), " (Burning Hands' tail, Mirror Image, turn mark 01)")
    print(f"eclplay (effects 3, Burning Hands) ends {p['last']!r}", flush=True)


def effects_3_melee(v, work, wild, opts):
    """The melee at BALTHIR with his node 1 at Mirror Image, nothing memorised: E at BALTHIR's menu, E (SHARKMAN 2's turn, its two
    blows taken by images), then TIMASALIN's menu: the draws and RandSeed, BALTHIR's hit points, his node 0x1c (value 0x22), and
    eclplay's turn mark at TIMASALIN's menu (RandSeed, records, blocks, effect chains)."""
    save = dqk_modify.modified({CAST_BALTHIR: {1: EFFECTS_3_NODE}}, {})
    o = cast_original(work, wild, save, "effects-3-melee", opts.verbose, effects_1_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "effects-3-melee-save", save)
    nxt, second = o["next"], o["second"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == EFFECTS_1_NEXT, f"the original's RandSeed at WALKER's menu after E (BALTHIR's turn) is "
            f"{EFFECTS_1_NEXT[0]} draws past the menu, at {EFFECTS_1_NEXT[1]:#x}", f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, second["seed"])
    v.check((draws, second["seed"]) == EFFECTS_3_MELEE, f"the original's RandSeed at TIMASALIN's menu after the melee "
            f"(Mirror Image) is {EFFECTS_3_MELEE[0]} draws past the menu, at {EFFECTS_3_MELEE[1]:#x}",
            f"{draws} draws, {second['seed']:#x}")
    got = (rec_byte(o["menu"]["records"][CAST_BALTHIR], HIT_POINTS), rec_byte(second["records"][CAST_BALTHIR], HIT_POINTS))
    v.check(got == (EFFECTS_2_HP, EFFECTS_2_HP), "BALTHIR's hit points after the melee stay 0x6a (both blows taken by images)",
            f"{got[0]:#x} -> {got[1]:#x}")
    nodes = [n for n in second["chains"][CAST_BALTHIR] if n[0] == EFFECTS_3_NODE[0]]
    v.check(nodes == [[0x1C, 0, 0x22, 0]], "BALTHIR's Mirror Image at TIMASALIN's menu is value 0x22 (two images lost)",
            f"{nodes}")
    p = turn_port(work, wild, "effects-3-melee", EFFECTS_1_NAMES, entry=entry)
    mark = mark_of_seed(p, second["seed"])
    if mark is None:
        v.check(False, f"eclplay has a turn mark whose RandSeed is the original's at TIMASALIN's menu ({EFFECTS_3_MELEE[1]:#x}) "
                f"after {EFFECTS_1_NAMES}", p["last"])
        return
    command_state(v, f"the melee (Mirror Image: TIMASALIN's menu, turn mark {mark['number']})", second, mark, m0)
    chains_agree(v, second["chains"], mark["base"].with_suffix(".effects"), f" (Mirror Image melee, turn mark {mark['number']})")
    print(f"eclplay (effects 3, melee) ends {p['last']!r} at turn mark {mark['number']}", flush=True)


def effects_3(v, work, opts):
    """Band a's effects-3 (part 34c), the flow effects_3: BALTHIR's node 1 at Mirror Image (0x62), Burning Hands on himself and
    the melee against eclplay's turn marks (RandSeed, records, blocks, effect chains)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    effects_3_bh(v, work, wild, opts)
    effects_3_melee(v, work, wild, opts)


# Effects band a's 7e76 four (step 4, part 34d, effects-4): Silence on himself. Saved game A with BALTHIR memorising Silence
# (+0xce 0x19, as band a1's): C, C (WALKER's menu, 2 draws), E (WALKER's End: his pending Silence at its Aim, 3 draws), T
# (his own cell: no N), then TIMASALIN's menu, 43 draws past the menu, RandSeed d3e6c85d. BALTHIR gains effect 0x15 (silence,
# 20 minutes, value 10, flag 0, the save failed); SHARKMAN 2 hits ROARK 0x62 -> 0x5b (effects-1's 0x2f); TIMASALIN's combat
# block (+1 may cast, +2 may use) is 0, 0: silenced at his turn start by BALTHIR's node (shared within 1).
EFFECTS_4_SILENCE = 0x19
EFFECTS_4_NEXT = (2, 0xD9491FD4)        # C, C: WALKER's menu, 2 draws past BALTHIR's
EFFECTS_4_AIM = (3, 0x8A1EDDA5)         # E: BALTHIR's pending Silence at its Aim, 3 draws
EFFECTS_4_MENU = (43, 0xD3E6C85D)       # T: TIMASALIN's menu, 43 draws past BALTHIR's
EFFECTS_4_NODE = [0x15, 20, 10, 0]      # BALTHIR's new node: effect, minutes, value, flag
EFFECTS_4_ROARK = 4                     # ROARK's record (0-based): hit points 0x62 -> 0x5b
EFFECTS_4_TIMASALIN = 1                 # TIMASALIN's record (0-based): silenced, +1 and +2 0


def effects_4_run(box, menu, work, tag):
    """C, C (Begins Casting: WALKER's menu), E (WALKER's End: BALTHIR's pending Silence at its Aim), T (his own cell), then
    the next menu (TIMASALIN's), one trap at a time."""
    out = dict(next=cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "CC", None)])[0])
    out["aim"] = cast_key(box, work, f"{tag}-aim", "E")
    out["second"] = cast_traps(box, menu["handle"], [(MENU, "5045:08cf", "T", None)])[0]
    return out


def effects_4(v, work, opts):
    """Band a's 7e76 four (part 34d), the flow effects_4: Silence on himself (BALTHIR's node 2 is his Silence, 0x19, memorised),
    the original's draws and RandSeed at each menu, the new node, the hit points and TIMASALIN's silenced block, then
    eclplay's turn mark at TIMASALIN's menu (RandSeed, records, blocks, effect chains)."""
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    wild = hlib.replace(glb, JUMP, dqkecl.assemble(wild_block(*WILD_GROUNDS[0])))
    save = dqk_modify.modified({}, {CAST_BALTHIR: {CAST_MEMO: EFFECTS_4_SILENCE}})
    o = cast_original(work, wild, save, "effects-4", opts.verbose, effects_4_run)
    m0 = o["menu"]["seed"]
    entry = saved_entry(work / "effects-4-save", save)
    nxt, aim, second = o["next"], o["aim"], o["second"]
    draws = L.draws(m0, nxt["seed"])
    v.check((draws, nxt["seed"]) == EFFECTS_4_NEXT, f"the original's RandSeed at WALKER's menu after C, C is "
            f"{EFFECTS_4_NEXT[0]} draws past BALTHIR's menu, at {EFFECTS_4_NEXT[1]:#x}", f"{draws} draws, {nxt['seed']:#x}")
    draws = L.draws(m0, aim["seed"])
    v.check((draws, aim["seed"]) == EFFECTS_4_AIM, f"the original's RandSeed at BALTHIR's pending Silence at its Aim is "
            f"{EFFECTS_4_AIM[0]} draws past BALTHIR's menu, at {EFFECTS_4_AIM[1]:#x}", f"{draws} draws, {aim['seed']:#x}")
    draws = L.draws(m0, second["seed"])
    v.check((draws, second["seed"]) == EFFECTS_4_MENU, f"the original's RandSeed at TIMASALIN's menu after T (Silence on "
            f"himself) is {EFFECTS_4_MENU[0]} draws past BALTHIR's menu, at {EFFECTS_4_MENU[1]:#x}",
            f"{draws} draws, {second['seed']:#x}")
    nodes = [n for n in second["chains"][CAST_BALTHIR] if n[0] == EFFECTS_4_NODE[0]]
    v.check(nodes == [EFFECTS_4_NODE], f"BALTHIR's new effect at TIMASALIN's menu is 0x15, 20 minutes, value 10, flag 0",
            f"{second['chains'][CAST_BALTHIR]}")
    hp = (rec_byte(o["menu"]["records"][EFFECTS_4_ROARK], HIT_POINTS), rec_byte(second["records"][EFFECTS_4_ROARK], HIT_POINTS))
    v.check(hp == (0x62, 0x5B), "ROARK's hit points after SHARKMAN 2's hit (0x62 -> 0x5b)", f"{hp[0]:#x} -> {hp[1]:#x}")
    block = bytes.fromhex(second["blocks"][EFFECTS_4_TIMASALIN])
    v.check(block[1] == 0 and block[2] == 0, "TIMASALIN's block at his menu: may cast 0, may use 0 (silenced)",
            f"may cast {block[1]}, may use {block[2]}")
    p = turn_port(work, wild, "effects-4", "CCET", entry=entry)
    mark = mark_of_seed(p, second["seed"])
    if mark is None:
        v.check(False, f"eclplay has a turn mark whose RandSeed is the original's at TIMASALIN's menu ({EFFECTS_4_MENU[1]:#x}) "
                "after CCET", p["last"])
        return
    command_state(v, f"Silence on himself (TIMASALIN's menu, turn mark {mark['number']})", second, mark, m0)
    chains_agree(v, second["chains"], mark["base"].with_suffix(".effects"), f" (silence, turn mark {mark['number']})")
    print(f"eclplay (effects 4) ends {p['last']!r} at turn mark {mark['number']}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows", default=",".join(DEFAULT_FLOWS),
                        help="which flows: " + ", ".join(DEFAULT_FLOWS) + "; auto alone")
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
    if "timer" in flows:
        timer(v, work, opts)
    if "dispatch" in flows:
        dispatch(v, work, opts)
    if "keys" in flows:
        keys(v, work, opts)
    if "altq_missile" in flows:
        altq_missile(v, work, opts)
    if "turns" in flows:
        turns(v, work, opts)
    if "round" in flows:
        round_flow(v, work, opts)
    if "melee" in flows:
        melee(v, work, opts)
    if "melee-long" in flows:
        melee_long(v, work, opts)
    if "melee-dying" in flows:
        melee_dying(v, work, opts)
    if "won" in flows:
        won(v, work, opts)
    if "drop" in flows:
        drop(v, work, opts)
    if "walk" in flows:
        walk(v, work, opts)
    if "mouse" in flows:
        mouse(v, work, opts)
    if "move" in flows:
        move(v, work, opts)
    if "aim" in flows:
        aim(v, work, opts)
    if "turn" in flows:
        turn(v, work, opts)
    if "cast" in flows:
        cast(v, work, opts)
    if "cast_auto" in flows:
        cast_auto(v, work, opts)
    if "cast_auto_area" in flows:
        cast_auto_area(v, work, opts)
    if "band_a1" in flows:
        band_a1(v, work, opts)
    if "band_a2" in flows:
        band_a2(v, work, opts)
    if "band_a3" in flows:
        band_a3(v, work, opts)
    if "band_b1" in flows:
        band_b1(v, work, opts)
    if "band_b2" in flows:
        band_b2(v, work, opts)
    if "band_b3" in flows:
        band_b3(v, work, opts)
    if "band_b4" in flows:
        band_b4(v, work, opts)
    if "band_b5" in flows:
        band_b5(v, work, opts)
    if "band_c1" in flows:
        band_c1(v, work, opts)
    if "effects_1" in flows:
        effects_1(v, work, opts)
    if "effects_2" in flows:
        effects_2(v, work, opts)
    if "effects_4" in flows:
        effects_4(v, work, opts)
    if "effects_3" in flows:
        effects_3(v, work, opts)
    if "auto" in flows:
        auto(v, work, opts)
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

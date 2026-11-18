#!/usr/bin/env python3
"""The Dark Queen of Krynn's end of combat with no monsters, from the treasure branch's start to
the experience (DARK-QUEEN.md's step 4, part 19a: dqk-end-of-combat; docs/checks.md, "Checks
against the original": dqk_treasure).

A test block (tools/dqkecl.py) replaces block 1 of a copy of DISK3\\ECL.GLB, and the original
runs it from a saved game (dqk_fight's enter: plta, Begin Adventuring, the copy protection
answered), with the seed set after the block's entry PRESS KEY:

    PRESS KEY; LOAD FILES 2 1 (the jumper's, as dqk_shops's temple block); COMPARE 1 1 (the flags
    = <> < > <= >= become 01 00 00 00 01 01); SAVE 1 -> var 0x2e; PRESS KEY; COMBAT; EXIT

COMBAT with no monsters, no vault and no temple wanted runs 75cc:135c: its treasure start walks the
lost weapons (1297, none here), clears var 0x2e, the flags = <> < > (2093:0015), and DS:c7df, then
0593 (the party's after combat) sets the party's flags and removes the effects that end with the
battle. When a member stands, 0593 runs 01fb (the experience: the pool's coins over the party) and
0427 (the award), then its rest from 75cc:0736: a member standing walks the party (revives, fled
removals), the load asked (DS:b7a7) removes the party (818-86e). Then 135c sets the mode 6 (1390),
calls 11b0 (the enemies removed, part 19d), and finishes (part 19d, ported): the stats (177e:0be2)
of each record, the lost party's words (b7a7, 1460) or the results (091f: the texts, the share's
line, the bar's key), then the pool (63ee:0002), the variables, the monsters (75cc:151b), and back
to COMBAT's ending (monster.c). Where the pool has money or items the treasure menu (75cc:0e93) runs:
its waits are read one at a time (POOL_STEPS and the others: the screen, the variables, the records,
the pool's coins, and the selected record by name), and its last keys lead to COMBAT's return
(5f07:0987), the end. Detect's cast (part 28) is not typed: a cited stop. Take, Pool and Share are typed in the flows
below. A lost party runs on to COMBAT's return (5f07:0987), and the adventure's menu after it (5f07:11f3, no
b7a7 test) is the stop.

The menu's animation is frozen in the original only (freeze_frame: 1bdb:1168's incb DS:b20e is four
nops), so the menu's screens are compared exactly, the treasure picture included (eclplay never
advances a frame). The original is trapped in two stages (dqk_fight_lib): 2093:0015 (the flags' clear, in the root image,
so it is always resident) spins at the PRESS KEY before COMBAT's key; while it spins, the flow's end
(75cc:0e93 or 5f07:0987, COMBAT's return after 135c) is trapped too, then 2093:0015 is
given back. The game then waits at its first key (the results' bar, or the lost party's continue
prompt): its screen is read there, the key is typed, and the end's trap holds. Its state is read
where the spin holds: the 1 KB of variables, the records (pointers masked), the flags, DS:af3f, c7de,
c7df, b1da, b7a7, the mode (DS:ae57), RandSeed, and the 18 effect ids at DS:631e. Stage 1 also shows
that 135c's start ran: var 0x2e is 0 at the 2093:0015 spin.

eclplay runs the same block twice from the same saved game: with the PRESS KEY only (its last shot
is its first key wait: the screen compared with the original's there), and with every key typed (its
--state, --records, --vars and --screen at its end or its cited stop, compared with the original's).

Flows (each its own saved game as the original starts it; the block's ops before the PRESS KEY
before COMBAT are its `extra`, and each member's record changes are its `changes`):
- `won`: saved game A as it is (its members standing, status 0): 01fb and 0427 run, then 0736.
- `fled`: every member's status word 3 (the others' records as A): 0736 revives them (status 0, active),
  fled set, b7a7 clear.
- `lost`: every member's status word 2 and inactive: 0736 removes the six (818-86e: the count to 0),
  nobody standing or fled, b7a7 set, b1da 6.
- `pool`: TREASURE ITEMS 1000 3 1 0 first (the pool's coins 1000, 3, 1; no items): the share is
  4950 / 6 = 825, and each member gets its share by class (the bonus, the half and the third). The
  menu's steps (POOL_STEPS): View's sheet (v) and Escape back to the menu (the mode-6 screen), Down
  (the next member selected: by name), Exit's question (e), Yes (y: the menu, window cleared), the
  question again (e) and No (n): the end. Its frame check (frame_kept, bug 3): DS:b20e poked to 2 at
  the sheet, the mode-6 redraw's menu held at its entry (1bdb:1073: 2) and first pass (1bdb:113f:
  2), the first pass's screen the screen it settles to.
- `bonus`: `pool` with the classes changed so that each rule shows (+0x5a: 2, 0 at members 0 and 3,
  5 and 6 at 4 and 5): the 10% bonus, the mage with int 15 (none), the thief's dexterity. The menu:
  Escape (the question), No.
- `zero`: SAVE 0 -> var 0x20 and member 5 inactive: 01fb's divisor is 0 (b1da 1 -> 0), so LDIV@ gives
  0xffffffff; member 0 (class 1) gets -1, the others 0 (-1/3 and -1/2 are 0).
- `negative`: `pool`, SAVE 0 -> var 0x20 and members 3-5 inactive: b1da 3 -> 2, the divisor -2, the
  share -2475; members 0-2 get -2475, -825 and -825 (the third of a negative share is negative).
  The menu: Exit (e), No (n).
- `rest`: the statuses 3, 4 (hit points 0, 5) and 5 and 2 at members 1-5, none fled: 0736's loop A
  revives 3, 4 with hit points and 5 becomes 4 (no var 0x1d), the 4 with 0 hit points stays.
- `gentle`: `rest` with var 0x1d set (SAVE 1): 4 with 0 hit points and 5 are revived (777, 78e).
- `fledmix`: statuses 3, 2, 3, 3, 4 (hit points 0), 3: fled, so 7d8 removes the 2 and the 4 (the
  survivors are members 0, 2, 3 and 5: four records, matched by name, not by position).
- `lost9`: `lost` with var 0x20 set to 9 (SAVE 9): 818-86e removes the six and stores 0.
- `enemy`: `won` with member 5's hostile word 1 (status 0, active): 01fb skips it (status 0), the award
  adds 0, and 11b0 removes it (hostile 1, active: var 0x15 stays 0, var 0x20 one less) and sets DS:c7df.
- `enemyout`: member 5 hostile 1 and inactive (SAVE 5 -> var 0x15 first): 11ba clears var 0x15, 11f4
  counts it (1), and var 0x20 is one less.
- `enemyhigh`: member 5's hostile word is 0x0101: not 1, so 11b0 keeps it (six records stay).
- `take`: the pool 1000 3 1 0 (as `pool`): Take's list (t), Down twice and Select (the prompt), 500
  (Roland's steel 700, the pool's steel 500), the list again, Escape, Exit's question (e), No (n).
- `share`: `pool`'s pool, Share (s): the probe's shares (steel 367 four times, 366 twice; gems 1 for
  the first three; jewelry 1 for the first), the pool empty, Exit (e) with no question.
- `gather`: the pool 1000 3 1 0, Pool (p: the pool 2200 3 1), Share (s), Exit (e).
- `overload`: Roland's steel 64000 (member 0's 0x4c/0x4d; 177e:0be2 recomputes the weight, so 0x56 is
  not poked: 710 + 64000 = 64710), the pool 1000 3 1 0: Take (t), Steel (Down twice, Select), 1000:
  "Overloaded" on the bar and nothing taken (the 32-bit test; a 16-bit sum would let it through), the
  list (Escape), Exit (e), No (n).
- `takeall`: the pool 100 3 1 0: Take, Steel 100 taken whole, Return on the last row (Gems: 1b05:0055
  keeps the cursor on it), Escape on the prompt (0 gems), Escape on the list, Exit (e), No (n).
- `items`: TREASURE ITEMS 0 0 0 5 : 1 0 255 255 80 (part 21a): the pool's four items (id 80, the
  Red Mage's and the White Mage's random scrolls, id 1, head first, from RandSeed 12345) and the
  share 266 (1600 / 6); each item's 17 bytes are compared with the original's list (pool_items,
  member_items: the 0x3d-byte nodes of DS:b1e7 and of each record's +8, 17 bytes at +0x28). Steps:
  the menu (View Take Pool Exit), Exit's question (e), No (n).

eclplay runs the same block from the same saved game (--load, --play) and stops at its cited stop;
its state is --vars, --records and --state (the mode, RandSeed, the flags, and the end-of-combat
flags). Every flow compares them, and each original value is checked against the expectation the flow
sets up, so that a mistake in the flow shows.

    python3 tools/dosbox/checks/dqk_treasure.py [--workdir DIR] [--flows won,fled,lost] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what differs."""
import argparse
import pathlib
import shutil
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))  # tools/: hlib, dqkecl
import checklib  # noqa: E402
import dqk_ecl  # noqa: E402
import dqk_ecl_text  # noqa: E402
import dqk_fight  # noqa: E402
import dqk_fight_lib as L  # noqa: E402
import dqk_lib  # noqa: E402
import dqk_modify  # noqa: E402
import dqk_shops  # noqa: E402
import dqkecl  # noqa: E402
import hlib  # noqa: E402
from dqkecl import Block, imm, var  # noqa: E402

cokbox = checklib.cokbox
JUMP = dqk_ecl.JUMP
SEED = dqk_ecl.SEED
VARIABLES = dqk_ecl.VARIABLES
SURPRISE = 0x2e  # var 0x2e: 75cc:135c's start clears it (COK_VAR_SURPRISE in rules_dark_queen.c)
MEMBERS = 6      # saved game A's party (dqk_modify.record_at)
NAME = 0x68      # the record's name, 16 bytes (rules_dark_queen.c's COK_FIELD_NAME)
EFFECTS = bytes([0x03, 0x0b, 0x15, 0x17, 0x1b, 0x23, 0x1f, 0x33, 0x34, 0x35,
                 0x78, 0x76, 0x6f, 0x6a, 0xb2, 0xb1, 0x95, 0x4d])  # DS:631e, 18 bytes
TREASURE = ("TREASURE_ITEMS", (dqkecl.word(1000), imm(3), imm(1), imm(0)))  # steel, gems, jewelry
TREASURE100 = ("TREASURE_ITEMS", (dqkecl.word(100), imm(3), imm(1), imm(0)))  # steel 100
SAVE_ZERO = ("SAVE", (imm(0), var(0x20)))  # var 0x20 (the shares' divisor) is 0
ZERO = [0] * MEMBERS
NO_POOL = (0, 0, 0)
# The treasure menu's steps (75cc:0e93, dark_queen.c's treasure_menu): (what, the keys typed at
# it). The first step is the menu after the results' key; each later step's keys are typed at
# its key wait; the last step's keys lead to COMBAT's return (5f07:0987), which is the end.
POOL_STEPS = [("the treasure menu after the results", ""), ("View's sheet", "v"),
              ("the menu after the sheet (mode 6's screen)", "\\e"),
              ("Down: the next member selected", "\\v"), ("Exit's question", "e"),
              ("the menu after Yes (the window cleared)", "y"), ("Exit's question again", "e"),
              ("the adventure after No", "n")]
# Take, Pool and Share (part 20b): each step's keys as the probe typed them (saved game A, pool
# 1000 3 1 0): Take's list (Jewelry 1, Gems 3, Steel 1000), Down twice to Steel and Select (the
# prompt), 500 (Roland's steel 700, the pool's 500), the list again and Escape (the menu).
TAKE_STEPS = [("the treasure menu after the results", ""),
              ("Take's coin list (Jewelry 1, Gems 3, Steel 1000)", "t"),
              ("the prompt for Steel (Down twice, Select)", "\\v\\v\\r"),
              ("the coin list after 500 (Steel 500)", "500\\r"),
              ("the menu after the list (Escape)", "\\e"), ("Exit's question", "e"),
              ("the adventure after No", "n")]
SHARE_STEPS = [("the treasure menu after the results", ""),
               ("the menu after Share (View Pool Exit)", "s"),
               ("the adventure after Exit (no question: the pool is empty)", "e")]
OVERLOAD_STEPS = [("the treasure menu after the results", ""),
                  ("Take's coin list (Steel 64000 as Roland's, the pool 1000)", "t"),
                  ("the prompt for Steel (Down twice, Select)", "\\v\\v\\r"),
                  ("the coin list after 1000: Overloaded, nothing taken", "1000\\r"),
                  ("the menu after the list (Escape)", "\\e"), ("Exit's question", "e"),
                  ("the adventure after No", "n")]
# Take's whole row (Steel 100 taken whole; the cursor on Gems, the last row), then Return on Gems'
# prompt, Escape there (0 gems) and Escape on the list.
TAKEALL_STEPS = [("the treasure menu after the results", ""),
                 ("Take's coin list (Jewelry 1, Gems 3, Steel 100)", "t"),
                 ("the prompt for Steel (Down twice, Select)", "\\v\\v\\r"),
                 ("the coin list after 100 (Steel gone, Gems selected)", "100\\r"),
                 ("Gems' prompt (Return on the last row)", "\\r"),
                 ("the coin list after the prompt (Escape: 0 gems)", "\\e"),
                 ("the menu after the list (Escape)", "\\e"), ("Exit's question", "e"),
                 ("the adventure after No", "n")]
GATHER_STEPS = [("the treasure menu after the results", ""),
                ("the menu after Pool (the pool 2200 3 1)", "p"),
                ("the menu after Share", "s"),
                ("the adventure after Exit (no question)", "e")]
BONUS_STEPS = [("the treasure menu after the results", ""), ("Escape is Exit: the question", "\\e"),
               ("the adventure after No", "n")]
NEGATIVE_STEPS = [("the treasure menu after the results", ""), ("Exit's question", "e"),
                  ("the adventure after No", "n")]
# TREASURE ITEMS' items (part 21a): the header 0 0 0 and n 5, then the list 1 0 255 255 80 (id 1,
# id 0 with no entry, the two random scrolls, id 80). Measured: the pool at the menu is ITEMS_POOL
# (head first), RandSeed 0x476726e9; at the question, RandSeed 0x5202e7e7.
ITEMS = ("TREASURE_ITEMS", (dqkecl.word(0), imm(0), imm(0), imm(5), imm(1), imm(0), imm(0xff),
                            imm(0xff), imm(80)))
ITEMS_STEPS = [("the treasure menu after the results", ""), ("Exit's question", "e"),
               ("the adventure after No", "n")]
ITEMS_POOL = ["1e00701e0200dc0502000006000a000000", "276627880100b80b0100001000002e5b0f",
              "276627890100b80b0100002000005c540a", "1e00001e0100000000000000000a000000"]
FLOWS = {
    # flow: the saved game's changes (per member, or None), the block's ops before COMBAT (extra),
    # the stop's cite, the expected state (standing, fled, b7a7, out), DS:ae4f (the share; None
    # where 01fb does not run), the pool's coins 4-6, the experience each member gains, and each
    # member's may-train (+0xcd). The award is 0 where no member stands.
    "won": dict(changes=None, extra=[], end="end", expect=(True, False, False, 0),
                share=0, pool=NO_POOL, gained=ZERO, may=ZERO),
    "fled": dict(changes=[{0x64: 3, 0x65: 0}] * MEMBERS, extra=[], end="end",
                 expect=(False, True, False, 0), share=None, pool=NO_POOL, gained=ZERO, may=ZERO),
    "lost": dict(changes=[{0x64: 2, 0x65: 0, 0x18b: 0}] * MEMBERS, extra=[],
                 end="lost", expect=(False, False, True, MEMBERS), share=None,
                 pool=NO_POOL, gained=[], may=[], members=0),
    "pool": dict(changes=None, extra=[TREASURE], end="end", steps=POOL_STEPS, selects=(3, 1),
                 frame=True,
                 expect=(True, False, False, 0), share=825, pool=NO_POOL,
                 gained=[825, 275, 275, 412, 412, 412], may=[0, 0, 0, 0, 1, 0]),
    "bonus": dict(changes=[{0x5a: 2}, {}, {}, {0x5a: 0}, {0x5a: 5}, {0x5a: 6}],
                  extra=[TREASURE], end="end", steps=BONUS_STEPS, expect=(True, False, False, 0),
                  share=825, pool=NO_POOL, gained=[907, 275, 275, 907, 825, 907],
                  may=[0, 0, 0, 0, 1, 0]),
    "zero": dict(changes=[{}, {}, {}, {}, {}, {0x18b: 0}], extra=[SAVE_ZERO],
                 end="end", expect=(True, False, False, 0), share=0xffffffff,
                 pool=NO_POOL, gained=[-1, 0, 0, 0, 0, 0], may=ZERO),
    "negative": dict(changes=[{}, {}, {}, {0x18b: 0}, {0x18b: 0}, {0x18b: 0}],
                     extra=[TREASURE, SAVE_ZERO], end="end", steps=NEGATIVE_STEPS,
                     expect=(True, False, False, 2), share=-2475 & 0xFFFFFFFF, pool=NO_POOL,
                     gained=[-2475, -825, -825, 0, 0, 0], may=ZERO),
    "rest": dict(changes=[{}, {0x64: 3}, {0x64: 4, 0x198: 0}, {0x64: 4, 0x198: 5}, {0x64: 5},
                          {0x64: 2}], extra=[], end="end",
                 expect=(True, False, False, 0), share=None, pool=NO_POOL, gained=ZERO, may=ZERO),
    "gentle": dict(changes=[{}, {0x64: 3}, {0x64: 4, 0x198: 0}, {0x64: 4, 0x198: 5}, {0x64: 5},
                            {0x64: 2}], extra=[("SAVE", (imm(1), var(0x1d)))], end="end",
                   expect=(True, False, False, 0), share=None, pool=NO_POOL, gained=ZERO, may=ZERO),
    "fledmix": dict(changes=[{0x64: 3}, {0x64: 2}, {0x64: 3}, {0x64: 3}, {0x64: 4, 0x198: 0},
                             {0x64: 3}], extra=[], end="end",
                    expect=(False, True, False, 0), share=None, pool=NO_POOL, gained=[0] * 4,
                    may=[0] * 4, members=4),
    "enemy": dict(changes=[{}, {}, {}, {}, {}, {0x66: 1, 0x67: 0}], extra=[],
                  end="end", expect=(True, False, False, 0), share=0, pool=NO_POOL,
                  gained=[0] * 5, may=[0] * 5, members=5, monsters=1),
    "enemyout": dict(changes=[{}, {}, {}, {}, {}, {0x66: 1, 0x67: 0, 0x18b: 0}],
                     extra=[("SAVE", (imm(5), var(0x15)))], end="end",
                     expect=(True, False, False, 1), share=0, pool=NO_POOL,
                     gained=[0] * 5, may=[0] * 5, members=5, monsters=1),
    "enemyhigh": dict(changes=[{}, {}, {}, {}, {}, {0x66: 1, 0x67: 1}], extra=[],
                      end="end", expect=(True, False, False, 0), share=0, pool=NO_POOL,
                      gained=ZERO, may=ZERO),
    # The coins flows (part 20b): the pool 1000 3 1 0 as "pool" (the share 825 and the same
    # experience), with the treasure menu's Take, Overload, Share and Pool.
    "take": dict(changes=None, extra=[TREASURE], end="end", steps=TAKE_STEPS,
                 expect=(True, False, False, 0), share=825, pool=NO_POOL,
                 gained=[825, 275, 275, 412, 412, 412], may=[0, 0, 0, 0, 1, 0]),
    "share": dict(changes=None, extra=[TREASURE], end="end", steps=SHARE_STEPS,
                  expect=(True, False, False, 0), share=825, pool=NO_POOL,
                  gained=[825, 275, 275, 412, 412, 412], may=[0, 0, 0, 0, 1, 0]),
    "gather": dict(changes=None, extra=[TREASURE], end="end", steps=GATHER_STEPS,
                   expect=(True, False, False, 0), share=825, pool=NO_POOL,
                   gained=[825, 275, 275, 412, 412, 412], may=[0, 0, 0, 0, 1, 0]),
    # overload: Roland's steel 64000 (weight 710 + 64000 = 64710: the 16-bit sum would pass), the
    # pool 1000 3 1 0 stays after Take's 1000 ("Overloaded"). takeall: Steel 100 taken whole, then
    # Return on Gems (the list's cursor past the last row is kept on Gems, 1b05:0055).
    "overload": dict(changes=[{0x4c: 64000 & 0xff, 0x4d: 64000 >> 8}, {}, {}, {}, {}, {}],
                     extra=[TREASURE], end="end", steps=OVERLOAD_STEPS,
                     expect=(True, False, False, 0), share=825, pool=NO_POOL,
                     gained=[825, 275, 275, 412, 412, 412], may=[0, 0, 0, 0, 1, 0]),
    "takeall": dict(changes=None, extra=[TREASURE100], end="end", steps=TAKEALL_STEPS,
                    expect=(True, False, False, 0), share=525, pool=NO_POOL,
                    gained=[525, 175, 175, 262, 262, 262], may=[0, 0, 0, 0, 1, 0]),
    "items": dict(changes=None, extra=[ITEMS], end="end", steps=ITEMS_STEPS, items=True,
                  expect=(True, False, False, 0), share=266, pool=NO_POOL,
                  gained=[266, 88, 88, 133, 133, 133], may=[0, 0, 0, 0, 1, 0]),
    "lost9": dict(changes=[{0x64: 2, 0x65: 0, 0x18b: 0}] * MEMBERS,
                  extra=[("SAVE", (imm(9), var(0x20)))], end="lost",
                  expect=(False, False, True, MEMBERS), share=None, pool=NO_POOL, gained=[],
                  may=[], members=0),
}
# The small picture's frame advance (1bdb:1168, incb DS:b20e), frozen in the box only
# (freeze_frame): the frame then changes only by a load (63ae:0299) or the check's own poke, as
# eclplay's does (it never advances: no time passes there).
FRAME_ADVANCE = (0x1BDB, 0x1168)
FRAME_ADVANCE_BYTES = bytes.fromhex("fe060eb2")  # incb DS:b20e
FRAME_NOPS = b"\x90" * 4
FRAME = 0xB20E   # DS:b20e, the small picture's frame
FRAME_SEG = 0x1BDB               # the menu library's wait (1bdb:1008)
FRAME_ENTRY = 0x1073             # the wait's entry: the frame it reads
FRAME_FIRST = 0x113F             # its first pass: the frame it draws
MODE = 6     # DS:ae57 at the stop: 135c's 1390 sets it before 11b0 (and 11b0 leaves it)
EXP = 0x44   # the experience, a dword (rules_dark_queen.c: DWORD(0x44))
MAY = 0xcd   # the may-train byte (BYTE(0xcd))


def start_block(extra=()):
    """Block 1: the entry PRESS KEY, the 3D view's files, the flags set, var 0x2e, the extra ops
    (name, operands), a PRESS KEY before COMBAT, COMBAT, EXIT."""
    b = Block()
    b.op("PRESS_KEY")
    b.op("LOAD_FILES", imm(2), imm(1))  # the jumper's (block 1, ECL 8025), as dqk_shops
    b.op("COMPARE", imm(1), imm(1))     # the flags: = <= >= are set, < > <> are clear
    b.op("SAVE", imm(1), var(SURPRISE))
    for name, operands in extra:
        b.op(name, *operands)
    b.op("PRESS_KEY")
    b.op("COMBAT")
    b.op("EXIT")
    return b


def saved_game(work, name, changes):
    """Saved game A with each member changed (changes: a list of offset: byte dicts, one a member),
    in a folder of its own."""
    data = bytearray((dqk_lib.ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
    if changes is not None:
        for member in range(MEMBERS):
            at = dqk_modify.record_at(data, member)
            for offset, value in changes[member].items():
                data[at + offset] = value
    folder = work / f"saves-{name}"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    (folder / "SAVGAMA.QSV").write_bytes(bytes(data))
    return bytes(data), [folder]


def saved_member(qsv, raw):
    """The saved game's member whose name is the record's. 0593's removals move the survivors
    along the party, so a survivor is matched by its record, not by its position."""
    hits = [m for m in range(MEMBERS)
            if qsv[dqk_modify.record_at(qsv, m) + NAME:][:16] == raw[NAME:NAME + 16]]
    if len(hits) != 1:
        raise cokbox.BoxError(f"the record's name is in the saved game {len(hits)} times")
    return hits[0]


def flags_of(box):
    return list(box.ds_read(0xb2f5, 6))


def pool_of(box):
    """The pool's coins 4-6 (DS:b1db, b1df, b1e3)."""
    return tuple(int.from_bytes(box.ds_read(at, 4), "little") for at in (0xb1db, 0xb1df, 0xb1e3))


def selected_name(box):
    """The name of the selected record (DS:ad67, a far pointer; NAME bytes), or b"" for none."""
    sel = box.far(box.ds * 16 + 0xAD67)
    return box.read(sel + NAME, 16) if sel is not None else b""


def freeze_frame(box):
    """The small picture's frame advance (1bdb:1168) frozen in the box only: the four bytes
    incb DS:b20e become four nops. Checked by its bytes first: BoxError if they differ. Call it
    after the block's entry (the menu library is resident there)."""
    at = L.code(box, *FRAME_ADVANCE)
    if box.read(at, 4) != FRAME_ADVANCE_BYTES:
        raise cokbox.BoxError(f"1bdb:1168 is not incb DS:b20e: {box.read(at, 4).hex()}")
    box.write(at, FRAME_NOPS)


def frame_frozen(box):
    return box.read(L.code(box, *FRAME_ADVANCE), 4) == FRAME_NOPS


def hold(box, addr, seg, off):
    """A spin at addr (a mid-routine address: its two bytes EB FE), as the trap of dqk_fight_lib."""
    orig = box.read(addr, 2)
    box.write(addr, b"\xeb\xfe")
    return L.Trap([(addr, orig)], seg, off)


def held_at(box, handle, what):
    t0 = time.monotonic()
    while not L.held(box, handle, 0.3)[0]:
        if time.monotonic() - t0 > 30:
            raise cokbox.BoxError(f"{what} did not hold")


def frame_kept(box, keys):
    """Bug 3 in the original (63ae:00c4-00d9 keeps the frame of the picture already loaded). At
    the sheet DS:b20e was poked to 2 (the sheet animates nothing); the keys (Escape: the mode-6
    redraw, then the menu) are typed, and the menu's wait is held at its entry (1bdb:1073: the
    frame it reads) and at its first pass (1bdb:113f: the frame it draws). Its screen settles with
    frame 2 still (the shot of the first pass is the settled screen's box). Then DS:b20e is 0
    again, so the later passes draw frame 0 as eclplay does. Returns (the frame at the entry, the
    frame at the first pass, the first pass's shot, the settled screen with frame 2)."""
    seg = FRAME_SEG
    entry = hold(box, L.code(box, seg, FRAME_ENTRY), seg, FRAME_ENTRY)
    box.keys(keys)
    held_at(box, entry, "1bdb:1073 (the menu's entry after the sheet)")
    at_entry = box.ds_byte(FRAME)
    first = hold(box, L.code(box, seg, FRAME_FIRST), seg, FRAME_FIRST)
    L.untrap(box, entry)
    held_at(box, first, "1bdb:113f (the menu's first pass)")
    at_first = box.ds_byte(FRAME)
    shot = box.screenshot()
    L.untrap(box, first)
    t0 = time.monotonic()
    while not box.waiting():
        if time.monotonic() - t0 > 60:
            raise cokbox.BoxError("the game waits for no key after the first pass")
    settled = box.settled()
    box.ds_write(FRAME, b"\x00")
    return at_entry, at_first, shot, settled


def item_list(box, head):
    """The items of the list whose far pointer lies at linear address HEAD: each node's 17 bytes
    at +0x28 (hex), in order; a node is 0x3d bytes with the next far pointer at +0."""
    out, node = [], box.far(head)
    while node is not None:
        if len(out) >= 64:
            raise cokbox.BoxError("the item list does not end")
        out.append(box.read(node + 0x28, 17).hex())
        node = box.far(node)
    return out


def pool_items(box):
    """The pool's items (DS:b1e7, head first)."""
    return item_list(box, box.ds * 16 + 0xB1E7)


def member_items(box):
    """Each party record's items (the list at its +8), in list order."""
    return [item_list(box, seg * 16 + off + 8) for (seg, off), _ in L.records(box)]


def step_state(box, what, items=False):
    """The treasure menu's wait (a key wait, its screen settled): the screen, the variables, the
    records, the pool, RandSeed (read after the settle: the animation's clock moves it not),
    the selected record's name, whether the frame advance is frozen, and whether the game waits.
    With items, the pool's and the records' items too."""
    t0 = time.monotonic()
    while not box.waiting():
        if time.monotonic() - t0 > 60:
            raise cokbox.BoxError(f"the game waits for no key at {what}")
    shot = box.settled()
    state = dict(what=what, shot=shot, vars=dqk_fight.block_vars(box),
                 records=[raw for _, raw in L.records(box)], pool=pool_of(box),
                 seed=f"{int.from_bytes(box.ds_read(0xca0e, 4), 'little'):08x}",
                 selected=selected_name(box), frozen=frame_frozen(box), waiting=box.waiting())
    if items:
        state.update(pool_items=pool_items(box), member_items=member_items(box))
    return state


def read_state(box, items=False):
    """The original's state where it spins: the variables, the records, the flags and the
    end-of-combat bytes, the mode, RandSeed and the effect table; with items, the items too."""
    state = dict(
        vars=dqk_fight.block_vars(box),
        records=[raw for _, raw in L.records(box)],
        flags=flags_of(box),
        standing=box.ds_read(0xaf3f, 1)[0] != 0, fled=box.ds_read(0xc7de, 1)[0] != 0,
        monsters=box.ds_read(0xc7df, 1)[0], out=box.ds_read(0xb1da, 1)[0],
        b7a7=box.ds_read(0xb7a7, 1)[0] != 0, demo=box.ds_read(0xae59, 1)[0],
        mode=box.ds_word(0xae57), seed=f"{int.from_bytes(box.ds_read(0xca0e, 4), 'little'):08x}",
        effects=box.ds_read(0x631e, 18), share=int.from_bytes(box.ds_read(0xae4f, 4), "little"),
        pool=pool_of(box))
    if items:
        state.update(pool_items=pool_items(box), member_items=member_items(box))
    return state


# lost: 5f07:0987 is COMBAT's return after the lost party (the continue prompt answered first)
END_STOPS = {"lost": (0x5F07, 0x0987), "end": (0x5F07, 0x0987)}
TYPED_TO_WAIT = "\\r"            # the PRESS KEY before COMBAT (eclplay's entry key comes first)
TYPED_TO_END = "\\r\\r\\r"       # that, the results' key (or the continue prompt's) and one more
TYPED_TO_RESULTS = "\\r\\r"      # the PRESS KEY and the results' key: the treasure menu's wait


def original(work, name, changes, patched, verbose, end, steps=None, frame_check=False,
             items=False):
    """The original's run of the block from its entry to the end's stop (END_STOPS): stage 1 at
    2093:0015 (the flags' clear), the end's trap set while it spins, then the game waits at its
    first key (the results' bar, or the continue prompt): its screen there is read, the key is
    typed, and the end's trap holds. Returns (state, the saved game, the screen at the first key
    wait, the screen at the end's stop)."""
    qsv, saves = saved_game(work, name, changes)
    with cokbox.Box(workdir=work / f"box-{name}", game="dark-queen", verbose=verbose, timeout=900,
                    files=L.files(patched), saves=saves if changes is not None else []) as box:
        dqk_fight.enter(box)
        freeze_frame(box)
        box.set_seed(SEED)
        box.keys("\\r")  # the entry PRESS KEY: LOAD FILES, the flags, SAVE, then the PRESS KEY
        box.settled()
        waiting = box.waiting()
        v = box.far(box.ds * 16 + dqk_ecl.DS_VARS)
        code = L.code(box, 0x2093, 0x0015)
        orig = box.read(code, 2)
        if orig != b"\x55\x8b":
            raise cokbox.BoxError(f"2093:0015 is not where it was expected: {orig.hex()}")
        box.write(code, b"\xeb\xfe")
        h1 = L.Trap([(code, orig)], 0x2093, 0x0015)
        box.keys("\\r")  # COMBAT
        t0 = time.monotonic()
        while not L.held(box, h1)[0]:
            if time.monotonic() - t0 > 60:
                raise cokbox.BoxError("stage 1 did not hold at 2093:0015 (135c's start)")
        stage1_surprise = box.byte(v + SURPRISE)  # 135c's start cleared it before the flags
        seg, off = END_STOPS[end]
        h_end = L.trap(box, seg, off, seconds=10)
        L.untrap(box, h1)
        t0 = time.monotonic()
        while not box.waiting():
            if time.monotonic() - t0 > 60:
                raise cokbox.BoxError("the game waits for no key before the end of the treasure")
        wait_shot = box.settled()
        box.keys("\\r")  # the results' bar, or the continue prompt
        found = []
        frame = None
        if steps:  # the treasure menu: each step's keys at its wait, the last step's lead to the end
            for i, (what, keys) in enumerate(steps[:-1]):
                if i == 2 and frame_check:
                    frame = frame_kept(box, keys)  # the mode-6 redraw after the sheet (bug 3)
                elif i > 0:
                    box.keys(keys)
                found.append(step_state(box, what, items))
                if i == 1 and frame_check:
                    box.ds_write(FRAME, b"\x02")  # the sheet: DS:b20e poked (frame 2)
            box.keys(steps[-1][1])
        hold = L.Trap(list(h_end), seg, off)
        t0 = time.monotonic()
        while not L.held(box, hold)[0]:
            if time.monotonic() - t0 > 60:
                raise cokbox.BoxError(f"the trap at {seg:04x}:{off:04x} did not hold")
        state = read_state(box, items)
        state["stage1_surprise"] = stage1_surprise
        state["waiting"] = waiting
        state["steps"] = found
        state["frame"] = frame
        if end == "end":  # 5f07:0987 is before the adventure loop draws its bar: its next wait
            L.untrap(box, hold)
            box.wait_input()
        end_shot = box.settled()
        return state, qsv, wait_shot, end_shot


def port(work, name, patched, qsv, typed, shots=False):
    """eclplay's run of the block from the saved game's copy (qsv, --load, --play), the keys TYPED
    after the entry. With SHOTS its screen is the last of its shots (one at each key wait: the
    one where the keys ran out); else its --screen at its end. Returns (variables, the records as
    raw bytes, the state's lines, the transcript, the screen or None)."""
    entry = dqk_shops.entry_file(work, name, qsv)
    records = work / f"port-{name}.records"
    state = work / f"port-{name}.state"
    shot_dir = work / f"port-{name}-shots"
    screen = work / f"port-{name}.bmp"
    items_path = work / f"port-{name}.items"
    for path in (records, state, screen, items_path):
        if path.exists():
            path.unlink()
    if shot_dir.exists():
        shutil.rmtree(shot_dir)
    extra = ["--load", str(entry), "--play", "--records", str(records), "--state", str(state),
             "--items", str(items_path)]
    extra += ["--shots", str(shot_dir)] if shots else ["--screen", str(screen)]
    variables, _, _ = dqk_ecl.port(work, name, patched, typed=typed, extra=extra, block=None)
    data = records.read_bytes() if records.exists() else b""
    size = L.RECORD
    ported = [data[k:k + size] for k in range(0, max(len(data) - 3, 0), size)]
    lines = {}
    if state.exists():
        for line in state.read_text().splitlines():
            key, _, value = line.partition(" ")
            lines[key] = value
    # --items: the pool's items, then a line a record (its index, its items), each item as hex.
    if items_path.exists():
        for line in items_path.read_text().splitlines():
            fields = line.split()
            if fields[0] == "pool":
                lines["pool_items"] = fields[1:]
            else:
                lines.setdefault("member_items", []).append(fields[1:])
    transcript = (work / f"port-{name}.txt").read_text()
    if shots:
        waits = sorted(shot_dir.glob("*.bmp"), key=lambda p: int(p.stem)) if shot_dir.is_dir() else []
        picture = cokbox.RGBScreen.load(waits[-1]) if waits else None
    else:
        picture = cokbox.RGBScreen.load(screen) if screen.exists() else None
    return variables, ported, lines, transcript, picture


MASKS = [dqk_ecl_text.POINTER_WIDE]  # the mouse's pointer at the corner (DOSBox-X's own)


def check_items(v, what, want, have, name):
    """The pool's items and each record's items (hex, in order) agree with the original's."""
    v.check(have.get("pool_items", []) == want["pool_items"],
            f"the pool's items at {what} agree ({name})",
            f"original {want['pool_items']}, port {have.get('pool_items')}")
    v.check(have.get("member_items", []) == want["member_items"],
            f"the party's items at {what} agree ({name})",
            f"original {want['member_items']}, port {have.get('member_items')}")


def check_steps(v, work, patched, name, steps, selects, qsv, found, items=False):
    """Each treasure menu wait but the last (the end): eclplay's run typed up to that step, its
    last shot compared with the original's screen, and its variables, records and pool compared
    with the original's there. SELECTS (step, member): the selected record is that member, by
    name, at that step (the original's DS:ad67; eclplay's screen shows the highlight)."""
    for i, ((what, _), o) in enumerate(zip(steps[:-1], found)):
        typed = TYPED_TO_RESULTS + "".join(keys for _, keys in steps[:i + 1])
        variables, ported, lines, transcript, shot = port(work, f"{name}-{i}", patched, qsv,
                                                          typed=typed, shots=True)
        v.check(o["waiting"], f"the original waits for a key at {what} ({name})")
        v.check(len(variables) == VARIABLES and "error:" not in transcript,
                f"eclplay runs to {what} with no error ({name})", transcript.strip()[-200:])
        if shot is not None:
            v.screen(f"the screen at {what} ({name})", o["shot"], shot, masks=MASKS, work=work)
        else:
            v.check(False, f"eclplay saves the screen at {what} ({name})")
        differing = [a for a in range(1, VARIABLES + 1) if o["vars"][a - 1] != variables[a - 1]]
        v.check(not differing, f"the variables at {what} agree ({name})",
                ", ".join(f"{a:x}: original {o['vars'][a - 1]:02x}, port {variables[a - 1]:02x}"
                          for a in differing[:8]))
        want = [dqk_shops.masked(raw) for raw in o["records"]]
        have = [dqk_shops.masked(raw) for raw in ported]
        v.check(len(want) == len(have) and want == have,
                f"the party's records at {what} agree (pointers masked, {name})",
                f"{len(have)} records, the original {len(want)}")
        port_pool = tuple(int(x) for x in lines.get("pool", "").split())
        v.check(port_pool == o["pool"], f"the pool's coins 4-6 at {what} agree ({name})",
                f"original {o['pool']}, port {port_pool}")
        v.check(lines.get("seed") == o["seed"], f"RandSeed at {what} agrees ({name})",
                f"original {o['seed']}, port {lines.get('seed')}")
        if items:
            check_items(v, what, o, lines, name)
        v.check(o["frozen"], f"the frame advance is still frozen at {what} ({name})")
        if selects is not None and selects[0] == i:
            member = selects[1]
            want_name = qsv[dqk_modify.record_at(qsv, member) + NAME:][:16]
            v.check(o["selected"] == want_name, f"the selected record at {what} is member "
                    f"{member} by name ({name})", f"{o['selected']!r}, want {want_name!r}")


def check_frame(v, work, name, frame):
    """Bug 3 in the original: the frame DS:b20e poked to 2 at the sheet is still 2 at the menu's
    entry (63ae:00c4-00d9 keeps it), at its first pass, and the first pass's screen is the screen
    the menu settles to with frame 2 (the frame it draws). eclplay's side is the repo test
    (test_dark_queen_picture_frame_kept): it cannot be poked."""
    at_entry, at_first, shot, settled = frame
    v.check(at_entry == 2, f"DS:b20e is 2 at the menu's entry after the sheet (1bdb:1073): the "
            f"mode-6 redraw keeps the frame ({name})", f"{at_entry}")
    v.check(at_first == 2, f"DS:b20e is 2 at the menu's first pass (1bdb:113f) ({name})",
            f"{at_first}")
    v.screen(f"the first pass draws frame 2 (the screen it settles to with frame 2) ({name})",
             settled, shot, masks=MASKS, work=work)


def run_flow(v, work, patched, name, verbose):
    spec = FLOWS[name]
    end = spec["end"]
    steps = spec.get("steps")
    items = spec.get("items", False)
    orig, qsv, wait_shot, end_shot = original(work, name, spec["changes"], patched, verbose, end,
                                              steps, spec.get("frame", False), items)
    v.check(orig["waiting"], f"the original waits at the PRESS KEY before COMBAT ({name})")
    v.check(orig["stage1_surprise"] == 0, f"var 0x2e is 0 at 2093:0015's spin: 135c's start ran "
            f"({name})", f"{orig['stage1_surprise']}")
    members = spec.get("members", MEMBERS)
    v.check(len(orig["records"]) == members, f"the original's party after 0593 ({name})",
            f"{len(orig['records'])} records, want {members}")
    got = (orig["standing"], orig["fled"], orig["b7a7"], orig["out"])
    v.check(got == spec["expect"], f"the original's af3f, c7de, b7a7 and b1da are as the flow "
            f"sets them ({name})", f"standing, fled, b7a7, out: {got}, want {spec['expect']}")
    v.check(orig["effects"] == EFFECTS, "the 18 effect ids at DS:631e are as the port's table "
            f"({name})", orig["effects"].hex())
    v.check(orig["demo"] == 0, f"the original is not the demonstration ({name})")
    # The mode: 6 where the flow stops in 135c (the treasure menu), else COMBAT's ending has set
    # it (3 or 4: monster.c's overland, at 5f07:0987 for the lost party too).
    v.check(orig["mode"] in (3, 4), f"the mode is 3 or 4 after COMBAT's ending ({name})",
            f"mode {orig['mode']}")
    v.check(orig["monsters"] == spec.get("monsters", 0), f"DS:c7df is as 11b0 sets it ({name})",
            f"{orig['monsters']}, want {spec.get('monsters', 0)}")
    # The flow's own expectations on the original's side: the share, the pool's coins, each
    # member's experience gained (the saved game's, before the award) and its may-train.
    if spec["share"] is not None:
        v.check(orig["share"] == spec["share"], f"the original's share (DS:ae4f) is as the flow "
                f"sets it ({name})", f"{orig['share']:08x}, want {spec['share']:08x}")
    v.check(orig["pool"] == spec["pool"], f"the original's pool coins (DS:b1db, b1df, b1e3) are "
            f"as the flow sets them ({name})", f"{orig['pool']}, want {spec['pool']}")
    gained, may = [], []
    for raw in orig["records"]:
        at = dqk_modify.record_at(qsv, saved_member(qsv, raw))
        before = int.from_bytes(qsv[at + EXP:at + EXP + 4], "little")
        after = int.from_bytes(raw[EXP:EXP + 4], "little")
        gained.append((after - before + 2 ** 31) % 2 ** 32 - 2 ** 31)
        may.append(raw[MAY])
    v.check(gained == spec["gained"], f"the experience each member gains is as the flow's "
            f"arithmetic gives ({name})", f"{gained}, want {spec['gained']}")
    v.check(may == spec["may"], f"each member's may-train is as the flow's ({name})",
            f"{may}, want {spec['may']}")

    # The first key wait: the port's run with the keys ran out there (its last shot).
    _, _, _, _, first = port(work, name + "-wait", patched, qsv, typed=TYPED_TO_WAIT, shots=True)
    v.screen(f"the screen at the first key wait ({name})", wait_shot, first, masks=MASKS,
             work=work)

    if items:  # the flow's own expectations: the four items at the menu, RandSeed there and after
        menu = orig["steps"][0]
        v.check(menu["pool_items"] == ITEMS_POOL, f"the original's pool at the menu holds the four "
                f"items of TREASURE ITEMS ({name})", f"{menu['pool_items']}")
        v.check(menu["seed"] == "476726e9" and orig["steps"][1]["seed"] == "5202e7e7",
                f"RandSeed at the menu and at the question is as measured ({name})",
                f"{menu['seed']}, {orig['steps'][1]['seed']}")
    if steps:
        check_steps(v, work, patched, name, steps, spec.get("selects"), qsv, orig["steps"], items)
    if orig["frame"] is not None:
        check_frame(v, work, name, orig["frame"])
    # The menu's last keys (the end's) are the last of the typed keys, then the adventure's wait.
    typed_end = TYPED_TO_RESULTS + "".join(keys for _, keys in steps) + "\\r" if steps \
        else TYPED_TO_END
    variables, ported, lines, transcript, end_screen = port(work, name, patched, qsv,
                                                           typed=typed_end)
    stop = {"lost": "dqk 5f07:11f3", "end": None}[end]
    if stop is not None:
        v.check(stop in transcript and "error:" in transcript,
                f"eclplay stops at {stop}, cited ({name})", transcript.strip()[-200:])
    else:
        v.check("error:" not in transcript, f"eclplay runs the block to its end ({name})",
                transcript.strip()[-200:])
    v.check(len(variables) == VARIABLES, f"eclplay runs the block to the stop ({name})")
    differing = [a for a in range(1, VARIABLES + 1) if orig["vars"][a - 1] != variables[a - 1]]
    v.check(not differing, f"the 1 KB of variables agree, var 0x2e included ({name})",
            ", ".join(f"{a:x}: original {orig['vars'][a - 1]:02x}, port {variables[a - 1]:02x}"
                      for a in differing[:8]))
    want = [dqk_shops.masked(raw) for raw in orig["records"]]
    have = [dqk_shops.masked(raw) for raw in ported]
    v.check(len(want) == len(have) and want == have,
            f"the party's records agree (pointers masked, {name})",
            f"{len(have)} records, the original {len(want)}")
    port_flags = [int(x) for x in lines.get("flags", "").split()]
    v.check(port_flags == orig["flags"], f"the COMPARE flags agree: = <> < > <= >= ({name})",
            f"original {orig['flags']}, port {port_flags}")
    v.check(int(lines.get("share", -1)) == orig["share"], f"the share (DS:ae4f) agrees ({name})",
            f"original {orig['share']}, port {lines.get('share')}")
    port_pool = tuple(int(x) for x in lines.get("pool", "").split())
    v.check(port_pool == orig["pool"], f"the pool's coins 4-6 agree ({name})",
            f"original {orig['pool']}, port {port_pool}")
    port_seed = lines.get("seed", "")
    v.check(port_seed == orig["seed"], f"RandSeed agrees ({name})",
            f"original {orig['seed']}, port {port_seed}")
    if items:
        check_items(v, "the end", orig, lines, name)
    v.check(int(lines.get("mode", -1)) == orig["mode"], f"the mode agrees ({name})",
            f"original {orig['mode']}, port {lines.get('mode')}")
    # DS:af3f, c7de, c7df, b1da, b7a7 and the share DS:ae4f (eclplay --state's names).
    for key, addr, value in (("standing", "af3f", orig["standing"]), ("fled", "c7de", orig["fled"]),
                             ("monsters", "c7df", orig["monsters"]), ("out", "b1da", orig["out"]),
                             ("b7a7", "b7a7", orig["b7a7"])):
        port_value = int(lines.get(key, -1))
        v.check(port_value == int(value), f"DS:{addr} agrees ({name})",
                f"original {int(value)}, port {port_value}")
    v.screen(f"the screen at the end ({name})", end_shot, end_screen, masks=MASKS, work=work)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--flows", default=",".join(FLOWS),
                        help="which flows: " + ", ".join(FLOWS))
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_treasure")
    v = checklib.Verdict()
    glb = (dqk_lib.ASSETS / "DISK3" / "ECL.GLB").read_bytes()
    for name in opts.flows.split(","):
        if name not in FLOWS:
            parser.error(f"flow {name}: one of {', '.join(FLOWS)}")
        patched = hlib.replace(glb, JUMP, dqkecl.assemble(start_block(FLOWS[name]["extra"])))
        run_flow(v, work, patched, name, opts.verbose)
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

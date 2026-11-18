"""The Dark Queen of Krynn's combat state in the original, for the fight checks
(dqk_fight.py and the later combat parts' checks): what a battle's records,
combatants, battlefield, turn and RandSeed hold, read from the original's data
segment (DS is the data segment of cokbox's Box(game="dark-queen"), as
dqk_lib reads it), and the traps that stop the battle's routines in the
overlay buffer.

Each offset below was checked against the code that reads or writes it in
build/DQK_FULL.EXE (Ghidra's numbering; the disassembly of the handlers that
the ECL's opcodes reach is objdump's, dis.sh):

  DS:c6e5  the party's list, a far pointer to the first record, the next at +0.
           The monsters are appended to it by 6c70:29fc, which COMBAT's monsters
           branch reaches through 6c70:2d82 (one call per icon slot, from slot 8),
           and by 6c70:170e (MONCHA id DS:75aa[2 * var 0x3a], 10 records, the
           GORGONs of a fight with var 0x3a = 0), not by LOAD MONSTER: LOAD MONSTER
           (6c70:29a3, opcode 0x0b) only registers an icon slot. A record is 0x19e
           bytes (rules_dark_queen.c's dark_queen_character). Placement (59c7:1d34-
           1d94) unlinks and frees the monsters it does not place (size 0, combat
           +0x14 == 1), so the list at the first turn has only the placed ones.
  +0x40    the record's combat record, a far pointer to 0x18 bytes (59c7:0f7b:
           1df9:02fc(&record+0x40, 0x18), then 10d7:27a6 zeroes it); outside combat
           NULL. (5045:05e6's loop calls 148f:0512 with event 0x18: the effects'
           dispatch, not an allocation.)
  DS:bd4e  a byte set to 1 by LOAD MONSTER (6c70:29f6, "monsters loaded") and
           cleared by CLEARMONSTERS (5f07:07fe, through 75cc:151b, with DS:c6f1).
  DS:c6f1  a byte: the monsters appended to the list, counted by 29fc (cap 0x28)
           and 170e (cap 0x32); placement takes nothing off it (12 at the first
           turn: 2 SHARKMEN and 10 GORGONs, of which 10 are dropped).
  DS:ad6b  a byte: the next icon slot, which LOAD MONSTER fills (the slots'
           id, count and icon at DS:c7c4, c7ca, c7be + slot; 6c70:29a3 stops at
           0x0e); 2d82 reads the slots from 8 up to DS:ad6b. 170e takes the next
           slot for its extras (6c70:17db, then ad6b++ at 1a3b).
  DS:c2ed  a byte: the undead count, incremented once per slot by 29fc (2ad6)
           when the slot's monster is undead (+0x5c), and cleared by CLEARMONSTERS
           (75cc:151b).
  DS:bd99 + 4n  a far pointer to combatant n's record, n from 1 (the turn
           list; 5045:059d's loop reads it as bd99 + 4 * DS:bd2d, 0x48 the last).
  DS:c3ef  a byte: set to 1 by placement (59c7:196d) and raised by 1 for each
           combatant (1d2b, 1da3): the combatants' count plus one after placement
           (9 with 8 combatants at the first turn). 5837:0330 loops n = 1 .. c3ef
           inclusive; the entries past the count are stale (not compared).
  DS:c3f1 + 6n  combatant n's position: x and y, words (5837:0330 writes
           [c3f1+6n] + dx and [c3f3+6n] + dy into the occupants), then a byte
           at +4 (not read here), and the size at +5 (c3f6+6n; 0 = not on the
           map, 5837:0330's test).
  DS:b7fd  the occupants: 50 by 25 bytes, the combatant index n at
           y * 50 + x (5837:0330 writes `(y)*0x32 + -0x4803 + x`, the
           -0x4803 being 0xb7fd).
  DS:bedb  the battlefield, a far pointer: +2 the view's x and +4 its y
           (5837:0330 takes x - [+2] and y - [+4]), the cells 50 by 25 at +9,
           cell (x, y) at y * 50 + x + 9 (5837's and 52db's uses of 0xbedb).
  DS:bd2d  the turn: the combatant index whose turn it is (5045:059d sets 1,
           then 5045:0726 counts it up to 0x48).
  DS:bd9d  combatant 1's entry, DS:bd99 + 4 * 1 (not a separate pointer).
  DS:b7f2, DS:b7f3  the sides that can still act: the party's and the
           enemy's, bytes; the battle goes on while both are nonzero
           (5045:059d, 52db's checks).
  DS:ae4c  the round count (5045:0f6c adds 1 at the round's end), and
  DS:b45a  a byte the round is compared with: set to DS:ae4c + 0x0f by
           5837:09ab (57183), and a round's loop ends once b45a <= ae4c
           (5045:059d's test, 56681; 52db:0ffc, 18d6 too).
  DS:ae57  the mode, a word: 5 in a battle (5045:059d).
  DS:ca0e  RandSeed, a dword (dqk_lib.DS_SEED; 2102:0234).

The slot tables: DS:c7be + slot (icon), DS:c7c4 + slot (id) and DS:c7ca + slot
(count), slots 8 to 13 (6c70:29ac-29ee); 2d82 reads them from 8 up to DS:ad6b.
"""
import pathlib
import re
import struct
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402

cokbox = checklib.cokbox

DS_PARTY = dqk_lib.DS_PARTY              # DS:c6e5
DS_MONSTERS_LOADED = 0xBD4E              # DS:bd4e
DS_MONSTER_COUNT = 0xC6F1                # DS:c6f1
DS_ICON_SLOT = 0xAD6B                    # DS:ad6b
DS_UNDEAD = 0xC2ED                       # DS:c2ed, the undead count (Turn's)
DS_COMBATANT_LIST = 0xBD99               # DS:bd99 + 4n
DS_FIRST = 0xBD9D                        # DS:bd9d
DS_COMBATANT_COUNT = 0xC3EF              # DS:c3ef
DS_POSITIONS = 0xC3F1                    # DS:c3f1 + 6n
DS_OCCUPANTS = 0xB7FD                    # DS:b7fd, 50 x 25
DS_MAP = 0xBEDB                          # DS:bedb, a far pointer
DS_TURN = 0xBD2D                         # DS:bd2d
DS_SIDE_PARTY, DS_SIDE_ENEMY = 0xB7F2, 0xB7F3
DS_ROUND, DS_ROUND_LIMIT = 0xAE4C, 0xB45A
DS_MODE = 0xAE57                         # DS:ae57, a word
DS_SEED = dqk_lib.DS_SEED                # DS:ca0e, a dword

RECORD = 0x19E                           # rules_dark_queen.c's dark_queen_character.size
COMBAT_RECORD = 0x18                     # 5045:...'s 1df9:02fc(..., 0x18, ...)
COMBAT_AT = 0x40                         # the record's far pointer to it
MAP_WIDTH, MAP_HEIGHT, MAP_CELLS = 50, 25, 9  # cells at +9
COMBATANTS = 0x48                        # the turn list, n = 1 .. 0x48
CHAIN_MAX = 0x80                         # a bound on the list (a loop is a bug)
POSITION_STRIDE = 6
# The sound. DQK.CFG's u16 at 2 is the sound (INSTALL's list: 6 is none); the
# sound driver's setup (1fd3:000b) compares DS:acd8 with 6 and skips the driver
# for it. Fights run with it: the original's draws before the first turn are
# then the same in every run (round 4: five runs, the same callers).
DQK_CFG = "DISK1/DQK.CFG"
SOUND_NONE = 6
# The first turn's RandSeed: dqk_ecl.SEED after 11 draws (2102:0234).
FIRST_TURN_DRAWS = 11
FIRST_TURN_SEED = 0x1756582C


def far(box, off):
    """The far pointer at DS:OFF as (segment, offset), None if nil."""
    return dqk_lib.far(box, off)


def files(patched):
    """The files a fight's box is made with: the block's ECL.GLB, and DQK.CFG
    with the sound off (cokbox.Box's files: DISK1\\DQK.CFG written over the
    copy)."""
    cfg = bytearray((dqk_lib.ASSETS / "DISK1" / "DQK.CFG").read_bytes())
    cfg[2:4] = struct.pack("<H", SOUND_NONE)
    return {"DISK3/ECL.GLB": patched, DQK_CFG: bytes(cfg)}


def draws(start, end, limit=100000):
    """How many RandSeed draws take START to END (2102:0234: seed * 0x6d25 + 1,
    32 bits), None if they do not within LIMIT."""
    x = start & 0xFFFFFFFF
    for n in range(limit):
        if x == end & 0xFFFFFFFF:
            return n
        x = (x * 0x6D25 + 1) & 0xFFFFFFFF
    return None


# The memory cache's purge, frozen in the box only (freeze_purges). 27fa:0f14 (a far routine, its
# argument 0 or 1 the table to evict from) adds one to its counter, then draws random(3) at 0f2c:
# lcall 2102:0234 (9a 34 02 02 11 in the file; the box relocates the segment), its argument pushed
# at 0f28-0f2b and popped at 0f31. AX is the draw's only result read: the `or ax,ax` at 0f32 picks
# the walk. 0: the LRU order (DS:d6ba from its end, 0f63-0f98); else the block numbers from the top
# (da80-1 down to 0, 0f36-0f61). Both take the first block no handle in DS:d98a refers to
# (27dc at 0f47 and 0f79). Every later write of AX overwrites the draw: 0f3e (mov ax,0x30), 0f6e
# (cbtw, after 0f6a loads AL), 0f70 (mov ax,0x30), and 0f9a (mov al,0) for the no victim. The draw's
# AH is 0 too (random(3) gives 0..2). The callers read AL only (025b's 0329, 05f8's 062e, both as
# `pop cx; or al,al`), and 05f8's and 0652's callers (05f8: 0a46, 0baf and 065c inside 0652; 0652:
# 03f7 and 087b) do the same before any read of BX, CX or DX (their next uses of DX follow the lcall
# 27fa:0d7d they make themselves). 2102:0234 clobbers BX, CX and DX and keeps SI and BP, so AX = 0 with the
# same stack is the whole change.
PURGE_ENTRY = (0x27FA, 0x0F14)
PURGE_DRAW = (0x27FA, 0x0F2C)
RANDOM = (0x2102, 0x0234)                       # 2102:0234, random(n): push bp; mov bp, sp
PURGE_FROZEN = bytes.fromhex("31c0" "909090")   # xor ax, ax; three nops: AX = 0, the same stack


def code(box, seg, off):
    """Ghidra SEG:OFF in the resident image, as dqk_menus' freeze_cycles locates 261b:05be."""
    return (box.ds - 0x422E + seg) * 16 + off


def freeze_purges(box):
    """The memory cache's purge draws nothing, in the box only: the draw at 27fa:0f2c is replaced
    by AX = 0 (the walk of DS:d6ba from its end, the LRU order), so the purge still evicts a block (the counters
    DS:d9ba and DS:da8c still count it) but RandSeed is not drawn. Checked by the bytes first, as
    they are in the box (the lcall's segment is relocated there, to the paragraph of 2102): BoxError
    if the entry's push bp, the draw's lcall or 2102:0234's push bp differ. Call it after the
    block's entry (the image is resident; the box is paused there), before the seed is set."""
    draw = bytes([0x9A]) + struct.pack("<HH", RANDOM[1], (box.ds - 0x422E + RANDOM[0]) & 0xFFFF)
    if box.read(code(box, *PURGE_ENTRY), 1) != b"\x55" or \
            box.read(code(box, *PURGE_DRAW), len(draw)) != draw or \
            box.read(code(box, *RANDOM), 1) != b"\x55":
        raise cokbox.BoxError("27fa:0f14 is not where it was expected")
    box.write(code(box, *PURGE_DRAW), PURGE_FROZEN)


def frozen_at_trap(box):
    """At a fight flow's trap: the draw's five bytes are still PURGE_FROZEN, so the patch is in
    place even where no purge happened (the counters would show nothing). BoxError otherwise."""
    got = box.read(code(box, *PURGE_DRAW), len(PURGE_FROZEN))
    if got != PURGE_FROZEN:
        raise cokbox.BoxError(f"27fa:0f2c is not frozen at the trap: {got.hex()}")
    return got


def read_far(box, ptr, size):
    seg, off = ptr
    return box.read(seg * 16 + off, size)


def signed(b):
    return b - 256 if b > 127 else b


def records(box):
    """The party's list (party members, then the monsters LOAD MONSTER
    appended): [(pointer, raw record bytes)] in list order."""
    out, ptr = [], far(box, DS_PARTY)
    while ptr is not None:
        if len(out) >= CHAIN_MAX:
            raise cokbox.BoxError("the party's list does not end")
        raw = read_far(box, ptr, RECORD)
        out.append((ptr, raw))
        off, seg = struct.unpack_from("<HH", raw, 0)
        ptr = None if (off, seg) == (0, 0) else (seg, off)
    return out


def effect_chains(box):
    """Each record's effect chain, in list order: [[id, minutes, value, flag], ...] per node in
    chain order (the record's chain pointer at +4; a node is 9 bytes: +0 id, +1 minutes (a word),
    +3 value, +4 the flag that runs its handler on removal (0 or 1), +5 the next node's far
    pointer). eclplay's --effects writes the same thing; dqk_fight.py's chains_agree compares."""
    out = []
    for _, raw in records(box):
        off, seg = struct.unpack_from("<HH", raw, 4)
        nodes = []
        while (off, seg) != (0, 0):
            if len(nodes) >= CHAIN_MAX:
                raise cokbox.BoxError("a record's effect chain does not end")
            node = read_far(box, (seg, off), 9)
            nodes.append([node[0], struct.unpack_from("<H", node, 1)[0], node[3],
                          1 if node[4] else 0])
            off, seg = struct.unpack_from("<HH", node, 5)
        out.append(nodes)
    return out


def combat_record(box, raw):
    """A record's combat record: (pointer, 0x18 bytes), None for a NULL pointer
    (outside combat)."""
    off, seg = struct.unpack_from("<HH", raw, COMBAT_AT)
    if off == 0 and seg == 0:
        return None
    return (seg, off), read_far(box, (seg, off), COMBAT_RECORD)


def combatants(box):
    """Combatant n's entry, n from 1 to 0x48: its record's pointer (None if
    NULL), its position (x, y words as signed bytes, the raw 6 bytes) and its
    size, from DS:bd99 + 4n and DS:c3f1 + 6n."""
    out = []
    for n in range(1, COMBATANTS + 1):
        raw_ptr = box.ds_read(DS_COMBATANT_LIST + 4 * n, 4)
        off, seg = struct.unpack("<HH", raw_ptr)
        pos = box.ds_read(DS_POSITIONS + POSITION_STRIDE * n, POSITION_STRIDE)
        x, y = struct.unpack_from("<hh", pos, 0)
        out.append(dict(n=n, record=None if (off, seg) == (0, 0) else f"{seg:04x}:{off:04x}",
                        x=x, y=y, size=pos[5], raw=pos.hex()))
    return out


def turn_order(box, records):
    """The turn list (5045:0447's: DS:bd99 + 4n, n from 1 to 0x48), each entry that is not
    empty as eclplay's --turns writes it: (n, index, initiative), index the record's place in
    `records` (the party list's order, from records(box)) and initiative the combat block's
    +0x03, signed. Returns the list in n's order."""
    places = {ptr: i for i, (ptr, _) in enumerate(records)}
    out = []
    for n in range(1, COMBATANTS + 1):
        ptr = far(box, DS_COMBATANT_LIST + 4 * n)
        if ptr is None:
            continue
        if ptr not in places:
            raise cokbox.BoxError(f"turn list entry {n} ({ptr[0]:04x}:{ptr[1]:04x}) is not a "
                                  "record of the party's list")
        combat = combat_record(box, records[places[ptr]][1])
        if combat is None:
            raise cokbox.BoxError(f"turn list entry {n}'s record has no combat record")
        out.append((n, places[ptr], signed(combat[1][3])))
    return out


def battlefield(box):
    """The battlefield: the view (+2, +4), the header's cursor, cursor size and see-all
    (+6, +7, +8), the cells (50 by 25 at +9) and the occupants (DS:b7fd, 50 by 25). Cells
    and occupants as hex text."""
    ptr = far(box, DS_MAP)
    if ptr is None:
        return None
    head = read_far(box, ptr, MAP_CELLS + MAP_WIDTH * MAP_HEIGHT)
    return dict(far=f"{ptr[0]:04x}:{ptr[1]:04x}", view=[head[2], head[4]],
                header=[head[6], head[7], head[8]], cells=head[MAP_CELLS:].hex(),
                occupants=box.ds_read(DS_OCCUPANTS, MAP_WIDTH * MAP_HEIGHT).hex())


def state(box):
    """Everything the fight checks compare, as JSON-ready values: the records
    (raw, and the combat record), the combatants, the battlefield, the turn,
    the sides, the round, the mode, the monsters' flag and count, RandSeed."""
    recs = []
    for ptr, raw in records(box):
        combat = combat_record(box, raw)
        recs.append(dict(at=f"{ptr[0]:04x}:{ptr[1]:04x}", raw=raw.hex(),
                         combat=None if combat is None else
                         dict(at=f"{combat[0][0]:04x}:{combat[0][1]:04x}", raw=combat[1].hex())))
    return dict(
        records=recs,
        chains=effect_chains(box),
        monsters_loaded=box.ds_byte(DS_MONSTERS_LOADED),
        monster_count=box.ds_byte(DS_MONSTER_COUNT),
        icon_slot=box.ds_byte(DS_ICON_SLOT),
        combatant_count=box.ds_byte(DS_COMBATANT_COUNT),
        combatants=combatants(box),
        first=None if far(box, DS_FIRST) is None else "%04x:%04x" % far(box, DS_FIRST),
        battlefield=battlefield(box),
        turn=box.ds_byte(DS_TURN),
        sides=[box.ds_byte(DS_SIDE_PARTY), box.ds_byte(DS_SIDE_ENEMY)],
        round=box.ds_byte(DS_ROUND), round_limit=box.ds_byte(DS_ROUND_LIMIT),
        mode=box.ds_word(DS_MODE),
        seed=box.ds_dword(DS_SEED),
    )


def consistent(s):
    """Whether the readings agree with each other: each combatant's record is
    one of the list's records, every combatant on the map is in the occupants at
    its footprint's first cell (the occupants hold n there), and the monsters'
    count matches the list. Returns the problems found (none if they agree)."""
    problems = []
    listed = {r["at"] for r in s["records"]}
    for c in s["combatants"][:s["combatant_count"]]:
        if c["record"] is not None and c["record"] not in listed:
            problems.append(f"combatant {c['n']}: record {c['record']} not in the list")
        if c["size"] and s["battlefield"] is not None and 0 <= c["x"] < MAP_WIDTH \
                and 0 <= c["y"] < MAP_HEIGHT:
            occ = s["battlefield"]["occupants"]
            at = c["y"] * MAP_WIDTH + c["x"]
            if int(occ[2 * at:2 * at + 2], 16) != c["n"]:
                problems.append(f"combatant {c['n']} at {c['x']},{c['y']} not in the occupants")
    return problems


# ------------------------------------------------------------ the traps

def code_at(box, seg, off, span=0x60, size=12):
    """Where the overlay buffer holds the merged image's code at Ghidra SEG:OFF
    (dkk_lib.code_at's windows and votes, over the program's memory from its
    PSP to A0000): the paragraph where most windows agree, n >= 2."""
    lo, hi = box.psp * 16, 0xA0000
    data = box.read(lo, hi - lo)
    votes = {}
    for k in range(0, span, size):
        probe = box._exe_bytes(seg, off + k, size)
        for m in re.finditer(re.escape(probe), data):
            addr = lo + m.start() - k
            if (addr - off) % 16 == 0:
                votes[addr] = votes.get(addr, 0) + 1
    if not votes:
        return []
    most = max(votes.values())
    return [a for a, n in votes.items() if n == most and n >= 2]


class Trap(list):
    """A trap's patched (address, bytes) and the routine it is at."""

    def __init__(self, patched, seg, off):
        super().__init__(patched)
        self.seg, self.off = seg, off


def trap(box, seg, off, until=lambda: True, seconds=60, answer=None):
    """The routine at Ghidra SEG:OFF made to spin where it starts (EB FE), in
    every copy the overlay buffer holds, as the overlay manager loads it afresh,
    until UNTIL() holds (the game runs meanwhile: this loops in Python, patching
    each time the code is found). ANSWER, if given, is called each round (to type
    the keys a battle's text waits for). Returns a Trap; BoxError after SECONDS.

    The copies are looked for (code_at, which reads the whole buffer) until one is
    found; after that each round only reads the copies' first 12 bytes, at the same
    addresses, and patches a copy whose bytes are its routine again (the overlay
    manager loaded it afresh). A copy whose bytes are neither is looked for again."""
    patched = []  # (address, the 2 bytes patched over), each address once
    known = {}     # address -> the routine's 12 bytes as found
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if not known:
            for addr in code_at(box, seg, off):
                known[addr] = box.read(addr, 12)
        for addr, sig in list(known.items()):
            now = box.read(addr, 12)
            if now[:2] == b"\xeb\xfe":
                continue
            if now != sig:
                del known[addr]  # moved or changed: look for the copies again
                continue
            if all(a != addr for a, _ in patched):
                patched.append((addr, now[:2]))
            box.write(addr, b"\xeb\xfe")
        if patched and until():
            return Trap(patched, seg, off)
        if answer is not None:
            answer()
        time.sleep(0.002)
    raise cokbox.BoxError(f"the trap at {seg:04x}:{off:04x} did not hold")


def held(box, handle, seconds=1.0):
    """Whether the spin holds: the patched bytes still EB FE, and the keyboard
    polls (the hook's count: the game waits for no key while it spins) do not move
    over SECONDS of the game's time. Returns (held, polls before, polls after)."""
    before = box.hook("polls")
    box.settle(seconds)
    after = box.hook("polls")
    spins = all(box.read(addr, 2) == b"\xeb\xfe" for addr, _ in handle)
    return spins and before == after, before, after


def untrap(box, handle):
    """The code a trap patched given back, where the spin is still there (the
    overlay manager may have copied the routine afresh since)."""
    moved = [(addr, orig) for addr, orig in handle if box.read(addr, 2) == b"\xeb\xfe"]
    box.write_paused(moved)


def call_then_spin(addresses, seg, off):
    """The writes that make a trap's spin (at each of ADDRESSES, an instruction of 5 bytes or
    more) call real SEG:OFF and then spin after it: lcall SEG:OFF (9A, the offset, the segment)
    over the trap's 5 bytes, EB FE at +5. For write_paused (the CPU stands at the spin's jump)."""
    call = b"\x9a" + struct.pack("<HH", off, seg) + b"\xeb\xfe"
    return [(addr, call) for addr in addresses]


DS_BEGUN = 0x1CB5          # DS:1cb5: drawing goes to the back page (19a4:05ee sets it, 06d3 clears)
FLIP = (0x19A4, 0x06D3)    # the page flip: the back page shown (22b7:0a0b), drawing to the front


def flip(box, handle, seconds=0.6):
    """The original's own page flip (19a4:06d3) run where HANDLE's trap spins (at a far call:
    the keys flow's 5045:066a), then the spin 5 bytes on: what is drawn since the last flip
    (19a4:05ee's begin: 22b7:0849 copies the front page to the back, 22b7:0c21(0) draws to the
    back) is then shown, as at the game's next wait. Returns DS:1cb5 after it (0: flipped)."""
    seg, _ = box.from_ghidra(*FLIP)
    spin = [(addr + 5, box.read(addr + 5, 2)) for addr, _ in handle]
    box.write_paused(call_then_spin([addr for addr, _ in handle], seg, FLIP[1]))
    held(box, spin, seconds)
    return box.ds_byte(DS_BEGUN)


def flip_continue(box, handle, path):
    """At a spin HANDLE (EB FE at a routine's entry, every copy), the original's own page flip (19a4:06d3)
    run and the routine then entered as if untouched: each copy k jumps (EA) to a trampoline in the hook
    TSR's own PSP command tail (PSP:0080 + 16k, unused once it is resident): lcall 19a4:06d3, EB FE. Once
    DS:1cb5 is 0 the screenshot; then, paused, each copy's 5 bytes given back and each trampoline's spin
    made a jmp far to its copy. Returns (DS:1cb5 before, the screenshot)."""
    assert len(handle) <= 8, "the PSP command tail holds eight trampolines"
    begun = box.ds_byte(DS_BEGUN)
    fseg, foff = box.from_ghidra(*FLIP)
    psp = (box.hook_addr - 0x110) // 16
    writes, back, spins = [], [], []
    for k, (addr, orig2) in enumerate(handle):
        off = handle.off
        cseg = (addr - off) // 16
        assert cseg * 16 + off == addr
        orig5 = orig2 + box.read(addr + 2, 3)
        t_off = 0x80 + 16 * k
        t_lin = psp * 16 + t_off
        writes += [(t_lin, bytes([0x9a]) + struct.pack("<HH", foff, fseg) + bytes([0xeb, 0xfe])),
                   (addr, bytes([0xea]) + struct.pack("<HH", t_off, psp))]
        back += [(addr, orig5), (t_lin + 5, bytes([0xea]) + struct.pack("<HH", off, cseg))]
        spins.append((t_lin + 5, b""))
    # trampolines first (not running), then the copies' jumps, paused
    box.write_paused(writes)
    end = time.monotonic() + 60
    while True:
        if time.monotonic() > end:
            raise cokbox.BoxError("the flip's trampoline does not hold (DS:1cb5 stays set)")
        box.settle(0.3)
        if box.ds_byte(DS_BEGUN) == 0 and any(box.read(a, 2) == bytes([0xeb, 0xfe]) for a, _ in spins):
            if held(box, [(a, b"") for a, _ in spins if box.read(a, 2) == bytes([0xeb, 0xfe])], seconds=0.4)[0]:
                break
    shot = box.screenshot(path=path)
    box.write_paused(back)
    return begun, shot


def portable(s):
    """The state as setup.json keeps it. The heap pointers (the records' and the
    combat records' far pointers, the combatants' and the first record's) are not
    the port's heap, so each names its record by the record's index in the list
    (None for NULL), and the records' own next pointer (+0) and combat-record
    pointer (+0x40) are zeroed in their raw bytes; the combat records' addresses and
    the battlefield's far pointer are dropped. Everything else is game state."""
    index = {r["at"]: i for i, r in enumerate(s["records"])}

    def ref(p):
        return None if p is None else index.get(p, "?")

    out = dict(s)
    out["records"] = []
    for r in s["records"]:
        raw = bytearray.fromhex(r["raw"])
        raw[0:4] = bytes(4)
        raw[COMBAT_AT:COMBAT_AT + 4] = bytes(4)
        combat = None if r["combat"] is None else dict(raw=r["combat"]["raw"])
        out["records"].append(dict(raw=raw.hex(), combat=combat))
    out["combatants"] = [dict(c, record=ref(c["record"])) for c in s["combatants"]]
    out["first"] = ref(s["first"])
    if s["battlefield"] is not None:
        out["battlefield"] = dict(s["battlefield"], far="(masked)")
    return out

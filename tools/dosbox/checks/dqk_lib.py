"""The Dark Queen of Krynn's helpers for the DOSBox-X checks (dqk_*.py).

The original runs in cokbox.Box(game="dark-queen"): DQK.EXE from a copy of
DQKAssets/, VGA, the Sound Blaster, screens compared in RGB (RGBScreen).

It polls the keyboard all the time: the logo, the title's scenes and every
menu loop on dqk 251e:00dc (INT 16h function 1, through 10d7:005c) while
they animate or wait, so the hook's "waiting" (polls advancing) holds from
the logo on and says nothing. So the Box types a key once the screen has
settled (Box.settled: two screenshots a quiet spell of the game's time
apart agree; GAMES' polls_always), and it is taken once the game has read
it from the BIOS's buffer (251e:00e7's INT 16h function 0, which the hook
counts as reads). The game's flush (2102:00c4) reads too: a key goes in
only once the screen is still, the game at its menu. Box.keys and the
command line's `cokbox.py run --game dark-queen --keys ...` type so.

The original's memory, as the code that reads it has it (DS 0x422e in
build/DQK_FULL.EXE):

  DS:ae5c  the ECL block's buffer, a far pointer (64b2:0131 clears 0x2ee3
           bytes from its -0x8000)
  DS:b7f9  the ECL's address (5f07:0f46 sets it), DS:b1c7 the opcode
  DS:c6e5  the party's list, a far pointer (64b2:0131 clears it)
  DS:ca41  the display adapter (65fa:00a1; 0x0f VGA's 320x200x256)
  DS:ca0e  the random numbers' seed, a long (2102:0234: seed * 0x6d25 + 1;
           cokbox's GAMES)
  DS:c74c  the copy protection's journal entry and DS:c74d its word, drawn
           at random (65cc:008b); DS:c6f9 the answer, decoded (65cc:0003),
           DS:c723 the word typed, DS:c722 the questions asked
"""
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402

cokbox = checklib.cokbox

ROOT = cokbox.ROOT
ASSETS = ROOT / "DQKAssets"

DS_ECL, DS_PC, DS_OPCODE = 0xAE5C, 0xB7F9, 0xB1C7
DS_PARTY = 0xC6E5
DS_ADAPTER = 0xCA41
DS_SEED = 0xCA0E
DS_ENTRY, DS_WORD, DS_ANSWER, DS_TYPED = 0xC74C, 0xC74D, 0xC6F9, 0xC723
DS_ASKED = 0xC722
DS_VARS = 0xB20F                        # the block variables' far pointer (dqk_ecl.DS_VARS)


def need_assets():
    if not (ASSETS / "DQK.EXE").exists():
        raise cokbox.BoxError("needs The Dark Queen of Krynn in DQKAssets/")
    if not (ROOT / "build/DQK_FULL.EXE").exists():
        raise cokbox.BoxError("needs build/DQK_FULL.EXE: make dqk-merged")


def cstr(raw):
    return raw.split(b"\0", 1)[0].decode("latin-1")


def far(box, off):
    """The far pointer at DS:OFF as (segment, offset), None if nil."""
    o, s = struct.unpack("<HH", box.ds_read(off, 4))
    return None if o == s == 0 else (s, o)


def saved_game_loaded(box):
    """Saved game A is loaded at the start menu: the party's head (DS:c6e5) is set (604b:28f8
    joined its members) and var 0x20 (the party's size) is above 0."""
    variables = box.far(box.ds * 16 + DS_VARS)
    return far(box, DS_PARTY) is not None and box.read(variables + 0x20, 1)[0] > 0


def load_saved_game(box):
    """The start menu's "plta" (saved game A's load), confirmed. Where the keys were lost the
    party is not joined, and Begin would run the block with no party (59c7:1e99 then reads
    records 0, c3ef 1, the origin -3,-3): Escape and "plta" once more, then BoxError."""
    for attempt in range(2):
        if attempt:
            box.keys("\\e")
            box.settled(tolerance=64)
        box.keys("plta")
        box.settled(tolerance=64)
        if saved_game_loaded(box):
            return
    raise cokbox.BoxError("saved game A was not loaded at the start menu (DS:c6e5 nil or var "
                          "0x20 0), plta typed twice")


def protection(box):
    """The copy protection's question and answer in the original's memory:
    (entry, word, the answer)."""
    return box.ds_byte(DS_ENTRY), box.ds_byte(DS_WORD), cstr(box.ds_read(DS_ANSWER, 42))


def ecl_state(box):
    """The ECL's buffer (segment:offset), address and opcode."""
    buf = far(box, DS_ECL)
    return dict(buffer="%04x:%04x" % buf if buf else None, pc=box.ds_word(DS_PC),
                opcode=box.ds_byte(DS_OPCODE))


# The title's loop (dqk 6523:0345) waits for each scene in 6523:000c, called
# from 6523:03a2; the scene's number is the loop's word at [bp-4].
TITLE_RETURN = 0x03A5
TITLE_RETURN_BYTES = bytes.fromhex("598846ff3c1b")  # pop cx; mov [bp-1], al; cmp al, 1b


def title_scene(box):
    """The title's scene the original waits in (6523:0345's [bp-4], found
    up the frames of its last poll of the keyboard), or None where it is
    not waiting in the title's loop."""
    ss, bp = box.hook("pollss"), box.hook("pollbp")
    for _ in range(8):
        if bp == 0:
            return None
        up, ip, cs = struct.unpack("<HHH", box.read(ss * 16 + bp, 6))
        if ip == TITLE_RETURN and box.read(cs * 16 + ip, 6) == TITLE_RETURN_BYTES:
            return struct.unpack("<H", box.read(ss * 16 + up - 4, 2))[0]
        if up <= bp:
            return None
        bp = up
    return None

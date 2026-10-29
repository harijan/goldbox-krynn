"""The Dark Queen of Krynn read headless (--game dark-queen): ecldump over
its ECL.GLB and eclplay's runs of its blocks and title, their text, menus,
screens and 3D view (the screens' digests those tools/dosbox/checks'
dqk_views.py and dqk_boot.py found equal to the original's). Skipped,
saying so, where DQKAssets/ is not there (stdlib only)."""
import hashlib
import os
import pathlib
import re
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ASSETS = ROOT / "DQKAssets"
ECL = ASSETS / "DISK3" / "ECL.GLB"
GAME = ("--game", "dark-queen")
POINTER = (1, 1, 16, 16)    # the mouse driver's pointer: not the port's
PARTY = (136, 8, 176, 120)  # the party's list and status: not drawn yet


def pixels(bmp, masks=()):
    """A 24-bit BMP's pixels, top row first, those in MASKS (x, y, w, h)
    zeroed."""
    data = pathlib.Path(bmp).read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    stride = (width * 3 + 3) & ~3
    out = bytearray()
    for y in range(height):
        row = bytearray(data[offset + (height - 1 - y) * stride:][:width * 3])
        for mx, my, mw, mh in masks:
            if my <= y < my + mh:
                row[3 * mx:3 * (mx + mw)] = bytes(3 * mw)
        out += row
    return bytes(out)


def digest(bmp, masks=()):
    return hashlib.md5(pixels(bmp, masks)).hexdigest()


def characters(data):
    """(offset, size) of each character of a .QSV (dqk 604b:28f8): the
    record, its items of 17 bytes (their count the low word at +8) and its
    effects of 9 (a chain: each one's far pointer, +5, not nil, another
    follows)."""
    at, out = 0x40F, []
    for _ in range(data[0x40D] | data[0x40E] << 8):
        size = 0x19E + 17 * struct.unpack_from("<H", data, at + 8)[0]
        if data[at + 4:at + 8] != bytes(4):
            e = at + size
            while True:
                size += 9
                if data[e + 5:e + 9] == bytes(4):
                    break
                e += 9
        out.append((at, size))
        at += size
    return out


def without_pointers(data):
    """A .QSV's bytes from its party's size with the heap's far pointers
    (each character's next, effects, items but their count, slots and
    combat, and each effect's next) zeroed: what the original's file and
    the port's agree on."""
    data = bytearray(data)
    for at, size in characters(data):
        count = struct.unpack_from("<H", data, at + 8)[0]
        for off, n in [(0, 4), (4, 4), (10, 2), (0x40, 4)] + [(0xC + 4 * i, 4) for i in range(13)]:
            data[at + off:at + off + n] = bytes(n)
        for e in range(at + 0x19E + 17 * count, at + size, 9):
            data[e + 5:e + 9] = bytes(4)
    return bytes(data[0x40D:])


@unittest.skipUnless(ECL.exists(), "needs The Dark Queen of Krynn in DQKAssets/")
class DarkQueenTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def run_tool(self, tool, *args):
        return subprocess.run([str(BUILD / tool), *map(str, args)], capture_output=True,
                              text=True, cwd=ROOT)

    def play(self, *args):
        return self.run_tool("eclplay", *GAME, *args)

    def test_ecldump(self):
        # Every block decodes with the game's table (5f07:0d34): the five
        # vectors' GOTOs first, LOAD FILES with two operands, no 88 13.
        result = self.run_tool("ecldump", *GAME, "--summary", ECL)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), 48)
        self.assertEqual(lines[-1], "Decoded 44780 instructions; "
                         "0 IFs skip an instruction by a different length")
        self.assertIn("ID 67: 448 instructions, vectors 9654 9654 9654 9654 8014", lines[-2])
        listing = self.run_tool("ecldump", *GAME, "--block", 4, ECL).stdout.splitlines()
        self.assertEqual(listing[0], f"; {ECL}: ID 4, 10100 bytes, "
                         "vectors 89df 8a27 89a8 897c 8014")
        for line in ["8066: LOAD FILES 4 4", "81bb: PRESS KEY", "829c: PICTURE 150"]:
            self.assertIn(line, listing)
        listing = self.run_tool("ecldump", *GAME, "--block", 1, ECL).stdout.splitlines()
        self.assertIn('811e: HORIZONTAL MENU [00bf] 5 : "JUMPER" "START" "DARG" "HIZD" "OVER"',
                      listing)
        listing = self.run_tool("ecldump", *GAME, "--block", 10, ECL).stdout.splitlines()
        self.assertIn("8077: FIND NPC s[01db] [00bf]", listing)
        listing = self.run_tool("ecldump", *GAME, "--block", 15, ECL).stdout.splitlines()
        self.assertIn("9474: TREASURE ITEMS 0 0 0 1 : 141", listing)
        # Read as a DAX archive (Champions'), it is not one.
        result = self.run_tool("ecldump", "--summary", ECL)
        self.assertEqual(result.returncode, 1)

    def test_text_and_menu(self):
        # Block 4 for a new game (0x16 0): Palanthas' picture, its text
        # typed into the window, PRESS KEY's Return, the bar's menu read by
        # its letters (Return picks none), LEAVE.
        result = self.play("--set", "22=1", "--keys", r"\r\r\r\rL\r", ASSETS, 4)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "print: YOU ARRIVE IN PALANTHAS AS THE CITY AWAKENS AND "
                         "MERCHANTS AND TOWNSFOLK BEGIN TO CROWD THE STREETS. ALTHOUGH NOTHING "
                         "SEEMS OUTWARDLY WRONG, YOU SENSE A GREAT TENSION AMONG THE CITIZENS.")
        self.assertEqual(lines[1], "menu: ~Press button or enter to continue")
        self.assertIn("menu: ~PALACE ~TEMPLE ~HALL ~SHOP ~INN ~BAR ~LEAVE", lines)
        self.assertIn("choice: 6", lines)
        self.assertIn("print: THE GUARD STOPS YOU, ", lines)
        self.assertEqual(lines[-1], "(out of keys in block 4 at 82fe)")

    def test_continue_by_return(self):
        # "Press [Return] to continue." takes Return alone (1bdb:0495).
        result = self.play("--set", "22=1", "--keys", " x", ASSETS, 4)
        self.assertEqual(result.stdout.splitlines()[-1], "(out of keys in block 4 at 81bc)")

    def test_journal(self):
        result = self.play("--set", "22=1", "--keys", r"\r\r\rP\r\r\r\r\r", ASSETS, 4)
        lines = result.stdout.splitlines()
        self.assertIn("print: This is recorded as journal entry 54.", lines)

    def test_unported(self):
        # Opcodes that need characters stop the run, citing the handler.
        result = self.play("--set", "22=1", ASSETS, 7)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "not yet ported: The Dark Queen of Krynn's opcode 0x36, "
                         "ADD NPC (dqk 6c70:307b)\n")

    def test_characters(self):
        # --character adds .QCH characters (dqk 604b:05fd): block 5's patrol
        # compares a roll with the first one's class level 4 (0x0556) and
        # prints its name (s[0401], a C string at +0x68).
        save = ASSETS / "SAVE"
        for character, lines in [
                ("BALTHIRN.QCH", ["print: BALTHIR NOMOON",
                                  "print:  HELPS YOU EVADE THE THENOL ARMY PATROL.",
                                  "(done in block 5)"]),
                ("ROLANDOF.QCH", ["print: THE THENOL ARMY PATROL ATTACKS YOU!"])]:
            result = self.play("--character", save / character, "--character",
                               save / "WALKERKE.QCH", "--start", "9695", "--set", "bf=0",
                               "--keys", r"\r", ASSETS, 5)
            self.assertEqual(result.stdout.splitlines()[:len(lines)], lines, result.stderr)
        # A file that ends inside its items fails to load.
        short = self.folder / "SHORT.QCH"
        short.write_bytes((save / "ROLANDOF.QCH").read_bytes()[:0x1a0])
        result = self.play("--character", short, ASSETS, 5)
        self.assertEqual(result.returncode, 1)
        self.assertIn("the items end at byte 416 (604b:05fd)", result.stderr)

    def test_import_maps(self):
        # Dark Queen's import of Death Knights' ids (rules_dark_queen.c's
        # three maps) as its switches have them: 604b:1ea3's and 1bf1's
        # tables of keys and handlers, 604b:1cc7's jump table for 2-0x79,
        # each handler a mov si, imm (or xor si, si) and an id no case
        # lists kept.
        merged = self.folder / "DQK_FULL.EXE"
        result = subprocess.run(["python3", ROOT / "tools" / "dqkmerge.py",
                                 ASSETS / "DQK.EXE", merged], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        exe = merged.read_bytes()
        header = struct.unpack_from("<H", exe, 8)[0] * 16

        def word(offset):
            return struct.unpack_from("<H", exe, header + 0x604b * 16 + offset - 0x10000)[0]

        def handler(at, x, end):
            code = exe[header + 0x604b * 16 + at - 0x10000:][:3]
            if code[0] == 0xbe:
                return struct.unpack_from("<H", code, 1)[0] & 0xff
            if code[:2] == b"\x33\xf6":
                return 0
            self.assertEqual(at, end)
            return x

        def cases(keys, count, end):
            return {word(keys + 2 * i): handler(word(keys + 2 * (i + count)), word(keys + 2 * i),
                                                end) for i in range(count)}

        spells = cases(0x1ec2, 0x25, 0x201c)
        words = cases(0x1c10, 0x14, 0x1cc2)
        effects = {x: handler(word(0x1ce3 + 2 * (x - 2)), x, 0x1e9e) for x in range(2, 0x7a)}
        source = (ROOT / "src" / "rules_dark_queen.c").read_text()

        def table(name):
            body = source.split(f"static const uint8_t {name}[][2] = {{")[1].split("};")[0]
            return {int(a, 16): int(b, 16) for a, b in re.findall(r"\{(0x\w+), (0x\w+)\}", body)}

        self.assertEqual(table("dark_queen_import_spells"),
                         {x: y for x, y in spells.items() if x != y})
        self.assertEqual(table("dark_queen_import_words"),
                         {x: y for x, y in words.items() if x != y})
        self.assertEqual(table("dark_queen_import_effects"),
                         {x: y for x, y in effects.items() if x != y})

    def test_create_tables(self):
        # The tables creation reads (src/queen_create_data.c) are the data
        # segment's (DS 0x422e) bytes, each at its offset; the spell
        # table's rows are the first three bytes of 22.
        merged = self.folder / "DQK_FULL.EXE"
        result = subprocess.run(["python3", ROOT / "tools" / "dqkmerge.py",
                                 ASSETS / "DQK.EXE", merged], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        exe = merged.read_bytes()
        header = struct.unpack_from("<H", exe, 8)[0] * 16
        ds = header + 0x422e * 16 - 0x10000
        source = (ROOT / "src" / "queen_create_data.c").read_text()

        def table(name):
            body = source.split(f"const uint8_t {name}[")[1].split("};")[0].split("= {", 1)[1]
            return [int(v, 16) for v in re.findall(r"0x([0-9a-f]{2})", body)]

        for name, at in (("queen_race_classes", 0x328), ("queen_class_alignments", 0x563),
                         ("queen_age_dice", 0x3f3), ("queen_age_limits", 0x4b7),
                         ("queen_score_limits", 0x2b8), ("queen_class_minimums", 0x4fd),
                         ("queen_kits", 0x180d), ("queen_thac0", 0x6359),
                         ("queen_cleric_spells", 0x16cd)):
            values = table(name)
            self.assertEqual(bytes(values), exe[ds + at:ds + at + len(values)], name)
        spells = table("queen_spell_levels")
        self.assertEqual(len(spells), 0x7f * 3)
        for i in range(0x7f):
            self.assertEqual(bytes(spells[3 * i:3 * i + 3]),
                             exe[ds + 0x27e9 + 0x16 * i:][:3], f"spell {i}")

    def test_refused(self):
        # Saved games load from the start menu alone; --load, parties and
        # --play are not ported yet.
        result = self.play("--play", ASSETS, 4)
        self.assertEqual(result.returncode, 2)
        self.assertIn("saved games load from its start menu", result.stderr)
        result = self.play("--load", ASSETS / "SAVGAMA.QSV", ASSETS)
        self.assertEqual(result.returncode, 2)
        # VGA alone.
        result = self.play("--display", "ega", "--start-menu", ASSETS)
        self.assertEqual(result.returncode, 2)
        self.assertIn("drawn in VGA alone", result.stderr)

    def test_config(self):
        # DQK.CFG (6478:0000): its 0x90 bytes from DISK1, or the game ends
        # saying so; installed for EGA it ends too (6478:00e2).
        shipped = (ASSETS / "DISK1" / "DQK.CFG").read_bytes()
        short = self.folder / "short.cfg"
        short.write_bytes(shipped[:0x8f])
        ega = self.folder / "ega.cfg"
        ega.write_bytes(b"\x01\x00" + shipped[2:])
        for config, words in (("missing.cfg", "Error reading configuration file!"),
                              (short, "Error reading configuration file!"),
                              (ega, "Installed for EGA only!")):
            result = self.play("--start-menu", "--config", self.folder / config, "--keys", "Q",
                               ASSETS)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stderr.splitlines()[0], words)

    def test_title(self):
        # Eight scenes, each passed by Space, then the menu: Quit.
        shots = self.folder / "title"
        result = self.play("--start-menu", "--shots", shots, "--keys", r"\k " * 8 + "Q",
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         ["menu: Play Demo Quit", "choice: 2", "(quit to DOS)"])
        self.assertEqual(digest(shots / "005.bmp"), "2aafd853fbc2c01992b442bb946c2e26")
        self.assertEqual(digest(shots / "008.bmp", [POINTER]),
                         "7460a39579c08ddd2d7fadd455184740")

    def test_camp(self):
        # Play: the camp's menu without a party (7368:046f), its buttons
        # read by letter; Load's "load from where?" (604b:2e87), Escape
        # back; Exit From Game, "Exit to DOS?" No, then Yes. The screens
        # are the original's (dqk_menus).
        shots = self.folder / "camp"
        result = self.play("--start-menu", "--shots", shots, "--keys", r"\k\ePmlx\eENEY",
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        alone = ("menu: Create Character Delete Character Add Character Load Saved Game "
                 "Exit From Game")
        self.assertEqual(result.stdout.splitlines(),
                         ["menu: Play Demo Quit", "choice: 0", alone, "choice: 8",
                          "menu: Tdqk Dkk Exit", "choice: 27", alone, "choice: 11", alone,
                          "choice: 11", "(quit to DOS)"])
        self.assertEqual(digest(shots / "002.bmp", [POINTER]),
                         "efb0b5e394057e108ead812d71070c16")
        self.assertEqual(digest(shots / "003.bmp", [POINTER]),  # m: Modify is off
                         "efb0b5e394057e108ead812d71070c16")
        self.assertEqual(digest(shots / "004.bmp", [POINTER]),
                         "73d831f55db6ae1ab8ac2d52feb30b32")
        # A click on Create Character's button.
        result = self.play("--start-menu", "--mouse", "--keys", r"\k\eP\m200,120,1.", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "not yet ported: The Dark Queen of Krynn's Create "
                         "Character (dqk 66e5:2f95)\n")
        # Delete's roster when the save path has none: its message, back.
        result = self.play("--start-menu", "--saves", self.folder, "--keys", r"\k\ePDE",
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("message: No characters to delete.", result.stdout)

    def test_demonstration(self):
        # Demo (or the menu's 70 seconds with no key) runs block 7 to its
        # ADD NPC; Escape typed in it ends it, back to the menu.
        for keys in (r"\k\eD", r"\k\e\t"):
            result = self.play("--start-menu", "--keys", keys, ASSETS)
            self.assertEqual(result.returncode, 1)
            self.assertIn("music: 36", result.stdout.splitlines())
            self.assertEqual(result.stderr, "not yet ported: The Dark Queen of Krynn's opcode "
                             "0x36, ADD NPC (dqk 6c70:307b)\n")
        result = self.play("--start-menu", "--keys", r"\k\eD\k\eQ", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         ["menu: Play Demo Quit", "choice: 1", "menu: Play Demo Quit",
                          "choice: 2", "(quit to DOS)"])

    def test_front_end(self):
        # krynn plays the title and the camp's menu in 256 colours, its
        # frames eclplay's, with the game's own mouse cursor (the shield).
        if not (BUILD / "krynn").exists():
            self.skipTest("no build/krynn (SDL2)")
        shots = self.folder / "krynn"
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        result = subprocess.run([str(BUILD / "krynn"), *GAME, "--no-delay", "--shots",
                                 str(shots), "--keys", r"\k\ePEY"], capture_output=True,
                                text=True, cwd=ROOT, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(digest(shots / "002.bmp", [POINTER]),
                         "efb0b5e394057e108ead812d71070c16")
        self.assertNotEqual(digest(shots / "002.bmp"), "1d62a33cf8633397f4f43349f89fe0fe")
        mine = self.folder / "eclplay"
        self.play("--start-menu", "--mouse", "--shots", mine, "--keys", r"\k\ePEY", ASSETS)
        self.assertEqual(digest(shots / "002.bmp"), digest(mine / "002.bmp"))

    def test_menus(self):
        # VERTICAL MENU (1bdb:1f25): Up wraps to the last, Down back to the
        # first, others beep, Return selects (block 18's "WHO SENT YOU?").
        shots = self.folder / "vertical"
        result = self.play("--set", "22=1", "--start", "83fb", "--shots", shots, "--keys",
                           r"\^\vx\r", ASSETS, 18)
        self.assertIn("choice: 0", result.stdout.splitlines())
        self.assertEqual(digest(shots / "001.bmp"), "0b0ce6b58c9acf368b0b24b808c619f5")
        result = self.play("--set", "22=1", "--start", "83fb", "--keys", r"\v\v\e", ASSETS,
                           18)
        self.assertIn("choice: 2", result.stdout.splitlines())  # Escape keeps it
        # INPUT NUMBER (6c70:2e7c): typed, Backspace, Return; block 1's
        # developers' menu goes to the block typed.
        shots = self.folder / "input"
        result = self.play("--set", "22=1", "--start", "842d", "--shots", shots, "--keys",
                           r"1x\b4\r", ASSETS, 1)
        self.assertIn("input: 14", result.stdout.splitlines())
        self.assertEqual(digest(shots / "002.bmp"), "de5ab1aa892c78efecc6d92498472aa0")

    def test_main_menu_keys(self):
        # Escape at main's menu is Play (1bdb:06c6's filter gives 0x1b,
        # which 5ebd:00ef takes as anything but Demo and Quit), after a
        # demonstration too; an extended key at the title ends it (6523:000c
        # gives its code's low byte, not Space).
        alone = ("menu: Create Character Delete Character Add Character Load Saved Game "
                 "Exit From Game")
        for keys, before in ((r"\k\e\eEY", ["menu: Play Demo Quit", "choice: 27"]),
                             (r"\k\eD\k\e\eEY", ["menu: Play Demo Quit", "choice: 1",
                                                   "menu: Play Demo Quit", "choice: 27"])):
            result = self.play("--start-menu", "--keys", keys, ASSETS)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(),
                             before + [alone, "choice: 11", "(quit to DOS)"])
        result = self.play("--start-menu", "--keys", r"\k\^\kQ", ASSETS)
        self.assertEqual(result.stdout.splitlines(),
                         ["menu: Play Demo Quit", "choice: 2", "(quit to DOS)"])

    def test_input(self):
        # INPUT NUMBER as 2d84:000b reads it: up to the first other
        # character, 16 bits, empty 0; a negative one asked again.
        for keys, number in ((r"12x\r", "12"), (r"70000\r", "4464"), (r"\r", "0"),
                             (r"-5\r7\r", "7"), (r"10h\r", "16"), (r" 12\r", "12")):
            result = self.play("--set", "22=1", "--start", "842d", "--keys", keys, ASSETS, 1)
            self.assertEqual([l for l in result.stdout.splitlines() if l.startswith("input")],
                             [f"input: {number}"], keys)
        # INPUT STRING (6c70:2ec8): upper case, 35 at most, " " for none,
        # stored on Escape too; then block 1's FIND NPC stops.
        for keys, text in ((r"ab\r", "AB"), (r"ab\e", "AB"), (r"\r", " "),
                           ("x" * 40 + r"\r", "X" * 35)):
            result = self.play("--set", "22=1", "--start", "9bfd", "--keys", keys, ASSETS, 1)
            self.assertIn(f"input: {text}", result.stdout.splitlines(), keys)

    def vertical(self, keys, *more):
        """A VERTICAL MENU of six items (I0-I5, from string variables) with
        an empty prompt: four rows (18-21), so that it scrolls."""
        items = [0x300 + 0x10 * i for i in range(6)]
        code = [0x15, 0x01, 0xBF, 0x00, 0x81, 0x00, 0x02, 0x00, len(items)]
        for a in items:
            code += [0x81, a & 0xFF, a >> 8]
        code.append(0)
        sets = [(0x8014 + i, b) for i, b in enumerate(code)]
        for i, a in enumerate(items):
            sets += [(a, ord("I")), (a + 1, ord(str(i)))]
        args = [x for a, b in sets for x in ("--set", f"{a:x}={b:x}")]
        return self.play("--start", "8014", *args, *more, "--keys", keys, ASSETS, 4)

    def test_vertical_scrolls(self):
        # Down past the rows scrolls; PgUp the top row shown, then a page;
        # PgDn the bottom one, then a page; End wraps (1b05:0664).
        def choice(keys, *more):
            result = self.vertical(keys, *more)
            return [l for l in result.stdout.splitlines() if l.startswith("choice")]
        self.assertEqual(choice(r"\v\v\v\v\v\r"), ["choice: 5"])
        self.assertEqual(choice(r"\v\v\v\v\v\#49\r"), ["choice: 2"])  # top shown: 2
        self.assertEqual(choice(r"\v\v\v\v\v\#49\#49\r"), ["choice: 0"])
        self.assertEqual(choice(r"\#51\r"), ["choice: 3"])
        self.assertEqual(choice(r"\#51\#51\r"), ["choice: 5"])
        self.assertEqual(choice(r"\#47\r"), ["choice: 5"])  # Home: up, wrapping
        shots = self.folder / "scroll"
        self.vertical(r"\v\v\v\v\v\r", "--shots", shots)
        top, bottom = pixels(shots / "000.bmp"), pixels(shots / "005.bmp")
        self.assertNotEqual(top, bottom)
        # Clicks: a row makes it current, a click on the current one or on
        # Select takes it.
        self.assertEqual(choice(r"\m20,152,1.\m20,152,1.", "--mouse"), ["choice: 1"])
        self.assertEqual(choice(r"\m20,160,1.\m20,190,1.", "--mouse"), ["choice: 2"])

    def test_bar_clicks(self):
        # A click on a HORIZONTAL MENU's button (block 1's jumper: START at
        # x 64) and on ASK MENU's (its "what do you do?": WORKING at 48).
        result = self.play("--set", "22=1", "--mouse", "--keys", r"\m70,190,1.", ASSETS, 1)
        self.assertIn("choice: 1", result.stdout.splitlines())
        result = self.play("--set", "22=1", "--mouse", "--start", "9ac8", "--keys",
                           r"\m60,190,1.", ASSETS, 1)
        self.assertIn("menu: ~AM I ~WORKING", result.stdout.splitlines())
        self.assertIn("choice: 1", result.stdout.splitlines())

    def test_big_pictures(self):
        # The overland (block 5's PICTURE [3e], BIGPIC 240: 63ae:0321), in
        # mode 3 (its SAVE 0 [0022] from 3D) with the party's place (ALWAYS
        # 29 past 0x25, 0x26: 7065:0011), and the ending (block 60's
        # PROGRAM 1: 6523:03cb, FINAL.TLB).
        bmp = self.folder / "overland.bmp"
        self.play("--set", "22=1", "--set", "16=5", "--set", "3e=f0", "--set", "25=a", "--set",
                  "26=5", "--screen", bmp, ASSETS, 5)
        self.assertEqual(digest(bmp), "fb0bc519ce58e409de84eb27643172c7")
        shots = self.folder / "ending"
        result = self.play("--start", "9c43", "--shots", shots, "--keys", r"\r\r\r", ASSETS,
                           60)
        self.assertIn("music: 39", result.stdout.splitlines())
        self.assertEqual(digest(shots / "002.bmp"), "bdf98d9fcd66eacd24be5012005941fd")

    def test_animation(self):
        # CALL [0003] (5f07:0aba): the frame drawn, record 1 and over it the
        # frame's record past frame 0, then the next; past the last, the
        # loop's first: 0 for picture 0x17 (records 1-3), 1 for 0x46
        # (63ae:0262). Block 4's code from 8014 written: PICTURE, CALLs, EXIT.
        def screen(picture, calls):
            code = [0x0E, 0, picture] + [0x2D, 1, 3, 0] * calls + [0]
            sets = [a for i, b in enumerate(code) for a in ("--set", f"{0x8014 + i:x}={b:x}")]
            bmp = self.folder / f"anim-{picture}-{calls}.bmp"
            result = self.play("--start", "8014", *sets, "--screen", bmp, ASSETS, 4)
            self.assertEqual(result.returncode, 0, result.stderr)
            return digest(bmp)
        for picture, back in ((0x17, 1), (0x46, 2)):
            shots = [screen(picture, n) for n in range(5)]
            self.assertEqual(shots[1], shots[0])  # frame 0: record 1 alone
            self.assertEqual(len({shots[1], shots[2], shots[3]}), 3)
            self.assertEqual(shots[4], shots[back])

    def test_screens_and_views(self):
        # The first screen (block 4: Palanthas' picture and text), block 1's
        # jumper and block 14's arrival in the sea (wall sets 13, 14, 11),
        # and block 18's corridor (its night's backdrop) facing each way.
        shots = self.folder / "first"
        self.play("--set", "22=1", "--set", "1fa=2", "--set", "1fb=2", "--set", "1fc=2",
                  "--shots", shots, "--keys", r"\r", ASSETS, 4)
        self.assertEqual(digest(shots / "000.bmp", [POINTER, PARTY]),
                         "9d526387beb5bd73959dc1e057393688")
        shots = self.folder / "jumper"
        self.play("--set", "22=1", "--shots", shots, "--keys", r"D\r", ASSETS, 1)
        self.assertEqual(digest(shots / "000.bmp", [POINTER, PARTY]),
                         "57cf38dc7bbff0f3a8b5cddb718083a7")
        self.assertEqual(digest(shots / "001.bmp", [POINTER, PARTY]),
                         "0094edc0b3181d061086a056bf2785c5")
        views = {}
        for facing in (0, 1, 2, 3):
            bmp = self.folder / f"hizden-{facing}.bmp"
            self.play("--start", "8065", "--set", "34=6", "--set", "35=d", "--set",
                      f"11={facing}", "--set", "22=1", "--set", "16=1", "--screen", bmp,
                      ASSETS, 18)
            views[facing] = digest(bmp)
        # (CALL 1 draws the status too, 6c70:1685: the square and the time.)
        self.assertEqual(views, {0: "0a976746efa49e955e574a615bf329e6",
                                 1: "9a90511dc44cb98068119db80de00bc4",
                                 2: "584012cb6e5b8a267d80151b2452911b",
                                 3: "78ee9900f8cefeea87b261916a58febd"})

    def test_saved_game(self):
        # Load Saved Game, Tdqk, A: SAVE/SAVGAMA.QSV (604b:2e87, 28f8) and
        # the camp with its party; Down; View (86b8:0b0c) and Exit; Begin
        # Adventuring, the copy protection passed over, block 4's first
        # text with the party's list and the status. The screens are the
        # original's (dqk_party), its cursor too (--mouse).
        shots = self.folder / "loaded"
        result = self.play("--start-menu", "--mouse", "--shots", shots, "--keys",
                           r"\k\ePLTA\vV\eB", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        party = ("menu: Create Character Delete Character Modify Character View Character "
                 "Remove Character Load Saved Game Save Current Game Begin Adventuring "
                 "Exit From Game")
        lines = result.stdout.splitlines()
        self.assertEqual(lines[4:15], ["menu: Tdqk Dkk Exit", "choice: 0", "menu: A",
                                       "choice: 0", party, party, "choice: 5",
                                       "menu: Items Trade Drop Exit", "choice: 7", party,
                                       "choice: 10"])
        self.assertEqual(lines[15], "protection: the journal's word, passed over "
                         "(dqk 65cc:008b)")
        self.assertEqual(digest(shots / "005.bmp"), "31f954e7a44fbb5e834220525b0c98e5")
        self.assertEqual(digest(shots / "007.bmp"), "d0577b581dae6c184f892c088927a6a7")
        self.assertEqual(digest(shots / "009.bmp"), "3b674649f17f2d0b8e5eaa1fe11117ca")

    def test_items(self):
        # View's Items (86b8:0f41) of Roland of Dust: the list with its
        # ready column, Trade and Drop of a readied item refused, "Can't
        # halve that"; the helm unreadied (Rdy), Drop asked and refused,
        # then traded to Timasalin (3ae5's picker: Down, Select); back in
        # the camp Save Current Game as the saved game the original writes
        # (dqk_items compares the screens and the saved game).
        saves = self.folder / "saves"
        saves.mkdir()
        (saves / "SAVGAMA.QSV").write_bytes((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
        keys = r"\k\ePLTAVITDHJ\vRDN" r"T\vSEESA"
        result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout.splitlines()
        self.assertIn("menu: Rdy Trade Drop Halve Join Exit", out)
        self.assertIn("item:  Yes\tLong Sword +2 ", out)
        self.assertIn("item:  No\t Helm +2 ", out)
        messages = [l for l in out if l.startswith("message: ")]
        self.assertEqual(messages, ["message: Must be unreadied", "message: Must be unreadied",
                                    "message: Can't halve that"])
        self.assertIn("menu: Select Exit", out)
        written = (saves / "SAVGAMA.QSV").read_bytes()
        at, counts = 0x40f, []
        for _ in range(written[0x40d]):
            items = written[at + 8]
            counts.append(items)
            effects = any(written[at + 4:at + 8])
            at += 0x19e + 17 * items
            while effects:
                effects = any(written[at + 5:at + 9])
                at += 9
        # The helm went from Roland (4 items) to Timasalin (5, then 6).
        self.assertEqual(counts[:2], [3, 6])

    def test_items_ready_halve_join(self):
        # The second member's Items (Timasalin): his bow readied past two
        # hands refused ("already using Long Sword +2": the weapon's slot,
        # 86b8:1965), his arrows (40) halved into two stacks of 20 (1d13)
        # and joined again (1fda).
        saves = self.folder / "saves"
        saves.mkdir()
        shipped = (ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes()

        def arrows(data):
            at, members = 0x40f, []
            for _ in range(2):
                n = data[at + 8]
                members.append((n, at + 0x19e))
                effects = any(data[at + 4:at + 8])
                at += 0x19e + 17 * n
                while effects:
                    effects = any(data[at + 5:at + 9])
                    at += 9
            n, first = members[1]
            return n, [data[first + 17 * k + 13] for k in range(n) if data[first + 17 * k] == 0x1e]

        for keys, message, expected in ((r"\k\ePLTA\vVI\vR\^HEESA", "already using Long Sword +2 ",
                                         (6, [20, 20])),
                                        (r"\k\ePLTA\vVIHJEESA", None, (5, [40])),
                                        # The sword unreadied, then the bow: a shield and
                                        # two hands more.
                                        (r"\k\ePLTA\vVI\v\vR\^R" r"EESA", "Your hands are full!",
                                         (5, [40]))):
            (saves / "SAVGAMA.QSV").write_bytes(shipped)
            result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
            self.assertEqual(result.returncode, 0, result.stderr)
            if message:
                self.assertIn("message: " + message, result.stdout.splitlines())
            self.assertEqual(arrows((saves / "SAVGAMA.QSV").read_bytes()), expected)

    def test_coins(self):
        # View's Trade (86b8:286a) and Drop (86b8:2c32): 5 steel traded to
        # Timasalin (the picker's Down and Select, the type of coin, the
        # amount typed), Exit asking whom again; then 129 typed and a 9
        # more, which the number cannot pass (195, the steel left), a
        # Backspace and 19 dropped. The saved game holds the coins and the
        # weights (dqk_items compares the screens with the original's).
        saves = self.folder / "saves"
        saves.mkdir()
        shipped = (ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes()
        (saves / "SAVGAMA.QSV").write_bytes(shipped)
        keys = r"\k\ePLTAVT\vSS5\rEEDS1299" r"9\b\rEESA"
        result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("menu: Select Exit", result.stdout)

        def records(data):
            at, out = 0x40f, []
            for _ in range(data[0x40d]):
                out.append(data[at:at + 0x19e])
                items = data[at + 8]
                effects = any(data[at + 4:at + 8])
                at += 0x19e + 17 * items
                while effects:
                    effects = any(data[at + 5:at + 9])
                    at += 9
            return out

        before = records(shipped)
        after = records((saves / "SAVGAMA.QSV").read_bytes())
        steel = lambda r: r[0x4c] | r[0x4d] << 8
        weight = lambda r: r[0x56] | r[0x57] << 8
        self.assertEqual(steel(before[0]) - steel(after[0]), 5 + 19)
        self.assertEqual(steel(after[1]) - steel(before[1]), 5)
        self.assertEqual(weight(before[0]) - weight(after[0]), 5 + 19)
        self.assertEqual(weight(after[1]) - weight(before[1]), 5)

    def test_first_fight(self):
        # To Caergoth (the palace, Leave), its commands and steps (6fea:0365,
        # 0753) along the ruins to the dragons' SETUP MONSTERS, where the
        # port stops (the first fight: step 4's).
        route = (r"\^" * 10 + r"\>\^\^\^\>\^\^\<\^\^\^\>\^\^L\r\v\^\r\^\^\^")
        result = self.play("--start-menu", "--keys",
                           r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r" + route, ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "not yet ported: The Dark Queen of Krynn's opcode 0x0c, "
                         "SETUP MONSTERS (dqk 6c70:2db1)\n")
        moves = [line for line in result.stdout.splitlines()
                 if line.startswith(("step:", "turn:"))]
        self.assertEqual(len(moves), 29)
        self.assertEqual(moves[:3], ["step: 4,2 facing 2", "step: 5,2 facing 2",
                                     "step: 6,2 facing 2"])
        self.assertEqual(moves[-1], "step: 13,8 facing 2")
        self.assertIn("print: HUGE SHADOWS SKIM OVER THE GROUND. GIANT SHAPES DISAPPEAR OVER "
                      "THE TREE TOPS.", result.stdout)

    def test_save_and_remove(self):
        # With --saves: Load A, Save Current Game as B (604b:3395, 31a7):
        # SAVGAMB.QSV the same as SAVGAMA.QSV but for the characters' far
        # pointers, the vault copied to VAULTB.DAT; Remove Character
        # (604b:23a7) writes ROLANDOF.QCH, his bytes in the saved game but
        # his pointers; then Exit asks "Game not saved. Exit anyway?".
        saves = self.folder / "saves"
        saves.mkdir()
        shipped = (ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes()
        (saves / "SAVGAMA.QSV").write_bytes(shipped)
        (saves / "VAULTA.DAT").write_bytes((ASSETS / "SAVE" / "VAULTA.DAT").read_bytes())
        result = self.play("--start-menu", "--saves", saves, "--keys", r"\k\ePLTASBREYY",
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("menu: ABCDEFGHIJ", result.stdout)
        self.assertTrue(result.stdout.endswith("(quit to DOS)\n"), result.stdout)
        written = (saves / "SAVGAMB.QSV").read_bytes()
        self.assertEqual(len(written), len(shipped))

        def masked(data):
            # Each character's far pointers (+0-7, +0xc-0x43: not the items'
            # count at +8) and its effects' next pointers.
            data = bytearray(data)
            at = 0x40f
            for _ in range(data[0x40d]):
                items, effects = data[at + 8], data[at + 4] | data[at + 5]
                data[at:at + 8] = bytes(8)
                data[at + 0xc:at + 0x44] = bytes(0x38)
                at += 0x19e + 17 * items
                while effects:
                    effects = any(data[at + 5:at + 9])
                    data[at + 5:at + 9] = bytes(4)
                    at += 9
            return bytes(data)

        self.assertEqual(masked(written), masked(shipped))
        self.assertEqual((saves / "VAULTB.DAT").read_bytes(),
                         (ASSETS / "SAVE" / "VAULTA.DAT").read_bytes())
        roland = (saves / "ROLANDOF.QCH").read_bytes()
        original = shipped[0x40f:0x40f + len(roland)]  # the saved game's first
        self.assertEqual(roland[0x44:], original[0x44:])
        self.assertEqual(roland[8:12], original[8:12])
        # Remove leaves the game not saved (7368:061b): Exit to DOS's Yes
        # asks "Game not saved. Exit anyway?" (one Y does not quit).
        result = self.play("--start-menu", "--saves", saves, "--keys", r"\k\ePLTASBREY",
                           ASSETS)
        self.assertTrue(result.stdout.endswith("(out of keys in the start menu)\n"),
                        result.stdout)
        # Without --saves the port writes nothing: Save says so.
        result = self.play("--start-menu", "--keys", r"\k\ePLTASB", ASSETS)
        self.assertIn("error: Save: no save path (--saves)", result.stdout)

    def test_add_and_delete(self):
        # Add Character (7368:08dc) from Tdqk: the roster by name, Balthir
        # and Roland added ("* " before them), Exit, the camp with them;
        # Delete Character (7368:0c23): Balthir, "Delete ... forever?" No.
        # The screens are the original's (whole, its cursor too).
        shots = self.folder / "add"
        result = self.play("--start-menu", "--mouse", "--shots", shots, "--keys",
                           r"\k\ePAT\vA\v\v\rED\vDN", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("list: add a character:", result.stdout)
        self.assertEqual(digest(shots / "004.bmp"), "16014acc9126b26203b39e277296d221")
        self.assertEqual(digest(shots / "006.bmp"), "1e98bb6ac691dd6c6a321666070171c9")
        self.assertEqual(digest(shots / "010.bmp"), "a0e66bfa3329d4c376d75bf50a5bd0e0")
        self.assertEqual(digest(shots / "013.bmp"), "0cf157a7c2e6d764a07229d5ec619d28")
        # Delete Yes, Yes erases the file from --saves.
        saves = self.folder / "saves"
        saves.mkdir()
        for name in ("ROLANDOF.QCH", "WALKERKE.QCH"):
            (saves / name).write_bytes((ASSETS / "SAVE" / name).read_bytes())
        result = self.play("--start-menu", "--saves", saves, "--keys", r"\k\ePDDYYE", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(p.name for p in saves.iterdir()), ["WALKERKE.QCH"])

    def patched_saves(self, hall, experience):
        """SAVE copied with saved game A's variable 0x30 (the training
        hall's classes) and its characters' experience (index: value)
        patched, as tools/dosbox/checks/dqk_train.py does."""
        saves = self.folder / "saves"
        saves.mkdir()
        for path in (ASSETS / "SAVE").iterdir():
            (saves / path.name).write_bytes(path.read_bytes())
        data = bytearray((saves / "SAVGAMA.QSV").read_bytes())
        data[0x2F] = hall
        chars = characters(data)
        for index, value in experience.items():
            struct.pack_into("<I", data, chars[index][0] + 0x44, value)
        (saves / "SAVGAMA.QSV").write_bytes(data)
        return saves

    def test_train_and_knight_order(self):
        # Load A with the hall open to every class and Roland (a knight
        # of the Crown), Roark and Walker holding the experience of the
        # next level: Knight new Order (7368:10c2: the sword, then the
        # rose), Train Character (6be8:0022) for Roland, Roark (his
        # experience held below the level after's), Walker and Timasalin
        # ("Not Enough Experience"), the same seed (DS:ca0e) as the
        # original's run (dqk_train), whose SAVGAMA.QSV this writes but
        # for the heap's far pointers.
        saves = self.patched_saves(0x7F, {0: 1200001, 4: 1000001, 5: 700000})
        keys = r"\k\ePlta" + "ty" + "kcke" + r"\v" * 4 + "ty" + r"\v" + "ty" + r"\^" * 4 + "tsa"
        result = self.play("--start-menu", "--seed", 5, "--saves", saves, "--keys", keys,
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        for line in ("a level 12 Knight", "knight of the sword", "knight of the rose",
                     "a level 10 Fighter", "a level 13 Thief", "Not Enough Experience"):
            self.assertIn(line, result.stdout)
        saved = (saves / "SAVGAMA.QSV").read_bytes()
        self.assertEqual(len(saved), 7965)
        self.assertEqual(hashlib.md5(without_pointers(saved)).hexdigest(),
                         "135c38c7993e6be4615f294571df97f4")

    def test_knight_too_inexperienced(self):
        # A knight of the Crown below the third knight level, though the
        # experience asked: "is too inexperienced" (7368:10c2), nothing
        # to change.
        saves = self.patched_saves(0x7F, {0: 1200001})
        data = bytearray((saves / "SAVGAMA.QSV").read_bytes())
        data[characters(data)[0][0] + 0xA6] = 2
        (saves / "SAVGAMA.QSV").write_bytes(data)
        result = self.play("--start-menu", "--saves", saves, "--keys", r"\k\ePltak", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("print: ROLAND OF DUST is too inexperienced", result.stdout)
        self.assertNotIn("may become", result.stdout)

    def test_train_hall_of_knights(self):
        # The hall of knights only (variable 0x30 = 0x40): Roark, a
        # fighter and thief, is told "we don't train that class here"
        # and nothing is written but what Save wrote.
        saves = self.patched_saves(0x40, {4: 1000001})
        before = (saves / "SAVGAMA.QSV").read_bytes()
        result = self.play("--start-menu", "--saves", saves, "--keys",
                           r"\k\ePlta" + r"\v" * 4 + "t", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("we don't train that class here", result.stdout)
        self.assertEqual((saves / "SAVGAMA.QSV").read_bytes(), before)

    @unittest.skipUnless((ROOT / "DKKAssets" / "SAVE" / "SAVGAMA.PTY").exists(),
                         "needs Death Knights' saved games in DKKAssets/SAVE")
    def test_death_knights_game(self):
        # Load, Dkk (604b:2bcf): Death Knights' saved game A from --import,
        # its six characters imported and joined, the first selected.
        result = self.play("--start-menu", "--import", ROOT / "DKKAssets" / "SAVE", "--keys",
                           r"\k\ePLDAV\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[4:9], ["menu: Tdqk Dkk Exit", "choice: 1", "menu: AB",
                                      "choice: 0",
                                      "menu: Create Character Delete Character Modify "
                                      "Character View Character Remove Character Load Saved "
                                      "Game Save Current Game Begin Adventuring Exit From "
                                      "Game"])

    def saved_game(self, data, name="SAVGAMA.QSV"):
        saves = self.folder / "edited"
        saves.mkdir(exist_ok=True)
        (saves / name).write_bytes(bytes(data))
        return saves

    def test_loaded_locals(self):
        # A loaded game's first block keeps its locals 0x97-0xc8 (6c70:0001
        # with DS:b1ed set by 604b:2e87): 0xc8 written in the saved game is
        # still there at block 4's first text (eclplay --vars).
        data = bytearray((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
        data[0xc8 - 1] = 0x5a
        saves = self.saved_game(data)
        out = self.folder / "vars"
        result = self.play("--start-menu", "--saves", saves, "--vars", out, "--keys",
                           r"\k\ePLTAB", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(out.read_bytes()[0xc8 - 1], 0x5a)

    def test_loaded_scroll_and_short(self):
        # A saved game whose first character's first item is a scroll
        # holding a spell (604b:05fd): its contents read past and dropped,
        # "SCROLLS DROPPED!"; a party's size past its characters: the load
        # fails at the first missing one, cited.
        shipped = (ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes()
        at = 0x40f + 0x19e  # Roland's first item
        item = bytearray(shipped[at:at + 17])
        item[0] = 0x49
        item[0x35 - 0x28] = 1
        data = bytearray(shipped[:at]) + item + bytes(17) + shipped[at + 17:]
        data = data[:0x1f1d]
        result = self.play("--start-menu", "--saves", self.saved_game(data), "--keys",
                           r"\k\ePLTA", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("print: SCROLLS DROPPED!", result.stdout)
        data = bytearray(shipped)
        data[0x40d] = 0x40  # the item table after them read as characters, until short
        result = self.play("--start-menu", "--saves", self.saved_game(data, "SAVGAMB.QSV"),
                           "--keys", r"\k\ePLTB", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("SAVGAMB.QSV: character 9: the items end at byte 2815 (604b:05fd)",
                      result.stderr)


if __name__ == "__main__":
    unittest.main()

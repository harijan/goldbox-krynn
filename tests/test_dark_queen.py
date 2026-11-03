"""The Dark Queen of Krynn read headless (--game dark-queen): ecldump over
its ECL.GLB and eclplay's runs of its blocks and title, their text, menus,
screens and 3D view (the screens' digests those tools/dosbox/checks'
dqk_views.py and dqk_boot.py found equal to the original's). Skipped,
saying so, where DQKAssets/ is not there (stdlib only)."""
import hashlib
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import hlib  # noqa: E402
BUILD = ROOT / "build"
ASSETS = ROOT / "DQKAssets"
ECL = ASSETS / "DISK3" / "ECL.GLB"
GAME = ("--game", "dark-queen")
POINTER = (1, 1, 16, 16)    # the mouse driver's pointer: not the port's
PARTY = (136, 8, 176, 120)  # the party's list and status: not drawn yet
FIGHTER_SCORES, FIGHTER_SIZE = "scores: 16 15 15 15 14 16 (0) age 29", 499
FIGHTER_DIGEST = "e2f5d1e48d7031fce6f4a6f2f20b1eaa"
# The adventure's keys from Begin Adventuring to the dragons' SETUP MONSTERS: to
# Caergoth (the palace, Leave), its commands and steps along the ruins.
TO_FIGHT = r"\^" * 10 + r"\>\^\^\^\>\^\^\<\^\^\^\>\^\^L\r\v\^\r\^\^\^"
FORM_DIGESTS = ["4bdabc0f2321fa13894e7f6a39f629ce", "b2799d8d1e3148afdfaceb811822cab0",
                "a48ac17deba6bf6d808c030b98e619a7", "013a9c8c6d86d460556ec3e8070d0ccd",
                "afddccf7a4452e69e351c8e8ce6f7401", "df450e074f4a296cb043095b2f88eb90",
                "60bd67b3551e42ea06c76a0f19cb022b", "d175dc92800fe291ce9b2e4985e5b392"]


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
        # Parties from other saved games are not ported yet; --play needs a
        # party to begin the adventure with (the camp's Begin is off without one).
        result = self.play("--play", ASSETS, 4)
        self.assertEqual(result.returncode, 1)
        self.assertIn("--play: Begin Adventuring needs a party", result.stderr)
        result = self.play("--party", ASSETS / "SAVE" / "SAVGAMA.QSV", ASSETS)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--party and --test-party are not ported yet", result.stderr)
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
        # A click on Create Character's button opens its form (the creation
        # starts with the generator's seed said).
        result = self.play("--start-menu", "--mouse", "--keys", r"\k\eP\m200,120,1.", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"seed: [0-9a-f]{8}\n\(out of keys")
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

    def vertical(self, keys, *more, number=False):
        """A VERTICAL MENU of six items (I0-I5, from string variables) with
        an empty prompt: four rows (18-21), so that it scrolls. NUMBER makes
        the first item the immediate 7 (its type 0), the rest as before."""
        items = [0x300 + 0x10 * i for i in range(6)]
        code = [0x15, 0x01, 0xBF, 0x00, 0x81, 0x00, 0x02, 0x00, len(items)]
        for i, a in enumerate(items):
            code += [0x00, 7] if number and i == 0 else [0x81, a & 0xFF, a >> 8]
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

    def test_vertical_store_takes_the_first_items_width(self):
        # The choice is stored as wide as the first item (1bdb's 6c70:1e91
        # reads DS:b216, the first item's type): a number first makes a
        # byte store, which leaves the byte after the header variable (0xc0)
        # as it was; strings first make a word, clearing it.
        for number, after in ((True, 0x55), (False, 0)):
            varfile = self.folder / f"vars{number}"
            result = self.vertical(r"\v\r", "--set", "c0=55", "--vars", varfile,
                                   number=number)
            self.assertIn("choice: 1", result.stdout.splitlines())
            variables = varfile.read_bytes()
            self.assertEqual(variables[0xBF - 1], 1)
            self.assertEqual(variables[0xC0 - 1], after, number)

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

    def test_lost_party_view(self):
        # A lost party leaves the view black: block 18's corridor (LOAD FILES at 8065), then
        # COMBAT with no party and no monsters (0593 keeps DS:b7a7: the lost party, 1460, its
        # continue prompt answered) and COMBAT's ending redraws the screen in mode 4 (177e:1b5d),
        # whose view (5e22:0000) draws nothing while DS:b7a7 is set (5e22:0000-0005).
        bmp, state = self.folder / "lost-view.bmp", self.folder / "lost-view.state"
        result = self.play("--start", "8065", "--set", "806a=24", "--set", "806b=0", "--set",
                           "34=6", "--set", "35=d", "--set", "22=1", "--set", "16=1", "--keys",
                           r"\r", "--state", state, "--screen", bmp, ASSETS, 18)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("b7a7 1", state.read_text().splitlines())
        self.assertIn("mode 4", state.read_text().splitlines())
        data = pixels(bmp)
        view = {data[3 * (320 * y + x):][:3] for y in range(24, 112) for x in range(24, 112)}
        self.assertEqual(view, {bytes(3)})

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

    def test_camp_load_from_adventure(self):
        # Encamp's Load (7070:2efe) asks "Game not saved. Load anyway? " (Yes); the run
        # ends (DS:acc4, DS:b7a7) and main takes the camp from main again (5ebd:0331, 1d99:1175
        # with 1): its Load (Yes to its question, Tdqk, A) loads saved game A, and Begin (B)
        # takes the game on from its block (10cd), with no copy protection question (only
        # 5ebd:00ef's Begin asks it).
        start = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        saves = self.folder / "load"
        shutil.copytree(ASSETS / "SAVE", saves)
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--keys",
                           start + "Ely" + "TAB", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("not yet", result.stderr)
        lines = result.stdout.splitlines()
        camp = ("menu: Create Character Delete Character Modify Character View Character "
                "Remove Character Load Saved Game Save Current Game Begin Adventuring "
                "Exit From Game")
        encamp = "menu: View Magic Rest Alt Fix Load Save Exit"
        at = lines.index(encamp)
        self.assertEqual(lines[at:at + 7], [encamp, "choice: 5", "menu: Tdqk Dkk Exit",
                                            "choice: 0", "menu: A", "choice: 0", camp])
        self.assertEqual(lines[at + 7], "choice: 10")  # Begin: the block's first text next
        self.assertTrue(lines[at + 8].startswith("print: YOU ARRIVE IN PALANTHAS"))
        self.assertEqual(sum(line.startswith("protection:") for line in lines), 1)

    def test_camp_alt(self):
        # The adventure's camp at Caergoth's three steps west: Encamp (e), then Alt
        # (7070:1ce2) with Speed (1bab) Slower, Level (1b15) Champion, Order (19a3)
        # Select, Down (the member moved down), Place, Exit, Drop (7368:0ee9) of the
        # selected member (ROLAND, Yes and Yes: "bids you farewell"), Exit, and the
        # camp's Exit. The menus and choices in the log; the variables after: the speed
        # (0x12, from 4), the party's size (0x20, from 6) and the difficulty (0x27).
        start = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        keys = start + r"Eas" + r"se" + r"lc" + r"os\vped" + r"yye\e"
        saves = self.folder / "saves"
        shutil.copytree(ASSETS / "SAVE", saves)
        varfile = self.folder / "alt.vars"
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--vars", varfile,
                           "--keys", keys, ASSETS)
        self.assertNotIn("not yet", result.stderr)
        lines = result.stdout.splitlines()
        camp = "menu: View Magic Rest Alt Fix Load Save Exit"
        alt = "menu: ~Order ~Drop ~Speed ~Icon ~Level ~Exit"
        self.assertIn(camp, lines)
        at = lines.index(camp)
        self.assertEqual(lines[at:at + 29], [
            camp, "choice: 3",
            alt, "choice: 2",
            "menu: ~Faster ~Slower ~Exit", "choice: 1",
            "menu: ~Faster ~Slower ~Exit", "choice: 2",
            alt, "choice: 4",
            "menu: ~Novice ~Squire ~Veteran ~Adept ~Champion", "choice: 4",
            alt, "choice: 0",
            "menu: ~Select ~Exit", "choice: 0",
            "menu: ~Place ~Exit", "choice: 132",
            "menu: ~Place ~Exit", "choice: 0",
            "menu: ~Select ~Exit", "choice: 1",
            alt, "choice: 1",
            "message: ROLAND OF DUST bids you farewell.",
            alt, "choice: 5",
            camp, "choice: 7"])
        variables = varfile.read_bytes()
        self.assertEqual((variables[0x11], variables[0x1F], variables[0x26]), (5, 5, 5))

    def camp_alt_saves(self, name="alt", speed=None):
        """SAVE copied, saved game A's variable 0x12 set to speed when given."""
        saves = self.folder / name
        shutil.copytree(ASSETS / "SAVE", saves)
        if speed is not None:
            data = bytearray((saves / "SAVGAMA.QSV").read_bytes())
            data[0x11] = speed  # the variables' byte n - 1 is variable n
            (saves / "SAVGAMA.QSV").write_bytes(bytes(data))
        return saves

    def test_camp_alt_order(self):
        # Alt's Order (19a3) with ROLAND, the first member, moved up: Up on the first
        # wraps him to the last (179b, as cok_party_move does), Place, Exit. The camp's
        # Save (C, No) then writes the party in its new order: each record's name (the
        # C string at +0x68).
        start = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        saves = self.camp_alt_saves()
        shipped = (saves / "SAVGAMA.QSV").read_bytes()
        names = [self.name_of(shipped, at) for at, _ in characters(shipped)]
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--keys",
                           start + r"Eaos\^pe" + r"e" + "SCN" + r"\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("choice: 136", result.stdout.splitlines())  # Up, while moving
        saved = (saves / "SAVGAMC.QSV").read_bytes()
        self.assertEqual([self.name_of(saved, at) for at, _ in characters(saved)],
                         names[1:] + names[:1])

    @staticmethod
    def name_of(data, at):
        return bytes(data[at + 0x68:at + 0x78]).split(b"\0")[0].decode("latin-1")

    def test_camp_alt_speed_bounds(self):
        # Alt's Speed reads variable 0x12 (7070:1bab), not the game's speed. At 0 the
        # menu is "Slower Exit" alone and Down does nothing; Exit leaves it at 0. At 9 the
        # menu is "Faster Exit" alone and Up does nothing; Faster goes to 8.
        start = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        for speed, keys, menu, pick, want in (
                (0, r"\ve", "menu: ~Slower ~Exit", "choice: 132", 0),
                (9, r"\^fe", "menu: ~Faster ~Exit", "choice: 136", 8)):
            saves = self.camp_alt_saves(f"speed{speed}", speed)
            varfile = self.folder / f"speed{speed}.vars"
            result = self.play("--start-menu", "--mouse", "--saves", saves, "--vars", varfile,
                               "--keys", start + "Eas" + keys + r"\e" + r"\e", ASSETS)
            self.assertEqual(result.returncode, 0, result.stderr)
            lines = result.stdout.splitlines()
            self.assertIn(menu, lines)
            self.assertIn(pick, lines)
            self.assertEqual(varfile.read_bytes()[0x11], want)

    def test_camp_alt_selected_colour(self):
        # Order's Select (177e:156e, the camp's branch): "has been selected" on row 19 in
        # colour 7 (1a62:0311), as the original draws it (170, 170, 170), not colour 10.
        start = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        shots = self.folder / "selected"
        result = self.play("--start-menu", "--mouse", "--saves", self.camp_alt_saves(), "--shots",
                           shots, "--keys", start + "Eaos", ASSETS)
        self.assertIn("menu: ~Place ~Exit", result.stdout.splitlines())
        bmp = sorted(shots.glob("*.bmp"))[-1]
        width = struct.unpack_from("<i", bmp.read_bytes(), 18)[0]
        data = pixels(bmp)
        text = {data[3 * (y * width + x):3 * (y * width + x) + 3]
                for y in range(152, 159) for x in range(9, 144)}
        self.assertIn(bytes((170, 170, 170)), text)
        self.assertNotIn(bytes((85, 255, 85)), text)

    def test_program_0(self):
        # PROGRAM 0 (5f07:0c15) in block 39, the training hall (92f2), with
        # BALTHIRN alone, from the adventure (7368:046f with 0): View and
        # Escape back to the camp's menu, then Begin Adventuring, the block
        # going on to its end. Then Remove the only member (no new game, the
        # menu without a party: 7368:06dc) and Exit From Game, Yes.
        camp = ("menu: Create Character Delete Character Modify Character View Character "
                "Add Character Remove Character Load Saved Game Save Current Game "
                "Begin Adventuring Exit From Game")
        shots = self.folder / "hall"
        result = self.play("--character", ASSETS / "SAVE" / "BALTHIRN.QCH", "--start", "92f2",
                           "--shots", shots, "--keys", "VeB", ASSETS, 39)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [
            camp, "choice: 5", "menu: Items Trade Drop Exit", "choice: 7", camp, "choice: 10",
            "(done in block 39)"])
        saves = self.folder / "saves"
        saves.mkdir()
        result = self.play("--character", ASSETS / "SAVE" / "BALTHIRN.QCH", "--start", "92f2",
                           "--saves", saves, "--keys", "REY", ASSETS, 39)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [
            camp, "choice: 7", "save: BALTHIRN.QCH",
            "menu: Create Character Delete Character Add Character Load Saved Game Exit From Game",
            "choice: 11", "(quit to DOS in block 39)"])
        # Remove the only member (DS:b7a7 set), Add her back (the roster's list
        # is the saves folder's as it was at the start, so the file is there
        # first) and Begin: the ECL run halts at the camp's end (5f07:0fc4), the
        # adventure's loop frees the party (5f07:1338) and main's new game and
        # menu (5ebd:039b) is not ported, so it stops there.
        saves = self.folder / "removed"
        saves.mkdir()
        (saves / "BALTHIRN.QCH").write_bytes((ASSETS / "SAVE" / "BALTHIRN.QCH").read_bytes())
        result = self.play("--character", ASSETS / "SAVE" / "BALTHIRN.QCH", "--start", "92f2",
                           "--saves", saves, "--keys", "REYATAEB", ASSETS, 39)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(result.stdout.splitlines()[-2:], [
            "choice: 10",
            "error: not yet ported: The Dark Queen of Krynn's the party emptied in the "
            "adventure's camp: main's new game and menu (dqk 5ebd:039b)"])
        self.assertNotIn("(done in block 39)", result.stdout)

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

    def test_modify_tables(self):
        # Modify's tables (data_dark_queen_modify.c) as the original's data
        # segment holds them (DS 0x422e of the merged image): the races'
        # limits (DS:02b8), the classes' least (04fd), the constitution's
        # hit points (082d) and the spells' class (a word) and level
        # (DS:27e9, 0x16 bytes each).
        merged = self.folder / "DQK_FULL.EXE"
        result = subprocess.run(["python3", ROOT / "tools" / "dqkmerge.py",
                                 ASSETS / "DQK.EXE", merged], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        exe = merged.read_bytes()
        header = struct.unpack_from("<H", exe, 8)[0] * 16
        ds = header + 0x422e * 16 - 0x10000
        source = (ROOT / "src" / "data_dark_queen_modify.c").read_text()

        def array(name):
            body = source.split(f"{name}[")[1].split("= {", 1)[1].split("};")[0]
            return [int(x) for x in re.findall(r"\d+", body)]

        self.assertEqual(array("cok_queen_limits"), list(exe[ds + 0x2b8:ds + 0x2b8 + 7 * 16]))
        self.assertEqual(array("cok_queen_minimums"), list(exe[ds + 0x4fd:ds + 0x4fd + 17 * 6]))
        self.assertEqual(array("cok_queen_constitution_bonus"),
                         list(exe[ds + 0x82d:ds + 0x82d + 32]))
        classes = [struct.unpack_from("<H", exe, ds + 0x27e9 + 0x16 * i)[0] for i in range(127)]
        levels = [exe[ds + 0x27e9 + 0x16 * i + 2] for i in range(127)]
        self.assertEqual(array("cok_queen_spell_class"), classes)
        self.assertEqual(array("cok_queen_spell_level"), levels)

    def modify(self, keys, saved=None):
        """Modify's keys from the camp with saved game A loaded, the saves
        (SAVE, the saved game changed) in self.folder / "saves"; the shots
        and the output."""
        saves = self.folder / "saves"
        if saves.exists():
            shutil.rmtree(saves)
        shutil.copytree(ASSETS / "SAVE", saves)
        if saved is not None:
            (saves / "SAVGAMA.QSV").write_bytes(saved)
        shots = self.folder / "shots"
        if shots.exists():
            shutil.rmtree(shots)
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--shots", shots,
                           "--keys", r"\k\ePLTA" + keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        return shots, result.stdout.splitlines()

    def test_modify(self):
        # Modify Character (6a87:1293; dqk_modify compares each key's screen
        # with the original's): the sheet with "Modify:" and strength lit;
        # Add (Right) twice and Next (Down), then Exit; the screens' digests
        # are those dqk_modify found equal to the original's.
        items = (8, 168, 120, 16)
        pointer = (0, 0, 24, 24)
        shots, lines = self.modify(r"m\>\>\ve")
        bar = "menu: Next Previous Add Sub Keep Exit"
        self.assertEqual(lines[9:], ["choice: 2", bar, bar, bar, bar,
                                     "menu: Create Character Delete Character Modify Character "
                                     "View Character Remove Character Load Saved Game Save "
                                     "Current Game Begin Adventuring Exit From Game",
                                     "(out of keys in the start menu)"], lines)
        for shot, expected in [(6, "cf5e0523649495e363454014bff55f13"),
                               (7, "0ad14b6e0dbfaf012118d605e6f6fd20"),
                               (8, "ee635ee9ae1af898643538d063e98a9c"),
                               (9, "51b22cddde793c4f2847299c3f014671")]:
            self.assertEqual(digest(shots / f"{shot:03d}.bmp", (items, pointer)), expected)

    def test_modify_exit_and_names(self):
        # Exit gives the scores, the exceptional strength, the most hit
        # points and the name back; the items it unreadied stay so (the
        # original's, 86b8:1965). A name taken by another of the party
        # stays (the message names it); a character of another experience
        # is not modified.
        shots, lines = self.modify(r"m\>\>\ve" + "ry")
        saves = self.folder / "saves"
        written = (saves / "ROLANDOF.QCH").read_bytes()
        shipped = (ASSETS / "SAVE" / "ROLANDOF.QCH").read_bytes()
        self.assertEqual(written[0x68:0x90], shipped[0x68:0x90])   # name, scores, hit points
        self.assertEqual(written[0x84:0x8a], shipped[0x84:0x8a])
        count = written[8]
        readied = [written[0x19e + 17 * k + 0x0a] for k in range(count)]
        self.assertEqual(readied, [0] * count)
        self.assertTrue(any(shipped[0x19e + 17 * k + 0x0a] for k in range(count)))
        shots, lines = self.modify(r"mpp" + r"\#53" * 14 + "TIMASALIN" + r"\r")
        self.assertIn("message: Already a TIMASALIN in party!", lines)
        data = bytearray((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
        data[0x40f + 0x44:0x40f + 0x48] = bytes([1, 0, 0, 0])
        shots, lines = self.modify("m", bytes(data))
        self.assertIn("message: ROLAND OF DUST can't be modified.", lines)
        self.assertNotIn("menu: Next Previous Add Sub Keep Exit", lines)

    def test_modify_limits(self):
        # Roland with strength 18/99 (the human's most 100), intelligence 18
        # (most), wisdom 10, dexterity 8 and constitution 10 (the knight's
        # least) and charisma 3 (the human's least): Add and Sub stop at the
        # limits (6a87:03c7-0b84), the exceptional strength counts, the
        # hit points keep within their most (86b8:354b) and least (6a87:007e).
        # The screens' digests those dqk_modify found equal to the original's.
        data = bytearray((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
        for k, v in enumerate([18, 18, 10, 8, 10, 3]):
            data[0x40f + 0x78 + 2 * k] = data[0x40f + 0x79 + 2 * k] = v
        data[0x40f + 0x84] = data[0x40f + 0x85] = 99
        keys = (r"m\>\>\<n\>n\<n\<n\<\>n\<\>pp\^\^\^\^\>\>\<\<e")
        shots, lines = self.modify(keys, bytes(data))
        digests = [
            "d973da50cd1bea4eba1236cd2f9afdcd", "727544507d41efbe3aacec890ea0fc6a",
            "727544507d41efbe3aacec890ea0fc6a", "d973da50cd1bea4eba1236cd2f9afdcd",
            "40308e0783ced3f15633351557d9728f", "40308e0783ced3f15633351557d9728f",
            "e14f85ff449193d54081eb8523e2c13a", "e14f85ff449193d54081eb8523e2c13a",
            "c6a256a407b6ba2668d3b63f54650009", "c6a256a407b6ba2668d3b63f54650009",
            "bbd908a6c6d9921c621c5f309ede0295", "e0f8b8ec65acec3fd285355778b8f904",
            "87fd9918bbb42d620c69445801df0d84", "eeb44c89245790db0b0e879e1e5e533e",
            "eeb44c89245790db0b0e879e1e5e533e", "c7d0384a40d8316b8181d08479a1996f",
            "7660a8cfe669a631c6eac15a2e5f506d", "850876e9703224946c8f9d50615f4501",
            "d96533a651803984a68a546f6d46e8e4", "f697b9e63f6ee8ca1024591f7cb5e5bd",
            "0335ca16bb9fd2f5d3bc7cd377e2f9f2", "2a8b908070ac52837fb0004953cdf895",
            "2a8b908070ac52837fb0004953cdf895", "2a8b908070ac52837fb0004953cdf895",
            "34a6a6f1bce5e49f8b9c5dcb8744aa18", "235505f02ed777d5965ac93bc707e466"]
        masks = ((8, 168, 120, 16), (0, 0, 24, 24))
        for shot, expected in enumerate(digests, 6):
            self.assertEqual(digest(shots / f"{shot:03d}.bmp", masks), expected,
                             f"after key {shot}")

    def test_modify_names(self):
        # The name edited in place (6a87:0d30): all 14 characters deleted,
        # TIMASALIN typed and taken by another member (the bar says so), two
        # Backspaces and 16 letters (15 at most), Home on to charisma; the
        # screen is the original's (dqk_modify's, but the cursor's cell).
        keys = r"mpp" + r"\#53" * 14 + "TIMASALIN" + r"\r\b\b" + "XYZABCDEFGHIJKLM" + r"\#47"
        shots, lines = self.modify(keys)
        self.assertEqual(lines.count("message: Already a TIMASALIN in party!"), 1)
        masks = ((8, 168, 120, 16), (0, 0, 24, 24), (128, 8, 8, 8))
        self.assertEqual(digest(shots / "051.bmp", masks), "7eedd32197ef5a0c2862c011705e12a2")

    def test_modify_cases(self):
        # Modify's other paths, each the screens' digests (but the sheet's
        # readied lines, the cursor and the pointer) dqk_modify found equal
        # to the original's: a mountain dwarf paladin's charisma held at 17,
        # wisdom's Add setting a cleric's first level spells a day to 1 and
        # constitution's Add raising 50 hit points to the levels' least;
        # a female's strength at her race's most; the name's cursor
        # wrapping and a full name taking no letter; the third and fourth
        # members, of the other two experiences; a cursed readied item
        # staying readied.
        def patched(changes, member=0):
            data = bytearray((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
            at = 0x40f
            for _ in range(member):
                items, more = data[at + 8] | data[at + 9] << 8, any(data[at + 4:at + 8])
                at += 0x19e + 17 * items
                while more:
                    more = any(data[at + 5:at + 9])
                    at += 9
            for offset, value in changes.items():
                data[at + offset] = value
            return bytes(data)

        items, pointer = (8, 168, 120, 16), (0, 0, 24, 24)
        cases = [
            ("dwarf", patched({0x58: 3, 0x5a: 3, 0x83: 17, 0x82: 17, 0x167: 3, 0x89: 10,
                               0x198: 10}), r"mnn\>nn\>n\<\<\<\>\>e",
             {6: "ce8e03f979525a70fb20c044d6ed0ebf", 9: "f85db2af412f25d1f4f09fdaa25ad8cb",
              12: "fe35b09d34a6ab9da27d9a4ef168909f", 14: "0a4e2b90d6d660627e12151af71fe65c",
              16: "0a4e2b90d6d660627e12151af71fe65c", 18: "0a4e2b90d6d660627e12151af71fe65c",
              19: "be6a20d157f5a6d4f295efd39132fd1e"}),
            ("female", patched({0x79: 15, 0x78: 15}, 2), r"\v\vm\>\>\>e",
             {8: "ea70548d46b0b36b4223e6a00ef9c6c4", 9: "92078f33c52df7d0168c2c5a4cd2c39a",
              11: "92078f33c52df7d0168c2c5a4cd2c39a", 12: "56e8f74515109f875e4abb9c87c98518"}),
            ("members", None, r"\vm\>e\v\vm\>e",
             {7: "421025b47ae54acda620517e705ceca7", 8: "f780da7c0def10fd60b278600c7cccee",
              9: "a95787fd5ed30d74d30a8d7ede8c95cf", 12: "d7fc65e6018db0807cd58a9182d7d88f",
              13: "e178ef19c6a82002e61d369bdbda4ba9", 14: "117ffff88f321eaa56297c6c6069b6da"}),
            ("cursed", patched({0x19e + 0x0c: 1}), r"m\>e",
             {6: "d511984db63d3b9de0fd8c08baf066ce", 7: "29bc9e899d8f4fdf7fac7b69d019edbc",
              8: "7903e253b6e51d4c982d51810899656f"}),
        ]
        for name, saved, keys, digests in cases:
            with self.subTest(name):
                shots, lines = self.modify(keys, saved)
                for shot, expected in digests.items():
                    self.assertEqual(digest(shots / f"{shot:03d}.bmp", (items, pointer)),
                                     expected, f"{name}: after key {shot}")
                if name == "cursed":
                    self.assertIn("message: It's Cursed!", lines)
                if name == "dwarf":
                    # Wisdom's Add set the cleric's first level spells a day
                    # to 1 (the record it writes: +0x167).
                    shots, lines = self.modify(keys + "ry", saved)
                    written = (self.folder / "saves" / "ROLANDOF.QCH").read_bytes()
                    self.assertEqual(written[0x167], 1)
        # The name's cursor wraps from the start to the end and the other
        # way; a full name takes no letter: after Return it reads
        # "ROLAND OF DUSTz" (the second letter finds it full).
        shots, lines = self.modify(r"mpp\<z\>y\>\r")
        self.assertEqual(digest(shots / "014.bmp", (items, pointer)),
                         "b188baed3ba2d88e84ae15b01136132b")

    # Create Character's clicks: the form's buttons (66e5:2a6c: rows from 24
    # by 8, columns 12 and 148) and the keys after it.
    HUMAN, SILVANESTI, HILL_DWARF, KENDER = (30, 75), (30, 27), (30, 59), (30, 67)
    CLERIC, FIGHTER, THIEF, CLERIC_RANGER = (170, 27), (170, 43), (170, 75), (170, 99)
    MALE, FEMALE = (30, 167), (30, 175)
    LAWFUL, CHAOTIC, GOOD, NEUTRAL_AXIS = (30, 103), (30, 119), (30, 131), (30, 139)

    @staticmethod
    def clicks(*spots):
        return "".join(r"\m%d,%d,1." % spot for spot in spots)

    def create(self, keys, mouse=True, name="Zarth"):
        """eclplay's Create Character from the camp with KEYS (the pointer's
        clicks where MOUSE): its result, the folder of its shots and the
        saved file NAME.QCH's bytes (or None)."""
        saves, shots = self.folder / "saves", self.folder / "shots"
        for folder in (saves, shots):
            if folder.exists():
                shutil.rmtree(folder)
        saves.mkdir()
        result = self.play(*(["--mouse"] if mouse else []), "--shots", shots, "--saves", saves,
                           "--start-menu", "--keys", r"\k\ePC" + keys, ASSETS)
        written = saves / (name.upper() + ".QCH")
        return result, shots, written.read_bytes() if written.exists() else None

    @staticmethod
    def without_pointers(data):
        """A character file but its far pointers (a record's, its items'
        and its effects', each effect's next: the original's are its heap's;
        dqk_create compares the rest with the original's)."""
        data = bytearray(data)
        for at, n in ((0, 8), (10, 2), (0xc, 0x34)):
            data[at:at + n] = bytes(n)
        at = 0x19e + 17 * (data[8] | data[9] << 8)
        for at in range(at, len(data) - 8, 9):  # the effects: all that follows
            data[at + 5:at + 9] = bytes(4)
        return bytes(data)

    def test_create_form(self):
        # The form (66e5:2a6c) as the original draws it, a screen's digest
        # (dqk_create and its formcmp compare these with the original's): at
        # first, a Silvanesti, a cleric, a female lawful good; the cleric's
        # multi-class buttons and the neutral axis for the chaotic.
        spots = [self.SILVANESTI, self.CLERIC, self.FEMALE, self.LAWFUL, self.GOOD,
                 self.CHAOTIC, self.CLERIC_RANGER]
        result, shots, _ = self.create(self.clicks(*spots))
        self.assertEqual(result.returncode, 0, result.stderr)
        digests = [digest(shots / f"{n:03d}.bmp") for n in range(3, 11)]
        self.assertEqual(digests, FORM_DIGESTS)
        self.assertEqual(len(set(digests)), 8)
        # A button takes the clicks within its text and 16 pixels more (the
        # original's hit-test, checked with the original's screens): Human at
        # columns 12 to 67 takes the one at 67, not at 68 (the pointer's cursor
        # left out of the compare).
        same = []
        for x in (68, 67):
            result, folder, _ = self.create(self.clicks(self.SILVANESTI, (x, 75)))
            masks = [(18, 14, 32, 32), (x - 9, 62, 32, 32)]
            same.append(digest(folder / "005.bmp", masks) == digest(folder / "004.bmp", masks))
        self.assertEqual(same, [True, False])

    def test_create_fighter(self):
        # A human male lawful good fighter made as dqk_create makes it (the
        # same seed): the roll (66e5:1a90), Done, the name, the icon moved
        # right once, saved: ZARTH.QCH byte for byte as the original writes
        # it but the far pointers (the digest of the original's own file).
        keys = self.clicks(self.HUMAN, self.FIGHTER, self.MALE, self.LAWFUL, self.GOOD)
        result, shots, data = self.create(keys + r"ddZarth\r\>dy")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[lines.index("input: ZARTH") - 1], FIGHTER_SCORES)
        self.assertEqual(len(data), FIGHTER_SIZE)
        self.assertEqual(hashlib.md5(self.without_pointers(data)).hexdigest(), FIGHTER_DIGEST)
        record = data[:0x19e]
        self.assertEqual(record[0x68:0x6d], b"ZARTH")
        self.assertEqual([record[0x58], record[0x5a], record[0x60], record[0x62]],
                         [6, 2, 0, 0])
        self.assertEqual(record[0xc4], 4)  # the default body, 3 (66e5:0001), moved right once

    def test_create_modify(self):
        # The sheet's Modify (creation's flag: no name to edit, charisma's
        # Next the hit points): strength and intelligence added, Keep (the
        # sheet again), every field in turn, the hit points added to, Exit;
        # the file the original's (dqk_create's "modify" case).
        keys = self.clicks(self.HUMAN, self.FIGHTER, self.MALE, self.LAWFUL, self.GOOD)
        modifying = r"dm\>n\>km" + "n" * 7 + r"p\>e"
        result, _, data = self.create(keys + modifying + r"dQuin\rdy", name="Quin")
        lines = result.stdout.splitlines()
        # The sheet again after Keep (strength 17, intelligence 16) and Exit.
        self.assertEqual([l for l in lines if l.startswith("scores:")],
                         ["scores: 16 15 15 15 14 16 (0) age 29",
                          "scores: 17 16 15 15 14 16 (0) age 29",
                          "scores: 17 16 15 15 14 16 (0) age 29"])
        self.assertEqual(hashlib.md5(self.without_pointers(data)).hexdigest(),
                         "7be22ddbf1894b665034f7848ae3cdbe")

    def test_create_keyboard(self):
        # No mouse: the form's keys (66e5:2967) Left and Right for the
        # column, Up and Down for the row (not through the gaps), Return
        # for the button: the same human fighter, the same file.
        spots = (self.HUMAN, self.FIGHTER, self.MALE, self.LAWFUL, self.GOOD)
        tail = r"ddZarth\r\>dy"
        _, _, mouse = self.create(self.clicks(*spots) + tail)
        keys = (r"\v" * 6 + r"\r" + r"\v\r" + r"\v" * 3 + r"\r" + r"\v\v\r" + r"\>" + r"\^" * 13
                + r"\r" + r"\<\^\^\^\^\^\^\^\^\^\^\^\^\^\^\^\^")
        _, _, keyboard = self.create(keys + tail, mouse=False)
        self.assertIsNotNone(mouse)
        self.assertEqual(keyboard, mouse)

    def test_create_icon_keys(self):
        # The picker's arrows (66e5:09e4) wrap in the 7 by 7 grid: the human
        # fighter's icon starts at 3 (66e5:0001) and Right seven times is 3,
        # Up once 45 (the bottom row), Left four times 6, Down seven times 3
        # and a click on a cell (24 pixels each from 8, 8) takes it.
        keys = self.clicks(self.HUMAN, self.FIGHTER, self.MALE, self.LAWFUL, self.GOOD)
        for icons, expected in ((r"\>" * 7, 3), (r"\^", 45), (r"\<" * 4, 6), (r"\v" * 7, 3),
                                (r"\>\v\<", 10), (self.clicks((8 + 24 * 3 - 1, 8 + 24 * 3 + 5)), 23)):
            _, _, data = self.create(keys + r"ddZarth\r" + icons + "dy")
            self.assertEqual(data[0xc4], expected, icons)

    def test_create_long_name(self):
        # The name is 15 characters at most (66e5:193f), the file's the first
        # eight.
        keys = self.clicks(self.HUMAN, self.FIGHTER, self.MALE, self.LAWFUL, self.GOOD)
        _, _, data = self.create(keys + r"ddabcdefghijklmnopq\rdy", name="abcdefgh")
        self.assertEqual(data[0x68:0x78], b"ABCDEFGHIJKLMNO\0")

    def test_create_refused_name(self):
        # 66e5:193f copies a name with its NUL and no more: after a refused
        # one ("1ABCDEFGHI") the bytes past the new name's NUL stay, as in the
        # original's file (dqk_create's knights case).
        keys = self.clicks(self.HUMAN, self.FIGHTER, self.MALE, self.LAWFUL, self.GOOD)
        _, _, data = self.create(keys + r"dd1abcdefghi\rZed\rdy", name="Zed")
        self.assertEqual(data[0x68:0x78], b"ZED\0DEFGHI\0\0\0\0\0\0")

    def test_create_cleric(self):
        # A Silvanesti cleric (lawful good): the deity's list (66e5:36b0:
        # Paladine, Majere, Kiri-Jolith, Mishakal), the second chosen, a
        # Reroll Stats (another roll), the sheet's Done; and a dwarf cleric
        # gets Reorx without being asked.
        keys = self.clicks(self.SILVANESTI, self.CLERIC, self.FEMALE, self.LAWFUL, self.GOOD)
        result, _, data = self.create(keys + r"d\vsrdElara\rdy", name="Elara")
        lines = result.stdout.splitlines()
        self.assertIn("list: Pick Diety", lines)
        self.assertEqual([l for l in lines if l.startswith("item:")],
                         ["item: \tPaladine", "item: \tMajere", "item: \tKiri-Jolith",
                          "item: \tMishakal"])
        self.assertIn("choice: Majere", lines)
        scores = [l for l in lines if l.startswith("scores:")]
        self.assertEqual(len(scores), 2)
        self.assertNotEqual(scores[0], scores[1])
        self.assertEqual(data[0x19b], 2)
        keys = self.clicks(self.HILL_DWARF, self.CLERIC, self.MALE, self.LAWFUL, (30, 139))
        result, _, data = self.create(keys + r"ddDorn\rdy", name="Dorn")
        self.assertNotIn("list: Pick Diety", result.stdout)
        self.assertEqual(data[0x19b], 6)

    def test_create_ends(self):
        # Exit from the form, a name that is a digit's or empty asks again,
        # Escape from the name, Exit from the icons and No to the save
        # leave no file; the screens come back to the camp.
        keys = self.clicks(self.HUMAN, self.FIGHTER, self.MALE, self.LAWFUL, self.GOOD)
        for ending in ("e", r"d\e", "de", r"ddZ\e", r"ddZarth\re", r"ddZarth\rdn"):
            # Back at the camp, C begins another: a second seed is said.
            result, _, data = self.create(keys + ending + "C")
            self.assertIsNone(data, ending)
            self.assertEqual(result.stdout.count("seed: "), 2, ending)
            self.assertEqual(result.stdout.splitlines()[-1], "(out of keys in the start menu)")
        result, _, data = self.create(keys + r"dd" + "\\r" + "1a\\r" + "0b\\r" + r"Zarth\r" + "dy")
        self.assertIsNotNone(data)
        self.assertEqual([l for l in result.stdout.splitlines() if l.startswith("input:")],
                         ["input: ZARTH"])

    def test_modify_keep(self):
        # Modify's Keep (6a87:025e) for a knight, a cleric, fighter and mage
        # and a cleric and ranger: the spells a day and known (8bcc:01e7,
        # 6a87:1599's forgetting a bit off, 66e5:30d3), the hit points' base;
        # the .QCH Remove Character writes, as the original's but the far
        # pointers (the md5 of the original's, dqk_modify's "Keep" cases).
        for keys, name, expected in (
                (r"m\>k", "ROLANDOF", "dacbb7e09e251d99da3657346b6bb3ae"),
                (r"\vmn\>k", "TIMASALI", "afba4624c9d314bb95fe7b2f32c3ee59"),
                (r"\v\v\vmnn\<k", "BALTHIRN", "564bdf50a73083986ead4dbd26e3d9b5")):
            saves = self.folder / "saves"
            if saves.exists():
                shutil.rmtree(saves)
            saves.mkdir()
            (saves / "SAVGAMA.QSV").write_bytes((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
            result = self.play("--start-menu", "--saves", saves, "--keys",
                               r"\k\ePLTA" + keys + "ry", ASSETS)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = (saves / f"{name}.QCH").read_bytes()
            self.assertEqual(hashlib.md5(self.without_pointers(data)).hexdigest(), expected, name)

    @staticmethod
    def hand_made(data):
        """The shipped saved game with its first member made by hand
        (dqk_stats.py): a body armour (type 0x1f) of +100, so that the armour
        class sum wraps below 0 and 177e:0f5f compares it unsigned, a cloak
        (0x3a) that magic armour keeps, and effect 0x4a, which doubles the
        movement."""
        data = bytearray(data)
        at = 0x40F
        items = at + 0x19E
        for k, (kind, bonus) in ((1, (0x3A, 3)), (3, (0x1F, 100))):
            item = items + 17 * k
            data[item], data[item + 8], data[item + 10], data[item + 13] = kind, bonus, 1, 0
        data[at + 4:at + 8] = bytes([0x51, 0x00, 0x71, 0x58])
        end = items + 17 * (data[at + 8] | data[at + 9] << 8)
        data[end:end] = bytes([0x4A, 0, 0, 0xFF, 0, 0, 0, 0, 0])
        return bytes(data)

    @staticmethod
    def saved_without_pointers(data):
        """A .QSV but the heap's far pointers: each character's (next,
        effects, the items' count word's first, the slots, combat) and each
        effect's next."""
        data = bytearray(data)
        at = 0x40F
        for _ in range(data[0x40D]):
            items = data[at + 8] | data[at + 9] << 8
            effects = any(data[at + 4:at + 8])
            for off, n in [(0, 4), (4, 4), (10, 2), (0x40, 4)] + [(0xC + 4 * i, 4)
                                                                  for i in range(13)]:
                data[at + off:at + off + n] = bytes(n)
            at += 0x19E + 17 * items
            while effects:
                effects = any(data[at + 5:at + 9])
                data[at + 5:at + 9] = bytes(4)
                at += 9
        return bytes(data)

    def test_recompute_hand_made(self):
        # 177e:0be2 on a record the original had not already recomputed (the
        # shipped characters were): Load recomputes (604b:28f8), Save Current
        # Game writes it, as the original's own save is but for its pointers
        # (dqk_stats.py's digest). It drives the cloak 0x3a kept by magic
        # armour, the armour class compared unsigned, the cloak's part of
        # the armour class from behind and effect 0x4a on the movement.
        saves = self.folder / "saves"
        saves.mkdir()
        (saves / "SAVGAMA.QSV").write_bytes(
            self.hand_made((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes()))
        result = self.play("--start-menu", "--saves", saves, "--keys", r"\k\ePLTAsa", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = (saves / "SAVGAMA.QSV").read_bytes()
        record = data[0x40F:0x40F + 0x19E]
        self.assertEqual([record[0x18E], record[0x18F], record[0x199]], [160, 153, 18])
        self.assertEqual(hashlib.md5(self.saved_without_pointers(data)).hexdigest(),
                         "149066432d22bda4ab7838419aeed6cf")

    def test_trade_members(self):
        # The picker of Trade (86b8:3ae5) starts on the member viewed (86b8:0b1d
        # sets DS:c3eb), not the one last given to, and each member's row is a
        # mouse area: a click makes it the current member (checked in the
        # original's picker). Roland gives 5 to the second member; Cinder's View
        # then gives 5 to the one below her (the fourth), by Down; and the
        # first member, viewed again, gives 5 to the sixth by a click on its row.
        def steel(data):
            at, out = 0x40F, []
            for _ in range(data[0x40D]):
                out.append(data[at + 0x4C] | data[at + 0x4D] << 8)
                effects = any(data[at + 4:at + 8])
                at += 0x19E + 17 * (data[at + 8] | data[at + 9] << 8)
                while effects:
                    effects = any(data[at + 5:at + 9])
                    at += 9
            return out

        saves = self.folder / "saves"
        saves.mkdir()
        (saves / "SAVGAMA.QSV").write_bytes((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
        trade = r"SS5\rEE"  # Select, the steel, 5, Return, Exit the coins, Exit the picker
        keys = (r"\k\ePLTAVT\v" + trade + r"E" + r"\v\vVT\v" + trade + r"E" +
                r"\^\^VT\m20,76,1." + trade + "E" + "SA")
        result = self.play("--mouse", "--start-menu", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(steel((saves / "SAVGAMA.QSV").read_bytes()),
                         [190, 205, 195, 205, 200, 205])

    def test_first_fight(self):
        # To Caergoth (the palace, Leave), its commands and steps (6fea:0365,
        # 0753) along the ruins to the dragons' fight: SETUP MONSTERS, APPROACH
        # and the sprite (step 4, part 6), then the battle's set-up, where the
        # setup's event 8 runs a monster's effect 0x64 (dqk 78e4:14d1, not ported):
        # the port stops there, after the approach and the combat view, before any turn.
        walk = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r" + TO_FIGHT
        result = self.play("--start-menu", "--keys", walk + r"\r", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("effect 0x64 (dqk 78e4:14d1)", result.stderr)
        moves = [line for line in result.stdout.splitlines()
                 if line.startswith(("step:", "turn:"))]
        self.assertEqual(len(moves), 29)
        self.assertEqual(moves[:3], ["step: 4,2 facing 2", "step: 5,2 facing 2",
                                     "step: 6,2 facing 2"])
        self.assertEqual(moves[-1], "step: 13,8 facing 2")
        self.assertIn("print: HUGE SHADOWS SKIM OVER THE GROUND. GIANT SHAPES DISAPPEAR OVER "
                      "THE TREE TOPS.", result.stdout)

    def test_load_and_play(self):
        # eclplay --load SAVGAMA.QSV is the start menu's Load (cok_drawing.load),
        # and --play its Begin Adventuring (cok_drawing.play): the variables and
        # the screen at block 4's first text equal the start menu's \k\ePLTAB
        # ones, and the same keys walk to the same battle as test_first_fight's.
        saves = self.folder / "saves"
        saves.mkdir()
        for name in ("SAVGAMA.QSV", "VAULTA.DAT"):
            shutil.copy(ASSETS / "SAVE" / name, saves / name)
        menu, loaded = self.folder / "menu", self.folder / "loaded"
        result = self.play("--start-menu", "--saves", saves, "--vars", menu.with_suffix(".vars"),
                           "--screen", menu.with_suffix(".bmp"), "--keys", r"\k\ePLTAB", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-1], "(out of keys in block 4 at 81bc)")
        result = self.play("--load", saves / "SAVGAMA.QSV", "--play", "--vars",
                           loaded.with_suffix(".vars"), "--screen", loaded.with_suffix(".bmp"),
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-1], "(out of keys in block 4 at 81bc)")
        self.assertEqual(loaded.with_suffix(".vars").read_bytes(),
                         menu.with_suffix(".vars").read_bytes())
        self.assertEqual(loaded.with_suffix(".bmp").read_bytes(),
                         menu.with_suffix(".bmp").read_bytes())
        walk = r"\r\r\rP\r\r\r\r\rL\r" + TO_FIGHT + r"\r"
        outs = []
        # The start menu's title draws seven random(8) (its palette cycles, 261b:00b1 -> 261b:03db
        # -> 2102:0234) that --load skips: 0x3e6298eb, the seed they leave from 0, is the
        # --start-menu route's RandSeed at the battle.
        for result in (self.play("--load", saves / "SAVGAMA.QSV", "--play", "--seed", "1046649067",
                                 "--keys", walk, ASSETS),
                       self.play("--start-menu", "--saves", saves, "--keys", r"\k\ePLTAB" + walk,
                                 ASSETS)):
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("effect 0x64 (dqk 78e4:14d1)", result.stderr)
            self.assertEqual(sum(line.startswith(("step:", "turn:"))
                                 for line in result.stdout.splitlines()), 29)
            # The whole walk from "protection:" on agrees, the turn order and the rounds included.
            outs.append(result.stdout[result.stdout.index("protection:"):].splitlines(True))
        self.assertEqual(outs[0], outs[1])

    def test_view_clicks(self):
        # The view's areas of the commands' bar (1bdb:1aef, with the mouse on):
        # a click gives the arrow key's command (1206's codes, 126e): columns
        # 0-39 turn left, 40-95 step ahead, 96-135 turn right, rows 100-159
        # turn around (an arrow key's Left, Up, Right, Down); a click on
        # nothing beeps (2aa6:169d). From Caergoth's 3D view at 3,2 facing 2.
        # The original's check: dqk_view_mouse.py.
        clicks = [(39, 50), (96, 50), (40, 50), (95, 50), (135, 90), (0, 0), (60, 99),
                  (60, 100), (135, 120), (60, 159), (136, 90), (60, 160), (136, 120),
                  (20, 150)]
        keys = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r" + "".join(
            r"\m%d,%d,1." % spot for spot in clicks)
        result = self.play("--start-menu", "--mouse", "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        moves = [line for line in result.stdout.splitlines()
                 if line.startswith(("step:", "turn:"))]
        self.assertEqual(moves, ["turn: 3,2 facing 0", "turn: 3,2 facing 2",
                                 "step: 4,2 facing 2", "step: 5,2 facing 2",
                                 "turn: 5,2 facing 4", "turn: 5,2 facing 2",
                                 "step: 6,2 facing 2", "turn: 6,2 facing 6",
                                 "turn: 6,2 facing 2", "turn: 6,2 facing 6",
                                 "turn: 6,2 facing 2"])

    def hover_shot(self, x, y):
        """The pixels of the shot with the pointer at x, y in the commands'
        bar, the menu waiting again after an Escape (its controls live)."""
        shots = self.folder / f"hover-{x}-{y}"
        keys = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r" + r"\m%d,%d,0.\e" % (x, y)
        result = self.play("--start-menu", "--mouse", "--shots", shots, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        return pixels(sorted(shots.glob("*.bmp"))[-1])

    def test_view_cursors(self):
        # The pointer's shape over the view's areas (2aa6:15e3's cursor, 1bdb:1aef's
        # records: ALWAYS.TLB's 26 over columns 0-39, 11 over 40-95, 25 over
        # 96-135, 27 over rows 100-159) and the shield (2) elsewhere, each drawn
        # at its hotspot (the header's row, then column). The pixels a move to
        # the spot changes (the shot less the one with the pointer parked at
        # 250,150, within 24 pixels of it) are the record's opaque pixels there,
        # nine in ten of them or more.
        always = hlib.load(ASSETS / "DISK1" / "ALWAYS.TLB")
        park = self.hover_shot(250, 150)
        for (x, y), record in [((39, 50), 26), ((40, 50), 11), ((96, 50), 25),
                               ((60, 100), 27), ((136, 90), 2)]:
            shot = self.hover_shot(x, y)
            near = [(c, r) for r in range(max(0, y - 24), min(200, y + 24))
                    for c in range(max(0, x - 24), min(320, x + 24))]
            moved = {(c, r) for c, r in near
                     if shot[3 * (r * 320 + c):][:3] != park[3 * (r * 320 + c):][:3]}
            w, h, hot_row, hot_col, _, rows = hlib.picture(always.record(record))
            placed = {(x - hot_col + c, y - hot_row + r) for r in range(h) for c in range(w)
                      if rows[r][c] is not None}
            placed = {(c, r) for c, r in placed if 0 <= c < 320 and 0 <= r < 200}
            agreed = len(moved & placed) / len(moved | placed)
            self.assertGreaterEqual(agreed, 0.9, f"the pointer at {x},{y}: record {record}")

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


    def test_encamp(self):
        # The adventure's camp (Encamp, 6fea:0365's key 4, then 5f07:0b3b): its
        # menu's words, View and its sheet, Exit back to the commands; Magic
        # stops, cited; Save writes the game to a new slot (SAVGAMC.QSV). The
        # walk to the commands is dqk_camp.py's.
        walk = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        saves = self.saved_game((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
        result = self.play("--start-menu", "--saves", saves, "--keys", walk + r"Ev\e\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("menu: View Magic Rest Alt Fix Load Save Exit", result.stdout)
        self.assertIn("choice: 0", result.stdout)
        self.assertEqual(result.stdout.count("choice: 7"), 2, result.stdout)
        self.assertIn("menu: Area Cast View Encamp Search Look Inv", result.stdout)
        result = self.play("--start-menu", "--saves", saves, "--keys", walk + "ESCN\\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((saves / "SAVGAMC.QSV").exists())
        result = self.play("--start-menu", "--keys", walk + "EM", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not yet ported: The Dark Queen of Krynn's Magic in the adventure's camp "
                      "(dqk 7070:1671)", result.stderr + result.stdout)

    def test_encamp_marks(self):
        # The camp forgets the scrolls' marks (7070:0218, at the camp's start and end):
        # an item of type 0x27 (its type at +0x28) loses bit 7 of its spells at
        # 0x36-0x38, so the game saved from the camp holds them clear. The saved
        # game's first character's first item is made one with marks 0x81 and 0x83.
        data = bytearray((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
        item = characters(data)[0][0] + 0x19E
        data[item] = 0x27
        data[item + 0x0E:item + 0x11] = bytes([0x81, 0x83, 0x00])
        saves = self.saved_game(data)
        walk = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        result = self.play("--start-menu", "--saves", saves, "--keys", walk + "ESCN\\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        saved = (saves / "SAVGAMC.QSV").read_bytes()
        item = characters(saved)[0][0] + 0x19E
        self.assertEqual(saved[item], 0x27)
        self.assertEqual(saved[item + 0x0E:item + 0x11], bytes([0x01, 0x03, 0x00]))

    def test_encamp_load(self):
        # Save, then Exit (5f07:0ba6 clears the game saved, DS:ae5b), then Encamp and
        # Load: "Game not saved. Load anyway? " is asked, and No goes back to the menu
        # (7070:2f07), so the menu is shown four times and the run ends at Exit.
        walk = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        saves = self.saved_game((ASSETS / "SAVE" / "SAVGAMA.QSV").read_bytes())
        result = self.play("--start-menu", "--saves", saves, "--keys", walk + "ESCN\\eEln\\e",
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("not yet ported", result.stderr + result.stdout)
        self.assertEqual(result.stdout.count("menu: View Magic Rest Alt Fix Load Save Exit"), 4,
                         result.stdout)

    def test_encamp_arrows(self):
        # The camp's menu takes the arrow keys as the adventure's commands do: Down picks
        # a member (5e22:067c), not ported, so the run stops there, cited.
        walk = r"\k\ePLTAB\r\r\rP\r\r\r\r\rL\r\^\^\^"
        result = self.play("--start-menu", "--keys", walk + "E\\v\\e", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not yet ported: The Dark Queen of Krynn's a member picked by a key "
                      "(dqk 5e22:067c)", result.stderr + result.stdout)

    def test_ground(self):
        # The battle's ground (queen_arena.c, eclplay --ground): a made-up map of 50
        # by 25 terrain values, its view at 3, 2, drawn with DUNGCOM's set 4 (the
        # palettes of ALWAYS and FRAME under it) as tools/hlib.py draws each cell:
        # the terrain table's tile (DS:6197 of build/DQK_FULL.EXE, the 4th byte of
        # its entry; the port's own table is not the oracle) as record tile + 1 of
        # the set, its 24 by 24 pixels at 8 + 24 * column, 8 + 24 * row, in the
        # palette's colours (the shown bits 2-7 of each 8-bit value). A value of 0
        # or past 0x4c is drawn as 0x15 (5837:172c:1784-1790).
        exe = BUILD / "DQK_FULL.EXE"
        if not exe.exists():
            self.skipTest("build/DQK_FULL.EXE not built (make dqk-merged)")
        image = exe.read_bytes()
        ds = struct.unpack_from("<H", image, 8)[0] * 16 + 0x322E * 16
        terrain = image[ds + 0x6197:ds + 0x6197 + 308]
        self.assertEqual(len(terrain), 308)
        dungeon = hlib.Library(hlib.load(ASSETS / "DISK2" / "DUNGCOM.TLB").record(4))
        # The clouds and bodies (0x1b-0x1f, 0x46) stop the port (5837:172c, 137a).
        clouds = {0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x46}
        shown_value = lambda v: 0x15 if v == 0 or v > 0x4C else v
        # Values whose tile is in the set (tile + 1 is one of its records).
        usable = [v for v in range(1, 0x4D) if v not in clouds and
                  terrain[4 * v + 3] + 1 < len(dungeon.records)]
        view_x, view_y = 3, 2
        cells = [[usable[(x * 7 + y * 3) % len(usable)] for x in range(50)] for y in range(25)]
        # Out-of-range values in the map, outside the view: they draw as 0x15 where
        # the view has them.
        for x, y in ((0, 0), (49, 24), (1, 24)):
            cells[y][x] = (0x00, 0x4D, 0xFF)[x % 3]
        cells[view_y + 1][view_x + 1] = 0x00
        cells[view_y + 2][view_x + 5] = 0xFE
        mapfile = self.folder / "ground.txt"
        lines = [f"view {view_x} {view_y}"]
        lines += ["".join(f"{v:02x}" for v in row) for row in cells]
        mapfile.write_text("\n".join(lines) + "\n")
        bmp = self.folder / "ground.bmp"
        result = self.play("--ground", mapfile, "--ground-set", "dungcom4", "--screen", bmp,
                           ASSETS, 4)
        self.assertEqual(result.returncode, 0, result.stderr)
        base = {}
        for name in ("ALWAYS.TLB", "FRAME.TLB"):
            base.update(hlib.palettes(hlib.load(ASSETS / ("DISK1" if name[0] == "A" else "DISK3") / name)))
        colours = dict(base)
        colours.update(hlib.palettes(dungeon))
        shown = lambda q: bytes(((c >> 2) << 2) | ((c >> 2) >> 4) for c in q)
        got = pixels(bmp)
        for row in range(7):
            for col in range(7):
                value = shown_value(cells[view_y + row][view_x + col])
                tile = terrain[4 * value + 3]
                _, _, _, _, _, rows = hlib.picture(dungeon.records[tile + 1], dungeon)
                for j in range(24):
                    for i in range(24):
                        index = rows[j][i]
                        # The BMP stores blue, green, red.
                        want = shown(colours.get(index, (index, index, index)))[::-1]
                        y = 8 + 24 * row + j
                        at = (y * 320 + 8 + 24 * col + i) * 3
                        # pixels() gives the rows from the top.
                        self.assertEqual(got[at:at + 3], want, (row, col, i, j))
        self.assertEqual(self.play("--ground", mapfile, "--ground-set", "dungcom7", ASSETS, 4).returncode, 2)
        # A cloud in the view stops the drawing, citing the original, with no picture.
        for stopped in (0x1C, 0x46):
            cells[view_y + 3][view_x + 2] = stopped
            mapfile.write_text("\n".join([f"view {view_x} {view_y}"] +
                                         ["".join(f"{v:02x}" for v in row) for row in cells]) + "\n")
            result = self.play("--ground", mapfile, "--ground-set", "dungcom4", "--screen", bmp,
                               ASSETS, 4)
            self.assertEqual(result.returncode, 1)
            self.assertIn(f"value 0x{stopped:02x}", result.stderr)
            self.assertIn("dqk 5837:172c, 137a", result.stderr)

if __name__ == "__main__":
    unittest.main()

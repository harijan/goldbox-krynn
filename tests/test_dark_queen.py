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
        self.assertEqual(digest(shots / "007.bmp"), "9585996ef87d161423a5c6faa8150957")
        self.assertEqual(digest(shots / "009.bmp"), "3b674649f17f2d0b8e5eaa1fe11117ca")

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

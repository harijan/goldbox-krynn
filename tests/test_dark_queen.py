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
        # Saved games, parties and the adventure loop are not read yet.
        result = self.play("--play", ASSETS, 4)
        self.assertEqual(result.returncode, 2)
        self.assertIn("characters and saved games are not read yet", result.stderr)
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
        # A click on Create Character's button; Delete's stops too.
        result = self.play("--start-menu", "--mouse", "--keys", r"\k\eP\m200,120,1.", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "not yet ported: The Dark Queen of Krynn's Create "
                         "Character (dqk 66e5:2f95)\n")
        result = self.play("--start-menu", "--keys", r"\k\ePLT", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("saved games (dqk 604b:2e87)", result.stderr)

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
        self.assertEqual(views, {0: "98123e9ef1291a98b380cdef5d865b32",
                                 1: "a22074115c3a6132808e4ff22619a9a0",
                                 2: "5fc47b795b88f0fc0a91f7a44ba11397",
                                 3: "7e8332df4d5dfb820347cf175a394a7a"})


if __name__ == "__main__":
    unittest.main()

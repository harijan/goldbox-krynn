"""Death Knights of Krynn read headless (--game death-knights): the tools
over DKKAssets/ and eclplay's runs of its scripts, their text, menus and 3D
view. Skipped, saying so, where DKKAssets/ is not there (stdlib only)."""
import hashlib
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ASSETS = ROOT / "DKKAssets"
GAME = ("--game", "death-knights")


def pixel(bmp, x, y):
    """RGB of pixel (x, y) from a bottom-up 24-bit BMP."""
    offset = struct.unpack_from("<I", bmp, 10)[0]
    width, height = struct.unpack_from("<ii", bmp, 18)
    stride = (width * 3 + 3) & ~3
    at = offset + (height - 1 - y) * stride + x * 3
    return tuple(reversed(bmp[at:at + 3]))


def view(bmp):
    """A digest of the 3D view's cells 3-13 across and down."""
    return hashlib.md5(bytes(c for y in range(24, 112) for x in range(24, 112)
                             for c in pixel(bmp, x, y))).hexdigest()


class DeathKnightsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def run_tool(self, tool, *args):
        return subprocess.run([str(BUILD / tool), *map(str, args)], capture_output=True,
                              text=True, cwd=ROOT)

    def play(self, *args):
        return self.run_tool("eclplay", *GAME, *args)

    def test_archives(self):
        archives = sorted(ASSETS.glob("*.DAX"))
        result = self.run_tool("daxcheck", *archives)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-1],
                         "Verified 50 archives, 631 records, 1069692 decoded bytes")

    def test_images(self):
        # Every graphics archive the descriptor lists exports whole but the
        # font (8X8D1 record 201), as Champions' does; the 256-tile sets
        # too, whose headers count 0xff for 256 frames.
        names = ["TITLE", "BIGPIC1", "BIGPIC2", "BIGPIC3", "CPIC1", "CBODY", "CHEAD", "COMSPR",
                 "BACK1", "BACK2", "BACK3", "BACK4", "BACK5", "SKYGRND"] + \
                [f"8X8D{n}" for n in range(1, 6)] + [f"PIC{n}" for n in range(1, 8)] + \
                [f"SPRIT{n}" for n in range(1, 5)]
        total = 0
        for name in names:
            out = self.folder / name
            result = self.run_tool("daximages", *GAME, ASSETS / f"{name}.DAX", out)
            if name == "8X8D1":
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stderr,
                                 "entry 0 ID 201: unsupported or malformed image layout\n")
            else:
                self.assertEqual(result.returncode, 0, name + result.stderr)
            total += int(result.stdout.split()[1])
        self.assertEqual(total, 2374)
        self.assertEqual(len(list((self.folder / "8X8D4").glob("*.bmp"))), 256)
        # Champions' descriptor does not know Death Knights' new archives.
        result = self.run_tool("daximages", ASSETS / "BACK1.DAX", self.folder / "back")
        self.assertEqual(result.returncode, 1)
        result = self.run_tool("daxcompose", *GAME, self.folder / "x.bmp",
                               "draw", ASSETS / "BACK1.DAX", 0, 0, 2, 2,
                               "draw", ASSETS / "8X8D4.DAX", 0, 255, 20, 2)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ecldump(self):
        files = [ASSETS / f"ECL{n}.DAX" for n in (1, 2, 3)]
        result = self.run_tool("ecldump", *GAME, "--summary", *files)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout.splitlines()[-1], "Decoded 14781 instructions; "
                         "0 IFs skip an instruction by a different length")
        listing = self.run_tool("ecldump", *GAME, "--block", 48, files[1]).stdout.splitlines()
        for line in ["807a: SETUP MONSTERS 64 1 255 0", "8451: JOURNAL 49", "88f7: YES NO",
                     "8925: CLEAR EXIT", "80c7: PRESS KEY", "82c4: MUSIC 130"]:
            self.assertIn(line, listing)
        listing = self.run_tool("ecldump", *GAME, "--block", 49, files[1]).stdout.splitlines()
        self.assertIn('89be: ASK MENU [7f79] 4 : "ATTACK" "TAKE SWORD" "REASON" "LEAVE"',
                      listing)
        listing = self.run_tool("ecldump", *GAME, "--block", 80, files[2]).stdout.splitlines()
        self.assertIn("9569: TREASURE ITEMS $03e8 0 0 4 : 7 39 78 72", listing)
        # Read as Champions' they do not decode.
        result = self.run_tool("ecldump", "--summary", *files)
        self.assertEqual(result.returncode, 1)

    def test_text_and_press_key(self):
        result = self.play("--keys", r"\r\r\r", ASSETS, 48)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [
            "print: WITH THE SPATE OF UNDEAD ATTACKS, MOST OF THE HIGH CLERIST'S TOWER IS ON "
            "ALERT. THEY WILL GLADLY GIVE YOU A ROOM, BUT MOST OF THE TOWER IS OFF LIMITS.",
            "menu: ~Press button or enter to continue",
            "music: 5", "music: 5", "(done in block 48)"])

    def test_journal(self):
        result = self.play("--file", 2, "--start", "83c7", "--keys", r"\r\r", ASSETS, 48)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[1:4], ["menu: ~Press button or enter to continue",
                                      "print: This is recorded as journal entry 49.",
                                      "menu: ~Press button or enter to continue"])
        # ADD NPC then reads MON1CHA record 74 (46b2:19a2), who joins.
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(lines[4:], ["party: DURFEY joins, icon 74 in slot 0",
                                     "(done in block 48)"])

    def test_npcs_join(self):
        # The intro (ECL1 block 2) adds its three NPCs, then fights, whose
        # computer's turns run until one casts a spell of use (5340:43ea),
        # which stops the run at its handler (not ported yet).
        result = self.play("--keys", r"\r" * 20, ASSETS, 2)
        self.assertEqual(result.returncode, 1)
        self.assertEqual([line for line in result.stdout.splitlines()
                          if line.startswith("party:")],
                         ["party: SIR DRYDEN joins, icon 48 in slot 0",
                          "party: TANAR joins, icon 49 in slot 1",
                          "party: FLAVIUS joins, icon 50 in slot 2"])
        self.assertEqual(result.stderr.splitlines()[-1], "not yet ported: Death Knights of "
                         "Krynn's spell 108, Delay Blast Fireball (dkk 5dc5:3241)")

    def test_characters(self):
        # The saved characters (--character), a sheet in camp: name, class,
        # items (the screen's frame is still Champions').
        party = [arg for n in range(1, 7)
                 for arg in ("--character", ASSETS / f"SAVE/CHRDATA{n}.GAM")]
        result = self.play(*party, "--play", "--keys", r"ev\r\e", ASSETS, 17)
        lines = result.stdout.splitlines()
        self.assertIn("menu: Save View Magic Rest Alter Fix Exit", lines)
        at = lines.index("print: SIR DRYDEN")
        self.assertEqual(lines[at:at + 7], [
            "print: SIR DRYDEN", "print: Knight of the Crown", "menu: Items Trade Drop Exit",
            "list: Items", "item:  Yes  Long Sword +1 ", "item:  Yes  Shield ",
            "item:  Yes  Solamnic Plate Mail "])

    def test_yes_no_and_clear_exit(self):
        result = self.play("--file", 2, "--start", "88f7", "--keys", r"N\r", ASSETS, 48)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [
            "menu: ~Yes ~No", "choice: 1", "print: AREAS IN THIS DIRECTION ARE OFF LIMITS.",
            "menu: ~Press button or enter to continue", "(done in block 48)"])
        # Yes: = holds, and the run goes to block 17.
        result = self.play("--file", 2, "--start", "88f7", "--keys", r"Y\r", ASSETS, 48)
        self.assertIn("choice: 0", result.stdout.splitlines())
        self.assertEqual(result.stdout.splitlines()[-1], "(done in block 17)")

    def test_ask_menu(self):
        result = self.play("--file", 2, "--start", "8983", "--keys", r"\r", ASSETS, 49)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:3], [
            "print: SIR GARREN GROWLS AT YOU, THE MYSTERIOUS SWORD POINTED IN YOUR DIRECTION. ",
            "print: what do you do", "menu: ~ATTACK ~TAKE SWORD ~REASON ~LEAVE"])

    def test_treasure(self):
        # Block 2's fight won: the results, then the treasure menu
        # (3a37:0eed); Take with only items lists them (3a37:0ce8) and gives
        # the first to the selected; Exit with treasure left asks, the
        # Yes/No on No (6a69:2048 with 1), and Enter leaves.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--combat", "won", "--keys",
                           r"\r\rT\r\eE\r", ASSETS, 2)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("menu: View Take Pool Exit")
        self.assertEqual(lines[at + 1:at + 3], ["item: Plate Mail ", "item: Shield "])
        self.assertIn("choice: Plate Mail ", lines)
        at = lines.index("print: there is still treasure left. ")
        self.assertEqual(lines[at + 1:at + 3], [
            "print: do you want to go back and claim your treasure?", "menu: ~Yes ~No"])
        self.assertEqual(lines[-1], "(done in block 2)")
        # TREASURE ITEMS (3632:1933): its coins added and its items from the
        # item table (ITEM0), the last first.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--file", 3, "--start", "9569",
                           "--keys", r"\r\r", ASSETS, 80)
        self.assertEqual(result.stdout.splitlines()[:5], [
            "treasure: 1000 Steel", "treasure: item Gauntlets ", "treasure: item Ring ",
            "treasure: item Chain Mail ", "treasure: item Mace "])
        # STOCK (3632:28d2): shop 1's items, the last first and shown, then
        # its shop, which stops the run (3b89:06f3).
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--start",
                           "9109", ASSETS, 36)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout.splitlines()[:2], [
            "treasure: item Banded Mail ", "treasure: item Chain Mail "])
        self.assertEqual(result.stderr.splitlines()[-1], "not yet ported: Death Knights of "
                         "Krynn's shop (dkk 3b89:06f3)")
        # ADD EP 0 (3632:25f1): its words, the selected's experience.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--start",
                           "9796", "--keys", r"\r" * 4, ASSETS, 36)
        self.assertEqual(result.stdout.splitlines()[0],
                         "print: congratulations...SIR DRYDEN gains experience!")

    def test_rob(self):
        # ROB 1 50 0 (ECL1 block 34, the copper dragon, 3632:1a76): half of
        # each record's steel, gems and jewelry, its three coins.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--start", "8755", ASSETS, 34)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("monster: CELESTE robbed, 0 items taken", result.stdout.splitlines())

    def test_vault(self):
        # ECL1 block 35: COMBAT's vault branch (var 0x4d00) opens the vault's
        # menu (3948:000e): mode 7's line, Pool puts the party's money in
        # it, Take lists it, Exit writes the temporary vault (46b2:302d):
        # the pool's steel, gems and jewelry; then NEWECL 36.
        saves = self.saves(ASSETS / "SAVE")
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--saves", saves, "--set",
                           "4be6=1", "--keys", r"PT\eE", ASSETS, 35)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[lines.index("print: bank of gargath") + 1],
                         "menu: View Pool Money Items Exit")
        self.assertIn("menu: View Take Pool Items Exit", lines)
        self.assertIn("item: Steel 3000", lines)
        self.assertEqual(lines[-1], "(done in block 36)")
        self.assertEqual((saves / "TMPVAULT.DAT").read_bytes(),
                         (3000).to_bytes(4, "little") + bytes(8))
        # View and Items, the character's sheet and items in mode 7, stop.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--saves", saves, "--set",
                           "4be6=1", "--keys", "V", ASSETS, 35)
        self.assertEqual(result.stderr.splitlines()[-1], "not yet ported: Death Knights of "
                         "Krynn's the vault's View (dkk 580d:0c66)")

    # Saved game A to block 16's fight: its texts, No rest, the festival's.
    TO_FIGHT = r"\r\rN" + r"\r" * 9

    def test_first_fight(self):
        # The first fight (ECL1 block 16): SETUP MONSTERS 255 0 1 0 shows no
        # sprite (dkk 3c1e:0515); leadership (5340:4acf): SIR DRYDEN, a
        # knight, leads the Solamnic knights; the battlefield on open ground
        # (410b:107e) and every record placed (410b:1aa4), as the original
        # in DOSBox-X places them (tools/dosbox/checks/dkk_fight.py); the
        # rounds' order, then the first turn, the player's, whose menu
        # (3e05:0ccc) is not ported: it waits for a key there, and any but
        # Alt-Q, Alt-A and Alt-X stops the run.
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1, "--keys",
                           self.TO_FIGHT + r"\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("print:   Using leadership ability ")
        self.assertEqual(lines[at - 4:at + 7], [
            "monster: cleared", "monster: 4 SKEL WARRIOR, icon 1 in slot 8",
            "monster: 4 NIGHTMARE, icon 0 in slot 9", "monster: 4 SOLAMNIC KNIGHT, icon 64 in slot 10",
            "print:   Using leadership ability ", "print: SIR DRYDEN leads SOLAMNIC KNIGHT.",
            "print: Press button or enter to continue", "print: a battle begins...",
            "combat: open ground facing N, the enemy 2 squares ahead",
            "combat: 1 SIR DRYDEN at 27,13", "combat: 2 LILLITH at 26,13"])
        self.assertIn("combat: 18 SOLAMNIC KNIGHT at 31,14", lines)
        self.assertEqual([line for line in lines if line.startswith("turn: ")],
                         ["turn: NEACAIL (initiative 8)"])
        self.assertEqual(lines[-1], "(out of keys in block 16 at 8637)")
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1, "--keys",
                           self.TO_FIGHT + r"\r\r", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr.splitlines()[-1], "not yet ported: Death Knights of "
                         "Krynn's player's menu (dkk 3e05:0ccc)")

    def test_first_fight_on_auto(self):
        # Alt-Q at the first turn (3e05:08af) puts the party on Auto: the
        # computer's turns (3f63:0000) play the fight to its end, in 13
        # rounds, with the original's dice, its moves, attacks, the
        # nightmares' smoke (effect 0x6a) and turning undead, as the
        # original in DOSBox-X plays it (dkk_fight.py's auto flow, round by
        # round); the enemy gone, "continue battle?" (No), and the results;
        # --combat auto presses it.
        expected = None
        for keys, args in ((self.TO_FIGHT + r"\r\@q" + r"\r" * 5, ()),
                           (self.TO_FIGHT + r"\r" * 6, ("--combat", "auto"))):
            result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1, *args,
                               "--keys", keys, ASSETS)
            self.assertEqual(result.returncode, 0, result.stderr)
            lines = result.stdout.splitlines()
            rounds = [line for line in lines if line.startswith("round: ")]
            self.assertEqual(len(rounds), 13)
            self.assertIn("print: is blinded by smoke", lines)
            self.assertIn("menu: continue battle? ", lines)
            self.assertIn("print: each character receives 3242", lines)
            fight = lines[lines.index(rounds[0]):lines.index("menu: continue battle? ")]
            if expected is None:
                expected = [line for line in fight if not line.startswith("menu: ")]
            else:
                self.assertEqual([line for line in fight if not line.startswith("menu: ")],
                                 expected)

    def test_first_fight_out(self):
        # Out of it with --combat: won, the experience (3a37:0000, 02fa)
        # and no treasure (3a37:09b7), then NEWECL 36, the outpost; fled,
        # none; lost, the party destroyed.
        run = lambda how: self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1,
                                    "--combat", how, "--keys", self.TO_FIGHT + r"\r" * 5, ASSETS)
        result = run("won")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("combat: won")
        self.assertEqual(lines[at + 5:at + 11], [
            "combat: removed 4 SKEL WARRIOR, 4 NIGHTMARE, 4 SOLAMNIC KNIGHT; 8 dropped",
            "print: the party has won.", "print: each character receives 3242",
            "print: experience points.", "print: the party has found no treasure",
            "menu: Press button or enter to continue"])
        self.assertIn("print: YOU WALK UP TO THE MAIN GATE.", lines)
        self.assertIn("block 36", lines[-1])
        result = run("fled")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("print: the party has fled.", result.stdout.splitlines())
        self.assertIn("print: each character receives 0", result.stdout.splitlines())
        result = run("lost")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-2:], [
            "print: the monsters rejoice for the party has been destroyed",
            "(the party was killed in block 16)"])

    def test_ambush(self):
        # ECL1 block 17's knights surrounded by undead: SETUP MONSTERS 60 0
        # 255 15, an ambush from every side (DS:c4cc), the enemies from
        # each in turn (410b:1aa4), then the six Solamnic knights on the
        # party's side. Where the last enemy faced other than the party
        # (the party facing E), the first knight's formations are laid out
        # again (410b:18c2), which leaves it placed by the enemy's side
        # (DS:86e8 1): out by the enemy's anchor, the rest beside the party,
        # as the original places them (dkk_fight.py's ambushes).
        def knights(direction):
            result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--file", 1, "--start",
                               "893c", "--at", f"5,5,{direction}", "--seed", 1, "--keys",
                               r"\r\r\r", ASSETS, 17)
            self.assertIn("player's menu (dkk 3e05:0ccc)", result.stderr)
            return [line for line in result.stdout.splitlines()
                    if line.startswith("combat: ") and "SOLAMNIC KNIGHT at" in line]
        east = knights(2)
        self.assertEqual(east[:3], ["combat: 31 SOLAMNIC KNIGHT at 39,11",
                                    "combat: 32 SOLAMNIC KNIGHT at 25,13",
                                    "combat: 33 SOLAMNIC KNIGHT at 23,11"])
        # Facing N, the last enemy's way 0 is the party's: none laid out
        # again, all six beside the party.
        self.assertEqual(knights(0)[0], "combat: 31 SOLAMNIC KNIGHT at 29,14")

    def test_fights_in_and_out(self):
        # The blocks that stopped at SETUP MONSTERS: each fight entered and
        # left with --combat, the script going on (block 2's into its
        # treasure menu).
        expected = {2: "(done in block 2)", 51: "(done in block 51)", 67: "(done in block 67)",
                    97: "(done in block 97)", 98: "(done in block 97)"}
        # Those that flee from 51 and 67 are sent back to block 17 (var
        # 0x7ec7 0x81, 3a37:0506).
        fled = {51: "(done in block 17)", 67: "(done in block 17)"}
        for block, last in expected.items():
            for how in ("won", "fled", "lost"):
                result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1",
                                   "--set", "4be7=1", "--set", "4be8=1", "--combat", how,
                                   "--keys", r"\r" * 80, ASSETS, block)
                lines = result.stdout.splitlines()
                with self.subTest(block=block, how=how):
                    self.assertIn(f"combat: {how}", lines)
                    if how == "lost":
                        self.assertIn("(the party was killed in block", lines[-1])
                    elif block == 2 and how == "won":
                        # The treasure menu, where Enter opens View.
                        self.assertIn("menu: View Take Pool Exit", lines)
                    else:
                        self.assertEqual(lines[-1], fled.get(block, last) if how == "fled"
                                         else last, result.stderr)

    def saves(self, *sources):
        """A saves folder holding copies of the files of sources."""
        folder = self.folder / f"saves{len(list(self.folder.glob('saves*')))}"
        folder.mkdir()
        for source in sources:
            for f in pathlib.Path(source).iterdir():
                if f.is_file():
                    (folder / f.name).write_bytes(f.read_bytes())
        return folder

    # Title keys: one for the pictures, one for the credits, then Play.
    TITLE = r"\kx\kxP"

    def test_start_menu(self):
        # From the title (35c6:051c, its credits whatever was pressed, until
        # a key), its menu, the start menu (49cb:010b) with no party: Load
        # Saved Game, from Death Knights' games (DS:306c), A; then Begin
        # Adventuring: the saved game's party in block 16 (its 0x4bf2 is 0,
        # a new game's block, 3632:2e06).
        saves = self.saves(ASSETS / "SAVE")
        keys = self.TITLE + r"\v\v\rDA" + r"\v" * 5 + r"\r\r\r"
        result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        self.assertIn("print: CREATED BY:SSI SPECIAL PROJECTS TEAM\n", out)
        self.assertIn('print: "THE FAT MAN"     DAVE GOVETT\nsound: install\nmenu: Play Demo\n',
                      out)
        self.assertIn("list: \nitem: Create New Character\nitem: Add Character to Party\n"
                      "item: Load Saved Game\nitem: Initialize Mouse/Joystick\n"
                      "item: Exit to DOS\n", out)
        self.assertIn("menu: Death Champions Exit\nchoice: D\nmenu: A B\nchoice: A\n", out)
        self.assertIn("item: Save Current Game\nitem: Begin Adventuring\n", out)
        self.assertIn("choice: Begin Adventuring\nprint: A YEAR HAS PASSED", out)
        self.assertTrue(out.endswith("menu: ~Yes ~No\n(out of keys in block 16 at 817b)\n"),
                        out[-300:])

    def test_saved_games(self):
        # Each saved game loaded and saved again as its letter (46b2:1ef0,
        # 2808): the same file but for the bytes its stack leaves after
        # each name and in the slots past the party (tests/test_start.c
        # compares the rest, and the characters).
        for letter in "AB":
            saves = self.saves(ASSETS / "SAVE")
            keys = self.TITLE + r"\v\v\rD" + letter + r"\v" * 4 + r"\r" + letter
            result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("choice: Save Current Game\nmenu: A B C D E F G H I J\nchoice: "
                          + letter + "\n", result.stdout)
            old = (ASSETS / "SAVE" / f"SAVGAM{letter}.PTY").read_bytes()
            new = (saves / f"SAVGAM{letter}.PTY").read_bytes()
            self.assertEqual((len(old), len(new)), (2913, 2913))
            self.assertEqual(old[:0xa19], new[:0xa19])
            for slot in range(old[0xa18]):
                name = old[0xa19 + 41 * slot:0xa19 + 41 * slot + 1 + old[0xa19 + 41 * slot]]
                self.assertEqual(new[0xa19 + 41 * slot:0xa19 + 41 * slot + len(name)], name)
            self.assertEqual((saves / f"VAULT{letter}.DAT").read_bytes(), bytes(12))

    def test_champions(self):
        # From Champions (DKK.CFG's fifth line; --import): its saved game A
        # loaded, its party imported and Champions' NPCs left out, Begin; its
        # roster added (*.WHO) until six have joined.
        champions = ROOT / "SAVE"
        if not (champions / "SAVGAMA.DAT").exists():
            self.skipTest("no Champions saved games in SAVE")
        saves = self.saves()
        keys = self.TITLE + r"\v\v\rCA" + r"\v" * 5 + r"\r\r"
        result = self.play("--start-menu", "--saves", saves, "--import", champions, "--keys",
                           keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("menu: Death Champions Exit\nchoice: C\nmenu: A B\nchoice: A\n",
                      result.stdout)
        self.assertIn("choice: Begin Adventuring\nprint: A YEAR HAS PASSED", result.stdout)
        roster = self.saves()
        for f in champions.glob("CHRDATA*"):
            name = f.name.replace(".SAV", ".WHO")
            (roster / name).write_bytes(f.read_bytes())
        keys = self.TITLE + r"\v\rC" + r"\r\v" * 6
        result = self.play("--start-menu", "--saves", saves, "--import", roster, "--keys", keys,
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("list: add a character: \nitem: SIR STRONGSWORD\n", result.stdout)
        self.assertEqual(result.stdout.count(" joins\n"), 6)

    def test_no_woof(self):
        # Death Knights has no debug mode: --woof is refused.
        result = self.play("--woof", "--start-menu", ASSETS)
        self.assertEqual((result.returncode, result.stderr),
                         (2, "--woof: Death Knights of Krynn has no Woof (its debug mode)\n"))

    def test_program_ends_game(self):
        # PROGRAM 0 (ECL1 block 17's trainers, 917b): Remove the only
        # member empties the party, which ends the game (49cb:010b, DS:8c5b);
        # the script stops there and runs no more (3632:2cd5): the trainers'
        # parting words never print. Add it back and Begin.
        saves = self.saves()
        keys = r"\v\v\v\v\v\r\r\v\r\r\r\e\v\v\v\v\v\v\r"
        result = self.play("--character", ASSETS / "SAVE/CHRDATA1.GAM", "--saves", saves,
                           "--start", "917b", "--keys", keys, ASSETS, 17)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("roster: SIRDRYDE.PC\n", result.stdout)
        self.assertIn("choice: Begin Adventuring\n", result.stdout)
        self.assertNotIn("THEY COLLECT THEIR THINGS", result.stdout)
        self.assertTrue(result.stdout.endswith("(done in block 17)\n"), result.stdout[-200:])

    def test_unported_stops(self):
        # What is not ported stops the run, naming the routine: creating a
        # character (49cb:05c7), and in block 16 the fight (SETUP MONSTERS).
        result = self.play("--start-menu", "--keys", self.TITLE + r"\r", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "not yet ported: Death Knights of Krynn's Create New "
                         "Character (dkk 49cb:05c7)\n")
        # Camp's Magic, Display (4381:15aa, not ported).
        keys = self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\rMD\e\e"
        result = self.play("--start-menu", "--saves", self.saves(ASSETS / "SAVE"), "--keys", keys,
                           ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "not yet ported: Death Knights of Krynn's Display "
                         "(dkk 4381:15aa)\n")
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--keys", r"\r\r" + "N" +
                           r"\r" * 11, ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr.splitlines()[-1], "not yet ported: Death Knights of "
                         "Krynn's player's menu (dkk 3e05:0ccc)")
        if (BUILD / "cok").exists():
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            result = subprocess.run([str(BUILD / "cok"), *GAME, "--saves", self.saves(),
                                     "--no-delay", "--keys", self.TITLE + r"\r"],
                                    capture_output=True, text=True, env=env, cwd=ROOT)
            self.assertEqual(result.returncode, 1)
            self.assertIn("not yet ported: Death Knights of Krynn's Create New Character "
                          "(dkk 49cb:05c7)", result.stderr)

    def test_view_by_wall_id(self):
        # Block 48 in a 3D area: LOAD PIECES 2 loads wall set 2 from
        # WALLDEF4, its 256 tiles from 8X8D4 and its backdrop from BACK4 (the
        # files its id names), into slot 0.
        screen = self.folder / "48.bmp"
        result = self.play("--set", "4be6=1", "--screen", screen, "--keys", r"\r", ASSETS, 48)
        self.assertEqual(result.returncode, 0, result.stderr)
        bmp = screen.read_bytes()
        self.assertEqual(view(bmp), "7beafb524a8cae135560bc86a6ead245")
        # Another wall set and file: block 64 loads wall set 4 (WALLDEF2), the
        # ground by day under the sky.
        screen = self.folder / "64.bmp"
        result = self.play("--set", "4be6=1", "--screen", screen, "--keys", r"\r" * 4, ASSETS, 64)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(view(screen.read_bytes()), "3996456907014c8c014375b745c732a6")
        # Past hour 13 the night sky (SKYGRND 1) over the ground's top half.
        screen = self.folder / "64n.bmp"
        result = self.play("--set", "4be6=1", "--set", "4bc9=e", "--screen", screen, "--keys",
                           r"\r" * 4, ASSETS, 64)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(view(screen.read_bytes()), "30d67fd51eee9fb04d844d4ee56e6766")

    def test_views_seen_in_the_original(self):
        # Two of the views tools/dosbox/checks/dkk_views.py found the same in
        # the original, pixel for pixel: block 32's street (wall set 2, from
        # WALLDEF4, 8X8D4) facing north, out of doors (SKYGRND's ground,
        # opaque), and block 36's hall (wall set 1) at 5,4, indoors (BACK1).
        # Each the block's load vector from LOAD FILES, its GOSUB made a CALL
        # that does nothing and an EXIT after CALL [2dcb], on the square.
        runs = {
            "49b93cb70f6a48ca2072ed80efd0f584": (32, "8040", {"804e": "2d", "806e": "0",
                                                              "805a": "6", "8060": "f",
                                                              "8066": "0", "4bfb": "0",
                                                              "4bfe": "a"}),
            "ac9292e1e6ccd57541d343e87e85b215": (36, "805a", {"8068": "2d", "8089": "0",
                                                              "8074": "5", "807a": "4",
                                                              "8080": "0", "4bfe": "c"}),
        }
        for digest, (block, start, sets) in runs.items():
            screen = self.folder / f"{block}.bmp"
            args = ["--file", 1, "--start", start, "--screen", screen]
            for k, v in {"4be6": "1", "4be7": "1", "4be8": "1", "4be9": "1", "4bfd": "4",
                         **sets}.items():
                args += ["--set", f"{k}={v}"]
            result = self.play(*args, ASSETS, block)
            self.assertEqual(result.stdout.splitlines()[-1], f"(done in block {block})")
            self.assertEqual(view(screen.read_bytes()), digest)

    def test_overland_map(self):
        # PICTURE 0x70 is the overland map (BIGPIC1), with the cursor,
        # 8X8D1 record 204, on the party's place.
        screen = self.folder / "17.bmp"
        result = self.play("--set", "4bf2=17", "--screen", screen, ASSETS, 17)
        self.assertEqual(result.returncode, 0, result.stderr)
        bmp = screen.read_bytes()
        self.assertNotEqual(pixel(bmp, 160, 60), (0, 0, 0))


if __name__ == "__main__":
    if not (ASSETS / "START.EXE").exists():
        print("death knights: DKKAssets not found; its scripts' and tools' checks skipped")
        sys.exit(0)
    unittest.main()

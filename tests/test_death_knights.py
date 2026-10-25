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
        # computer's turns cast spells of use (5340:43ea): FLAVIUS's Delay
        # Blast Fireball (5dc5:3241, its caster level added to the damage),
        # then Flesh to Stone (5dc5:40a6), aborted with no target in
        # reach, to the end of the fight. Its side is NPCs alone (+0x3e
        # 0x80 and up), which 3a37:0506 does not count: the party is
        # destroyed though every enemy dropped; then 3632:2e06's menu after
        # the load vector, in mode 3 without the overland menu, reads no key
        # (4307:0257) and the game ends.
        result = self.play("--keys", r"\r" * 20, ASSETS, 2)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-3:], [
            "combat: removed 2 EVIL WARLORD, 1 DREAD WOLF, 8 GHOUL; 11 dropped",
            "print: the monsters rejoice for the party has been destroyed",
            "(the party was killed in block 2)"])
        self.assertEqual([line for line in result.stdout.splitlines()
                          if line.startswith("party:")],
                         ["party: SIR DRYDEN joins, icon 48 in slot 0",
                          "party: TANAR joins, icon 49 in slot 1",
                          "party: FLAVIUS joins, icon 50 in slot 2"])
        lines = result.stdout.splitlines()
        at = lines.index("cast: FLAVIUS: Delay Blast Fireball")
        self.assertEqual([line for line in lines[at + 1:] if not line.startswith("sound")][:7],
                         ["print: FLAVIUS", "print: casts a spell",
                          "print: Spell:Delay Blast Fireball", "print: EVIL WARLORD",
                          "print: takes 44 points of damagefrom fire", "print: EVIL WARLORD",
                          "print: takes 44 points of damagefrom fire"])
        at = lines.index("cast: FLAVIUS: Flesh to Stone")
        self.assertEqual(lines[at + 1:at + 5], ["print: FLAVIUS", "print: casts a spell",
                                                "print: Spell:Flesh to Stone",
                                                "print: Spell Aborted"])

    def test_characters(self):
        # The saved characters (--character), a sheet in camp: name, class,
        # items, the readied column a space shorter than Champions'
        # (65cd:047f).
        party = [arg for n in range(1, 7)
                 for arg in ("--character", ASSETS / f"SAVE/CHRDATA{n}.GAM")]
        result = self.play(*party, "--play", "--keys", r"ev\r\e", ASSETS, 17)
        lines = result.stdout.splitlines()
        self.assertIn("menu: Save View Magic Rest Alter Fix Exit", lines)
        at = lines.index("print: SIR DRYDEN")
        self.assertEqual(lines[at:at + 7], [
            "print: SIR DRYDEN", "print: Knight of the Crown", "menu: Items Trade Drop Exit",
            "list: Items", "item:  Yes Long Sword +1 ", "item:  Yes Shield ",
            "item:  Yes Solamnic Plate Mail "])

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
            "print: do you want to go back and claim your treasure?", "menu: "])
        self.assertEqual(lines[-1], "(done in block 2)")
        # Detect, offered once the selection (Down, 65cd:0959) is FLAVIUS,
        # who has Detect Magic memorized, casts it (5dc5:1298).
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--combat", "won", "--keys",
                           r"\r\r\#50\#50\#50D\r\r", ASSETS, 2)
        lines = result.stdout.splitlines()
        at = lines.index("menu: View Take Pool Detect Exit")
        self.assertEqual(lines[at + 1:at + 4], ["print: FLAVIUS", "print: is affected",
                                                "menu: View Take Pool Exit"])
        # TREASURE ITEMS (3632:1933): its coins added and its items from the
        # item table (ITEM0), the last first.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--file", 3, "--start", "9569",
                           "--keys", r"\r\r", ASSETS, 80)
        self.assertEqual(result.stdout.splitlines()[:5], [
            "treasure: 1000 Steel", "treasure: item Gauntlets ", "treasure: item Ring ",
            "treasure: item Chain Mail ", "treasure: item Mace "])
        # STOCK (3632:28d2): shop 1's items, the last first and shown, then
        # its shop (3b89:06f3), which Escape leaves.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--start",
                           "9109", "--keys", r"\e", ASSETS, 36)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:2], ["treasure: item Banded Mail ", "treasure: item Chain Mail "])
        self.assertIn("menu: Buy View Pool Appraise Exit", lines)
        # ADD EP 0 (3632:25f1): its words, the selected's experience.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--start",
                           "9796", "--keys", r"\r" * 4, ASSETS, 36)
        self.assertEqual(result.stdout.splitlines()[0],
                         "print: congratulations...SIR DRYDEN gains experience!")

    def test_shop(self):
        # ECL1 block 36's weapons store (STOCK 1 at prices of 16, 3b89:06f3):
        # Buy (0453) from SIR DRYDEN's steel, View's Items: Sell (580d:2077)
        # and Id (21d3) in mode 1, Appraise with none (5c72:0dc2), Exit.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--start",
                           "9103", "--keys", r"B\r\eVI\v\v\vS\rI\r\e\eAE\r", ASSETS, 36)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("shop: prices at 16")
        self.assertEqual(lines[at + 1:at + 5], [
            "menu: Buy View Pool Appraise Exit", "choice: B", "print: Personal funds:       500",
            "print: Pooled Funds:           0"])
        self.assertIn("item: Banded Mail                 40", lines)
        at = lines.index("shop: SIR DRYDEN pays 40 steel")
        self.assertEqual(lines[at + 1:at + 3], ["print: SIR DRYDEN buys a Banded Mail ",
                                                "print: Personal funds:       460"])
        self.assertIn("menu: Ready Trade Drop Halve Join Sell Id Exit", lines)
        at = lines.index("print: I'll give you 750 steel pieces for your Mace +1 ")
        self.assertEqual(lines[at + 1:at + 5], ["menu: is it a deal? ", "choice: Y", "print: sold!",
                                                "shop: sold for 750 steel"])
        at = lines.index("print: for 100 steel pieces i'll identify your Banded Mail ")
        self.assertEqual(lines[at + 3:at + 5], [
            "shop: identified for 100 steel",
            "print: i cannot tell anything new about your Banded Mail "])
        self.assertIn("print: no gems or jewelry", lines)
        self.assertEqual(lines[-1], "(done in block 36)")

    def test_temple(self):
        # ECL1 block 36's temple (COMBAT with 0x7ee2, 3999:07d3): Heal
        # (04e2) lists the spells' names; Cure Blindness for one not blind
        # asks (3999:0000), No; Cure Light Wounds is free (00b0).
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--start",
                           "867f", "--keys", r"YHH\r\v\vH\eE\r", ASSETS, 36)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("menu: Heal View Pool Appraise Exit")
        self.assertEqual(lines[at + 1:at + 6], [
            "choice: H", "print: SIR DRYDEN", "print: 95", "print: /95",
            "print: SIR DRYDEN, how can we help yo"])
        self.assertEqual(lines[at + 6:at + 8], ["item: Cure Blindness", "item: Cure Disease"])
        self.assertIn("item: Restoration", lines)
        at = lines.index("choice: Cure Blindness")
        self.assertEqual(lines[at + 1:at + 5], ["print: SIR DRYDEN", "print: is not blind.",
                                                "menu: cast cure anyway? ", "choice: N"])
        at = lines.index("choice: Cure Light Wounds")
        self.assertEqual(lines[at + 6:at + 9], ["menu: Heal View Pool Appraise Exit", "choice: E",
                                                "print: 'GODS SPEED YOU BACK.'"])
        self.assertEqual(lines[-1], "(done in block 36)")

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
        # View and Items in mode 7 (580d:0c66, 0ffa): Deposit for Drop; the
        # money and the mace deposited go to the pool, which Exit writes.
        saves = self.saves(ASSETS / "SAVE")
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--saves", saves, "--set",
                           "4be6=1", "--keys", r"VD\r2\r\e\eI\v\v\vD\eE", ASSETS, 35)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("menu: Items Trade Deposit Exit", lines)
        self.assertIn("menu: How much Steel will you deposit? ", lines)
        self.assertIn("menu: Ready Trade Deposit Halve Join Exit", lines)
        self.assertIn("print: Your Mace +1 will be left in the vault", lines)
        self.assertEqual(lines[-1], "(done in block 36)")
        vault = (saves / "TMPVAULT.DAT").read_bytes()
        self.assertEqual(vault[:12], (2).to_bytes(4, "little") + bytes(8))
        self.assertEqual(len(vault), 12 + 63)
        self.assertEqual(vault[12:12 + 9], b"\x08Mace +1 ")

    # Saved game A to block 16's fight: its texts, No rest, the festival's.
    TO_FIGHT = r"\r\rN" + r"\r" * 9

    def test_first_fight(self):
        # The first fight (ECL1 block 16): SETUP MONSTERS 255 0 1 0 shows no
        # sprite (dkk 3c1e:0515); leadership (5340:4acf): SIR DRYDEN, a
        # knight, leads the Solamnic knights; the battlefield on open ground
        # (410b:107e) and every record placed (410b:1aa4), as the original
        # in DOSBox-X places them (tools/dosbox/checks/dkk_fight.py); the
        # rounds' order, then the first turn, the player's, at its menu
        # (3e05:0ccc: two rows by index, NEACAIL a kender with spells).
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
        self.assertIn("menu: Move Aim Use Cast Guard Quick@Delay Yell View Speed End", lines)
        self.assertEqual(lines[-1], "(out of keys in block 16 at 8637)")

    def test_adventure_menu(self):
        # The adventure's menu (dkk 4307:0257) with the menu loop by index
        # (6a69:0f1b): Move's "Exit" left by Escape, a special key there;
        # Down, a special key that keeps the character selected (580d:2bb2:
        # Down picked LILLITH); Right moves the selection (to Area) and reads
        # on (6a69:0c59), and the menu keeps its own selection: Enter is
        # Area.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--play",
                           "--keys", r"\rM\e\#50\#4d\r\#50V", ASSETS, 48)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("music")]
        menu = "menu: Move Area Cast View Encamp Search Look"
        at = lines.index(menu)
        self.assertEqual(lines[at:at + 6], [menu, "menu: Exit", menu, menu, "area: on", menu])
        self.assertEqual(lines[-4:-1], ["print: TANAR", "print: Majere Cleric/Ranger",
                                        "menu: Items Spells Trade Drop Exit"])
        # Encamp: camp's menu from its first item (dkk 4381:3106's own),
        # whatever the adventure's selection: Enter is Save.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--play",
                           "--keys", r"\rE\r", ASSETS, 48)
        self.assertEqual(result.stdout.splitlines()[-3:-1], [
            "menu: Save View Magic Rest Alter Fix Exit", "menu: A B C D E F G H I J"])

    def test_overland_menu(self):
        # The overland's menu (dkk 4307:0257 in mode 3, var 0x4cf7): Escape
        # in Move ends the command (a step) and leaves Move: "Move Encamp"
        # again. Move's "Exit" neither puts back nor takes the menu's own
        # selection: camp (Ctrl-F8 in Move) left on View, Exit, and the
        # menu shows from Encamp: Enter camps.
        def menus(keys):
            result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4cf7=1",
                               "--play", "--keys", keys, ASSETS, 17)
            self.assertEqual(result.returncode, 0, result.stderr)
            return [line for line in result.stdout.splitlines()
                    if line.startswith(("menu: ", "print: the party"))]
        self.assertEqual(menus(r"M\eM\#48")[-5:], ["menu: Move Encamp", "menu: Exit",
                                                    "menu: Move Encamp", "menu: Exit",
                                                    "menu: Exit"])
        self.assertEqual(menus(r"M\#65\#4d\e\r\r")[-7:], [
            "menu: Exit", "print: the party makes camp",
            "menu: Save View Magic Rest Alter Fix Exit", "menu: Exit", "menu: Move Encamp",
            "print: the party makes camp", "menu: Save View Magic Rest Alter Fix Exit"])

    def test_first_fight_by_keys(self):
        # The player's turn by keys (3e05:08af), as dkk_fight.py's keys flow
        # plays it in the original: the two-row menu by index (3e05:0ccc;
        # Down, Right and Up between its rows, which read on), Move's step and Escape's
        # undo (3e05:10bb, 1001), Aim's targets, Next, Prev, the cursor
        # (5340:3888, 2f3d) and Center, Speed (3e05:14aa), Delay, Guard, End
        # and Quick; the original's dice after (RandSeed).
        keys = self.TO_FIGHT + r"\r\#50\#4d\#48M2\eANPM8\eCESFEDGEQ"
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1, "--keys", keys,
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        lines = [line for line in lines[lines.index("turn: NEACAIL (initiative 8)"):]
                 if not line.startswith(("sound: ", "effect: "))]
        menu = "menu: Move Aim Use Cast Guard Quick@Delay Yell View Speed End"
        aim = "menu: Next Prev Manual Center Exit"
        self.assertEqual(lines, [
            "turn: NEACAIL (initiative 8)", menu, "menu: Done", "menu: Done",
            menu, aim, "menu: Next Prev Manual Target Center Exit", aim, "menu: Exit",
            "menu: Exit", aim, aim, menu, "menu: Faster Slower Exit",
            "menu: Faster Slower Exit", menu, "turn: CELESTE (initiative 8)",
            "menu: Move Aim Use Cast Guard Quick@Delay View Speed End", "print: guarding",
            "turn: SIR DRYDEN (initiative 7)",
            "menu: Move Aim Use Guard Quick@Delay View Speed End",
            "turn: SOLAMNIC KNIGHT (initiative 6)",
            "menu: Move Aim Use Guard Quick@Delay View Speed End", "print: guarding",
            "turn: TANAR (initiative 6)",
            "menu: Move Aim Use Cast Guard Quick@Delay View Speed End",
            "combat: seed 3410845111", "(out of keys in block 16 at 8637)"])
        # The actor's cursor at the first menu: COMSPR 15's box in slot 0x1c,
        # its dark gray edge recoloured black (6f03:036c, 7090:016f), as the
        # original draws it.
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1, "--display",
                           "ega", "--shots", shots, "--keys", self.TO_FIGHT + r"\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        bmp = max(shots.glob("*.bmp"), key=lambda p: int(p.stem)).read_bytes()
        self.assertEqual([pixel(bmp, x, y) for x, y in ((104, 104), (106, 105), (126, 110),
                                                        (105, 127))],
                         [(255, 255, 255), (170, 170, 170), (0, 0, 0), (0, 0, 0)])
        # Cast (5dc5:1298): CELESTE's Magic Missile, "casts a spell"
        # (5dc5:552a), aimed (5340:1eeb) and Target: its damage in the
        # game's words (638c:1c52: " points of damage" and "from magic"
        # run together).
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1, "--keys",
                           self.TO_FIGHT + r"\rYC" + r"\v" * 6 + r"\rT", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("cast: CELESTE: Magic Missile")
        self.assertEqual(lines[at + 1:at + 5], ["print: CELESTE", "print: casts a spell",
                                                "print: Spell:Magic Missile",
                                                "menu: Next Prev Manual Target Center Exit"])
        self.assertTrue(any(line.startswith("print: takes ") and line.endswith(
            " points of damagefrom magic") for line in lines[at:]), lines[at:])

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
        # none; lost, the party destroyed, and the adventure's menu read once
        # more, the fight being in the load vector (3632:2e06 reads it,
        # 4307:0257, before it tests DS:8c5a): "Move Encamp" in the open,
        # Encamp ending the game.
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
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1, "--combat",
                           "lost", "--keys", self.TO_FIGHT + r"\r\rE", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-3:], [
            "print: the monsters rejoice for the party has been destroyed",
            "menu: Move Encamp", "(the party was killed in block 16)"])
        # At that menu the text window is not drawn over: the view draws
        # nothing once the party is destroyed (6c84:00a6 tests DS:8c5a), as
        # the original's screen shows (DOSBox-X, dkk_fight.py's lost flow).
        shot = self.folder / "lost.bmp"
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", "--seed", 1, "--combat",
                           "lost", "--screen", shot, "--keys", self.TO_FIGHT + r"\r\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-2], "menu: Move Encamp")
        bmp = shot.read_bytes()
        self.assertEqual({pixel(bmp, x, y) for x in range(8, 312, 4) for y in range(64, 136, 4)},
                         {(0, 0, 0)})

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
            self.assertEqual(result.returncode, 0, result.stderr)
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

    def test_title_menu(self):
        # The title's menu by index (1000:0118, 6a69:0f1b with DS:c0fa): the
        # time running out gives DS:c0fe's 1, Demo, as do D and Enter on
        # Demo; any other result plays, F10 (Champions' Demo, its scan code
        # 0x44 its D) and Escape too. The demonstration reads ECL5, which
        # the game does not ship (3632:2e06).
        saves = self.saves(ASSETS / "SAVE")
        for keys, demo in ((r"\t", True), (r"D", True), (r"\>\r", True), (r"\>\t", True),
                           (r"\#44", False), (r"\e", False), (r"P", False), (r"\r", False)):
            result = self.play("--start-menu", "--saves", saves, "--keys", r"\kx\kx" + keys,
                               ASSETS)
            if demo:
                self.assertIn("ECL5.DAX: file read failed", result.stderr)
            else:
                self.assertEqual(result.returncode, 0, result.stderr)
            out = result.stdout
            self.assertIn("menu: Play Demo\nchoice: " + ("Demo\n" if demo else ""), out)
            self.assertEqual("item: Create New Character\n" in out, not demo, keys)

    def test_lost_to_start_menu(self):
        # The first fight lost from the start menu's Begin: "the monsters
        # rejoice", the adventure's menu once more (3632:2e06's 4307:0257
        # before DS:8c5a), Encamp, then the start menu again with no party
        # (DOSBox-X: dkk_fight.py's lost flow).
        saves = self.saves(ASSETS / "SAVE")
        keys = self.TITLE + r"\v\v\rDA" + r"\v" * 5 + r"\r" + self.TO_FIGHT + r"\r\rE"
        result = self.play("--start-menu", "--saves", saves, "--seed", 1, "--combat", "lost",
                           "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        self.assertIn("print: the monsters rejoice for the party has been destroyed\n"
                      "menu: Move Encamp\nlist: \nitem: Create New Character\n"
                      "item: Add Character to Party\nitem: Load Saved Game\n"
                      "item: Initialize Mouse/Joystick\nitem: Exit to DOS\n"
                      "(out of keys in the start menu)\n", out)

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

    def test_save_and_load_menus(self):
        # The front end's menus by index (6a69:0f1b), as dkk_levels.py's
        # saves and load flows play them. Camp's Save (46b2:2808): Escape
        # saves none (then Quit to DOS, No); F7 (scan code 0x41, Champions'
        # A) is read past, Right twice from A: C; Left from A wraps to J.
        camp = self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\r"
        which = "menu: A B C D E F G H I J\n"
        saves = self.saves(ASSETS / "SAVE")
        result = self.play("--start-menu", "--saves", saves, "--seed", 1, "--keys",
                           camp + r"S\e\rS\#41\>\>\r\rS\<\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(which + "menu: Quit to DOS \nchoice: N\n", result.stdout)
        self.assertIn(which + which + "choice: C\n", result.stdout)
        self.assertIn(which + "choice: J\n", result.stdout)
        for letter in "CJ":
            self.assertTrue((saves / f"SAVGAM{letter}.PTY").exists(), letter)
        self.assertEqual((saves / "SAVGAMA.PTY").read_bytes(),
                         (ASSETS / "SAVE" / "SAVGAMA.PTY").read_bytes())
        # Load (46b2:1ef0): those there by index, F7 read past, Right: B;
        # Escape leaves, the start menu again. Then View's Pick Character
        # (580d:3067): Select alone, E no key, Escape its Exit.
        start = self.TITLE + r"\v\v\r\r"
        result = self.play("--start-menu", "--saves", self.saves(ASSETS / "SAVE"), "--keys",
                           start + r"\#41\>\r\v\rE\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        self.assertIn("menu: A B\nmenu: A B\nchoice: B\n", out)
        self.assertIn("choice: View Character\nmenu: Select\nlist: \n", out)
        self.assertTrue(out.endswith("item: Exit to DOS\n(out of keys in the start menu)\n"),
                        out[-200:])
        result = self.play("--start-menu", "--saves", self.saves(ASSETS / "SAVE"), "--keys",
                           start + r"\e\r\r\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("menu: A B\nlist: \nitem: Create New Character\n", result.stdout)
        self.assertIn("menu: A B\nchoice: A\n", result.stdout)
        # A special key but Escape in "load from where? " goes on with a path
        # never set (46b2:1ef0's [bp-0x326]): the run stops.
        result = self.play("--start-menu", "--saves", self.saves(ASSETS / "SAVE"), "--keys",
                           self.TITLE + r"\v\v\r\#48", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("46b2:1ef0's [bp-0x326]", result.stderr)

    def test_load_with_a_gap(self):
        # Load's list holds the letters' indexes (46b2:204f, DS:c120), not
        # their places: with saved games A and C alone it shows A C, and
        # Right, Enter loads C (B's party, saved as C first), saved again
        # as D the same.
        saves = self.saves(ASSETS / "SAVE")
        result = self.play("--start-menu", "--saves", saves, "--keys",
                           self.TITLE + r"\v\v\rDB" + r"\v" * 4 + r"\rC", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        for f in saves.glob("*B*"):
            f.unlink()
        result = self.play("--start-menu", "--saves", saves, "--keys",
                           self.TITLE + r"\v\v\r\r\>\r" + r"\v" * 4 + r"\rD", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("menu: A C\nchoice: C\n", result.stdout)
        old = (saves / "SAVGAMC.PTY").read_bytes()
        new = (saves / "SAVGAMD.PTY").read_bytes()
        self.assertEqual(old[:0xa19], new[:0xa19])
        self.assertNotEqual(old[:0xa19], (saves / "SAVGAMA.PTY").read_bytes()[:0xa19])

    def test_add_from_where_special(self):
        # Add from where (49cb:343c) with a special key but Escape (Up,
        # 0x48) goes on with DS:c468 as the last Save, Load or Add left it:
        # Death's roster after Death, Champions' after Champions; before
        # any, DS:c468 was never set (past the image EXEPACK unpacks, and no
        # startup code clears it): the run stops.
        saves = self.saves()
        (saves / "DRYDEN.PC").write_bytes((ASSETS / "SAVE" / "CHRDATA1.GAM").read_bytes())
        result = self.play("--start-menu", "--saves", saves, "--keys",
                           self.TITLE + r"\v\r\#48", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("DS:c468, never set since startup", result.stderr)
        add = "choice: Add Character to Party\nmenu: Death Champions Exit\n"
        result = self.play("--start-menu", "--saves", saves, "--keys",
                           self.TITLE + r"\v\r\r\e\r\#48\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("list: add a character: \nitem: SIR DRYDEN"), 2)
        self.assertIn(add + "list: add a character: \nitem: SIR DRYDEN", result.stdout)
        champions = ROOT / "SAVE"
        if not (champions / "CHRDATA1.SAV").exists():
            self.skipTest("no Champions characters in SAVE")
        roster = self.saves()
        for f in champions.glob("CHRDATA*.SAV"):
            (roster / f.name.replace(".SAV", ".WHO")).write_bytes(f.read_bytes())
        result = self.play("--start-menu", "--saves", saves, "--import", roster, "--keys",
                           self.TITLE + r"\v\r\>\r\e\r\#48\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(add + "choice: C\nlist: add a character: \nitem: SIR STRONGSWORD",
                      result.stdout)
        self.assertIn(add + "list: add a character: \nitem: SIR STRONGSWORD", result.stdout)

    def test_display_exit(self):
        # Display (4381:15aa) by 6a69:1b82: its Exit, index 0, highlighted
        # from column 0 of row 24 (Champions' list draws " Exit" from 1).
        screen = self.folder / "display.bmp"
        keys = self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\rMD"
        result = self.play("--start-menu", "--saves", self.saves(ASSETS / "SAVE"), "--screen",
                           screen, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        bmp = screen.read_bytes()
        self.assertNotEqual(pixel(bmp, 0, 199), (0, 0, 0))
        self.assertNotEqual(pixel(bmp, 31, 199), (0, 0, 0))
        self.assertEqual(pixel(bmp, 32, 199), (0, 0, 0))
        # LILLITH's effect three times: 21 rows, one more than the window's
        # 19, so Next (index 0) shows and pages; then Exit (1) leaves to
        # Magic's menu.
        saves = self.saves(ASSETS / "SAVE")
        (saves / "CHRDATA2.EFF").write_bytes((saves / "CHRDATA2.EFF").read_bytes() * 3)
        result = self.play("--start-menu", "--saves", saves, "--keys", keys + "NE", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("item:  Protection From Evil, 10' Radius\n"), 3)
        self.assertTrue(result.stdout.endswith(
            "menu: Cast Memorize Scribe Display Rest Exit\n(out of keys in block 16 at 8189)\n"),
            result.stdout[-300:])

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
        keys = self.TITLE + r"\v\rC" + r"A\v" * 6
        result = self.play("--start-menu", "--saves", saves, "--import", roster, "--keys", keys,
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("list: add a character: \nitem: SIR STRONGSWORD\n", result.stdout)
        self.assertEqual(result.stdout.count(" joins\n"), 6)
        # The list by index (49cb:34db, 6a69:1b82) starts on the item "from
        # where" left selected (DS:c0f7): Champions, the second, is Exit
        # here, which Enter picks: no one joins and the start menu is back.
        result = self.play("--start-menu", "--saves", saves, "--import", roster, "--keys",
                           self.TITLE + r"\v\rC\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(" joins\n", result.stdout)
        at = result.stdout.index("list: add a character: ")
        self.assertIn("item: SIRRION        \nlist: \nitem: Create New Character\n",
                      result.stdout[at:])
        # Escape leaves too (the special 0x1b as 1).
        result = self.play("--start-menu", "--saves", saves, "--import", roster, "--keys",
                           self.TITLE + r"\v\rCA\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count(" joins\n"), 1)
        self.assertTrue(
            result.stdout.endswith("item: Exit to DOS\n(out of keys in the start menu)\n"),
            result.stdout[-200:])

    def hall_saves(self):
        """SAVE's games with game A's hall variable 0x7ea8 at 127 (every
        class: Train and Knight Change Order show), and FLAVIUS (cleric 6,
        fighter 7, mage 6) with 100,000 experience."""
        saves = self.saves(ASSETS / "SAVE")
        game = bytearray((saves / "SAVGAMA.PTY").read_bytes())
        game[1 + 0x400 + 0x2A8] = 127  # var 0x7ea8: the 0x7c00 block's bytes
        (saves / "SAVGAMA.PTY").write_bytes(bytes(game))
        flavius = bytearray((saves / "CHRDATA4.GAM").read_bytes())
        struct.pack_into("<I", flavius, 0x62, 100000)
        (saves / "CHRDATA4.GAM").write_bytes(bytes(flavius))
        return saves

    def test_training(self):
        # Knight Change Order (49cb:4ea4): SIR DRYDEN, a knight 7 of the
        # Crown, may become a knight of the Sword: Change. Train Character
        # (49cb:43b6): FLAVIUS's cleric and mage rise to 7 (his fighter
        # needs 125,001), Yes first, the mage learns the spell the list
        # starts on, the last shown (Bestow Curse, 100: it starts with a
        # heading);
        # then Save Current Game C, and Train again: too little experience.
        saves = self.hall_saves()
        keys = (self.TITLE + r"\v\v\rDA" + r"\v\v\r\rC" + r"\^\r\v\v\v\r\r\r" +
                r"\v" * 5 + r"\rC" + r"\^" * 5 + r"\r\v\v\v\r")
        result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        self.assertIn("item: Train Character\nitem: Knight Change Order\n", out)
        self.assertIn("choice: Knight Change Order\nmenu: Select\nprint: SIR DRYDEN\n"
                      "print: is petitioning to become a\nprint: knight of the sword\n"
                      "print: SIR DRYDEN may become a\nprint: knight of the sword\n"
                      "menu: Change Exit\n", out)
        self.assertIn("print:  will become:\nprint:     a level 7 Cleric\n"
                      "print: and a level 7 Mage\nmenu: do you wish to train? \nchoice: Y\n"
                      "print: congratulations...\nlist: Spells to Choose\nheading: 1st Level\n"
                      "item:   Friends\n", out)
        self.assertIn("choice: 100\n", out)
        self.assertTrue(out.endswith("print: not enough experienc\nlist: \n" + "".join(
            f"item: {i}\n" for i in self.MENU) + "(out of keys in the start menu)\n"),
            out[-500:])
        dryden = (saves / "CHRDATC1.GAM").read_bytes()
        self.assertEqual((dryden[0x22], dryden[0x4C]), (2, 7))  # the Sword, still 7
        flavius = (saves / "CHRDATC4.GAM").read_bytes()
        self.assertEqual(list(flavius[0x45:0x4D]), [7, 0, 7, 0, 0, 7, 0, 0])
        self.assertEqual(flavius[0xC6], 0)  # may not train
        wiz = (saves / "CHRDATC4.WIZ").read_bytes()
        self.assertEqual(wiz[0x49 + 100], 1)  # Bestow Curse known

    MENU = ["Create New Character", "Drop Character", "Modify Character", "Train Character",
            "Knight Change Order", "View Character", "Add Character to Party",
            "Remove Character from Party", "Save Current Game", "Begin Adventuring",
            "Exit to DOS"]

    def test_magic(self):
        # Camp (block 16's rest): Magic's Memorize for FLAVIUS with a spell
        # marked (Bless, 0x81): the list of those marked asks with Keep
        # (4381:0f67's mode 5, "spells OK? "), Escape unmarks it, then the
        # grimoire; Display (4381:15aa) names each member's effects; Fix
        # with no one hurt does nothing (4381:2fd4).
        saves = self.saves(ASSETS / "SAVE")
        wiz = bytearray((saves / "CHRDATA4.WIZ").read_bytes())
        wiz[wiz.index(0)] = 0x81
        (saves / "CHRDATA4.WIZ").write_bytes(bytes(wiz))
        keys = (self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\rM\v\v\vM\e\eD\e\eF")
        result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        self.assertIn("list: Spells to Memorize\nheading: 1st Level\nitem:  *Bless\n", out)
        self.assertIn("list: Spells in Grimoire\nheading: 1st Level\nitem:   Bless\n", out)
        self.assertIn("heading: LILLITH\nitem:  Protection From Evil, 10' Radius\n", out)
        # Its blank rows are headings too (4381:13fb with 1).
        self.assertIn("heading:  \nheading: SIR DRYDEN\nitem:  <No Spell Effects>\nheading:  \n",
                      out)
        self.assertTrue(out.endswith("menu: Save View Magic Rest Alter Fix Exit\n"
                                     "menu: Save View Magic Rest Alter Fix Exit\n"
                                     "(out of keys in block 16 at 8189)\n"), out[-200:])

    def test_fix(self):
        # Camp's Fix (4381:2fd4) as dkk_levels.py's fix flow plays it in the
        # original: SIR DRYDEN dead, LILLITH stoned, TANAR poisoned and of
        # effect 0x3d, FLAVIUS cursed (0x24) and poisoned, CELESTE blind and
        # hurt; NEACAIL's spells the cures, a spell of level 5 a day, CELESTE
        # one of level 6. The first rest's cures, in the party's order, and
        # Cure Blindness's of the selected SIR DRYDEN; the second's for
        # those left: FLAVIUS's poison (a spell a day), his curse again
        # (Remove Curse cures 0x2b, not 0x24), the selected's sight again.
        saves = self.saves(ASSETS / "SAVE")

        def edit(name, change):
            data = bytearray((saves / name).read_bytes())
            change(data)
            (saves / name).write_bytes(bytes(data))

        def effects(name, ids):
            old = (saves / name).read_bytes() if (saves / name).exists() else b""
            data = b""
            for n, id_ in enumerate(ids):
                data += bytes([id_, 0, 0, 0, 0]) + (bytes(4) if n + 1 == len(ids) and not old
                                                   else b"\1\0\1\0")
            (saves / name).write_bytes(data + old)

        def dead(r):
            r[0xC7], r[0xD6], r[0xC8] = 6, 0, 0

        def stoned(r):
            r[0xC7], r[0xC8] = 7, 0

        def hurt(r):
            r[0xD6] -= 20

        def spells(w):
            ids = [0x03, 0x03, 0x4B, 0x42, 0x2B, 0x35, 0x43]
            w[:0x4A] = bytes(ids) + bytes(0x4A - len(ids))
            w[0xBC + 4] = 1

        def sixth(w):
            w[0xBC + 5] = 1

        edit("CHRDATA1.GAM", dead)
        edit("CHRDATA2.GAM", stoned)
        effects("CHRDATA3.EFF", [0x37, 0x3D])
        effects("CHRDATA4.EFF", [0x24, 0x37])
        effects("CHRDATA6.EFF", [0x21])
        edit("CHRDATA6.GAM", hurt)
        edit("CHRDATA5.WIZ", spells)
        edit("CHRDATA6.WIZ", sixth)
        keys = self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\rF" + r"S\>\>\r"
        result = self.play("--start-menu", "--saves", saves, "--seed", 1, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        said = "".join(f"print: {who}\nprint: {what}\n" for who, what in [
            ("SIR DRYDEN", "has been raised from the dead"), ("LILLITH", "is changed back to flesh"),
            ("TANAR", "is healed"), ("TANAR", "is detoxified"),
            ("FLAVIUS", "is relieved of a curse"), ("SIR DRYDEN", "regains sight"),
            ("FLAVIUS", "is detoxified"), ("FLAVIUS", "is relieved of a curse"),
            ("SIR DRYDEN", "regains sight")])
        self.assertIn("menu: Save View Magic Rest Alter Fix Exit\n" + said +
                      "menu: Save View Magic Rest Alter Fix Exit\n", out)
        # The records saved: all okay, SIR DRYDEN raised (constitution 16,
        # his most 95 less 21, the knight level outside the classes summed),
        # healed from the pool; CELESTE still blind; FLAVIUS still 0x24.
        for n, (hp, most) in enumerate([(74, 74), (102, 102), (78, 78), (52, 52), (58, 58),
                                        (53, 53)], 1):
            r = (saves / f"CHRDATC{n}.GAM").read_bytes()
            self.assertEqual((r[0xC7], r[0xD6], r[0x28]), (0, hp, most), n)
        self.assertEqual((saves / "CHRDATC1.GAM").read_bytes()[0x19], 16)
        self.assertEqual((saves / "CHRDATC6.EFF").read_bytes()[0], 0x21)
        self.assertEqual((saves / "CHRDATC4.EFF").read_bytes()[0], 0x24)

    def test_rest(self):
        # Camp's Rest (5b7d:0d51) in block 16, as dkk_levels.py's rest flows
        # play it: its menu by index (081d), from the camp menu's selection
        # (Rest's, so Enter is Mins); "Stop resting? " from Yes, which Enter
        # takes; No rests on; a day's rest, interrupted at the 255th tick by
        # the block's camp vector's 0x7ed2 and 0x7ed3 (255, 100), runs the
        # rest vector. Save asks the start menu's "Quit to DOS " (DS:124c).
        start = self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\r"
        rest = "menu: Rest Days Hours Mins Add Subtract Exit\n"
        camp = "menu: Save View Magic Rest Alter Fix Exit\n"
        for keys, then in [
                (r"Rhaar\k\r", rest * 4 + "menu: Stop resting? \nchoice: Y\n" + camp),
                (r"R\ra\rr\knS\>\>\r", rest * 4 + "menu: Stop resting? \nchoice: N\n" + camp +
                 "menu: A B C D E F G H I J\nchoice: C\nmenu: Quit to DOS \n"),
                (r"Rdar\r", rest * 3 + "print: your repose is suddenly interrupted!\n"
                 "print: YOU ARE AWOKEN IN TIME FOR THE CELEBRATION.\n")]:
            with self.subTest(keys=keys):
                saves = self.saves(ASSETS / "SAVE")
                result = self.play("--start-menu", "--saves", saves, "--seed", 1, "--keys",
                                   start + keys, ASSETS)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("print: the party makes camp\n" + camp + then, result.stdout)
        # From Magic (4381:1a34) Rest starts the menu on Magic's selection,
        # Rest's place there, the fifth: Add, which Enter takes (0:05); then
        # Rest. Saved game C's clock (0x4bc7, the minutes) has the 5.
        with self.subTest(keys="MR"):
            saves = self.saves(ASSETS / "SAVE")
            magic = "menu: Cast Memorize Scribe Display Rest Exit\n"
            result = self.play("--start-menu", "--saves", saves, "--seed", 1, "--keys",
                               start + r"MR\rr\eS\>\>\r", ASSETS)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(camp + magic + rest * 2 + magic + camp, result.stdout)
            self.assertEqual((saves / "SAVGAMC.PTY").read_bytes()[1 + 0xC7], 5)

    def test_camp_menus(self):
        # The camp's, Magic's and Alter's menus by index (4381:3106, 1a34,
        # 21df; 6a69:0f1b), as dkk_levels.py's menus flow plays them: Escape
        # leaves Magic; the camp keeps its own selection (Enter is Magic
        # again), Magic's starts from Cast; Alter (no Pics) from Order each
        # time: Right twice, Speed in Death Knights' words, Escape; Enter is
        # Order ("party order "), Select, Escape while moving leaves; Escape
        # leaves Alter and the camp, and block 16 goes on.
        start = self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\r"
        camp = "menu: Save View Magic Rest Alter Fix Exit\n"
        magic = "menu: Cast Memorize Scribe Display Rest Exit\n"
        alter = "menu: Order Drop Speed Icon Level Exit\n"
        saves = self.saves(ASSETS / "SAVE")
        result = self.play("--start-menu", "--saves", saves, "--seed", 1, "--keys",
                           start + r"M\e\r\eA\>\>\r\e\r\r\e\e\e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "print: the party makes camp\n" + camp + magic + camp + magic + camp + alter +
            "print: Game Speed (0=fastest 9=slowest) = 4\nmenu: Faster Slower Exit\n" + alter +
            "menu: Select Exit\nprint: SIR DRYDEN\nprint: has been selected\nmenu: Place Exit\n" +
            alter + camp + "print: THE BEAUTIFUL LADY MAYA", result.stdout)
        self.assertNotIn("Pics", result.stdout)
        # Order's prompt, "party order " (DS:30df): its Select highlighted
        # from column 12 of row 24.
        screen = self.folder / "order.bmp"
        result = self.play("--start-menu", "--saves", self.saves(ASSETS / "SAVE"), "--seed", 1,
                           "--screen", screen, "--keys", start + r"A\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        bmp = screen.read_bytes()
        self.assertEqual(pixel(bmp, 95, 199), (0, 0, 0))
        self.assertNotEqual(pixel(bmp, 96, 199), (0, 0, 0))

    def test_view_and_items(self):
        # Camp (block 16's rest): View (580d:0c66) by index for SIR DRYDEN,
        # Items (580d:0ffa) with Death Knights' readied column (DS:2c02),
        # Ready of the mace with the sword readied, the sword unreadied and
        # traded, Drop of the mace (Lose it?, Yes), Escape back to View,
        # Drop of 5 steel; Escape to camp, as dkk_items.py's camp flow.
        saves = self.saves(ASSETS / "SAVE")
        keys = (self.TITLE + r"\v\v\rDA" + r"\v" * 5 + r"\r\r\r\r" +
                r"VI\v\v\vRH\^\^\^RT\v\r\v\vDY\eD\r5\r\e\e")
        result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
        out = result.stdout
        self.assertIn("menu: Items Trade Drop Exit\nlist: Items\nitem:  Yes Long Sword +1 \n"
                      "item:  Yes Shield \nitem:  Yes Solamnic Plate Mail \nitem:  No  Mace +1 \n"
                      "menu: Ready Use Trade Drop Halve Join Exit\nchoice: R\n"
                      "print: already using Long Sword +1 \n", out)
        self.assertIn("choice: H\nprint: Can't halve that\n", out)
        # The pick (65cd:33c9): Select alone by index, Down redrawing it.
        self.assertIn("choice: T\nmenu: Select\nmenu: Select\nlist: Items\n", out)
        self.assertIn("print: Your Mace +1 will be gone forever\nmenu: Lose it? \nchoice: Y\n",
                      out)
        self.assertIn("item: Steel          500\nchoice: S\nmenu: How much Steel will you drop? "
                      "\ninput: 5\nitem: Steel          495\n", out)
        self.assertTrue(out.endswith("menu: Save View Magic Rest Alter Fix Exit\n"
                                     "(out of keys in block 16 at 8189)\n"), out[-300:])
        # Escape in the pick picks none and ends it at once.
        keys = self.TITLE + r"\v\v\rDA" + r"\v" * 5 + r"\r\r\r\r" + r"VI\^T\e"
        result = self.play("--start-menu", "--saves", self.saves(ASSETS / "SAVE"), "--keys",
                           keys, ASSETS)
        self.assertTrue(result.stdout.endswith(
            "choice: T\nmenu: Select\nlist: Items\nitem:  Yes Long Sword +1 \n"
            "item:  Yes Shield \nitem:  Yes Solamnic Plate Mail \nitem:  No  Mace +1 \n"
            "menu: Ready Use Trade Drop Halve Join Exit\n(out of keys in block 16 at 8189)\n"),
            result.stdout[-400:])
        # Camp's menu keeps its own selection (4381:3106): View left with
        # Drop selected, Enter is View again.
        keys = self.TITLE + r"\v\v\rDA" + r"\v" * 5 + r"\r\r\r\r" + r"V\#4d\#4d\e\r"
        result = self.play("--start-menu", "--saves", self.saves(ASSETS / "SAVE"), "--keys",
                           keys, ASSETS)
        self.assertTrue(result.stdout.endswith(
            "menu: Items Trade Drop Exit\nmenu: Save View Magic Rest Alter Fix Exit\n"
            "print: SIR DRYDEN\nprint: Knight of the Crown\nmenu: Items Trade Drop Exit\n"
            "(out of keys in block 16 at 8189)\n"), result.stdout[-300:])

    def memorized(self, saves, spells):
        """Each character file's spells memorized set (its .WIZ's first 0x4a
        bytes), by file name."""
        for name, ids in spells.items():
            wiz = bytearray((saves / f"{name}.WIZ").read_bytes())
            wiz[:0x4A] = bytes(ids) + bytes(0x4A - len(ids))
            (saves / f"{name}.WIZ").write_bytes(bytes(wiz))

    def test_camp_cast(self):
        # Camp's Magic, Cast (4381:091c) for NEACAIL, who has Cure Light
        # Wounds alone: the spells in memory, the cure (5dc5:1298) "on whom"
        # (5dc5:1182: the game's own pick, "Select" alone, 65cd:33c9),
        # CELESTE, 6 down, healed; the spell marked to memorize again
        # (65cd:17c4), so none left; as dkk_levels.py's cast flow plays it
        # in the original.
        saves = self.saves(ASSETS / "SAVE")
        self.memorized(saves, {"CHRDATA5": [0x03]})
        celeste = bytearray((saves / "CHRDATA6.GAM").read_bytes())
        celeste[0xD6] -= 6
        (saves / "CHRDATA6.GAM").write_bytes(bytes(celeste))
        keys = self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\rM" + r"\v" * 4 + r"C\r\v\r"
        result = self.play("--start-menu", "--saves", saves, "--seed", 1, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        out = result.stdout
        self.assertIn("list: Spells in Memory\nheading: 1st Level\nitem:   Cure Light Wounds\n"
                      "choice: 3\nprint: NEACAIL\nprint: casts\nprint: Cure Light Wounds\n"
                      "menu: Select\nmenu: Select\nprint: CELESTE\nprint: is fully healed\n"
                      "print: NEACAIL\nprint: has no spells memorized\n", out)

    def camp_cast(self, who, spell, keys, effects=None):
        """Camp's Magic, Cast for party member WHO (1 the first) with SPELL
        alone memorized, the cast's KEYS after Cast; EFFECTS (id, value) the
        sixth member's (CELESTE's) permanent effects: the run's stdout from
        the spells in memory on."""
        saves = self.saves(ASSETS / "SAVE")
        self.memorized(saves, {f"CHRDATA{who}": [spell]})
        if effects is not None:
            data = b""
            for n, (id_, value) in enumerate(effects):
                data += bytes([id_, 0, 0, value, 0]) + (b"\1\0\1\0" if n + 1 < len(effects)
                                                        else bytes(4))
            (saves / "CHRDATA6.EFF").write_bytes(data)
        keys = (self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\rM" + r"\v" * (who - 1) + "C" +
                keys)
        result = self.play("--start-menu", "--saves", saves, "--seed", 1, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout[result.stdout.index("list: Spells in Memory"):]

    def test_camp_spells(self):
        # Band B's handlers (5dc5) cast in camp: each in the original's
        # words, on CELESTE (the pick's second from NEACAIL), or the caster.
        # Resist Fire (2763, DS:204c).
        out = self.camp_cast(5, 0x18, r"\r\v\r")
        self.assertIn("print: Resist Fire\nmenu: Select\nmenu: Select\nprint: CELESTE\n"
                      "print: is fire-resistant\n", out)
        # Remove Curse (311b) cures 0x2b ("is cured.", 638c:1479), "is
        # uncursed".
        out = self.camp_cast(5, 0x2B, r"\r\v\r", [(0x2B, 5), (0x5F, 0xFF)])
        self.assertIn("print: CELESTE\nprint: is cured.\nprint: CELESTE\nprint: is uncursed\n",
                      out)
        # Dispel Magic (2e5c): a d100 against each effect not 0xff; Remove
        # Curse's 0x2b of level 5 goes.
        out = self.camp_cast(5, 0x29, r"\r\v\r", [(0x2B, 5), (0x11, 3), (0x5F, 0xFF)])
        self.assertIn("print: Dispel Magic\nmenu: Select\nmenu: Select\nprint: CELESTE\n"
                      "print: is affected\n", out)
        # Cure Critical Wounds (4037): 3d8 + 3.
        out = self.camp_cast(5, 0x47, r"\r\v\r")
        self.assertIn("print: CELESTE\nprint: is fully healed\n", out)
        # CELESTE's: Fire Touch (4b7d) on SIR DRYDEN; Strength (2bc9) on him,
        # silent; Fire Shield (468f) on herself, by index ("Hot Cold"): Hot
        # "is Protected"; Escape asks "Abort Spell", Yes first.
        out = self.camp_cast(6, 0x62, r"\r\v\r")
        self.assertIn("print: SIR DRYDEN\nprint: is covered in flame\n", out)
        out = self.camp_cast(6, 0x23, r"\r\v\r")
        self.assertIn("print: Strength\nmenu: Select\nmenu: Select\nprint: CELESTE\n"
                      "print: has no spells memorized\n", out)
        out = self.camp_cast(6, 0x55, r"\r\r")
        self.assertIn("print: Fire Shield\nmenu: Hot Cold\nprint: CELESTE\nprint: is Protected\n",
                      out)
        out = self.camp_cast(6, 0x55, r"\r\rD")
        self.assertIn("heading: CELESTE\nitem:  Hot Fire Shield\n", out)
        # Cold by its letter, index 1: "is Protected" too, and Display names
        # the cold shield (0x36).
        out = self.camp_cast(6, 0x55, r"\rCD")
        self.assertIn("menu: Hot Cold\nprint: CELESTE\nprint: is Protected\n", out)
        self.assertIn("heading: CELESTE\nitem:  Cold Fire Shield\n", out)
        out = self.camp_cast(6, 0x55, r"\r\e\r")
        self.assertIn("print: Fire Shield\nmenu: Hot Cold\nmenu: Abort Spell\nchoice: Y\n"
                      "print: CELESTE\nprint: has no spells memorized\n", out)

    def test_band_b_in_a_fight(self):
        # The first fight with band B's spells cast by keys (dkk_fight.py's
        # spells flow, which plays it round by round in the original):
        # NEACAIL's Dispel Magic on CELESTE's cell after her Shield, FLAVIUS's
        # Fireball on two nightmares, TANAR's Bless, CELESTE's Haste (and
        # its aging), FLAVIUS's Lightning Bolt; then on Auto, Magic Off.
        saves = self.saves(ASSETS / "SAVE")
        self.memorized(saves, {"CHRDATA3": [0x01, 0x01], "CHRDATA4": [0x2F, 0x33, 0x33],
                               "CHRDATA5": [0x29, 0x01], "CHRDATA6": [0x13, 0x30, 0x30]})
        keys = (self.TO_FIGHT + r"\rC\#50\#50\rC\#50\rGTGC\#50\rGC\#50\rNTT"
                r"GC\#50\#50\rGTGGGC\#50\rT\@q")
        result = self.play("--load", saves / "SAVGAMA.PTY", "--seed", 1, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines()
                 if not line.startswith(("sound: ", "menu: Next"))]
        self.assertEqual([line[6:] for line in lines if line.startswith("cast: ")],
                         ["CELESTE: Shield", "NEACAIL: Dispel Magic", "FLAVIUS: Fireball",
                          "TANAR: Bless", "CELESTE: Haste", "FLAVIUS: Lightning Bolt"])
        at = lines.index("cast: FLAVIUS: Fireball")
        self.assertEqual(lines[at + 4:at + 8], ["print: NIGHTMARE",
                                                "print: takes 9 points of damagefrom fire",
                                                "print: NIGHTMARE",
                                                "print: takes 19 points of damagefrom fire"])
        at = lines.index("cast: TANAR: Bless")
        self.assertEqual(lines[at + 4:at + 6], ["print: TANAR", "print: is Blessed"])
        at = lines.index("cast: CELESTE: Haste")
        self.assertEqual(lines[at + 4:at + 6], ["print: CELESTE", "print: is hasted"])
        at = lines.index("cast: FLAVIUS: Lightning Bolt")
        self.assertEqual(lines[at + 4:at + 6], ["print: NIGHTMARE",
                                                "print: takes 26 points of damagefrom electricity"])
        self.assertIn("menu: continue battle? ", lines)

    def test_fear_and_faerie_fire(self):
        # On Auto with Magic On: FLAVIUS's Fear (5dc5:4566, the cone 175d
        # with its range set): those failing "run away" and flee while they
        # hold 0x6f; CELESTE's Faerie Fire (4391, 1b20): its lone pick is
        # unaffected, as Champions' Charm Monsters' (in its words: "is
        # unaffected", 638c:1f86's DS:1d0f).
        saves = self.saves(ASSETS / "SAVE")
        self.memorized(saves, {"CHRDATA3": [0x4F, 0x0A], "CHRDATA4": [0x54, 0x54],
                               "CHRDATA5": [0x47, 0x2C, 0x0A], "CHRDATA6": [0x4F, 0x4F]})
        result = self.play("--load", saves / "SAVGAMA.PTY", "--seed", 1, "--keys",
                           self.TO_FIGHT + r"\r\@q\[\@m\]", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("sound: ")]
        self.assertEqual([line[6:] for line in lines if line.startswith("cast: ")],
                         ["FLAVIUS: Fear", "CELESTE: Faerie Fire", "FLAVIUS: Fear"])
        at = lines.index("cast: FLAVIUS: Fear")
        self.assertEqual(lines[at + 4:at + 8], ["print: NIGHTMARE", "print: runs away",
                                                "print: SOLAMNIC KNIGHT", "print: runs away"])
        at = lines.index("cast: CELESTE: Faerie Fire")
        self.assertEqual(lines[at + 4:at + 6], ["print: NIGHTMARE", "print: is unaffected"])
        self.assertIn("menu: continue battle? ", lines)

    def test_casters_on_auto(self):
        # The first fight on Auto with Magic On (Alt-M typed ahead, read in
        # the first computer's turn, 3f63:117c) and casters: the computer's
        # choice (3f63:056c with 5340:43ea), its picks (5340:1eeb: 1da5,
        # 44f8), FLAVIUS's Ice Storm with its burst (65cd:39bd) and
        # Lightning Bolt (aborted: none left in its reach), TANAR's cure, as
        # dkk_fight.py's cast flow plays them round by round in the
        # original; no one casts Shocking Grasp with none in its reach.
        saves = self.saves(ASSETS / "SAVE")
        self.memorized(saves, {"CHRDATA3": [0x4A, 0x03], "CHRDATA4": [0x0F, 0x33, 0x57],
                               "CHRDATA5": [0x03, 0x03], "CHRDATA6": [0x14, 0x0C]})
        result = self.play("--load", saves / "SAVGAMA.PTY", "--seed", 1, "--keys",
                           self.TO_FIGHT + r"\r\@q\[\@m\]", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith("cast: ")],
                         ["cast: FLAVIUS: Ice Storm", "cast: FLAVIUS: Lightning Bolt",
                          "cast: TANAR: Cure Light Wounds"])
        at = lines.index("cast: FLAVIUS: Ice Storm")
        self.assertEqual(lines[at + 1:at + 7], ["print: FLAVIUS", "print: casts a spell",
                                                "print: Spell:Ice Storm", "sound: 2",
                                                "sound: 11", "sound: 3"])
        self.assertIn("print: takes 21 points of damagefrom magic", lines[at:at + 12])
        at = lines.index("cast: FLAVIUS: Lightning Bolt")
        self.assertEqual(lines[at + 4], "print: Spell Aborted")
        self.assertEqual(len([line for line in lines if line.startswith("round: ")]), 14)
        self.assertIn("menu: continue battle? ", lines)

    def test_monsters_effects(self):
        # Fights of ECL1-3 from their SETUP MONSTERS on Auto (the effects
        # part): the dragons' breaths (5dc5:51cc death and fire, a blue
        # dragon's lightning 508c, "breathes "), the undead's level drain
        # (504d:14c7) and a vampire's gaze (5dc5:4fd3), whose Charm Person
        # (5dc5:2044) is not ported yet.
        keys = r"\r" * 120
        for file, block, start, at, said in (
                (2, 50, "9564", "5,5,0", {"print: breathes death": 3}),
                (2, 49, "8620", "5,5,0", {"print: breathes fire": 3}),
                (1, 32, "899c", "5,5,0", {"print: breathes ": 7}),
                (3, 80, "8d8b", "5,5,0", {"print: gets drained": 4}),
                (2, 64, "85cf", "5,5,2", {"print: gazes!": 1})):
            result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--file", file, "--start",
                               start, "--at", at, "--seed", 1, "--combat", "auto", "--keys", keys,
                               ASSETS, block)
            lines = result.stdout.splitlines()
            for line, n in said.items():
                self.assertEqual(lines.count(line), n, (block, line))
            if block == 50:
                at_breath = lines.index("print: breathes death")
                self.assertEqual(lines[at_breath + 1:at_breath + 4],
                                 ["sound: 3", "print: LILLITH", "print: takes 120 points of damage"])
        # Death's type is 0x60, not fire's: LILLITH resisting fire (0x14)
        # takes it whole.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--file", 2, "--start", "9564",
                           "--at", "5,5,0", "--seed", 1, "--combat", "auto", "--effect", "2,14,0,ff",
                           "--keys", keys, ASSETS, 50)
        lines = result.stdout.splitlines()
        at_breath = lines.index("print: breathes death")
        self.assertEqual(lines[at_breath + 2:at_breath + 4],
                         ["print: LILLITH", "print: takes 120 points of damage"])

    def test_aging(self):
        # SIR DRYDEN (race 6) at 115 and hasted (effect 0x27, 504d:0d88):
        # the first round's event ages him a year and asks for the aging
        # after the battle (DS:c463), on Auto to its end: past 110 he "is
        # looking very old" (3e05:0264), before the continue question's
        # answer leads to the results.
        saves = self.saves(ASSETS / "SAVE")
        dryden = bytearray((saves / "CHRDATA1.GAM").read_bytes())
        struct.pack_into("<H", dryden, 0x26, 115)
        (saves / "CHRDATA1.GAM").write_bytes(bytes(dryden))
        result = self.play("--load", saves / "SAVGAMA.PTY", "--seed", 1, "--combat", "auto",
                           "--effect", "1,27,0,0", "--keys", self.TO_FIGHT + r"\r" * 8, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("print: ages", lines)
        battle = lines.index("menu: continue battle? ")
        old = lines.index("print: who is looking very old")
        self.assertLess(battle, old)
        self.assertEqual(lines.count("print: ages"), 1)

    def test_memorize_table(self):
        # The memorize table (4381:09c7) clears rows 18-21 and says no
        # name, where Champions' says it on row 18 (4888:0b24): BALTHIR
        # NOMOON's (party B) would show past the 12-letter labels.
        saves = self.saves(ASSETS / "SAVE")
        screen = self.folder / "table.bmp"
        keys = self.TITLE + r"\v\v\r\rB" + r"\v" * 5 + r"\r\r\r\rM\v\v\vM"
        result = self.play("--start-menu", "--saves", saves, "--screen", screen, "--keys", keys,
                           ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("menu: Cast Memorize Scribe Display Rest Exit\nlist: Spells in Grimoire\n",
                      result.stdout)
        self.assertNotIn("print: BALTHIR NOMOON", result.stdout)
        bmp = screen.read_bytes()
        self.assertTrue(all(pixel(bmp, x, y) == (0, 0, 0)
                            for y in range(18 * 8, 19 * 8) for x in range(13 * 8, 14 * 8)))

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

    # Create New Character (49cb:05c7) and Modify (25d4) from the title,
    # seeded, the keys tools/dosbox/checks/dkk_create.py typed to the
    # original: its roster files (far pointers masked, the hands, save
    # bonus and weight compared) are those the
    # original wrote, digest for digest. (name, seed, keys after the title's
    # two, the files, their digest.)
    CREATED = [
        ("CLERIC", 3, r"\r\r\v\v\v\v\v\v\r\r\r\r\rYNCLERIC\rPHNNKES\rK1BNKE2WPEEEYY\v\r\r\r\e"
         r"\v\r\rAAS\vS\v\v\v\vA\vASKE\b\b\vASK\v\v\v\r\r", ["ASKC", "CLERIC"], "JNK PC WIZ",
         "e2c6c41411ec5fd7af69fbe1c8b26a53"),
        ("PALADIN", 5, r"\r\r\v\v\v\r\v\r\v\v\v\r\rNPALADIN\rEYY\v\r\r\r\e\v\r\r\^\^\^SAAE"
         r"\v\v\v\r\rY", ["PALADIN"], "EFF JNK PC WIZ", "ce6e8ed3e773f25dc4fabd9ddc67223b"),
        ("TRIPLE", 7, r"\r\r\v\r\r" + r"\v" * 11 + r"\r\rYNTRIPLE\rEYY", ["TRIPLE"],
         "EFF JNK PC WIZ", "a2eab35c303d581886df2d27a0350ec1"),
        ("WHITE", 11, r"\r\r\r\v\r\v\v\r\rNWHITE\rEYY", ["WHITE"], "EFF JNK PC WIZ",
         "81594272cc8dcd08f3af114678a70a13"),
        ("KENDER", 13, r"\r\r\v\v\v\v\v\r\r\v\v\v\v\v\v\r\v\v\v\v\r\v\v\rNKENDER\rEYY",
         ["KENDER"], "EFF JNK PC WIZ", "adbaa60b4d190be648343f82ef8624ef"),
        ("KNIGHT", 17, r"\r\r\v\v\v\v\v\v\r\r\v\v\v\v\v\r\rNKNIGHT\rEYY", ["KNIGHT"],
         "JNK PC WIZ", "d368b06f712aca14ad2a4ecb0277b47f"),
        ("RANGER", 19, r"\r\r\v\v\r\v\r\v\v\v\v\v\v\v\r\v\r\v\rNRANGER\rEYY", ["RANGER"],
         "EFF JNK PC WIZ", "7d7bf0ce6fecc61f1746bc928ed1b9e7"),
    ]

    @staticmethod
    def masked(name, data):
        """A character's file, its far pointers 0: the record's effects
        (+0x3a), items and slots (+0x7d-+0xb4), next, combat record and
        spells' block (+0xb9, +0xbd, +0xc2); the hands, save bonus and
        weight between (+0xb5-+0xb8) compared."""
        data = bytearray(data)
        ext = name.rsplit(".", 1)[-1]
        if ext == "PC":
            for at, n in ((0x3A, 4), (0x7D, 0xB5 - 0x7D), (0xB9, 4), (0xBD, 4), (0xC2, 4)):
                data[at:at + n] = bytes(n)
        elif ext == "JNK":
            for k in range(0, len(data) - 62, 63):
                data[k + 0x2A:k + 0x2E] = bytes(4)
        elif ext == "EFF":
            for k in range(0, len(data) - 8, 9):
                data[k + 5:k + 9] = bytes(4)
        return bytes(data)

    def test_create(self):
        for name, seed, keys, bases, exts, digest in self.CREATED:
            with self.subTest(name):
                saves = self.saves(ASSETS / "SAVE")
                result = self.play("--start-menu", "--saves", saves, "--seed", seed, "--keys",
                                   r"\kx\kx" + keys, ASSETS)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"roster: {name}.PC", result.stdout)
                files = {f.name: f.read_bytes() for f in saves.iterdir()
                         if f.suffix in (".PC", ".WIZ", ".JNK", ".EFF")
                         and not f.name.startswith("CHRDAT")}
                self.assertEqual(sorted(files), sorted(f"{b}.{e}" for b in bases
                                                       for e in exts.split()))
                h = hashlib.md5()
                for n in sorted(files):
                    h.update(n.encode())
                    h.update(self.masked(n, files[n]))
                self.assertEqual(h.hexdigest(), digest)

    def test_mouse(self):
        # Initialize Mouse/Joystick (49cb:52cd) by clicks: the start menu's
        # fourth row clicked twice (a click picks out a row, a click on the
        # row picked picks it: 6a69:1b82 with DS:c0f9), then its list's
        # first row, the mouse's, already picked: the mouse off (N), into
        # krynn.cfg, not DKK.CFG; Escape; again by keys, the mouse on (M);
        # Exit clicked on row 24 ("Init mouse/joystick: Select Exit").
        keys = (self.TITLE + r"\m80,122,1.\m80,122,1.\m80,98,1.\e\r\r\m234,194,1.")
        saves = self.saves()
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines.count("choice: Initialize Mouse/Joystick"), 2)
        self.assertEqual([line for line in lines if line.startswith("config:")],
                         ["config: krynn.cfg line 3 N", "config: krynn.cfg line 3 M"])
        at = lines.index("list: Init mouse/joystick: ")
        self.assertEqual(lines[at + 1:at + 4], ["item: mouse on", "item: joystick off",
                                                "item: disable both"])
        self.assertEqual(lines[-1], "(out of keys in the start menu)")
        self.assertFalse((saves / "DKK.CFG").exists())
        # No krynn.cfg to read: the first left empty, the second read so.
        self.assertEqual((saves / "krynn.cfg").read_bytes(),
                         b"\r\n\r\nM\r\n\r\n\r\n" + b"0\r\n" * 14)
        # The party list's clicks in Pick Character (580d:3067): saved game
        # A loaded, View clicked twice, the second member clicked (kept by
        # 6a69:0513, selected by 580d:2bb2), then again (Select): its sheet.
        keys = (self.TITLE + r"\m80,114,1.\m80,114,1.DA\m80,122,1.\m80,122,1."
                r"\m140,42,1.\m140,42,1.")
        saves = self.saves(ASSETS / "SAVE")
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("choice: View Character")
        self.assertEqual(lines[at + 1:at + 5], ["menu: Select", "menu: Select", "print: LILLITH",
                                                "print: Paladin"])
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--keys",
                           keys[:-len(r"\m140,42,1.\m140,42,1.")] + r"\r", ASSETS)
        self.assertIn("print: SIR DRYDEN", result.stdout.splitlines())
        # "from where" and Load's letters by index (46b2:1ef0's 6a69:0f1b)
        # take row 24's clicks: Death, then A.
        keys = self.TITLE + r"\m80,114,1.\m80,114,1.\m138,194,1.\m138,194,1."
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--keys", keys, ASSETS)
        lines = result.stdout.splitlines()
        at = lines.index("choice: Load Saved Game")
        self.assertEqual(lines[at + 1:at + 5], ["menu: Death Champions Exit", "choice: D",
                                                "menu: A B", "choice: A"])
        # A click off the list and the menu (row 5, column 2): 0xff, which
        # 6a69:1ea6 compares with an Exit index the start menu never set.
        result = self.play("--start-menu", "--mouse", "--keys", self.TITLE + r"\m18,42,1.",
                           ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("(dkk 6a69:1ea6, [bp-0x242])", result.stderr)
        if (BUILD / "cok").exists():
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            saves = self.saves()
            keys = self.TITLE + r"\m80,122,1.\m80,122,1.\m80,98,1.\e"
            result = subprocess.run([str(BUILD / "cok"), *GAME, "--saves", saves,
                                     "--no-delay", "--keys", keys],
                                    capture_output=True, text=True, env=env, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((saves / "krynn.cfg").exists())

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

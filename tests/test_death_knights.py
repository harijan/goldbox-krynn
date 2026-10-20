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
        # Right moves the selection (to Area) and comes back, a special key
        # that keeps the character selected (580d:2bb2: Down picked
        # LILLITH), and the menu keeps its own selection: Enter is Area.
        result = self.play("--party", ASSETS / "SAVE/SAVGAMA.PTY", "--set", "4be6=1", "--play",
                           "--keys", r"\rM\e\#50\#4d\r\#50V", ASSETS, 48)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("music")]
        menu = "menu: Move Area Cast View Encamp Search Look"
        at = lines.index(menu)
        self.assertEqual(lines[at:at + 7], [menu, "menu: Exit", menu, menu, menu, "area: on", menu])
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
        # Down, Right and Up between its rows), Move's step and Escape's
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
            "turn: NEACAIL (initiative 8)", menu, menu, menu, menu, "menu: Done", "menu: Done",
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
        # is not ported yet (4381:2fd4) and stops.
        saves = self.saves(ASSETS / "SAVE")
        wiz = bytearray((saves / "CHRDATA4.WIZ").read_bytes())
        wiz[wiz.index(0)] = 0x81
        (saves / "CHRDATA4.WIZ").write_bytes(bytes(wiz))
        keys = (self.TITLE + r"\v\v\r\rA" + r"\v" * 5 + r"\r\r\r\rM\v\v\vM\e\eD\e\eF")
        result = self.play("--start-menu", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 1, result.stderr)
        out = result.stdout
        self.assertIn("list: Spells to Memorize\nheading: 1st Level\nitem:  *Bless\n", out)
        self.assertIn("list: Spells in Grimoire\nheading: 1st Level\nitem:   Bless\n", out)
        self.assertIn("heading: LILLITH\nitem:  Protection From Evil, 10' Radius\n", out)
        # Its blank rows are headings too (4381:13fb with 1).
        self.assertIn("heading:  \nheading: SIR DRYDEN\nitem:  <No Spell Effects>\nheading:  \n",
                      out)
        self.assertEqual(result.stderr, "not yet ported: Death Knights of Krynn's Fix "
                         "(dkk 4381:2fd4)\n")

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

    def test_unported_stops(self):
        # What is not ported stops the run, naming the routine: creating a
        # character (49cb:05c7).
        result = self.play("--start-menu", "--keys", self.TITLE + r"\r", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "not yet ported: Death Knights of Krynn's Create New "
                         "Character (dkk 49cb:05c7)\n")
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

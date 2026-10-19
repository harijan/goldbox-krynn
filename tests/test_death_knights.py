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
        # The intro (ECL1 block 2) adds its three NPCs, then sets up a
        # battle, which stops the run (step 3).
        result = self.play("--keys", r"\r" * 20, ASSETS, 2)
        self.assertEqual(result.returncode, 1)
        self.assertEqual([line for line in result.stdout.splitlines()
                          if line.startswith("party:")],
                         ["party: SIR DRYDEN joins, icon 48 in slot 0",
                          "party: TANAR joins, icon 49 in slot 1",
                          "party: FLAVIUS joins, icon 50 in slot 2"])
        self.assertEqual(result.stderr.splitlines()[-1], "not yet ported: Death Knights of "
                         "Krynn's opcode 0x0c, SETUP MONSTERS (dkk 3632:038f)")

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

    def test_not_yet(self):
        # Combat's setup, the treasure's items and a shop's stock stop the
        # run, naming the routine.
        result = self.play("--keys", r"\r" * 20, ASSETS, 32)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr.splitlines()[-1], "not yet ported: Death Knights of "
                         "Krynn's opcode 0x0c, SETUP MONSTERS (dkk 3632:038f)")
        result = self.play("--file", 3, "--start", "9569", ASSETS, 80)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr.splitlines()[-1], "not yet ported: Death Knights of "
                         "Krynn's opcode 0x27, TREASURE ITEMS (dkk 3632:1933)")

    def test_unplayable(self):
        # Not playable yet: the title and start menu, saved games, and cok.
        message = ("Death Knights of Krynn is not playable yet: eclplay --game death-knights "
                   "shows its scripts and 3D view (see DEATH-KNIGHTS.md)\n")
        result = self.play("--start-menu", ASSETS)
        self.assertEqual((result.returncode, result.stderr), (2, message))
        result = self.play("--load", ASSETS / "SAVE/SAVGAMA.PTY", ASSETS)
        self.assertEqual((result.returncode, result.stderr), (2, message))
        if (BUILD / "cok").exists():
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            result = subprocess.run([str(BUILD / "cok"), *GAME], capture_output=True, text=True,
                                    env=env, cwd=ROOT)
            self.assertEqual((result.returncode, result.stderr), (1, message))

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

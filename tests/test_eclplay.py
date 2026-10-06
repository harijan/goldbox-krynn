"""Play the shipped ECL scripts headlessly with eclplay (stdlib only)."""
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "build/eclplay"
ASSETS = ROOT / "Assets"
SAVE = ROOT / "SAVE"


def pixel(bmp, x, y):
    """RGB of pixel (x, y) from a bottom-up 24-bit BMP."""
    offset = struct.unpack_from("<I", bmp, 10)[0]
    width, height = struct.unpack_from("<ii", bmp, 18)
    stride = (width * 3 + 3) & ~3
    at = offset + (height - 1 - y) * stride + x * 3
    return tuple(reversed(bmp[at:at + 3]))


def ink(bmp, x, y):
    """The first colour other than black in cell x, y, or black."""
    for row in range(8):
        for column in range(8):
            colour = pixel(bmp, x * 8 + column, y * 8 + row)
            if colour != (0, 0, 0):
                return colour
    return (0, 0, 0)


WHITE, CYAN, GREEN, YELLOW = (255, 255, 255), (85, 255, 255), (85, 255, 85), (255, 255, 85)


class PlayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def play(self, *args):
        return subprocess.run([str(TOOL), *map(str, args)], capture_output=True, text=True)

    def test_opening_plays_into_the_outpost(self):
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--keys", r"\r\r\r\r\r", "--shots", shots, ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[1], "print: AS YOU TOP A RISE, YOU SPOT A CARAVAN UNDER ")
        self.assertIn("menu: ~PRESS <ENTER>/<RETURN> TO CONTINUE.", lines)
        self.assertIn("menu: ~YES ~NO", lines)
        self.assertIn("[COMBAT]", lines)
        self.assertIn("print: 'THANK YOU FOR YOUR HELP.'", lines)
        self.assertEqual(lines[-1], "(out of keys in block 17 at 9d9b)")
        # One screen per key read, including the one that found no key.
        images = sorted(shots.glob("*.bmp"))
        self.assertEqual(len(images), 6)
        first = images[0].read_bytes()
        self.assertEqual(struct.unpack_from("<ii", first, 18), (320, 200))
        # The frame's corner tile is drawn with colour 13 (light magenta)
        # transparent, so black; the menu row is inverted white.
        self.assertEqual(pixel(first, 0, 0), (0, 0, 0))
        self.assertEqual(pixel(first, 0, 1), (85, 255, 255))
        self.assertNotIn((255, 85, 255), {pixel(first, x, y) for x in range(320) for y in range(8)})
        self.assertEqual(pixel(first, 0, 24 * 8), (255, 255, 255))

    def test_no_to_the_survivors(self):
        result = self.play("--keys", r"\r\r\rn\r", ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        menu = lines.index("menu: ~YES ~NO")
        self.assertEqual(lines[menu + 1], "choice: 1")

    def test_vertical_menu_at_the_gates_of_gargath(self):
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--start", "899b", "--keys", r"\r2", "--shots", shots, ASSETS, 48)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        items = lines.index("list: WHAT DO YOU DO?")
        self.assertEqual(lines[items + 1:items + 4],
                         ["item: SAY YOU'RE TRADESMEN", "item: LEAVE", "item: ATTACK"])
        bmp = sorted(shots.glob("*.bmp"))[-1].read_bytes()
        # The prompt prints on row 17; LEAVE, on row 19, is picked: black on white.
        self.assertEqual(pixel(bmp, 8, 19 * 8 + 7), (255, 255, 255))
        result = self.play("--start", "899b", "--keys", r"\r22\r", ASSETS, 48)
        self.assertIn("choice: 2", result.stdout.splitlines())
        self.assertIn("[LOAD MONSTER 36 4 35]", result.stdout.splitlines())

    def test_view_of_throtl(self):
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--set", "4be6=1", "--keys", "", "--shots", shots, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertFalse([line for line in lines if line.startswith("[LOAD ")])
        self.assertEqual(lines[-2], "menu: ~ATTACK ~LEAVE")
        bmp = sorted(shots.glob("*.bmp"))[0].read_bytes()
        # The party stands at 7, 15 facing north, down a street: grey stone
        # sides near the view's edges and cobbles below a black sky.
        self.assertEqual(pixel(bmp, 8 * 8 + 4, 3 * 8 + 2), (0, 0, 0))
        self.assertEqual(pixel(bmp, 8 * 8 + 4, 13 * 8 + 4), (170, 170, 170))
        self.assertEqual(pixel(bmp, 3 * 8 + 1, 8 * 8), (0, 170, 170))
        colours = {pixel(bmp, x, y) for x in range(24, 112) for y in range(24, 112)}
        self.assertGreaterEqual(len(colours), 5)

    def test_walking_through_throtl(self):
        shots = self.folder / "shots"
        shots.mkdir()
        # Leave the guards, move north into an ambush and the gibbering
        # man, then turn east into a hedge, which stops the next step.
        keys = r"\r\rm\^\r\^\^\r\r\>\^\<"
        result = self.play("--play", "--set", "4be6=1", "--keys", keys, "--shots", shots,
                           ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith("at: ")],
                         ["at: 7,14,0", "at: 7,13,0", "at: 7,12,0", "at: 7,12,2", "at: 7,12,0"])
        self.assertIn("menu: Move Area Cast View Encamp Search Look", lines)
        self.assertIn("print: MONSTERS ATTACK!", lines)
        self.assertEqual(lines[-1], "(out of keys in block 32 at 8335)")
        # Each step and turn redraws the view: from the start, three
        # squares north, east and back north, with the man's portrait.
        def view(shot):
            bmp = shot.read_bytes()
            return tuple(pixel(bmp, x, y) for x in range(24, 112, 4) for y in range(24, 112, 4))
        views = [view(shot) for shot in sorted(shots.glob("*.bmp"))]
        self.assertEqual(len(set(views)), 6)
        self.assertNotEqual(views[-1], views[-2])
        self.assertEqual(views[-1], views[-4])

    def test_missing_wall_set_halts(self):
        # With 0x4be7 clear, LOAD PIECES 1 2 255 loads a wall set for each
        # slot, and WALLDEF1.DAX has no record 2: the original halts.
        result = self.play("--start", "802f", "--set", "4be6=1", "--set", "4be7=0", ASSETS, 32)
        self.assertEqual(result.returncode, 1)
        self.assertIn("WALLDEF1.DAX has no record 2", result.stderr)

    def test_missing_block_and_bad_options(self):
        result = self.play(ASSETS, 200)
        self.assertEqual(result.returncode, 1)
        self.assertIn("no ECL file holds block 200", result.stderr)
        result = self.play("--file", 2, ASSETS, 16)
        self.assertEqual(result.returncode, 1)
        self.assertIn("ECL2.DAX has no record 16", result.stderr)
        self.assertEqual(self.play("--vector", 5, ASSETS, 16).returncode, 2)
        self.assertEqual(self.play("--play", "--vector", 0, ASSETS, 16).returncode, 2)
        self.assertEqual(self.play(ASSETS).returncode, 2)


@unittest.skipUnless((SAVE / "SAVGAMA.DAT").exists(), "needs the saved games in SAVE")
class PartyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def play(self, *args):
        return subprocess.run([str(TOOL), *map(str, args)], capture_output=True, text=True)

    def test_party_beside_the_view(self):
        screen = self.folder / "screen.bmp"
        # Leave the guards and pick the second character with the down arrow.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\r\v", "--screen", screen, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        bmp = screen.read_bytes()
        # Headings, then a row per character: the selected one white, the
        # others light cyan, AC and hit points light green.
        self.assertEqual(ink(bmp, 17, 2), WHITE)
        self.assertEqual(ink(bmp, 17, 4), CYAN)
        self.assertEqual(ink(bmp, 17, 5), WHITE)
        self.assertEqual(ink(bmp, 17, 9), CYAN)
        self.assertEqual(ink(bmp, 17, 10), (0, 0, 0))
        self.assertEqual(ink(bmp, 33, 4), GREEN)  # AC -1
        self.assertEqual(ink(bmp, 37, 5), GREEN)  # 30 hit points
        # The status line: 7,15 N 00:00.
        self.assertEqual(ink(bmp, 17, 15), GREEN)
        self.assertEqual(ink(bmp, 28, 15), GREEN)
        self.assertEqual(ink(bmp, 29, 15), (0, 0, 0))

    def test_arrows_and_a_pit(self):
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--start", "9891", "--set", "4be6=1",
                           "--keys", r"\r", "--shots", shots, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "print: A PIT OPENS UP BENEATH YOUR FEET!")
        hits = [line for line in lines if " is hit FOR " in line]
        self.assertTrue(hits)
        self.assertTrue(all(line.endswith(" points of Damage.") for line in hits))
        # The prompt waits on row 24 with the hurt in yellow.
        bmp = sorted(shots.glob("*.bmp"))[0].read_bytes()
        self.assertEqual(ink(bmp, 0, 24), WHITE)
        self.assertIn(YELLOW, {ink(bmp, 38, y) for y in range(4, 10)})

    def test_who_disarms_the_trap(self):
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--start", "9c8d", "--set", "4be6=1",
                           "--keys", r"\v\v\v\r\r", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        # Down three times from the first character; the random roll
        # against her skill (0x7ca5) succeeds.
        who = lines.index("who: MOLLY")
        self.assertEqual(lines[who + 1:who + 3],
                         ["print: YOU HAVE SUCCESSFULLY DISARMED THE TRAP.",
                          "print: Congratulations MOLLY gains experience!"])

    def test_saved_game_resumes(self):
        result = self.play("--load", SAVE / "SAVGAMA.DAT", "--keys", r"\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "print: AT THE INN OF THE LAST HOME IN SOLACE, A BRAVE ")
        result = self.play("--load", self.folder / "SAVGAMZ.DAT", ASSETS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("SAVGAMZ.DAT: not found", result.stderr)


if __name__ == "__main__":
    unittest.main()

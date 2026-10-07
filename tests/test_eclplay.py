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
        self.assertIn("monster: 4 EVIL FIGHTER, icon 35 in slot 8", result.stdout.splitlines())

    def test_view_of_throtl(self):
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--set", "4be6=1", "--keys", "", "--shots", shots, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertFalse([line for line in lines if line.startswith("[LOAD ")])
        self.assertEqual(lines[-2], "menu: ~ATTACK ~LEAVE")
        # The guards stand in the gateway, a wall ahead (of a kind the party
        # can pass), so at distance 0: their picture replaces the view.
        self.assertEqual(lines[1:3], ["monster: sprite 12 at 0", "monster: picture 12"])
        # After the fight the screen is redrawn with the view.
        final = self.folder / "final.bmp"
        result = self.play("--set", "4be6=1", "--keys", r"\r", "--screen", final, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("[COMBAT]", result.stdout.splitlines())
        bmp = final.read_bytes()
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
        # squares north, east and back north, with the man's portrait, and
        # the hobgoblins' close-up, at the gate and in the ambush.
        def view(shot):
            bmp = shot.read_bytes()
            return tuple(pixel(bmp, x, y) for x in range(24, 112, 4) for y in range(24, 112, 4))
        views = [view(shot) for shot in sorted(shots.glob("*.bmp"))]
        self.assertEqual(len(set(views)), 7)
        self.assertEqual(views[0], views[4])
        self.assertNotEqual(views[-1], views[-2])
        self.assertEqual(views[-1], views[-4])

    def test_camp_in_throtl(self):
        shots = self.folder / "shots"
        shots.mkdir()
        # Attack (combat is not ported), camp, rest an hour, and leave; then
        # camp again, rest an hour, and stop at once with a key.
        keys = r"\rerhar\e" r"erhar\kyy\e"
        result = self.play("--play", "--set", "4be6=1", "--keys", keys, "--shots", shots,
                           ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        camp = lines.index("print: The party makes camp...")
        self.assertEqual(lines[camp - 1], "menu: Move Area Cast View Encamp Search Look")
        self.assertEqual(lines[camp + 1:camp + 4],
                         ["menu: Save View Magic Rest Alter Fix Exit",
                          "menu: Rest Days Hours Mins Add Subtract Exit",
                          "menu: Rest Days Hours Mins Add Subtract Exit"])
        self.assertIn("menu: Stop Resting? ", lines)
        self.assertEqual(lines[lines.index("menu: Stop Resting? ") + 1], "choice: Y")
        self.assertEqual(lines[-1], "(out of keys in block 32 at 80c0)")
        # In camp the status line ends "camping" (columns 30-36) under the
        # party's list, with the camp's picture in the view.
        images = sorted(shots.glob("*.bmp"))
        camping = images[2].read_bytes()
        self.assertEqual(ink(camping, 31, 15), GREEN)
        self.assertEqual(ink(camping, 0, 24), (255, 255, 255))
        after = images[7].read_bytes()  # the commands, after the first camp
        self.assertEqual(ink(after, 31, 15), (0, 0, 0))

    def test_yes_survives_escape(self):
        # Rest three hours and press a key: Yes, chosen with the left
        # arrow, stays chosen through Escape, and Enter stops the rest.
        result = self.play("--play", "--set", "4be6=1", "--keys", r"\rerhaaar\k\<\e\r\e",
                           ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        stop = lines.index("menu: Stop Resting? ")
        self.assertEqual(lines[stop + 1], "choice: Y")

    def test_missing_wall_set_halts(self):
        # With 0x4be7 clear, LOAD PIECES 1 2 255 loads a wall set for each
        # slot, and WALLDEF1.DAX has no record 2: the original halts.
        result = self.play("--start", "802f", "--set", "4be6=1", "--set", "4be7=0", ASSETS, 32)
        self.assertEqual(result.returncode, 1)
        self.assertIn("WALLDEF1.DAX has no record 2", result.stderr)

    def test_encounter_menu_needs_a_party(self):
        # With no party, 3775:1f8b reads 0000:0198: the port stops.
        result = self.play("--start", "851e", "--keys", r"\r", ASSETS, 33)
        self.assertEqual(result.returncode, 1)
        self.assertIn("ENCOUNTER MENU with an empty party", result.stderr)

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

    def test_the_cleric_comes_round_the_corner(self):
        shots = self.folder / "shots"
        shots.mkdir()
        # ENCOUNTER MENU in Throtl's temple: wait twice while he approaches,
        # parlay, and answer slyly.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--set", "4be6=1", "--start", "851e",
                           "--keys", r"wwps\r\r", "--shots", shots, ASSETS, 33)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:6], ["monster: sprite 35 at 2",
                                     "print: YOU SEE A CLERIC ROUND THE CORNER.",
                                     "menu: ~COMBAT ~WAIT ~FLEE ~ADVANCE", "choice: 1",
                                     "monster: sprite 35 at 1", "print: A CLERIC APPROACHES."])
        self.assertIn("menu: ~COMBAT ~WAIT ~FLEE ~PARLAY", lines)
        menu = lines.index("menu: ~HAUGHTY ~SLY ~NICE ~MEEK ~ABUSIVE")
        self.assertEqual(lines[menu + 1:menu + 3],
                         ["choice: 1", "print: THE CLERIC NODS AND POINTS YOU TO A STACK OF PAPERS."])
        # The menu shows no close-up: the sprite's nearest group stands in
        # the (empty) view.
        self.assertNotIn("monster: picture 41", lines)
        def colours(shot):
            bmp = shot.read_bytes()
            return {pixel(bmp, x, y) for x in range(24, 112) for y in range(24, 112)}
        images = sorted(shots.glob("*.bmp"))
        self.assertGreaterEqual(len(colours(images[0])), 3)
        self.assertNotEqual(colours(images[0]), colours(images[2]))

    def test_the_patrol(self):
        # The patrol of Throtl's outpost: a won fight sends another; a fled
        # one, [7ec7] 0x81, ends the script.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--combat", "won", "--start", "8cda",
                           "--keys", r"\r", ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("monster: 9 RED DRAGON, icon 21 in slot 8", lines)
        self.assertIn("combat: won", lines)
        self.assertIn("combat: removed 9 RED DRAGON, 9 BOZAK, 9 SIVAK; 27 dropped", lines)
        self.assertIn("print: THE CITY IS SENDING ANOTHER PATROL. DO YOU FLEE?", lines)
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--combat", "fled", "--start", "8cda",
                           "--keys", r"\r", ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertIn("combat: fled", lines)
        self.assertNotIn("print: THE CITY IS SENDING ANOTHER PATROL. DO YOU FLEE?", lines)
        self.assertEqual(lines[-1], "(done in block 16)")
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--combat", "lost", "--start", "8cda",
                           "--keys", r"\r", ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[-2:], ["print: The monsters rejoice for the party has been destroyed",
                                      "(the party was killed in block 16)"])
        self.assertEqual(self.play("--combat", "draw", ASSETS, 16).returncode, 2)

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

    def test_camp_saves_the_game(self):
        saves = self.folder / "saves"
        saves.mkdir()
        # Camp, rest an hour, save as game A without quitting, and leave.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--saves", saves, "--keys", r"\rerharsan\e", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        save = lines.index("menu: A B C D E F G H I J")
        self.assertEqual(lines[save + 1:save + 4],
                         ["choice: A", "menu: Quit TO DOS ", "choice: N"])
        game = (saves / "SAVGAMA.DAT").read_bytes()
        self.assertEqual(len(game), 5469)
        # The ECL file, an hour on the clock (0x4bc9), the block (0x4bf2),
        # the camp's mode after the adventure's, and the six characters.
        self.assertEqual(game[0], 1)
        self.assertEqual(struct.unpack_from("<H", game, 1 + 2 * 0xc9)[0], 1)
        self.assertEqual(struct.unpack_from("<H", game, 1 + 2 * 0xf2)[0], 32)
        self.assertEqual(game[0x1406:0x1408], bytes([4, 2]))
        self.assertEqual(game[0x1414], 6)
        self.assertEqual(game[0x1415:0x1415 + 9], b"\x08CHRDATA1")
        self.assertEqual((saves / "CHRDATA1.SAV").read_bytes()[1:20],
                         (SAVE / "CHRDATA1.SAV").read_bytes()[1:20])
        # It loads, resumes in Throtl, and camps there again.
        result = self.play("--load", saves / "SAVGAMA.DAT", "--play", "--keys", "e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-2:],
                         ["menu: Save View Magic Rest Alter Fix Exit",
                          "(out of keys in block 32 at 80c0)"])
        # Quitting to DOS ends the run.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--saves", saves, "--keys", r"\resby", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-1], "(quit to DOS in block 32)")
        self.assertTrue((saves / "SAVGAMB.DAT").exists())
        # A directory name too long for the game is refused.
        result = self.play("--saves", "d" * 600, ASSETS, 32)
        self.assertEqual(result.returncode, 1)
        self.assertIn("directory name too long", result.stderr)

    def test_drop_with_the_arrows(self):
        # Alter, Drop: Yes with the left arrow, then Escape and Enter.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\read\<\e\r", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("menu: Drop from party? ", lines)
        self.assertEqual(lines[lines.index("menu: Drop from party? ") + 1], "choice: Y")
        self.assertIn("print: bids you farewell", lines)

    def test_view_and_ready(self):
        screen = self.folder / "screen.bmp"
        # View the first character, unready its sword in Items, and leave.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\rvi\v\vr\e", "--screen", screen, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        view = lines.index("print: Knight of the Crown")
        self.assertEqual(lines[view - 1:view + 3],
                         ["print: SIR STRONGSWORD", "print: Knight of the Crown",
                          "menu: Items Trade Drop Exit", "list: Items"])
        self.assertEqual(lines[view + 3:view + 8],
                         ["item:  Yes  Plate Mail ", "item:  Yes  Shield ",
                          "item:  Yes  Long Sword ", "menu: Ready Use Trade Drop Halve Join",
                          "choice: R"])
        self.assertIn("item:  No   Long Sword ", lines)
        # The sheet again: name light cyan, labels white, values light
        # green, the menu on row 24.
        bmp = screen.read_bytes()
        self.assertEqual(ink(bmp, 1, 1), CYAN)
        self.assertEqual(ink(bmp, 20, 1), WHITE)
        self.assertEqual(ink(bmp, 27, 1), GREEN)
        self.assertEqual(ink(bmp, 1, 21), (0, 0, 0))  # no weapon readied now
        self.assertEqual(ink(bmp, 1, 22), GREEN)      # the plate mail

    def test_cast_from_the_commands(self):
        # Down to Kal, Cast, up to Bless, and cast it on the party.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\r\v\v\v\vc\^\^\^\^\r\e", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        cast = lines.index("print: casts")
        self.assertEqual(lines[cast - 2:cast + 4],
                         ["choice: 1", "print: KAL", "print: casts", "print: Bless",
                          "print: SIR STRONGSWORD", "print: is Blessed"])
        self.assertEqual(lines.count("print: is Blessed"), 6)

    def test_cast_in_camp_on_one(self):
        # Camp, Magic, Cast: Kal's list starts on Sleep, which cannot be
        # cast here; keep it. Then Cure Light Wounds on Molly.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\r\v\v\v\vemc\rn\^\^\^\r\^S\ee\e", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        sleep = lines.index("print: can't be cast here...")
        self.assertEqual(lines[sleep - 1:sleep + 3],
                         ["print: Sleep", "print: can't be cast here...", "menu: Lose it? ",
                          "choice: N"])
        cure = lines.index("print: Cure Light Wounds")
        self.assertEqual(lines[cure + 1:cure + 4],
                         ["menu: Select Exit", "menu: Select Exit", "print: MOLLY"])
        self.assertEqual(lines[cure + 4], "print: is fully healed")

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

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


def block(bmp, x, y):
    """The 8 by 8 pixels of cell x, y."""
    return tuple(pixel(bmp, x * 8 + c, y * 8 + r) for r in range(8) for c in range(8))


def combat_screen(bmp):
    """Whether bmp shows the combat screen: its frame's column 22, beside
    the map, is the side columns' tile (1128:04c1)."""
    return block(bmp, 22, 10) == block(bmp, 0, 10)


# A party for the scripts that fight: eclplay's made-up one, and the saved
# game's when SAVE/ holds it.
PARTIES = [("--test-party", 6)]
if (SAVE / "SAVGAMA.DAT").exists():
    PARTIES.append(("--party", SAVE / "SAVGAMA.DAT"))

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
        lines = result.stdout.splitlines()
        # DESTROY ITEMS 63 runs first (block 16 at 82be) and logs nothing.
        self.assertEqual(lines[0], "print: AS YOU TOP A RISE, YOU SPOT A CARAVAN UNDER ")
        self.assertIn("menu: ~PRESS <ENTER>/<RETURN> TO CONTINUE.", lines)
        # With no party the first record is a monster two squares north, in
        # row 1, and the view's top row is above the map: the original would
        # draw it from the heap before the map (6beb:07a9), and the run stops.
        self.assertEqual(result.returncode, 1)
        self.assertIn("cell 14,-1 is drawn from the heap around the combat map", result.stderr)
        self.assertEqual(lines[-2], "combat: the view from 14,-1")
        # One screen per key read: the caravan's.
        images = sorted(shots.glob("*.bmp"))
        self.assertEqual(len(images), 1)
        first = images[0].read_bytes()
        self.assertEqual(struct.unpack_from("<ii", first, 18), (320, 200))
        # The frame's corner tile is drawn with colour 13 (light magenta)
        # transparent, so black; the menu row is inverted white.
        self.assertEqual(pixel(first, 0, 0), (0, 0, 0))
        self.assertEqual(pixel(first, 0, 1), (85, 255, 255))
        self.assertNotIn((255, 85, 255), {pixel(first, x, y) for x in range(320) for y in range(8)})
        self.assertEqual(pixel(first, 0, 24 * 8), (255, 255, 255))

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

    def test_the_battlefield(self):
        # At Throtl's gate the guards stand in the gateway, at distance 0:
        # the battlefield is built from the 3D map around the party
        # (3cb2:08cd), the party placed in ranks behind 7, 15 facing north
        # and the guards in theirs ahead; then the rounds.
        dump = self.folder / "map.txt"
        result = self.play("--test-party", 6, "--set", "4be6=1", "--combat-map", dump,
                           "--keys", r"\r\rE", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        start = lines.index("print: A battle begins...")
        self.assertEqual(lines[start + 1:start + 5],
                         ["combat: the 3D map around 7,15 facing N, the enemy 0 squares ahead",
                          "combat: 1 ALDA at 27,13", "combat: 2 BRAM at 28,13",
                          "combat: 3 CERA at 26,13"])
        self.assertIn("combat: 7 HOBGOBLIN at 27,12", lines)
        self.assertIn("combat: 21 WARRIOR at 25,10", lines)
        end = [line.startswith("round: 1: ") for line in lines].index(True)
        self.assertEqual(lines[end - 2:end], ["combat: 21 WARRIOR at 25,10",
                                              "combat: the view from 24,10"])
        text = dump.read_text().splitlines()
        self.assertEqual(text[0], "battle in block 32: view 24,10, 21 combatants")
        picture, cells = text[1:26], text[26:51]
        self.assertTrue(all(len(row) == 50 for row in picture))
        self.assertTrue(all(len(row) == 100 for row in cells))
        # The party in two ranks, the guards before them, in the street
        # walled to the north but for the gateway.
        self.assertEqual(picture[13][26:29], "312")
        self.assertEqual(picture[14][27:30], "645")
        self.assertEqual(picture[10][24:27], ".l#")
        self.assertEqual(cells[13][52:54], "17")
        self.assertEqual(text[51], "1 1 ALDA: at 27,13 size 1 side 0 facing 7")
        self.assertEqual(text[71], "l 21 WARRIOR: at 25,10 size 1 side 1 facing 3")
        # The caravan's fight is on open ground.
        result = self.play("--test-party", 6, "--combat-map", dump, "--keys", r"\r\rE",
                           ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertIn("combat: open ground facing N, the enemy 2 squares ahead", lines)
        self.assertIn("combat: 7 BAAZ at 17,2", lines)
        self.assertEqual(dump.read_text().splitlines()[0],
                         "battle in block 16: view 24,10, 10 combatants")

    def test_the_rounds(self):
        # The patrol of Throtl's outpost, fought with every turn passing:
        # fifteen rounds without an attack end it (3995:0b6d), the order of
        # each logged, highest initiative first, and the turns of those with
        # initiative in that order. The script surprises the party (SAVE 1
        # [7ecb]), whose initiatives in the first round are 0.
        result = self.play("--test-party", 6, "--start", "8cda", "--keys", r"\rE\r", ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        rounds = [i for i, line in enumerate(lines) if line.startswith("round: ")]
        self.assertEqual(len(rounds), 15)
        self.assertEqual(lines[rounds[0]],
                         "round: 1: RED DRAGON 6, RED DRAGON 6, RED DRAGON 6, RED DRAGON 4, "
                         "BOZAK 4, RED DRAGON 3, BOZAK 3, RED DRAGON 3, RED DRAGON 3, "
                         "RED DRAGON 1, RED DRAGON 1, FARO 0, ELIN 0, DUNN 0, ALDA 0, CERA 0, "
                         "BRAM 0")
        # The last round pins the dice of the fourteen before it: each
        # round's initiatives, and each turn's rolls, come from one seed.
        self.assertEqual(lines[rounds[14]],
                         "round: 15: DUNN 8, RED DRAGON 6, RED DRAGON 6, BOZAK 5, RED DRAGON 5, "
                         "RED DRAGON 5, BOZAK 5, CERA 5, RED DRAGON 5, RED DRAGON 4, ALDA 4, "
                         "BRAM 4, FARO 3, RED DRAGON 3, RED DRAGON 2, ELIN 2, RED DRAGON 1")
        for n, at in enumerate(rounds, 1):
            head, order = lines[at].split(": ", 2)[1:]
            self.assertEqual(head, str(n))
            entries = [entry.rsplit(" ", 1) for entry in order.split(", ")]
            self.assertEqual(len(entries), 17)
            turns = [line for line in lines[at + 1:rounds[n] if n < 15 else None]
                     if line.startswith("turn: ")]
            self.assertEqual(turns, ["turn: %s (initiative %s)" % (name, value)
                                     for name, value in entries if int(value) > 0])
        # The screen is drawn: nothing of it is logged as unported.
        self.assertFalse([line for line in lines if line.startswith("unported: ")
                          and ("6beb:" in line or "6346:0af6" in line)])
        end = lines.index("combat: removed 9 RED DRAGON, 2 BOZAK; 0 dropped")
        self.assertEqual(lines[end + 1:end + 3],
                         ["print: The party has won.", "print: Each character receives 0"])
        self.assertEqual(lines[-1], "(done in block 16)")
        # --seed starts Random elsewhere: the script's own dice then call up
        # nineteen of the patrol.
        result = self.play("--test-party", 6, "--seed", 12345, "--start", "8cda",
                           "--keys", r"\rE\r", ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith("round: 1: ")],
                         ["round: 1: RED DRAGON 6, RED DRAGON 5, RED DRAGON 5, RED DRAGON 5, "
                          "BOZAK 4, RED DRAGON 4, RED DRAGON 4, RED DRAGON 3, BOZAK 3, BOZAK 2, "
                          "DUNN 2, BOZAK 1, RED DRAGON 1, RED DRAGON 1, ALDA 0, FARO 0, CERA 0, "
                          "BRAM 0, ELIN 0"])
        # The Gods intervene at the player's first turn (432f:41e2, Alt-X);
        # with the enemy gone, "Continue Battle:" asks: Yes fights on, and
        # the Gods intervene again.
        result = self.play("--test-party", 6, "--combat", "gods", "--start", "8cda",
                           "--keys", r"Y\r\rSE\r", ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertEqual(lines.count("print: The Gods intervene!"), 2)
        menus = [i for i, line in enumerate(lines) if line == "menu: Continue Battle:"]
        self.assertEqual([lines[i + 1] for i in menus], ["choice: Y", "choice: N"])
        self.assertTrue(lines[menus[1] + 2].startswith("combat: removed 9 RED DRAGON, 2 BOZAK"))
        self.assertIn("combat: removed 9 RED DRAGON, 2 BOZAK; 11 dropped", lines)
        self.assertEqual(lines[-1], "(done in block 16)")

    def test_the_ambush(self):
        # Throtl's ambush (ECL1 block 32 at 835f): MONSTERS ATTACK!, four
        # hobgoblins and two leaders against the party, fought through the
        # rounds with every turn passing until fifteen without an attack.
        result = self.play("--test-party", 6, "--set", "4be6=1", "--start", "835f",
                           "--keys", r"\r\rE", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "print: MONSTERS ATTACK!")
        self.assertIn("monster: 4 HOBGOBLIN, icon 12 in slot 8", lines)
        self.assertIn("monster: 2 HOBGOBLIN LDR, icon 13 in slot 9", lines)
        rounds = [line for line in lines if line.startswith("round: ")]
        self.assertEqual(len(rounds), 15)
        self.assertEqual(rounds[0],
                         "round: 1: DUNN 7, HOBGOBLIN 6, CERA 6, HOBGOBLIN 6, BRAM 5, "
                         "HOBGOBLIN LDR 5, HOBGOBLIN 3, HOBGOBLIN 2, FARO 2, HOBGOBLIN LDR 1, "
                         "ELIN 1, ALDA 1")
        self.assertEqual(rounds[14],
                         "round: 15: DUNN 7, FARO 6, CERA 6, HOBGOBLIN LDR 5, ALDA 3, "
                         "HOBGOBLIN 2, HOBGOBLIN LDR 2, HOBGOBLIN 2, HOBGOBLIN 1, BRAM 1, "
                         "ELIN 1, HOBGOBLIN 1")
        self.assertEqual(len([line for line in lines if line.startswith("turn: ")]),
                         sum(len([e for e in r.split(": ", 2)[2].split(", ")
                                  if not e.endswith(" 0")]) for r in rounds))
        end = lines.index("combat: removed 4 HOBGOBLIN, 2 HOBGOBLIN LDR; 0 dropped")
        self.assertEqual(lines[end + 1], "print: The party has won.")
        self.assertEqual(lines[-1], "(done in block 32)")

    def test_the_battle_screen(self):
        # Once the guards' battle is set up the screen is the combat screen
        # (6346:300f): the frame's column 22, the map of 7 by 7 cells in the
        # view's box centred on the first character, an empty panel, and "A
        # battle begins..." left on row 24.
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--test-party", 6, "--set", "4be6=1", "--keys", r"\r\rE",
                           "--shots", shots, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        battle = [shot.read_bytes() for shot in sorted(shots.glob("*.bmp"))
                  if combat_screen(shot.read_bytes())]
        self.assertEqual(len(battle), 1)
        bmp = battle[0]
        self.assertEqual(ink(bmp, 0, 24), GREEN)
        self.assertEqual({ink(bmp, x, y) for x in range(23, 39) for y in range(1, 22)},
                         {(0, 0, 0)})
        # ALDA at 27, 13 of the view from 24, 10: cell 3, 3 of the map, the
        # map's cells 24 by 24 pixels from 8, 8; a guard at 27, 12 above.
        alda = {pixel(bmp, x, y) for x in range(80, 104) for y in range(80, 104)}
        floor = {pixel(bmp, x, y) for x in range(8 + 2 * 24, 8 + 3 * 24) for y in range(128, 152)}
        self.assertGreater(len(alda), len(floor))
        self.assertTrue({(0, 0, 0), (85, 85, 85)} <= floor | alda)
        # The red dragons' fear flashes each character in the panel (6346:228c).
        result = self.play("--test-party", 6, "--file", 3, "--start", "99fd", "--combat", "won",
                           "--keys", r"\r\r\r", ASSETS, 97)
        lines = result.stdout.splitlines()
        start = lines.index("sound: 4")
        self.assertEqual(lines[start:start + 3], ["sound: 4", "print: ALDA", "print: is terrified"])
        self.assertEqual(lines.count("sound: 4"), 6)

    # Block 16, the overland map, entered as from the first outpost (block
    # 17, 0x4bf2 0x11), the party there (1, 3) and the caravan met
    # (0x4c2d); --set takes hex.
    OVERLAND = ["--test-party", 4, "--play", "--set", "4bf2=11", "--set", "4bc3=1",
                "--set", "4bc4=3", "--set", "4c2d=1"]

    def cursor_at(self, bmp, x, y):
        """Whether cell x, y holds the overland map's cursor."""
        return (pixel(bmp, x * 8, y * 8) == WHITE and pixel(bmp, x * 8 + 1, y * 8 + 1) == (0, 0, 0)
                and pixel(bmp, x * 8 + 3, y * 8 + 4) == YELLOW)

    def test_overland_travel(self):
        # Decline the outpost, travel north-east and north to Throtl
        # (2, 1), go in, leave its guards and travel south: each step is
        # marked on the map, 0x4bc3 + 1 across and 0x4bc4 + 1 down.
        final = self.folder / "final.bmp"
        result = self.play(*self.OVERLAND, "--keys", "nm98yLn2", "--screen", final, ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith("overland: ")],
                         ["overland: 2,2,1", "overland: 2,1,0", "overland: 2,2,4"])
        self.assertEqual(lines.count("print: THROTL"), 2)
        self.assertIn("print: 'THROTL'S OFF LIMITS TO YOU. LEAVE AND NO ONE GETS HURT.'", lines)
        self.assertEqual(lines[-1], "(out of keys in block 16 at 8824)")
        bmp = final.read_bytes()
        self.assertTrue(self.cursor_at(bmp, 3, 3))
        self.assertFalse(self.cursor_at(bmp, 3, 2) or self.cursor_at(bmp, 2, 4))
        # Swimming: the after-move vector stops the step into the sea.
        result = self.play(*self.OVERLAND[:5], "--set", "4bc3=23", "--set", "4bc4=4",
                           "--set", "4c2d=1", "--keys", r"m3\r2", ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertIn("print: YOU QUICKLY TIRE OF SWIMMING AND RETURN TO SHORE.", lines)
        self.assertEqual([line for line in lines if line.startswith("overland: ")],
                         ["overland: 35,5,4"])

    def test_overland_encounter(self):
        # Ten steps in the wild, then a roll each step (block 16 at 8813):
        # with this seed the eleventh meets hill giants and kapaks; the
        # fight won, the map is shown again with the party marked, and
        # camping there shows it again too.
        final = self.folder / "final.bmp"
        result = self.play(*self.OVERLAND, "--seed", 3, "--combat", "won", "--keys",
                           "nm" + r"\>" * 11 + r"\r\rENm\v" + "eee", "--screen", final,
                           ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        steps = [line for line in lines if line.startswith("overland: ")]
        self.assertEqual(steps[10:], ["overland: 12,3,2", "overland: 12,4,4"])
        found = lines.index("print: YOU HAVE DISCOVERED SOME HOSTILE CREATURES.")
        self.assertLess(lines.index(steps[10]), found)
        self.assertIn("combat: open ground facing E, the enemy 2 squares ahead", lines)
        self.assertIn("print: The party has won.", lines)
        self.assertIn("print: The party makes camp...", lines)
        self.assertNotIn("4877", result.stdout)
        bmp = final.read_bytes()
        self.assertTrue(self.cursor_at(bmp, 13, 5))
        self.assertFalse(self.cursor_at(bmp, 13, 4))

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

    def test_the_caravan(self):
        for party in PARTIES:
            with self.subTest(party=party[0]):
                # With a party, the caravan's fight ends with the results and the
                # treasure menu; then the survivors, and the merchant's thanks.
                result = self.play(*party, "--keys", r"\r\rE\r\r\r\r",
                                   ASSETS, 16)
                self.assertEqual(result.returncode, 0, result.stderr)
                lines = result.stdout.splitlines()
                combat = lines.index("combat: removed 4 BAAZ; 0 dropped")
                self.assertTrue(lines[combat - 1].startswith("turn: "))
                self.assertEqual(lines[combat:combat + 6],
                                 ["combat: removed 4 BAAZ; 0 dropped",
                                  "print: The party has won.",
                                  "print: Each character receives 0", "print: experience points.",
                                  "menu: press <enter>/<return> to continue",
                                  "menu: View Pool Exit"])
                self.assertIn("menu: ~YES ~NO", lines)
                self.assertIn("print: 'THANK YOU FOR YOUR HELP.'", lines)
                self.assertEqual(lines[-1], "(out of keys in block 17 at 9d9b)")
                result = self.play(*party, "--keys", r"\r\rE\r\rn\r", ASSETS, 16)
                lines = result.stdout.splitlines()
                menu = lines.index("menu: ~YES ~NO")
                self.assertEqual(lines[menu + 1], "choice: 1")

    def test_view_after_the_fight(self):
        for party in PARTIES:
            with self.subTest(party=party[0]):
                # After the fight at Throtl's gate the screen is redrawn with the
                # view.
                final = self.folder / ("final%s.bmp" % party[0])
                result = self.play(*party, "--set", "4be6=1", "--keys", r"\r\rE",
                                   "--screen", final, ASSETS, 32)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue([line for line in result.stdout.splitlines()
                                 if line.startswith("round: 15: ")])
                bmp = final.read_bytes()
                # The party stands at 7, 15 facing north, down a street: grey stone
                # sides near the view's edges and cobbles below a black sky.
                self.assertEqual(pixel(bmp, 8 * 8 + 4, 3 * 8 + 2), (0, 0, 0))
                self.assertEqual(pixel(bmp, 8 * 8 + 4, 13 * 8 + 4), (170, 170, 170))
                self.assertEqual(pixel(bmp, 3 * 8 + 1, 8 * 8), (0, 170, 170))
                colours = {pixel(bmp, x, y) for x in range(24, 112) for y in range(24, 112)}
                self.assertGreaterEqual(len(colours), 5)

    def test_walking_through_throtl(self):
        for party in PARTIES:
            with self.subTest(party=party[0]):
                shots = self.folder / ("shots" + party[0])
                shots.mkdir()
                # Fight the guards and leave the treasure, move north to meet
                # the gibbering man, then turn east into a hedge, which stops the
                # next step.
                keys = r"\r\rE\rm\^\^\^\r\r\>\^\<"
                result = self.play(*party, "--play", "--set", "4be6=1",
                                   "--keys", keys, "--shots", shots, ASSETS, 32)
                self.assertEqual(result.returncode, 0, result.stderr)
                lines = result.stdout.splitlines()
                self.assertEqual([line for line in lines if line.startswith("at: ")],
                                 ["at: 7,14,0", "at: 7,13,0", "at: 7,12,0", "at: 7,12,2",
                                  "at: 7,12,0"])
                self.assertIn("menu: Move Area Cast View Encamp Search Look", lines)
                self.assertEqual(lines[-1], "(out of keys in block 32 at 8320)")
                # Each step and turn redraws the view: the guards' close-up at
                # the gate, the results and the treasure's picture, then from the
                # start three squares north, east and back north, with the man's
                # portrait.
                def view(shot):
                    bmp = shot.read_bytes()
                    return tuple(pixel(bmp, x, y) for x in range(24, 112, 4)
                                 for y in range(24, 112, 4))
                images = [shot.read_bytes() for shot in sorted(shots.glob("*.bmp"))]
                # And the battle's screen, once set up.
                self.assertEqual(sum(map(combat_screen, images)), 1)
                views = [view(shot) for shot in sorted(shots.glob("*.bmp"))
                         if not combat_screen(shot.read_bytes())]
                self.assertEqual(len(set(views)), 9)
                self.assertEqual(views[3:6], [views[3]] * 3)
                self.assertNotEqual(views[-1], views[-2])
                self.assertEqual(views[-1], views[-4])

    def test_the_overhead_map(self):
        for party in PARTIES:
            with self.subTest(party=party[0]):
                shots = self.folder / ("shots" + party[0])
                shots.mkdir()
                # Fight the guards and leave the treasure, turn the map on at
                # 7,15, walk two squares north and turn east, then turn it off
                # and on again.
                keys = r"\r\rE\ram\^\^\>eaa"
                result = self.play(*party, "--play", "--set", "4be6=1",
                                   "--keys", keys, "--shots", shots, ASSETS, 32)
                self.assertEqual(result.returncode, 0, result.stderr)
                lines = result.stdout.splitlines()
                self.assertEqual([line for line in lines if line.startswith(("at: ", "area: "))],
                                 ["area: on", "at: 7,14,0", "at: 7,13,0", "at: 7,13,2",
                                  "area: off", "area: on"])
                self.assertNotIn("unported: Area", lines)
                self.assertEqual(lines[-1], "(out of keys in block 32 at 8320)")
                # (Not the battle's screen, saved once it is set up.)
                images = [shot.read_bytes() for shot in sorted(shots.glob("*.bmp"))
                          if not combat_screen(shot.read_bytes())]
                GREY, BLACK = (85, 85, 85), (0, 0, 0)
                def view(bmp):
                    return [pixel(bmp, x, y) for x in range(24, 112) for y in range(24, 112)]
                def status(bmp):
                    return [pixel(bmp, x, y) for x in range(136, 312) for y in range(120, 128)]
                # In the view's place the map, in the frame's greys, with the
                # party's arrow, white, at cell 8, 13 for 7,15, the window
                # from square 2, 5; the status line as it was.
                self.assertEqual(set(view(images[5])),
                                 {GREY, (170, 170, 170), BLACK, WHITE})
                self.assertEqual(pixel(images[5], 68, 105), WHITE)
                self.assertEqual(status(images[5]), status(images[4]))
                # At 7,13 the arrow is at cell 8, 11, then turns east.
                self.assertEqual(pixel(images[8], 68, 89), WHITE)
                self.assertEqual((pixel(images[9], 68, 89), pixel(images[9], 65, 91)),
                                 (BLACK, WHITE))
                # Off, the view; on again, the same map.
                self.assertGreater(len(set(view(images[11]))), 4)
                self.assertEqual(view(images[12]), view(images[10]))

    def test_the_catacombs_hide_the_map(self):
        # The catacombs of Throtl (ECL1 block 34) set 0x4bfb: Area says "Not
        # Here", the status line has no square, and a saved game keeps it.
        saves = self.folder / "saves"
        saves.mkdir()
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--test-party", 2, "--play", "--set", "4be6=1", "--saves", saves,
                           "--shots", shots, "--keys", r"\r\reaesb\e", ASSETS, 34)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("print: Not Here", lines)
        self.assertFalse([line for line in lines if line.startswith("area: ")])
        self.assertEqual(lines[-1], "(out of keys in block 34 at 8082)")
        # "E 00:" from column 17, where a square would put "0,0 E".
        bmp = sorted(shots.glob("*.bmp"))[3].read_bytes()
        self.assertEqual((ink(bmp, 17, 15), ink(bmp, 18, 15)), (GREEN, (0, 0, 0)))
        save = saves / "SAVGAMB.DAT"
        result = self.play("--load", save, "--play", "--keys", r"\ra", ASSETS)
        self.assertEqual(result.stdout.splitlines()[-3:],
                         ["print: Not Here", "menu: Move Area Cast View Encamp Search Look",
                          "(out of keys in block 34 at 8287)"])
        # Started with Helm, the map shows there, until the next step's view.
        result = self.play("--load", save, "--play", "--helm", "--keys", r"\ram\^", ASSETS)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith(("at: ", "area: "))],
                         ["area: on", "at: 1,0,2", "area: off"])

    def test_camp_in_throtl(self):
        for party in PARTIES:
            with self.subTest(party=party[0]):
                shots = self.folder / ("shots" + party[0])
                shots.mkdir()
                # Fight, leave the treasure, camp, rest an hour, and leave; then
                # camp again, rest an hour, and stop at once with a key.
                keys = r"\r\rEerhar\e" r"erhar\kyy\e"
                result = self.play(*party, "--play", "--set", "4be6=1",
                                   "--keys", keys, "--shots", shots, ASSETS, 32)
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
                images = [shot for shot in sorted(shots.glob("*.bmp"))
                          if not combat_screen(shot.read_bytes())]
                camping = images[4].read_bytes()
                self.assertEqual(ink(camping, 31, 15), GREEN)
                self.assertEqual(ink(camping, 0, 24), (255, 255, 255))
                after = images[9].read_bytes()  # the commands, after the first camp
                self.assertEqual(ink(after, 31, 15), (0, 0, 0))

    def test_the_caravans_shop(self):
        # The caravan's shop (prices at 16 here): Buy lists the stock, the
        # price ending at column 30; the party's money pooled, the wand is
        # still too dear; leaving with money asks, Yes goes back, Share
        # empties the pool and Exit leaves.
        result = self.play("--test-party", 4, "--start", "891c",
                           "--keys", r"y\rB\v\v\r\ePB\r\eEySE", ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        shop = lines.index("shop: prices at 16")
        self.assertEqual(lines[shop + 1:shop + 4],
                         ["menu: Buy View Pool Appraise Exit",
                          "item: Wand of Magic Missiles    3500",
                          "item: Potion of Healing          200"])
        self.assertIn("item: 20 Arrows                    1", lines)
        bought = lines.index("choice: 20 Arrows                    1")
        self.assertEqual(lines[bought + 1:bought + 3],
                         ["shop: ALDA pays 1 steel", "print: ALDA buys 20 Arrows "])
        self.assertIn("print: Not enough Money.", lines)
        leave = lines.index('print: As you Leave the Shopkeeper says, "Excuse me but you have '
                            'Left Some Money here."  ')
        self.assertEqual(lines[leave + 1:leave + 5],
                         ["print: Do you want to go back and get your Money?", "menu: ~Yes ~No",
                          "menu: Buy View Take Pool Share Appraise Exit",
                          "menu: Buy View Pool Appraise Exit"])
        self.assertEqual(lines[-1], "(out of keys in block 16 at 8dd1)")

    def test_every_shop(self):
        # Each of the six shops, buying the first item listed with a
        # fighter's 20 steel. The weapon smith of ECL2 block 50 (at 8cea)
        # sets no price factor; with none set before, Buy charges 15.
        cases = [
            ("891c", 16, r"y\rB\r\eE", "shop: prices at 16", "print: Not enough Money."),
            ("8782", 17, r"B\r\eE", "shop: prices at 16", "shop: ALDA pays 2 steel"),
            ("8b84", 50, r"B\r\eE", "shop: prices at 64", "print: Not enough Money."),
            ("8cea", 50, r"B\r\eE", "shop: prices at 0", "shop: ALDA pays 15 steel"),
            ("8efc", 80, r"B\r\eE", "shop: prices at 64", "shop: ALDA pays 8 steel"),
            ("9457", 80, r"B\r\eE", "shop: prices at 64", "print: Not enough Money."),
        ]
        for start, block, keys, prices, outcome in cases:
            with self.subTest(start=start):
                result = self.play("--test-party", 4, "--start", start, "--keys", keys, ASSETS,
                                   block)
                self.assertEqual(result.returncode, 0, result.stderr)
                lines = result.stdout.splitlines()
                self.assertIn(prices, lines)
                self.assertIn(outcome, lines)
                self.assertNotIn("unported: the shop (36d0:07da)", lines)
        result = self.play("--test-party", 4, "--start", "8cea", "--keys", r"B\r\eE",
                           ASSETS, 50)
        self.assertIn("choice: Battle Axe                   2", result.stdout.splitlines())

    def test_sell_and_id(self):
        # In Throtl's armoury: buy a hoopak, then sell it from View's Items
        # for half its value, and ask Id about the chain mail.
        result = self.play("--test-party", 4, "--start", "8782",
                           "--keys", r"B\r\eVI\v\vSyI\ry\e\eE", ASSETS, 17)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("menu: Ready Trade Drop Halve Join Sell Id", lines)
        sold = lines.index("print: I'll give you 1 steel pieces for your Hoopak ")
        self.assertEqual(lines[sold + 1:sold + 5],
                         ["menu: Is It a Deal? ", "choice: Y", "print: Sold!",
                          "shop: sold for 1 steel"])
        self.assertIn("print: For 100 steel pieces I'll identify your Chain Mail ", lines)
        self.assertEqual(lines[-1], "(out of keys in block 17 at 86ec)")

    def test_temples(self):
        # The pilgrims' temple: Pool, then Heal's Cure Light Wounds, paid
        # from the pool; leaving with money asks.
        result = self.play("--test-party", 4, "--start", "8a8f",
                           "--keys", r"\rPH\v\vHy\eEn\r", ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        heal = lines.index("print: ALDA, how can we help you?")
        self.assertEqual(lines[heal + 1], "item: Cure Blindness")
        self.assertEqual(lines[heal + 10], "item: Stone to Flesh")
        paid = lines.index("choice: Cure Light Wounds")
        self.assertEqual(lines[paid + 1:paid + 7],
                         ["print: Cure Light Wounds will only cost 50 steel pieces.",
                          "menu: pay for cure ", "choice: Y", "shop: the pool pays 50 steel",
                          "print: ALDA", "print: is cured."])
        self.assertIn('print: As you leave a priest says, "Excuse me but you have left some '
                      'money here" ', lines)
        self.assertEqual(lines[-1], "(done in block 16)")
        # Throtl's temple: a cure the character does not need asks first.
        result = self.play("--test-party", 4, "--start", "87ab", "--keys", r"HH\r\eE",
                           ASSETS, 17)
        lines = result.stdout.splitlines()
        anyway = lines.index("print: is not blind.")
        self.assertEqual(lines[anyway + 1:anyway + 3],
                         ["menu: cast cure anyway: ", "choice: N"])
        self.assertEqual(lines[-1], "(out of keys in block 17 at 86ec)")

    def test_yes_survives_escape(self):
        for party in PARTIES:
            with self.subTest(party=party[0]):
                # Rest three hours and press a key: Yes, chosen with the left
                # arrow, stays chosen through Escape, and Enter stops the rest.
                result = self.play(*party, "--play", "--set", "4be6=1",
                                   "--keys", r"\r\rEerhaaar\k\<\e\r\e", ASSETS, 32)
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

    def test_treasure(self):
        # ECL1 block 32's TREASURE 0 0 0 0 250 5 3 255 after CLEARMONSTERS:
        # the results, then Take: the coins, jewelry first; take the
        # jewelry, leave, and share the rest.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--start", "8bcd",
                           "--keys", r"\rT\r3\r\eSE", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:12],
                         ["monster: cleared", "treasure: 250 Steel, 5 Gems, 3 Jewelry",
                          "print: The party has found Treasure!",
                          "print: Each character receives 1391", "print: experience points.",
                          "menu: press <enter>/<return> to continue",
                          "menu: View Take Pool Share Exit", "item: Jewelry 3", "item: Gems 5",
                          "item: Steel 250", "choice: J",
                          "menu: How much Jewelry  will you take? "])
        share = lines.index("menu: View Pool Exit")
        self.assertEqual(lines[share - 3:share],
                         ["item: Gems 5", "item: Steel 250", "menu: View Take Pool Share Exit"])
        self.assertEqual(lines[-1], "(done in block 32)")
        # ECL3 block 81's TREASURE 0 0 0 0 100 20 5 130, two random items:
        # Take the mace, Pool, Share, and leave the short sword behind.
        result = self.play("--file", 3, "--party", SAVE / "SAVGAMA.DAT", "--start", "8927",
                           "--keys", r"\rTIT\eEPSEN", ASSETS, 81)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[2:4], ["treasure: item Mace ", "treasure: item Short Sword "])
        take = lines.index("menu: Money Items Exit")
        self.assertEqual(lines[take + 1:take + 5],
                         ["item: Mace ", "item: Short Sword ", "choice: Mace ",
                          "item: Short Sword "])
        self.assertIn("print: There is still treasure left.  ", lines)
        self.assertEqual(lines[-1], "(done in block 81)")

    def test_every_treasure(self):
        # Each of the 26 uses of CLEARMONSTERS; TREASURE ...; COMBAT, from
        # the CLEARMONSTERS: the results, then Pool, Share and Exit.
        uses = [(1, 16, "8a0d"), (1, 17, "8794"), (1, 17, "8af0"), (1, 32, "8bcd"),
                (1, 32, "92e0"), (1, 32, "9312"), (1, 34, "8754"), (1, 34, "89d3"),
                (2, 49, "8d7c"), (2, 49, "9127"), (2, 67, "8982"), (2, 67, "928e"),
                (2, 67, "92a5"), (2, 67, "92ea"), (2, 68, "8936"), (2, 68, "965e"),
                (3, 80, "9996"), (3, 81, "8927"), (3, 81, "89f5"), (3, 81, "8b1a"),
                (3, 81, "91a5"), (3, 81, "962d"), (3, 82, "90d9"), (3, 96, "9b4d"),
                (3, 98, "8507"), (3, 99, "9297")]
        for file, block, start in uses:
            result = self.play("--file", file, "--party", SAVE / "SAVGAMA.DAT", "--start", start,
                               "--keys", r"\rPSEN", ASSETS, block)
            self.assertEqual(result.returncode, 0, (block, start, result.stderr))
            lines = result.stdout.splitlines()
            self.assertEqual(lines[0], "monster: cleared")
            self.assertIn("print: The party has found Treasure!", lines)
            # Pool and Share leave only items, or nothing; but the party
            # cannot carry the 95,000 coins of ECL3 block 98, and what is
            # left stays in the pool.
            menus = [line for line in lines if line.startswith("menu: View ")]
            if block == 98:
                self.assertEqual(menus[2], "menu: View Take Pool Share Exit")
            else:
                self.assertIn(menus[2], ["menu: View Take Pool Exit", "menu: View Pool Exit"])
            self.assertFalse([line for line in lines if line.startswith("error:")])

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
                           "--keys", r"\rSE", ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("monster: 9 RED DRAGON, icon 21 in slot 8", lines)
        self.assertIn("combat: won", lines)
        # On open ground, with eclplay's state (no 3D map loaded, the party
        # at 0, 0), their formations hold 11 of the 27, the dragons two cells
        # wide; setup removes the rest (3cb2:17f7), which bring no
        # experience.
        self.assertEqual(lines.count("combat: BOZAK has no place and is removed"), 7)
        self.assertEqual(lines.count("combat: SIVAK has no place and is removed"), 9)
        self.assertIn("combat: removed 9 RED DRAGON, 2 BOZAK; 11 dropped", lines)
        # The dragons' experience and coins: shared, the pool is empty.
        self.assertIn("print: Each character receives 4937", lines)
        self.assertEqual(lines[lines.index("menu: View Take Pool Share Exit") + 1],
                         "menu: View Pool Exit")
        self.assertIn("print: THE CITY IS SENDING ANOTHER PATROL. DO YOU FLEE?", lines)
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--combat", "fled", "--start", "8cda",
                           "--keys", r"\rE", ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertIn("combat: fled", lines)
        self.assertIn("print: The party has fled.", lines)
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
        # Fight the guards, leave the results and the treasure, and pick the
        # second character with the down arrow.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\r\rE\v", "--screen", screen, ASSETS, 32)
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
                           "--saves", saves, "--keys", r"\r\rEerharsan\e", ASSETS, 32)
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
                           "--saves", saves, "--keys", r"\r\rEesby", ASSETS, 32)
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
                           "--keys", r"\r\rEead\<\e\r", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("menu: Drop from party? ", lines)
        self.assertEqual(lines[lines.index("menu: Drop from party? ") + 1], "choice: Y")
        self.assertIn("print: bids you farewell", lines)

    def test_view_and_ready(self):
        screen = self.folder / "screen.bmp"
        # View the first character, unready its sword in Items, and leave.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\r\rEvi\v\vr\e", "--screen", screen, ASSETS, 32)
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
                           "--keys", r"\r\rE\v\v\v\vc\^\^\^\^\r\e", ASSETS, 32)
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
                           "--keys", r"\r\rE\v\v\v\vemc\rn\^\^\^\r\^S\ee\e", ASSETS, 32)
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

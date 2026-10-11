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
        result = self.play("--combat", "pass",
                           "--test-party", 6, "--start", "8cda", "--keys", r"\rE\r", ASSETS, 16)
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
        result = self.play("--combat", "pass",
                           "--test-party", 6, "--seed", 12345, "--start", "8cda",
                           "--keys", r"\rE\r", ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith("round: 1: ")],
                         ["round: 1: RED DRAGON 6, RED DRAGON 5, RED DRAGON 5, RED DRAGON 5, "
                          "BOZAK 4, RED DRAGON 4, RED DRAGON 4, RED DRAGON 3, BOZAK 3, BOZAK 2, "
                          "DUNN 2, BOZAK 1, RED DRAGON 1, RED DRAGON 1, ALDA 0, FARO 0, CERA 0, "
                          "BRAM 0, ELIN 0"])
        # The Gods intervene at the player's first turn (432f:41e2, Alt-X);
        # with the enemy gone, "Continue Battle:" asks: Yes fights on, and
        # the Gods intervene again. (From the default seed the dragons'
        # fire kills the surprised party first; seed 5 leaves DUNN a turn.)
        result = self.play("--test-party", 6, "--combat", "gods", "--seed", 5, "--start", "8cda",
                           "--keys", r"Y\r\rSE\r", ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertEqual(lines.count("print: The Gods intervene!"), 2)
        self.assertIn("print: breathes Fire", lines)
        menus = [i for i, line in enumerate(lines) if line == "menu: Continue Battle:"]
        self.assertEqual([lines[i + 1] for i in menus], ["choice: Y", "choice: N"])
        self.assertTrue(lines[menus[1] + 2].startswith("combat: removed 9 RED DRAGON, 4 BOZAK"))
        self.assertIn("combat: removed 9 RED DRAGON, 4 BOZAK; 13 dropped", lines)
        self.assertEqual(lines[-1], "(done in block 16)")

    def test_melee(self):
        # The ambush of ECL1 block 32 with --combat melee: every turn attacks
        # the nearest enemy it can reach (432f:1a45). The round lines, the
        # attacks and the seed at the battle's end pin the dice.
        result = self.play("--test-party", 6, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                           "--combat", "melee", "--keys", r"\r\rEN", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        rounds = [line for line in lines if line.startswith("round: ")]
        self.assertEqual(len(rounds), 21)
        self.assertEqual(rounds[0],
                         "round: 1: FARO 6, BRAM 6, HOBGOBLIN 6, HOBGOBLIN LDR 5, HOBGOBLIN 5, "
                         "CERA 5, ALDA 5, HOBGOBLIN 3, DUNN 3, ELIN 2, HOBGOBLIN 2, HOBGOBLIN LDR 1")
        attacks = [line for line in lines if line.startswith("attack: ")]
        self.assertEqual(len(attacks), 27)
        self.assertEqual(attacks[:6], ["attack: HOBGOBLIN and Misses",
                                       "attack: CERA Hitting for 3 points of damage",
                                       "attack: HOBGOBLIN and Misses",
                                       "attack: HOBGOBLIN and Misses",
                                       "attack: BRAM and Misses",
                                       "attack: ALDA and Misses"])
        at = lines.index("turn: BRAM (initiative 6)")
        # A miss: sound 9, then the panel's lines (6346:1883, 1521:04ac).
        self.assertEqual(lines[at + 1:at + 5], ["sound: 9", "print: BRAM", "print: Attacks",
                                                "attack: HOBGOBLIN and Misses"])
        self.assertIn("print: Your Teammate is Dying", lines)
        self.assertIn("attack: and is Dying", lines)
        self.assertIn("combat: seed 1879734825", lines)
        self.assertIn("combat: removed 4 HOBGOBLIN, 2 HOBGOBLIN LDR; 3 dropped", lines)
        self.assertIn("print: Each character receives 16", lines)
        self.assertEqual(lines[-1], "(done in block 32)")
        # The last attack moves the round limit: 15 rounds after it.
        last = max(i for i, line in enumerate(lines) if line.startswith("attack: "))
        self.assertEqual(sum(1 for line in lines[last:] if line.startswith("round: ")), 14)

    def test_auto(self):
        # The ambush of ECL1 block 32 with --combat auto: FARO's first turn
        # presses Alt-Q, putting the whole party on Auto, and has its turn
        # again, the computer's (0x14 then 0x13); from then on the computer
        # plays every turn (3afb:004b). The round lines, the texts and the
        # seed at the battle's end pin the dice.
        result = self.play("--test-party", 6, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                           "--combat", "auto", "--keys", r"\r\rEN", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        rounds = [line for line in lines if line.startswith("round: ")]
        self.assertEqual(len(rounds), 2)
        at = lines.index(rounds[0])
        self.assertEqual(lines[at + 1:at + 4],
                         ["turn: FARO (initiative 6)", "menu: Move View Aim Use Cast Done",
                          "turn: FARO (initiative 19)"])
        # It steps up (sound 10 a step) and attacks.
        texts = [line for line in lines[at + 4:] if not line.startswith("sound: 10")]
        self.assertEqual(texts[:4], ["sound: 9", "print: FARO", "print: Attacks",
                                     "attack: HOBGOBLIN and Misses"])
        self.assertEqual(len([line for line in lines if line.startswith("attack: ")]), 15)
        self.assertEqual(lines.count("print: is bandaged"), 2)
        self.assertEqual(lines.count("print: Surrenders"), 5)
        self.assertIn("print: Guarding", lines)
        self.assertIn("combat: seed 3958868712", lines)
        self.assertIn("combat: removed 4 HOBGOBLIN, 2 HOBGOBLIN LDR; 6 dropped", lines)
        self.assertEqual(lines[-1], "(out of keys in block 32 at 8417)")
        # Another seed: hobgoblins of low morale flee in panic, said at each
        # of their turns, and are struck from behind as they go.
        result = self.play("--test-party", 6, "--set", "4be6=1", "--start", "835f", "--seed", 15,
                           "--combat", "auto", "--keys", r"\r\rEN", ASSETS, 32)
        lines = result.stdout.splitlines()
        self.assertEqual(lines.count("print: flees in panic"), 9)
        self.assertEqual(lines.count("print: Got Away"), 1)
        self.assertEqual(lines.count("print: Surrenders"), 2)
        self.assertIn("attack: HOBGOBLIN (from behind) Hitting for 2 points of damage", lines)
        self.assertIn("combat: seed 1835066672", lines)
        # With no --combat the computer plays the monsters, while the player's
        # turns read the keys: here the first quits (Done, Quit) and the
        # second runs out of them.
        result = self.play("--test-party", 6, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                           "--keys", r"\rDQ", ASSETS, 32)
        lines = result.stdout.splitlines()
        at = lines.index("turn: FARO (initiative 6)")
        self.assertEqual(lines[at:at + 4],
                         ["turn: FARO (initiative 6)", "menu: Move View Aim Use Cast Done",
                          "menu: Guard Delay Quit Auto Speed Exit", "turn: BRAM (initiative 6)"])
        self.assertEqual(lines[-3:], ["menu: Move View Aim Use Cast Done",
                                      "combat: seed 2996491197",
                                      "(out of keys in block 32 at 8417)"])

    def test_casters_on_auto(self):
        # Two clerics of ECL1 block 33 at 86a4 against the test party on
        # Auto (3afb:0613, 432f:28bd): one Begins Casting Hold Person (time
        # 1), moves down the round's order and casts it at its next turn
        # (5b04:1415 from 3afb:004b): the computer's three picks, each
        # throwing, missiles (sound 2) to all but the last picked; the
        # other's spell, begun, is lost when FARO hits it.
        result = self.play("--file", 1, "--test-party", 6, "--start", "86a4", "--set", "4be6=1",
                           "--combat", "auto", "--keys", r"\r\r\r\r\r", ASSETS, 33)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("turn: CLERIC (initiative 6)")
        self.assertEqual(lines[at + 1:at + 3], ["print: CLERIC", "print: Begins Casting"])
        at = lines.index("cast: CLERIC: Hold Person")
        self.assertEqual(lines[at - 1:at + 16],
                         ["turn: CLERIC (initiative 5)", "cast: CLERIC: Hold Person",
                          "print: CLERIC", "print: Casts a Spell", "print: Spell:Hold Person",
                          "sound: 2", "sound: 4", "print: BRAM", "print: is held", "sound: 2",
                          "print: FARO", "print: is Unaffected", "sound: 2", "sound: 4",
                          "print: DUNN", "print: is held", "turn: CLERIC (initiative 2)"])
        at = lines.index("turn: FARO (initiative 2)")
        hit = lines.index("attack: CLERIC Hitting for 2 points of damage", at)
        self.assertEqual(lines[hit + 1:hit + 3], ["print: CLERIC", "print: lost a spell"])
        self.assertEqual(lines.count("cast: CLERIC: Hold Person"), 1)
        self.assertIn("combat: seed 457552249", lines)
        self.assertIn("combat: removed 2 CLERIC; 2 dropped", lines)

    def test_dragon_cone_side_byte(self):
        # ECL3 block 98's red dragons at 8f44, seed 8: a cone whose line
        # meets no one takes its side lines from a stack byte (5b04:1b2a),
        # the game's for a script 2fd3:3c28 runs (0xe0, 0xea after a
        # mirror; see cok_cast_breath). The 256 values would give different
        # targets; the measured one lets the battle go on.
        result = self.play("--file", 3, "--test-party", 6, "--combat", "auto", "--seed", 8,
                           "--start", "8f44", "--keys", "\\r" * 20, ASSETS, 98)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("sound:")]
        breaths = [i for i, line in enumerate(lines) if line == "print: breathes Fire"]
        self.assertEqual([lines[i + 1:i + 3] for i in breaths],
                         [["print: CERA", "print: takes 33 points of damage "],
                          ["print: DUNN", "print: takes 33 points of damage "],
                          ["print: ELIN", "print: takes 16 points of damage "]])
        self.assertEqual(lines[-1], "(the party was killed in block 98)")

    def test_lebaum_dispels(self):
        # ECL3 block 82's Sir Lebaum on Auto, the test party blessed for good
        # with value 3 (--effect): at event 0x0e his effect 0x4a (3f44:26a5)
        # rolls a d20, and on 1 Dispel Magic's handler (5b04:320f) runs on
        # what his fireball (0x4e) left, its ten targets each pass taking
        # the first, DUNN: a d100 of 80 fails the 65, the second, 31, takes
        # his Bless.
        blessed = [arg for k in range(1, 7) for arg in ("--effect", "%d,1,0,3" % k)]
        result = self.play("--file", 3, "--test-party", 6, "--combat", "auto", "--seed", 148,
                           "--start", "9afb", "--set", "4be6=1", *blessed, "--keys", r"\r",
                           ASSETS, 82)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("sound:")]
        at = lines.index("effect: SIR LEBAUM loses 0x4e")
        self.assertEqual(lines[at + 1:at + 5], ["effect: DUNN loses 0x01", "print: DUNN",
                                                "print: is affected", "print: Guarding"])
        self.assertEqual([line for line in lines if line.endswith("loses 0x01")],
                         ["effect: DUNN loses 0x01"])
        self.assertIn("combat: seed 315613810", lines)
        self.assertNotIn("unported", result.stdout)

    def test_timers_in_battle(self):
        # Effects given by --effect run out in the hobgoblins' battle (ECL1
        # block 32), a minute a round (57e4:0171): ALDA's 0x16 (Slow
        # Poison's, 3f44:0725) kills her, poisoned (0x37), in the third;
        # 0x3e (a high constitution's, 1acd) heals the hurt a point and
        # comes back for an hour; CERA's 0x22 (Cause Disease's, 0d19) runs
        # 0x2b's handler (1469), which weakens her, and 0x2c's (1504),
        # which at a hit point brings 0x1f.
        effects = ["1,37,0,ff", "1,16,3,0", "2,3e,2,ff", "3,3e,3,ff", "4,3e,2,ff", "3,22,1,7"]
        result = self.play("--test-party", 6, "--combat", "auto", "--seed", 1, "--play",
                           "--set", "4be6=1", *[arg for e in effects for arg in ("--effect", e)],
                           "--keys", r"\r\r", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("sound:")]
        weak = lines.index("print: is weakened")
        self.assertEqual(lines[weak - 1:weak + 2],
                         ["print: CERA", "print: is weakened", "effect: CERA loses 0x22"])
        self.assertTrue(lines[weak + 2].startswith("round: 2: "))
        healed = [lines[i - 1] for i, line in enumerate(lines)
                  if line == "print: is partially healed"]
        self.assertEqual(healed, ["print: BRAM", "print: DUNN", "print: CERA"])
        dies = lines.index("print: dies from poison")
        self.assertEqual(lines[dies - 1:dies + 3], ["print: ALDA", "print: dies from poison",
                                                    "effect: ALDA loses 0x37",
                                                    "effect: ALDA loses 0x16"])
        self.assertIn("combat: seed 1859194198", lines)
        self.assertIn("effect: CERA loses 0x1f", lines)
        self.assertNotIn("unported", result.stdout)
        # Minutes are hex words: 0x102 and 0xb outlast the battle.
        effects = ["2,3e,102,ff", "4,3e,0b,ff"]
        result = self.play("--test-party", 6, "--combat", "auto", "--seed", 1, "--play",
                           "--set", "4be6=1", *[arg for e in effects for arg in ("--effect", e)],
                           "--keys", r"\r\r", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[-1], "(the party was killed in block 32)")
        self.assertFalse([line for line in lines if line.endswith("loses 0x3e")])
        # A member past the party, or a value past a byte, is refused.
        result = self.play("--test-party", 2, "--effect", "3,1,0,3", ASSETS, 32)
        self.assertEqual(result.returncode, 1)
        self.assertIn("--effect: no party member 3", result.stderr)
        result = self.play("--test-party", 2, "--effect", "1,1,0,100", ASSETS, 32)
        self.assertEqual(result.returncode, 2)

    def test_dragons_breathe(self):
        # The red dragons of ECL1 block 16 at 8cda against the test party on
        # Auto: on event 0x0e (3afb:0d49) a dragon breathes fire (effect
        # 0x68, 5b04:546e) in a cone of width 3 and length 9 at a target the
        # computer's spell 0x3d picks: each in it throws (type 3) and takes
        # the dragon's most hit points, 33, half if saved; the damage type
        # 0x21 names no element. The party dies.
        result = self.play("--test-party", 6, "--combat", "auto", "--seed", 5, "--start", "8cda",
                           "--keys", r"\r\r\r\r", ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("sound:")]
        breaths = [i for i, line in enumerate(lines) if line == "print: breathes Fire"]
        self.assertEqual([lines[i - 1] for i in breaths], ["print: RED DRAGON"] * 3)
        self.assertEqual([lines[i + 1:i + 3] for i in breaths],
                         [["print: ELIN", "print: takes 33 points of damage "],
                          ["print: ALDA", "print: takes 16 points of damage "],
                          ["print: FARO", "print: takes 33 points of damage "]])
        self.assertIn("combat: seed 2266347106", lines)
        self.assertEqual(lines[-1], "(the party was killed in block 16)")
        # Played by the keys, DUNN guarding: the dragons breathe three times.
        keys = r"\r" + "DG" * 12 + r"\r\r\r"
        result = self.play("--test-party", 6, "--seed", 5, "--start", "8cda", "--keys", keys,
                           ASSETS, 16)
        lines = result.stdout.splitlines()
        self.assertEqual(lines.count("print: breathes Fire"), 3)
        self.assertIn("combat: seed 393207360", lines)
        self.assertEqual(lines[-1], "(the party was killed in block 16)")

    def test_kapak_dissolves(self):
        # The guards of Gargath Keep (ECL2 block 49 at 8440): a kapak that
        # drops dissolves into acid (effect 0x46 on event 0x0d, 3f44:2037),
        # a pool on its cell (terrain 0x1d) for 1d6 minutes (0x50).
        result = self.play("--file", 2, "--test-party", 6, "--start", "8440", "--seed", 6,
                           "--combat", "auto", "--keys", r"\r\r\r\r\r", ASSETS, 49)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("sound:")]
        at = lines.index("print: dissolves into acid!")
        self.assertEqual(lines[at - 4:at + 1], ["print: KAPAK", "print: goes down",
                                                "attack: and is Dying", "print: KAPAK",
                                                "print: dissolves into acid!"])
        self.assertIn("combat: seed 593253837", lines)
        self.assertIn("combat: removed 2 KAPAK, 3 BAAZ, 4 ELVISH KILLER; 9 dropped", lines)

    def test_stinking_cloud(self):
        # The ambush of ECL1 block 32 at 835f: HOLT, the eighth of the test
        # party, casts Stinking Cloud at the second target the aim offers
        # (5b04:29de): "Creates a noxious cloud", then each in its four
        # cells throws (60f4:0dc3): FARO saves and coughs (0x1e), HOLT,
        # beside him, fails and chokes (0x1f), losing its turns. Then
        # Alt-Q: FARO, stepping in the cloud again, coughs anew.
        result = self.play("--test-party", 8, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                           "--keys", r"\rCCNT\@qN\rEN\r\r", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [line for line in result.stdout.splitlines() if not line.startswith("sound:")]
        at = lines.index("cast: HOLT: Stinking Cloud")
        self.assertEqual(lines[at + 6:at + 14],
                         ["print: HOLT", "print: Creates a noxious cloud", "print: FARO",
                          "print: starts to cough", "print: FARO", "print: is coughing",
                          "print: HOLT", "print: chokes and gags from nausea"])
        at = lines.index("turn: FARO (initiative 6)")
        self.assertEqual(lines[at + 1:at + 4],
                         ["effect: FARO loses 0x1e", "print: FARO", "print: starts to cough"])
        self.assertIn("combat: seed 1404601903", lines)
        self.assertEqual(lines[-1], "(done in block 32)")

    def test_player_turns(self):
        # The skeletons of ECL1 block 32 at 87b4, the player's turns played
        # by the keys (3995:0573): FARO and BRAM, clerics, Turn the undead
        # (432f:12b7); CERA Views herself, then Done, Speed, Slower, Exit,
        # Quit; ALDA Aims at BRAM, an ally (Attack Ally: No), Moves two
        # north and Guards. The curate Begins Casting Hold Person and casts
        # it at its turn again (5b04:1415, the computer's three picks, each
        # thrown against, 5b04:1cbd's missiles to all but the last): FARO
        # and ELIN are held and lose their turns. DUNN Delays, and at its
        # turn again presses Done, Auto; CERA Alt-Q in round 2, and the
        # computer finishes the battle.
        keys = r"TTVEDSSEQANTNM88DDGDDDA\@qN\rEN\r\r"
        result = self.play("--test-party", 6, "--set", "4be6=1", "--start", "87b4", "--seed", 1,
                           "--keys", keys, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        turns = [line for line in lines if line.startswith("turn: ")]
        self.assertEqual(turns[:8], ["turn: FARO (initiative 6)", "turn: BRAM (initiative 6)",
                                     "turn: CERA (initiative 5)", "turn: ALDA (initiative 5)",
                                     "turn: EVIL CURATE (initiative 4)",
                                     "turn: EVIL CURATE (initiative 3)",
                                     "turn: DUNN (initiative 3)", "turn: DUNN (initiative 1)"])
        at = lines.index("turn: FARO (initiative 6)")
        self.assertEqual(lines[at + 1:at + 4], ["menu: Move View Aim Use Cast Turn Done",
                                                "print: FARO", "print: turns undead..."])
        self.assertEqual(lines.count("print: is turned"), 10)
        at = lines.index("turn: CERA (initiative 5)")
        self.assertEqual(lines[at + 1:at + 10],
                         ["menu: Move View Aim Use Cast Done", "print: CERA", "print: White Mage",
                          "menu: Items Spells Drop Exit", "menu: Move View Aim Use Cast Done",
                          "menu: Guard Delay Quit Auto Speed Exit",
                          "menu:  Slower Faster Exit", "menu:  Slower Faster Exit",
                          "menu: Guard Delay Quit Auto Speed Exit"])
        at = lines.index("turn: ALDA (initiative 5)")
        self.assertEqual(lines[at + 1:at + 6],
                         ["menu: Move View Aim Use Done", "menu: Next Prev Manual Center Exit",
                          "menu: Next Prev Manual Target Center Exit", "menu: Attack Ally: ",
                          "choice: N"])
        self.assertIn("print: Guarding", lines[at:lines.index("turn: EVIL CURATE (initiative 4)")])
        at = lines.index("turn: EVIL CURATE (initiative 4)")
        self.assertEqual(lines[at + 1:at + 19],
                         ["print: EVIL CURATE", "print: Begins Casting",
                          "turn: EVIL CURATE (initiative 3)", "cast: EVIL CURATE: Hold Person",
                          "print: EVIL CURATE", "print: Casts a Spell",
                          "print: Spell:Hold Person", "sound: 2", "print: DUNN",
                          "print: is Unaffected", "sound: 2", "sound: 4", "print: FARO",
                          "print: is held", "sound: 2", "sound: 4", "print: ELIN",
                          "print: is held"])
        self.assertIn("round: 2: DUNN 7, CERA 5, ALDA 5, ELIN 3, EVIL CURATE 2, FARO 2, BRAM 1, "
                      "SKELETON 0, ZOMBIE 0, SKELETON 0, ZOMBIE 0, SKELETON 0, SKELETON 0, "
                      "SKELETON 0, SKELETON 0, SKELETON 0, SKELETON 0", lines)
        self.assertNotIn("turn: ELIN (initiative 3)", lines)
        at = lines.index("turn: CERA (initiative 5)", lines.index("turn: DUNN (initiative 7)"))
        self.assertEqual(lines[at + 1:at + 3],
                         ["menu: Move View Aim Use Cast Done", "turn: CERA (initiative 19)"])
        self.assertIn("print: Surrenders", lines)
        self.assertIn("combat: seed 1876183322", lines)
        self.assertIn("combat: removed 8 SKELETON, 2 ZOMBIE, 1 EVIL CURATE; 11 dropped", lines)
        self.assertEqual(lines[-1], "(done in block 32)")
        # Without --keys the player's turns pass, and the passive party is
        # killed.
        result = self.play("--test-party", 6, "--set", "4be6=1", "--start", "87b4", "--seed", 1,
                           ASSETS, 32)
        lines = result.stdout.splitlines()
        self.assertNotIn("menu: Move View Aim Use Cast Turn Done", lines)
        self.assertEqual(lines[-1], "(the party was killed in block 32)")

    def test_wand_and_weighed_picks(self):
        # The ambush of ECL1 block 32 with seven of the test party: ALDA
        # Moves north into a hobgoblin and attacks (3995:1117); GWEN Uses
        # her Wand of Magic Missiles (546c:24d7, 5b04:1415 as an item: "uses
        # an item", no "Casts a Spell"), aimed at a hobgoblin; then Begins
        # Casting Charm Monsters and casts it at her next turn, two picks
        # weighed (432f:2337's kind 5), the last picked saved (1cbd).
        keys = r"\rDQDQDQM8DQDQU\#50UPTDQCCPTPPT\eDQDQDQ"
        result = self.play("--test-party", 7, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                           "--keys", keys, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("turn: ALDA (initiative 5)")
        self.assertEqual(lines[at + 1:at + 7],
                         ["menu: Move View Aim Use Done", "menu:  Done", "sound: 7",
                          "print: ALDA", "print: Attacks",
                          "attack: HOBGOBLIN Hitting for 7 points of damage"])
        at = lines.index("turn: GWEN (initiative 2)")
        self.assertEqual(lines[at + 1:at + 20],
                         ["menu: Move View Aim Use Cast Done", "list: Items",
                          "item:  Yes  Quarter Staff ", "item:  Yes  Wand ",
                          "menu: Ready Use Drop Halve Join", "choice: U", "print: GWEN",
                          "print: uses an item", "print: Item:", "print: Wand ",
                          "cast: GWEN: Magic Missile",
                          "menu: Next Prev Manual Target Center Exit",
                          "menu: Next Prev Manual Target Center Exit", "sound: 2", "sound: 3",
                          "print: HOBGOBLIN", "print: takes 12 points of damage from Magic",
                          "print: HOBGOBLIN", "print: Goes Down, and is Dying"])
        at = lines.index("cast: GWEN: Charm Monsters")
        self.assertEqual(lines[at - 1], "turn: GWEN (initiative 5)")
        self.assertEqual(lines[at + 1:at + 19],
                         ["print: GWEN", "print: Casts a Spell", "print: Spell:Charm Monsters"] +
                         ["menu: Next Prev Manual Target Center Exit"] * 6 +
                         ["sound: 2", "print: DUNN", "print: is Unaffected", "sound: 2",
                          "sound: 4", "print: HOBGOBLIN LDR", "print: is charmed",
                          "turn: HOBGOBLIN (initiative 5)", "print: HOBGOBLIN"])
        self.assertIn("combat: seed 3176944188", lines)

    def test_player_ambush(self):
        # The ambush of ECL1 block 32, played: FARO Moves west twice and
        # Escape takes the moves back, then Guards; BRAM Aims at the
        # hobgoblin north of him and strikes (Target); ALDA Bandages CERA,
        # who is dying; DUNN moves the cursor onto BRAM's body (Manual, no
        # target there), leaves and Quits; ELIN presses Alt-Q.
        keys = r"\rM44\eDGANTDBAM8\e\eDQ\@qN\rEN"
        result = self.play("--test-party", 6, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                           "--keys", keys, ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("turn: FARO (initiative 6)")
        self.assertEqual(lines[at:at + 13],
                         ["turn: FARO (initiative 6)", "menu: Move View Aim Use Cast Done",
                          "menu:  Done", "sound: 10", "menu:  Done", "sound: 10", "menu:  Done",
                          "menu: Move View Aim Use Cast Done",
                          "menu: Guard Delay Quit Auto Speed Exit", "print: Guarding",
                          "turn: BRAM (initiative 6)", "menu: Move View Aim Use Cast Done",
                          "menu: Next Prev Manual Center Exit"])
        self.assertEqual(lines[at + 13:at + 18],
                         ["menu: Next Prev Manual Target Center Exit", "sound: 9", "print: BRAM",
                          "print: Attacks", "attack: HOBGOBLIN and Misses"])
        at = lines.index("turn: ALDA (initiative 5)")
        self.assertEqual(lines[at + 1:at + 5],
                         ["menu: Move View Aim Use Done",
                          "menu: Guard Delay Quit Bandage Auto Speed Exit", "print: CERA",
                          "print: is bandaged"])
        at = lines.index("turn: DUNN (initiative 3)")
        self.assertEqual(lines[at + 1:at + 8],
                         ["menu: Move View Aim Use Done", "menu: Next Prev Manual Center Exit",
                          "menu: Exit", "menu: Exit", "menu: Next Prev Manual Center Exit",
                          "menu: Move View Aim Use Done",
                          "menu: Guard Delay Quit Bandage Auto Speed Exit"])
        self.assertEqual(lines[at + 8:at + 11], ["turn: ELIN (initiative 2)",
                                                 "menu: Move View Aim Use Done",
                                                 "turn: ELIN (initiative 19)"])
        self.assertEqual(len([line for line in lines if line.startswith("attack: ")]), 21)
        self.assertIn("combat: seed 2970518320", lines)
        self.assertIn("combat: removed 4 HOBGOBLIN, 2 HOBGOBLIN LDR; 6 dropped", lines)
        self.assertEqual(lines[-1], "(done in block 32)")
        # Alt-Q by its scan code: the same battle.
        again = self.play("--test-party", 6, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                          "--keys", keys.replace(r"\@q", r"\#10"), ASSETS, 32)
        self.assertEqual(again.stdout, result.stdout)

    def test_the_ambush(self):
        # Throtl's ambush (ECL1 block 32 at 835f): MONSTERS ATTACK!, four
        # hobgoblins and two leaders against the party, fought through the
        # rounds with every turn passing until fifteen without an attack.
        result = self.play("--combat", "pass",
                           "--test-party", 6, "--set", "4be6=1", "--start", "835f",
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
        result = self.play("--test-party", 6, "--set", "4be6=1", "--combat", "pass", "--keys",
                           r"\r\rE", "--shots", shots, ASSETS, 32)
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
                result = self.play("--combat", "pass", *party, "--keys", r"\r\rE\r\r\r\r",
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
                result = self.play("--combat", "pass",
                                   *party, "--keys", r"\r\rE\r\rn\r", ASSETS, 16)
                lines = result.stdout.splitlines()
                menu = lines.index("menu: ~YES ~NO")
                self.assertEqual(lines[menu + 1], "choice: 1")

    def test_view_after_the_fight(self):
        for party in PARTIES:
            with self.subTest(party=party[0]):
                # After the fight at Throtl's gate the screen is redrawn with the
                # view.
                final = self.folder / ("final%s.bmp" % party[0])
                result = self.play("--combat", "pass",
                                   *party, "--set", "4be6=1", "--keys", r"\r\rE",
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
                result = self.play("--combat", "pass", *party, "--play", "--set", "4be6=1",
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
                result = self.play("--combat", "pass", *party, "--play", "--set", "4be6=1",
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
                result = self.play("--combat", "pass", *party, "--play", "--set", "4be6=1",
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
                result = self.play("--combat", "pass", *party, "--play", "--set", "4be6=1",
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

    def test_npcs_join_and_leave(self):
        # The kender of ECL1 block 32 joins (ADD NPC 15 100) in the lowest
        # icon slot free, after the party.
        result = self.play("--test-party", 4, "--set", "4be6=1", "--start", "8c4e", "--keys",
                           "Y", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("party: KILDIRF joins, icon 15 in slot 4", lines)
        self.assertEqual(lines[-1], "(done in block 32)")
        # Skyla joins in ECL2 block 50 (ADD NPC 44 100); CPIC2 has no
        # record 44, so she has no combat icon, as in the original.
        result = self.play("--test-party", 4, "--set", "4be6=1", "--start", "8985", "--keys",
                           r"\r\r\r", ASSETS, 50)
        lines = result.stdout.splitlines()
        joined = lines.index("party: SKYLA joins, icon 44 in slot 4")
        self.assertEqual(lines[joined + 3], "print: SKYLA JOINS YOUR PARTY.")
        # When she leaves (87c0), 9add selects her by name with LOAD
        # CHARACTER and DUMP removes the character selected; without her the
        # last LOAD CHARACTER inside the party, the fourth, stays selected,
        # and DUMP takes DUNN.
        result = self.play("--test-party", 4, "--set", "4be6=1", "--start", "87c0", "--keys",
                           r"\r", ASSETS, 50)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[3], "party: DUNN leaves")

    def test_the_spell_finds_the_trap(self):
        # ECL1 block 32's trap (9b82): SPELL 22 finds the first character
        # with Find Traps memorized, here ALDA's second spell byte (0x7c21,
        # set with ALDA selected); CAST SPELL forgets it. Without it, the
        # trap is not found and the script returns at once.
        result = self.play("--test-party", 4, "--set", "4be6=1", "--set", "7c21=16", "--start",
                           "9b82", "--trace", "--keys", r"C\r", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("print: YOU HAVE FOUND A TRAP!", lines)
        self.assertIn("  9c1c SAVE TABLE", lines)
        result = self.play("--test-party", 4, "--set", "4be6=1", "--start", "9b82", "--keys",
                           r"C\r", ASSETS, 32)
        self.assertEqual(result.stdout.splitlines(), ["(done in block 32)"])

    def test_find_item_the_tomb_sword(self):
        # ECL2 block 67 gives the Long Sword +5 (type 63) at 92a6; FIND ITEM
        # 63 then marks it taken (92bc) only if a character took it.
        taken = self.play("--test-party", 4, "--set", "4be6=1", "--trace", "--start", "92a5",
                          "--keys", r"\rT\r\e\e", ASSETS, 67)
        left = self.play("--test-party", 4, "--set", "4be6=1", "--trace", "--start", "92a5",
                         "--keys", r"\rEN", ASSETS, 67)
        for result in taken, left:
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines()[-1], "(done in block 67)")
        self.assertIn("  92bc ADD", taken.stdout.splitlines())
        self.assertNotIn("  92bc ADD", left.stdout.splitlines())

    def test_calls_step_and_sound(self):
        # ECL2 block 48 at 8ce1: CALL [c01e] steps forward, CALL [b203]
        # plays sound 10 twice (0x3de is 5).
        result = self.play("--test-party", 4, "--set", "4be6=1", "--at", "5,5,0", "--start",
                           "8ce1", ASSETS, 48)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:3], ["at: 5,4,0", "sound: 10", "sound: 10"])

    def test_the_demo(self):
        # ECL2 block 57, a demonstration: three NPCs join, the view walks
        # with CALL [b203]'s sounds, a portrait (0x7ee1 69 over body 68), a
        # battle, a picture's frames (CALL [6803]) and PROGRAM 3, which does
        # nothing, then EXIT.
        result = self.play("--test-party", 4, "--set", "4be6=1", "--combat", "won", "--keys",
                           r"\r\eN", ASSETS, 57)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[2:5], ["party: SIR RINGWALD joins, icon 60 in slot 4",
                                      "party: VARIANNA joins, icon 62 in slot 5",
                                      "party: LYN SWIFTFOOT joins, icon 61 in slot 6"])
        self.assertEqual(lines.count("sound: 10"), 24)
        self.assertFalse([line for line in lines if line.startswith(("[", "unported: "))])
        self.assertEqual(lines[-1], "(done in block 57)")

    def test_the_magic_shops_portrait(self):
        # ECL2 block 50's magic shop shows its shopkeeper's portrait, HEAD2
        # record 69 over BODY2 record 68, and the shop's redraw after Buy
        # shows it again beside the party list.
        shots = self.folder / "shots"
        result = self.play("--test-party", 4, "--set", "4be6=1", "--start", "8b47", "--shots",
                           shots, "--keys", r"\>\rB\eE", ASSETS, 50)
        self.assertEqual(result.returncode, 0, result.stderr)
        images = [path.read_bytes() for path in sorted(shots.glob("*.bmp"))]
        self.assertEqual(len(images), 5)

        def view(bmp):
            return tuple(ink(bmp, x, y) for x in range(3, 14) for y in range(3, 14))
        self.assertEqual(view(images[0]), view(images[4]))
        self.assertNotEqual(ink(images[0], 8, 5), (0, 0, 0))
        self.assertNotEqual(ink(images[4], 17, 4), (0, 0, 0))  # ALDA beside it


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
        result = self.play("--combat", "pass",
                           "--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
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
        result = self.play("--combat", "pass",
                           "--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
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
        result = self.play("--combat", "pass",
                           "--load", saves / "SAVGAMA.DAT", "--play", "--keys", "e", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-2:],
                         ["menu: Save View Magic Rest Alter Fix Exit",
                          "(out of keys in block 32 at 80c0)"])
        # Quitting to DOS ends the run.
        result = self.play("--combat", "pass",
                           "--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--saves", saves, "--keys", r"\r\rEesby", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-1], "(quit to DOS in block 32)")
        self.assertTrue((saves / "SAVGAMB.DAT").exists())
        # A directory name too long for the game is refused.
        result = self.play("--combat", "pass", "--saves", "d" * 600, ASSETS, 32)
        self.assertEqual(result.returncode, 1)
        self.assertIn("directory name too long", result.stderr)

    def test_drop_with_the_arrows(self):
        # Alter, Drop: Yes with the left arrow, then Escape and Enter.
        result = self.play("--combat", "pass",
                           "--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\r\rEead\<\e\r", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("menu: Drop from party? ", lines)
        self.assertEqual(lines[lines.index("menu: Drop from party? ") + 1], "choice: Y")
        self.assertIn("print: bids you farewell", lines)

    def test_view_and_ready(self):
        screen = self.folder / "screen.bmp"
        # View the first character, unready its sword in Items, and leave.
        result = self.play("--combat", "pass",
                           "--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
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
        result = self.play("--combat", "pass",
                           "--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
                           "--keys", r"\r\rE\v\v\v\vc\^\^\^\^\r\e", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        cast = lines.index("print: casts")
        self.assertEqual(lines[cast - 2:cast + 4],
                         ["choice: 1", "print: KAL", "print: casts", "print: Bless",
                          "print: SIR STRONGSWORD", "print: is Blessed"])
        self.assertEqual(lines.count("print: is Blessed"), 6)

    def test_casting_in_battle(self):
        # Throtl's ambush with the saved party, casting from the combat menu
        # (432f:28bd): KAL Begins Casting Bless; GARIN casts Sleep at once,
        # aimed at a cell (432f:2337's area within 1, either side): 4d4 hit
        # dice put his allies to sleep, SIRRION throwing against it; KAL
        # casts Bless at its next turn, aimed at its own cell (within 2),
        # every ally blessed.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--start", "835f", "--seed", 1,
                           "--keys", r"\rC\#50\#50CNTCCNTDQCCNTDQDQ", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("turn: KAL (initiative 8)")
        self.assertEqual(lines[at + 1:at + 3], ["menu: Move View Aim Use Cast Done",
                                                "list: Spells in Memory"])
        self.assertIn("print: Begins Casting", lines[at:at + 20])
        at = lines.index("cast: GARIN : Sleep")
        self.assertEqual(lines[at:at + 17],
                         ["cast: GARIN : Sleep", "print: GARIN ", "print: Casts a Spell",
                          "print: Spell:Sleep", "menu: Next Prev Manual Target Center Exit",
                          "menu: Next Prev Manual Target Center Exit", "sound: 2", "sound: 4",
                          "print: SIR STRONGSWORD", "print: falls asleep", "sound: 4",
                          "print: ISTAN HORBIN", "print: falls asleep", "print: SIRRION",
                          "print: is Unaffected", "turn: HOBGOBLIN (initiative 6)", "sound: 10"])
        at = lines.index("cast: KAL: Bless")
        self.assertEqual(lines[at - 1], "turn: KAL (initiative 5)")
        blessed = [lines[i - 1] for i in range(at, at + 40) if lines[i] == "print: is Blessed"]
        self.assertEqual(blessed, ["print: MOLLY", "print: ISTAN HORBIN", "print: KAL",
                                   "print: SIRRION", "print: SIR STRONGSWORD", "print: GARIN "])
        # Nothing picked: Exit, "Abort Spell? " No aims again, Exit, Yes:
        # "Spell Aborted", the spell forgotten.
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--start", "835f", "--seed", 1,
                           "--keys", r"\rC\#50\#50CCC\eN\eYDQ", ASSETS, 32)
        lines = result.stdout.splitlines()
        at = lines.index("cast: GARIN : Sleep")
        self.assertEqual(lines[at + 4:at + 11],
                         ["menu: Next Prev Manual Target Center Exit", "menu: Abort Spell? ",
                          "choice: N", "menu: Next Prev Manual Target Center Exit",
                          "menu: Abort Spell? ", "choice: Y", "print: Spell Aborted"])
        self.assertNotIn("print: falls asleep", lines)
        # KAL casts Detect Magic on himself (432f:2337's kind 0), GARIN Magic
        # Missile at the nearest (a single pick), SIRRION Begins Casting
        # Silence 15' radius and casts it at his next turn (3995:0573) at
        # the nearest (kind 15).
        result = self.play("--party", SAVE / "SAVGAMA.DAT", "--start", "835f", "--seed", 1,
                           "--keys", r"\rC\#48\#48CC\#48CNTDQC\#50CDQDQNTDQ", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        at = lines.index("cast: KAL: Detect Magic")
        self.assertEqual(lines[at + 1:at + 9],
                         ["print: KAL", "print: Casts a Spell", "print: Spell:Detect Magic",
                          "sound: 2", "sound: 4", "print: KAL", "print: is affected",
                          "turn: GARIN  (initiative 7)"])
        at = lines.index("cast: GARIN : Magic Missile")
        self.assertEqual(lines[at + 1:at + 10],
                         ["print: GARIN ", "print: Casts a Spell", "print: Spell:Magic Missile",
                          "menu: Next Prev Manual Target Center Exit",
                          "menu: Next Prev Manual Target Center Exit", "sound: 2", "sound: 3",
                          "print: SIR STRONGSWORD",
                          "print: takes 4 points of damage from Magic"])
        at = lines.index("turn: SIRRION (initiative 5)")
        self.assertIn("choice: 102", lines[at:at + 12])
        self.assertEqual(lines[at + 12:at + 14], ["print: SIRRION", "print: Begins Casting"])
        at = lines.index("cast: SIRRION: Silence 15' radius")
        self.assertEqual(lines[at - 1:at + 10],
                         ["turn: SIRRION (initiative 4)", "cast: SIRRION: Silence 15' radius",
                          "print: SIRRION", "print: Casts a Spell",
                          "print: Spell:Silence 15' radius",
                          "menu: Next Prev Manual Target Center Exit",
                          "menu: Next Prev Manual Target Center Exit", "sound: 2", "sound: 4",
                          "print: SIR STRONGSWORD", "print: is silenced"])

    def test_cast_in_camp_on_one(self):
        # Camp, Magic, Cast: Kal's list starts on Sleep, which cannot be
        # cast here; keep it. Then Cure Light Wounds on Molly.
        result = self.play("--combat", "pass",
                           "--party", SAVE / "SAVGAMA.DAT", "--play", "--set", "4be6=1",
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


# From the title (a key skips it) and its menu (Play) to the start menu.
TITLE = r"\kxP"
# Create a human male fighter, lawful good, named BOB, keeping his first
# rolls and icon, and save him to the roster.
CREATE_BOB = r"\r" + r"\v" * 6 + r"\r\r\v\r\rNBOB\r\eYY"


class StartMenuTests(unittest.TestCase):
    """The game from its title: the start menu, creating characters, the
    roster, training and saved games (eclplay --start-menu)."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def play(self, keys, *args):
        return subprocess.run([str(TOOL), "--start-menu", "--saves", str(self.folder), *map(str, args),
                               "--keys", keys, str(ASSETS)], capture_output=True, text=True)

    def test_the_title_times_out_to_the_demonstration(self):
        # A key while the title waits ends it; the menu's time running out
        # (\t) plays ECL2 block 57; afterwards the title shows again, in
        # full without a key, and its menu.
        result = self.play(r"\kx\t", "--combat", "won")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[2:4], ["menu: Play Demo", "choice: Demo"])
        self.assertEqual(lines[4], "print: EVEN WITH THE DEFEAT OF THE DRAGONARMIES,")
        self.assertIn("print: Electronic Arts", lines)
        self.assertEqual(lines.count("menu: Play Demo"), 2)
        self.assertEqual(lines[-1], "(out of keys at the title)")
        # Fought round by round, the demonstration's battle reads no key and
        # asks no "Continue Battle:"; the credits and the title follow.
        result = self.play(r"\kx\t")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(len([line for line in lines if line.startswith("party: ")
                              and line.endswith(("slot 0", "slot 1", "slot 2"))]), 3)
        self.assertTrue([line for line in lines if line.startswith("round: ")])
        self.assertNotIn("menu: Continue Battle:", lines)
        self.assertEqual(lines[-3:], ["print: Electronic Arts", "menu: Play Demo",
                                      "(out of keys at the title)"])
        # Without a key or a timeout the title's menu waits.
        result = self.play(r"\kx")
        self.assertEqual(result.stdout.splitlines()[-1], "(out of keys at the title)")
        self.assertNotIn("print: Electronic Arts", result.stdout)

    def test_a_space_first_leaves_add(self):
        # Add's loop tests a count not yet set when the first key is a
        # space; what the start menu's FreeMem leaves there ends it.
        result = self.play(TITLE + CREATE_BOB + r"\vS ")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        add = lines.index("list: Add a character: ")
        self.assertEqual(lines[add + 1:add + 3], ["item: BOB            ", "list: Choose a FUNCTION "])
        self.assertNotIn("party: BOB joins", lines)
        self.assertEqual(lines[-1], "(out of keys in the start menu)")

    def test_a_new_character_joins_saves_trains_and_loads(self):
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play(TITLE + CREATE_BOB, "--shots", shots)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[-1], "(out of keys in the start menu)")
        self.assertIn("roster: BOB.WHO", lines)
        self.assertEqual(lines[lines.index("list: Pick Race") + 7], "item:   Human")
        # A fighter of 2001 experience trains to level 2 as he is made.
        bob = bytearray((self.folder / "BOB.WHO").read_bytes())
        self.assertEqual(len(bob), 409)
        self.assertEqual((bob[0x5a], bob[0x5b], bob[0xfb], bob[0x10a]), (6, 2, 2, 0))
        self.assertEqual(int.from_bytes(bob[0x116:0x11a], "little"), 2001)
        # Enough for level 3 (4001).
        bob[0x116:0x11a] = (4001).to_bytes(4, "little")
        (self.folder / "BOB.WHO").write_bytes(bob)
        # Add him, save game A, begin; at the outpost's HALL (PROGRAM 0) the
        # start menu offers training; then back to the outpost.
        keys = (TITLE + r"\vS\rE" + r"\v" * 5 + "SA" + r"\vS" + r"\r" * 7 + "H" + r"\v" * 3 +
                "SSY" + r"\v" * 6 + "S")
        result = self.play(keys)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("party: BOB joins", lines)
        self.assertEqual(lines.count("choice: Begin Adventuring"), 2)
        self.assertIn("item: Train Character", lines)
        self.assertIn("print:     a level 3 Fighter", lines)
        self.assertIn("print: Congratulations...", lines)
        self.assertEqual(lines[-2:], ["menu: ~INN ~HALL ~COMMANDANT ~ARMOURY ~NEXT ~LEAVE",
                                      "(out of keys in block 17 at 86ec)"])
        # Saving erased his roster copy; the game holds him.
        self.assertFalse((self.folder / "BOB.WHO").exists())
        self.assertTrue((self.folder / "SAVGAMA.DAT").exists())
        self.assertEqual((self.folder / "CHRDATA1.SAV").read_bytes()[:4], b"\x03BOB")
        # Load it from the start menu and begin again.
        result = self.play(TITLE + r"\v\vSA" + r"\v" * 5 + "S")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("menu: A", lines)
        self.assertEqual(lines[-1], "(out of keys in block 36 at 817f)")
        self.assertIn("print: AT THE INN OF THE LAST HOME IN SOLACE, A BRAVE ", lines)
        # The screens: the title's menu, the start menu and the lists.
        self.assertGreater(len(list(shots.glob("*.bmp"))), 20)

    def test_quitting_from_the_start_menu(self):
        result = self.play(TITLE + r"\^SY")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-2:], ["quit: to DOS", "(quit to DOS)"])


if __name__ == "__main__":
    unittest.main()

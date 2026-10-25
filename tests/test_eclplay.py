"""Play the shipped ECL scripts headlessly with eclplay (stdlib only)."""
import hashlib
import os
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "build/eclplay"
ASSETS = ROOT / "COKAssets"
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
        self.assertIn("menu: ~PRESS <ENTER>/<RETURN> TO CONTINUE", lines)
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

    def test_game(self):
        # --game names the descriptor (src/rules.c): champions is the
        # default, the same run; an unknown name is an error that lists the
        # games.
        default = self.play("--test-party", 4, "--keys", r"\r\r\r\r\r", ASSETS, 16)
        named = self.play("--game", "champions", "--test-party", 4, "--keys", r"\r\r\r\r\r",
                          ASSETS, 16)
        self.assertEqual(default.returncode, 0, default.stderr)
        self.assertEqual(named.stdout, default.stdout)
        self.assertEqual(named.stdout.splitlines()[-1], "(out of keys in block 16 at 8613)")
        bad = self.play("--game", "dragonlance", ASSETS, 16)
        self.assertEqual(bad.returncode, 2)
        self.assertEqual(bad.stderr, "--game: unknown game dragonlance (games: champions, death-knights)\n")
        self.assertEqual(bad.stdout, "")

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
        # The side is +0x18a (HOSTILE), not who controls it: ALDA given to
        # the computer (0x7d0c 0x80) is still on side 0.
        result = self.play("--test-party", 6, "--set", "4be6=1", "--set", "7d0c=80",
                           "--combat-map", dump, "--keys", r"\r\rE", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(dump.read_text().splitlines()[51],
                         "1 1 ALDA: at 27,13 size 1 side 0 facing 7")
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
        # Resumed, only the load vector runs before the first command (no
        # entrance text from the location vector).
        result = self.play("--load", save, "--play", "--keys", "a", ASSETS)
        self.assertEqual(result.stdout.splitlines(),
                         ["menu: Move Area Cast View Encamp Search Look", "print: Not Here",
                          "menu: Move Area Cast View Encamp Search Look",
                          "(out of keys in block 34 at 801c)"])
        # Started with Helm, the map shows there, until the next step's view.
        result = self.play("--load", save, "--play", "--helm", "--keys", r"am\^", ASSETS)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith(("at: ", "area: "))],
                         ["area: on", "at: 1,0,2", "area: off"])

    def test_the_test_party(self):
        # --test-party's eight characters as a saved game writes them
        # (CHRDATB1-8.SAV), byte for byte as eclplay made them before the
        # layout's accessors (main a2f2ee3): their scores, base and current,
        # heads by gender, bodies by class, hit points, attacks, spells known
        # and memorized.
        saves = self.folder / "saves"
        saves.mkdir()
        result = self.play("--test-party", 8, "--play", "--set", "4be6=1", "--saves", saves,
                           "--keys", r"\r\reaesb\e", ASSETS, 34)
        self.assertEqual(result.returncode, 0, result.stderr)
        records = [(saves / f"CHRDATB{n}.SAV").read_bytes() for n in range(1, 9)]
        self.assertEqual([r[1:1 + r[0]] for r in records],
                         [b"ALDA", b"BRAM", b"CERA", b"DUNN", b"ELIN", b"FARO", b"GWEN", b"HOLT"])
        self.assertEqual(list(records[2][0x10:0x1c]),
                         [15, 15, 17, 17, 13, 13, 14, 14, 15, 15, 12, 12])
        self.assertEqual([r[0x135] for r in records], [5, 9, 5, 9, 5, 9, 5, 9])
        self.assertEqual([(r[0x1e], r[0x1f]) for r in records[6:]], [(0x0b, 0x51), (0x22, 0)])
        self.assertEqual([hashlib.sha256(r).hexdigest()[:16] for r in records],
                         ["0a0838436110c129", "6e12d7ff2cf35b15", "bbca068830ed0de2",
                          "d0a7f9eb6aa2295a", "4f10910897e2cf85", "b63f4444ec5908d0",
                          "10d39830c898f89d", "83958af3d57c0625"])

    def test_continue_has_no_full_stop(self):
        # 2fd3:116d replaces "PRESS BUTTON OR RETURN TO CONTINUE." with the
        # 34 characters of 2fd3:1146, which end without a full stop: row 24
        # is blank from column 34 (Solace's first page, ECL1 block 36).
        screen = self.folder / "solace.bmp"
        result = self.play("--test-party", 1, "--screen", screen, ASSETS, 36)
        self.assertIn("menu: ~PRESS <ENTER>/<RETURN> TO CONTINUE", result.stdout.splitlines())
        bmp = screen.read_bytes()
        self.assertEqual(ink(bmp, 33, 24), WHITE)
        self.assertEqual(block(bmp, 34, 24), block(bmp, 35, 24))
        self.assertEqual(ink(bmp, 34, 24), (0, 0, 0))

    def test_throtl_second_wall_set(self):
        # Throtl's walls are WALLDEF1 record 1, two sets loaded from slot 1:
        # 69ea:1025 adds slot 1's offset (none) to both, so the far doors two
        # squares ahead of 7,15 facing north are the second set's own tiles.
        # These are the original's pixels there (DOSBox-X, batch C's #27).
        screen = self.folder / "street.bmp"
        result = self.play("--test-party", 2, "--play", "--set", "4be6=1", "--set", "4c4d=2",
                           "--screen", screen, "--keys", "", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        bmp = screen.read_bytes()
        colours = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170),
                   (170, 85, 0), (170, 170, 170), (85, 85, 85), (85, 85, 255), (85, 255, 85),
                   (85, 255, 255), (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]
        rows = ["".join("%x" % colours.index(pixel(bmp, x, y)) for x in range(56, 80))
                for y in range(56, 80)]
        self.assertEqual(rows, [
            "800000000000000000000008", "300000000000000000000003", "680000000000000000000067",
            "638800000000000000008636", "767630000000000000083873", "336308000000000000068377",
            "763786000000000003637637", "737676880000000067367637", "777377676038386777737767",
            "736736737367678373673673", "773776737737767377377673", "737883777373737773788377",
            "376060373778873737606037", "376060773760607737606077", "736060737360607373606073",
            "776060737760607377606073", "736060337877877873606037", "366060387887888873606063",
            "736060778778877887806036", "366088788887888887806077", "676077877778787778778033",
            "738777787787778777877063", "378887888888878887888837", "378778777877788778777787"])

    def test_resumed_camp_keeps_last_mode(self):
        # Saved in Throtl's camp: .DAT 0x1406 (DS:4b4a) 4, 0x1407 (4b49) 2.
        # The loader sets 4b4a to the saved mode, 2; resuming (2fd3:3c28)
        # leaves it, only 3b47 after a NEWECL copying 4b49 there. So the
        # game camped and saved again holds 2 at 0x1406.
        saves = self.folder / "saves"
        saves.mkdir()
        self.play("--test-party", 2, "--play", "--set", "4be6=1", "--set", "4c4d=2",
                  "--saves", saves, "--keys", "eSA", ASSETS, 32)
        first = (saves / "SAVGAMA.DAT").read_bytes()
        self.assertEqual((first[0x1406], first[0x1407]), (4, 2))
        # Resumed, its first screen is the street's at 7,15 facing north, as
        # entering the block draws it: the loaded game's redraw and view
        # (3c28's 3d36, DS:4b52) before the first command.
        street, resumed = self.folder / "street.bmp", self.folder / "resumed.bmp"
        self.play("--test-party", 2, "--play", "--set", "4be6=1", "--set", "4c4d=2",
                  "--screen", street, "--keys", "", ASSETS, 32)
        result = self.play("--load", saves / "SAVGAMA.DAT", "--play", "--screen", resumed,
                           "--keys", "", ASSETS)
        self.assertEqual(result.stdout.splitlines(),
                         ["menu: Move Area Cast View Encamp Search Look",
                          "(out of keys in block 32 at 801c)"])
        self.assertEqual(resumed.read_bytes(), street.read_bytes())
        result = self.play("--load", saves / "SAVGAMA.DAT", "--play", "--saves", saves,
                           "--keys", "eSB", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        again = (saves / "SAVGAMB.DAT").read_bytes()
        self.assertEqual((again[0x1406], again[0x1407]), (2, 2))

    def test_resumed_overland_asks_nothing(self):
        # Saved in camp on Throtl's square of the overland map: resumed, only
        # block 16's load vector runs before the first command, so the map's
        # menu comes, not "YOU ARE NEAR THROTL. DO YOU ENTER?" again.
        saves = self.folder / "saves"
        saves.mkdir()
        result = self.play("--test-party", 4, "--play", "--set", "4bf2=11", "--set", "4bc3=1",
                           "--set", "4bc4=3", "--set", "4c2d=1", "--saves", saves,
                           "--keys", "nm98neeSA", ASSETS, 16)
        self.assertIn("overland: 2,1,0", result.stdout.splitlines())
        result = self.play("--load", saves / "SAVGAMA.DAT", "--play", "--keys", "", ASSETS)
        self.assertEqual(result.stdout.splitlines(),
                         ["menu: Move Encamp", "(out of keys in block 16 at 807f)"])
        # With the block named, eclplay enters it as 3b47 does: the location
        # vector runs at once and asks.
        result = self.play("--load", saves / "SAVGAMA.DAT", "--play", "--keys", "", ASSETS, 16)
        self.assertIn("print: . DO YOU ENTER?", result.stdout.splitlines())
        # A step away and back asks again, after the step back.
        result = self.play("--load", saves / "SAVGAMA.DAT", "--play", "--keys", "m28", ASSETS)
        self.assertEqual(result.stdout.splitlines()[:6],
                         ["menu: Move Encamp", "menu: Exit", "overland: 2,2,4", "menu: Exit",
                          "overland: 2,1,0", "print: YOU ARE NEAR "])

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

    def test_debug_mode(self):
        # Started with Woof, Ctrl-D sets the debug flag and opens DEBUG.TXT
        # (1614:025b): after each opcode the trace waits for a key
        # (2fd3:3add, 6d7e:093e), the battle's setup traces each record,
        # the combat menu ends with "*", Ctrl-Z writes each record's
        # effects and Ctrl-C quits, closing the file.
        keys = r"\cD\r" + "x" * 14 + r"\cZ\cC"
        result = self.play("--woof", "--test-party", 4, "--effect", "2,22,10,0", "--saves",
                           self.folder, "--keys", keys, ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[-1], "(quit to DOS in block 16)")
        self.assertIn("quit: to DOS", lines)
        self.assertIn("debug: on", lines)
        traced = [line for line in lines if line.startswith("debug: 0 , ")]
        self.assertEqual(traced[:6], ["debug: 0 , 0 , 0 | HORIZONTAL MENU",
                                      "debug: 0 , 0 , 0 | PRINTCLEAR", "debug: 0 , 0 , 0 | RETURN",
                                      "debug: 0 , 0 , 0 | SETUP MONSTER",
                                      "debug: 0 , 0 , 0 | CLEARMONSTERS",
                                      "debug: 0 , 0 , 0 | LOAD MONSTER"])
        self.assertEqual(traced[6:], ["debug: 0 , 1 , 0 | " + name for name in
                                      ("ALDA", "BRAM", "CERA", "DUNN", "BAAZ", "BAAZ", "BAAZ",
                                       "BAAZ")])
        self.assertIn("menu: Move View Aim Use Cast Done*", lines)
        written = ["who: BRAM  sp#: 34"] + ["who: BAAZ  sp#: 67"] * 4
        self.assertEqual([line[11:] for line in lines if line.startswith("debug.txt: ")], written)
        self.assertEqual((self.folder / "DEBUG.TXT").read_bytes(),
                         "".join(line + "\r\n" for line in written).encode())
        # The same in CGA mode (see Display modes).
        cga = self.folder / "cga"
        cga.mkdir()
        result = self.play("--display", "cga", "--woof", "--test-party", 4, "--effect", "2,22,10,0",
                           "--saves", cga, "--keys", keys, ASSETS, 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.replace(str(cga), str(self.folder)), "\n".join(lines) + "\n")
        self.assertEqual((cga / "DEBUG.TXT").read_bytes(), (self.folder / "DEBUG.TXT").read_bytes())
        # Without Woof the keys do nothing more than any other.
        result = self.play("--test-party", 4, "--saves", self.folder / "none", "--keys",
                           "\x04" + r"\r\r", ASSETS, 16)
        self.assertNotIn("debug:", result.stdout)
        self.assertIn("menu: Move View Aim Use Cast Done", result.stdout.splitlines())

    def test_debug_bad_news(self):
        # Effect 0x07's handler (3f44:0208) starts with the trace.
        keys = r"\cD\r\rE" + "x" * 80
        result = self.play("--woof", "--test-party", 6, "--effect", "1,7,0,0", "--set", "4be6=1",
                           "--start", "835f", "--seed", 1, "--combat", "melee", "--keys", keys,
                           ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(result.stdout.splitlines().count("debug: 0 , 0 , 0 | bad news"), 3)

    def test_debug_view_item(self):
        # The debug flag's View in Items (546c:12be) and the status line's
        # "*" (6346:2d75).
        shots = self.folder / "shots"
        shots.mkdir()
        result = self.play("--woof", "--combat", "pass", "--test-party", 6, "--play", "--set",
                           "4be6=1", "--saves", self.folder, "--shots", shots, "--keys",
                           r"\r\rE\cDvivx\e\e\e", ASSETS, 32)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[-1], "(out of keys in block 32 at 8320)")
        self.assertIn("menu: View Ready Use Trade Drop Halve Join", lines)
        start = lines.index("debug: itemptr:      18")
        self.assertEqual(lines[start + 13:start + 17], ["debug: missile type: 4",
                                                        "debug: max range :   0",
                                                        "debug: item type :    0",
                                                        "debug: press a key"])
        images = sorted(shots.glob("*.bmp"))
        view = images[-5].read_bytes()
        self.assertEqual(ink(view, 1, 1), GREEN)
        self.assertEqual(ink(view, 20, 1), GREEN)
        # The adventure screen after: "7,15 N 00:15*" on row 15.
        last = images[-1].read_bytes()
        self.assertEqual(ink(last, 0x11 + 11, 15), GREEN)
        self.assertEqual(ink(last, 0x11 + 12, 15), GREEN)
        self.assertEqual(ink(last, 0x11 + 13, 15), (0, 0, 0))


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
        self.assertEqual(lines[0], "crt: TANDY 1000 USERS - Make sure NUMLOCK is on.")
        self.assertEqual(lines[3:5], ["menu: Play Demo", "choice: Demo"])
        self.assertEqual(lines[5], "print: EVEN WITH THE DEFEAT OF THE DRAGONARMIES,")
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

    def test_ctrl_c_quits_in_the_adventure(self):
        # Started with Woof, Ctrl-C at the first fight's menu quits to DOS
        # (1614:0000): the program halts, main's loop does not go on to
        # the start menu.
        keys = (r"P\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\vS\rE\v\v\v\v\vSA\vS\r\r\r\r\r\r\rH"
                r"\v\v\vSS\v\v\v\v\v\vSLNM\>\r" + "\x03")
        result = self.play(keys, "--woof", "--seed", "7")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[-1], "(quit to DOS)")
        quit = lines.index("quit: to DOS")
        self.assertEqual(lines[quit - 1], "menu: Move View Aim Done")
        self.assertNotIn("list: Choose a FUNCTION ", lines[quit:])
        # Without Woof it is a key like any other.
        result = self.play(keys, "--seed", "7")
        self.assertNotIn("quit: to DOS", result.stdout)

    def test_quitting_from_the_start_menu(self):
        result = self.play(TITLE + r"\^SY")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-2:], ["quit: to DOS", "(quit to DOS)"])


def wav_data(path):
    """The samples of a 16-bit mono 44,100 Hz WAV file, as bytes, its
    header checked."""
    data = path.read_bytes()
    riff, size, wave, fmt, fmt_size, tag, channels, rate, rate2, align, bits, name, length = \
        struct.unpack_from("<4sI4s4sIHHIIHH4sI", data)
    assert (riff, wave, fmt, name) == (b"RIFF", b"WAVE", b"fmt ", b"data"), data[:44]
    assert (fmt_size, tag, channels, rate, rate2, align, bits) == (16, 1, 1, 44100, 88200, 2, 16)
    assert size == len(data) - 8 and length == len(data) - 44
    return data[44:]


class SoundTests(unittest.TestCase):
    """The game's calls to the sound driver (--sound-calls) where the
    original makes them, and the sound rendered by the game's clock
    (--wav)."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def calls(self, *args):
        result = subprocess.run([str(TOOL), "--sound-calls", *map(str, args)], capture_output=True,
                                text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return [line for line in result.stdout.splitlines() if line != "call: record"]

    def test_logged_sounds_are_the_drivers_commands(self):
        # Every sound logged is the driver's command (17e8:0020) at once;
        # the other commands are sound 10 for steps and turns, and main's.
        for args in (["--test-party", 6, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                      "--combat", "auto", "--keys", r"\r\rEN", ASSETS, 32],
                     ["--start-menu", "--saves", self.folder, "--keys", r"\kx\t", ASSETS]):
            lines = self.calls(*args)
            logged = [i for i, line in enumerate(lines) if line.startswith("sound: ")
                      and line[7:].isdigit()]
            self.assertGreater(len(logged), 20)
            for i in logged:
                self.assertEqual(lines[i + 1], "call: command " + lines[i][7:])
            unlogged = {line for i, line in enumerate(lines) if line.startswith("call: command ")
                        and i - 1 not in logged}
            self.assertLessEqual(unlogged, {"call: command 10", "call: command 255"})

    def test_overlays_unhook_the_driver(self):
        # COMBAT (2fd3:191c): the driver unhooked and hooked again around
        # each overlay it loads; through the end of combat (351b) it stays
        # off.
        lines = self.calls("--test-party", 6, "--set", "4be6=1", "--start", "835f", "--seed", 1,
                           "--combat", "auto", "--keys", r"\r\rEN", ASSETS, 32)
        begins = lines.index("print: A battle begins...")
        self.assertEqual(lines[begins - 2:begins], ["call: overlay", "call: overlay end"])
        removed = next(i for i, line in enumerate(lines) if line.startswith("combat: removed"))
        self.assertEqual(lines[removed - 1], "call: overlay")
        self.assertEqual(lines[removed + 1:].count("call: overlay end"), 1)
        self.assertNotIn("call: command", " ".join(lines[removed:]))
        # A shop (36d0) and treasure alone (351b): before and after.
        lines = self.calls("--test-party", 4, "--start", "891c", "--keys",
                           r"y\rB\v\v\r\ePB\r\eEySE", ASSETS, 16)
        shop = lines.index("menu: Buy View Pool Appraise Exit")
        self.assertEqual(lines[shop - 3:shop - 1], ["call: overlay", "call: overlay end"])
        self.assertEqual(lines.count("call: overlay"), 2)
        lines = self.calls("--test-party", 6, "--start", "8bcd", "--keys", r"\rT\r3\r\eSE",
                           ASSETS, 32)
        first = lines.index("call: overlay")
        self.assertEqual(lines[first + 1], "call: overlay end")
        self.assertLess(first, lines.index("menu: View Take Pool Share Exit"))
        self.assertEqual(lines.count("call: overlay"), 2)
        # The temple (340d): before it and after.
        lines = self.calls("--test-party", 4, "--start", "8a8f", "--keys", r"\rPH\v\vHy\eEn\r",
                           ASSETS, 16)
        lines = [line for line in lines if line.startswith(("call: overlay", "menu: Heal View Pool"))
                 or line == "menu: ~Yes ~No"]
        self.assertEqual(lines, ["call: overlay", "call: overlay end", "menu: Heal View Pool Appraise Exit",
                                 "menu: ~Yes ~No", "call: overlay", "call: overlay end"])
        # A battle won: before it, around the end of combat, and after
        # (475c or 6961).
        lines = self.calls("--test-party", 6, "--combat", "won", "--start", "8cda", "--keys", r"\rSE\r",
                           ASSETS, 16)
        lines = [line for line in lines if line.startswith("call: overlay")
                 or line == "menu: View Take Pool Share Exit"]
        self.assertEqual(lines, ["call: overlay", "call: overlay end", "call: overlay",
                                 "menu: View Take Pool Share Exit", "call: overlay end",
                                 "call: overlay", "call: overlay end"])

    def test_install_without_the_title(self):
        # A block or saved game runs past main's install (1000:0160); --sound
        # wants --wav.
        lines = self.calls("--test-party", 4, "--play", "--set", "4be6=1", "--keys", r"\r\rm\^",
                           ASSETS, 32)
        self.assertEqual(lines[0], "call: install")
        result = subprocess.run([str(TOOL), "--sound", "speaker", "--start-menu", str(ASSETS)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--sound needs --wav", result.stderr)

    def test_the_title_and_main(self):
        lines = self.calls("--start-menu", "--saves", self.folder, "--keys", r"\kx\t", ASSETS)
        # After startup's note to Tandy users (11c7:08ac).
        self.assertEqual(lines[0], "crt: TANDY 1000 USERS - Make sure NUMLOCK is on.")
        self.assertEqual(lines[1:4], ["sound: music", "call: music", "call: music end"])
        self.assertEqual(lines[4:6], ["sound: install", "call: install"])
        demo = lines.index("choice: Demo")
        # 1000:01d8, 0282 and 02b8.
        self.assertEqual(lines[demo + 1:demo + 7], ["call: overlay", "call: overlay end"] * 3)
        # After the demonstration (3e99:0000), the driver off for the title
        # (1000:0304) and on again after it (1000:030e).
        title = len(lines) - lines[::-1].index("call: command 255") - 1
        self.assertEqual(lines[title - 2:title], ["call: overlay", "call: overlay end"])
        self.assertEqual(lines[title + 1:title + 3], ["sound: music", "call: music"])
        self.assertEqual([line for line in lines[title:] if line.startswith("call: ")],
                         ["call: command 255", "call: music", "call: music end", "call: install"])

    def test_steps_and_turns(self):
        # A step forward plays sound 10 as it starts (475c:081a) and again
        # once the party is on its new square (2fd3:3eb9); a turn plays it
        # (475c:0c58); none is logged.
        lines = self.calls("--combat", "pass", "--test-party", 6, "--play", "--set", "4be6=1",
                           "--keys", r"\r\rE\rm\^\>", ASSETS, 32)
        lines = [line for line in lines if line.startswith(("at: ", "call: ", "sound: "))]
        start = lines.index("at: 7,14,0") - 1
        self.assertEqual(lines[start:start + 5], ["call: command 10", "at: 7,14,0", "call: command 10",
                                                  "call: command 10", "at: 7,14,2"])
        self.assertNotIn("sound: 10", lines)

    def test_ctrl_s(self):
        # Ctrl-S read by a menu (1614:025b) calls the driver; typed into a
        # name (1521:0739, Crt's ReadKey) it does not.
        lines = self.calls("--start-menu", "--saves", self.folder, "--keys",
                           r"\kx" + "\x13" + r"P\r" + r"\v" * 6 + r"\r\r\v\r\rN" + "\x13" + r"BOB\r",
                           ASSETS)
        self.assertEqual(lines.count("call: ctrl-s"), 1)
        self.assertLess(lines.index("call: ctrl-s"), lines.index("list: Choose a FUNCTION "))

    def test_wav(self):
        # By the game's clock: the same samples each time; the title's
        # music for its 35 seconds and more; a \p pause before a key read
        # its hundredths more; the speaker's sound unlike the Tandy's; none
        # with the sound off.
        def render(name, keys, *args):
            path = self.folder / name
            result = subprocess.run([str(TOOL), "--start-menu", "--saves", self.folder, "--wav", path,
                                     *map(str, args), "--keys", keys, str(ASSETS)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return wav_data(path)
        tandy = render("a.wav", r"\kx")
        self.assertEqual(tandy, render("b.wav", r"\kx"))
        # \kx ends the title at its first poll: then a second of the tail.
        self.assertEqual(len(tandy), 2 * 44100)
        paused = render("c.wav", r"\kx\p250.x")
        self.assertEqual(len(paused) - len(tandy), 2 * 110250)
        title = render("d.wav", r"\t")
        samples = struct.unpack("<%dh" % (len(title) // 2), title)
        self.assertGreater(len(samples), 35 * 44100)
        for second in range(34):
            chunk = samples[second * 44100:(second + 1) * 44100]
            self.assertGreater(max(chunk) - min(chunk), 2000, second)
        speaker = render("e.wav", r"\t", "--sound", "speaker")
        self.assertEqual(len(speaker), len(title))
        self.assertNotEqual(speaker, title)
        off = render("f.wav", r"\t", "--sound", "off")
        self.assertEqual(off, bytes(len(title)))

    def assets_with(self, config):
        """A copy of the assets (links) whose KRYNN.CFG holds config, or
        none."""
        assets = self.folder / ("assets-%d" % len(list(self.folder.glob("assets-*"))))
        assets.mkdir()
        for path in ASSETS.iterdir():
            if path.name.upper() != "KRYNN.CFG":
                (assets / path.name).symlink_to(path)
        if config is not None:
            (assets / "KRYNN.CFG").write_bytes(config)
        return assets

    def test_krynn_cfg(self):
        # The device is KRYNN.CFG's second line's first character (11c7:031a);
        # --sound overrides it; without the file the setup asks.
        def render(assets, *args, keys=r"\t"):
            path = self.folder / ("r%d.wav" % len(list(self.folder.glob("r*.wav"))))
            result = subprocess.run([str(TOOL), "--start-menu", "--saves", self.folder, "--wav", path,
                                     *map(str, args), "--keys", keys, str(assets)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result, wav_data(path)
        _, tandy = render(ASSETS)
        _, speaker = render(ASSETS, "--sound", "speaker")
        _, off = render(ASSETS, "--sound", "off")
        result, data = render(self.assets_with(b"E\r\nP\r\nM\r\n"))
        self.assertEqual(data, speaker)
        self.assertEqual(render(self.assets_with(b"E\r\nP\r\n"), "--sound", "tandy")[1], tandy)
        self.assertEqual(render(self.assets_with(b"E\r\nS\r\n"))[1], off)
        # Without the file the setup asks: [4] Tandy 1000.
        result, data = render(self.assets_with(None), keys=r"343\r\t")
        self.assertEqual(data, tandy)
        self.assertIn("crt:              [4] Tandy 1000", result.stdout.splitlines())
        (self.folder / "KRYNN.CFG").unlink()  # the setup's, in --saves
        # The AdLib: the title's music (MIDI.DAX record 3), the effects on
        # the speaker, as --sound adlib renders them.
        result, data = render(self.assets_with(b"E\r\nA\r\nM\r\n"))
        self.assertNotIn("unported", result.stdout)
        self.assertNotIn("not ported", result.stderr)
        self.assertEqual(render(ASSETS, "--sound", "adlib")[1], data)
        samples = struct.unpack("<%dh" % (len(data) // 2), data)
        self.assertTrue(any(samples[:35 * 44100]))
        self.assertNotEqual(data, tandy)
        self.assertNotEqual(data, speaker)
        # CMS, the MT-32: the title silent, its music logged as not ported,
        # the effects on the speaker.
        for letter, name, record in ((b"C", "C/MS", 1), (b"R", "Roland LAPC", 2)):
            result, data = render(self.assets_with(b"E\r\n" + letter + b"\r\nM\r\n"))
            line = "unported: the title's music on %s (MIDI.DAX record %d)" % (name, record)
            self.assertIn(line, result.stdout.splitlines())
            self.assertIn("not ported", result.stderr)
            samples = struct.unpack("<%dh" % (len(data) // 2), data)
            self.assertFalse(any(samples[:35 * 44100]))
            self.assertTrue(any(samples[35 * 44100:]))

    def test_without_the_music_files(self):
        # Without MIDI.DAX the title is silent and says why; the effects
        # play all the same.
        assets = self.folder / "assets"
        assets.mkdir()
        for path in ASSETS.iterdir():
            if path.name != "MIDI.DAX":
                (assets / path.name).symlink_to(path)
        path = self.folder / "a.wav"
        result = subprocess.run([str(TOOL), "--start-menu", "--saves", self.folder, "--wav", path,
                                 "--keys", r"\kx\t", str(assets)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("sound: no music: ", result.stderr)
        self.assertIn("MIDI.DAX", result.stderr)
        samples = wav_data(path)
        self.assertTrue(any(samples))


# The CGA's colours (mode 4, palette 1 with intensity, as the BIOS leaves
# it), and the 16 of the EGA and the Tandy.
CGA = [(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)]
EGA = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170),
       (170, 85, 0), (170, 170, 170), (85, 85, 85), (85, 85, 255), (85, 255, 85),
       (85, 255, 255), (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]
# Text's colours in CGA mode (DS:1de9, 1521:0d1e).
CGA_TEXT = [0, 0, 0, 1, 2, 2, 2, 3, 0, 1, 1, 1, 2, 2, 3, 3]
# From the title: Play, create BOB; at the icon editor, Enter on
# 1st-color, then Escape.
TO_ICON = r"\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\r\e"
NEW_GAME = (r"P\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\vS\rE\v\v\v\v\vSA\vS\r\r\r\r\r\r\rH"
            r"\v\v\vSS\v\v\v\v\v\vSLNM\>\r\@xN\r\eN\r\r\r\r\r\rELN98yA\@xN\r\eN"
            r"\^\^\>\^L\vEERESANE")


class DisplayTests(unittest.TestCase):
    """The display mode (DS:4b76): KRYNN.CFG's first line or --display."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def play(self, keys, *args, assets=ASSETS):
        n = len(list(self.folder.glob("run*")))
        saves, shots = self.folder / ("run%d" % n), self.folder / ("shots%d" % n)
        saves.mkdir()
        result = subprocess.run([str(TOOL), "--start-menu", "--seed", "7", "--saves", str(saves),
                                 "--shots", str(shots), *map(str, args), "--keys", keys,
                                 str(assets)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result, [p.read_bytes() for p in sorted(shots.glob("*.bmp"))]

    def same(self, shots, expected):
        """Shots compared by digest: a failing assertEqual on lists of
        BMPs would spend minutes diffing them."""
        digest = lambda bmps: [hashlib.sha1(b).hexdigest() for b in bmps]
        self.assertEqual(digest(shots), digest(expected))

    def colours(self, bmp):
        return {pixel(bmp, x, y) for x in range(320) for y in range(200)}

    def test_ega_is_tandy(self):
        # EGA's routines give the Tandy's 16-colour screens: the new game
        # (the title, the start menu, creating BOB, the overland map, the
        # fights, camps, Throtl in 3D) frame for frame and line for line;
        # the assets' KRYNN.CFG says E.
        tandy, tandy_shots = self.play(NEW_GAME, "--display", "tandy")
        ega, ega_shots = self.play(NEW_GAME, "--display", "ega")
        default, default_shots = self.play(NEW_GAME)
        self.assertGreater(len(tandy_shots), 90)
        self.assertEqual(ega.stdout, tandy.stdout)
        self.assertEqual(default.stdout, tandy.stdout)
        self.same(ega_shots, tandy_shots)
        self.same(default_shots, tandy_shots)

    def test_cga(self):
        # The same game in CGA mode: the same lines, every screen in the
        # four CGA colours; the menus' text through DS:1de9.
        tandy, tandy_shots = self.play(NEW_GAME, "--display", "tandy")
        cga, cga_shots = self.play(NEW_GAME, "--display", "cga")
        self.assertEqual(cga.stdout, tandy.stdout)
        self.assertEqual(len(cga_shots), len(tandy_shots))
        everything = set()
        for shot in cga_shots:
            everything |= self.colours(shot)
        self.assertEqual(everything, set(CGA))
        texts = 0
        for shot, before in zip(cga_shots, tandy_shots):
            # Row 24 holds a menu or nothing: text alone.
            row = [pixel(before, x, 24 * 8 + y) for x in range(320) for y in range(8)]
            if set(row) != {(0, 0, 0)}:
                texts += 1
                self.assertEqual([pixel(shot, x, 24 * 8 + y) for x in range(320) for y in range(8)],
                                 [CGA[CGA_TEXT[EGA.index(c)]] for c in row])
        self.assertGreater(texts, 50)

    def test_cga_view(self):
        # The view after the fight at Throtl's gate (7, 15 facing north): in
        # CGA mode the backdrop is four fills through DS:1e1e and no
        # pictures (69ea:0184): the sky (0) black, the ground 6 (0xc059 at
        # 0x41 when the block was entered: 6945:007c) cyan.
        final = self.folder / "final.bmp"
        result = subprocess.run([str(TOOL), "--display", "cga", "--combat", "pass",
                                 "--test-party", "6", "--set", "4be6=1", "--keys", r"\r\rE",
                                 "--screen", str(final), str(ASSETS), "32"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        bmp = final.read_bytes()
        self.assertEqual(pixel(bmp, 8 * 8 + 4, 3 * 8 + 2), CGA[0])
        self.assertEqual(pixel(bmp, 8 * 8 + 4, 13 * 8 + 4), CGA[1])
        self.assertEqual(self.colours(bmp), set(CGA))

    def test_icon_editor(self):
        # 1st-color and 2nd-color are not in CGA mode (4def:4276): the
        # message on row 24, and the editor stays at its first menu; the
        # icons shown on cyan (colour 3 through DS:1e1e), without COMSPR
        # 25's frame.
        cga, shots = self.play(TO_ICON, "--display", "cga")
        lines = cga.stdout.splitlines()
        at = lines.index("print: Sorry, not in CGA")
        self.assertEqual(lines[at - 1:at + 2], ["menu: Parts 1st-color 2nd-color Size Exit"] * 1 +
                         ["print: Sorry, not in CGA", "menu: Parts 1st-color 2nd-color Size Exit"])
        self.assertEqual(pixel(shots[-2], 10 * 8, 7 * 8 + 4), CGA[1])
        # The frame's corner (white in the other modes) is not there.
        self.assertEqual(pixel(shots[-2], 4 * 8, 7 * 8), CGA[1])
        # 2nd-color (4def:42d0) likewise, after 1st-color's message.
        second, _ = self.play(TO_ICON[:-2] + r"2\e", "--display", "cga")
        lines = second.stdout.splitlines()
        self.assertEqual(lines.count("print: Sorry, not in CGA"), 2)
        at = len(lines) - 1 - lines[::-1].index("print: Sorry, not in CGA")
        self.assertEqual(lines[at - 1:at + 2], ["menu: Parts 1st-color 2nd-color Size Exit",
                                                "print: Sorry, not in CGA",
                                                "menu: Parts 1st-color 2nd-color Size Exit"])
        self.assertNotIn("menu: Weapon Body Face Shield Arm Leg Exit", lines)
        ega, ega_shots = self.play(TO_ICON, "--display", "ega")
        self.assertEqual(pixel(ega_shots[-3], 10 * 8, 7 * 8 + 4), EGA[0])
        self.assertEqual(pixel(ega_shots[-3], 4 * 8, 7 * 8), EGA[15])
        self.assertNotIn("print: Sorry, not in CGA", ega.stdout)
        self.assertIn("menu: Weapon Body Hair Shield Arm Leg Exit", ega.stdout.splitlines())

    def test_krynn_cfg(self):
        # KRYNN.CFG's first line (11c7:031a): C (or anything but E and T)
        # CGA, T the Tandy; without the file the setup asks; --display
        # overrides it.
        def assets_with(config):
            assets = self.folder / ("assets-%d" % len(list(self.folder.glob("assets-*"))))
            assets.mkdir()
            for path in ASSETS.iterdir():
                if path.name.upper() != "KRYNN.CFG":
                    (assets / path.name).symlink_to(path)
            if config is not None:
                (assets / "KRYNN.CFG").write_bytes(config)
            return assets
        keys = r"\kx\t"
        _, tandy = self.play(keys, "--display", "tandy")
        _, cga = self.play(keys, "--display", "cga")
        self.assertFalse(tandy == cga)
        self.same(self.play(keys, assets=assets_with(b"C\r\nT\r\n"))[1], cga)
        self.same(self.play(keys, assets=assets_with(b"x\r\nT\r\n"))[1], cga)
        self.same(self.play(keys, assets=assets_with(b"T\r\nT\r\n"))[1], tandy)
        self.same(self.play(keys, "--display", "tandy",
                            assets=assets_with(b"C\r\nT\r\n"))[1], tandy)
        # Without the file the setup asks ([3] Tandy), its text screens
        # saved at its four key reads.
        result, shots = self.play(r"343\r" + keys, assets=assets_with(None))
        self.assertEqual([struct.unpack_from("<i", b, 18)[0] for b in shots[:5]],
                         [640, 640, 640, 640, 320])
        self.same(shots[4:], tandy)
        bad = subprocess.run([str(TOOL), "--display", "vga", "--start-menu", str(ASSETS)],
                             capture_output=True, text=True)
        self.assertEqual(bad.returncode, 2)


class SetupTests(unittest.TestCase):
    """Startup's text screens (11c7:031a): the setup without KRYNN.CFG, which
    writes it, and the note to Tandy users at every start."""

    NOTE = "crt: TANDY 1000 USERS - Make sure NUMLOCK is on."

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)
        self.assets = self.folder / "assets"
        self.assets.mkdir()
        for path in ASSETS.iterdir():
            if path.name.upper() != "KRYNN.CFG":
                (self.assets / path.name).symlink_to(path)

    def play(self, keys, *args, assets=None):
        return subprocess.run([str(TOOL), "--start-menu", *map(str, args), "--keys", keys,
                               str(assets or self.assets)], capture_output=True, text=True)

    def test_the_setup_writes_the_file(self):
        saves, shots = self.folder / "saves", self.folder / "shots"
        saves.mkdir()
        # A digit out of range and a letter do nothing; Alt-M's scan code
        # (0x32) is a 2; the path's letters are upper-cased and a \ added.
        keys = r"91\#32\#3bx2a:\\some\\pla\b\bath\r\t"
        result = self.play(keys, "--saves", saves, "--shots", shots, "--sound-calls")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:11], [
            "crt: GRAPHICS ADAPTER TYPE:  [1] CGA  [2] EGA  [3] Tandy  1",
            "crt: SOUND TYPE:  [1] C/MS", "crt:              [2] Roland LAPC",
            "crt:              [3] AdLib", "crt:              [4] Tandy 1000",
            "crt:              [5] PC", "crt:              [6] Silent", "crt: 2",
            "crt: ALTERNATE INPUT TYPE:  [1] JOYSTICK  [2] MOUSE  [3] NEITHER  2",
            r"crt: PATH TO SAVE DATA (DEFAULT - C:\COK\SAVE):  a:\some\path",
            self.NOTE])
        written = (saves / "KRYNN.CFG").read_bytes()
        self.assertEqual(written, b"C\r\nR\r\nM\r\nA:\\SOME\\PATH\\\r\nF\r\n" + b"0\r\n" * 14)
        # CGA, as the file says, and the text screen at each key read,
        # 640x200, light grey on black.
        bmps = sorted(shots.glob("*.bmp"))
        first = bmps[0].read_bytes()
        self.assertEqual(struct.unpack_from("<ii", first, 18), (640, 200))
        self.assertEqual(pixel(first, 0, 0), (0, 0, 0))
        self.assertIn((170, 170, 170), {pixel(first, x, y) for x in range(8) for y in range(8)})
        # The path as typed, in the PC-style font (src/crt.c): its a in
        # lower case, column 44, not the A of PATH at column 1.
        text = [b.read_bytes() for b in bmps if struct.unpack_from("<i", b.read_bytes(), 18)[0] == 640]
        cell = lambda bmp, column: [sum(128 >> x for x in range(8)
                                        if pixel(bmp, column * 8 + x, y) == (170, 170, 170))
                                    for y in range(8)]
        self.assertEqual(cell(text[-1], 44), [0x00, 0x00, 0x3c, 0x06, 0x3e, 0x66, 0x3e, 0x00])
        self.assertEqual(cell(text[-1], 1), [0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00])
        later =[b.read_bytes() for b in bmps if struct.unpack_from("<i", b.read_bytes(), 18)[0] == 320]
        self.assertTrue(later)
        self.assertLessEqual({pixel(later[0], x, y) for x in range(0, 320, 7) for y in range(0, 200, 7)},
                             {(0, 0, 0), (85, 255, 255), (255, 85, 255), (255, 255, 255)})
        # The next run reads it: only the note.
        again = self.play(r"\t", "--saves", saves, "--sound-calls")
        self.assertEqual(again.stdout.splitlines()[0], self.NOTE)
        self.assertNotIn("crt: SOUND TYPE:  [1] C/MS", again.stdout.splitlines())
        self.assertEqual((saves / "KRYNN.CFG").read_bytes(), written)

    def test_the_default_path(self):
        # Too short, no colon, a drive past P: the default, for 2 seconds;
        # A: on a hard disk is taken.
        for typed, line, path in ((r"c:\r", "c:", b"C:\\COK\\SAVE\\"),
                                  (r"cx\\abc\r", "cx\\abc", b"C:\\COK\\SAVE\\"),
                                  (r"q:\\abc\r", "q:\\abc", b"C:\\COK\\SAVE\\"),
                                  (r"a:\\x\\\r", "a:\\x\\", b"A:\\X\\")):
            config = self.folder / ("k%d.cfg" % len(list(self.folder.glob("k*.cfg"))))
            wav = self.folder / "a.wav"
            result = self.play("343" + typed + r"\t", "--config", config, "--wav", wav)
            self.assertEqual(result.returncode, 0, result.stderr)
            lines = result.stdout.splitlines()
            self.assertIn(r"crt: PATH TO SAVE DATA (DEFAULT - C:\COK\SAVE):  " + line, lines)
            self.assertEqual(r"crt: USING DEFAULT PATH: C:\COK\SAVE" in lines,
                             path == b"C:\\COK\\SAVE\\")
            self.assertEqual(config.read_bytes().splitlines()[3], path)
            # The sound's time starts after the text screens.
            self.assertEqual(wav_data(wav), wav_data_of(self, r"\t"))

    def test_the_line_editor(self):
        # Escape erases the line, Ctrl-D and Ctrl-F bring back what the
        # buffer holds, an extended key and Ctrl-Z do nothing.
        config = self.folder / "k.cfg"
        result = self.play("343xyz\\ec:\\\\ab\\b\\b\x04\\#3b\x1a\x06\\r", "--config", config)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(config.read_bytes().splitlines()[3], b"C:\\AB\\")

    def test_the_end_of_the_keys(self):
        saves = self.folder / "saves"
        saves.mkdir()
        result = self.play("34", "--saves", saves)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines()[-1], "(out of keys in the setup)")
        self.assertFalse((saves / "KRYNN.CFG").exists())

    def test_where_the_file_is(self):
        saves = self.folder / "saves"
        saves.mkdir()
        # Without --saves or --config the setup's answers hold, written
        # nowhere.
        result = self.play(r"143c:\\x\r\t")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("written nowhere", result.stderr)
        self.assertEqual(list(self.assets.glob("*.CFG")) + list(self.assets.glob("*.cfg")), [])
        # It cannot be made: the original's message, status 1.
        result = self.play("1", "--config", self.folder / "no" / "KRYNN.CFG")
        self.assertEqual(result.returncode, 1)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:4], [
            "crt: Sorry, disk A can't be write-protected.", "crt: ",
            "crt: If these are your masters, use DISKCOPY to make duplicates.", "crt: "])
        self.assertTrue(lines[4].startswith("crt: The port stops here: "), lines)
        self.assertIn("No such file or directory", result.stderr)
        # Without --start-menu no setup runs: the file missing is named.
        missing = self.folder / "no.cfg"
        result = subprocess.run([str(TOOL), "--config", str(missing), "--keys", r"\r", "--wav",
                                 str(self.folder / "m.wav"), str(ASSETS), "16"],
                                capture_output=True, text=True)
        self.assertIn("display: no %s, " % missing, result.stderr)
        self.assertIn("sound: no %s, " % missing, result.stderr)
        # The saves directory's comes before the assets'; --config before both.
        (self.assets / "krynn.cfg").write_bytes(b"T\r\nT\r\nN\r\n")
        (saves / "KRYNN.CFG").write_bytes(b"T\r\nS\r\nN\r\n")
        wav = self.folder / "s.wav"
        result = self.play(r"\t", "--saves", saves, "--wav", wav)
        self.assertEqual(result.stdout.splitlines()[0], self.NOTE)
        self.assertFalse(any(wav_data(wav)))
        mine = self.folder / "mine.cfg"
        mine.write_bytes(b"T\r\nT\r\n")
        self.play(r"\t", "--saves", saves, "--config", mine, "--wav", wav)
        self.assertTrue(any(wav_data(wav)))
        (saves / "KRYNN.CFG").unlink()
        self.play(r"\t", "--saves", saves, "--wav", wav)
        self.assertTrue(any(wav_data(wav)))

    def test_initialize_mouse_joystick(self):
        # Each Select writes KRYNN.CFG again with its third line N, read
        # from the assets, written in the saves directory.
        saves = self.folder / "saves"
        saves.mkdir()
        (self.assets / "KRYNN.CFG").write_bytes(b"E\r\nT\r\nM\r\nC:\\COK\\SAVE\\\r\nF\r\n" + b"0\r\n" * 14)
        result = self.play(r"\kxP\v\v\v\r\r\e", "--saves", saves)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("config: line 3 N", result.stdout.splitlines())
        self.assertEqual((saves / "KRYNN.CFG").read_bytes(),
                         b"E\r\nT\r\nN\r\nC:\\COK\\SAVE\\\r\nF\r\n" + b"0\r\n" * 14)
        self.assertEqual((self.assets / "KRYNN.CFG").read_bytes()[6:9], b"M\r\n")


# The mouse and the joystick: \\mX,Y,B. clicks at pixel X, Y (B 1 the left
# button, 2 the right), \\mX,Y. only moves the pointer, \\jX,Y,B. puts the
# stick at counts X, Y with its buttons B.
CREATE = r"\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\v\rAE"
# A KRYNN.CFG with the joystick, calibrated: counts 1-250 across and down.
JOYSTICK_CFG = (b"E\r\nT\r\nJ\r\nC:\\COK\\SAVE\\\r\nF\r\n" +
                b"".join(b"%d\r\n%d\r\n" % (n, n) for n in (1, 40, 80, 120, 160, 200, 250)))
PRESS, DOWN = r"\j129,129,1.\j129,129.", r"\j129,250.\j129,129."


class MouseTests(unittest.TestCase):
    """The mouse's clicks (1614:025b's, 67b5:03e2's) and the joystick's keys,
    as eclplay --mouse and --joystick give them."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def play(self, *args):
        return subprocess.run([str(TOOL), *map(str, args)], capture_output=True, text=True)

    def both(self, clicks, keys, *args, name="", start_menu=True):
        """The run with clicks, and with keys where the pointer moves alone to
        each click's place: the same transcript and screens."""
        runs = []
        for which, script in (("c", clicks), ("k", keys)):
            folder = self.folder / (name + which)
            shots, saves = folder / "shots", folder / "saves"
            saves.mkdir(parents=True)
            front = ["--start-menu", "--saves", saves] if start_menu else []
            result = self.play(*front, "--mouse", "--shots", shots, "--keys", script, *args)
            self.assertEqual(result.returncode, 0, result.stderr)
            runs.append((result, shots, saves))
        (clicked, cshots, csaves), (keyed, kshots, ksaves) = runs
        self.assertEqual(clicked.stdout, keyed.stdout)
        names = sorted(p.name for p in kshots.glob("*.bmp"))
        self.assertEqual(sorted(p.name for p in cshots.glob("*.bmp")), names)
        for n in names:
            self.assertEqual((cshots / n).read_bytes(), (kshots / n).read_bytes(), n)
        return clicked, cshots, csaves, ksaves

    def test_menus_lists_and_modify(self):
        """Clicks on a list's row (it is picked out), on row 24's items, on
        Modify's sheet (Home to the field) are the keys that do the same."""
        # First a click on the party's row in the start menu, which does
        # nothing there (DS:8985 clear); on the sheet, after the score's
        # field, Down, then a click beside a field (Home, the field kept),
        # a click on the field it is on, Enter to the next field and the
        # Home key (not a click: nothing).
        clicks = (CREATE + r"\m100,36,1.\m100,116,1.\m148,196,1.\m124,196,1.\m52,84,1.\v"
                  r"\m160,100,1.\m52,92,1.\r\#47\m68,196,1.\m172,196,1.")
        keys = (CREATE + r"\m100,36.\#47\m100,116.\v\m148,196.\r\m124,196.\r\m52,84.\v\v"
                r"\m160,100.\#47\m52,92.\#47\r\#47\m68,196.A\m172,196.K")
        result, shots, csaves, ksaves = self.both(clicks, keys, "--seed", 5, ASSETS)
        lines = result.stdout.splitlines()
        self.assertIn("choice: Modify Character", lines)
        self.assertEqual(lines.count("menu: Add Subtract Keep Exit"), 8)
        self.assertEqual((csaves / "BOB.WHO").read_bytes(), (ksaves / "BOB.WHO").read_bytes())
        # The cursor, where the last click left it: its second row white
        # after black (DS:42b4).
        last = sorted(shots.glob("*.bmp"))[-1].read_bytes()
        self.assertEqual((pixel(last, 172, 197), pixel(last, 173, 197)), ((0, 0, 0), WHITE))

    def test_home_needs_a_click(self):
        """Modify's Home goes to the field clicked only after a click
        (4def:2bc5): the name clicked, Enter to the hit points (the field
        clicked kept), then the Home key does nothing, so Add and Keep follow
        as without it (the name's field would read them as its keys)."""
        enter = CREATE + r"\m100,116,1.\m148,196,1.\m124,196,1.\m20,12,1.\r"
        runs = []
        for extra in (r"\#47", ""):
            saves = self.folder / ("saves" + str(len(runs)))
            saves.mkdir()
            result = self.play("--start-menu", "--mouse", "--seed", 5, "--saves", saves, "--keys",
                               enter + extra + r"A\m172,196,1.", ASSETS)
            self.assertEqual(result.returncode, 0, result.stderr)
            runs.append([line for line in result.stdout.splitlines()
                         if line != "menu: Add Subtract Keep Exit"])
        self.assertEqual(runs[0], runs[1])
        self.assertEqual(runs[0].count("list: Choose a FUNCTION "), 4)

    def test_without_a_driver(self):
        """Without --mouse the machine has no driver: KRYNN.CFG's M finds
        none, the clicks do nothing and no cursor shows."""
        shots = self.folder / "shots"
        plain = self.play("--start-menu", "--keys", r"\kxP", ASSETS)
        result = self.play("--start-menu", "--shots", shots, "--keys", r"\kx\m190,195,1.P", ASSETS)
        self.assertEqual(result.stdout, plain.stdout)
        menu = sorted(shots.glob("*.bmp"))[1].read_bytes()
        self.assertEqual(pixel(menu, 161, 101), (0, 0, 0))

    def test_the_title_hides_the_cursor(self):
        """The title hides the cursor while it waits (2f55:0651), the menu
        after it shows it, at the screen's middle (3e99:005b)."""
        mouse, plain = self.folder / "mouse", self.folder / "plain"
        self.play("--start-menu", "--mouse", "--shots", mouse, "--keys", r"\kx", ASSETS)
        self.play("--start-menu", "--shots", plain, "--keys", r"\kx", ASSETS)
        self.assertEqual((mouse / "000.bmp").read_bytes(), (plain / "000.bmp").read_bytes())
        menu = (mouse / "001.bmp").read_bytes()
        self.assertEqual((pixel(menu, 160, 100), pixel(menu, 160, 101), pixel(menu, 161, 101)),
                         ((0, 0, 0), (0, 0, 0), WHITE))

    def test_initialize_mouse_off_and_on(self):
        """Initialize Mouse/Joystick says mouse ON; turned off (N written) the
        cursor goes and clicks do nothing; on again by key, M written."""
        saves, shots = self.folder / "saves", self.folder / "shots"
        saves.mkdir()
        # The start menu's fourth row, Select; then the first row, Select.
        keys = r"\kx\m190,195,1.\m100,124,1.\m148,196,1.\m80,100,1.\m172,196,1."
        # Off: the pointer moved (a click there not read as one), a key that
        # does nothing (Home), then Enter: on again.
        result = self.play("--start-menu", "--mouse", "--saves", saves, "--shots", shots,
                           "--keys", keys + r"\m300,60,1.\#47\r", ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith("config: ")],
                         ["config: line 3 N", "config: line 3 M"])
        self.assertEqual((saves / "KRYNN.CFG").read_bytes()[6:9], b"M\r\n")
        frames = sorted(shots.glob("*.bmp"))
        # Off: no cursor where the pointer went (4def:6072's hide).
        off = frames[-2].read_bytes()
        self.assertEqual((pixel(off, 300, 60), pixel(off, 301, 61)), ((0, 0, 0), (0, 0, 0)))
        # On again: the cursor shows at 639, 100 (no move: 4def:5cc7).
        on = frames[-1].read_bytes()
        self.assertEqual((pixel(on, 319, 100), pixel(on, 319, 101)), ((0, 0, 0), (0, 0, 0)))

    def test_walking_and_travelling(self):
        """In Move mode a click in the view steps forward, turns or turns
        round (DS:1ff5); on the overland map it steps toward the cell
        (1743:0167)."""
        fwd, right, left = r"\m68,36,1.", r"\m108,68,1.", r"\m28,68,1."
        walk = ("--combat", "pass", "--test-party", 6, "--play", "--set", "4be6=1")
        clicks = r"\r\rE\rm" + fwd * 3 + r"\r\r" + right + fwd + left
        keys = r"\r\rE\rm" + (r"\m68,36." + r"\^") * 3 + r"\r\r\m108,68.\>\m68,36.\^\m28,68.\<"
        result, _, _, _ = self.both(clicks, keys, *walk, ASSETS, 32, name="walk", start_menu=False)
        self.assertEqual([line for line in result.stdout.splitlines() if line.startswith("at: ")],
                         ["at: 7,14,0", "at: 7,13,0", "at: 7,12,0", "at: 7,12,2", "at: 7,12,0"])
        overland = ("--test-party", 4, "--play", "--set", "4bf2=11", "--set", "4bc3=1",
                    "--set", "4bc4=3", "--set", "4c2d=1")
        result, _, _, _ = self.both(r"nm\m36,20,1.\m28,12,1.y", r"nm\m36,20.9\m28,12.8y",
                                    *overland, ASSETS, 16, name="overland", start_menu=False)
        self.assertEqual([line for line in result.stdout.splitlines()
                          if line.startswith("overland: ")][:2],
                         ["overland: 2,2,1", "overland: 2,1,0"])

    def test_combat_steps_and_the_cursor(self):
        """In combat's Move a click is a step toward its cell, from the
        mover; in Manual aim, a step of the cursor (DS:71a6, 71a5)."""
        battle = ("--test-party", 4, "--play", "--set", "4be6=1")
        self.both(r"\r\rm\m164,104,1.\r", r"\r\rm\m164,104.6\r", *battle, ASSETS, 32,
                  name="move", start_menu=False)
        result, _, _, _ = self.both(r"\rAM\m164,104,1.\m116,80,1.E", r"\rAM\m164,104.6\m116,80.9E",
                                    *battle, ASSETS, 32, name="aim", start_menu=False)
        self.assertIn("menu: Next Prev Manual Center Exit", result.stdout.splitlines())

    def test_the_party_list_picks(self):
        """A click on the party list beside the view selects that member
        (DS:8985), drawn in white."""
        shots = self.folder / "shots"
        result = self.play("--combat", "pass", "--test-party", 6, "--play", "--set", "4be6=1",
                           "--mouse", "--input", "mouse", "--shots", shots, "--keys",
                           r"\r\rE\r\m140,60,1.\m140,36.", ASSETS, 32)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse([line for line in result.stdout.splitlines() if line.startswith("at: ")])
        last = sorted(shots.glob("*.bmp"))[-1].read_bytes()
        self.assertEqual((ink(last, 0x11, 7), ink(last, 0x11, 4)), (WHITE, CYAN))

    def test_modify_starts_with_no_field_clicked(self):
        """A second Modify starts with no field clicked (4def:29d8, DS:6e35
        0xff): a click beside a field's row does nothing, where it would go
        back to the field the first Modify clicked."""
        modify = r"\m100,116,1.\m148,196,1.\m124,196,1."
        first = modify + r"\m52,84,1.\m172,196,1."  # a score clicked, Keep
        clicks = CREATE + first + modify + r"\m20,76.\m20,76,1.A\m172,196,1."
        keys = CREATE + first + modify + r"\m20,76.\#47A\m172,196,1."
        self.both(clicks, keys, "--seed", 5, ASSETS)

    def test_stick_pushes_and_the_file(self):
        """Two pushes in a row are two keys (the menus wait for the stick's
        buttons only, 67b5:0c2d); with the assets' M a stick on the machine
        stays off, its pushes nothing."""
        saves = self.folder / "saves"
        saves.mkdir()
        (saves / "KRYNN.CFG").write_bytes(JOYSTICK_CFG)
        keys = r"\kx" + PRESS + r"\j129,250.\j129,250.\j129,129." + PRESS
        result = self.play("--start-menu", "--joystick", "--saves", saves, "--keys", keys, ASSETS)
        self.assertIn("choice: Load Saved Game", result.stdout.splitlines())
        result = self.play("--start-menu", "--joystick", "--keys",
                           r"\kxP\j129,250.\j129,129.\v\v\v\r\e", ASSETS)
        lines = result.stdout.splitlines()
        self.assertIn("choice: Initialize Mouse/Joystick", lines)
        self.assertIn("item: joystick OFF", lines)

    def test_input_joystick_calibrates(self):
        """--input joystick over a file without J: the stick calibrated at
        startup as the setup's [1] does (177c:029d), for the run, the file
        left as it is; then a push up in Move mode is a step."""
        keys = r"\j10,12,1.\j129,129.\j250,240,1.\j129,129."
        result = self.play("--combat", "pass", "--test-party", 6, "--play", "--set", "4be6=1",
                           "--input", "joystick", "--joystick", "--keys",
                           keys + r"\r\rE\rm\j129,1.\j129,129.", ASSETS, 32)
        lines = result.stdout.splitlines()
        self.assertIn("crt: Push JOYSTICK #1 to the extreme upper left position and press "
                      "BUTTON #1.", lines)
        self.assertEqual([line for line in lines if line.startswith("at: ")], ["at: 7,14,0"])
        saves = self.folder / "saves"
        saves.mkdir()
        result = self.play("--start-menu", "--input", "joystick", "--joystick", "--saves", saves,
                           "--keys", keys + r"\kx" + PRESS, ASSETS)
        lines = result.stdout.splitlines()
        self.assertLess(lines.index("crt: Push JOYSTICK #1 to the extreme lower right position "
                                    "and press BUTTON #1."),
                        lines.index("crt: TANDY 1000 USERS - Make sure NUMLOCK is on."))
        self.assertIn("choice: P", lines)
        self.assertFalse((saves / "KRYNN.CFG").exists())

    def test_the_joystick(self):
        """With KRYNN.CFG's J: the stick's button is Enter, a push down the
        keypad's 2 (1614:00fa); Initialize Mouse/Joystick turns it off (N)
        and on again, calibrating it (4def:638d), J and its thresholds."""
        saves = self.folder / "saves"
        saves.mkdir()
        (saves / "KRYNN.CFG").write_bytes(JOYSTICK_CFG)
        keys = (r"\kx" + PRESS + DOWN * 3 + PRESS + DOWN + PRESS +
                r"\r\j10,12,1.\j129,129.\j250,240,1.\j129,129.\e\e")
        result = self.play("--start-menu", "--joystick", "--saves", saves, "--keys", keys, ASSETS)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith("choice: ")][:2],
                         ["choice: P", "choice: Initialize Mouse/Joystick"])
        self.assertIn("print: Push JOYSTICK #1 to the extreme upper", lines)
        self.assertEqual([line for line in lines if line.startswith("config: ")],
                         ["config: line 3 N", "config: line 3 J"])
        self.assertEqual((saves / "KRYNN.CFG").read_bytes(),
                         b"E\r\nT\r\nJ\r\nC:\\COK\\SAVE\\\r\nF\r\n" +
                         b"10\r\n12\r\n49\r\n51\r\n89\r\n90\r\n129\r\n129\r\n169\r\n166\r\n"
                         b"209\r\n203\r\n250\r\n240\r\n")
        # The mouse turned on there turns the joystick off (4def:5f47).
        (saves / "KRYNN.CFG").write_bytes(JOYSTICK_CFG)
        keys = r"\kx" + PRESS + DOWN * 3 + PRESS + PRESS + r"\e"
        result = self.play("--start-menu", "--joystick", "--mouse", "--saves", saves, "--keys", keys,
                           ASSETS)
        lines = result.stdout.splitlines()
        rows = [line for line in lines if line.startswith("item: ") and "OFF" in line or " ON" in line]
        self.assertEqual(rows, ["item: mouse OFF", "item: joystick ON", "item: mouse ON",
                                "item: joystick OFF"])
        self.assertIn("config: line 3 M", lines)
        # A block run reads the file's thresholds with the stick there: a
        # push up in Move mode is a step (the keypad's 8).
        cfg = self.folder / "joystick.cfg"
        cfg.write_bytes(JOYSTICK_CFG)
        result = self.play("--combat", "pass", "--test-party", 6, "--play", "--set", "4be6=1",
                           "--config", cfg, "--joystick", "--keys",
                           r"\r\rE\rm\j129,1.\j129,129.", ASSETS, 32)
        self.assertEqual([line for line in result.stdout.splitlines() if line.startswith("at: ")],
                         ["at: 7,14,0"])
        # No stick on the machine: J finds none, the keyboard alone.
        (saves / "KRYNN.CFG").write_bytes(JOYSTICK_CFG)
        result = self.play("--start-menu", "--saves", saves, "--keys", r"\kx" + PRESS + "P", ASSETS)
        self.assertIn("choice: P", result.stdout.splitlines())

    def test_the_setups_calibration(self):
        """Without KRYNN.CFG, the setup's [1] JOYSTICK calibrates the stick on
        the text screen (177c:029d) and writes its thresholds; the game
        then plays with it."""
        assets, saves = self.folder / "assets", self.folder / "saves"
        assets.mkdir()
        saves.mkdir()
        for path in ASSETS.iterdir():
            if path.name.upper() != "KRYNN.CFG":
                (assets / path.name).symlink_to(path)
        keys = r"241d:\\x\r\j10,12,1.\j129,129.\j250,240,1.\j129,129.\kx" + PRESS
        result = self.play("--start-menu", "--joystick", "--saves", saves, "--keys", keys, assets)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn("crt: Push JOYSTICK #1 to the extreme upper left position and press BUTTON #1.",
                      lines)
        self.assertIn("choice: P", lines)
        self.assertEqual((saves / "KRYNN.CFG").read_bytes(),
                         b"E\r\nT\r\nJ\r\nD:\\X\\\r\nF\r\n" +
                         b"10\r\n12\r\n49\r\n51\r\n89\r\n90\r\n129\r\n129\r\n169\r\n166\r\n"
                         b"209\r\n203\r\n250\r\n240\r\n")
        # Without a stick: no screens, zeros.
        saves = self.folder / "saves2"
        saves.mkdir()
        result = self.play("--start-menu", "--saves", saves, "--keys", r"241d:\\x\r", assets)
        self.assertNotIn("Push JOYSTICK", result.stdout)
        self.assertTrue((saves / "KRYNN.CFG").read_bytes().endswith(b"F\r\n" + b"0\r\n" * 14))


def wav_data_of(test, keys):
    """The WAV eclplay renders from the assets (with their KRYNN.CFG's E and
    T) for keys."""
    path = test.folder / "ref.wav"
    result = subprocess.run([str(TOOL), "--start-menu", "--wav", str(path), "--sound", "tandy",
                             "--keys", keys, str(ASSETS)], capture_output=True, text=True)
    test.assertEqual(result.returncode, 0, result.stderr)
    return wav_data(path)


def flat(suite):
    for t in suite:
        yield from flat(t) if isinstance(t, unittest.TestSuite) else [t]


def load_tests(loader, tests, pattern):
    """With COK_TEST_SHARD=K/N, only every Nth test from the Kth (1 to N), so
    that make test runs the N parts side by side; their ids, one a line, in
    the file COK_TEST_SHARD_LIST names, for check_shards."""
    shard = os.environ.get("COK_TEST_SHARD")
    if not shard:
        return tests
    k, n = (int(x) for x in shard.split("/"))
    if not 1 <= k <= n:
        raise SystemExit(f"COK_TEST_SHARD {shard}: K must be 1 to N")
    chosen = list(flat(tests))[k - 1::n]
    listing = os.environ.get("COK_TEST_SHARD_LIST")
    if listing:
        pathlib.Path(listing).write_text("".join(f"{t.id()}\n" for t in chosen))
    return unittest.TestSuite(chosen)


def check_shards(lists):
    """The tests the shards ran (LISTS, their COK_TEST_SHARD_LIST files)
    against the whole module's, loaded unsharded: each exactly once. 0 if
    so, else 1 with the ones missing or run twice."""
    import collections
    import sys
    whole = [t.id() for t in flat(unittest.defaultTestLoader.loadTestsFromModule(
        sys.modules[__name__]))]
    ran = collections.Counter(line for f in lists
                              for line in pathlib.Path(f).read_text().splitlines())
    missing = sorted(set(whole) - set(ran))
    twice = sorted(i for i, c in ran.items() if c > 1)
    extra = sorted(set(ran) - set(whole))
    if missing or twice or extra:
        print(f"test_eclplay.py's shards: {len(missing)} tests not run, {len(twice)} run "
              f"twice, {len(extra)} unknown: {(missing + twice + extra)[:5]}")
        return 1
    print(f"test_eclplay.py's {len(lists)} shards ran its {len(whole)} tests, each once")
    return 0


if __name__ == "__main__":
    if os.environ.get("COK_TEST_SHARD_CHECK"):
        raise SystemExit(check_shards(os.environ["COK_TEST_SHARD_CHECK"].split()))
    unittest.main()

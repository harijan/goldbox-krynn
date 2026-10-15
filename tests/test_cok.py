"""Play the SDL front end (build/cok) under SDL's dummy video driver
and check that its window shows exactly what eclplay draws for the same
keys, typed through SDL's key events; that it sleeps while it waits, ends
at once when closed, and shows where the port stops (stdlib only)."""
import os
import pathlib
import shutil
import signal
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COK = ROOT / "build/cok"
ECLPLAY = ROOT / "build/eclplay"
ASSETS = ROOT / "Assets"

ENV = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
           SDL_AUDIODRIVER="dummy")
# The assets' KRYNN.CFG says M, the mouse, which cok always finds (SDL's)
# and eclplay only with --mouse: the tests of the keyboard leave it off.
KEYBOARD = ("--input", "none")

# From the title: Play, then the start menu: create BOB (eclplay's roster
# test), train, Begin; leave the outpost, step onto the caravan and fight
# it with Alt-X (the Gods, with --helm), decline the treasure, help the
# survivors to the inn and camp there; leave again, travel north-east and
# north to Throtl, fight at its gate, walk up the street in 3D, leave an
# encounter, turn and camp in 3D: Rest, then Save as game A (not quitting),
# then Exit.
NEW_GAME = (r"P\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\vS\rE\v\v\v\v\vSA\vS\r\r\r\r\r\r\rH"
            r"\v\v\vSS\v\v\v\v\v\vSLNM\>\r\@xN\r\eN\r\r\r\r\r\rELN98yA\@xN\r\eN"
            r"\^\^\>\^L\vEERESANE")


# The new game up to the hobgoblin's ATTACK/LEAVE/PARLAY menu in Throtl,
# over PIC1 record 12 (frames of 400, 200, 200 and 200 ms), the title
# skipped (\kx).
TO_HOBGOBLIN = r"\kx" + NEW_GAME[:NEW_GAME.index(r"L\vEERE")]
# Up to the Gods' cheat in the caravan fight.
TO_GODS = r"\kx" + NEW_GAME[:NEW_GAME.index(r"\@x") + 3]
# The new game's 3D camp, Rest: three hours to rest.
TO_REST = NEW_GAME[:NEW_GAME.index(r"L\vEERE")] + r"L\vEERHAAAR"


def region(bmp, x, y, w, h):
    """The bytes of a w by h pixel box of a bottom-up 24-bit 320x200 BMP."""
    data = bmp[54:]
    return b"".join(data[(199 - row) * 960 + x * 3:(199 - row) * 960 + (x + w) * 3]
                    for row in range(y, y + h))


def cpu_seconds(pid):
    fields = (pathlib.Path("/proc") / str(pid) / "stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


class FrontEndTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def eclplay(self, keys, *args):
        saves, shots = self.folder / "eclplay-saves", self.folder / "eclplay-shots"
        saves.mkdir()
        run = subprocess.run([str(ECLPLAY), "--start-menu", "--saves", str(saves), "--shots",
                              str(shots), "--keys", keys, *args, str(ASSETS)],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        return run, saves, shots

    def cok(self, keys, *args, name="cok", device=KEYBOARD):
        saves, shots = self.folder / f"{name}-saves", self.folder / f"{name}-shots"
        run = subprocess.run([str(COK), "--assets", str(ASSETS), "--saves", str(saves),
                              "--shots", str(shots), "--no-delay", "--keys", keys, *device,
                              *args],
                             capture_output=True, text=True, env=ENV, timeout=300)
        return run, saves, shots

    def assertSameShots(self, expected, shown):
        names = sorted(p.name for p in expected.glob("*.bmp"))
        self.assertTrue(names)
        self.assertEqual(names, sorted(p.name for p in shown.glob("*.bmp")))
        for name in names:
            self.assertEqual((expected / name).read_bytes(), (shown / name).read_bytes(), name)

    def assertSameDirs(self, a, b):
        self.assertEqual(sorted(p.name for p in a.iterdir()), sorted(p.name for p in b.iterdir()))
        for p in a.iterdir():
            self.assertEqual(p.read_bytes(), (b / p.name).read_bytes(), p.name)

    def test_title_and_demonstration(self):
        """The title (a key ends its wait), its menu timing out with \\t,
        and the demonstration's walks and fight, frame for frame."""
        keys = r"\kx\t"
        expected, _, eshots = self.eclplay(keys, "--seed", "3")
        run, _, shots = self.cok(keys, "--seed", "3", "--aspect", "1:1", "--scale", "2")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout.splitlines()[-1], "(out of keys at the title)")
        self.assertEqual(run.stdout.splitlines()[-1], expected.stdout.splitlines()[-1])
        self.assertSameShots(eshots, shots)

    def test_new_game(self):
        """A new game from the title: creating a character, the overland
        map, two fights with the player's keys, a camp, walking in 3D and
        camping and saving there, frame for frame, in both aspects; and the
        saved game written the same."""
        expected, esaves, eshots = self.eclplay(NEW_GAME, "--seed", "7", "--helm")
        self.assertGreater(len(list(eshots.glob("*.bmp"))), 90)
        for aspect, scale in (("1:1", 2), ("4:3", 3)):
            run, saves, shots = self.cok(NEW_GAME, "--seed", "7", "--helm", "--aspect", aspect,
                                         "--scale", str(scale), "--log", name=aspect + "x")
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(run.stdout, expected.stdout.splitlines()[-1] + "\n")
            # --log prints eclplay's lines.
            self.assertIn("print: The Gods intervene!", run.stderr)
            self.assertIn("at: 7,13,0", run.stderr)
            self.assertIn("print: The party makes camp...", run.stderr)
            width, height = 320 * scale, (240 if aspect == "4:3" else 200) * scale
            self.assertIn(f"window: {width}x{height}", run.stderr)
            self.assertIn(f"shown: {width}x{height} at 0,0 of {width}x{height}", run.stderr)
            self.assertSameShots(eshots, shots)
            self.assertTrue((saves / "SAVGAMA.DAT").exists())
            self.assertSameDirs(esaves, saves)

    def test_resumed_game(self):
        """A game saved in camp on Throtl's square of the overland map,
        loaded from the start menu and begun: only block 16's load vector
        runs before the first command (2fd3:3c28), so the map's menu comes
        and "YOU ARE NEAR THROTL. DO YOU ENTER?" is not asked again; frame
        for frame as eclplay draws it."""
        made = self.folder / "made"
        made.mkdir()
        keys = r"\kx" + NEW_GAME[:NEW_GAME.index("ELN98") + 5] + "neESBN"
        run = subprocess.run([str(ECLPLAY), "--start-menu", "--seed", "7", "--helm", "--saves",
                              str(made), "--keys", keys, str(ASSETS)], capture_output=True,
                             text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("print: . DO YOU ENTER?", run.stdout.splitlines())
        self.assertTrue((made / "SAVGAMB.DAT").exists())
        load = r"\kxP\v\v\rB\v\v\v\v\v\r"
        shutil.copytree(made, self.folder / "eclplay-saves")
        shots = self.folder / "eclplay-shots"
        expected = subprocess.run([str(ECLPLAY), "--start-menu", "--saves",
                                   str(self.folder / "eclplay-saves"), "--shots", str(shots),
                                   "--keys", load, str(ASSETS)], capture_output=True, text=True)
        self.assertEqual(expected.returncode, 0, expected.stderr)
        lines = expected.stdout.splitlines()
        at = lines.index("choice: Begin Adventuring")
        self.assertEqual(lines[at + 1], "menu: Move Encamp")
        shutil.copytree(made, self.folder / "cok-saves")
        run, _, cshots = self.cok(load, "--aspect", "1:1", "--scale", "2", "--log")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("choice: Begin Adventuring\nmenu: Move Encamp\n", run.stderr)
        self.assertNotIn("YOU ARE NEAR", run.stderr)
        self.assertSameShots(shots, cshots)

    def test_cga(self):
        """CGA mode (--display cga, or KRYNN.CFG's first line C): the
        title, its menu, creating BOB and the icon editor's 'Sorry, not in
        CGA', frame for frame as eclplay draws them, in the four CGA
        colours read back from the window; the assets' E is EGA."""
        keys = r"\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\r\e"
        expected, _, eshots = self.eclplay(keys, "--seed", "3", "--display", "cga")
        run, _, shots = self.cok(keys, "--seed", "3", "--aspect", "1:1", "--scale", "2",
                                 "--display", "cga", "--log")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("display: CGA", run.stderr)
        self.assertIn("print: Sorry, not in CGA", run.stderr)
        self.assertEqual(run.stdout.splitlines()[-1], expected.stdout.splitlines()[-1])
        self.assertSameShots(eshots, shots)
        assets = self.folder / "assets"
        assets.mkdir()
        for path in ASSETS.iterdir():
            if path.name.upper() != "KRYNN.CFG":
                (assets / path.name).symlink_to(path)
        (assets / "KRYNN.CFG").write_bytes(b"C\r\nT\r\n")
        run = subprocess.run([str(COK), "--assets", str(assets), "--saves", str(self.folder / "s2"),
                              "--no-delay", "--log", "--keys", r"\kx\t"],
                             capture_output=True, text=True, env=ENV, timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("display: CGA", run.stderr)
        run, _, _ = self.cok(r"\kx\t", "--log", name="ega")
        self.assertIn("display: EGA", run.stderr)

    def test_debug_mode(self):
        """Started with Woof: Ctrl-D, Ctrl-Z and Ctrl-C typed through SDL's
        key events, frame for frame as eclplay draws the debug mode (the
        trace after each opcode), DEBUG.TXT written the same, and Ctrl-C
        quitting to DOS."""
        keys = NEW_GAME[:NEW_GAME.index(r"\@x")] + r"\cD\cZ\@xN\r\eNxxxx\cC"
        expected, esaves, eshots = self.eclplay(keys, "--seed", "7", "--helm", "--woof")
        self.assertIn("debug: 0 , 0 , 0 | GOSUB", expected.stdout)
        run, saves, shots = self.cok(keys, "--seed", "7", "--helm", "--woof", "--aspect", "1:1",
                                     "--scale", "2")
        self.assertEqual((run.returncode, run.stdout), (0, "(quit to DOS)\n"), run.stderr)
        self.assertEqual(expected.stdout.splitlines()[-1], "(quit to DOS)")
        self.assertSameShots(eshots, shots)
        self.assertSameDirs(esaves, saves)
        self.assertEqual((saves / "DEBUG.TXT").read_bytes(), b"who: BAAZ  sp#: 67\r\n" * 4)
    def test_keypad_keys(self):
        """NumLock, the keypad and Alt's keypad numbers pressed as keys
        (\\n, \\dC, \\aN.), which cok turns into codes as the BIOS does
        (cok_keymap_event), give what eclplay reads for the same script:
        the menus moved by the keypad's arrows with NumLock off, and the
        name typed with Alt's numbers (322 as a byte, 0 typing nothing),
        keypad 5 a digit and with NumLock off nothing, in a group typed
        ahead too."""
        keys = (r"P\r\n\d2\d2\d2\n\d2\d2\d2\r\r\v\r\rN\a66.\a079.\d5\a0.\a322.\n\d5\d.\n"
                r"\[\a66.\n\d8\n\]\r")
        expected, _, eshots = self.eclplay(keys, "--seed", "7")
        self.assertIn("input: BO5BB", expected.stdout)
        run, _, shots = self.cok(keys, "--seed", "7", "--log")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("input: BO5BB", run.stderr)
        self.assertEqual(run.stdout.splitlines()[-1], expected.stdout.splitlines()[-1])
        self.assertSameShots(eshots, shots)

    def test_setup(self):
        """Without KRYNN.CFG, startup's setup (11c7:031a) on the text
        screen, 640x200, frame for frame as eclplay draws it, its file
        written in the saves directory as eclplay writes it; then the game
        in the mode it chose (CGA), as eclplay plays it, and Initialize
        Mouse/Joystick writing the file again."""
        assets = self.folder / "assets"
        assets.mkdir()
        for path in ASSETS.iterdir():
            if path.name.upper() != "KRYNN.CFG":
                (assets / path.name).symlink_to(path)
        keys = r"91\#32x2c:\\gam\bmes\r\kxP\v\v\v\r\r\e"
        esaves, eshots = self.folder / "es", self.folder / "eshots"
        esaves.mkdir()
        # The setup's 2, the mouse: the game then has it, eclplay's too.
        expected = subprocess.run([str(ECLPLAY), "--start-menu", "--mouse", "--seed", "3",
                                   "--saves", str(esaves), "--shots", str(eshots), "--keys",
                                   keys, str(assets)], capture_output=True, text=True)
        self.assertEqual(expected.returncode, 0, expected.stderr)
        saves, shots = self.folder / "s", self.folder / "shots"
        run = subprocess.run([str(COK), "--assets", str(assets), "--saves", str(saves),
                              "--shots", str(shots), "--no-delay", "--seed", "3", "--aspect",
                              "1:1", "--scale", "2", "--log", "--keys", keys],
                             capture_output=True, text=True, env=ENV, timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("crt: PATH TO SAVE DATA (DEFAULT - C:\\COK\\SAVE):  c:\\games", run.stderr)
        self.assertIn("display: CGA", run.stderr)
        self.assertSameShots(eshots, shots)
        self.assertEqual(int.from_bytes((shots / "000.bmp").read_bytes()[18:22], "little"), 640)
        self.assertEqual((saves / "KRYNN.CFG").read_bytes(), (esaves / "KRYNN.CFG").read_bytes())
        # Written M by the setup, then N by Initialize Mouse/Joystick.
        self.assertEqual((saves / "KRYNN.CFG").read_bytes()[:19], b"C\r\nR\r\nN\r\nC:\\GAMES\\\r")
        self.assertIn("crt: ALTERNATE INPUT TYPE:  [1] JOYSTICK  [2] MOUSE  [3] NEITHER  2", run.stderr)
        self.assertIn("config: line 3 N", run.stderr)
        # Read on the next start: no setup.
        run = subprocess.run([str(COK), "--assets", str(assets), "--saves", str(saves),
                              "--no-delay", "--log", "--keys", r"\kx"],
                             capture_output=True, text=True, env=ENV, timeout=300)
        self.assertNotIn("crt: SOUND TYPE:  [1] C/MS", run.stderr)
        self.assertIn("crt: TANDY 1000 USERS - Make sure NUMLOCK is on.", run.stderr)
        # Keys that end in the setup end the run there.
        run = subprocess.run([str(COK), "--assets", str(assets), "--saves",
                              str(self.folder / "s2"), "--no-delay", "--keys", "12"],
                             capture_output=True, text=True, env=ENV, timeout=300)
        self.assertEqual((run.returncode, run.stdout), (0, "(out of keys in the setup)\n"))
        self.assertFalse((self.folder / "s2" / "KRYNN.CFG").exists())

    def test_letterbox(self):
        """Full screen on the dummy driver's 1024x768: the screen 1024x640,
        letterboxed, scaled by 3.2, still shows eclplay's frames."""
        keys = r"\kx\t"
        _, _, eshots = self.eclplay(keys, "--seed", "3")
        run, _, shots = self.cok(keys, "--seed", "3", "--aspect", "1:1", "--fullscreen", "--log")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("shown: 1024x640 at 0,64 of 1024x768", run.stderr)
        self.assertSameShots(eshots, shots)

    def test_presented(self):
        """What the window presents, as SDL's dummy driver saves each frame
        it is given (SDL_VIDEO_DUMMY_SAVE_FRAMES): startup's note to Tandy
        users for 2 seconds, then, waiting at the title's menu (--woof),
        the menu eclplay draws, presented once."""
        _, _, eshots = self.eclplay("", "--woof")
        shown = self.presented("--woof")
        self.assertTrue(2 <= len(shown) <= 4, shown)
        # The note: light grey on black, 640x200 squeezed into the 320x200
        # window.
        note = shown[0].read_bytes()[54:]
        self.assertNotEqual(note, shown[-1].read_bytes()[54:])
        self.assertEqual(note[(199 - 100) * 960:(199 - 100) * 960 + 3], bytes(3))
        # Both 320x200, 24 bits, bottom-up, after a 54-byte header.
        self.assertEqual(shown[-1].read_bytes()[54:], (eshots / "000.bmp").read_bytes()[54:])
        # The title's first picture, shown while it waits in the delay hook:
        # TITLE.DAX record 1 (2f55:063c), drawn whole at 0, 0.
        shown = self.presented()
        self.assertTrue(2 <= len(shown) <= 4, shown)
        self.assertGreater(len(set(shown[-1].read_bytes()[54:])), 2)

    def presented(self, *args):
        frames = self.folder / ("frames" + "".join(args))
        frames.mkdir()
        process = subprocess.Popen([str(COK), "--assets", str(ASSETS), "--saves",
                                    str(self.folder / "s"), "--aspect", "1:1", "--scale", "1",
                                    *KEYBOARD, *args], cwd=frames,
                                   env=dict(ENV, SDL_VIDEO_DUMMY_SAVE_FRAMES="1"),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            time.sleep(3.5)  # the note's 2 seconds first
        finally:
            process.send_signal(signal.SIGTERM)
            process.communicate(timeout=5)
        return sorted(frames.glob("SDL_window*.bmp"))

    def test_type_ahead_flushed(self):
        """Keys typed ahead (\\[...\\]) wait in the buffer: the title's menu
        takes P and its flush (67b5:0c28) drops the down arrow, so Enter
        then picks the start menu's first row, in both players."""
        keys = r"\kx\[P\v\]\r"
        expected, _, eshots = self.eclplay(keys)
        self.assertIn("flush: 1 keys typed ahead dropped", expected.stdout)
        self.assertIn("choice: Create New Character", expected.stdout)
        run, _, shots = self.cok(keys, "--aspect", "1:1", "--log")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("flush: 1 keys typed ahead dropped", run.stderr)
        self.assertIn("choice: Create New Character", run.stderr)
        self.assertSameShots(eshots, shots)

    def test_type_ahead_kept(self):
        """Typed ahead into the name's line, which does not flush, every
        key is read, a held key's repeats too: the new game comes out the
        same."""
        # BOOB: the second O comes as the held key's repeat.
        keys = NEW_GAME.replace(r"NBOB\r", r"N\[BOOB\r\]", 1)
        self.assertNotEqual(keys, NEW_GAME)
        expected, esaves, eshots = self.eclplay(keys, "--seed", "7", "--helm")
        self.assertNotIn("flush:", expected.stdout)
        run, saves, shots = self.cok(keys, "--seed", "7", "--helm", "--aspect", "1:1")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertSameShots(eshots, shots)
        self.assertSameDirs(esaves, saves)
        self.assertTrue((saves / "SAVGAMA.DAT").exists())

    def test_key_during_the_title(self):
        """A key pressed in the middle of the title's waits (after 600 of
        the game's waits: the first picture's 500 polls and delays, then
        50 of the second's) ends the title there, as KeyPressed sees it;
        no credits."""
        keys = r"\w600.\[x\]P\v\v\v\v\rY"
        expected, _, eshots = self.eclplay(keys)
        self.assertNotIn("Electronic Arts", expected.stdout)
        self.assertEqual(expected.stdout.splitlines()[-1], "(quit to DOS)")
        run, _, shots = self.cok(keys, "--aspect", "1:1", "--log")
        self.assertEqual((run.returncode, run.stdout), (0, "(quit to DOS)\n"), run.stderr)
        self.assertNotIn("Electronic Arts", run.stderr)
        self.assertSameShots(eshots, shots)

    def test_arrow_during_the_title(self):
        """F10 pressed in the title's wait: KeyPressed sees it, the title
        reads its 0 and flushes the scan code owed (2f55:005f), which the
        title's menu would take as D, the demonstration."""
        keys = r"\w600.\[\#44\]P\v\v\v\v\rY"
        expected, _, eshots = self.eclplay(keys)
        self.assertIn("flush: 1 keys typed ahead dropped", expected.stdout)
        run, _, shots = self.cok(keys, "--aspect", "1:1", "--log")
        self.assertEqual((run.returncode, run.stdout), (0, "(quit to DOS)\n"), run.stderr)
        self.assertIn("flush: 1 keys typed ahead dropped", run.stderr)
        self.assertSameShots(eshots, shots)

    def test_key_during_a_rest(self):
        """A key pressed in the middle of a rest (57e4's loop polls once a
        step) stops it at that step: Stop Resting?, Yes."""
        shots = []
        for wait in (2, 30):
            keys = TO_REST + r"\w%d.\[x\]YE" % wait
            expected, _, eshots = self.eclplay(keys, "--seed", "7", "--helm")
            self.assertIn("menu: Stop Resting? ", expected.stdout)
            run, _, cshots = self.cok(keys, "--seed", "7", "--helm", "--aspect", "1:1",
                                      name="rest%d" % wait)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertSameShots(eshots, cshots)
            shots.append([p.read_bytes() for p in sorted(eshots.glob("*.bmp"))])
            shutil.rmtree(eshots.parent / "eclplay-saves")
            shutil.rmtree(eshots)
        # Later is less time left: the screens from Stop Resting? on differ.
        self.assertEqual(len(shots[0]), len(shots[1]))
        self.assertNotEqual(shots[0], shots[1])

    def start(self, *args, frames=None):
        """cok running in the background, its standard error read line by
        line as it comes, with the time of each."""
        env = dict(ENV)
        if frames is not None:
            frames.mkdir()
            env["SDL_VIDEO_DUMMY_SAVE_FRAMES"] = "1"
        process = subprocess.Popen([str(COK), "--assets", str(ASSETS), "--saves",
                                    str(self.folder / "bg"), *KEYBOARD, *args], cwd=frames,
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True)
        lines = []

        def collect():
            for line in process.stderr:
                lines.append((time.monotonic(), line.rstrip("\n")))

        reader = threading.Thread(target=collect)
        reader.start()

        def stop():
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=5)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
            reader.join()
        self.addCleanup(stop)
        return process, lines, stop

    def wait_for(self, lines, text, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for t, line in list(lines):
                if line == text:
                    return t
            time.sleep(0.05)
        self.fail(f"no {text!r} in {timeout} s")

    def test_timed_menu(self):
        """After a demonstration the title's menu waits 10 seconds from
        when it opens (DS:6e11 1000 hundredths), then plays it again."""
        # Half way, an arrow, which does not start the time again (\p500.:
        # five seconds before it is typed).
        frames = self.folder / "frames"
        _, lines, stop = self.start("--no-delay", "--log", "--keys", r"\kx\t\p500.\>",
                                    "--continue", frames=frames)
        self.wait_for(lines, "choice: Demo", 30)
        menus = []
        end = time.monotonic() + 30
        while time.monotonic() < end:
            menus = [t for t, line in lines if line == "menu: Play Demo"]
            demos = [t for t, line in lines if line == "choice: Demo"]
            if len(demos) >= 2:
                break
            time.sleep(0.05)
        stop()
        self.assertGreaterEqual(len(menus), 2)
        self.assertEqual(len(demos), 2)
        self.assertGreater(demos[1] - menus[1], 9.8)
        self.assertLess(demos[1] - menus[1], 11)
        # The arrow, five seconds in, redrew the menu's selection.
        wall = time.time() - time.monotonic()
        moved = [p for p in frames.glob("SDL_window*.bmp")
                 if menus[1] + 4.5 + wall < p.stat().st_mtime < menus[1] + 6 + wall]
        self.assertEqual(len(moved), 1)

    def test_animation(self):
        """The hobgoblin's menu (HORIZONTAL MENU over PIC1 record 12, which
        replaced the view) animates the picture at cell 3, 3 while it
        waits: its four frames in turn, 1 s round."""
        frames = self.folder / "frames"
        _, lines, stop = self.start("--seed", "7", "--helm", "--no-delay", "--aspect", "1:1",
                                    "--scale", "1", "--log", "--continue",
                                    "--keys", TO_HOBGOBLIN, frames=frames)
        shown = self.wait_for(lines, "menu: ~ATTACK ~LEAVE ~PARLAY", 30)
        time.sleep(3)
        stop()
        late = [p for p in sorted(frames.glob("SDL_window*.bmp"))
                if p.stat().st_mtime > time.time() - (time.monotonic() - shown)]
        boxes = [region(p.read_bytes(), 24, 24, 88, 88) for p in late]
        self.assertGreaterEqual(len(boxes), 10, len(boxes))
        self.assertEqual(len(set(boxes[1:9])), 4)
        self.assertEqual(boxes[1:5], boxes[5:9])
        # Each frame stays its own delay: a round of the four is a second,
        # the first frame's 400 ms the longest.
        times = [p.stat().st_mtime for p in late]
        steps = [b - a for a, b in zip(times[1:], times[2:])]
        self.assertAlmostEqual(sum(steps[:4]), 1.0, delta=0.1)
        self.assertEqual(sorted(round(t, 1) for t in steps[:4]), [0.2, 0.2, 0.2, 0.4])

    def test_never_stale(self):
        """With real delays, what the game drew before a delay is shown
        within a few frames of it (--log's check), through the Gods' long
        pause in the caravan fight."""
        frames = self.folder / "frames"
        _, lines, stop = self.start("--seed", "7", "--helm", "--log", "--aspect", "1:1",
                                    "--scale", "1", "--keys", TO_GODS, frames=frames)
        self.wait_for(lines, "print: The Gods intervene!", 120)
        self.wait_for(lines, "menu: Continue Battle:", 10)
        stop()
        self.assertEqual([line for _, line in lines if line.startswith("stale")], [])
        # And at most about 60 frames a second, however fast the text types.
        times = sorted(p.stat().st_mtime for p in frames.glob("SDL_window*.bmp"))
        self.assertGreater(len(times), 100)
        busiest = max(sum(1 for t in times[i:] if t < start + 1) for i, start in enumerate(times))
        self.assertLessEqual(busiest, 66)

    def test_rest_pace(self):
        """Three hours' rest, 36 steps of five minutes, a hundredth each,
        the time left shown as it goes."""
        frames = self.folder / "frames"
        _, lines, stop = self.start("--seed", "7", "--helm", "--no-delay", "--aspect", "1:1",
                                    "--scale", "1", "--log", "--continue", "--keys", TO_REST,
                                    frames=frames)
        self.wait_for(lines, "menu: Save View Magic Rest Alter Fix Exit", 60)
        time.sleep(0.5)
        stop()
        rests = [t for t, line in lines if line == "menu: Rest Days Hours Mins Add Subtract Exit"]
        done = [t for t, line in lines if line == "menu: Save View Magic Rest Alter Fix Exit"]
        took = done[-1] - rests[-1]
        self.assertGreater(took, 0.33)
        self.assertLess(took, 2)
        # The count shown on the way, a frame a few steps.
        wall = time.time() - time.monotonic()
        during = [p for p in frames.glob("SDL_window*.bmp")
                  if rests[-1] + wall < p.stat().st_mtime < done[-1] + wall]
        self.assertGreaterEqual(len(during), 4)

    def test_close_during_delays(self):
        """Closed while the demonstration types its text, delay after
        delay, cok ends at once."""
        process, lines, stop = self.start("--log", "--keys", r"\kx\t")
        self.wait_for(lines, "choice: Demo", 30)
        time.sleep(2.5)
        start = time.monotonic()
        process.send_signal(signal.SIGTERM)
        process.wait(timeout=5)
        self.assertLess(time.monotonic() - start, 0.3)
        self.assertEqual(process.returncode, 0)

    def test_front_end_keys(self):
        """F11 (\\#85 in a script) makes the window full screen, which is
        shown at once, letterboxed; F12 (\\#86) saves the screen in
        screenshots/, as eclplay saves it."""
        _, _, eshots = self.eclplay("", "--woof")
        frames = self.folder / "frames"
        _, lines, stop = self.start("--woof", "--aspect", "1:1", "--scale", "1", "--log",
                                    "--continue", "--keys", r"\p50.\#85\#86", frames=frames)
        self.wait_for(lines, "shown: 1024x640 at 0,64 of 1024x768", 10)
        time.sleep(0.5)
        stop()
        last = sorted(frames.glob("SDL_window*.bmp"))[-1].read_bytes()
        self.assertEqual(int.from_bytes(last[18:22], "little"), 1024)
        self.assertEqual((frames / "screenshots/cok000.bmp").read_bytes(),
                         (eshots / "000.bmp").read_bytes())

    def test_sigint_in_the_background(self):
        """Started with SIGINT ignored, as a shell starts a job in the
        background, SIGINT still ends it."""
        process = subprocess.Popen([str(COK), "--assets", str(ASSETS), "--saves",
                                    str(self.folder / "s"), "--woof"], env=ENV,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_IGN))
        try:
            time.sleep(1)
            process.send_signal(signal.SIGINT)
            process.communicate(timeout=3)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        self.assertEqual(process.returncode, 0)

    def test_shots_need_two_rows(self):
        run, _, _ = self.cok(r"\kx", "--scale", "1")
        self.assertEqual(run.returncode, 2)
        self.assertIn("--scale 2", run.stderr)

    def test_quit_to_dos(self):
        run, _, _ = self.cok(r"\kxP\v\v\v\v\rY", "--aspect", "1:1")
        self.assertEqual((run.returncode, run.stdout), (0, "(quit to DOS)\n"), run.stderr)

    def test_keys_no_keyboard_has(self):
        """NumLock's scan code is not a key the game can be given."""
        run, _, _ = self.cok(r"\kxP\#45")
        self.assertEqual(run.returncode, 2)
        self.assertIn("no key gives scan code 45", run.stderr)

    def test_stop(self):
        """A missing file stops the port: the reason on stderr and on the
        screen's last rows, in light red, and a status of 1."""
        assets = self.folder / "assets"
        assets.mkdir()
        for p in ASSETS.iterdir():
            if p.name != "TITLE.DAX":
                (assets / p.name).symlink_to(p)
        run = subprocess.run([str(COK), "--assets", str(assets), "--saves",
                              str(self.folder / "s"), "--shots", str(self.folder / "shots"),
                              "--aspect", "1:1", "--keys", "x"],
                             capture_output=True, text=True, env=ENV, timeout=60)
        self.assertEqual(run.returncode, 1)
        self.assertIn("TITLE.DAX", run.stderr)
        bmp = (self.folder / "shots/000.bmp").read_bytes()
        # Bottom-up rows: row 199 (the last, "Press a key") first; the
        # reason's rows above it hold light red (255, 85, 85).
        offset = int.from_bytes(bmp[10:14], "little")
        rows = [bmp[offset + (199 - y) * 960:offset + (200 - y) * 960] for y in range(160, 192)]
        self.assertTrue(any(bytes((85, 85, 255)) in row for row in rows))

    def idle(self, *args):
        """Start cok waiting without keys; return its CPU seconds over two
        seconds of wall time, then close it (SIGTERM is SDL_QUIT, as the
        window's close is) and check it ends at once, writing nothing."""
        saves = self.folder / "idle"
        process = subprocess.Popen([str(COK), "--assets", str(ASSETS), "--saves", str(saves),
                                    "--log", *args], env=ENV, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        try:
            time.sleep(1.5)
            self.assertIsNone(process.poll(), "cok ended early")
            before = cpu_seconds(process.pid)
            time.sleep(2)
            used = cpu_seconds(process.pid) - before
            start = time.monotonic()
            process.send_signal(signal.SIGTERM)
            out, err = process.communicate(timeout=5)
            self.assertLess(time.monotonic() - start, 1)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        self.assertEqual((process.returncode, out), (0, ""), err)
        self.assertEqual(list(saves.iterdir()), [])
        self.log = err
        return used

    def test_waiting_for_a_key_sleeps(self):
        """The title's menu waits for a key (--woof: no title): well under
        a tenth of a core."""
        self.assertLess(self.idle("--woof"), 0.2)
        # 3.5 seconds are not the 30 the menu waits.
        self.assertIn("menu: Play Demo", self.log)
        self.assertNotIn("choice:", self.log)

    def test_waiting_in_a_delay_sleeps(self):
        """The title's pictures wait seconds through the delay hook,
        polling for a key each hundredth."""
        self.assertLess(self.idle(), 0.2)
        # Still on the title's pictures (5 + 10 seconds), not its menu.
        self.assertIn("sound: music", self.log)
        self.assertNotIn("menu: Play Demo", self.log)


# The mouse's clicks through SDL's mouse events, the pointer put where the
# pixel clicked shows in the window: creating BOB, then Modify's list row,
# Select, Pick Character's Select, a score on the sheet, Add and Keep.
CREATE = r"\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\v\rAE"
CLICKS = CREATE + r"\m100,116,1.\m148,196,1.\m124,196,1.\m52,84,1.\m68,196,1.\m172,196,1.\e"
# The joystick (SDL's virtual one): a KRYNN.CFG with it calibrated, its
# button and pushes in the menus, then Initialize Mouse/Joystick turning it
# off and on again, calibrating it.
JOYSTICK_CFG = (b"E\r\nT\r\nJ\r\nC:\\COK\\SAVE\\\r\nF\r\n" +
                b"".join(b"%d\r\n%d\r\n" % (n, n) for n in (1, 40, 80, 120, 160, 200, 250)))
PRESS, DOWN = r"\j129,129,1.\j129,129.", r"\j129,250.\j129,129."
STICK = (r"\kx" + PRESS + DOWN * 3 + PRESS + DOWN + PRESS +
         r"\r\j10,12,1.\j129,129.\j256,240,1.\j129,129.\e\e")


class DeviceTests(unittest.TestCase):
    """The mouse (SDL's, which cok always has: the assets' KRYNN.CFG says M)
    and the joystick, frame for frame as eclplay --mouse and --joystick
    play the same keys."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def run_both(self, keys, eclplay_args, cok_args, name, saves_cfg=None):
        out = {}
        for tool in ("eclplay", "cok"):
            saves, shots = self.folder / f"{name}-{tool}-saves", self.folder / f"{name}-{tool}-shots"
            saves.mkdir()
            if saves_cfg is not None:
                (saves / "KRYNN.CFG").write_bytes(saves_cfg)
            if tool == "eclplay":
                run = subprocess.run([str(ECLPLAY), "--start-menu", "--seed", "5", "--saves",
                                      str(saves), "--shots", str(shots), *eclplay_args, "--keys",
                                      keys, str(ASSETS)], capture_output=True, text=True)
            else:
                run = subprocess.run([str(COK), "--assets", str(ASSETS), "--saves", str(saves),
                                      "--shots", str(shots), "--no-delay", "--seed", "5", "--log",
                                      *cok_args, "--keys", keys],
                                     capture_output=True, text=True, env=ENV, timeout=300)
            self.assertEqual(run.returncode, 0, run.stderr)
            out[tool] = (run, saves, shots)
        (erun, esaves, eshots), (crun, csaves, cshots) = out["eclplay"], out["cok"]
        self.assertEqual(crun.stdout.splitlines()[-1], erun.stdout.splitlines()[-1])
        names = sorted(p.name for p in eshots.glob("*.bmp"))
        self.assertTrue(names)
        self.assertEqual(names, sorted(p.name for p in cshots.glob("*.bmp")))
        for n in names:
            self.assertEqual((eshots / n).read_bytes(), (cshots / n).read_bytes(), n)
        self.assertEqual(sorted(p.name for p in esaves.iterdir()),
                         sorted(p.name for p in csaves.iterdir()))
        for p in esaves.iterdir():
            self.assertEqual(p.read_bytes(), (csaves / p.name).read_bytes(), p.name)
        return crun

    def test_mouse(self):
        """Clicks in the menus and on Modify's sheet, and the cursor drawn
        where the driver draws it, in both aspects, and full screen
        letterboxed at 1024x768 (no whole multiple)."""
        for aspect, scale, more in (("1:1", 2, []), ("4:3", 3, []), ("1:1", 1, ["--fullscreen"])):
            run = self.run_both(CLICKS, ["--mouse"],
                                ["--aspect", aspect, "--scale", str(scale), *more],
                                f"mouse{scale}{len(more)}")
            self.assertIn("input: mouse", run.stderr)
            self.assertIn("choice: Modify Character", run.stderr)

    def test_mouse_in_cga_with_other_keys(self):
        """In CGA mode the cursor two bits a pixel (DS:42f4); clicks mixed
        with the debug keys (Woof's Ctrl-D) and the keypad with NumLock
        toggled (\\n, \\dC) and an Alt number (\\aN.)."""
        keys = r"\d2\n\d2\n\m100,116,1.\cD\a51.\m148,196,1.\e"
        run = self.run_both(keys, ["--mouse", "--woof", "--display", "cga"],
                            ["--woof", "--display", "cga", "--aspect", "1:1", "--scale", "2"],
                            "cga")
        self.assertIn("display: CGA", run.stderr)

    def test_joystick(self):
        """SDL's stick: its button and pushes in the menus, and the
        calibration in Initialize Mouse/Joystick, written to KRYNN.CFG."""
        run = self.run_both(STICK, ["--joystick"], ["--aspect", "1:1", "--scale", "2"], "stick",
                            saves_cfg=JOYSTICK_CFG)
        self.assertIn("input: joystick", run.stderr)
        self.assertIn("config: line 3 J", run.stderr)

    def test_screenshot_shows_the_cursor(self):
        """F12 saves the screen as shown, the mouse's cursor in it."""
        shots = self.folder / "eshots"
        subprocess.run([str(ECLPLAY), "--start-menu", "--mouse", "--shots", str(shots), "--keys",
                        r"\kx", str(ASSETS)], capture_output=True, text=True)
        run = subprocess.run([str(COK), "--assets", str(ASSETS), "--saves", str(self.folder / "s"),
                              "--no-delay", "--screenshots", str(self.folder / "f12"), "--keys",
                              r"\kx\#86"], capture_output=True, text=True, env=ENV, timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual((self.folder / "f12" / "cok000.bmp").read_bytes(),
                         (shots / "001.bmp").read_bytes())

    def test_input_joystick_calibrates(self):
        """--input joystick over the assets' M: calibrated at startup as the
        setup's J does, then the stick's keys, as eclplay plays them."""
        keys = r"\j10,12,1.\j129,129.\j256,240,1.\j129,129.\kx" + PRESS + DOWN + PRESS + r"\e"
        run = self.run_both(keys, ["--joystick", "--input", "joystick"],
                            ["--input", "joystick", "--aspect", "1:1", "--scale", "2"], "calib")
        self.assertIn("input: joystick", run.stderr)
        self.assertIn("crt: Push JOYSTICK #1 to the extreme lower right position and press "
                      "BUTTON #1.", run.stderr)
        self.assertIn("choice: Add Character to Party", run.stderr)

    def test_clicks_at_scale_one(self):
        """At 320x240 (4:3, --scale 1) the clicks land where they do at any
        other size (the window's pixels taken by their centres)."""
        (self.folder / "e").mkdir()
        expected = subprocess.run([str(ECLPLAY), "--start-menu", "--mouse", "--seed", "5",
                                   "--saves", str(self.folder / "e"), "--keys", CLICKS,
                                   str(ASSETS)], capture_output=True, text=True)
        run = subprocess.run([str(COK), "--assets", str(ASSETS), "--saves", str(self.folder / "c"),
                              "--no-delay", "--seed", "5", "--log", "--aspect", "4:3", "--scale",
                              "1", "--keys", CLICKS], capture_output=True, text=True, env=ENV,
                             timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        logged = [line for line in run.stderr.splitlines()
                  if line.split(":")[0] in ("choice", "menu", "list", "item")]
        printed = [line for line in expected.stdout.splitlines()
                   if line.split(":")[0] in ("choice", "menu", "list", "item")]
        self.assertEqual(logged, printed)

    def test_input_option(self):
        """--input picks the device in place of KRYNN.CFG's: none, or a
        joystick SDL does not have."""
        for name, expected in (("none", "input: keyboard"),
                               ("joystick", "input: joystick not found"),
                               ("mouse", "input: mouse")):
            run = subprocess.run([str(COK), "--assets", str(ASSETS), "--saves",
                                  str(self.folder / name), "--no-delay", "--log", "--input", name,
                                  "--keys", r"\kx"], capture_output=True, text=True, env=ENV,
                                 timeout=300)
            self.assertIn(expected, run.stderr)
        run = subprocess.run([str(COK), "--input", "trackball"], capture_output=True, text=True,
                             env=ENV, timeout=300)
        self.assertEqual(run.returncode, 2)


def bursts(samples, rate=44100):
    """The runs of sound in samples, as (start, end) in seconds, gaps of
    silence (|x| < 300) shorter than 20 ms joined."""
    runs, start, last = [], None, None
    for i, x in enumerate(samples):
        if abs(x) >= 300:
            if start is None or i - last > rate // 50:
                if start is not None:
                    runs.append((start / rate, (last + 1) / rate))
                start = i
            last = i
    if start is not None:
        runs.append((start / rate, (last + 1) / rate))
    return runs


class SoundTests(unittest.TestCase):
    """The sound through SDL's disk writer: by the game's clock the samples
    eclplay --wav renders; in real time the drivers timed by the device,
    the music and the sounds going on while the game waits."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)

    def disk(self, *args, delay=None, timeout=300, sleep=None, assets=ASSETS):
        out = self.folder / "disk.raw"
        env = dict(ENV, SDL_AUDIODRIVER="disk", SDL_DISKAUDIOFILE=str(out))
        if delay is not None:
            env["SDL_DISKAUDIODELAY"] = str(delay)
        process = subprocess.Popen([str(COK), "--assets", str(assets), "--saves",
                                    str(self.folder / "saves"), *args], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if sleep is not None:
            time.sleep(sleep)
            process.send_signal(signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        self.assertEqual(process.returncode, 0, stderr)
        return out.read_bytes(), stdout, stderr

    def test_the_game_clock(self):
        """With --no-delay the sound goes by the game's delays and \\p
        pauses: the disk writer gets eclplay's samples for the same keys,
        seed and device (the title's music, a pause at its menu, the
        demonstration's sounds and the title again), after the silence
        the device starts with; the Tandy's unlike the speaker's."""
        keys = r"\p150.\t"
        renders = []
        for device in ("tandy", "speaker"):
            wav = self.folder / f"eclplay-{device}.wav"
            saves = self.folder / f"eclplay-saves-{device}"
            saves.mkdir()
            run = subprocess.run([str(ECLPLAY), "--start-menu", "--saves", str(saves), "--seed", "3",
                                  "--sound", device, "--wav", str(wav), "--keys", keys, str(ASSETS)],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            expected = wav.read_bytes()[44:]
            self.assertGreater(len(expected), 2 * 44100 * 150)
            raw, stdout, stderr = self.disk("--no-delay", "--seed", "3", "--sound", device,
                                            "--keys", keys, delay=0)
            self.assertEqual(stdout, run.stdout.splitlines()[-1] + "\n")
            at = raw.find(expected)
            self.assertGreaterEqual(at, 0, device)
            self.assertEqual(at % 2, 0)
            self.assertFalse(any(raw[:at]))
            self.assertFalse(any(raw[at + len(expected):]))
            renders.append(expected)
        self.assertNotEqual(renders[0], renders[1])

    def test_krynn_cfg(self):
        """The device is KRYNN.CFG's (11c7:031a): a copy of the assets
        whose file says P plays the speaker, as eclplay --sound speaker
        renders it."""
        assets = self.folder / "assets"
        assets.mkdir()
        for path in ASSETS.iterdir():
            if path.name.upper() != "KRYNN.CFG":
                (assets / path.name).symlink_to(path)
        (assets / "KRYNN.CFG").write_bytes(b"E\r\nP\r\nM\r\n")
        keys = r"\kx\t"
        wav = self.folder / "eclplay.wav"
        saves = self.folder / "eclplay-saves"
        saves.mkdir()
        run = subprocess.run([str(ECLPLAY), "--start-menu", "--saves", str(saves), "--seed", "3",
                              "--sound", "speaker", "--wav", str(wav), "--keys", keys, str(ASSETS)],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        expected = wav.read_bytes()[44:]
        raw, _, stderr = self.disk("--no-delay", "--seed", "3", "--log", "--keys", keys, delay=0,
                                   assets=assets)
        self.assertIn("sound: disk, PC", stderr)
        at = raw.find(expected)
        self.assertGreaterEqual(at, 0)
        self.assertFalse(any(raw[:at]) or any(raw[at + len(expected):]))

    def test_music_in_real_time(self):
        """In real time the device's playing times the drivers, as the
        original's timer interrupt did: the title's music plays on
        through the title's wait for a key (its delays and polls)."""
        raw, _, stderr = self.disk("--log", sleep=5.5)  # after the note's 2 s
        self.assertIn("sound: disk, Tandy 1000", stderr)
        samples = memoryview(raw).cast("h")
        first = next(i for i, x in enumerate(samples) if x)
        for k in range(25):
            window = samples[first + 4410 * k:first + 4410 * (k + 1)]
            self.assertGreater(max(window) - min(window), 800, k)

    def test_a_sound_while_waiting_for_a_key(self):
        """In real time a sound plays out while the game waits for a key:
        a step on the overland map (CALL [b203], sound 10) before the
        next, typed two seconds later (\\p200.)."""
        keys = r"\kx" + NEW_GAME[:NEW_GAME.index("ELN98") + 4] + r"\p200.8"
        raw, stdout, stderr = self.disk("--no-delay", "--sound-clock", "real", "--seed", "7",
                                        "--helm", "--keys", keys)
        samples = memoryview(raw).cast("h")
        runs = bursts(samples)
        self.assertIn(len(runs), (1, 2), runs)
        start, end = runs[0]
        self.assertAlmostEqual(end - start, 0.07, delta=0.02)
        later = runs[1][0] if len(runs) == 2 else len(samples) / 44100
        # The gap is in the device's samples, which SDL's disk writer
        # produces at its own thread's pace: on a loaded machine it falls
        # behind the wall clock that times the pause (1.5 s of pause came
        # out as 0.75 s). What matters is that the step's 70 ms sound ended
        # while the game waited and the next step came only after the
        # pause, which any gap of several times the sound's length shows.
        self.assertGreater(later - end, 0.5)

    def test_without_sound(self):
        """--no-sound opens no device; with no device to be had, or no
        MIDI.DAX, the game plays on silently, saying why."""
        keys = r"\kx\t"
        run = subprocess.run([str(COK), "--assets", str(ASSETS), "--saves", str(self.folder / "a"),
                              "--no-delay", "--no-sound", "--keys", keys, "--log"],
                             capture_output=True, text=True, env=ENV, timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertNotIn("sound: dummy", run.stderr)
        self.assertEqual(run.stdout, "(out of keys at the title)\n")
        run = subprocess.run([str(COK), "--assets", str(ASSETS), "--saves", str(self.folder / "b"),
                              "--no-delay", "--keys", keys],
                             capture_output=True, text=True,
                             env=dict(ENV, SDL_AUDIODRIVER="no-such-driver"), timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("playing silently", run.stderr)
        self.assertEqual(run.stdout, "(out of keys at the title)\n")
        assets = self.folder / "assets"
        assets.mkdir()
        for path in ASSETS.iterdir():
            if path.name != "SOUND.DAX":
                (assets / path.name).symlink_to(path)
        run = subprocess.run([str(COK), "--assets", str(assets), "--saves", str(self.folder / "c"),
                              "--no-delay", "--keys", keys],
                             capture_output=True, text=True, env=ENV, timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("sound: no music: ", run.stderr)
        self.assertIn("SOUND.DAX", run.stderr)


if __name__ == "__main__":
    unittest.main()

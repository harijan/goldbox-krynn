"""What batch F's checks share (f_*.py, load_game_screen.py): steps with a
screenshot each, the port's frames at the same points and the fixed port's
only when needed, the saved games the port writes by Throtl, and the
question the port asks after loading one (the known `resume` difference).
Not a check itself."""
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402

cokbox = checklib.cokbox


def marked(steps):
    """STEPS [(name, keys)] as one key string with a \\sNAME. after each."""
    return "".join(rf"{k}\s{name}." for name, k in steps)


class Lazy:
    """FN's value, computed at the first call only."""

    def __init__(self, fn):
        self.fn, self.done, self.value = fn, False, None

    def __call__(self):
        if not self.done:
            self.value, self.done = self.fn(), True
        return self.value


def eclplay(fixed=False, pauses=False, transforms=()):
    """checklib.stopping_eclplay(FIXED) (the port, ending where the keys
    ran out) with TRANSFORMS, and a shot at each delay too when PAUSES
    (checklib.shot_at_delays): its frames are the port's."""
    return checklib.build("eclplay", fixed=fixed, transforms=[
        *transforms, *([("eclplay.c", checklib.shot_at_delays)] if pauses else []),
        ("start.c", checklib.stop_at_input_end)])


def frames(keys, out, **kw):
    """(the port's frames at each \\sNAME. of KEYS, a Lazy of the fixed
    port's); KW as checklib.frames_at's."""
    out = pathlib.Path(out)
    port = checklib.frames_at(keys, out / "port", **kw)
    fixed = Lazy(lambda: checklib.frames_at(keys, out / "fixed", fixed=True, **kw))
    return port, fixed


def judge(verdict, what, shots, port, fixed, work, masks=()):
    """Each of SHOTS {name: the original's Screen} against the port's frame
    of the same name (and the fixed port's, for a difference)."""
    for name, shot in shots.items():
        verdict.screen(f"{what} {name}", shot, port.get(name),
                       fixed=lambda n=name: fixed().get(n), masks=masks, work=work)


# Game A's party on the overland map by Throtl (the caravan met): eclplay
# from ECL block 16, decline the outpost, Move NE then N, decline Throtl,
# Exit, Encamp, Save; BEATEN sets var 0x4c4d bit 2 (the gate's guards
# beaten), for Throtl's street.
THROTL_ARGS = ["--play", "--set", "4bf2=11", "--set", "4bc3=1", "--set", "4bc4=3",
               "--set", "4c2d=1"]


def throtl_save(out, letter, beaten=False):
    """SAVE/'s files and the port's saved game LETTER by Throtl, written by
    the port (cokbox.ECLPLAY) into OUT/saves; returns that directory."""
    checklib.need_saves()
    saves = pathlib.Path(out) / "saves"
    if saves.exists():
        shutil.rmtree(saves)
    saves.mkdir(parents=True)
    for f in checklib.SAVES.iterdir():
        if f.is_file():
            shutil.copyfile(f, saves / f.name)
    args = ["--load", str(checklib.SAVES / "SAVGAMA.DAT"), *THROTL_ARGS,
            *(["--set", "4c4d=2"] if beaten else [])]
    subprocess.run([str(cokbox.ECLPLAY), *args, "--saves", str(saves),
                    "--keys", f"nm98nEES{letter}", str(cokbox.ROOT / "Assets"), "16"],
                   cwd=cokbox.ROOT, check=True, capture_output=True, timeout=120)
    if not (saves / f"SAVGAM{letter}.DAT").exists():
        raise cokbox.BoxError(f"the port wrote no saved game {letter}")
    return saves


def load_letter(letter):
    """Title, Play, Load Saved Game LETTER, Begin Adventuring."""
    return rf"\kxP\v\v\r{letter}\v\v\v\v\v\r"


def resumed(verdict, what, original, keys, out, **kw):
    """A game saved on the overland map by Throtl, loaded (KEYS): the
    original shows the map's Move Encamp; the port (before the fixes) runs
    the block's location vector and asks `YOU ARE NEAR THROTL. DO YOU
    ENTER?` again, the known `resume` difference when the fixed port's
    screen is the original's. Judges ORIGINAL against the port's screen
    after KEYS; returns the keys the port then needs to be where the
    original is: "" when it agrees, "n" (declining the question) when not."""
    out = pathlib.Path(out)
    port = checklib.frame_after(keys, out / "port", **kw)
    count = verdict.screen(what, original, port, work=out, fixed=lambda: checklib.frame_after(
        keys, out / "fixed", fixed=True, **kw))
    if count == 0:
        return ""
    print(f"  {what}: the port is given n (declining its question) to go on from the "
          "original's screen", flush=True)
    return "n"

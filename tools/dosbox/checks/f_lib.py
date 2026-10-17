"""What batch F's checks share (f_*.py, load_game_screen.py): steps with a
screenshot each judged against the port's frames at the same points, an
eclplay built with instrumentation, and the saved games the port writes by
Throtl. Not a check itself."""
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


def eclplay(pauses=False, transforms=()):
    """checklib.stopping_eclplay() (the port, ending where the keys ran
    out, saying which shot is each key read's) with TRANSFORMS, and a shot
    at each delay too when PAUSES (checklib.shot_at_delays): its frames are
    the port's."""
    return checklib.build("eclplay", transforms=[
        *transforms, *([("eclplay.c", checklib.shot_at_delays)] if pauses else []),
        ("start.c", checklib.stop_at_input_end), ("eclplay.c", checklib.print_read_shot)])


def judge(verdict, what, shots, port, work, masks=()):
    """Each of SHOTS {name: the original's Screen} against the port's frame
    of the same name."""
    for name, shot in shots.items():
        verdict.screen(f"{what} {name}", shot, port.get(name), masks=masks, work=work)


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
    subprocess.run([*cokbox.BELOW_BOXES, str(cokbox.ECLPLAY), *args, "--saves", str(saves),
                    "--keys", f"nm98nEES{letter}", str(cokbox.ROOT / "COKAssets"), "16"],
                   cwd=cokbox.ROOT, check=True, capture_output=True, timeout=120)
    if not (saves / f"SAVGAM{letter}.DAT").exists():
        raise cokbox.BoxError(f"the port wrote no saved game {letter}")
    return saves


def load_letter(letter):
    """Title, Play, Load Saved Game LETTER, Begin Adventuring."""
    return rf"\kxP\v\v\r{letter}\v\v\v\v\v\r"

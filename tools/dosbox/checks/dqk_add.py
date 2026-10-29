#!/usr/bin/env python3
"""The Dark Queen of Krynn's Add Character (dqk 7368:08dc, 604b:2021: the
character read, its stats 177e:0be2 and its levels 8bcc:07f1 recomputed)
in the original against eclplay --game dark-queen --start-menu (DARK-
QUEEN.md's step 2: dqk-create; docs/checks.md, "Checks against the
original": dqk_add), compared in RGB with the game's own mouse cursor (the
port's with --mouse).

From the camp with no party: Add, Tdqk (shipped saved characters), then
Dkk (characters of Death Knights' saved game A copied as roster files into
DKK\\SAVE: 604b:0b82), then Save Current Game as J. Two runs: Roland of
Dust and Walker Keys with Celeste, Flavius, Lillith and Neacail; and
Balthir Nomoon (a ranger), Cinder, Roark, Roland (a knight of the Sword,
his file's order patched) with Celeste and Lillith made a paladin of the
twelfth level (the roster file patched), whose levels give spells to
know. The files are patched on disk for both, no memory. Compared: every
screen, and the saved game written (from its party's size: the characters as
the original holds them once added, but the heap's far pointers). About
four minutes.

    python3 tools/dosbox/checks/dqk_add.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402
from dqk_train import CURSOR, GAME, SEED, characters, pointer_mask  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
DKK = ROOT / "DKKAssets" / "SAVE"

# Each run: the roster's characters (name: Death Knights' CHRDATAA<n>), the
# keys (what, typed), and the patches.
RUNS = {
    "a": dict(
        roster={"CELESTE": 6, "FLAVIUS": 4, "LILLITH": 2, "NEACAIL": 5}, patch=lambda s, r: None,
        steps=[("the camp", "p"), ("Add", "a"), ("Tdqk", "t"), ("down 1", "v"), ("down 2", "v"),
               ("down 3", "v"), ("Add Roland", "a"), ("down 4", "v"), ("down 5", "v"),
               ("Add Walker", "a"), ("Exit", "e"), ("Add again", "a"), ("Dkk", "d"),
               ("Add Celeste", "a"), ("down to Flavius", "v"), ("Add Flavius", "a"),
               ("down to Lillith", "v"), ("Add Lillith", "a"), ("down to Neacail", "v"),
               ("Add Neacail", "a"), ("Save Current Game", "s"), ("save J", "j")]),
    "b": dict(
        roster={"CELESTE": 6, "LILLITH": 2}, patch="knights",
        steps=[("the camp", "p"), ("Add", "a"), ("Tdqk", "t"), ("Add Balthir", "a"),
               ("down 1", "v"), ("Add Cinder", "a"), ("down 2", "v"), ("Add Roark", "a"),
               ("down 3", "v"), ("Add Roland", "a"), ("Exit", "e"), ("Add again", "a"),
               ("Dkk", "d"), ("Add Celeste", "a"), ("down to Lillith", "v"),
               ("Add Lillith", "a"), ("Save Current Game", "s"), ("save J", "j")]),
}


def roster_files(out, roster):
    """Death Knights' saved characters CHRDATAA<n>.GAM as roster files
    (<NAME>.PC with its .WIZ, .JNK and .EFF)."""
    out.mkdir(parents=True, exist_ok=True)
    for name, n in roster.items():
        for extension, source in (("PC", "GAM"), ("WIZ", "WIZ"), ("JNK", "JNK"), ("EFF", "EFF")):
            path = DKK / f"CHRDAT{'A'}{n}.{source}"
            if path.exists():
                shutil.copyfile(path, out / f"{name}.{extension}")


def patch_files(saves, roster):
    """The files of run b patched: Roland a knight of the Sword (+0x19a), and
    Lillith's paladin level 12 (Death Knights' +0x48)."""
    path = saves / "ROLANDOF.QCH"
    data = bytearray(path.read_bytes())
    data[0x19A] = 2
    path.write_bytes(data)
    path = roster / "LILLITH.PC"
    data = bytearray(path.read_bytes())
    data[0x48] = 12
    path.write_bytes(data)


class DarkBox(cokbox.Box):
    """The box with Death Knights' roster files in C:\\DKK\\SAVE (DQK.CFG's
    path), there before DOSBox-X reads its drive."""
    roster = None

    def prepare(self):
        super().prepare()
        shutil.copytree(self.roster, self.cdrive / "DKK" / "SAVE", dirs_exist_ok=True)


def play(work, name, run, verdict, verbose):
    steps = run["steps"]
    roster, saves = work / f"roster-{name}", work / f"saves-{name}"
    roster_files(roster, run["roster"])
    shutil.copytree(dqk_lib.ASSETS / "SAVE", saves, dirs_exist_ok=True)
    if run["patch"] == "knights":
        patch_files(saves, roster)
    own = work / f"port-saves-{name}"
    shutil.copytree(saves, own, dirs_exist_ok=True)
    keys = "\\k\\e" + "".join("\\v" if k == "v" else k for _, k in steps)
    shots = work / f"port-shots-{name}"
    if shots.exists():
        shutil.rmtree(shots)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--mouse", "--seed", str(SEED),
                             "--saves", str(own), "--import", str(roster), "--shots", str(shots),
                             "--start-menu", "--keys", keys, str(dqk_lib.ASSETS)],
                            capture_output=True, text=True, cwd=ROOT, timeout=300)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    port = [cokbox.RGBScreen.load(p) for p in
            sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))]
    DarkBox.roster = roster
    original = work / f"original-{name}"
    original.mkdir(exist_ok=True)
    with DarkBox(workdir=work / f"box-{name}", game="dark-queen", verbose=verbose, seed=SEED,
                 saves=[saves], timeout=900) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        box.keys("\\e")
        box.settled(tolerance=64)
        freeze = (box.ds - 0x422E + 0x261B) * 16 + 0x05BE
        if box.read(freeze, 1) != b"\x55":
            raise cokbox.BoxError("261b:05be is not where it was expected")
        box.write(freeze, b"\xcb")
        for i, (what, key) in enumerate(steps):
            box.keys("\\v" if key == "v" else key)
            shot = box.settled(tolerance=64)
            shot.save_png(original / f"{i:02d}-{checklib.slug(what)}.png")
            verdict.screen(f"{name}: {what}", shot, port[i + 2] if len(port) > i + 2 else None,
                           masks=[CURSOR], work=work)
    theirs = work / f"box-{name}" / "c" / "DQK" / "SAVE" / "SAVGAMJ.QSV"
    ours = own / "SAVGAMJ.QSV"
    a, b = (p.read_bytes() if p.exists() else b"" for p in (theirs, ours))
    mask = pointer_mask(b) if b else set()
    # From the party's size: the variables and the square before it are the
    # new game's, which Begin Adventuring has not entered.
    differing = [i for i in range(0x40D, min(len(a), len(b))) if i not in mask and a[i] != b[i]]
    verdict.check(len(a) == len(b) == 7965 and not differing and len(characters(b)) == 6,
                  f"{name}: the saved game written (SAVGAMJ.QSV)",
                  f"{len(a)} and {len(b)} bytes, differing at {[hex(i) for i in differing[:16]]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_add")
    verdict = checklib.Verdict()
    for name, run in RUNS.items():
        play(work, name, run, verdict, opts.verbose)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

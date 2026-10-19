#!/usr/bin/env python3
"""Death Knights of Krynn's front end in the original against eclplay
--game death-knights --start-menu (DEVELOPERS.md, "Death Knights' front
end"): the title's four pictures (35c6:051c), the credits (35c6:026e) and
the title's menu; the start menu (49cb:010b) with and without a party;
Load Saved Game from Death Knights' games and from Champions' (DKK.CFG's
fifth line, C:\\COK\\SAVE; the port's --import), Save Current Game, Begin
Adventuring, the game's first texts and camp; Remove Character to the
roster, Add Character from it, Drop Character with its questions, and the
game ended by the last Drop. Each flow types the same keys to both; at
each step the original's screen, once it waits for input, against the
port's at the key read the same keys leave it in (its --shots), whole
screens. The games saved (Save Current Game C) against each other: the
saved game but the bytes the original's stack leaves (after each name, the
slots past the party), the vault (VAULTC.DAT) byte for byte, the
characters but their far pointers. Champions' saved games A and B have
their bank (Champions' 0x4c33) set to 0x1234, which the new vault holds a
hundred times over in steel. Not part of run_all.py's set (Champions'
checks). About four minutes.

    python3 tools/dosbox/checks/dkk_start.py [--workdir DIR]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

ROOT = cokbox.ROOT
ASSETS = ROOT / "DKKAssets"
CHAMPIONS = ROOT / "SAVE"

# The flows: (name, keys typed after the previous step's). The first two
# steps are the title's: a key ending the pictures, one ending the credits.
TITLE = [("credits", "x"), ("menu", "x"), ("start", "\\r")]
FLOWS = {
    "death": TITLE + [
        ("start-load", "\\v\\v"), ("from", "\\r"), ("which", "\\r"), ("loaded", "\\r"),
        ("save-which", "\\v\\v\\v\\v\\r"), ("saved", "\\>\\>\\r"), ("begun", "\\v\\r"),
        ("text-2", "\\r"), ("rest", "\\r"), ("camp", "\\r")],
    # The selection carries from "from where" (Champions) to the letters:
    # B is loaded.
    "champions": TITLE + [
        ("from", "\\v\\v\\r"), ("from-champions", "\\>"), ("which", "\\r"), ("loaded", "\\r"),
        ("save-which", "\\v\\v\\v\\v\\r"), ("saved", "\\>\\>\\r"), ("begun", "\\v\\r")],
    "roster": TITLE + [
        ("loaded", "\\v\\v\\r\\r\\r"), ("remove-pick", "\\v\\v\\v\\r"), ("removed", "\\r"),
        ("add-from", "\\^\\r"), ("add-list", "\\r"), ("added", "\\r"), ("add-done", "\\e"),
        ("drop-pick", "\\^\\^\\^\\r"), ("drop-ask", "\\r"), ("drop-sure", "Y"),
        ("dropped", "Y")] + [(f"dropped-{n}", "\\r\\rYY") for n in range(2, 7)] +
        [("empty-load", "\\v\\r")],
}
# Champions' bank, the word at 0x267 of its saved games (variable 0x4c33).
BANK = 0x1234


def champions(work):
    """Champions' saved games, their bank set, in WORK/champions."""
    folder = work / "champions"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for f in CHAMPIONS.iterdir():
        if f.is_file():
            data = bytearray(f.read_bytes())
            if f.name.startswith("SAVGAM") and f.suffix == ".DAT":
                data[0x267:0x269] = BANK.to_bytes(2, "little")
            (folder / f.name).write_bytes(data)
    return folder


def port(work, flow, steps, i, imports):
    """The port's screen at step i of FLOW: its shot at the key read the
    keys leave it waiting in (the last), the credits' at the read that ends
    them; and its saves folder."""
    out = work / f"port-{flow}-{i:02d}"
    if out.exists():
        shutil.rmtree(out)
    saves, shots = out / "saves", out / "shots"
    shutil.copytree(ASSETS / "SAVE", saves)
    shots.mkdir()
    cfg = out / "DKK.CFG"
    cfg.write_bytes((ASSETS / "DKK.CFG").read_bytes())
    keys = "\\kx\\kx" + "".join(k for _, k in steps[2:i + 1])
    name = steps[i][0]
    if name == "credits":
        keys = "\\kx\\kx"
    elif name == "menu":
        keys = "\\kx\\kx"
    result = subprocess.run(
        [str(cokbox.ECLPLAY), "--game", "death-knights", "--start-menu", "--display", "ega",
         "--config", str(cfg), "--saves", str(saves), "--import", str(imports), "--shots",
         str(shots), "--keys", keys, str(ASSETS)],
        capture_output=True, text=True, cwd=ROOT, timeout=300)
    (out / "log.txt").write_text(keys + "\n" + result.stdout + result.stderr)
    found = sorted(shots.glob("*.bmp"), key=lambda p: int(p.stem))
    return cokbox.Screen.load(found[1] if name == "credits" else found[-1]), saves


def stable(box, first):
    """Screenshots until two in a row agree (a picture is drawn a line at a
    time)."""
    last = first
    for _ in range(40):
        box.keys("\\p20.")
        shot = box.screenshot()
        if shot.diff(last)[0] == 0:
            return shot
        last = shot
    return last


def masked_character(name, data):
    """A character's file with its far pointers 0 (the original writes them
    as they are in memory, the port 0)."""
    data = bytearray(data)
    ext = name.rsplit(".", 1)[-1].upper()
    if ext in ("GAM", "PC"):
        for at, n in ((0x3A, 4), (0x7D, 0xB9 - 0x7D), (0xB9, 4), (0xBD, 4), (0xC2, 4)):
            data[at:at + n] = bytes(n)
    elif ext == "JNK":
        for k in range(0, len(data) - 62, 63):
            data[k + 0x2A:k + 0x2E] = bytes(4)
    elif ext == "EFF":
        for k in range(0, len(data) - 8, 9):
            data[k + 5:k + 9] = bytes(4)
    return bytes(data)


def same_saved(a, b):
    """Death Knights' saved games A and B the same but for the bytes the
    original's stack leaves: after each name, and the slots past the
    party."""
    if len(a) != 2913 or len(b) != 2913:
        return False
    for k in range(len(a)):
        if k >= 0xA19:
            slot, at = divmod(k - 0xA19, 41)
            if slot >= a[0xA18] or (at > 0 and at > a[0xA19 + 41 * slot]):
                continue
        if a[k] != b[k]:
            return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(ROOT / "build/dosbox-checks/dkk_start"))
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir)
    work.mkdir(parents=True, exist_ok=True)
    results = []
    imports = champions(work)

    def compare(name, shown, want):
        count, area = shown.diff(want)
        if count:
            shown.save_png(work / f"diff-{name.replace('/', '-')}.png", highlight=want)
        results.append((name, count))
        print(f"{name}: {count} pixels differ" + (f" in {area}" if count else ""), flush=True)

    files = {f.name: f.read_bytes() for f in imports.iterdir()}
    pre = ["mkdir C:\\COK", "mkdir C:\\COK\\SAVE", "copy C:\\DKK\\SAVGAM*.DAT C:\\COK\\SAVE",
           "copy C:\\DKK\\CHRDAT*.* C:\\COK\\SAVE", "del C:\\DKK\\SAVGAM*.DAT",
           "del C:\\DKK\\CHRDAT*.*"]
    for flow, steps in FLOWS.items():
        box_dir = work / f"box-{flow}"
        saved = None
        with cokbox.Box(workdir=box_dir, game="death-knights", machine="ega", files=files,
                        pre_commands=pre, saves=[ASSETS / "SAVE"], verbose=opts.verbose,
                        timeout=900, keep=True) as box:
            # The title's pictures: each new screen, once drawn (the keys
            # wait for the last, so that the first key ends a picture's
            # wait, not startup's). Compared in the first flow.
            shown, title = None, []
            for _ in range(400):
                box.keys("\\p25.")
                shot = box.screenshot()
                if (shown is None and sum(1 for p in shot.pixels if p) > 10000) or \
                        (shown is not None and shot.diff(shown)[0] > 1000):
                    shot = stable(box, shot)
                    title.append(shot)
                    if len(title) == 4:
                        break
                shown = shot if shown is not None or title else None
            draws = [[(0, 0)], [(1, 0)], [(1, 0), (2, 11)], [(1, 0), (2, 11), (3, 11)]]
            for k, shot in enumerate(title if flow == "death" else []):
                bmp = work / f"port-title-{k + 1}.bmp"
                args = []
                for record, row in draws[k]:
                    args += ["draw", str(ASSETS / "TITLE.DAX"), str(record), "0", "0",
                             str(row)]
                subprocess.run([str(ROOT / "build/daxcompose"), "--game", "death-knights",
                                str(bmp), *args], check=True, cwd=ROOT, timeout=60)
                compare(f"title-{k + 1}", shot, cokbox.Screen.load(bmp))
            if len(title) < 4:
                results.append(("title pictures seen", 4 - len(title)))
            for i, (name, keys) in enumerate(steps):
                box.keys(keys)
                box.wait_input(settle=1.5)
                shown = box.screenshot(work / f"dosbox-{flow}-{i:02d}-{name}.png")
                mine, saves = port(work, flow, steps, i, imports)
                compare(f"{flow}/{name}", shown, mine)
                if name == "saved":
                    saved = saves
        if saved is None:
            continue
        original = box_dir / "c/DKK/SAVE"
        ok = same_saved((original / "SAVGAMC.PTY").read_bytes(),
                        (saved / "SAVGAMC.PTY").read_bytes())
        ok &= (original / "VAULTC.DAT").read_bytes() == (saved / "VAULTC.DAT").read_bytes()
        names = sorted(p.name for p in original.iterdir() if p.name.startswith("CHRDATC"))
        ok &= names == sorted(p.name for p in saved.iterdir() if p.name.startswith("CHRDATC"))
        for n in names:
            ok &= masked_character(n, (original / n).read_bytes()) == \
                masked_character(n, (saved / n).read_bytes())
        vault = (original / "VAULTC.DAT").read_bytes()
        print(f"{flow}: saved game C, its vault ({vault.hex()}) and {len(names)} character "
              f"files {'the same' if ok else 'DIFFER'}", flush=True)
        results.append((f"{flow}/files", 0 if ok else 1))
    bad = [name for name, count in results if count]
    print("VERDICT: AGREES" if not bad else f"VERDICT: DIFFERS new ({', '.join(bad)})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Death Knights of Krynn in the original against eclplay --game
death-knights (docs/checks.md, "Checks against the original": dkk_views):
the title's first two pictures; the first text of the game, in the saved
game DKKAssets/SAVE/SAVGAMA.PTY resumes (ECL1 block 16, over the overland
map), the whole screen: the big picture's frame (110f:02ab), the map, the
text and the menu; and the 3D view in three places of ECL1 with two wall
sets, out of doors (the ground by day, SKYGRND) and in (the wall set's
BACK), facing each way the party turns there, with the adventure screen's
frame (110f:0162) around it: all of it but the party list and the status
line (cells 17-38 of rows 1-15: the port has no party yet), the text
window and the menu's row; and two of the places in CGA mode (6d21:0177's
backdrop, without Champions' line row).

When this check was written Death Knights was read headless only
(DEATH-KNIGHTS.md's step 1): the port could not load its saved games, and
the original's game must have a party (dkk_start.py checks the loads). So
each place is a copy of SAVGAMA.PTY with the block, the ECL file, the
square and facing, the wall set and the area's variables written in (its
byte 0 and 0x7f12 at 0x713 the ECL file, the area's 0x4b00-0x4eff from
byte 1, the square and facing from 0xa01, the wall sets' table from 0xa08);
the original resumes there (its load vector EXITs for a resumed block, and
the saved map and wall set load), and the party turns in Move mode. The
port runs the block's load vector from its LOAD FILES, the code patched
(--set on the code) to skip the GOSUB after LOAD PIECES, put the party on
the same square and facing, and EXIT after the view is shown (CALL
[2dcb]). In run_all.py's Death Knights set (make dkk-checks). About a
minute.

    python3 tools/dosbox/checks/dkk_views.py [--workdir DIR]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import dkk_lib as K  # noqa: E402

ROOT = cokbox.ROOT
ASSETS = ROOT / "DKKAssets"
GAME = ("--game", "death-knights")
# The original's state, dkk DS offsets: the square and facing (c073-c075,
# 3c1e:0d85's 0xc04b-0xc04d), the ECL file (98a2, 15fe:0a9f), the far
# pointer to the area's variables 0x4b00-0x4eff (8c2d, bytes).
DS_X, DS_FILE, DS_AREA = 0xC073, 0x98A2, 0x8C2D
# Regions compared: the whole screen for the text; for the views the
# adventure screen but the party list and status line (cells 17-38 of rows
# 1-15), the text window (rows 17-22) and the menu's row 24: row 0 with the
# moons, cells 0-16 of rows 1-15 (the view's box, the view, the divider),
# column 39 there, row 16, the sides of rows 17-22, row 23.
SCREEN = (0, 0, 320, 200)
FRAME = [(0, 0, 320, 8), (0, 8, 136, 120), (312, 8, 8, 120), (0, 128, 320, 8),
         (0, 136, 8, 48), (312, 136, 8, 48), (0, 184, 320, 8)]
# The moons' phases (bytes 0x1f9-0x1fb of the area, 0x4cf9-0x4cfb), which
# the port takes from the original's.
MOONS = 0x1F9
# The places: ECL1's block, its load vector from LOAD FILES (start) and the
# code bytes patched there (the GOSUB after LOAD PIECES made a CALL that
# does nothing, the instruction after CALL [2dcb] an EXIT), the immediate
# bytes of its SAVE to 0xc04b, 0xc04c and 0xc04d; the square and the
# facings (0-3) seen; the wall set (its id) and map (GEO1 record) the block
# loads; the area's variables its vector sets (sky colours, the wall sets'
# path); the moons' phases written in the saved game. The port draws the
# frame before its --set (eclplay: as the run starts), and block 36 draws it
# again (CLEAR BOX) but block 32 does not: there the phases are the port's
# own at the start, 0.
PLACES = [
    dict(name="gate", block=36, start=0x805A, patch={0x8068: 0x2D, 0x8089: 0},
         at=(0x8074, 0x807A, 0x8080), square=(0, 8), facings=(1, 2), wall=1, geo=36,
         area={0xE7: 1, 0xE8: 1, 0xE9: 1, 0xFD: 4, 0xFE: 12}, moons=(1, 3, 0)),
    dict(name="hall", block=36, start=0x805A, patch={0x8068: 0x2D, 0x8089: 0},
         at=(0x8074, 0x807A, 0x8080), square=(5, 4), facings=(0, 1), wall=1, geo=36,
         area={0xE7: 1, 0xE8: 1, 0xE9: 1, 0xFD: 4, 0xFE: 12}, moons=(1, 3, 0)),
    dict(name="city", block=32, start=0x8040, patch={0x804E: 0x2D, 0x806E: 0},
         at=(0x805A, 0x8060, 0x8066), square=(6, 15), facings=(0, 1, 2), wall=2, geo=32,
         area={0xE7: 1, 0xE8: 1, 0xE9: 1, 0xFB: 0, 0xFD: 4, 0xFE: 10}, moons=(0, 0, 0)),
    dict(name="city-in", block=32, start=0x8040, patch={0x804E: 0x2D, 0x806E: 0},
         at=(0x805A, 0x8060, 0x8066), square=(14, 1), facings=(0, 1), wall=2, geo=32,
         area={0xE7: 1, 0xE8: 1, 0xE9: 1, 0xFB: 0, 0xFD: 4, 0xFE: 10}, moons=(0, 0, 0)),
]


def region(screen, box):
    x, y, w, h = box
    return cokbox.Screen(w, h, screen.region(x, y, w, h))


def state(box):
    """The block, ECL file, square, facing and area variables shown."""
    x, y, facing = box.ds_read(DS_X, 3)
    off, seg = cokbox.struct.unpack("<HH", box.ds_read(DS_AREA, 4))
    area = box.read(seg * 16 + off, 0x400)
    return dict(x=x, y=y, dir=facing, file=box.ds_byte(DS_FILE), area=area)


def saved_game(work, place, facing):
    """SAVGAMA.PTY's copy resuming at PLACE facing FACING, with the other
    saved files, in WORK/saves."""
    saves = work / "saves"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(ASSETS / "SAVE", saves)
    path = saves / "SAVGAMA.PTY"
    data = bytearray(path.read_bytes())
    data[0] = 1                                   # the ECL file
    data[0x401 + 0x312] = 1                       # 0x7f12: ECL1
    data[1 + 0xF2] = place["block"]
    data[1 + 0xE6] = 1                            # a 3D area
    data[1 + 0xC5] = place["geo"]
    for k, v in place["area"].items():
        data[1 + k] = v
    data[1 + MOONS:1 + MOONS + 3] = bytes(place["moons"])
    data[0xA01:0xA04] = bytes([*place["square"], facing * 2])
    data[0xA08:0xA0C] = bytes([place["wall"], 0, 0, 0])  # slot 0: the wall set
    path.write_bytes(data)
    return saves


def moons(area):
    """--set arguments giving the port the moons' phases of AREA."""
    return [a for i in range(3) for a in ("--set", f"{0x4b00 + MOONS + i:x}={area[MOONS + i]:x}")]


def port(work, name, place, facing, area, display="ega"):
    """eclplay's screen at PLACE facing FACING, as a Screen."""
    bmp = work / f"port-{name}.bmp"
    sets = {**place["patch"], place["at"][0]: place["square"][0],
            place["at"][1]: place["square"][1], place["at"][2]: facing}
    args = ["--display", display, "--file", 1, "--start", f"{place['start']:x}", "--set",
            "4be6=1", *moons(area)]
    for k, v in sets.items():
        args += ["--set", f"{k:x}={v:x}"]
    for k, v in place["area"].items():
        args += ["--set", f"4b{k:02x}={v:x}"]
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--screen", str(bmp), *map(str, args),
                             str(ASSETS), str(place["block"])],
                            capture_output=True, text=True, cwd=ROOT, timeout=120)
    (work / f"port-{name}.txt").write_text(result.stdout + result.stderr)
    return cokbox.Screen.load(bmp)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(ROOT / "build/dosbox-checks/dkk_views"))
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir)
    work.mkdir(parents=True, exist_ok=True)
    results = []

    def compare(name, shown, want, boxes):
        count, where = 0, []
        for b in boxes:
            n, area = region(shown, b).diff(region(want, b))
            if n:
                region(shown, b).save_png(work / f"diff-{name}-{b[1]}.png",
                                          highlight=region(want, b))
                where.append(area)
            count += n
        results.append((name, count))
        print(f"{name}: {count} pixels differ" + (f" in {where}" if where else ""), flush=True)

    def box_for(saves, machine="ega"):
        return cokbox.Box(workdir=work / "box", game="death-knights", machine=machine,
                          saves=[saves], verbose=opts.verbose, timeout=900)

    # The title and the first text.
    with box_for(ASSETS / "SAVE") as box:
        shown = None
        for k in (1, 2, 3, 4):
            # Each of the title's pictures stays some seconds: the first
            # screen not mostly black (after "Loading"), then the next one
            # that differs from it. The first two compared; the fourth
            # waited for, as K.title does (keys typed sooner can be lost).
            last = shown
            for _ in range(60):
                box.keys("\\p50.")
                shown = box.screenshot()
                if (last is None and sum(1 for p in shown.pixels if p) > 10000) or \
                        (last is not None and shown.diff(last)[0] > 1000):
                    break
            box.keys("\\p50.")
            if k > 2:
                shown = box.screenshot()
                continue
            bmp = work / f"port-title-{k}.bmp"
            subprocess.run([str(ROOT / "build/daxcompose"), *GAME, str(bmp), "draw",
                            str(ASSETS / "TITLE.DAX"), str(k - 1), "0", "0", "0"],
                           check=True, cwd=ROOT, timeout=60)
            shown = box.screenshot(work / f"dosbox-title-{k}.png")
            compare(f"title-{k}", shown, cokbox.Screen.load(bmp), [(0, 0, 320, 200)])
        K.type_steps(box, K.LOAD_A_STEPS, settle=1.0)
        s = state(box)
        print(f"text: block {s['area'][0xF2]}, file {s['file']}", flush=True)
        bmp = work / "port-text.bmp"
        subprocess.run([str(cokbox.ECLPLAY), *GAME, "--screen", str(bmp), *moons(s["area"]),
                        str(ASSETS), "16"],
                       check=False, capture_output=True, cwd=ROOT, timeout=120)
        compare("text", box.screenshot(work / "dosbox-text.png"), cokbox.Screen.load(bmp),
                [SCREEN])
    # The places, each facing each way; in CGA mode the first facing of the
    # gate (out of doors) and the hall (in).
    runs = [(place, place["facings"], "ega") for place in PLACES]
    runs += [(place, place["facings"][:1], "cga") for place in PLACES[:2]]
    for place, facings, display in runs:
        with box_for(saved_game(work, place, facings[0]), display) as box:
            if not K.title(box):
                results.append((f"{place['name']} (no title)", 1))
                continue
            K.type_steps(box, K.LOAD_A_STEPS, settle=1.0)
            box.keys("M")  # Move: the arrows turn
            for i, facing in enumerate(facings):
                if i:
                    box.keys("\\>")
                box.wait_input(settle=0.8)
                s = state(box)
                name = f"{place['name']}-{facing}" + ("-cga" if display == "cga" else "")
                print(f"{name}: block {s['area'][0xF2]}, at {s['x']},{s['y']} facing "
                      f"{s['dir']}", flush=True)
                if (s["area"][0xF2], s["x"], s["y"], s["dir"]) != \
                        (place["block"], *place["square"], facing * 2):
                    results.append((name + " (not there)", 1))
                    continue
                compare(name, box.screenshot(work / f"dosbox-{name}.png"),
                        port(work, name, place, facing, s["area"], display), FRAME)
    differ = [r for r in results if r[1]]
    if differ:
        print("VERDICT: DIFFERS new (" + ", ".join(f"{n} {c} pixels" for n, c in differ) + ")")
        return 1
    print("VERDICT: AGREES")
    return 0


if __name__ == "__main__":
    sys.exit(main())

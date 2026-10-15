#!/usr/bin/env python3
"""Checks: the display modes (README, Checks against the original: "In CGA
mode (KRYNN.CFG's first line C): compare the title, the start menu,
character creation, the overland map, combat, a 3D view and the camp with
eclplay --display cga --shots, colour for colour ..." and "In EGA mode, the
same screens should match eclplay's").

In each machine (tandy, ega, cga) the original in DOSBox-X and eclplay
--start-menu take the same keys with the same seed; at each step the
DOSBox-X screen is compared with the eclplay frame it matches best (eclplay
saves one at each key read). Two runs a mode:

- adventure: the title's menu, game A loaded, Solace's first page, the
  outpost's menu, its inn's camp, a character's sheet from the camp, the
  overland map, Move mode, a step (the caravan's text), the battle's first
  turn (4 baaz) and its map with the aiming cursor.
- create: the start menu's Create New Character: race, gender, class, god,
  alignment, the rolled scores.
- throtl: a saved game the port writes by Throtl (the guards beaten),
  loaded: Throtl's street in 3D, a step, a turn, the camp over the view.

For CGA it also reads the palette and the 3D view's colours: DS:6d81-6d83.
Prints one line per screen and a verdict; exits 1 if any screen differs.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402


SEED = 1
ROUTES = {
    "adventure": [
        ("title menu", r"\kx"),
        ("game A loaded", r"P\v\v\rA"),
        ("Solace", r"\v\v\v\v\v\r"),
        ("outpost menu", r"\r\r\r\r\r\r\r"),
        ("camp", r"I\r"),
        ("sheet", r"V"),
        ("camp again", r"E"),
        ("outpost again", r"E"),
        ("near outpost", r"L"),
        ("overland", r"N"),
        ("move mode", r"M"),
        ("step", r"\>\>\^"),
        ("battle turn", r"\r"),
        ("aim", r"A"),
    ],
    "create": [
        ("start menu", r"\kxP"),
        ("race", r"\r"),
        ("gender", r"\r"),
        ("class", r"\r"),
        ("god", r"\r"),
        ("alignment", r"\r"),
        ("scores", r"\r"),
        ("icon editor", r"NBOB\r"),
    ],
    # A port-written save on the overland map by Throtl, the guards beaten
    # (as batch C's street save). Loading it, the port asks "YOU ARE NEAR
    # THROTL. DO YOU ENTER?" where the original shows the map's Move Encamp
    # (see the results), so eclplay declines first ("n", ECL_EXTRA); then a
    # step away and back, Throtl's street in 3D, a step, a turn, the camp.
    "throtl": [
        ("overland by Throtl", r"\kxP\v\v\rD\v\v\v\v\v\r"),
        ("move", r"m"),
        ("step away", r"2"),
        ("near Throtl", r"8"),
        ("street 3D", r"y"),
        ("move 3D", r"m"),
        ("step 3D", r"\^"),
        ("turn 3D", r"\>"),
        ("menu 3D", r"e"),
        ("camp 3D", r"e"),
    ],
}
# Keys eclplay takes after a step's (the original's screen being the same).
ECL_EXTRA = {("throtl", "overland by Throtl"): "n"}
# Game A's party on the overland map by Throtl, var 0x4c4d bit 2 (the gate's
# guards beaten): decline the outpost, Move NE then N, decline Throtl, Exit,
# Encamp, Save as D (eclplay from ECL block 16).
THROTL_SAVE = (["--load", "SAVE/SAVGAMA.DAT", "--play", "--set", "4bf2=11", "--set",
                "4bc3=1", "--set", "4bc4=3", "--set", "4c2d=1", "--set", "4c4d=2"],
               "nm98nEESD", 16)
# The full stop eclplay adds after "PRESS <ENTER>/<RETURN> TO CONTINUE" (row
# 24, column 34): the port bug load_game_screen.py found; not counted again.
KNOWN = (272, 192, 279, 199)


def port_save(out):
    """Write THROTL_SAVE's game D with eclplay; return the saves directory."""
    args, keys, block = THROTL_SAVE
    saves = out / "saves"
    if saves.exists():
        shutil.rmtree(saves)
    saves.mkdir(parents=True)
    for f in (cokbox.ROOT / "SAVE").iterdir():
        if f.is_file():
            shutil.copyfile(f, saves / f.name)
    subprocess.run([str(cokbox.ROOT / "build/eclplay"), *args, "--saves", str(saves),
                    "--keys", keys, str(cokbox.ROOT / "Assets"), str(block)],
                   cwd=cokbox.ROOT, check=True, capture_output=True, timeout=120)
    if not (saves / "SAVGAMD.DAT").exists():
        raise SystemExit("the port wrote no saved game D")
    return saves
# Where the screens' parts are (for a short report of what differs).
REGIONS = {"view/picture": (8, 8, 128, 128), "party list": (136, 8, 176, 64),
           "status": (136, 112, 176, 8), "text": (8, 136, 304, 48),
           "row 24": (0, 192, 320, 8)}


def compare(machine, route, work, verbose, status):
    steps = ROUTES[route]
    keys = "".join(k + ECL_EXTRA.get((route, name), "") for name, k in steps)
    saves = port_save(work) if route == "throtl" else cokbox.ROOT / "SAVE"
    _, _, eshots = cokbox.eclplay(keys, work / "eclplay", machine=machine,
                                  saves=[saves], args=["--seed", str(SEED)])
    expected = [cokbox.Screen.load(p) for p in eshots]
    results = []
    with cokbox.Box(workdir=work / "box", machine=machine, saves=[saves],
                    seed=SEED, verbose=verbose) as box:
        for name, step in steps:
            box.keys(step)
            box.wait_input(settle=1.5)
            shown = box.screenshot(work / f"{name.replace(' ', '-')}.png")
            best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
            count, area = shown.diff(expected[best])
            colours = sorted(set(shown.pixels))
            line = (f"{machine} {route} {name}: {count} pixels differ from eclplay's "
                    f"{eshots[best].name}" + (f" in {area}" if area else ""))
            known = area and KNOWN[0] <= area[0] and area[2] <= KNOWN[2] and \
                KNOWN[1] <= area[1] and area[3] <= KNOWN[3]
            if known:
                line += " (the known full stop: load_game_screen.py)"
            elif count:
                status[0] = 1
                shown.save_png(work / f"diff-{name.replace(' ', '-')}.png",
                               highlight=expected[best])
                parts = [r for r, (x, y, w, h) in REGIONS.items()
                         if shown.region(x, y, w, h) != expected[best].region(x, y, w, h)]
                line += f" ({', '.join(parts)})"
            if machine == "cga":
                line += f"; colours {colours}"
            print(line, flush=True)
            results.append((name, count, colours))
        if route == "create":
            notice(box, machine, work)
        if route == "throtl":  # the 3D view's colours, set on entering the block
            print(f"{machine} DS:6d81-6d83 = {box.ds_read(0x6d81, 3).hex(' ')}")
    return results


def notice(box, machine, work):
    """The icon editor's 1st-color: in CGA mode `Sorry, not in CGA` on row 24
    for speed x 100 ms (4def:4276), not a key wait, so eclplay's frames do not
    hold it: the speed (DS:4b38, 4) poked to 50 (5 s) for the screenshots,
    taken at once; row 24's colours reported (kept as notice-N.png)."""
    box.wait_input()
    speed = box.ds_byte(0x4B38)
    box.ds_write(0x4B38, bytes([50]))
    box.push_key(cokbox.SCAN["1"] << 8 | ord("1"))
    rows = []
    for i in range(3):
        shot = box.screenshot(work / f"notice-{i}.png")
        row = shot.region(0, 192, 320, 8)
        rows.append(sorted({c: row.count(c) for c in set(row)}.items()))
    box.ds_write(0x4B38, bytes([speed]))
    for i, r in enumerate(rows):
        print(f"{machine} icon editor 1st-color, shot {i}: row 24's colours (index, pixels) {r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--machine", action="append", choices=sorted(cokbox.MACHINES))
    parser.add_argument("--route", action="append", choices=sorted(ROUTES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-display-modes-"))
    if not (cokbox.ROOT / "SAVE/SAVGAMA.DAT").exists():
        print("needs the original's saved games in SAVE/")
        return 2
    status = [0]
    for machine in args.machine or ["tandy", "ega", "cga"]:
        for route in args.route or list(ROUTES):
            compare(machine, route, work / machine / route, args.verbose, status)
    print("verdict:", "AGREES" if status[0] == 0 else "screens differ (see above)")
    print(f"files in {work}")
    return status[0]


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Checks: the display modes (DEVELOPERS.md, Checks against the original: "In CGA
mode (KRYNN.CFG's first line C): compare the title, the start menu,
character creation, the overland map, combat, a 3D view and the camp with
eclplay --display cga --shots, colour for colour ..." and "In EGA mode, the
same screens should match eclplay's").

In each machine (tandy, ega, cga) the original in DOSBox-X and eclplay
--start-menu take the same keys with the same seed; each step's screen is
compared with the port's at the same key read (checklib.frames_at). Three
routes a mode:

- adventure: the title's menu, game A loaded, Solace's first page, the
  outpost's menu, its inn's camp, a character's sheet from the camp, the
  overland map, Move mode, a step (the caravan's text), the battle's first
  turn (4 baaz) and its map with the aiming cursor.
- create: the start menu's Create New Character: race, gender, class, god,
  alignment, the rolled scores, the icon editor; in CGA mode its 1st-color
  (`Sorry, not in CGA` on row 24 during a delay, compared with the port's
  frame at that delay).
- throtl: a saved game the port writes by Throtl (the guards beaten),
  loaded: the overland map (Move Encamp, the location's question not
  asked again), a step away and back, Throtl's street in 3D (its far
  walls: two-set WALLDEF records), a step, a turn, the camp over the view.

CGA's screens hold only its four colours (0, 11, 13, 15); the 3D view's
colours DS:6d81-6d83 are asserted as batch F read them (the port's are
compared through the view's pixels).
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import f_lib  # noqa: E402

cokbox = checklib.cokbox

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
    "throtl": [
        ("overland by Throtl", f_lib.load_letter("D")),
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
CGA_COLOURS = {0, 11, 13, 15}
# DS:6d81-6d83 on Throtl's entry, as batch F read them.
VIEW_COLOURS = {"cga": "0f0706", "tandy": "000008", "ega": "000008"}
SPEED = 0x4B38


def notice(v, box, keys, work, kw):
    """The icon editor's 1st-color in CGA mode: `Sorry, not in CGA` on row
    24 for speed x 100 ms (4def:4276), a delay, not a key wait. The speed
    (DS:4b38) is poked to 50 (5 s) and the screen taken as soon as it
    changes; the port's frames at its delays after the same key
    (checklib.eclplay_with_pauses) must hold it."""
    box.wait_input()
    before = box.screenshot()
    speed = box.ds_byte(SPEED)
    box.ds_write(SPEED, bytes([50]))
    box.push_key(cokbox.SCAN["1"] << 8 | ord("1"))
    shot = None
    for i in range(20):
        shot = box.screenshot(work / f"notice-{i}.png")
        if shot.pixels != before.pixels:
            break
    box.ds_write(SPEED, bytes([speed]))
    row = shot.region(0, 192, 320, 8)
    print(f"  cga 1st-color: row 24's colours (index, pixels) "
          f"{sorted((c, row.count(c)) for c in set(row))}")
    v.check(any(c == 15 for c in row) and set(row) <= {0, 15},
            "cga 1st-color: white text on row 24 in the original (Sorry, not in CGA)")
    exe = f_lib.eclplay(pauses=True)
    plain = checklib.port_keys(keys)
    _, _, a = cokbox.eclplay(plain, work / "pauses-before", binary=exe, **kw)
    _, _, b = cokbox.eclplay(plain + "1", work / "pauses-after", binary=exe, **kw)
    # b[len(a) - 1] is the wait before the 1; then the delays' frames.
    seq = [cokbox.Screen.load(p) for p in b[len(a):]]
    counts = [checklib.masked_diff(shot, s)[0] for s in seq]
    print(f"  cga 1st-color: the port's frames after the key (pixels differing): {counts}")
    v.check(0 in counts, "cga 1st-color: the original's notice is one of the port's frames "
            "at its delays", f"closest {min(counts) if counts else None} pixels")


def compare(v, machine, route, work, verbose):
    steps = ROUTES[route]
    saves = f_lib.throtl_save(work, "D", beaten=True) if route == "throtl" else checklib.SAVES
    kw = dict(machine=machine, saves=[saves], args=["--seed", str(SEED)])
    shots = {}
    with cokbox.Box(workdir=work / "box", machine=machine, saves=[saves],
                    seed=SEED, verbose=verbose) as box:
        for name, step in steps:
            box.keys(step)
            box.wait_input(settle=1.5)
            shots[name] = box.screenshot(work / f"{name.replace(' ', '-')}.png")
        if route == "create" and machine == "cga":
            notice(v, box, f_lib.marked(steps), work, kw)
        if route == "throtl":  # the 3D view's colours, set on entering the block
            colours = box.ds_read(0x6D81, 3).hex()
            if machine not in VIEW_COLOURS:
                print(f"  {machine} DS:6d81-6d83 in Throtl: {colours} (not read by batch F)")
            else:
                v.check(colours == VIEW_COLOURS[machine],
                        f"{machine} DS:6d81-6d83 in Throtl as batch F read them "
                        f"({VIEW_COLOURS[machine]}; the port's: through the view's pixels)",
                        colours)
    port = checklib.frames_at(f_lib.marked(steps), work / "port", **kw)
    f_lib.judge(v, f"{machine} {route}", shots, port, work)
    if machine == "cga":
        seen = set().union(*(set(s.pixels) for s in shots.values()))
        v.check(seen <= CGA_COLOURS, f"cga {route}: the original's screens hold only "
                f"CGA's colours {sorted(CGA_COLOURS)}", f"{sorted(seen)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--machine", action="append", choices=sorted(cokbox.MACHINES))
    parser.add_argument("--route", action="append", choices=sorted(ROUTES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "display-modes")
    v = checklib.Verdict()
    for machine in args.machine or ["tandy", "ega", "cga"]:
        for route in args.route or list(ROUTES):
            compare(v, machine, route, work / machine / route, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check: the mouse (docs/checks.md, Checks against the original: "The mouse (see
The mouse and the joystick), in DOSBox with its own driver (machine=tandy,
then svga_s3 and machine=cga): the cursor's look in each mode ..., where it
is hidden (the title's waits, running text), and that a click lands on the
cell under the arrow's tip. ... A click in the title menu's column 0 before
any list ... Both buttons at once ...").

KRYNN.CFG's third line M; DOSBox-X's own INT 33h driver, its pointer
following X's (sdl mouse_emulation=integration: the 640x400 window's pixel
2x, 2y is the game's pixel x, y), moved and clicked with xdotool. eclplay
--mouse takes the same moves and clicks as \\mX,Y,B. in its keys, and its
--shots draw the port's cursor; each screen is compared with the port's at
the same key read (checklib.frames_at).

Parts:
- look: the title's menu with the pointer at 100,60 and at 200,150, in
  tandy, vga (svga_s3, EGA) and cga. In CGA mode the arrow's 16x16 box is
  not compared: DOSBox-X's driver draws CGA's cursor a bit a pixel (16
  pixels wide), the port two bits a pixel as Microsoft's driver does (8
  wide), as docs/front-end.md says (The mouse and the joystick).
- hidden: the pointer parked over the picture through the title's first
  wait (the SSI picture), and through game A's route (Solace's running text
  and pages, the outpost, the camp, the overland map).
- click: the title menu: a click on PLAY (row 24) and, in another run, in
  column 0 (8 pixels) of row 12, where DS:6e2f, 6e30 and 6e16 are not yet
  set (the port takes them as 0: Enter, the game starts).
- both: both buttons at once at the title menu, then P: the menu takes
  the click's 0 as an extended key's prefix and P's scan code (0x19) after
  it (the port does so).
- mark: the pointer on the overland map's mark through a camp, then moved
  off, or a step first: see mark().
"""
import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import f_lib  # noqa: E402

cokbox = checklib.cokbox

CONF = {"sdl.mouse_emulation": "integration"}
ECLPLAY_ARGS = ["--mouse", "--input", "mouse", "--seed", "1"]
CGA_CURSOR = ("DOSBox-X's own driver draws CGA's cursor a bit a pixel, 16 wide; the port "
              "two bits a pixel, 8 wide, as Microsoft's (docs/front-end.md: The mouse and the joystick)")


class MouseBox(cokbox.Box):
    def point(self, x, y):
        """Put the pointer on the game's pixel X, Y."""
        self._xdo("mousemove", "--window", self.window, 2 * x + 1, 2 * y + 1)
        time.sleep(0.4)

    def click(self, x, y, buttons=1):
        self.point(x, y)
        down = ["1", "3"] if buttons == 3 else ["1" if buttons == 1 else "3"]
        # Held a while: the game polls the buttons' state (INT 33h 3), which
        # a press and release in one of DOSBox-X's event batches would never
        # show down.
        self._xdo(*[a for b in down for a in ("mousedown", b)])
        time.sleep(0.3)
        self._xdo(*[a for b in down for a in ("mouseup", b)])
        time.sleep(0.4)


def box(work, machine="tandy", saves=(), verbose=False):
    return MouseBox(workdir=work / "box", machine=machine, input="M", extra_conf=CONF,
                    saves=list(saves), seed=1, verbose=verbose)


def port(keys, work, machine="tandy", saves=()):
    """The port's frames at KEYS' marks."""
    return checklib.frames_at(keys, work / "port", machine=machine, saves=list(saves),
                              args=ECLPLAY_ARGS)


def look(v, work, verbose):
    points = ((100, 60), (200, 150))
    keys = r"\kx" + "".join(rf"\m{x},{y}.z\sat-{x}-{y}." for x, y in points)
    for machine in ("tandy", "vga", "cga"):
        w = work / f"look-{machine}"
        frames = port(keys, w, machine)
        shots = {}
        with box(w, machine, verbose=verbose) as b:
            b.keys(r"\kx")
            for x, y in points:
                b.wait_input()
                b.point(x, y)
                b.keys("z")
                b.wait_input(settle=0.5)
                shots[f"at-{x}-{y}"] = b.screenshot(w / f"at-{x}-{y}.png")
        for (x, y), (name, shot) in zip(points, shots.items()):
            masks = ([(x, y, 16, 16, f"the arrow's 16x16 box from {x},{y}: {CGA_CURSOR}")]
                     if machine == "cga" else [])
            v.screen(f"look {machine} pointer at {x},{y}", shot, frames.get(name),
                     masks=masks, work=w)


ROUTE = [("title menu", r"\kx"), ("pointer parked at the title menu", None),
         ("game A loaded", r"P\v\v\rA"), ("Solace", r"\v\v\v\v\v\r"), ("Solace page 2", r"\r"),
         ("outpost", r"\r\r\r\r\r\r"), ("camp", r"I\r"), ("outpost again", "E"),
         ("near outpost", "L"), ("overland", "N"), ("move mode", "M")]
PARK = (60, 90)


def hidden(v, work, verbose):
    w = work / "hidden"
    saves = [checklib.SAVES]
    park = r"\m%d,%d.z" % PARK
    steps = [(name, k if k is not None else park) for name, k in ROUTE]
    # The pointer is parked before the title's first wait in both (eclplay
    # moves it as the keys reach the \m, before that wait's \k).
    frames = port(r"\m%d,%d." % PARK + f_lib.marked(steps), w, saves=saves)
    shots = {}
    with box(w, saves=saves, verbose=verbose) as b:
        # The title's first wait (the SSI picture): the pointer over it.
        b.point(*PARK)
        b.wait_input(settle=0.5)
        ssi = b.screenshot(w / "ssi.png")
        for name, k in ROUTE:
            if k is None:  # parked, then z (nothing at the title's menu)
                b.wait_input()
                b.point(*PARK)
                b.keys("z")
            else:
                b.keys(k)
            b.wait_input(settle=1.0)
            shots[name] = b.screenshot(w / f"{name.replace(' ', '-')}.png")
    # eclplay given no key lets the title's timed waits run out, so its frame
    # at the first wait is the first shot of a run whose \k presses a key
    # there: the screen at that poll (a \m before it would take that wait
    # for the move: the pointer stays where the driver puts it, hidden in
    # both, so a cursor shown there would differ wherever it is).
    _, _, first = cokbox.eclplay(r"\kx", w / "port-ssi", saves=saves,
                                 args=ECLPLAY_ARGS,
                                 binary=checklib.stopping_eclplay())
    v.screen("hidden: the title's first picture, pointer at %d,%d" % PARK, ssi,
             cokbox.Screen.load(first[0]) if first else None, work=w)
    f_lib.judge(v, "hidden:", shots, frames, w)


def click(v, work, verbose):
    # PLAY at row 24 (the menu "CHAMPIONS OF KRYNN V1.2 PLAY DEMO"): x 200.
    for name, (x, y) in (("click PLAY", (201, 196)), ("click column 0", (3, 100))):
        w = work / name.replace(" ", "-")
        frames = port(r"\kx\m%d,%d,1.\safter." % (x, y), w)
        with box(w, verbose=verbose) as b:
            b.keys(r"\kx")
            b.wait_input()
            b.click(x, y)
            b.wait_input(settle=1.5)
            shot = b.screenshot(w / "after.png")
            print(f"  {name}: the original waits at {b.where()}")
        v.screen(f"{name} at {x},{y}", shot, frames.get("after"), work=w)


def both(v, work, verbose):
    w = work / "both"
    frames = port(r"\kx\m100,100,3.P\safter.", w)
    with box(w, verbose=verbose) as b:
        b.keys(r"\kx")
        b.wait_input()
        b.click(100, 100, 3)
        b.keys("P")
        b.wait_input(settle=1.5)
        shot = b.screenshot(w / "after.png")
        print(f"  both: the original waits at {b.where()}")
    v.screen("both buttons, then P", shot, frames.get("after"), work=w)


# Game A to the overland map by the first outpost: the party's mark at 1,3,
# the screen's cell 2,4 (pixels 16-23, 32-39); the pointer's tip on it.
MARK_KEYS = r"\kxP\v\v\rA\v\v\v\v\v\r\r\r\r\r\r\r\rLN"
MARK_AT, AWAY = (18, 34), (100, 120)
# The mark's cell under the arrow's box: where the original's mark loses
# pixels (docs/front-end.md, The mouse and the joystick: "Not reproduced").
MARK_GONE = (18, 34, 6, 6)
MARK_GONE_PIXELS = 25


def mark(v, work, verbose):
    """The pointer on the party's mark through a camp (Encamp, Exit): then
    (a) moved off: in the original the part of the mark under the arrow
    vanishes (the driver restores the background it saved before the mark
    was put over the arrow), which the port does not reproduce: there the
    original's recorded observation is asserted (25 pixels of the port's
    whole mark gone, all in the mark's cell under the arrow's box) and the
    rest of the screen compared; (b) left there through a step north (Move,
    8), and (c) then moved off: the screens compared (the old cell: no
    arrow's image painted back). The first step meets the caravan; its
    text's prompt ignores the z."""
    saves = [checklib.SAVES]
    off = r"\m%d,%d.z" % AWAY
    for name, after in (("moved off", off), ("step away", "M8"),
                        ("step away, then moved off", "M8" + off)):
        w = work / f"mark-{name.replace(' ', '-').replace(',', '')}"
        keys = MARK_KEYS + r"\m%d,%d.z" % MARK_AT + "EE" + after + r"\safter."
        frames = port(keys, w, saves=saves)
        with box(w, saves=saves, verbose=verbose) as b:
            b.keys(MARK_KEYS)
            b.wait_input()
            b.point(*MARK_AT)
            b.keys("z")
            b.keys("EE")
            b.wait_input(settle=1.0)
            b.screenshot(w / "after-camp.png")
            b.keys(after[:2] if after.startswith("M") else "")
            if after.endswith(off):
                b.wait_input()
                b.point(*AWAY)
                b.keys("z")
            b.wait_input(settle=1.0)
            shot = b.screenshot(w / "after.png")
        other = frames.get("after")
        what = f"mark: pointer on the mark, camp, {name}"
        if name != "moved off":
            v.screen(what, shot, other, work=w)
            continue
        reason = ("the mark's part under the arrow vanishes in the original, not "
                  "reproduced by the port (docs/front-end.md: The mouse and the joystick)")
        v.screen(what + " (but the mark's cell under the arrow)", shot, other,
                 masks=[MARK_GONE + (reason,)], work=w)
        if other is None:
            continue
        count, area = checklib.masked_diff(shot, other)
        inside = count - checklib.masked_diff(shot, other, [MARK_GONE])[0]
        print(f"  {what}: the port is not compared in the mark's cell under the arrow "
              f"({reason}); the original's observation is asserted there")
        v.check(inside == MARK_GONE_PIXELS, f"{what}: the original's mark lost "
                f"{MARK_GONE_PIXELS} pixels under the arrow's box, as batch F saw",
                f"{inside} pixels differ from the port's whole mark in {area}")


PARTS = {"look": look, "hidden": hidden, "click": click, "both": both, "mark": mark}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append", choices=list(PARTS))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "mouse")
    v = checklib.Verdict()
    for part in args.part or list(PARTS):
        PARTS[part](v, work, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

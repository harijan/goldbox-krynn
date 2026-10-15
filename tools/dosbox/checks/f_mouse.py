#!/usr/bin/env python3
"""Check: the mouse (README, Checks against the original: "The mouse (see
The mouse and the joystick), in DOSBox with its own driver (machine=tandy,
then svga_s3 and machine=cga): the cursor's look in each mode ..., where it
is hidden (the title's waits, running text), and that a click lands on the
cell under the arrow's tip. ... A click in the title menu's column 0 before
any list ... Both buttons at once ...").

KRYNN.CFG's third line M; DOSBox-X's own INT 33h driver, its pointer
following X's (sdl mouse_emulation=integration: the 640x400 window's pixel
2x, 2y is the game's pixel x, y), moved and clicked with xdotool. eclplay
--mouse takes the same moves and clicks as \\mX,Y,B. in its keys, and its
--shots draw the port's cursor; screens are compared pixel for pixel with
eclplay's frame that matches best.

Parts:
- look: the title's menu with the pointer at 100,60 and at 200,150, in
  tandy, vga (svga_s3, EGA) and cga.
- hidden: the pointer parked over the picture through the title's first
  wait (the SSI picture), and through game A's route (Solace's running text
  and pages, the outpost, the camp, the overland map).
- click: the title menu: a click on PLAY (row 24) and, in another run, in
  column 0 (8 pixels) of row 12, where DS:6e2f, 6e30 and 6e16 are not yet
  set (the port takes them as 0: Enter, the game starts).
- both: both buttons at once at the title menu, then P: the menu takes
  the click's 0 as an extended key's prefix and P's scan code (0x19) after
  it (the port does so).
"""
import argparse
import os
import pathlib
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402


CONF = {"sdl.mouse_emulation": "integration"}


class MouseBox(cokbox.Box):
    def point(self, x, y):
        """Put the pointer on the game's pixel X, Y."""
        self._xdo("mousemove", "--window", self.window, 2 * x + 1, 2 * y + 1)
        time.sleep(0.4)

    def click(self, x, y, buttons=1):
        self.point(x, y)
        if buttons == 3:
            self._xdo("mousedown", "1", "mousedown", "3")
            time.sleep(0.3)
            self._xdo("mouseup", "1", "mouseup", "3")
        else:
            # Held a while: the game polls the buttons' state (INT 33h 3),
            # which a press and release in one of DOSBox-X's event batches
            # would never show down.
            button = "1" if buttons == 1 else "3"
            self._xdo("mousedown", button)
            time.sleep(0.3)
            self._xdo("mouseup", button)
        time.sleep(0.4)


def eclplay(keys, work, machine="tandy", saves=()):
    stdout, _, shots = cokbox.eclplay(keys, work / "eclplay", machine=machine, saves=saves,
                                      args=["--mouse", "--input", "mouse", "--seed", "1"])
    return stdout, [cokbox.Screen.load(p) for p in shots], shots


def compare(name, shown, expected, names, work):
    best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
    count, area = shown.diff(expected[best])
    if count:
        shown.save_png(work / f"diff-{name}.png", highlight=expected[best])
    print(f"{name}: {count} pixels differ from eclplay's {names[best].name}"
          + (f" in {area}" if area else ""), flush=True)
    return count


def box(work, machine="tandy", saves=(), verbose=False):
    return MouseBox(workdir=work / "box", machine=machine, input="M", extra_conf=CONF,
                    saves=list(saves), seed=1, verbose=verbose)


def look(work, verbose):
    status = 0
    for machine in ("tandy", "vga", "cga"):
        w = work / f"look-{machine}"
        _, expected, names = eclplay(r"\kx\m100,60.z\m200,150.z", w, machine)
        with box(w, machine, verbose=verbose) as b:
            b.keys(r"\kx")
            for x, y in ((100, 60), (200, 150)):
                b.wait_input()
                b.point(x, y)
                b.keys("z")
                b.wait_input(settle=0.5)
                shown = b.screenshot(w / f"at-{x}-{y}.png")
                status |= bool(compare(f"look {machine} pointer at {x},{y}", shown, expected,
                                       names, w))
    return status


ROUTE = [("title menu", r"\kx"), ("pointer parked at the title menu", None), ("game A loaded", r"P\v\v\rA"),
         ("Solace", r"\v\v\v\v\v\r"), ("Solace page 2", r"\r"), ("outpost", r"\r\r\r\r\r\r"),
         ("camp", r"I\r"), ("outpost again", "E"), ("near outpost", "L"), ("overland", "N"),
         ("move mode", "M")]
PARK = (60, 90)


def hidden(work, verbose):
    status = 0
    w = work / "hidden"
    keys = "".join(k if k is not None else r"\m%d,%d.z" % PARK for _, k in ROUTE)
    _, expected, names = eclplay(keys, w, saves=[cokbox.ROOT / "SAVE"])
    with box(w, saves=[cokbox.ROOT / "SAVE"], verbose=verbose) as b:
        # The title's first wait (the SSI picture): the pointer over it.
        b.point(*PARK)
        b.wait_input(settle=0.5)
        status |= bool(compare("hidden: the title's first picture, pointer at %d,%d" % PARK,
                               b.screenshot(w / "ssi.png"), expected, names, w))
        for name, k in ROUTE:
            if k is None:  # parked, then z (nothing at the title's menu)
                b.wait_input()
                b.point(*PARK)
                b.keys("z")
            else:
                b.keys(k)
            b.wait_input(settle=1.0)
            shown = b.screenshot(w / f"{name.replace(' ', '-')}.png")
            count = compare(f"hidden: {name}", shown, expected, names, w)
            # The known full stop after "TO CONTINUE" (load_game_screen.py).
            status |= bool(count) and count != 59
    return status


def click(work, verbose):
    status = 0
    # PLAY at row 24 (the menu "CHAMPIONS OF KRYNN V1.2 PLAY DEMO"): x 200.
    for name, (x, y) in (("click PLAY", (201, 196)), ("click column 0", (3, 100))):
        w = work / name.replace(" ", "-")
        out, expected, names = eclplay(r"\kx\m%d,%d,1." % (x, y), w)
        with box(w, verbose=verbose) as b:
            b.keys(r"\kx")
            b.wait_input()
            b.click(x, y)
            b.wait_input(settle=1.5)
            shown = b.screenshot(w / "after.png")
            status |= bool(compare(f"{name} at {x},{y}", shown, expected, names, w))
            print(f"{name}: the original waits at {b.where()}; eclplay: "
                  f"{[l for l in out.splitlines() if l.startswith(('choice', 'list'))][:3]}")
    return status


def both(work, verbose):
    w = work / "both"
    out, expected, names = eclplay(r"\kx\m100,100,3.P", w)
    with box(w, verbose=verbose) as b:
        b.keys(r"\kx")
        b.wait_input()
        b.click(100, 100, 3)
        b.keys("P")
        b.wait_input(settle=1.5)
        shown = b.screenshot(w / "after.png")
        status = bool(compare("both buttons, then P", shown, expected, names, w))
        print(f"both: the original waits at {b.where()}; eclplay: "
              f"{[l for l in out.splitlines() if l.startswith(('choice', 'list', 'menu'))][-3:]}")
    return status


# Game A to the overland map by the first outpost: the party's mark at 1,3,
# the screen's cell 2,4 (pixels 16-23, 32-39); the pointer's tip on it.
MARK_KEYS = r"\kxP\v\v\rA\v\v\v\v\v\r\r\r\r\r\r\r\rLN"
MARK_AT, AWAY = (18, 34), (100, 120)


def mark(work, verbose):
    """The pointer on the party's mark through a camp (Encamp, Exit): then
    (a) moved off: does the part of the mark under the arrow vanish? and (b)
    left there through a step north (Move, 8), then moved off: does the old
    cell show the arrow? (The port: neither.) The mark's cell is reported.
    The first step meets the caravan; its text's prompt ignores the z."""
    status = 0
    saves = [cokbox.ROOT / "SAVE"]
    off = r"\m%d,%d.z" % AWAY
    for name, after, ecl_after in (("moved off", "", off), ("step away", "M8", "M8"),
                                   ("step away, then moved off", "M8", "M8" + off)):
        w = work / f"mark-{name.replace(' ', '-')}"
        keys = MARK_KEYS + r"\m%d,%d.z" % MARK_AT + "EE" + ecl_after
        _, expected, names = eclplay(keys, w, saves=saves)
        with box(w, saves=saves, verbose=verbose) as b:
            b.keys(MARK_KEYS)
            b.wait_input()
            b.point(*MARK_AT)
            b.keys("z")
            b.keys("EE")
            b.wait_input(settle=1.0)
            b.screenshot(w / "after-camp.png")
            b.keys(after)
            if ecl_after.endswith(off):
                b.wait_input()
                b.point(*AWAY)
                b.keys("z")
            b.wait_input(settle=1.0)
            shown = b.screenshot(w / "after.png")
            status |= bool(compare(f"mark: pointer on the mark, camp, {name}", shown, expected,
                                   names, w))
            best = min(range(len(expected)), key=lambda i: shown.diff(expected[i])[0])
            cell_o = shown.region(16, 32, 8, 8)
            cell_p = expected[best].region(16, 32, 8, 8)
            print(f"mark {name}: the mark's cell (16,32): original {cell_o.hex()}, "
                  f"port {cell_p.hex()}{'' if cell_o == cell_p else ' DIFFER'}")
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append",
                        choices=["look", "hidden", "click", "both", "mark"])
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-mouse-")).resolve()
    status = 0
    for part in args.part or ["look", "hidden", "click", "both", "mark"]:
        status |= {"look": look, "hidden": hidden, "click": click, "both": both,
                   "mark": mark}[part](
            work, args.verbose)
    print("verdict:", "AGREES" if not status else "screens differ (see above)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

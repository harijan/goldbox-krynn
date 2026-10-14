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

os.environ.setdefault("COKBOX_DISPLAYS", "160-169")

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
            self._xdo("click", "1" if buttons == 1 else "3")
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


ROUTE = [("title menu", r"\kx"), ("pointer parked", None), ("game A loaded", r"P\v\v\rA"),
         ("Solace", r"\v\v\v\v\v\r"), ("Solace page 2", r"\r"), ("outpost", r"\r\r\r\r\r\r"),
         ("camp", r"I\r"), ("outpost again", "E"), ("near outpost", "L"), ("overland", "N"),
         ("move mode", "M")]
PARK = (60, 90)


def hidden(work, verbose):
    status = 0
    w = work / "hidden"
    keys = "".join(k if k is not None else r"\m%d,%d." % PARK for _, k in ROUTE)
    _, expected, names = eclplay(keys, w, saves=[cokbox.ROOT / "SAVE"])
    with box(w, saves=[cokbox.ROOT / "SAVE"], verbose=verbose) as b:
        # The title's first wait (the SSI picture): the pointer over it.
        b.point(*PARK)
        b.wait_input(settle=0.5)
        status |= bool(compare("hidden: the title's first picture, pointer at %d,%d" % PARK,
                               b.screenshot(w / "ssi.png"), expected, names, w))
        for name, k in ROUTE:
            if k is None:
                b.point(*PARK)
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append", choices=["look", "hidden", "click", "both"])
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-mouse-")).resolve()
    status = 0
    for part in args.part or ["look", "hidden", "click", "both"]:
        status |= {"look": look, "hidden": hidden, "click": click, "both": both}[part](
            work, args.verbose)
    print("verdict:", "AGREES" if not status else "screens differ (see above)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

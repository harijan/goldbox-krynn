#!/usr/bin/env python3
"""The mouse's areas over The Dark Queen of Krynn's 3D view at the
adventure's commands (1bdb:1aef's four areas, 2aa6:15e3's cursor; the port's
bar_menu_areas, src/dark_queen.c) against eclplay --game dark-queen
--start-menu --mouse. The shipped saved game A (Load, Tdqk, A) is begun and
Caergoth's 3D view reached as dqk_party.py does (its copy protection answered
in the box), at the party's 3,2 facing 2. Then fourteen spots, one at a time,
each side of the view's edges (the columns 0, 39, 40, 95, 96, 135, 136; the
rows 99, 100, 159, 160):

- the pointer moved there (xdotool, the box's sdl.mouse_emulation=always):
  the whole screen compared with the port's shot with the pointer there (the
  game draws its own cursor, so none is masked: 26, 11, 25, 27 or the
  shield, 2), and the party's square and facing (DS:c9f0-c9f2) against the
  port's (the last step: or turn: line before it);
- then the click there, the party's state after it against the port's.

Zones (the rule of the port's commands, dark_queen.c's view_areas): columns
0-39 and rows 0-99 turn left, 40-95 step ahead, 96-135 turn right, rows
100-159 (columns 0-135) turn around, anything else nothing. About two
minutes.

    python3 tools/dosbox/checks/dqk_view_mouse.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import pathlib
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT
GAME = ("--game", "dark-queen")
SQUARE = 0xC9F0
START = (3, 2, 2)
CONF = {"sdl.mouse_emulation": "always"}
# The route to Caergoth's 3D view: Load, Tdqk, A, Begin Adventuring, the
# first texts, Palanthas, the palace, Leave (dqk_party.py's; the port passes
# the copy protection by itself).
ROUTE = "\\k\\ePLTAB\\r\\r\\rP\\r\\r\\r\\r\\rL\\r"
SPOTS = [((39, 50), "turn left"), ((96, 50), "turn right"), ((40, 50), "step ahead"),
         ((95, 50), "step ahead"), ((135, 90), "turn right"), ((0, 0), "turn left"),
         ((60, 99), "step ahead"), ((60, 100), "turn around"), ((135, 120), "turn around"),
         ((60, 159), "turn around"), ((136, 90), "nothing"), ((60, 160), "nothing"),
         ((136, 120), "nothing"), ((20, 150), "turn around")]
LOG = re.compile(r"^(?:step|turn): (-?\d+),(-?\d+) facing (\d)$", re.M)


class ViewBox(cokbox.Box):
    def point(self, x, y):
        """Move the pointer to the pixel x, y (no button)."""
        self._xdo("windowfocus", "--sync", self.window)
        self._xdo("mousemove", "--window", self.window, 2 * x, 2 * y)
        self.settle(0.4)


def hover(x, y):
    return f"\\m{x},{y},0.\\e"


def click(x, y):
    return f"\\m{x},{y},1."


def port_run(work, name, keys):
    """eclplay's last shot (the commands' menu waiting, its controls and the
    pointer as keys left them) and its log, for KEYS."""
    shots = work / f"port-{name}"
    if shots.exists():
        shutil.rmtree(shots)
    result = subprocess.run([str(cokbox.ECLPLAY), *GAME, "--start-menu", "--mouse", "--shots",
                             str(shots), "--keys", keys, str(dqk_lib.ASSETS)],
                            capture_output=True, text=True, cwd=ROOT, timeout=300)
    log = result.stdout + result.stderr
    (work / f"port-{name}.txt").write_text(log)
    last = sorted(shots.glob("*.bmp"))[-1]
    return cokbox.RGBScreen.load(last), log


def port_state(log):
    """The party's square and facing after the port's last step or turn."""
    found = LOG.findall(log)
    return tuple(int(v) for v in found[-1]) if found else START


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_view_mouse")
    out = work / "original"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    verdict = checklib.Verdict()

    # The port: before each spot, the route and the clicks before it, the
    # pointer there, Escape (the menu waits again: its last shot), and the
    # log of the steps and turns so far.
    port = []
    for i, (spot, _) in enumerate(SPOTS):
        keys = ROUTE + "".join(click(*s) for s, _ in SPOTS[:i]) + hover(*spot)
        port.append(port_run(work, f"spot-{i:02d}", keys))
    final = port_run(work, "final", ROUTE + "".join(click(*s) for s, _ in SPOTS))[1]

    with ViewBox(workdir=work / "box", game="dark-queen", verbose=opts.verbose, timeout=900,
                 extra_conf=CONF) as box:
        for _ in range(600):
            shot = box.screenshot()
            if (shot.width, shot.height) == (320, 200) and len(shot.colours()) >= 8:
                break
            box.settle(0.1)
        box.keys("\\e")
        box.settled(tolerance=64)
        freeze = (box.ds - 0x422E + 0x261B) * 16 + 0x05BE  # dqk_menus' freeze_cycles
        if box.read(freeze, 1) != b"\x55":
            raise cokbox.BoxError("261b:05be is not where it was expected")
        box.write(freeze, b"\xcb")
        dqk_lib.load_saved_game(box)
        box.keys("b")
        box.settled(tolerance=64)
        _, _, answer = dqk_lib.protection(box)
        box.keys(answer + "\\r")
        box.settled(tolerance=64)
        box.keys("\\r\\r\\rp\\r\\r\\r\\r\\rl\\r")
        box.settled(tolerance=64)
        for i, ((x, y), zone) in enumerate(SPOTS):
            state = tuple(box.ds_read(SQUARE, 3))
            verdict.value(f"the party's square and facing before the pointer's spot {i} "
                          f"({x},{y}) (DS:c9f0-c9f2)", state, port_state(port[i][1]))
            box.point(x, y)
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"spot-{i:02d}.png")
            verdict.screen(f"the commands' screen with the pointer at {x},{y} ({zone})", shot,
                           port[i][0], work=work)
            box.click(x, y)
            box.settled(tolerance=64)
        after = tuple(box.ds_read(SQUARE, 3))
        verdict.value("the party's square and facing after the last click (DS:c9f0-c9f2)",
                      after, port_state(final))
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

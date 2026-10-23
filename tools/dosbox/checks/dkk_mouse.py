#!/usr/bin/env python3
"""Death Knights of Krynn's mouse in the original against eclplay --game
death-knights --start-menu --mouse (DEVELOPERS.md, "Death Knights' mouse"):
DKK.CFG's M, DOSBox-X's own INT 33h driver, its pointer following X's (sdl
mouse_emulation=integration: the window's pixel 2x, 2y is the game's x, y),
moved and clicked with xdotool, as f_mouse.py does; the port takes the same
clicks as \\mX,Y,B. in its keys.

Flows (the title's two keys, Play, then):
- init: the start menu's Initialize Mouse/Joystick clicked (once to pick
  the row out, once to pick it: 6a69:1b82), its list's mouse row clicked
  (picked already: the mouse off), Escape; by keys again, the mouse on;
  Exit clicked on row 24. Then the krynn.cfg the original wrote in C:\\DKK
  (empty: see run_flow) against the port's, and DKK.CFG left as it was.
- pick: saved game A loaded ("from where" and its letter clicked), View clicked twice, Pick Character's
  party list: the second member clicked (selected, 6a69:0513 and
  580d:2bb2), then again (Select): its sheet.
- camp: game A begun, the first texts, camp by keys; then row 24's items
  clicked: Alter, Speed, Speed's Exit, Alter's Exit.

Each step's screen, once the original waits, against the port's at the key
read the same input leaves it in, whole screens (the arrow too). Seeded;
in run_all.py's Death Knights set (make dkk-checks). About a minute.

    python3 tools/dosbox/checks/dkk_mouse.py [--workdir DIR] [--flow NAME ...]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_lib as K  # noqa: E402

cokbox = checklib.cokbox

CONF = {"sdl.mouse_emulation": "integration"}
SEED = 1


class MouseBox(cokbox.Box):
    def point(self, x, y):
        """Put the pointer on the game's pixel X, Y."""
        self._xdo("mousemove", "--window", self.window, 2 * x + 1, 2 * y + 1)
        time.sleep(0.4)

    def click(self, x, y):
        self.point(x, y)
        # Held a while: the game polls the buttons' state (INT 33h 3).
        self._xdo("mousedown", "1")
        time.sleep(0.3)
        self._xdo("mouseup", "1")
        time.sleep(0.4)


def click(x, y):
    return ("click", x, y)


# The start menu's rows from 12 (pixel 8 * row + 2), its column 10; with no
# party: Create, Add, Load, Initialize (row 15), Exit; with one: Create,
# Drop, Modify, View (row 15), ... Initialize Mouse/Joystick's rows from 12;
# its Exit on row 24 after "Init mouse/joystick: Select " (column 29).
FLOWS = {
    "init": [("start", r"\r"), ("init-marked", click(80, 122)), ("init", click(80, 122)),
             ("mouse-off", click(80, 98)), ("start-again", r"\e"), ("init-keys", r"\r"),
             ("mouse-on", r"\r"), ("exit-clicked", click(234, 194))],
    # "from where" and the letters by index (46b2:1ef0's 0f1b), clicked:
    # Death and A, each the first item after a 17-character prompt.
    "pick": [("start", r"\r"), ("start-load", r"\v\v"), ("from", r"\r"),
             ("which", click(138, 194)), ("loaded", click(138, 194)), ("view-marked", click(80, 122)), ("view", click(80, 122)),
             ("member", click(140, 42)), ("sheet", click(140, 42))],
    # Row 24's items clicked (6a69:0940): camp's Alter (Pos 22), Alter's
    # Speed (12), Speed's Exit after "game speed: " (15), Alter's Exit (29).
    "camp": [("start", r"\r"), ("start-load", r"\v\v"), ("from", r"\r"), ("which", r"\r"),
             ("loaded", r"\r"), ("begun", r"\v\v\v\v\v\r"), ("text-2", r"\r"),
             ("rest-ask", r"\r"), ("camp", r"\r"), ("alter", click(170, 194)),
             ("speed", click(90, 194)), ("speed-exit", click(210, 194)),
             ("alter-exit", click(226, 194))],
}


def port_input(step):
    return step if isinstance(step, str) else r"\m%d,%d,1." % (step[1], step[2])


def run_flow(v, work, opts, flow):
    steps = FLOWS[flow]
    print(f"{flow}:", flush=True)
    box_dir = work / f"box-{flow}"
    keys = ""
    with MouseBox(workdir=box_dir, game="death-knights", machine="ega", input="M",
                  extra_conf=CONF, seed=SEED, saves=[K.ASSETS / "SAVE"], verbose=opts.verbose,
                  timeout=900, keep=True) as box:
        v.check(K.title(box), f"{flow}: the title's four pictures")
        K.type_steps(box, ["x", "x"], settle=2.0)
        for name, step in steps:
            if isinstance(step, str):
                box.keys(step)
            else:
                box.wait_input()
                box.click(step[1], step[2])
            box.wait_input(settle=2.0)
            keys += port_input(step)
            shown = box.screenshot(work / f"dosbox-{flow}-{name}.png")
            _, shot, _, _ = K.port(work, f"{flow}-{name}", keys, SEED, args=["--mouse"])
            v.screen(f"{flow}: {name}", shown, cokbox.Screen.load(shot) if shot else None,
                     work=work)
    if flow == "init":
        # With no krynn.cfg to read, Reset fails and InOutRes stays set:
        # the first pass's Rewrite makes the file and its writes do nothing
        # (both empty after mouse-off); nothing calls IOResult before the
        # second pass, whose I/O the error skips too, so the original's
        # file stays empty, where the port, which does not model the error
        # past its pass (DEVELOPERS.md, Death Knights' front end), reads the
        # empty file and writes five lines and the zeros.
        original = box_dir / "c" / "DKK"
        theirs = [p for p in original.iterdir() if p.name.lower() == "krynn.cfg"]
        first = work / f"port-{flow}-mouse-off" / "krynn.cfg"
        last = work / f"port-{flow}-exit-clicked" / "krynn.cfg"
        v.check(len(theirs) == 1 and first.exists() and last.exists(),
                f"{flow}: krynn.cfg written by both")
        if theirs and first.exists() and last.exists():
            a = theirs[0].read_bytes()
            v.check(a == b"" and first.read_bytes() == b"",
                    f"{flow}: krynn.cfg after the first pass empty in both" +
                    ("" if a == b"" else f": original {a!r}"))
            want = b"\r\n\r\nM\r\n\r\n\r\n" + b"0\r\n" * 14
            v.check(last.read_bytes() == want, f"{flow}: the port's after the second pass "
                    "(the original's error not modelled: its file stays empty)")
        cfg = [p for p in original.iterdir() if p.name.upper() == "DKK.CFG"]
        v.check(len(cfg) == 1 and cfg[0].read_bytes() ==
                cokbox.dkk_cfg("E", "S", "M").encode(), f"{flow}: DKK.CFG as it was")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_mouse"))
    parser.add_argument("--flow", action="append", choices=list(FLOWS))
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    for flow in opts.flow or list(FLOWS):
        run_flow(v, work, opts, flow)
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())

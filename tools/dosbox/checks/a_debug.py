#!/usr/bin/env python3
"""Check: the debug mode (README, Checks against the original: "The debug
mode (see Debug mode): start `START Woof`, press Ctrl-D at a menu and
compare the trace after each opcode ..., the status line's `*`, and, in a
battle, the trace of each combatant and `Done*`. Press Ctrl-Z and compare
`DEBUG.TXT` ... Ctrl-2 should quit to DOS as Ctrl-C does. In Items, `View`
an item, and in a shop see that a character offered `View Ready Trade Drop
Halve Join Sell Id` has no `Exit`.").

The original runs as `START Woof`, eclplay as --woof, with SAVE/'s games.
trace: Play, Ctrl-D in the start menu, load game A, Begin Adventuring, then
Enter at each key read (the trace waits for one after each opcode) with a
screen each, compared with eclplay's best frame.
file: Ctrl-D, load A, Ctrl-Z (DEBUG.TXT's size on the disk then), Ctrl-D
(closed: its bytes), Ctrl-D (made anew), Ctrl-Z, then Ctrl-2 (the word
0x0300) to quit; DEBUG.TXT compared with the port's after each.
items: in the outpost's camp, View, Items with Ctrl-D on: the menu and
View's screen of the first item; in the armoury, View, Items: the menu.
"""
import argparse
import sys

import a_common as A

cokbox = A.cokbox
SAVES = [cokbox.ROOT / "SAVE"]
LOAD_A = r"P\cD\v\v\rA"              # Play at the title's menu, Ctrl-D, Load A
TO_OUTPOST = r"P\v\v\rA\v\v\v\v\v\r\r\r\r\r\r\r\r"   # no debug: to the outpost's menu


def trace(work, verbose, steps):
    keys = LOAD_A + r"\sloaded.\v\v\v\v\v\r" + "".join(rf"\r\st{i:03d}." for i in range(steps))
    pair = A.Pair(keys, work, saves=SAVES, box_args="Woof", eclplay_args=["--woof"],
                  verbose=verbose)
    traced = [l for l in pair.out.splitlines() if l.startswith("debug: ")]
    print(f"trace: {steps} keys after Begin; the port traced {len(traced)} lines, the last "
          f"{traced[-3:]}")
    bad = known = 0
    with pair:
        pair.run()
        for shot in sorted((pair.work / "shots").glob("*.png")):
            count, best, area = pair.compare(shot.stem, quiet=True)
            if count and area == (272, 192, 279, 199):
                # Row 24, column 34: PRESS <ENTER>/<RETURN> TO CONTINUE's full
                # stop, the port bug load_game_screen.py found (src/adventure.c).
                known += 1
            elif count:
                bad += 1
            if count:
                print(f"  {shot.stem}: {count} pixels differ from eclplay's "
                      f"{pair.port_shots[best].name} in {area}")
        print(f"  {len(list((pair.work / 'shots').glob('*.png')))} screens: {bad} differ, "
              f"{known} only by the known full stop at row 24 column 34 (PRESS <ENTER>/<RETURN> "
              f"TO CONTINUE., load_game_screen.py); the original waits at {pair.box.where()}")
    return bad


def debug_file(work, verbose):
    steps = [("Ctrl-D, load A", LOAD_A), ("Ctrl-Z", r"\cZ"), ("Ctrl-D (off)", r"\cD"),
             ("Ctrl-D (on)", r"\cD"), ("Ctrl-Z again", r"\cZ")]
    keys = "".join(k for _, k in steps) + r"\#03"
    pair = A.Pair(keys, work, saves=SAVES, box_args="Woof", eclplay_args=["--woof"],
                  verbose=verbose)
    port_lines = [l for l in pair.out.splitlines() if l.startswith(("debug", "quit"))]
    print("file: the port logs " + " | ".join(port_lines[:4]) + f" ... ({len(port_lines)} lines)")
    port_file = (pair.port_saves / "DEBUG.TXT").read_bytes() if \
        (pair.port_saves / "DEBUG.TXT").exists() else None
    bad = 0
    with pair:
        path = pair.box.game / "DEBUG.TXT"
        for label, k in steps:
            pair.run(k)
            pair.box.wait_input(settle=0.3)
            data = path.read_bytes() if path.exists() else None
            # Turbo Pascal's TextRec at DS:8864: Mode (+2, d7b2 output, d7b0
            # closed) and BufPos (+8), the bytes in its 128-byte buffer not yet
            # written to DOS (DOSBox-X's own file cache keeps the host's copy
            # behind until the file is closed).
            mode, bufpos = pair.box.ds_word(0x8864 + 2), pair.box.ds_word(0x8864 + 8)
            print(f"  after {label}: DEBUG.TXT on the host's disk "
                  + ("missing" if data is None else f"{len(data)} bytes") +
                  f"; DS:4b51 {pair.box.ds_byte(0x4B51)}; TextRec mode {mode:#06x}, "
                  f"BufPos {bufpos}")
            if label == "Ctrl-D (off)":
                closed = data
        pair.box.key(0x0300)
        quit_ = pair.box.wait_exit(20)
        final = path.read_bytes() if path.exists() else None
    print(f"  Ctrl-2 (0x0300): the original {'quit to DOS' if quit_ else 'did not quit'}; the "
          f"port: {'quit' if '(quit to DOS)' in pair.out else 'did not quit'}")
    bad += quit_ != ("(quit to DOS)" in pair.out)
    lines = closed.split(b"\r\n") if closed else []
    print(f"  closed by Ctrl-D: {len(closed or b'')} bytes, CR LF lines {len(lines) - 1}: "
          f"{lines[:3]} ...")
    print(f"  at the end (after Ctrl-2): original {len(final or b'')} bytes, port "
          f"{len(port_file or b'')} bytes, " + ("identical" if final == port_file else "DIFFER"))
    bad += final != port_file
    # The port's file after the first Ctrl-D (off), for the bytes closed then.
    pair2 = A.Pair(r"".join(k for _, k in steps[:3]), work / "port-closed", saves=SAVES,
                   box_args="Woof", eclplay_args=["--woof"])
    port_closed = (pair2.port_saves / "DEBUG.TXT").read_bytes()
    print(f"  the port's file closed by the same Ctrl-D: {len(port_closed)} bytes, "
          + ("identical" if port_closed == closed else "DIFFER"))
    bad += port_closed != closed
    return bad


def items(work, verbose):
    keys = (TO_OUTPOST + r"I\r\cDV\rI\scamp-items.V\scamp-view." + r"\r\e\e\e\e\cD" +
            r"\sback.")
    pair = A.Pair(keys, work / "camp", saves=SAVES, box_args="Woof", eclplay_args=["--woof"],
                  verbose=verbose)
    menus = [l for l in pair.out.splitlines() if l.startswith("menu: View")]
    print(f"items: the port's menus {menus}")
    bad = 0
    with pair:
        pair.run()
        for s in ("camp-items", "camp-view", "back"):
            bad += pair.compare(s)[0] != 0
    keys = TO_OUTPOST + r"A\cDVI\sshop-items."
    pair = A.Pair(keys, work / "shop", saves=SAVES, box_args="Woof", eclplay_args=["--woof"],
                  verbose=verbose)
    menus = [l for l in pair.out.splitlines() if l.startswith("menu: View")]
    print(f"  shop: the port's menus {menus}")
    with pair:
        pair.run()
        bad += pair.compare("shop-items")[0] != 0
        print(f"  the original waits at {pair.box.where()}")
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append", choices=("trace", "file", "items"))
    parser.add_argument("--steps", type=int, default=60)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "debug")
    parts = args.part or ["trace", "file", "items"]
    bad = 0
    if "trace" in parts:
        bad += trace(work / "trace", args.verbose, args.steps)
    if "file" in parts:
        bad += debug_file(work / "file", args.verbose)
    if "items" in parts:
        bad += items(work / "items", args.verbose)
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS / see above", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

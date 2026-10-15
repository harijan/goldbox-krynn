#!/usr/bin/env python3
"""Check #13: the debug mode (README, Checks against the original: "The
debug mode (see Debug mode): start `START Woof`, press Ctrl-D at a menu and
compare the trace after each opcode ..., the status line's `*`, and, in a
battle, the trace of each combatant and `Done*`. Press Ctrl-Z and compare
`DEBUG.TXT` ... Ctrl-2 should quit to DOS as Ctrl-C does. In Items, `View`
an item, and in a shop see that a character offered `View Ready Trade Drop
Halve Join Sell Id` has no `Exit`.").

The original runs as `START Woof`, eclplay as --woof, with SAVE/'s games.
trace: Play, Ctrl-D in the start menu, load game A, Begin Adventuring, then
Enter at each key read (the trace waits for one after each opcode), each
screen against the port's at the same key read (a difference the port with
the known fixes does not have is the known `fullstop`), and the debug flag
DS:4b51 at the end.
file: Ctrl-D, load A, Ctrl-Z, Ctrl-D (off), Ctrl-D (on, made anew), Ctrl-Z,
then Ctrl-2 (the word 0x0300) to quit. At each step the flag and DEBUG.TXT's
TextRec (DS:8864: open or closed, BufPos, the bytes its 128-byte buffer
holds: DOSBox-X's own file cache keeps the host's copy behind until the
close) against the port's, and the screen; the file closed by the first
Ctrl-D (off) and the file after Ctrl-2 byte for byte against the port's.
items: in the outpost's camp, View, Items with Ctrl-D on: the menu and
View's screen of the first item; in the armoury, View, Items: the menu;
each screen against the port's.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

cokbox = A.cokbox
WOOF = dict(saves=A.SAVES, box_args="Woof", eclplay_args=["--woof"])
LOAD_A = r"P\cD\v\v\rA"              # Play at the title's menu, Ctrl-D, Load A
TO_OUTPOST = r"P\v\v\rA\v\v\v\v\v\r\r\r\r\r\r\r\r"   # no debug: to the outpost's menu
FM_OUTPUT = 0xD7B2                   # TextRec.Mode of a file open for output


def trace(v, work, verbose, steps):
    keys = LOAD_A + r"\sloaded.\v\v\v\v\v\r" + "".join(rf"\r\st{i:03d}." for i in range(steps))
    pair = A.Pair(keys, work, verbose=verbose, **WOOF)
    traced = [line for line in pair.out.splitlines() if line.startswith("debug: ")]
    print(f"trace: {steps} keys after Begin; the port traced {len(traced)} lines, the last "
          f"{traced[-3:]}")
    last = pair.marks()[-1]
    with pair:
        pair.run()
        flag = pair.box.ds_byte(0x4B51)
        print(f"  the original waits at {pair.box.where()}")
    pair.judge_all(v)
    v.value("the debug flag DS:4b51 at the end", flag,
            pair.probes()[last].get("debug"))


def debug_file(v, work, verbose):
    keys = LOAD_A + r"\sloaded.\cZ\sctrl-z.\cD\soff.\cD\son.\cZ\sctrl-z-2."
    pair = A.Pair(keys + r"\#03", work, verbose=verbose, **WOOF)
    probes = pair.probes()
    port_file = (pair.port_saves / "DEBUG.TXT").read_bytes() \
        if (pair.port_saves / "DEBUG.TXT").exists() else None
    state, closed = {}, None
    with pair:
        path = pair.box.game / "DEBUG.TXT"

        def during(mark):
            nonlocal closed
            mode, bufpos = pair.box.ds_word(0x8864 + 2), pair.box.ds_word(0x8864 + 8)
            state[mark] = (pair.box.ds_byte(0x4B51), int(mode == FM_OUTPUT), bufpos)
            data = path.read_bytes() if path.exists() else None
            print(f"  {mark}: DEBUG.TXT on the host's disk "
                  + ("missing" if data is None else f"{len(data)} bytes")
                  + f"; TextRec mode {mode:#06x}")
            if mark == "off":
                closed = data
        pair.steps(during)
        gone = pair.box.wait_exit(20)
        final = path.read_bytes() if path.exists() else None
    for mark in pair.marks():
        pair.judge(v, mark, what=f"file, screen {mark}")
        p = probes[mark]
        v.value(f"{mark}: the flag, DEBUG.TXT open, BufPos", state[mark],
                (p.get("debug"), p.get("open"), p.get("bufpos")))
    v.value("Ctrl-2 (0x0300) quits to DOS", gone, "(quit to DOS)" in pair.out)
    # The port's file closed by the same Ctrl-D (off).
    _, saves, _ = cokbox.eclplay(checklib.port_keys(keys[:keys.index(r"\soff.")]),
                                 work / "port-closed", shots=False, **pair.kw)
    v.value("DEBUG.TXT closed by Ctrl-D (off)", closed, (saves / "DEBUG.TXT").read_bytes())
    v.value("DEBUG.TXT after Ctrl-2", final, port_file)


def items(v, work, verbose):
    keys = (TO_OUTPOST + r"I\r\cDV\rI\scamp-items.V\scamp-view." + r"\r\e\e\e\e\cD" +
            r"\sback.")
    pair = A.Pair(keys, work / "camp", verbose=verbose, **WOOF)
    print(f"items: the port's menus "
          f"{[line for line in pair.out.splitlines() if line.startswith('menu: View')]}")
    with pair:
        pair.run()
    pair.judge_all(v)
    pair = A.Pair(TO_OUTPOST + r"A\cDVI\sshop-items.", work / "shop", verbose=verbose, **WOOF)
    print(f"  shop: the port's menus "
          f"{[line for line in pair.out.splitlines() if line.startswith('menu: View')]}")
    with pair:
        pair.run()
    pair.judge_all(v)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append", choices=("trace", "file", "items"))
    parser.add_argument("--steps", type=int, default=60)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "debug")
    parts = args.part or ["trace", "file", "items"]
    v = checklib.Verdict()
    if "trace" in parts:
        trace(v, work / "trace", args.verbose, args.steps)
    if "file" in parts:
        debug_file(v, work / "file", args.verbose)
    if "items" in parts:
        items(v, work / "items", args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #131: the selection kept in DS:43bf, removed in the training hall
(docs/checks.md, Checks against the original; docs/adventure.md, "The
party").

The outpost's HALL (ECL1 block 17's location vector, PROGRAM 0) opens the
start menu inside the block's vectors, with DS:43bf the selection kept as
they began (2fd3:3b47). Remove Character from Party on the first, SIR
STRONGSWORD, frees his record (4def:3b0a) and leaves DS:43bf naming it;
the original would restore it as the selection where the vectors end, but
Begin Adventuring goes back to the outpost's menu, and LEAVE takes the
party out (NEWECL), whose block's entry keeps the selection again: the
freed record is never restored. The port marks DS:43bf freed and stops
only where it is restored (src/ecl.h); here it goes on.

Game A loaded in the original and in eclplay --start-menu, seeded the same:
the hall, Remove of the first, Begin, LEAVE; at each the screen against the
port's at the same key read, and DS:43bf and the selection (DS:6096) read
in the original's memory against the party's records (DS:609a's list).
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import e_common as E  # noqa: E402

cokbox = E.cokbox
SEED = 1
SELECTION, KEPT = 0x6096, 0x43BF
TO_HALL = r"\kxP\v\v\rA\v\v\v\v\v\r" + r"\r\r\r\r\r\r\rH"
STEPS = [(TO_HALL, "hall"),                    # the start menu, from the HALL
         (r"\v\v\v\v\v\v\v\r", "remove"),      # Remove Character from Party: Select
         (r"\r", "removed"),                   # SIR STRONGSWORD removed (his file saved)
         (r"\v\v\r", "begun"),                 # Begin Adventuring: the outpost's menu
         ("L", "left")]                        # LEAVE: near an outpost, enter?


def far(box, off):
    o, s = struct.unpack("<HH", box.ds_read(off, 4))
    return s * 16 + o


def records(box):
    out, at = [], far(box, E.PARTY)
    while at:
        out.append(at)
        o, s = struct.unpack("<HH", box.read(at + 0x17F, 4))
        at = s * 16 + o
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "e-freed-selection")
    checklib.need_saves()
    v = checklib.Verdict()
    keys = E.marked(STEPS)
    pair = E.Pair(keys, work / "pair", saves=[E.SAVE], args=["--seed", str(SEED)])
    seen = {}
    with cokbox.Box(workdir=work / "box", saves=[E.SAVE], verbose=args.verbose, timeout=600,
                    seed=SEED) as box:
        first = None
        for typed, name in STEPS:
            box.keys(typed)
            box.wait_input(settle=0.5)
            pair.judge(v, name, box.screenshot(work / f"{name}.png"))
            party = records(box)
            first = first or party[0]
            seen[name] = (far(box, KEPT), far(box, SELECTION), party)
            print(f"{name}: DS:43bf {seen[name][0]:#x}, DS:6096 {seen[name][1]:#x}, party "
                  f"{[hex(r) for r in party]}")
    kept, sel, party = seen["begun"]
    v.check(kept == first and kept not in party and sel in party,
            "begun: DS:43bf still names the record removed, the selection another",
            f"{seen['begun']}")
    kept, sel, party = seen["left"]
    v.check(kept == sel and sel in party,
            "left: DS:43bf kept again from the selection, a member: the freed record never "
            "restored", f"{seen['left']}")
    for name in ("begun", "left"):
        v.check(E.port_error(pair.text(name)) is None, f"{name}: the port goes on",
                E.port_error(pair.text(name)) or "")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

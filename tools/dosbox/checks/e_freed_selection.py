#!/usr/bin/env python3
"""Check #131: the selection kept in DS:43bf, removed in the training hall
(docs/checks.md, Checks against the original; docs/adventure.md, "The
party").

The outpost's HALL (ECL1 block 17's location vector, PROGRAM 0) opens the
start menu inside the block's vectors, with DS:43bf the selection kept as
they began (2fd3:3b47). Remove Character from Party on the first, SIR
STRONGSWORD, frees his record (4def:3b0a) and leaves DS:43bf naming it;
the original would restore it as the selection where the vectors end, but
Begin Adventuring goes back to the outpost's menu. LEAVE takes the party
out (NEWECL), whose block's entry keeps the selection again: the freed
record is never restored (flow leave). NEXT, VAULT, a Deposit (whose
LOAD CHARACTERs set DS:43ba), Exit, PREV and INN reach PROGRAM 9, which
restores it: the camp then shows nobody selected (6346:07ba only compares
the pointer) and Up selects nil (546c:3334 finds no member before it), as
the port's "freed selection" does (src/ecl.h); what reads its bytes (View,
Down) stops the port (flow inn).

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
BEGUN = [(TO_HALL, "hall"),                    # the start menu, from the HALL
         (r"\v\v\v\v\v\v\v\r", "remove"),      # Remove Character from Party: Select
         (r"\r", "removed"),                   # SIR STRONGSWORD removed (his file saved)
         (r"\v\v\r", "begun")]                 # Begin Adventuring: the outpost's menu
FLOWS = {
    "leave": BEGUN + [("L", "left")],          # LEAVE: near an outpost, enter?
    # NEXT, VAULT, Deposit 1 (its LOAD CHARACTERs set DS:43ba), Exit, PREV,
    # INN: PROGRAM 9 restores the freed record, and the camp shows nobody
    # selected; Up finds no member before it: nil.
    "inn": BEGUN + [("N", "next"), ("V", "vault"), (r"\r", "vault-menu"), ("D", "deposit"),
                    (r"1\r", "deposited"), (r"\r", "vault-again"), ("E", "vault-left"),
                    (r"\r", "outpost"), ("P", "prev"), ("I", "inn"), (r"\r", "camp"),
                    (r"\^", "up")],
}


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


def run_flow(v, work, steps, args):
    keys = E.marked(steps)
    pair = E.Pair(keys, work / "pair", saves=[E.SAVE], args=["--seed", str(SEED)])
    seen = {}
    with cokbox.Box(workdir=work / "box", saves=[E.SAVE], verbose=args.verbose, timeout=600,
                    seed=SEED) as box:
        for typed, name in steps:
            box.keys(typed)
            box.wait_input(settle=0.5)
            pair.judge(v, name, box.screenshot(work / f"{name}.png"), f"{work.name}: {name}")
            seen[name] = (far(box, KEPT), far(box, SELECTION), records(box))
            print(f"{work.name} {name}: DS:43bf {seen[name][0]:#x}, DS:6096 "
                  f"{seen[name][1]:#x}, party {[hex(r) for r in seen[name][2]]}")
    for typed, name in steps[3:]:
        v.check(E.port_error(pair.text(name)) is None, f"{work.name}: {name}: the port goes on",
                E.port_error(pair.text(name)) or "")
    return seen


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "e-freed-selection")
    checklib.need_saves()
    v = checklib.Verdict()
    for flow, steps in FLOWS.items():
        seen = run_flow(v, work / flow, steps, args)
        kept, sel, party = seen["begun"]
        first = seen["hall"][2][0]
        v.check(kept == first and kept not in party and sel in party,
                f"{flow}: begun: DS:43bf still names the record removed, the selection "
                "another", f"{seen['begun']}")
        if flow == "leave":
            kept, sel, party = seen["left"]
            v.check(kept == sel and sel in party,
                    "leave: DS:43bf kept again from the selection, a member: the freed "
                    "record never restored", f"{seen['left']}")
        else:
            kept, sel, party = seen["camp"]
            v.check(sel == first and sel not in party,
                    "inn: the camp's selection is the freed record (PROGRAM's restore), "
                    "nobody highlighted", f"{seen['camp']}")
            v.check(seen["up"][1] == 0, "inn: Up selects nil (546c:3334 finds none before it)",
                    f"{seen['up']}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #24 (README, Checks against the original): "Space during the
computer's turns gives the party back: the turn it interrupts is given up
(initiative 0x14 with no DS:72ce decrement in 3afb:1200), and the next
party member's turn is the player's."

Game A, the caravan's four baaz (seed 7). At ISTAN HORBIN's first turn,
Alt-Q (the whole party to the computer; the turn given back, 200 ms, then
the computer runs ISTAN's turn again); Space is put in the BIOS's buffer
during the 200 ms, after the menu's flush, so that the computer's poll
(3afb:1200) of ISTAN's turn reads it. eclplay types it the same way
(`\\[ \\]`, typed ahead at the next wait, the 200 ms). Compared: the screen
the original waits at next (KAL's menu in eclplay), the turn's entry
(DS:72ce), and the party's +0x18b (the computer's control) and ISTAN's
combat record in memory.
"""
import argparse
import sys
import time

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--delay", type=float, default=0.08,
                        help="seconds after Alt-Q is read before Space goes in")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("space", args.workdir)
    route = d_lib.LOAD_A + d_lib.TO_CARAVAN
    lines, shots = d_lib.port(route + r"\@q\[ \]", work / "port", args.seed)
    turns = [l for l in lines if l.startswith("turn:")]
    print("port:", turns[:5])
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        d_lib.run_steps(b, [("istan", route)], shots, work)
        b.key(0x1000)               # Alt-Q, read by the menu
        time.sleep(args.delay)      # past the menu's flush, within the 200 ms
        b.push_key(0x3920)          # Space
        b._until(lambda: not b.buffer(), 30, "the computer's poll to take Space", 0.005)
        b.wait_input(settle=1.0)
        shown = b.screenshot(work / "next.png")
        count = d_lib.compare(shown, shots, "next", work)
        idx, order = d_lib.turn_entry(b)
        cs = d_lib.combatants(b)
        auto = {c["name"]: c["raw"][0x18B] for c in cs[:6]}
        istan = d_lib.combat_record(b, d_lib.by_name(cs, "ISTAN HORBIN")[0]["rec"])
        print(f"original: entry {idx} ({order[idx - 1]}), +0x18b {auto}, ISTAN's "
              f"initiative (+0x03) {istan[3]:#x}; where {b.where()}")
    ok = count == 0 and order[idx - 1] == "KAL" and not any(auto.values()) and \
        turns[1:3] == ["turn: ISTAN HORBIN (initiative 19)", "turn: KAL (initiative 7)"]
    status = d_lib.verdict(ok, "#24 Space at the computer's poll in ISTAN's turn run again "
                           "after Alt-Q: the party back to the player, ISTAN's turn given "
                           "up, KAL's menu next (screen equal to eclplay's)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

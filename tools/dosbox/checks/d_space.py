#!/usr/bin/env python3
"""Check #24 (docs/checks.md, Checks against the original): "Space during the
computer's turns gives the party back: the turn it interrupts is given up
(initiative 0x14 with no DS:72ce decrement in 3afb:1200), and the next
party member's turn is the player's."

Game A, the caravan's four baaz (seed 7). At ISTAN HORBIN's first turn,
Alt-Q (the whole party to the computer; the turn given back, 200 ms, then
the computer runs ISTAN's turn again); Space is put in the BIOS's buffer
during the 200 ms, after the menu's flush, so that the computer's poll
(3afb:1200) of ISTAN's turn reads it. eclplay types it the same way
(`\\[ \\]`, typed ahead at the next wait, the 200 ms). Compared with the port's
at the same point: the screen the original waits at next (KAL's menu), the
memory (d_lib.compare_state: whose turn it is, DS:72ce, among it), the
party's +0x18b (the computer's control) and ISTAN's initiative (combat
record +0x03).
"""
import argparse
import sys
import time

import checklib
import d_lib

# It judges what the original shows as it comes: its boxes at their cycles
# (when it runs, not when another check imports it).
if __name__ == "__main__":
    checklib.cokbox.REAL_TIME = True

ROUTE = d_lib.LOAD_A + d_lib.TO_CARAVAN


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--delay", type=float, default=0.08,
                        help="seconds after Alt-Q is read before Space goes in")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-space")
    port = d_lib.Port(work / "eclplay", d_lib.marked([("istan", ROUTE), ("next", r"\@q\[ \]")]),
                      args.seed)
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        shown = d_lib.step(b, ROUTE, "istan", work)
        d_lib.screen(v, "screen istan", shown, port, "istan", work)
        d_lib.compare_state(v, b, port, "istan")
        b.key(0x1000)               # Alt-Q, read by the menu
        time.sleep(args.delay)      # past the menu's flush, within the 200 ms
        b.push_key(0x3920)          # Space
        b._until(lambda: not b.buffer(), 30, "the computer's poll to take Space", 0.005)
        b.wait_input(settle=1.0)
        shown = b.screenshot(work / "next.png")
        d_lib.screen(v, "screen next", shown, port, "next", work)
        o = d_lib.compare_state(v, b, port, "next")
        cs = d_lib.combatants(b)
        auto = [c["raw"][0x18B] for c in cs[:6]]
        given_up = d_lib.by_name(cs, "ISTAN HORBIN")[0]["combat"][3]
    d_lib.value(v, "the party's Auto (+0x18b) after Space", auto, port, "next",
                lambda s: [c["raw"][0x18B] for c in d_lib.port_combatants(s)[:6]])
    d_lib.value(v, "ISTAN's initiative (+0x03) after Space", given_up, port, "next",
                lambda s: d_lib.by_name(d_lib.port_combatants(s), "ISTAN HORBIN")[0]["combat"][3])
    turns = [line for line in port.lines if line.startswith("turn:")]
    print("port:", turns[:5])
    v.check(o["turn"] == "KAL" and not any(auto) and given_up == 0x14,
            "#24 the original: the party back to the player (+0x18b 0), ISTAN's turn given up "
            "(+0x03 0x14), KAL's menu")
    v.check(turns[1:3] == ["turn: ISTAN HORBIN (initiative 19)", "turn: KAL (initiative 7)"],
            "#24 the port: ISTAN's turn run again at initiative 19, then KAL's")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

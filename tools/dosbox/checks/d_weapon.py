#!/usr/bin/env python3
"""Check #91 (docs/checks.md, Checks against the original): "Kill a baaz with a
sword a square away: on a high roll the killer "loses his weapon", and has
it back after the fight."

Game A, the caravan's baaz, seed 3 (found with eclplay): Alt-Q at the first
turn, the battle played by the computer; SIR STRONGSWORD's Long Sword kills
a baaz beside him and he "loses his weapon" (effect 0x43). The original's
lost weapons (DS:609e, a far pointer) and SIR STRONGSWORD's item count
(+0x142) are sampled as the battle runs; the screens and the memory
(d_lib.compare_state: the items' count among it) at the end of the round
and after it (Continue Battle No, the results, View, Items: the sword back,
not readied) against the port's at the same point.
"""
import argparse
import sys

import checklib
import d_lib

ROUTE = d_lib.LOAD_A + d_lib.TO_CARAVAN
AFTER = [("continue", r"\r"), ("results", r"\r"), ("view", "V"), ("items", "I")]


def lost_weapon(b, sir):
    """(a weapon lost in combat, DS:609e set; SIR STRONGSWORD's items +0x142)."""
    return b.ds_dword(0x609E) != 0, b.read(sir + 0x142, 1)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=3)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-weapon")
    port = d_lib.Port(work / "eclplay", d_lib.marked([("turn", ROUTE), ("fought", r"\@q")] + AFTER),
                      args.seed)
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        shown = d_lib.step(b, ROUTE, "turn", work)
        d_lib.screen(v, "screen turn", shown, port, "turn", work)
        d_lib.compare_state(v, b, port, "turn")
        sir = d_lib.by_name(d_lib.combatants(b), "SIR STRONGSWORD")[0]["rec"]
        seen = []

        def sample():
            state = lost_weapon(b, sir)
            if not seen or state != seen[-1]:
                seen.append(state)

        b.push_key(0x1000)  # Alt-Q: the computer plays to "Continue Battle:"
        b._last_polls, b._streak = b.hook("polls"), 0
        b._until(lambda: sample() or b.waiting(), 300, "the battle on Auto", 0.01)
        b.wait_input(settle=1.0)
        shown = b.screenshot(work / "fought.png")
        d_lib.screen(v, "screen fought", shown, port, "fought", work)
        d_lib.compare_state(v, b, port, "fought")
        for name, keys in AFTER:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            d_lib.compare_state(v, b, port, name)
        after = lost_weapon(b, sir)
    print("original, (a weapon lost at DS:609e, SIR STRONGSWORD's items) as the battle ran:",
          seen, "; after it", after)
    k = port.lines.index("print: loses his weapon") if "print: loses his weapon" in port.lines \
        else None
    v.check(k is not None, "#91 the port: SIR STRONGSWORD \"loses his weapon\"")
    v.check(seen[:1] == [(False, 3)] and (True, 2) in seen and after == (False, 3),
            "#91 the original: the Long Sword lost as the baaz dies (DS:609e set, items 3 -> "
            "2), back after the fight (3)")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

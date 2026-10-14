#!/usr/bin/env python3
"""Check #91 (README, Checks against the original): "Kill a baaz with a
sword a square away: on a high roll the killer "loses his weapon", and has
it back after the fight."

Game A, the caravan's baaz, seed 3 (found with eclplay): Alt-Q at the first
turn, the battle played by the computer; SIR STRONGSWORD's Long Sword kills
a baaz beside him and he "loses his weapon" (effect 0x43). The original's
lost weapons (DS:609e, a far pointer) and SIR STRONGSWORD's item count
(+0x142) are sampled as the battle runs; after it, Continue Battle No, the
results, View, Items: his items' screen is compared with eclplay's (the
sword back, not readied).
"""
import argparse
import sys
import time

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=3)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("weapon", args.workdir)
    route = d_lib.LOAD_A + d_lib.TO_CARAVAN
    steps = [("continue", r"\r"), ("results", r"\r"), ("view", "V"), ("items", "I")]
    lines, shots = d_lib.port(route + r"\@q" + "".join(k for _, k in steps), work / "port",
                              args.seed)
    k = lines.index("print: loses his weapon")
    print("port:", [l for l in lines[k - 8:k + 1] if not l.startswith("sound")])
    print("port's items after:", [l for l in lines if l.startswith("item:  ")][:3])
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        res = d_lib.run_steps(b, [("turn", route)], shots, work)
        sir = d_lib.by_name(d_lib.combatants(b), "SIR STRONGSWORD")[0]["rec"]
        seen = []
        b.push_key(0x1000)  # Alt-Q
        b._last_polls, b._streak = b.hook("polls"), 0
        end = time.monotonic() + 300
        while time.monotonic() < end and not b.waiting():
            state = (b.ds_dword(0x609E) != 0, b.read(sir + 0x142, 1)[0])
            if not seen or state != seen[-1]:
                seen.append(state)
            time.sleep(0.01)
        print("original, (a weapon lost at DS:609e, SIR STRONGSWORD's items) as the "
              "battle ran:", seen)
        res.update(d_lib.run_steps(b, steps, shots, work))
        items = b.read(sir + 0x142, 1)[0]
        print("original: SIR STRONGSWORD's items after the fight:", items,
              "; DS:609e", hex(b.ds_dword(0x609E)))
    ok = (True, 2) in seen and seen[0] == (False, 3) and items == 3 and \
        all(c == 0 for c, _ in res.values())
    status = d_lib.verdict(ok, "#91 the baaz's death takes SIR STRONGSWORD's Long Sword "
                           "(items 3 -> 2, DS:609e set), back after the fight (3, not "
                           "readied); every screen equal to eclplay's")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check #130: Molly's Hoopak unreadied outside combat (docs/checks.md,
Checks against the original; docs/party.md, "Spell effects").

The Hoopak's power gives effect 0x07, whose handler (3f44:0208) runs as
the effect goes (3f44:3888) and reads the target through the combat
record (+0x183): outside combat nil before any battle (0000:000a, the
interrupt table) or the record the last battle's end freed. It sets the
THAC0 and an attack's amount, which the items menu's recompute (6346:0d20)
and the next attack set again, and rolls a d6 or a d4 by the distance to
that target: one Random either way. The port (src/effect.c's kender) rolls
the die and keeps nothing else.

Game C (Throtl's street, the port's save after the gate fight) loaded in
the original and in eclplay --start-menu, seeded the same: MOLLY selected,
View, Items, Ready on the Hoopak (unreadied), then on the Ring Mail (no
power). At each the screen against the port's at the same key read, and
RandSeed (DS:43ae): one step for the Hoopak, none for the Ring Mail, in
both, and the same in both.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import e_common as E  # noqa: E402

cokbox = E.cokbox
SEED = 1
STEPS = [(r"\v\v\vVI", "items"),      # MOLLY: View, Items (the Hoopak first)
         ("R", "hoopak"),             # unreadied: 0x07 goes, its handler runs
         (r"\vR", "ringmail")]        # unreadied: no power, no handler


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "e-kender")
    checklib.need_saves()
    v = checklib.Verdict()
    saves = E.make_c(work)
    load = E.LOAD.format("C")
    pair = E.Pair(load + E.marked(STEPS), work / "pair", saves=[saves],
                  args=["--seed", str(SEED)])
    seeds = {}
    with cokbox.Box(workdir=work / "box", saves=[saves], verbose=args.verbose, timeout=600,
                    seed=SEED) as box:
        box.keys(load)
        box.wait_input(settle=0.5)
        for keys, name in STEPS:
            box.keys(keys)
            box.wait_input(settle=0.5)
            pair.judge(v, name, box.screenshot(work / f"{name}.png"))
            seeds[name] = (box.ds_dword(E.RANDSEED), E.port_seed(pair.text(name)))
            print(f"{name}: RandSeed original {seeds[name][0]:#010x}, port "
                  f"{seeds[name][1] if seeds[name][1] is None else hex(seeds[name][1])}")
    for side, i in (("original", 0), ("port", 1)):
        items, hoopak, ring = (seeds[n][i] for n in ("items", "hoopak", "ringmail"))
        v.check(hoopak == E.next_seed(items),
                f"{side}: the Hoopak unreadied rolls one die (0x07's handler, 3f44:0208)",
                f"{items} -> {hoopak}")
        v.check(ring == hoopak, f"{side}: the Ring Mail unreadied rolls none",
                f"{hoopak} -> {ring}")
    for name in seeds:
        v.value(f"RandSeed at {name}", seeds[name][0], seeds[name][1])
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

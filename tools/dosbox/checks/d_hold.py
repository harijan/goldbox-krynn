#!/usr/bin/env python3
"""Check #120 (DEVELOPERS.md, Checks against the original): "Cast Hold Person at
three targets: a missile should fly from the caster to the first two, none
to the last picked; pick the caster: "Cannot target yourself"."

The port writes game G: game A saved in the outpost inn's camp, KAL's .SAV
then patched so that his third Sleep (+0x54) is Hold Person (0x17, the
cleric's; 0x15 -> 0x17 keeps the list sorted). Both load G and meet the
caravan's baaz, seed 7: ISTAN Done/Quit, KAL casts Hold Person ("Begins
Casting", a third of 5), and at his next turn aims: Target on himself
("Cannot target yourself"), then three picks (Next Target, Next Next
Target, Next x3 Target: ISTAN, GARIN, SIR STRONGSWORD, the baaz being out
of range). Target on himself and the last Target with a burst of screenshots (the
speed poked to 30), each against the port's frames at its pauses and map
shows in order (the refusal shows during a pause; the missiles' flights
are 30 ms steps, each a pause); the screen and memory (the effects among
it: GARIN held, 0x34) at each aim and after the cast against the port's.
"""
import argparse
import sys

import checklib
import d_lib

STEPS = [("aim", d_lib.load("G") + r"Lnm9\r" + "DQ" + r"C\r"), ("yourself", "T"),
         ("pick1", "NT"), ("pick2", "NNT"), ("last", "NNN"), ("after", "T")]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--burst", type=float, default=25)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-hold")
    saves = d_lib.make_save("G", work / "save")
    d_lib.patch_record(saves, "G", 5, 0x54, [0x17])
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed, saves=[saves])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        for (before, _), (name, keys) in zip([(None, None)] + STEPS, STEPS):
            if name == "yourself":
                # Target on himself: the refusal shows for a moment, then the aim again.
                refusal, _, _ = d_lib.burst_step(v, b, port, before, name, ord("t") | 0x1400,
                                                 work, "yourself", 4, speed=4)
            elif name == "after":
                burst, _, _ = d_lib.burst_step(v, b, port, before, name, ord("t") | 0x1400,
                                               work, "cast", args.burst)
            else:
                shown = d_lib.step(b, keys, name, work)
                d_lib.screen(v, f"screen {name}", shown, port, name, work)
            o = d_lib.compare_state(v, b, port, name)
    refused = sorted({t for s in refusal for t in d_lib.screen_text(s).values()
                      if "TARGET" in t and "YOURSELF" in t})
    print("original after Target on himself:", refused)
    print("original's panel in the cast:", d_lib.panel(burst, range(10, 14)))
    k = port.lines.index("cast: KAL: Hold Person") if "cast: KAL: Hold Person" in port.lines \
        else None
    print("port:", [line for line in port.lines[k:k + 30] if line.startswith("print:")]
          if k is not None else None)
    v.check(refused, "#120 the original says \"CANNOT TARGET YOURSELF\" at Target on himself")
    held = {f[1]: [e[0] for e in f[7]] for f in o["fighters"][:6]}
    v.check([n for n, e in held.items() if 0x34 in e] == ["GARIN "],
            "#120 the original: GARIN held (0x34), ISTAN and SIR unaffected", f"{held}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

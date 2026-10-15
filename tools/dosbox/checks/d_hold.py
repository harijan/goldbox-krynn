#!/usr/bin/env python3
"""Check #120 (README, Checks against the original): "Cast Hold Person at
three targets: a missile should fly from the caster to the first two, none
to the last picked; pick the caster: "Cannot target yourself"."

The port writes game G: game A saved in the outpost inn's camp, KAL's .SAV
then patched so that his third Sleep (+0x54) is Hold Person (0x17, the
cleric's; 0x15 -> 0x17 keeps the list sorted). Both load G and meet the
caravan's baaz, seed 7: ISTAN Done/Quit, KAL casts Hold Person ("Begins
Casting", a third of 5), and at his next turn aims: Target on himself
("Cannot target yourself"), then three picks (Next Target, Next Next
Target, Next x3 Target: ISTAN, GARIN, SIR STRONGSWORD, the baaz being out
of range). The last Target with a burst of screenshots (the speed poked to
30): compared with eclplay's frames at its pauses (the missiles' flights
are 30 ms steps, each a pause); and the screen at each aim.
"""
import argparse
import sys

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--burst", type=float, default=25)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("hold", args.workdir)
    saves = d_lib.make_save("G", work / "save")
    d_lib.patch_record(saves, "G", 5, 0x54, [0x17])
    steps = [("aim", d_lib.load("G") + r"Lnm9\r" + "DQ" + r"C\r"), ("yourself", "T"),
             ("pick1", "NT"), ("pick2", "NNT"), ("last", "NNN")]
    lines, shots = d_lib.port("".join(k for _, k in steps) + "T", work / "port", args.seed,
                              saves=[saves], pauses=True)
    k = lines.index("cast: KAL: Hold Person")
    print("port:", [l for l in lines[k:k + 30] if l.startswith("print:")])
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        res = d_lib.run_steps(b, steps[:1], shots, work)
        # Target on himself: the refusal shows for a moment, then the aim again.
        b.push_key(ord("t") | 0x1400)
        refusal = d_lib.burst(b, 4, work=work, prefix="yourself")
        refused = sorted({t for s in refusal for t in d_lib.screen_text(s).values()
                          if "TARGET" in t and "YOURSELF" in t})
        print("original after Target on himself:", refused)
        res.update(d_lib.run_steps(b, [("yourself", "")] + steps[2:], shots, work))
        speed = b.ds_byte(0x4B38)
        b.ds_write(0x4B38, bytes([30]))
        b.push_key(ord("t") | 0x1400)
        burst = d_lib.burst(b, args.burst, work=work, prefix="cast")
        b.ds_write(0x4B38, bytes([speed]))
        cs = d_lib.combatants(b)
        held = {c["name"]: [e for e in d_lib.effects(b, c["rec"])] for c in cs[:6]}
    print("original: the effects after the cast:", held)
    frames = d_lib.frames_after(shots, res["last"][1], 200)
    matched = [getattr(frames.get(s.pixels), "name", None) for s in burst]
    print("original: the burst's screens equal to port frames:", matched)
    texts = [d_lib.screen_text(s, range(10, 14), 23, 39) for s in burst]
    said = []
    for t in texts:
        for r, line in sorted(t.items()):
            if (r, line.strip(" ?")) not in said:
                said.append((r, line.strip(" ?")))
    print("original's panel:", said)
    ok = bool(refused) and all(c == 0 for c, _ in res.values()) and sum(1 for m in matched if m) >= 3 and \
        any(0x34 in [e[0] for e in v] for v in held.values())
    status = d_lib.verdict(ok, "#120 Hold Person: \"CANNOT TARGET YOURSELF\" on Target on "
                           "himself; the screens at each aim equal eclplay's; the cast's screens equal its frames at "
                           "pauses (the flights to the first two picks among them)")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

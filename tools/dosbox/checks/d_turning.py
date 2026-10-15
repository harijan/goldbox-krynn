#!/usr/bin/env python3
"""Check #129 (DEVELOPERS.md, Checks against the original): "Cast Magic Missile at
a creature turning spells (effect 0x47) until it "reflects the spell": the
caster should take the damage."

The port writes game F: game A with SIR STRONGSWORD given 0x47 (spell
turning), saved in the outpost inn's camp. Both load F and meet the
caravan's baaz, seed 8 (found with eclplay): ISTAN Done/Quit, GARIN casts
Magic Missile at SIR (Next once, Target): SIR "reflects the spell", GARIN
"takes 2 points of damage from Magic". The speed poked to 30 and a burst of
screenshots through the cast; the screens compared with the port's frames
at its key reads and pauses in order and the panel read; the screen and the
memory (d_lib.compare_state: GARIN's and SIR's hit points among them) at the
aim and at the next wait against the port's there.
"""
import argparse
import sys

import checklib
import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=8)
    parser.add_argument("--burst", type=float, default=20)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-turning")
    saves = d_lib.make_save("F", work / "save", args=["--effect", "1,47,0,0"])
    aim = d_lib.load("F") + r"Lnm9\r" + "DQ" + r"C\^\rN"
    port = d_lib.Port(work / "eclplay", d_lib.marked([("aim", aim), ("after", "T")]), args.seed,
                      saves=[saves])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        shown = d_lib.step(b, aim, "aim", work)
        d_lib.screen(v, "screen aim", shown, port, "aim", work)
        before = d_lib.compare_state(v, b, port, "aim")
        shots, matched, _ = d_lib.burst_step(v, b, port, "aim", "after", ord("t") | 0x1400, work,
                                             "cast", args.burst)
        after = d_lib.compare_state(v, b, port, "after")
    panel = d_lib.panel(shots, range(10, 16))
    print("original's panel:", panel)
    k = port.lines.index("print: reflects the spell") if "print: reflects the spell" in \
        port.lines else None
    print("port:", port.lines[k - 1:k + 3] if k else None)
    v.check(k is not None and port.lines[k - 1] == "print: SIR STRONGSWORD",
            "#129 the port: SIR STRONGSWORD \"reflects the spell\"")
    reflect = [m for m, s in zip(matched, shots) if any(
        "REFLECTS" in d_lib.read_text(s, r, 23, 39)[0] for r in (11, 12))]
    # Each matched (one caught while drawing the next, N~M, passes the burst's
    # rule), and at least one exactly a port frame.
    v.check(reflect and all(reflect) and any("~" not in m for m in reflect),
            "#129 the original's screens with \"REFLECTS\" match the port's frames, one "
            "exactly", f"{reflect}")
    hp = {o: {f[1]: f[5] for f in st["fighters"]} for o, st in (("before", before),
                                                                ("after", after))}
    lost = {n: hp["before"][n] - hp["after"][n] for n in ("GARIN ", "SIR STRONGSWORD")}
    v.check(lost == {"GARIN ": 2, "SIR STRONGSWORD": 0},
            "#129 the original: the caster GARIN takes the 2 points, SIR none", f"{lost}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #129 (README, Checks against the original): "Cast Magic Missile at
a creature turning spells (effect 0x47) until it "reflects the spell": the
caster should take the damage."

The port writes game F: game A with SIR STRONGSWORD given 0x47 (spell
turning), saved in the outpost inn's camp. Both load F and meet the
caravan's baaz, seed 8 (found with eclplay): ISTAN Done/Quit, GARIN casts
Magic Missile at SIR (Next once, Target): SIR "reflects the spell", GARIN
"takes 2 points of damage from Magic". The speed poked to 30 and a burst of
screenshots through the cast; the screens compared with the port's frames
at its pauses and the panel read; GARIN's and SIR's hit points in memory.
"""
import argparse
import sys

import d_lib


def hp(b, name):
    return d_lib.by_name(d_lib.combatants(b), name)[0]["hp"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=8)
    parser.add_argument("--burst", type=float, default=20)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("turning", args.workdir)
    saves = d_lib.make_save("F", work / "save", args=["--effect", "1,47,0,0"])
    aim = d_lib.load("F") + r"Lnm9\r" + "DQ" + r"C\^\rN"
    lines, shots = d_lib.port(aim + "T", work / "port", args.seed, saves=[saves], pauses=True)
    k = lines.index("print: reflects the spell")
    print("port:", lines[k - 1:k + 3])
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        res = d_lib.run_steps(b, [("aim", aim)], shots, work)
        before = hp(b, "GARIN "), hp(b, "SIR STRONGSWORD")
        speed = b.ds_byte(0x4B38)
        b.ds_write(0x4B38, bytes([30]))
        b.push_key(ord("t") | 0x1400)
        burst = d_lib.burst(b, args.burst, work=work, prefix="cast")
        b.ds_write(0x4B38, bytes([speed]))
        after = hp(b, "GARIN "), hp(b, "SIR STRONGSWORD")
    frames = d_lib.frames_after(shots, res["aim"][1], 150)
    matched = [getattr(frames.get(s.pixels), "name", None) for s in burst]
    panel = []
    for s in burst:
        for r, t in d_lib.screen_text(s, range(10, 16), 23, 39).items():
            if (r, t.strip(" ?")) not in panel:
                panel.append((r, t.strip(" ?")))
    reflect = [m for m, s in zip(matched, burst)
               if "REFLECTS" in d_lib.read_text(s, 11, 23, 39)[0]
               or "REFLECTS" in d_lib.read_text(s, 12, 23, 39)[0]]
    print("original: screens equal to port frames", matched)
    print("original's panel:", panel)
    print(f"original: GARIN's and SIR's hit points before {before}, after {after}")
    status = d_lib.verdict(res["aim"][0] == 0 and any(reflect) and before[0] - after[0] == 2
                           and before[1] == after[1],
                           "#129 SIR reflects GARIN's Magic Missile: GARIN takes the 2 "
                           "points, SIR none; the screen with \"REFLECTS THE SPELL\" equals "
                           "the port's frame")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

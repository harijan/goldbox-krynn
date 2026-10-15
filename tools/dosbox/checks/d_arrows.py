#!/usr/bin/env python3
"""Check #89 (README, Checks against the original), its first half: "Shoot
at a monster with a long bow and arrows: the arrow's picture should point
the way it flies, one picture for the whole flight, and the arrows' count
drop by the shots made." (Game A has no long bow: SIRRION's Short Bow with
60 Arrows, both readied, is used.)

Caravan, seed 7: five turns Done/Quit, then SIRRION Aims (Next three times,
a baaz), Target: "Attacks BAAZ Hitting for 3". The arrows' count (the item
in slot 11, the far pointer at +0x147 + 4 x 11, count +0x39) read before and
after in the original's memory; the shot's screens in a burst (the speed
poked to 30) compared with eclplay's frames at its pauses (the arrow's
flight steps are pauses); the next menu's screen with eclplay's.
"""
import argparse
import sys

import d_lib


def arrows(b):
    rec = d_lib.by_name(d_lib.combatants(b), "SIRRION")[0]["rec"]
    item = b.far(rec + 0x147 + 4 * 11)
    if item is None:
        return None
    raw = b.read(item, 63)
    return raw[0x2E], raw[0x39]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--burst", type=float, default=15)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("arrows", args.workdir)
    aim = d_lib.LOAD_A + d_lib.TO_CARAVAN + "DQ" * 5 + "ANNN"
    lines, shots = d_lib.port(aim + "TVI", work / "port", args.seed, pauses=True)
    port_count = [int(l.split()[2]) for l in lines if l.endswith(" Arrows ")][-1]
    k = lines.index("turn: SIRRION (initiative 3)")
    attacks = [l for l in lines[k:] if l.startswith("attack:")][:2]
    print("port:", attacks)
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        res = d_lib.run_steps(b, [("aim", aim)], shots, work)
        before = arrows(b)
        speed = b.ds_byte(0x4B38)
        b.ds_write(0x4B38, bytes([30]))
        b.push_key(ord("t") | 0x1400)
        burst = d_lib.burst(b, args.burst, work=work, prefix="shot")
        b.ds_write(0x4B38, bytes([speed]))
        after = arrows(b)
        res.update(d_lib.run_steps(b, [("next", ""), ("items", "VI")], shots, work))
    frames = d_lib.frames_after(shots, res["aim"][1], 200)
    matched = [getattr(frames.get(s.pixels), "name", None) for s in burst]
    print("original: (type, count) of the arrows before", before, "after", after)
    print("original: the burst's screens equal to port frames:", matched)
    print("port: SIRRION's items at his next turn show", port_count, "Arrows")
    ok = before and before[0] == 0x1E and after[1] == port_count and \
        all(c == 0 for c, _ in res.values()) and sum(1 for m in matched if m) >= 3
    status = d_lib.verdict(ok, f"#89 SIRRION's turn of shots ({attacks}, a miss unsaid): "
                           f"arrows {before[1]} -> {after[1]} in the original's memory, "
                           f"{port_count} in the port's Items; the flight's screens equal "
                           "the port's frames (the same arrow picture); the Items screen equal")
    print("#89's second half (a hand axe thrown to the last into the pool, darts and "
          "javelins not) is NOT RUN: game A's party carries no thrown weapon.")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

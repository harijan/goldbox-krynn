#!/usr/bin/env python3
"""Check #16 (README, Checks against the original): "In the Done menu, F8
(scan 0x42, B) with no one dying ends the turn as Bandage would; Home (0x47)
guards; in Speed, Del slows past 9."

Game A, the caravan's four baaz (seed 7): ISTAN HORBIN's turn, Done, F8; KAL's
turn, Done, Home; GARIN's turn, Done, Speed, Del twelve times (from speed 4).
Compared with eclplay's screens at each menu; in the original's memory the
turn's entry (DS:72ce), KAL's guard (combat record +0x07) and the speed
(DS:4b38).
"""
import argparse
import sys

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("done", args.workdir)
    steps = [("istan", d_lib.LOAD_A + d_lib.TO_CARAVAN), ("istan-done", "D"),
             ("kal", r"\#42"), ("kal-done", "D"), ("garin", r"\#47"),
             ("speed", "DS"), ("del5", r"\#53" * 5), ("del12", r"\#53" * 7)]
    lines, shots = d_lib.port("".join(k for _, k in steps), work / "port", args.seed)
    turns = [l for l in lines if l.startswith(("turn:", "print: Guarding"))]
    print("port:", turns[:6])
    status = 0
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        res = d_lib.run_steps(b, steps[:3], shots, work)
        idx, order = d_lib.turn_entry(b)
        print(f"original at KAL's menu: entry {idx} of {order}")
        f8 = order[idx - 1] == "KAL"
        res.update(d_lib.run_steps(b, steps[3:5], shots, work))
        kal = d_lib.by_name(d_lib.combatants(b), "KAL")[0]
        guard = d_lib.combat_record(b, kal["rec"])[7]
        idx, order = d_lib.turn_entry(b)
        print(f"original at GARIN's menu: entry {idx} ({order[idx - 1]}); KAL's guard {guard}")
        res.update(d_lib.run_steps(b, steps[5:7], shots, work))
        speed5 = b.ds_byte(0x4B38)
        res.update(d_lib.run_steps(b, steps[7:], shots, work))
        speed12 = b.ds_byte(0x4B38)
        print(f"original's speed after 5 Dels {speed5}, after 12 {speed12}")
    screens = all(c == 0 for c, _ in res.values())
    status |= d_lib.verdict(f8 and guard and order[idx - 1] == "GARIN " and speed12 == 16
                            and screens and "print: Guarding" in turns,
                            "#16 F8 ends ISTAN's turn (KAL's next), Home has KAL guard "
                            "(+0x07 set, \"Guarding\"), Del slows past 9 (speed 4 -> 16); "
                            "every screen equal to eclplay's")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

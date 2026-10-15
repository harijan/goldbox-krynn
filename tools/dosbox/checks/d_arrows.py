#!/usr/bin/env python3
"""Check #89 (DEVELOPERS.md, Checks against the original), its first half: "Shoot
at a monster with a long bow and arrows: the arrow's picture should point
the way it flies, one picture for the whole flight, and the arrows' count
drop by the shots made." (Game A has no long bow: SIRRION's Short Bow with
60 Arrows, both readied, is used.)

Caravan, seed 7: five turns Done/Quit, then SIRRION Aims (Next three times,
a baaz), Target: "Attacks BAAZ Hitting for 3". The arrows' count (the item
in slot 11, the far pointer at +0x147 + 4 x 11, count +0x39) read before and
after in the original's memory against the port's at the same points; the
shot's screens in a burst (the speed poked to 30) against the port's frames
at its pauses and map shows, in order (the arrow's flight steps are
pauses); the screens and memory at the aim, the next menu and his Items at
his next turn against the port's.
"""
import argparse
import sys

import checklib
import d_lib

AIM = d_lib.LOAD_A + d_lib.TO_CARAVAN + "DQ" * 5 + "ANNN"
STEPS = [("aim", AIM), ("next", "T"), ("items", "VI")]


def arrows(rec):
    """(type, count) of the item readied in slot 11 (arrows) of record REC
    (the original's: its item at the far pointer +0x147 + 44)."""
    return rec["arrows"][0x2E], rec["arrows"][0x39]


def original_arrows(b):
    rec = d_lib.by_name(d_lib.party(b), "SIRRION")[0]["rec"]
    item = b.far(rec + 0x147 + 4 * 11)
    return None if item is None else arrows({"arrows": b.read(item, 63)})


def port_arrows(s):
    r = d_lib.by_name(s["records"], "SIRRION")[0]
    return None if not r["slots"][11] else arrows({"arrows": r["items"][r["slots"][11] - 1]})


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--burst", type=float, default=15)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-arrows")
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed)
    v = checklib.Verdict()
    seen = {}
    with d_lib.box(work, args.seed, verbose=args.verbose) as b:
        for name, keys in STEPS:
            if name == "next":
                d_lib.burst_step(v, b, port, "aim", "next", ord("t") | 0x1400, work, "shot",
                                 args.burst)
            else:
                shown = d_lib.step(b, keys, name, work)
                d_lib.screen(v, f"screen {name}", shown, port, name, work)
            d_lib.compare_state(v, b, port, name)
            seen[name] = original_arrows(b)
            d_lib.value(v, f"SIRRION's arrows (type, count) at {name}", seen[name], port, name,
                        port_arrows)
    k = port.lines.index("turn: SIRRION (initiative 3)")
    attacks = [line for line in port.lines[k:] if line.startswith("attack:")][:2]
    print("port:", attacks)
    v.check(seen["aim"] == (0x1E, 60) and seen["next"] == (0x1E, 58),
            "#89 the original: arrows (type 0x1e) 60 -> 58 for the turn's shots", f"{seen}")
    print("#89's second half (a hand axe thrown to the last into the pool, darts and "
          "javelins not) is NOT RUN: game A's party carries no thrown weapon.")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

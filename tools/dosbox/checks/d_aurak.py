#!/usr/bin/env python3
"""Check #12 (README, Checks against the original): "The aurak's breath at
one beside it that saves: its turn goes on."

Game J in ECL3 block 97 (d_lib.save_97) patched to square 12, 4
(square byte 0: the street's random encounters, 8205). The port meets the
encounter at Begin (its first-block location vector, see d_firstblock.py),
the original after Look: seed 101 (found with eclplay) rolls an aurak and 5
sivaks (859c). Alt-Q at the first turn, the computer plays. eclplay: the
AURAK "Breathes!" at GARIN beside it, who saves ("takes 10 points") and
goes down, and the AURAK then "Attacks" in the same turn. The original runs
at speed 15 (DS:4b38 poked, the port's too) with screenshots as fast as
they come until the battle waits again; they are judged in order against
the port's frames (d_lib.burst_step), the panel's texts read, and the
screen and memory at Look and at the end against the port's.
Both are given the same keys: the port as it is shows the encounter at
Begin Adventuring, before Look (the first block's vectors, d_firstblock.py),
so it differs from the original there and after; the port with the known
fixes does not, and the original against it decides (known: resume).
"""
import argparse
import sys

import checklib
import d_lib

STEPS = [("turn", d_lib.load("J") + "L"), ("end", r"\@q")]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=101)
    parser.add_argument("--burst", type=float, default=900)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-aurak")
    saves = d_lib.save_97(work)
    d_lib.patch_game(saves, "J", square=[12, 4, 0, 0, 0])
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed, saves=[saves])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        shown = d_lib.step(b, STEPS[0][1], "turn", work)
        d_lib.screen(v, "screen turn", shown, port, "turn", work, known="resume")
        d_lib.compare_state(v, b, port, "turn", known="resume")
        shots, _, _ = d_lib.burst_step(v, b, port, "turn", "end", 0x1000, work, "battle",
                                       args.burst, speed=15, known="resume")
        d_lib.compare_state(v, b, port, "end", known="resume")
    panel = []
    for s in shots:
        t = tuple(d_lib.read_text(s, r, 23, 39)[0].strip(" ?") for r in (10, 11, 12))
        if t[0] and (not panel or panel[-1] != t):
            panel.append(t)
    i = next((n for n, t in enumerate(panel) if t[:2] == ("AURAK", "BREATHES!")), None)
    tail = panel[i:i + 4] if i is not None else []
    print("original's panel from the breath:", tail)
    lines = [line for line in port.lines if not line.startswith("sound:")]
    k = lines.index("print: Breathes!") if "print: Breathes!" in lines else None
    print("port:", lines[k - 2:k + 8] if k is not None else None)
    v.check(len(tail) >= 3 and tail[1][:2] == ("GARIN", "TAKES 10 POINTS") and
            any(t[:2] == ("AURAK", "ATTACKS") for t in tail[1:]),
            "#12 the original: the aurak breathes at GARIN beside it, who saves (10 points), "
            "and its turn goes on: \"AURAK / ATTACKS\" next", f"{tail}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

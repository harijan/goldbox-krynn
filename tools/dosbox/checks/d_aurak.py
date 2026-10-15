#!/usr/bin/env python3
"""Check #12 (README, Checks against the original): "The aurak's breath at
one beside it that saves: its turn goes on."

Game J in ECL3 block 97 (d_dragonfear.py's save) patched to square 12, 4
(square byte 0: the street's random encounters, 8205). The port meets the
encounter at Begin (its first-block location vector, see d_firstblock.py),
the original after Look: seed 101 (found with eclplay) rolls an aurak and 5
sivaks (859c). Alt-Q at the first turn, the computer plays. eclplay: the
AURAK "Breathes!" at GARIN beside it, who saves ("takes 10 points") and
goes down, and the AURAK then "Attacks" in the same turn. The original runs
at speed 15 (DS:4b38 poked) with screenshots as fast as they come until its
panel shows the AURAK attacking after the breath; the panel's texts in
order and the screens against the port's frames at its pauses.
"""
import argparse
import sys
import time

import d_lib
from d_dragonfear import save_97


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=101)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("aurak", args.workdir)
    saves = save_97(work)
    path = saves / "SAVGAMJ.DAT"
    data = bytearray(path.read_bytes())
    data[0x1401:0x1406] = bytes([12, 4, 0, 0, 0])
    path.write_bytes(bytes(data))
    lines, shots = d_lib.port(d_lib.load("J") + r"\@q", work / "port", args.seed,
                              saves=[saves], pauses=True)
    lines = [l for l in lines if not l.startswith("sound:")]
    k = lines.index("print: Breathes!")
    print("port:", lines[k - 2:k + 8])
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves]) as b:
        res = d_lib.run_steps(b, [("turn", d_lib.load("J") + "L")], shots, work)
        b.ds_write(0x4B38, bytes([15]))
        b.push_key(0x1000)  # Alt-Q
        panel, screens = [], []
        breathed = False
        end = time.monotonic() + 400
        while time.monotonic() < end:
            s = b.screenshot()
            if screens and s.pixels == screens[-1].pixels:
                continue
            screens.append(s)
            t = tuple(d_lib.read_text(s, r, 23, 39)[0].strip(" ?") for r in (10, 11, 12))
            if t[0] and (not panel or panel[-1] != t):
                panel.append(t)
            breathed = breathed or t[:2] == ("AURAK", "BREATHES!")
            if breathed and t[:2] == ("AURAK", "ATTACKS"):
                break
        b.ds_write(0x4B38, bytes([4]))
    frames = d_lib.frames_after(shots, res["turn"][1], 4000)
    i = next((n for n, t in enumerate(panel) if t[:2] == ("AURAK", "BREATHES!")), None)
    tail = panel[i:i + 4] if i is not None else []
    print("original's panel from the breath:", tail)
    key = [s for s in screens if d_lib.read_text(s, 10, 23, 39)[0].strip(" ?") == "AURAK"
           and d_lib.read_text(s, 11, 23, 39)[0].strip(" ?") in ("BREATHES!", "ATTACKS")]
    matched = [bool(frames.get(s.pixels)) for s in key]
    print("original's AURAK screens equal to port frames:", matched)
    ok = len(tail) >= 3 and tail[1][:2] == ("GARIN", "TAKES 10 POINTS") and \
        any(t[:2] == ("AURAK", "ATTACKS") for t in tail[1:]) and any(matched)
    status = d_lib.verdict(ok, "#12 the aurak breathes at GARIN beside it, who saves "
                           "(10 points), and its turn goes on: \"AURAK / ATTACKS\" next, as "
                           "in the port")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

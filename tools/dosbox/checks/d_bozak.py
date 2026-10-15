#!/usr/bin/env python3
"""Check #90 (README, Checks against the original): "Kill a bozak beside two
characters: "explodes." and each beside it "takes N points of damage"
(1-6)."

Game A started with Helm (eclplay --helm), seed 168 (found with eclplay):
the caravan won by the Gods and the survivors taken back to the outpost
(as d_gate.py), then Leave, decline the outpost, Move east 24 steps along
row 3 (declining a merchant caravan on the way): at 24, 3 "YOU HAVE
DISCOVERED SOME HOSTILE CREATURES.", 3 giant spiders and 2 bozaks. Alt-Q at
the first turn, the computer plays the battle to "Continue Battle:". A
bozak drops beside SIRRION and MOLLY: "explodes.", and SIRRION, two
spiders and MOLLY take 1d6 each. (A save on the overland map would not do:
see the PORT DIFFERS entry on the first block's vectors.) The original runs at speed 15 (DS:4b38 poked) while
screenshots are taken and the party's hit points sampled: the screens
showing "EXPLODES." and the damages are compared with the port's frames at
its pauses, the hit points' changes with eclplay's log, and the final
screen with eclplay's.
"""
import argparse
import re
import sys
import time

import d_lib


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=168)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("bozak", args.workdir)
    route = (d_lib.LOAD_A + d_lib.TO_CARAVAN + r"\@x\r\rEN\r\rY\r\r" + "Lnm" +
             "6666666666666N6666666666" + r"\r")
    lines, shots = d_lib.port(route + r"\@q", work / "port", args.seed, args=["--helm"],
                              pauses=True)
    lines = [l for l in lines if not l.startswith("sound:")]
    k = lines.index("print: explodes.")
    blast = []
    for who, what in zip(lines[k + 1::2], lines[k + 2::2]):
        m = re.match(r"print: takes (\d+) points?", what)
        if not m:
            break
        blast.append((who[7:].rstrip(), int(m.group(1))))
    print("port: the first explosion:", blast)
    with d_lib.box(work, args.seed, verbose=args.verbose, args="x Helm") as b:
        res = d_lib.run_steps(b, [("turn", route)], shots, work)
        party = [(c["name"].rstrip(), c["rec"]) for c in d_lib.combatants(b)[:6]]
        b.ds_write(0x4B38, bytes([15]))
        b.push_key(0x1000)  # Alt-Q
        b._last_polls, b._streak = b.hook("polls"), 0
        hp_seen, screens = [], []
        end = time.monotonic() + 600
        while time.monotonic() < end and not b.waiting():
            hp = tuple(b.read(r + 0x197, 1)[0] for _, r in party)
            if not hp_seen or hp != hp_seen[-1]:
                hp_seen.append(hp)
            shot = b.screenshot()
            if not screens or shot.pixels != screens[-1].pixels:
                screens.append(shot)
        b.ds_write(0x4B38, bytes([4]))
        b.wait_input(settle=1.0)
        final = b.screenshot(work / "final.png")
    count = d_lib.compare(final, shots, "final", work)
    frames = d_lib.frames_after(shots, res["turn"][1], 3000)
    exploding = [s for s in screens if "EXPLODES." in d_lib.read_text(s, 11, 23, 39)[0]
                 or "EXPLODES." in d_lib.read_text(s, 10, 23, 39)[0]]
    matched = [bool(frames.get(s.pixels)) for s in exploding]
    print(f"original: {len(screens)} screens, {len(exploding)} showing \"EXPLODES.\", "
          f"equal to port frames: {matched}")
    for n, s in enumerate(exploding):
        s.save_png(work / f"explodes-{n}.png")
    names = [n for n, _ in party]
    drops = []
    for a, b_ in zip(hp_seen, hp_seen[1:]):
        drops.append({names[i]: a[i] - b_[i] for i in range(6) if a[i] != b_[i]})
    print("original: the party's hit points as they changed:", hp_seen)
    want = {n: d for n, d in blast if n in names}
    # The blast's damages to the party, in one change of the sampled hit
    # points or two running.
    def fits(d):
        return d == want
    seen = any(fits(d) for d in drops) or any(fits(d1 | d2) for d1, d2 in zip(drops, drops[1:]))
    status = d_lib.verdict(count == 0 and exploding and all(matched) and seen and
                           all(1 <= d <= 6 for _, d in blast),
                           f"#90 the bozak explodes beside two characters: {want} in the "
                           "original's hit points as in eclplay's log; the screens with "
                           "\"EXPLODES.\" equal the port's frames; the end equal")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

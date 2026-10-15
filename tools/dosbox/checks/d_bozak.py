#!/usr/bin/env python3
"""Check #90 (README, Checks against the original): "Kill a bozak beside two
characters: "explodes." and each beside it "takes N points of damage"
(1-6)."

Game A started with Helm (eclplay --helm), seed 168 (found with eclplay):
the caravan won by the Gods and the survivors taken back to the outpost
(as d_gate.py), then Leave, decline the outpost, Move east 24 steps along
row 3 (declining a merchant caravan on the way): at 24, 3 "YOU HAVE
DISCOVERED SOME HOSTILE CREATURES.", 3 giant spiders and 2 bozaks. Alt-Q at
the first turn, the computer plays the battle to its end (the party is
destroyed: "The monsters rejoice ..." and "Press any key to continue"). A
bozak drops beside SIRRION and MOLLY: "explodes.", and SIRRION, two
spiders and MOLLY take 1d6 each. The original runs at speed 15 (DS:4b38 poked, the
port's too) while screenshots are taken to the battle's end: they are
judged in order against the port's frames (d_lib.burst_step), the panel
read for "EXPLODES." and the damages that follow (against eclplay's log),
and the screen and memory (d_lib.compare_state: the party's hit points
among it) at the first turn and at the end against the port's. The
original types the defeat's text a letter at a time (the speed's delay
between letters): whether the port shows it so, from its frames.
"""
import argparse
import re
import sys

import checklib
import d_lib

ROUTE = (d_lib.LOAD_A + d_lib.TO_CARAVAN + d_lib.CARAVAN_WON + "Lnm" +
         "6666666666666N6666666666" + r"\r")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=168)
    parser.add_argument("--burst", type=float, default=900)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-bozak")
    steps = [("turn", ROUTE), ("end", r"\@q")]
    port = d_lib.Port(work / "eclplay", d_lib.marked(steps), args.seed, args=["--helm"])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, args="x Helm") as b:
        shown = d_lib.step(b, ROUTE, "turn", work)
        d_lib.screen(v, "screen turn", shown, port, "turn", work)
        d_lib.compare_state(v, b, port, "turn")
        shots, _, _ = d_lib.burst_step(v, b, port, "turn", "end", 0x1000, work, "battle",
                                       args.burst, speed=15)
        d_lib.compare_state(v, b, port, "end")
    lines = [line for line in port.lines if not line.startswith("sound:")]
    k = lines.index("print: explodes.") if "print: explodes." in lines else None
    blast = []
    for who, what in zip(lines[k + 1::2], lines[k + 2::2]) if k is not None else ():
        m = re.match(r"print: takes (\d+) points?", what)
        if not m:
            break
        blast.append((who[7:].strip(), m.group(1)))
    print("port: the first explosion:", blast)
    # The original's panel from "EXPLODES." on: (name, damage) of each
    # "TAKES N POINTS" until another text.
    panel = []
    for s in shots:
        t = tuple(d_lib.read_text(s, r, 23, 39)[0].strip(" ?") for r in (10, 11))
        if t[0] and (not panel or panel[-1] != t):
            panel.append(t)
    i = next((n for n, t in enumerate(panel) if "EXPLODES." in t), None)
    said = []
    for name, text in panel[i + 1:] if i is not None else ():
        if not text.startswith("TAKES"):
            break
        said.append((name, text.split()[1]))
    print("original's panel after \"EXPLODES.\":", said)
    # The party's defeat: "The monsters rejoice for the party has been
    # destroyed" (351b:1aab) in the text window, row 5 on.
    def stages(screens):
        """The parts of the text's first line shown on their own (typed)."""
        full = "THE MONSTERS REJOICE FOR THE PARTY"
        rows = {d_lib.read_text(s, 5, 1, 39)[0].strip(" ?") for s in screens}
        return sorted(t for t in rows if t and full.startswith(t) and t != full)

    typed = stages(shots)
    print("original: the defeat's text as it was typed:", typed)
    d_lib.value(v, "the defeat's text typed a letter at a time (some of its parts shown on "
                "their own)", bool(typed), port, "end",
                lambda s: bool(stages([f for _, f in port.frames])))
    v.check(i is not None and said == blast and blast and
            all(1 <= int(d) <= 6 for _, d in said),
            "#90 the original: the bozak \"EXPLODES.\" and each beside it takes 1-6, as the "
            "port's log has it", f"original {said}, port {blast}")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

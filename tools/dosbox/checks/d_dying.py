#!/usr/bin/env python3
"""Check #80 (README, Checks against the original): "Leave a character
dying (2-9 points past its hit points) in a battle: "Your Teammate is
Dying" should show at each round's end, and it should die once the rounds
counted from its damage pass 9, its hit points unchanged; Bandage (in Done)
should make it unconscious."

The port writes game D: game A with SIR STRONGSWORD at 1 hit point (+0x197
patched in its .SAV), saved in the outpost inn's camp. Both load D, started
with Helm, and meet the caravan's baaz, seed 41 (found with eclplay): ISTAN
Done/Quit, then a baaz hits SIR for 4, 3 past his hit points: "is Dying".
First run: at KAL's turn the Gods; then "Continue Battle:" Yes each round,
every turn Done/Quit, eight rounds; at each "Continue Battle:" the
original's SIR (status +0x188, rounds dying, combat record +0x0e, hit
points), the rest of the memory (d_lib.compare_state) and the screen
against the port's at the same point. Second run: at KAL's turn Done,
Bandage.
"""
import argparse
import sys

import checklib
import d_lib

ROUND = "Y" + "DQ" * 5


def dying(cs):
    """SIR STRONGSWORD's (status, rounds dying +0x0e, hit points)."""
    c = d_lib.by_name(cs, "SIR STRONGSWORD")[0]
    return c["status"], c["combat"][0x0E], c["hp"]


def run(v, args, work, port, steps, saves):
    """The STEPS in the original against PORT: SIR's (status, count, hit
    points) at each."""
    seen = []
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves], args="x Helm") as b:
        for name, keys in steps:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            d_lib.compare_state(v, b, port, name)
            seen.append(dying(d_lib.combatants(b)))
            d_lib.value(v, f"SIR's (status, rounds dying, hit points) at {name}", seen[-1], port,
                        name, lambda s: dying(d_lib.port_combatants(s)))
    return seen


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-dying")
    saves = d_lib.make_save("D", work / "save")
    d_lib.patch_record(saves, "D", 1, 0x197, [1])
    route = d_lib.load("D") + r"Lnm9\r" + "DQ"
    steps = [("kal", route), ("round1", r"\@x")] + [(f"round{n}", ROUND) for n in range(2, 10)]
    port = d_lib.Port(work / "eclplay", d_lib.marked(steps), args.seed, saves=[saves],
                      args=["--helm"])
    bandage = [("kal", route), ("bandaged", "DB")]
    bport = d_lib.Port(work / "eclplay-bandage", d_lib.marked(bandage), args.seed,
                       saves=[saves], args=["--helm"])
    v = checklib.Verdict()
    seen = run(v, args, work, port, steps, saves)
    print("original: SIR's (status, rounds dying, hit points) at KAL's turn, then at each "
          "round's Continue Battle:", seen)
    bseen = run(v, args, work / "bandage", bport, bandage, saves)
    print("original after Bandage: SIR's (status, rounds dying, hit points)", bseen[-1])
    # Per round's end: was "Your Teammate is Dying" printed before its prompt?
    said = [port.lines[k - 1] == "print: Your Teammate is Dying"
            for k, line in enumerate(port.lines) if line == "menu: Continue Battle:"]
    print("port: \"Your Teammate is Dying\" at the ends of rounds 1-9:", said)
    # The original, as batch D saw it: dying (5) with 3 rounds counted at KAL's
    # turn, a round more at each round's end, dead (6) past 9 (the count stays
    # 10), hit points 0 throughout.
    counts = [s[1] for s in seen[1:]]
    v.check(seen[0][:2] == (5, 3) and counts == [min(n, 10) for n in range(4, 13)] and
            [s[0] for s in seen[1:]] == [5 if c <= 9 else 6 for c in counts] and
            {s[2] for s in seen} == {0},
            "#80 the original: the count rises a round at a time, dead (6) once past 9, hit "
            "points 0", f"{seen}")
    v.check(said == [c <= 9 for c in counts],
            "#80 the port says \"Your Teammate is Dying\" at each round's end while the "
            "original's count is 9 or less (the text itself is never caught on the "
            "original's screen: the Continue prompt overwrites it)")
    v.check(bseen[-1][:2] == (4, 0) and any("is bandaged" in line for line in bport.lines),
            "#80 Bandage: unconscious (4), no rounds dying, in the original; \"is bandaged\" "
            "in the port")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

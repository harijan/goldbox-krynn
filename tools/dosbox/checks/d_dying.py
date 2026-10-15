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
original's SIR is read (status +0x188, rounds dying, combat record +0x0e,
hit points) and the screen compared. Second run: at KAL's turn Done,
Bandage.
"""
import argparse
import sys

import d_lib

ROUND = "Y" + "DQ" * 5


def sir(b):
    c = d_lib.by_name(d_lib.combatants(b), "SIR STRONGSWORD")[0]
    return c["status"], d_lib.combat_record(b, c["rec"])[0x0E], c["hp"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    d_lib.need_saves()
    work = d_lib.workdir("dying", args.workdir)
    saves = d_lib.make_save("D", work / "save")
    d_lib.patch_record(saves, "D", 1, 0x197, [1])
    route = d_lib.load("D") + r"Lnm9\r" + "DQ"
    steps = [("kal", route), ("round1", r"\@x")] + [(f"round{n}", ROUND) for n in range(2, 10)]
    lines, shots = d_lib.port("".join(k for _, k in steps), work / "port", args.seed,
                              saves=[saves], args=["--helm"])
    # Per round's end: was "Your Teammate is Dying" printed before its prompt?
    said = [lines[k - 1] == "print: Your Teammate is Dying"
            for k, l in enumerate(lines) if l == "menu: Continue Battle:"]
    print("port: \"Your Teammate is Dying\" at the ends of rounds 1-9:", said)
    bandage = [("kal", route), ("bandaged", "DB")]
    blines, bshots = d_lib.port("".join(k for _, k in bandage), work / "port-bandage",
                                args.seed, saves=[saves], args=["--helm"])
    print("port, Bandage:", [l for l in blines if "bandaged" in l])
    seen = []
    with d_lib.box(work, args.seed, verbose=args.verbose, saves=[saves], args="x Helm") as b:
        res = d_lib.run_steps(b, steps[:1], shots, work)
        seen.append(sir(b))
        for step in steps[1:]:
            res.update(d_lib.run_steps(b, [step], shots, work))
            seen.append(sir(b))
    print("original: SIR's (status, rounds dying, hit points) at KAL's turn, then at each "
          "round's Continue Battle:", seen)
    with d_lib.box(work / "bandage", args.seed, verbose=args.verbose, saves=[saves],
                   args="x Helm") as b:
        bres = d_lib.run_steps(b, bandage, bshots, work / "bandage")
        bseen = sir(b)
    print("original after Bandage: SIR's (status, rounds dying, hit points)", bseen)
    # Dying (5) while the count is 9 or less, dead (6) once past, hit points the same.
    # (the count stops at 10, past 9, once dead)
    counts = [s[1] for s in seen[1:]]
    expect = [min(n, 10) for n in range(4, 4 + len(counts))]
    ok = seen[0][0] == 5 and seen[0][1] == 3 and counts == expect and \
        [s[0] for s in seen[1:]] == [5 if c <= 9 else 6 for c in counts] and \
        len({s[2] for s in seen}) == 1 and said == [c <= 9 for c in counts] and \
        all(c == 0 for c, _ in res.values())
    status = d_lib.verdict(ok, "#80 dying 3 past his hit points: the count rises a round "
                           "at a time, dead (6) once past 9, hit points unchanged, the "
                           "port's \"Your Teammate is Dying\" at each round's end while "
                           "dying; every screen equal")
    status |= d_lib.verdict(bseen[0] == 4 and bseen[1] == 0 and
                            all(c == 0 for c, _ in bres.values()) and
                            any("is bandaged" in l for l in blines),
                            "#80 Bandage: unconscious (4), no rounds dying; screen equal")
    print(f"files in {work}")
    return status


if __name__ == "__main__":
    sys.exit(main())

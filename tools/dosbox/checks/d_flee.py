#!/usr/bin/env python3
"""Check #63 (docs/checks.md, Checks against the original): "Flee a fight: the
results should say `The party has fled.` and `Each character receives 0`."

Game A started with Helm (eclplay --helm), seed 7, the caravan's baaz: the
Gods at the first turn, "Continue Battle:" Yes, and the six walk south and
off the map, round after round ("Flee:" Yes, "Got Away"; the keys were
found with eclplay, each step tried until the map refuses it). At each
step the screen and the memory (d_lib.compare_state) against the port's,
the results read, and the original's party's statuses there (0 again: the
end of combat puts back those that ran).
"""
import argparse
import sys

import checklib
import d_lib

FLIGHT = (r"M222221111346\rDQM2222113344666\rDQM22222213446\rDQM22222N111346\rDQ"
          r"M221111N33346\rDQM22222111346\rDQYM2222YDQM222222YDQM2222211346\rDQ"
          r"M2211N333YYDQM22YDQYM21333333YDQYM22222Y")
STEPS = [("turn", d_lib.LOAD_A + d_lib.TO_CARAVAN), ("gods", r"\@xY"), ("results", FLIGHT)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "d-flee")
    port = d_lib.Port(work / "eclplay", d_lib.marked(STEPS), args.seed, args=["--helm"])
    v = checklib.Verdict()
    with d_lib.box(work, args.seed, verbose=args.verbose, args="x Helm") as b:
        for name, keys in STEPS:
            shown = d_lib.step(b, keys, name, work)
            d_lib.screen(v, f"screen {name}", shown, port, name, work)
            seen = d_lib.compare_state(v, b, port, name)
    text = " ".join(d_lib.screen_text(shown).values())
    print("original's results:", d_lib.screen_text(shown))
    v.check("THE PARTY HAS FLED." in text and "EACH CHARACTER RECEIVES 0" in text,
            "#63 the original's results: \"The party has fled.\", \"Each character receives 0\"")
    i = port.lines.index("print: The party has fled.") if "print: The party has fled." in \
        port.lines else None
    v.check(i is not None and port.lines[i + 1] == "print: Each character receives 0",
            "#63 the port's log says the same")
    v.check(all(r[2] == 0 for r in seen["records"][:6]),
            "the party's statuses 0 again at the results in the original (those that ran put back)")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

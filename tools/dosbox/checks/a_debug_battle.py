#!/usr/bin/env python3
"""Check #13's battle part (README, Checks against the original: "The debug
mode ...: and, in a battle, the trace of each combatant and `Done*`").

The original runs as `START Woof`, eclplay as --woof, both with seed 7 and
SAVE/'s games: game A loaded, Solace's pages to the outpost, Leave, No,
Move, Ctrl-D (the flag on), then the step north-east that meets the
caravan's four baaz (batch D's route), and Enter at each key read after it
(the trace waits for one after each opcode, the battle's setup after each
record, the combat menu reads one too). Each screen is compared with the
port's at the same key read. The battle's setup: the port's
`debug:` lines from LOAD MONSTER's to its first combat menu, each the trace
on the screen at one of the key reads; those screens, and the first combat
menu's with `Done*`, must be among those compared.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

TO_MOVE = r"P\v\v\rA\v\v\v\v\v\r" + r"\r" * 7 + "Lnm"    # the overland map, in Move
SEED = 7


def is_trace(line):
    """A trace's line, `debug: A , B , C | TEXT` (not `debug: on`)."""
    return line.startswith("debug: ") and " | " in line


def shown(text):
    """What the port shows where it stopped: ('debug', N) its Nth trace
    (from 0), ('menu', TEXT) a menu, or None."""
    lines = [line for line in text.splitlines() if is_trace(line) or line.startswith("menu: ")]
    if not lines:
        return None
    if is_trace(lines[-1]):
        return ("debug", sum(1 for line in lines if is_trace(line)) - 1)
    return ("menu", lines[-1][6:])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--steps", type=int, default=120)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "debug-battle")
    keys = (TO_MOVE + r"\cD\son." + "9" + r"\sstep." +
            "".join(rf"\r\st{i:03d}." for i in range(args.steps)))
    pair = A.Pair(keys, work, seed=SEED, saves=A.SAVES, box_args="Woof", eclplay_args=["--woof"],
                  verbose=args.verbose)
    lines = pair.out.splitlines()
    traced = [line for line in lines if is_trace(line)]
    loaded = next(i for i, line in enumerate(lines) if line.endswith("| LOAD MONSTER"))
    menu = next(i for i in range(loaded, len(lines)) if lines[i].startswith("menu: "))
    first = traced.index(lines[loaded])
    setup = [line for line in lines[loaded:menu] if is_trace(line)]
    print(f"port: {len(traced)} trace lines; the battle's setup, {len(setup)} traces: "
          + " / ".join(line.split("| ")[-1] for line in setup) + f"; then {lines[menu]}")
    at = {name: shown(probe.stdout) for name, probe in
          pair.probes().items()}
    v = checklib.Verdict()
    with pair:
        pair.run()
        print(f"  the original waits at {pair.box.where()}; DS:4b51 {pair.box.ds_byte(0x4B51)}")
    marks = {}
    for name in pair.marks():
        what = f"screen {name}"
        if at[name] and at[name][0] == "debug" and first <= at[name][1] < first + len(setup):
            what += f", the setup's trace {traced[at[name][1]][7:]!r}"
            marks.setdefault("setup", []).append(at[name][1])
        elif at[name] and at[name][0] == "menu" and "Done*" in at[name][1]:
            what += f", the combat menu {at[name][1]!r}"
            marks.setdefault("done", []).append(name)
        pair.judge(v, name, what=what)
    v.check(sorted(marks.get("setup", [])) == list(range(first, first + len(setup))),
            "each of the setup's traces is on a screen compared",
            f"{len(marks.get('setup', []))} of {len(setup)}")
    v.check(bool(marks.get("done")), "a combat menu with Done* is on a screen compared",
            f"{marks.get('done', [])[:3]} (more --steps if none)")
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

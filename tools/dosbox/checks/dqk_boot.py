#!/usr/bin/env python3
"""The Dark Queen of Krynn's start in the original: the MicroMagic and SSI
logos and the title (TITLE.TLB's scenes), the title's PLAY / DEMO / QUIT,
the camp menu, Load from TDQK's saved game A (SAVGAMA.QSV, its party), and
Begin Adventuring past the journal's copy protection (dqk 65cc:008b,
answered with the word it decoded into DS:c6f9) to the first 3D view.

Each step's screen, once settled, is saved as OUT/original/NN-NAME.png
with a few of the original's variables (dqk_lib: the adapter, the party's
list, the protection's question, the ECL's buffer, address and opcode) in
OUT/original/memory.json: the recorded original. There is no port side yet
(step 1 adds `--game dark-queen`): port() returns None for every step and
the verdict judges the original's run alone, that it reached every step
(the screens new at each, the party loaded, the protection asked once and
answered, the ECL running). In run_all.py's Dark Queen set (make
dqk-checks). About a minute.

    python3 tools/dosbox/checks/dqk_boot.py [--workdir DIR] [--verbose]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...) listing what
differs."""
import argparse
import json
import pathlib
import shutil
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dqk_lib  # noqa: E402

cokbox = checklib.cokbox

# The start's pictures, each a new screen (more than NEW pixels changed);
# each twinkles (the logo's spark, the title's stars), so it is taken once
# it settles within TWINKLE pixels.
PICTURES = ["micromagic", "ssi", "title"]
NEW, TWINKLE = 20000, 2000
# The steps after the title: (name, keys typed once the screen before has
# settled). ANSWER stands for the protection's word, read from the
# original's memory as it asks.
ANSWER = object()
STEPS = [("menu", "\\r"), ("camp", "p"), ("load-from", "l"), ("load-which", "t"),
         ("loaded", "a"), ("protection", "b"), ("answered", ANSWER)]
# What two runs of the original differ in (2026-10-26), not to be compared
# with the port's: (x, y, w, h, why).
MASKS = {
    "micromagic": [(35, 72, 212, 24, "the logo's spark")],
    "ssi": [(20, 101, 280, 13, "the logo's spark")],
    "title": [(187, 89, 13, 4, "the title's twinkle")],
    "protection": [(64, 189, 8, 8, "the input's blinking cursor")],
}
# Set once the party is loaded, so that the protection's question (two
# draws, 2102:0234) is the same each run.
SEED = 12345


def port(work, name):
    """The port's screen at step NAME: none yet (step 1 of DARK-QUEEN.md
    adds --game dark-queen to eclplay)."""
    return None


def pictures(box, out, verdict):
    """The logos and the title, each once it has settled; returns the last
    shown."""
    shown, found = None, []
    end = time.monotonic() + 120
    while len(found) < len(PICTURES) and time.monotonic() < end:
        shot = box.screenshot()
        if (shot.width, shot.height) != (320, 200):
            continue  # DOS's text screen, before the game starts
        if shown is None and len(shot.colours()) < 8:
            continue  # black, the palette not yet faded in
        if shown is None or shot.diff(shown)[0] > NEW:
            name = PICTURES[len(found)]
            shown = box.settled(quiet=0.3, tolerance=TWINKLE)
            shown.save_png(out / f"{len(found):02d}-{name}.png")
            found.append(name)
            verdict.ok(f"{name} shown", f"{len(shown.colours())} colours")
        box.settle(0.2)
    verdict.check(len(found) == len(PICTURES), "the start's pictures seen",
                  f"{len(found)} of {len(PICTURES)}")
    return shown


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    dqk_lib.need_assets()
    work = checklib.workdir(opts.workdir, "dqk_boot")
    out = work / "original"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    verdict = checklib.Verdict()
    memory = {}
    # At its cycles, not turbo: turbo ran it in 14 s, not 41, but a
    # screenshot then takes longer than the SSI logo's 17 s of the game's
    # time, which it missed (2026-10-26); the steps after agreed in both.
    with cokbox.Box(workdir=work / "box", game="dark-queen", verbose=opts.verbose,
                    timeout=600, turbo=False) as box:
        last = pictures(box, out, verdict)
        memory["title"] = dict(adapter=box.ds_word(dqk_lib.DS_ADAPTER))
        verdict.value("the display adapter (DS:ca41)", memory["title"]["adapter"], 0x0F)
        for i, (name, keys) in enumerate(STEPS, start=len(PICTURES)):
            if keys is ANSWER:
                entry, word, answer = dqk_lib.protection(box)
                memory["question"] = dict(entry=entry, word=word, answer=answer)
                verdict.check(answer.isalpha(), "the protection's answer decoded",
                              f"entry {entry}, word {word} of DS:6dd2's table: {answer!r}")
                keys = answer + "\\r"
            box.keys(keys)
            # The protection's input blinks its cursor (MASKS).
            shot = box.settled(tolerance=64)
            shot.save_png(out / f"{i:02d}-{name}.png")
            memory[name] = dict(ecl=dqk_lib.ecl_state(box),
                                party=dqk_lib.far(box, dqk_lib.DS_PARTY) is not None)
            if name == "loaded":
                box.set_seed(SEED)
            verdict.check(last is not None and shot.diff(last)[0] > 0, f"{name}: a new screen")
            mine = port(work, name)
            if mine is not None:
                verdict.screen(name, shot, mine, masks=MASKS.get(name, ()), work=work)
            last = shot
        verdict.check(memory["loaded"]["party"], "the party loaded (DS:c6e5)")
        typed = dqk_lib.cstr(box.ds_read(dqk_lib.DS_TYPED, 42))
        verdict.check(typed.upper() == memory["question"]["answer"].upper(),
                      "the word typed (DS:c723)", repr(typed))
        asked = box.ds_byte(dqk_lib.DS_ASKED)
        memory["question"]["asked"] = asked
        verdict.value("the protection asked once (DS:c722)", asked, 1)
        # Until Begin Adventuring the ECL's address stays at its buffer's
        # start (0x8000, opcode 0: the title's menu on); its first block
        # then runs.
        before, after = memory["protection"]["ecl"], memory["answered"]["ecl"]
        verdict.check(after["buffer"] is not None and after["pc"] > 0x8000 and
                      after["pc"] != before["pc"] and after["opcode"] != 0,
                      "the ECL running after Begin Adventuring",
                      f"{json.dumps(before)} then {json.dumps(after)}")
    (out / "memory.json").write_text(json.dumps(memory, indent=1) + "\n")
    print(f"the recorded original in {out}", flush=True)
    return verdict.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

#!/usr/bin/env python3
"""Check #8, first part: Slow Poison run out after 63 hours (README, Checks
against the original: "Timers ... cast Slow Poison on a poisoned character
and rest 63 hours: where 0x16 runs out with Slow Poison's 0x0f after it,
the port stops ... see whether the game stops with runtime error 204,
crashes or goes on, and whether the character 'dies from poison'").

As slow_poison.py, but with 0x16's full 3780 minutes: the port writes game
C (SAVE/'s A, SIR STRONGSWORD given poison 0x37, 0x16 for 3780 minutes and
0x0f for 10, in that order, as Slow Poison leaves them, in the outpost
inn's camp). The original loads C, camps at the inn, and rests 64 hours
(two days and sixteen hours) with turbo; 0x0f ends and puts itself back
every ten minutes (a point of damage while above a hit point) and 0x16
ends at the 3780th minute. Recorded: the screens through the rest, the
party's records and effects in memory, Turbo Pascal's heap, the interrupt
table, whether the game still takes keys, DOSBox-X's log. The port's run
of the same rest is shown for comparison (it stops where 0x16's handler
frees the 0x0f the walk goes to next).
"""
import argparse
import pathlib
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
from slow_poison import heap, PREFIX_SEG  # noqa: E402

EFFECTS = ["1,37,0,0", "1,16,ec4,0", "1,f,a,0"]
REST_64H = "RDAA" + "H" + "A" * 16 + "R"   # Rest, Days +2, Hours +16, Rest


def show(box, addr):
    raw = box.read(addr, L.REC)
    return (f"{L.name_of(raw)} status {raw[0x188]} hp {raw[0x197]}/{raw[0x62]} effects "
            + str([e if len(e) == 2 else (hex(e[0]), e[1], e[3], e[4]) for e in
                   L.effects_at(box, addr + 0xE3)]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-b-slowpoison-"))
    effects = [a for e in EFFECTS for a in ("--effect", e)]
    a = L.SAVE / "SAVGAMA.DAT"
    out, saves, _ = L.port(L.A_TO_CAMP + "SC", work / "make", ["--load", a, "--play", *effects])
    port_out, _, _ = L.port(L.A_TO_CAMP + REST_64H, work / "port",
                            ["--load", a, "--play", *effects])
    lines = port_out.strip().splitlines()
    print("port, resting 64 hours (the last lines):\n  " + "\n  ".join(lines[-8:]))

    with L.box(work, [saves], verbose=args.verbose, turbo=True, timeout=1800) as box:
        box.keys(L.load_keys("C") + L.INN)
        box.wait_input(settle=0.3)
        addr, _ = L.member(box, "SIR STRONGSWORD")
        print("original, in camp:", show(box, addr), "; clock", L.clock(box)[1:6])
        box.keys(REST_64H[:-1])
        box.wait_input(settle=0.3)
        box.screenshot(work / "rest-menu.png")
        assert box.ds_word(PREFIX_SEG) == box.psp
        print("  heap before:", heap(box))
        low_before = box.read(0, 0x600)
        box.key("R")
        seen = {"shots": 0, "last": None, "dead": False}

        def each(box):
            # The screens through the rest, and the clock at the death.
            s = box.screenshot()
            if seen["last"] is None or s.pixels != seen["last"].pixels:
                s.save_png(work / f"rest-{seen['shots']:03d}.png")
                seen["shots"] += 1
                seen["last"] = s
            if box.read(addr, L.REC)[0x188] == 6 and not seen["dead"]:
                seen["dead"] = True
                print("  status 6 (dead) seen at clock", L.clock(box)[1:6])
        L.wait_rest(box, each=each)
        shots = seen["shots"]
        print(f"original: {shots} different screens during the rest;",
              "running" if box.running else "ENDED", "; DOSBox-X log:",
              (work / "box" / "dosbox.log").read_text(errors="replace").strip().splitlines()[-2:])
        if box.running:
            low_after = box.read(0, 0x600)
            changed = [i for i in range(0, 0x600, 2) if low_before[i:i + 2] != low_after[i:i + 2]
                       and not 0x41A <= i < 0x440 and not 0x46C <= i < 0x472]
            print("  low memory words changed (not the keyboard buffer or the BIOS clock): "
                  + ", ".join(f"{i:04x}: {low_before[i:i + 2].hex()} -> {low_after[i:i + 2].hex()}"
                              for i in changed))
            print("  waits at", box.where(), "; clock", L.clock(box)[1:6])
            print("  after:", show(box, addr))
            print("  heap after:", heap(box))
            box.screenshot(work / "after.png")
            # Another rest: does the game go on?
            box.keys("RHAR")
            L.wait_rest(box, seconds=300)
            box.screenshot(work / "after-hour.png")
            print("  after another hour's rest:", "running" if box.running else "ENDED",
                  box.where() if box.running else "", show(box, addr) if box.running else "")
            print("  heap then:", heap(box) if box.running else "-")
            # And a save: Save as H, then load it again from the start menu? The
            # save's files show what the corrupted heap left in the records.
            box.keys(L.save_keys("H"))
            box.wait_input(settle=0.5, seconds=120)
            print("  saved as H:", sorted(p.name for p in (box.game / "SAVE").glob("*DATH*")))
    print(f"files in {work}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check #8, first part: Slow Poison run out after 63 hours (DEVELOPERS.md, Checks
against the original: "Timers ... cast Slow Poison on a poisoned character
and rest 63 hours: where 0x16 runs out with Slow Poison's 0x0f after it,
the port stops ... see whether the game stops with runtime error 204,
crashes or goes on, and whether the character 'dies from poison'").

As slow_poison.py, but with 0x16's full 3780 minutes: the port writes game
C (SAVE/'s A, SIR STRONGSWORD given poison 0x37, 0x16 for 3780 minutes and
0x0f for 10, in that order, as Slow Poison leaves them, in the outpost
inn's camp). The original loads C, camps at the inn, and rests 64 hours
(two days and sixteen hours); 0x0f ends and puts itself back
every ten minutes (a point of damage while above a hit point) and 0x16
ends at the 3780th minute. Recorded: the screens through the rest, the
party's records and effects in memory, Turbo Pascal's heap, the interrupt
table, whether the game still takes keys, DOSBox-X's log. The port stops
where 0x16's handler frees the 0x0f the walk goes to next: it is not
compared beyond, but must stop there with that reason after "dies from
poison". The original must do what batch B recorded: SIR STRONGSWORD dead
(status 6) at the clock's day 2, hour 15, with 0 hit points and no effects;
the rest going on to its end; the camp's menu taking keys (4888:2d03),
another hour's rest and a Save (all of H written); low memory changed at
most in the BIOS's motor timeout (0040:0040); the free list corrupted as
recorded (53ca:0000 of 56 bytes, then the link counted down, 53d2:fb00).
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402
from slow_poison import MOTOR, PREFIX_SEG, heap, heap_blocks, low_changes  # noqa: E402

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
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "b-slowpoison")
    v = checklib.Verdict()
    effects = [a for e in EFFECTS for a in ("--effect", e)]
    a = L.SAVE / "SAVGAMA.DAT"
    out, saves, _ = L.port(L.A_TO_CAMP + "SC", work / "make", ["--load", a, "--play", *effects])
    port_out, _, _ = L.port(L.A_TO_CAMP + REST_64H, work / "port",
                            ["--load", a, "--play", *effects])
    said = [ln for ln in port_out.splitlines() if ln.startswith(("print: ", "error: "))]
    print("port, resting 64 hours (the last lines):\n  " + "\n  ".join(said[-6:]))
    v.check(said[-2:] == ["print: dies from poison", "error: a handler removed effect 0x0f as "
                          "it was counted down; the original reads it after freeing it"],
            "the port stops where 0x0f is read after it was freed, after \"dies from poison\" "
            "(it is not compared beyond)")

    # Not turbo: the rest's screenshots then come too seldom to see the
    # death at hour 15 (the first at 16). Its turbo used to end at the
    # rest menu's screenshot (DOSBox-X's "stop turbo on key", now off).
    with L.box(work, [saves], verbose=args.verbose, turbo=False, timeout=1800) as box:
        box.keys(L.load_keys("C") + L.INN)
        box.wait_input(settle=0.3)
        addr, _ = L.member(box, "SIR STRONGSWORD")
        print("original, in camp:", show(box, addr), "; clock", L.clock(box)[1:6])
        box.keys(REST_64H[:-1])
        box.wait_input(settle=0.3)
        box.screenshot(work / "rest-menu.png")
        if box.ds_word(PREFIX_SEG) != box.psp:
            raise L.cokbox.BoxError("System's variables are not where expected")
        print("  heap before:", heap(box))
        low_before = box.read(0, 0x600)
        box.key("R")
        seen = {"shots": 0, "last": None, "dead": None}

        def each(box):
            # The screens through the rest, and the clock at the death.
            s = box.screenshot()
            if seen["last"] is None or s.pixels != seen["last"].pixels:
                s.save_png(work / f"rest-{seen['shots']:03d}.png")
                seen["shots"] += 1
                seen["last"] = s
            if box.read(addr, L.REC)[0x188] == 6 and seen["dead"] is None:
                seen["dead"] = L.clock(box)
                print("  status 6 (dead) seen at clock", seen["dead"][1:6])
        L.wait_rest(box, each=each)
        print(f"original: {seen['shots']} different screens during the rest;",
              "running" if box.running else "ENDED", "; DOSBox-X log:",
              (work / "box" / "dosbox.log").read_text(errors="replace").strip().splitlines()[-2:])
        v.check(box.running, "the original goes on through the rest")
        v.check(seen["dead"] is not None and seen["dead"][3:5] == (15, 2),
                "SIR STRONGSWORD dies at the clock's day 2, hour 15 (0x16's 3780th minute) in "
                "the original", str(seen["dead"] and seen["dead"][1:6]))
        changed = low_changes(low_before, box.read(0, 0x600))
        print("  low memory words changed (not the keyboard buffer or the BIOS clock): "
              + ", ".join(f"{i:04x}" for i in changed))
        v.check(set(changed) <= {MOTOR}, "the original leaves the interrupt table and the "
                "BIOS's data as they were (but the motor timeout)", str(changed))
        where, clock = box.where(), L.clock(box)
        print("  waits at", where, "; clock", clock[1:6])
        v.check("4888:2d03" in where and clock[1:5] == (0, 0, 16, 2),
                "the original's rest ends at day 2, hour 16 in the camp's menu (4888:2d03)",
                f"{where}, clock {clock[1:5]}")
        raw = box.read(addr, L.REC)
        after = show(box, addr)
        print("  after:", after)
        v.check(raw[0x188] == 6 and raw[0x197] == 0 and box.far(addr + 0xE3) is None,
                "SIR STRONGSWORD in the original: status 6, 0 hit points, no effects", after)
        top, blocks = heap_blocks(box)
        print("  heap after:", heap(box))
        v.check(blocks[:1] == [("53ca:0000", 56)] and len(blocks) > 1
                and blocks[1][0] == "53d2:fb00", "the original's free list corrupted as "
                "recorded: 53ca:0000 (56), then the link counted down (53d2:fb00)",
                str(blocks[:2]))
        box.screenshot(work / "after.png")
        # Another rest: does the game go on?
        box.keys("RHAR")
        L.wait_rest(box, seconds=300)
        box.screenshot(work / "after-hour.png")
        where = box.where() if box.running else None
        print("  after another hour's rest:", "running" if box.running else "ENDED", where)
        v.check(box.running and "4888:2d03" in where,
                "the original rests another hour and waits in the camp's menu", str(where))
        print("  heap then:", heap(box) if box.running else "-")
        # And a save: the files the corrupted heap left in the records.
        box.keys(L.save_keys("H"))
        box.wait_input(settle=0.5, seconds=120)
        written = sorted(p.name for p in (box.game / "SAVE").glob("*H*.*")
                         if p.name.startswith(("CHRDATH", "SAVGAMH")))
        print("  saved as H:", written)
        v.check("SAVGAMH.DAT" in written and all(f"CHRDATH{n}.SAV" in written
                                                 for n in range(1, 7)),
                "the original saves the game as H afterwards", str(written))
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

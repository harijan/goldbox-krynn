#!/usr/bin/env python3
"""Check: a small picture of many frames with little memory free (docs/checks.md,
Checks against the original: "A small picture of many frames (PIC1 record
23, eight) with little memory free, in CGA, EGA and Tandy mode: count the
frames shown. 6961:00e4 stops loading them where MaxAvail falls under
0xf3c, which differs by mode ...; the port loads them all").

PIC1 record 23 (Maya at the outpost, ECL1 block 17 8318, behind several
quest variables) is far from any saved game; PIC1 record 12, the close-up
of Throtl's gate guards (four frames, 3,893 bytes each in the file), is a
step away from a saved game the port writes by Throtl (game A's party, the
caravan met), and goes through the same loader.

In each mode the original runs once with all its memory, and then with
HOOK.COM keeping more paragraphs below the game (hook_pad), so that less is
left when the close-up loads; Turbo Pascal's MaxAvail (the largest free
block or the space above HeapPtr: DS:4392's free list, DS:438a HeapPtr,
DS:438e HeapEnd) is read at "YOU ARE NEAR THROTL. DO YOU ENTER?", the frames
loaded (DS:6da2) and the frames shown (DS:6da3, polled for 5 seconds while
the menu animates) after Yes. The memory is not modelled by the port (it
loads and shows all four in every case: docs/front-end.md, Display modes), so there
the original's observations, batch F's table, are asserted (EXPECTED):
the frames by mode and MaxAvail, and where a GetMem fails, Turbo Pascal's
HeapError (DS:439a, set by 2f48:00b2 to 2f48:0042) showing "WARNING:
Insufficient Memory" in yellow on row 24 (1521:096c, a key wait), the
close-up not shown, DS:6da2 still 4 and DS:6da3 1 (the guards' sprite).
The screens are the port's: the loaded game, the question, and the
close-up taken while its first frame shows (eclplay does not animate a
menu's picture: its frame at the menu's read is the first).
"""
import argparse
import hashlib
import struct
import sys
import pathlib
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import f_lib  # noqa: E402

# It judges what the original shows as it comes: its boxes at their cycles
# (when it runs, not when another check imports it).
if __name__ == "__main__":
    checklib.cokbox.REAL_TIME = True

cokbox = checklib.cokbox

HEAP_PTR, HEAP_END, FREE_LIST = 0x438A, 0x438E, 0x4392
DS_COUNT, DS_FRAME, DS_ANIMATION = 0x6DA2, 0x6DA3, 0x4B4F
SEED = 1
LOAD_C = f_lib.load_letter("C")
# The original then shows the overland map; a step away and back asks; Yes.
STEPS = "m28"
ALL = (4, [1, 2, 3, 4])
WARNING = "WARNING"
# Batch F's table (SP/scope/dosbox-batch-f.md, #126): the MaxAvail aimed at
# (the pad is computed from the full run's) -> (MaxAvail measured before,
# (frames loaded, frames shown) or WARNING). Not here: Tandy at 32,000 (the
# run failed in batch F, nothing recorded) and about 10,000 (no wait
# reached in any mode, which a run cannot tell from a harness failure).
EXPECTED = {
    "tandy": {48000: (48000, ALL), 40000: (40000, ALL), 25300: (25312, (2, [1, 2])),
              18000: (18264, WARNING)},
    "ega": {48000: (48000, ALL), 40000: (40000, ALL), 32000: (32000, ALL),
            25300: (25312, (2, [1, 2])), 18000: (18264, WARNING)},
    "cga": {48000: (48000, ALL), 40000: (40000, ALL), 32000: (32000, ALL),
            25300: (25312, ALL), 18000: (18264, (3, [1, 2, 3]))},
}
BAND = 256   # bytes: MaxAvail measured within the recorded value's band
# Row 24 with `WARNING: Insufficient Memory` (yellow, 711 pixels): the colours
# of its 320x8 pixels, hashed, as batch F's closeup.png in Tandy and EGA mode.
WARNING_ROW = "db83dee0ada89efce43fb11354b0595f66d4aaa0"
WARNING_PIXELS = 711


def far(box, off):
    o, s = struct.unpack("<HH", box.ds_read(off, 4))
    return s * 16 + o


def max_avail(box):
    """MaxAvail: the largest free block or HeapEnd - HeapPtr (bytes)."""
    top, end = far(box, HEAP_PTR), far(box, HEAP_END)
    best = end - top
    o, s = struct.unpack("<HH", box.ds_read(FREE_LIST, 4))
    for _ in range(64):
        if s * 16 + o == top:
            break
        nxt_o, nxt_s, size_o, size_s = struct.unpack("<4H", box.read(s * 16 + o, 8))
        best = max(best, size_s * 16 + size_o)
        o, s = nxt_o, nxt_s
    return best


def first_frame_shot(box, path):
    """A screenshot while the close-up's first frame shows (DS:6da3 1 before
    and after it, a little after it turned 1)."""
    for _ in range(20):
        box._until(lambda: box.ds_byte(DS_FRAME) == 1, 30, "the close-up's first frame", 0.002)
        time.sleep(0.05)
        if box.ds_byte(DS_FRAME) != 1:
            continue
        shot = box.screenshot(path)
        if box.ds_byte(DS_FRAME) == 1:
            return shot
        box._until(lambda: box.ds_byte(DS_FRAME) != 1, 30, "the next frame", 0.002)
    raise cokbox.BoxError("no screenshot within the close-up's first frame")


def one(machine, saves, pad, work, verbose):
    """One run of the original: MaxAvail before and after, the frames
    loaded and shown, the screens."""
    with cokbox.Box(workdir=work, machine=machine, saves=[saves], seed=SEED, hook_pad=pad,
                    verbose=verbose, timeout=400) as box:
        box.keys(LOAD_C)
        box.wait_input(settle=1.0)
        loaded = box.screenshot(work / "loaded.png")
        box.keys(STEPS)
        box.wait_input(settle=0.5)
        question = box.screenshot(work / "question.png")
        before = max_avail(box)
        box.keys("y")
        box.wait_input(settle=0.5)
        count = box.ds_byte(DS_COUNT)
        seen = set()
        end = time.monotonic() + 5
        while time.monotonic() < end:
            seen.add(box.ds_byte(DS_FRAME))
            time.sleep(0.002)
        after = max_avail(box)
        closeup = first_frame_shot(box, work / "closeup.png")
        return dict(psp=box.psp, before=before, after=after, count=count, shown=sorted(seen),
                    animation=box.ds_byte(DS_ANIMATION), loaded=loaded, question=question,
                    closeup=closeup)


def row24(shot):
    row = shot.region(0, 192, 320, 8)
    return hashlib.sha1(bytes(row)).hexdigest(), sum(1 for c in row if c == 14)


def mode(v, machine, saves, work, verbose):
    kw = dict(machine=machine, saves=[saves], args=["--seed", str(SEED)])
    full = one(machine, saves, 0, work / "pad0", verbose)
    print(f"  {machine} all memory: PSP {full['psp']:04x}, MaxAvail {full['before']} before, "
          f"{full['after']} after; frames loaded {full['count']}, shown {full['shown']}, "
          f"animation {full['animation']}")
    v.check((full["count"], full["shown"]) == ALL,
            f"{machine} all memory: the four frames loaded and shown",
            f"{full['count']}, shown {full['shown']}")
    marks = [("loaded", LOAD_C), ("question", STEPS), ("closeup", "y")]
    port = checklib.frames_at(f_lib.marked(marks), work / "port", **kw)
    f_lib.judge(v, f"{machine} all memory:", {name: full[name] for name, _ in marks}, port,
                work)
    for target, (recorded, expected) in EXPECTED[machine].items():
        pad = max(0, (full["before"] - target) // 16)
        r = one(machine, saves, pad, work / f"pad{pad:x}", verbose)
        what = f"{machine} MaxAvail about {target} (pad {pad:#x})"
        print(f"  {what}: PSP {r['psp']:04x}, MaxAvail {r['before']} before, {r['after']} after; "
              f"frames loaded {r['count']}, shown {r['shown']} (the port: 4, not compared: "
              "memory is not modelled)")
        v.check(abs(r["before"] - recorded) <= BAND, f"{what}: MaxAvail before within "
                f"{BAND} of batch F's {recorded}", f"{r['before']}")
        digest, yellow = row24(r["closeup"])
        warning = digest == WARNING_ROW
        if expected == WARNING:
            v.check((r["count"], r["shown"]) == (4, [1]) and warning,
                    f"{what}: WARNING: Insufficient Memory on row 24, no close-up "
                    "(DS:6da2 4, DS:6da3 1)",
                    f"frames {r['count']}, shown {r['shown']}, row 24 {yellow} yellow pixels "
                    f"(the warning's {WARNING_PIXELS}), {'as' if warning else 'not as'} recorded")
            continue
        v.check((r["count"], r["shown"]) == expected and not warning,
                f"{what}: frames loaded and shown as batch F saw ({expected[0]}, "
                f"{expected[1]})", f"{r['count']}, {r['shown']}")
        v.screen(f"{what}: the close-up's first frame", r["closeup"], port.get("closeup"),
                 work=work)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--machine", action="append", choices=sorted(EXPECTED))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "frames")
    saves = f_lib.throtl_save(work, "C")
    v = checklib.Verdict()
    for machine in args.machine or list(EXPECTED):
        mode(v, machine, saves, work / machine, args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

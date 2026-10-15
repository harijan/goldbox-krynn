#!/usr/bin/env python3
"""Check: a small picture of many frames with little memory free (README,
Checks against the original: "A small picture of many frames (PIC1 record
23, eight) with little memory free, in CGA, EGA and Tandy mode: count the
frames shown. 6961:00e4 stops loading them where MaxAvail falls under
0xf3c, which differs by mode ...; the port loads them all").

PIC1 record 23 (Maya at the outpost, ECL1 block 17 8318, behind several
quest variables) is far from any saved game; PIC1 record 12, the close-up
of Throtl's gate guards (four frames, 3,893 bytes each in the file), is a
step away from a saved game the port writes by Throtl (as batch C's gate
save: game A's party, the caravan met), and goes through the same loader.

In each mode the original runs once with all its memory, and then with
HOOK.COM keeping more paragraphs below the game (hook_pad), so that less is
left when the close-up loads; Turbo Pascal's MaxAvail (the largest free
block or the space above HeapPtr: DS:4392's free list, DS:438a HeapPtr,
DS:438e HeapEnd) is read at "YOU ARE NEAR THROTL. DO YOU ENTER?", the frames
loaded (DS:6da2) and the frames shown (DS:6da3, polled for 5 seconds while
the menu animates) after Yes. The port always loads and shows all four
(eclplay's frames at the menu compared for the full-memory run).

Found: with less memory still, a GetMem fails and Turbo Pascal's HeapError
(DS:439a, set by 2f48:00b2 to 2f48:0042) shows "WARNING: Insufficient
Memory" in yellow on row 24 and waits for a key (1521:096c): the close-up is
not shown (DS:6da2 then still holds the previous picture's count) - see
closeup.png; with less again the run does not reach the question.
"""
import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

os.environ.setdefault("COKBOX_DISPLAYS", "160-169")

HEAP_PTR, HEAP_END, FREE_LIST = 0x438A, 0x438E, 0x4392
DS_COUNT, DS_FRAME, DS_ANIMATION = 0x6DA2, 0x6DA3, 0x4B4F
MIN_AVAIL = 0xF3C
TARGETS = (48000, 40000, 32000, 25300, 18000, 10000)
# Game A's party on the overland map by Throtl (the caravan met): decline the
# outpost, Move NE then N, decline Throtl, Exit, Encamp, Save as C.
GATE_SAVE = (["--load", "SAVE/SAVGAMA.DAT", "--play", "--set", "4bf2=11", "--set", "4bc3=1",
              "--set", "4bc4=3", "--set", "4c2d=1"], "nm98nEESC", 16)
LOAD_C = r"\kxP\v\v\rC\v\v\v\v\v\r"
# The original then shows the overland map (the port asks "near Throtl"
# first: eclplay declines with n); a step away and back asks; Yes.
STEPS = ["m", "2", "8"]


def port_save(out):
    args, keys, block = GATE_SAVE
    saves = out / "saves"
    if saves.exists():
        shutil.rmtree(saves)
    saves.mkdir(parents=True)
    for f in (cokbox.ROOT / "SAVE").iterdir():
        if f.is_file():
            shutil.copyfile(f, saves / f.name)
    subprocess.run([str(cokbox.ROOT / "build/eclplay"), *args, "--saves", str(saves),
                    "--keys", keys, str(cokbox.ROOT / "Assets"), str(block)],
                   cwd=cokbox.ROOT, check=True, capture_output=True, timeout=120)
    if not (saves / "SAVGAMC.DAT").exists():
        raise SystemExit("the port wrote no saved game C")
    return saves


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


def one(machine, saves, pad, work, verbose):
    """(MaxAvail before, frames loaded, frames shown, PSP, screen) for one run."""
    with cokbox.Box(workdir=work, machine=machine, saves=[saves], seed=1, hook_pad=pad,
                    verbose=verbose, timeout=400) as box:
        box.keys(LOAD_C)
        for k in STEPS:
            box.keys(k)
        box.wait_input(settle=0.5)
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
        shot = box.screenshot(work / "closeup.png")
        return dict(psp=box.psp, before=before, after=after, count=count, shown=sorted(seen),
                    animation=box.ds_byte(DS_ANIMATION), shot=shot)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--machine", action="append", choices=sorted(cokbox.MACHINES))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-frames-")).resolve()
    work.mkdir(parents=True, exist_ok=True)
    saves = port_save(work)
    for machine in args.machine or ["tandy", "ega", "cga"]:
        keys = LOAD_C + "n" + "".join(STEPS) + "y"
        _, _, eshots = cokbox.eclplay(keys, work / machine / "eclplay", machine=machine,
                                      saves=[saves], args=["--seed", "1"])
        full = one(machine, saves, 0, work / machine / "pad0", args.verbose)
        diffs = min(full["shot"].diff(cokbox.Screen.load(p))[0] for p in eshots[-4:])
        if full["count"] != 4:
            print(f"{machine}: no four frames with all memory: stopping here")
            continue
        # Pads leaving MaxAvail about TARGETS at the question (the close-up's
        # sprite and frames load after it).
        for target in TARGETS:
            pad = max(0, (full["before"] - target) // 16)
            try:
                r = one(machine, saves, pad, work / machine / f"pad{pad:x}", args.verbose)
            except (cokbox.BoxError, OSError) as e:
                print(f"{machine} pad {pad:#x} (MaxAvail about {target}): the run ended: {e}",
                      flush=True)
                continue
            print(f"{machine} pad {pad:#x}: PSP {r['psp']:04x}, MaxAvail {r['before']} before, "
                  f"{r['after']} after; frames loaded {r['count']}, shown {r['shown']} "
                  f"(the port: 4)", flush=True)
    print(f"files in {work}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

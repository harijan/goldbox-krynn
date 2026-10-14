#!/usr/bin/env python3
"""Check: Slow Poison's 0x16 running out on a poisoned character (README,
Checks against the original: "Timers ... cast Slow Poison on a poisoned
character and rest 63 hours").

The port writes the saved game: game A loaded (eclplay --load), SIR
STRONGSWORD given poison (0x37, no time), 0x16 for 30 minutes and 0x0f for
10, in that order, as Slow Poison leaves them but 0x16's 3780 minutes cut
short; camped at the outpost's inn and saved as game C. The original in
DOSBox-X loads C from the start menu, goes to the inn, camps and rests an
hour. At 30 minutes 0x16's handler kills the poisoned character and removes
0x0f, the effect the count's walk (57e4:0171) goes to next: the walk reads
the freed block (FreeMem wrote its size over the next pointer). The port
stops there; this records what the original does: the screen, the party's
records and their effects (walked in memory), and whether the game still
takes keys.
"""
import argparse
import pathlib
import struct
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

EFFECTS = ["1,37,0,0", "1,16,1e,0", "1,f,a,0"]
TO_CAMP = r"\r\r\r\r\r\r\rI\r"          # game A's start, to the outpost's inn
SAVE_C = TO_CAMP + "SC"
LOAD_C = r"\kxP\v\v\rC\v\v\v\v\v\r"     # title, Play, Load C, Begin Adventuring
INN = r"I\r"                             # the outpost's menu: the inn, camp
REST_HOUR = "RHAR"                       # Rest, Hours, Add (1), Rest


# Turbo Pascal 6's System variables before RandSeed (DS:43ae).
HEAP_PTR, FREE_LIST, PREFIX_SEG = 0x438A, 0x4392, 0x43A8


def heap(box):
    """HeapPtr, FreeList (as seg:off) and the free blocks (seg:off, size)."""
    def ptr(off):
        o, sg = struct.unpack("<HH", box.ds_read(off, 4))
        return sg, o
    blocks = []
    seg, off = ptr(FREE_LIST)
    top = ptr(HEAP_PTR)
    while (seg, off) != top and len(blocks) < 16:
        nxt_o, nxt_s, size_o, size_s = struct.unpack("<4H", box.read(seg * 16 + off, 8))
        blocks.append(("%04x:%04x" % (seg, off), size_s * 16 + size_o))
        seg, off = nxt_s, nxt_o
    return "HeapPtr %04x:%04x, free blocks %s" % (top + (blocks,))


def party(box):
    """The party's records: (name, status, hit points, effects as (id,
    minutes, value) and the effect list's far pointers)."""
    out = []
    rec = box.far(box.ds * 16 + 0x609A)
    while rec is not None and len(out) < 8:
        raw = box.read(rec, 409)
        name = raw[1:1 + min(raw[0], 15)].decode("latin-1")
        effects = []
        eff = box.far(rec + 0xE3)
        while eff is not None and len(effects) < 20:
            e = box.read(eff, 9)
            effects.append((e[0], struct.unpack_from("<H", e, 1)[0], e[3], "%04x:%04x" % (
                struct.unpack_from("<H", e, 7)[0], struct.unpack_from("<H", e, 5)[0])))
            eff = box.far(eff + 5)
            if eff is not None and eff < 0x600:  # into the interrupt table: stop
                effects.append(("next", "%05x" % eff))
                break
        out.append((name, raw[0x188], raw[0x197], effects))
        rec = box.far(rec + 0x17F)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.workdir or tempfile.mkdtemp(prefix="check-slow-poison-"))
    save = cokbox.ROOT / "SAVE" / "SAVGAMA.DAT"
    if not save.exists():
        print("needs the original's saved games in SAVE/")
        return 2
    effects = [a for e in EFFECTS for a in ("--effect", e)]
    # The port: the saved game, then the same rest.
    out, saves, _ = cokbox.eclplay(SAVE_C, work / "eclplay", saves=(), shots=False,
                                   start_menu=False, args=["--load", save, "--play", *effects])
    print("port, saving:", out.splitlines()[-1])
    port, _, _ = cokbox.eclplay(TO_CAMP + REST_HOUR, work / "eclplay-rest", shots=False,
                                start_menu=False, args=["--load", save, "--play", *effects])
    print("port, resting:\n  " + "\n  ".join(port.splitlines()[-7:]))

    with cokbox.Box(workdir=work / "box", saves=[saves], verbose=args.verbose,
                    timeout=900) as box:
        box.keys(LOAD_C + INN)
        box.wait_input(settle=0.5)
        box.screenshot(work / "camp.png")
        before = party(box)
        print("original, in camp:", before[0])
        box.keys("RHA")
        box.wait_input(settle=0.5)
        box.screenshot(work / "rest-menu.png")
        assert box.ds_word(PREFIX_SEG) == box.psp, "System's variables are not where expected"
        nodes = [int(e[3].split(":")[0], 16) * 16 for e in before[0][3][:2]]
        print("  heap before:", heap(box), "; 0x16 and 0x0f's blocks:",
              [box.read(a, 9).hex(" ") for a in nodes])
        low_before = box.read(0, 0x600)
        box.key("R")
        # Watch the rest: screenshots as fast as they come for a few seconds
        # (the hour passes in about a second), then the camp's menu again.
        seen = []
        end = time.monotonic() + 6
        while time.monotonic() < end and box.running:
            shot = box.screenshot()
            if not seen or shot.pixels != seen[-1].pixels:
                shot.save_png(work / f"rest-{len(seen):02d}.png")
                seen.append(shot)
        box.wait_input(settle=0.5)
        low_after = box.read(0, 0x600)
        changed = [i for i in range(0, 0x600, 2) if low_before[i:i + 2] != low_after[i:i + 2]
                   and not 0x41A <= i < 0x440 and not 0x46C <= i < 0x472]
        print(f"original: {len(seen)} different screens during the rest; low memory words "
              "changed (the interrupt table, BIOS data; not the keyboard buffer or the "
              "clock): " + ", ".join(f"{i:04x}: {low_before[i:i + 2].hex()} -> "
                                    f"{low_after[i:i + 2].hex()}" for i in changed))
        print("original, after the rest:", "running" if box.running else "ended")
        if box.running:
            print("  where:", box.where())
            for member in party(box)[:2]:
                print("  ", member)
            print("  heap after:", heap(box), "; 0x16 and 0x0f's blocks:",
                  [box.read(a, 9).hex(" ") for a in nodes])
            box.screenshot(work / "after.png")
            # Does it go on? Enter, then the camp's menu again.
            box.keys(r"\r")
            box.wait_input(settle=0.5, seconds=30)
            box.screenshot(work / "after-enter.png")
            print("  after Enter, where:", box.where())
        log = (work / "box" / "dosbox.log").read_text(errors="replace")
        print("DOSBox-X log tail:", log.strip().splitlines()[-3:])
    print(f"files in {work}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

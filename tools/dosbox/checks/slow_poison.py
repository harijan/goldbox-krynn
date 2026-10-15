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
stops there (not compared beyond: it must stop with that reason after the
same messages); the original must do what phase 1 recorded: the rest goes
on, SIR STRONGSWORD dead (status 6, 0 hit points, no effects), the
interrupt table untouched (low memory changed at most in the BIOS's motor
timeout, 0040:0040), the free list corrupted as recorded (its first block
53ca:0000 of 56 bytes, then the freed 0x0f's 53d3:0000, whose link the walk
counted down to 53dc:fb00), and the camp's menu still taking keys (Enter
opens Save, 4b6d:2327).
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402

cokbox = checklib.cokbox

EFFECTS = ["1,37,0,0", "1,16,1e,0", "1,f,a,0"]
TO_CAMP = r"\r\r\r\r\r\r\rI\r"          # game A's start, to the outpost's inn
SAVE_C = TO_CAMP + "SC"
LOAD_C = r"\kxP\v\v\rC\v\v\v\v\v\r"     # title, Play, Load C, Begin Adventuring
INN = r"I\r"                             # the outpost's menu: the inn, camp
REST_HOUR = "RHAR"                       # Rest, Hours, Add (1), Rest


# Turbo Pascal 6's System variables before RandSeed (DS:43ae).
HEAP_PTR, FREE_LIST, PREFIX_SEG = 0x438A, 0x4392, 0x43A8


def heap_blocks(box):
    """(HeapPtr, the free list's blocks) as ('seg:off', size), at most 16."""
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
    return "%04x:%04x" % top, blocks


def heap(box):
    """HeapPtr, FreeList (as seg:off) and the free blocks (seg:off, size)."""
    return "HeapPtr %s, free blocks %s" % heap_blocks(box)


def low_changes(before, after):
    """The words of low memory (0-0x5ff) that changed, not the keyboard
    buffer's or the BIOS clock's."""
    return [i for i in range(0, 0x600, 2) if before[i:i + 2] != after[i:i + 2]
            and not 0x41A <= i < 0x440 and not 0x46C <= i < 0x472]


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


# The BIOS's motor timeout (0040:0040), counted down by its timer.
MOTOR = 0x440


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    checklib.need_saves()
    work = checklib.workdir(args.workdir, "slow-poison")
    v = checklib.Verdict()
    save = cokbox.ROOT / "SAVE" / "SAVGAMA.DAT"
    effects = [a for e in EFFECTS for a in ("--effect", e)]
    # The port: the saved game, then the same rest.
    out, saves, _ = cokbox.eclplay(SAVE_C, work / "eclplay", saves=(), shots=False,
                                   start_menu=False, args=["--load", save, "--play", *effects])
    if not (saves / "SAVGAMC.DAT").exists():
        raise cokbox.BoxError("the port saved no game C: " + out[-300:])
    port, _, _ = cokbox.eclplay(TO_CAMP + REST_HOUR, work / "eclplay-rest", shots=False,
                                start_menu=False, args=["--load", save, "--play", *effects])
    said = [ln for ln in port.splitlines() if ln.startswith(("print: ", "error: "))]
    print("port, resting:\n  " + "\n  ".join(said[-6:]))
    v.check(said[-6:] == ["print: takes 1 point of damage from Magic", "print: SIR STRONGSWORD",
                          "print: takes 1 point of damage from Magic", "print: SIR STRONGSWORD",
                          "print: dies from poison",
                          "error: a handler removed effect 0x0f as it was counted down; the "
                          "original reads it after freeing it"],
            "the port stops where 0x0f is read after it was freed, after the same messages "
            "(it is not compared beyond)")

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
        if box.ds_word(PREFIX_SEG) != box.psp:
            raise cokbox.BoxError("System's variables are not where expected")
        nodes = [int(e[3].split(":")[0], 16) * 16 for e in before[0][3][:2]]
        print("  heap before:", heap(box), "; 0x16 and 0x0f's blocks:",
              [box.read(a, 9).hex(" ") for a in nodes])
        low_before = box.read(0, 0x600)
        box.key("R")
        # The rest: screenshots as fast as they come for a few seconds (the
        # hour passes in about a second), then the camp's menu again.
        seen = checklib.watch(box, 6, work, "rest")
        box.wait_input(settle=0.5)
        changed = low_changes(low_before, box.read(0, 0x600))
        print(f"original: {len(seen)} different screens during the rest; low memory words "
              "changed: " + ", ".join(f"{i:04x}" for i in changed))
        v.check(set(changed) <= {MOTOR}, "the original leaves the interrupt table and the "
                "BIOS's data as they were (but the motor timeout)", str(changed))
        v.check(box.running, "the original goes on after the rest")
        after = party(box)
        print("  where:", box.where())
        for member in after[:2]:
            print("  ", member)
        v.check(after[0][:4] == ("SIR STRONGSWORD", 6, 0, []), "SIR STRONGSWORD dies from "
                "poison in the original: status 6, 0 hit points, no effects", str(after[0]))
        top, blocks = heap_blocks(box)
        print("  heap after:", heap(box), "; 0x16 and 0x0f's blocks:",
              [box.read(a, 9).hex(" ") for a in nodes])
        v.check(blocks[:2] == [("53ca:0000", 56), ("53d3:0000", 16)] and len(blocks) > 2
                and blocks[2][0] == "53dc:fb00", "the original's free list corrupted as "
                "recorded: the freed 0x0f's link counted down to 53dc:fb00", str(blocks[:3]))
        box.screenshot(work / "after.png")
        # Does it go on? Enter (Save's letters, as after Encamp).
        box.keys(r"\r")
        box.wait_input(settle=0.5, seconds=30)
        box.screenshot(work / "after-enter.png")
        where = box.where()
        print("  after Enter, where:", where)
        v.check("4b6d:2327" in where, "the original's camp menu takes Enter (Save's letters)",
                str(where))
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))

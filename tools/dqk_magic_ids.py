#!/usr/bin/env python3
"""Dark Queen's spell and effect handler tables, from the stores their fill
routines make in build/DQK_FULL.EXE (stdlib only; make dqk-merged builds it).

The original fills DS:af45 + 4 * id (spells) and DS:bee8 + 4 * id (effects)
with far pointers, by straight-line stores run at start-up:

    spells   7fd8:46bb (ids 1-0x2c, 0x8a-0x8c) and 7fd8:4892 (0x2d-0x89)
    effects  78e4:4fb2 (0-0x7f) and 78e4:549e (0x53-0xe5, called by 4fb2)

A store is `movw $imm, disp16` (c7 06: an offset, or an explicit segment) or
`mov %cs, disp16` (8c 0e: the routine's own segment). This tool reads the
stores in each routine's range, expects one offset and one segment store per
id a routine fills, and prints one C line per id for
src/rules_dark_queen_magic.c. Ghidra's numbering of an explicit segment is
the stored value + 0x1000 (`movw $0x6e76` is 7e76).

Two things are read from the routines themselves, not from the stores:
- an overlay stub (an `ljmp seg:off`, 0xea, at the stored address: the merge
  rewrites each stub so, docs/disassembly.md) is followed to its routine, and
  the line cites the routine; the line keeps the stored offset;
- a routine whose body is `push bp; mov bp,sp; pop bp; retf` (55 8b ec 5d cb)
  does nothing: an effect that is COK_FX_NOTHING.
An id no store fills is `{0}` for an effect; for a spell it is a stop citing
its bytes (DS:af45 + 4 * id is 0:0), since cok_cast_on and cok_cast_spell
would otherwise reach the wrong check. The test (tests/test_dark_queen.c)
reads the same bytes again from the executable.

Usage: tools/dqk_magic_ids.py [build/DQK_FULL.EXE]    (prints the C lines)
"""
import struct
import sys

SPELLS = {
    "base": 0xAF45,
    "count": 0x8D,
    "routines": [("7fd8", 0x46BB, 0x4892), ("7fd8", 0x4892, 0x4C2C)],
}
EFFECTS = {
    "base": 0xBEE8,
    "count": 0xE6,
    "routines": [("78e4", 0x4FB2, 0x549E), ("78e4", 0x549E, 0x5A00)],
}
NOOP = bytes([0x55, 0x8B, 0xEC, 0x5D, 0xCB])  # push bp; mov bp,sp; pop bp; retf


def image_of(data):
    """The file offset of the load image (the header's size)."""
    return struct.unpack_from("<H", data, 8)[0] * 16


def at(header, seg, off):
    """The file offset of seg:off (Ghidra numbering)."""
    return header + (int(seg, 16) - 0x1000) * 16 + off


def stores(data, header, seg, start, end, lo, hi):
    """(disp, imm, offset) of each store into [lo, hi) in the routine seg:start-end.
    imm is None for mov %cs."""
    base = at(header, seg, 0)
    out = []
    for i in range(base + start, base + end - 5):
        if data[i] == 0xC7 and data[i + 1] == 0x06:
            disp, imm = struct.unpack_from("<HH", data, i + 2)
            if lo <= disp < hi:
                out.append((disp, imm, i - base))
        elif data[i] == 0x8C and data[i + 1] == 0x0E:
            disp = struct.unpack_from("<H", data, i + 2)[0]
            if lo <= disp < hi:
                out.append((disp, None, i - base))
    return out


def table(data, header, spec):
    """id -> (offset, Ghidra segment) for each id the routines fill."""
    base, count = spec["base"], spec["count"]
    cells = {}
    for seg, start, end in spec["routines"]:
        for disp, imm, off in stores(data, header, seg, start, end, base, base + 4 * count + 2):
            if (disp - base) % 2:
                raise SystemExit("unaligned store at %s:%04x" % (seg, off))
            cells.setdefault(disp, []).append((imm, seg))
    result = {}
    for k in range(count):
        off = cells.get(base + 4 * k, [])
        sg = cells.get(base + 4 * k + 2, [])
        if not off and not sg:
            continue
        if len(off) != 1 or len(sg) != 1 or off[0][0] is None:
            raise SystemExit("id %02x: %d offset and %d segment stores" % (k, len(off), len(sg)))
        offset = off[0][0]
        imm, seg = sg[0]
        ghidra = seg if imm is None else "%04x" % (imm + 0x1000)
        result[k] = (offset, ghidra)
    return result


def routine(data, header, seg, off):
    """The routine a stored far pointer reaches: an overlay stub (0xea, ljmp
    seg:off) is followed to its target, in Ghidra numbering."""
    for _ in range(4):
        i = at(header, seg, off)
        if data[i] != 0xEA:
            return seg, off
        off, s = struct.unpack_from("<HH", data, i + 1)
        seg = "%04x" % (s + 0x1000)
    raise SystemExit("a stub chain at %s:%04x does not end" % (seg, off))


def lines(data, header, spec, filled, cast):
    """One C line per id 0 to count - 1."""
    fills = ", ".join("%s:%04x" % (seg, start) for seg, start, _ in spec["routines"])
    out = []
    for k in range(spec["count"]):
        if k not in filled:
            if cast and k:
                out.append('[0x%02x] = {0, COK_CAST_NONE, "DS:%04x + 0x%x is 0:0, never stored (%s)"},'
                           % (k, spec["base"], 4 * k, fills))
            else:
                out.append("[0x%02x] = {0}, /* no store fills it */" % k)
            continue
        off, stored = filled[k]
        seg, target = routine(data, header, stored, off)
        if cast:
            out.append('[0x%02x] = {0x%04x, COK_CAST_NONE, "dqk %s:%04x"},' % (k, off, seg, target))
        elif data[at(header, seg, target):at(header, seg, target) + 5] == NOOP:
            out.append("[0x%02x] = {0x%04x, COK_FX_NOTHING}, /* does nothing: retf */" % (k, off))
        else:
            out.append('[0x%02x] = {0x%04x, COK_FX_MISSING, "dqk %s:%04x"},' % (k, off, seg, target))
    return out


def main(argv):
    exe = argv[1] if len(argv) > 1 else "build/DQK_FULL.EXE"
    with open(exe, "rb") as f:
        data = f.read()
    header = image_of(data)
    for line in lines(data, header, SPELLS, table(data, header, SPELLS), True):
        print(line)
    for line in lines(data, header, EFFECTS, table(data, header, EFFECTS), False):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

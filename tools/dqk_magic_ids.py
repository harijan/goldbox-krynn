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
the stored value + 0x1000 (`movw $0x6e76` is 7e76). The test
(tests/test_dark_queen.c) reads the same stores again from the executable.

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


def image_of(data):
    """The file offset of the load image (the header's size)."""
    return struct.unpack_from("<H", data, 8)[0] * 16


def stores(data, header, seg, start, end, lo, hi):
    """(disp, imm, offset) of each store into [lo, hi) in the routine seg:start-end.
    imm is None for mov %cs."""
    base = header + (int(seg, 16) - 0x1000) * 16
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


def table(path, spec):
    """id -> (offset, Ghidra segment) for each id the routines fill."""
    with open(path, "rb") as f:
        data = f.read()
    header = image_of(data)
    base, count = spec["base"], spec["count"]
    cells = {}
    for seg, start, end in spec["routines"]:
        for disp, imm, at in stores(data, header, seg, start, end, base, base + 4 * count + 2):
            if (disp - base) % 2:
                raise SystemExit("unaligned store at %s:%04x" % (seg, at))
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


def lines(spec, filled, cast):
    """One C line per id 0 to count - 1."""
    out = []
    for k in range(spec["count"]):
        if k not in filled:
            out.append("[0x%02x] = {0}, /* no store fills it */" % k)
            continue
        off, seg = filled[k]
        if cast:
            out.append('[0x%02x] = {0x%04x, COK_CAST_NONE, "dqk %s:%04x"},' % (k, off, seg, off))
        elif (off, seg) == (0x4F2F, "78e4"):
            out.append("[0x%02x] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */" % k)
        else:
            out.append('[0x%02x] = {0x%04x, COK_FX_MISSING, "dqk %s:%04x"},' % (k, off, seg, off))
    return out


def main(argv):
    exe = argv[1] if len(argv) > 1 else "build/DQK_FULL.EXE"
    for line in lines(SPELLS, table(exe, SPELLS), True) + lines(EFFECTS, table(exe, EFFECTS), False):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

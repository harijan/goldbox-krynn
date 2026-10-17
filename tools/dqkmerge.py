#!/usr/bin/env python3
"""Merge DQK.EXE's overlays into one plain EXE for disassembly (stdlib only).

The Dark Queen of Krynn has no GAME.OVR: its overlays are stored in DQK.EXE
itself, after the part DOS loads, and are read by an overlay manager in the
segment of the entry point.  The manager keeps a table of 18-byte entries:

    +00  load segment            +08  paragraphs in memory
    +02  -> file name ("DQK.EXE") +0a  relocations (offset, segment pairs)
    +04  file paragraph (low)     +0c  parent overlay, loaded with it, or -1
    +06  file paragraph (high)    +0e  (unused here)
    +07  flags: 1 = load at start +10  paragraphs read from the file

At the file paragraph come the relocations, then (rounded up to a paragraph)
the code.  Overlays share load areas as a tree: a child loads above its
parent, siblings at the same segment, so a segment number in an overlay
means itself or one of its parents.  The routines are called through
10-byte stubs, `CALL handler / JMP FAR seg:off / DW overlay`.

This tool keeps the root image and every overlay loaded at start (the data
segment and stack) where they run, gives every other overlay a segment of
its own above the program, rewrites each relocated segment number to the
copy it means, and makes each stub a JMP FAR to its routine, so a
disassembler sees the whole program at fixed addresses.
"""
import argparse
import pathlib
import struct
import sys
from collections import Counter

import ovrmerge

ENTRY_SIZE = 18
STUB_SIZE = 10


class FormatError(Exception):
    pass


def find_table(image, manager):
    """Return (table offset, count) in the manager's segment.

    The manager finds an entry with `MOV BX,12h / MUL BX / MOV BX,AX /
    ADD BX,table` and walks the table with `MOV CX,[count] / MOV SI,table`."""
    seg = image[manager * 16:manager * 16 + 0x10000]
    at = seg.find(bytes.fromhex("bb1200f7e38bd881c3"))
    if at < 0:
        raise FormatError("no overlay table lookup in the manager's segment")
    (table,) = struct.unpack_from("<H", seg, at + 9)
    at = seg.find(b"\xbe" + struct.pack("<H", table))
    if at < 4 or seg[at - 4:at - 2] != b"\x8b\x0e":
        raise FormatError("no overlay count in the manager's segment")
    (count_at,) = struct.unpack_from("<H", seg, at - 2)
    (count,) = struct.unpack_from("<H", seg, count_at)
    return table, count


def read_entries(image, manager, table, count, exe):
    entries = []
    base = manager * 16 + table
    for number in range(count):
        load, name, pos, high, flags, paras, nrel, parent, _, read = struct.unpack_from(
            "<HHHBBHHhHH", image, base + number * ENTRY_SIZE)
        if flags & 8:
            (name,) = struct.unpack_from("<H", image, manager * 16 + name)
        end = image.find(b"\0", manager * 16 + name + 1)
        file_name = image[manager * 16 + name + 1:end].decode("ascii", "replace")
        start = (high << 16 | pos) * 16
        code = start + ((nrel + 3) >> 2) * 16
        if code + read * 16 > len(exe) or read > paras:
            raise FormatError(f"overlay {number} lies outside {file_name}")
        relocs = list(struct.iter_unpack("<HH", exe[start:start + 4 * nrel]))
        entries.append(dict(number=number, load=load, paras=paras, flags=flags,
                            parent=parent, file=file_name, start=start,
                            code=exe[code:code + read * 16], relocs=relocs))
    return entries


def find_stubs(image, manager, count):
    """Return [(offset in the manager's segment, handler, overlay, target)]."""
    seg = image[manager * 16:manager * 16 + 0x10000]
    found = []
    for at in range(len(seg) - STUB_SIZE + 1):
        if seg[at] == 0xE8 and seg[at + 3] == 0xEA:
            (rel,) = struct.unpack_from("<h", seg, at + 1)
            off, target_seg, overlay = struct.unpack_from("<HHH", seg, at + 4)
            if overlay < count:
                found.append((at, (at + 3 + rel) & 0xFFFF, overlay, (target_seg, off)))
    if not found:
        raise FormatError("no overlay stubs")
    handler = Counter(stub[1] for stub in found).most_common(1)[0][0]
    return [stub for stub in found if stub[1] == handler]


def merge(exe):
    """Return (merged EXE bytes, map text)."""
    image, (cs, ip, ss, sp), relocs, memory_paras = ovrmerge.load(exe)
    manager = cs
    table, count = find_table(image, manager)
    entries = read_entries(image, manager, table, count, exe)
    stubs = find_stubs(image, manager, count)
    image_paras = (len(image) + 15) // 16

    # Where each overlay goes: resident ones where they run, the rest above.
    out = bytearray(image) + bytes(memory_paras * 16 - len(image))
    for entry in entries:
        if entry["flags"] & 1:
            entry["seg"] = entry["load"]
            end = (entry["load"] + entry["paras"]) * 16
            if end > len(out):
                out += bytes(end - len(out))
    for entry in entries:
        if not entry["flags"] & 1:
            entry["seg"] = len(out) // 16
            out += bytes(entry["paras"] * 16)
    for entry in entries:
        at = entry["seg"] * 16
        out[at:at + len(entry["code"])] = entry["code"]

    def ancestry(entry):
        chain = [entry]
        while chain[-1]["parent"] >= 0:
            chain.append(entries[chain[-1]["parent"]])
        return chain + [e for e in entries if e["flags"] & 1 and e not in chain]

    def translate(value, chain):
        """The merged segment for VALUE as code in CHAIN's first overlay sees it."""
        if value < image_paras:
            return value, None
        for entry in chain:
            if entry["load"] <= value < entry["load"] + entry["paras"]:
                return value - entry["load"] + entry["seg"], entry
        return value, False

    unresolved = []
    stub_slots = {manager * 16 + at + 6 for at, _, _, _ in stubs}

    load_fields = {manager * 16 + table + n * ENTRY_SIZE for n in range(count)}

    # The root's relocations: stubs and the table's load segments are
    # rewritten below; any other segment number in an overlay area is left
    # alone (it means whichever overlay is loaded there) and listed in the map.
    merged = []
    for seg, off in relocs:
        where = seg * 16 + off
        if where in stub_slots:
            continue
        merged.append((seg, off))
        (value,) = struct.unpack_from("<H", out, where)
        if where not in load_fields and value >= image_paras and not any(
                e["flags"] & 1 and e["load"] <= value < e["load"] + e["paras"] for e in entries):
            unresolved.append(f"root {seg:04x}:{off:04x} holds {value:04x}")

    # Each overlay's relocations: the place to patch and the value both
    # mean the overlay itself or one of its parents.
    for entry in entries:
        chain = ancestry(entry)
        for off, seg in entry["relocs"]:
            new_seg, owner = translate(seg, chain)
            if owner in (None, False):
                raise FormatError(f"overlay {entry['number']} relocates {seg:04x}:{off:04x}, outside itself")
            where = new_seg * 16 + off
            (value,) = struct.unpack_from("<H", out, where)
            new_value, found = translate(value, chain)
            if found is False:
                unresolved.append(f"overlay {entry['number']} {seg:04x}:{off:04x} holds {value:04x}")
            struct.pack_into("<H", out, where, new_value)
            merged.append((new_seg, off))

    lines = [
        f"entry {cs:04x}:{ip:04x}  stack {ss:04x}:{sp:04x}  root image {len(image):#x} bytes",
        f"overlay manager {manager:04x}  table {manager:04x}:{table:04x}  {count} overlays  {len(stubs)} stubs",
        "addresses are relative to the load segment",
        "",
    ]
    for entry in entries:
        lines.append(
            f"overlay {entry['number']:2}  runs at {entry['load']:04x}  merged {entry['seg']:04x}  "
            f"paras {entry['paras']:#06x}  parent {entry['parent']:2}  "
            f"{entry['file']} {entry['start']:#07x}  relocs {len(entry['relocs'])}"
            + ("  (loaded at start)" if entry["flags"] & 1 else ""))
        struct.pack_into("<H", out, manager * 16 + table + entry["number"] * ENTRY_SIZE, entry["seg"])
        for at, _, number, (target_seg, off) in stubs:
            if number != entry["number"]:
                continue
            new_seg, owner = translate(target_seg, [entry])
            if owner is not entry:
                raise FormatError(f"stub {manager:04x}:{at:04x} jumps to {target_seg:04x}:{off:04x}, "
                                  f"outside overlay {number}")
            struct.pack_into("<BHH", out, manager * 16 + at, 0xEA, off, new_seg)
            merged.append((manager, at + 3))
            lines.append(f"    {manager:04x}:{at:04x} -> {new_seg:04x}:{off:04x}")
    if unresolved:
        lines += ["", "segment numbers in an overlay area that no overlay owns:"] + unresolved
    if len(merged) > 0xFFFF:
        raise FormatError("too many relocations for one MZ header")

    header_paras = (0x1C + 4 * len(merged) + 15) // 16
    size = header_paras * 16 + len(out)
    header = struct.pack("<2s13H", b"MZ", size % 512, (size + 511) // 512, len(merged),
                         header_paras, 0, 0xFFFF, ss, sp, 0, ip, cs, 0x1C, 0)
    reloc_table = b"".join(struct.pack("<HH", off, seg) for seg, off in merged)
    result = (header + reloc_table).ljust(header_paras * 16, b"\0") + out
    return result, "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=pathlib.Path, help="DQK.EXE")
    parser.add_argument("output", type=pathlib.Path, help="merged EXE to write")
    parser.add_argument("--map", type=pathlib.Path, help="write the overlay map here")
    args = parser.parse_args()
    try:
        merged, overlay_map = merge(args.exe.read_bytes())
    except (FormatError, ovrmerge.FormatError, struct.error) as error:
        sys.exit(f"dqkmerge: {error}")
    args.output.write_bytes(merged)
    if args.map:
        args.map.write_text(overlay_map)


if __name__ == "__main__":
    main()

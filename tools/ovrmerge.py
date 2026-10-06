#!/usr/bin/env python3
"""Merge START.EXE and GAME.OVR into one plain EXE for disassembly (stdlib only).

START.EXE is EXEPACK-compressed and calls GAME.OVR through Turbo Pascal
overlay stubs: one INT 3Fh segment per overlaid code segment, holding the
segment's place in GAME.OVR and a 5-byte entry per exported routine.  This
tool unpacks the root image, appends every overlay segment above the
program's stack, turns the overlay fixups into ordinary MZ relocations and
patches each stub entry into a JMP FAR to the loaded routine, so a
disassembler sees the whole program at fixed addresses.
"""
import argparse
import pathlib
import struct
import sys

STUB_HEADER = 0x20
ENTRY_SIZE = 5


class FormatError(Exception):
    pass


def unexepack(exe):
    """Return (image, (cs, ip, ss, sp), relocs, memory_paras) for an EXEPACK file."""
    if exe[:2] != b"MZ":
        raise FormatError("not an MZ executable")
    last, pages, _, header_paras, min_alloc, _, _, _, _, _, packed_cs = struct.unpack_from("<11H", exe, 2)
    end = (pages - 1) * 512 + (last or 512)
    module = exe[header_paras * 16:end]
    head = packed_cs * 16
    ip, cs, _, _, sp, ss, dest_paras, skip = struct.unpack_from("<8H", module, head)
    if module[head + 16:head + 18] != b"RB":
        if module[head + 14:head + 16] != b"RB":
            raise FormatError("no EXEPACK header")
        skip = 1

    # Decompress backwards from the end of the packed data.
    buf = bytearray(dest_paras * 16)
    buf[:head] = module[:head]
    src = head
    while src > 0 and buf[src - 1] == 0xFF:
        src -= 1
    dst = (dest_paras - skip + 1) * 16
    while True:
        command = buf[src - 1]
        length = buf[src - 3] | buf[src - 2] << 8
        src -= 3
        if command & 0xFE == 0xB0:
            src -= 1
            buf[dst - length:dst] = bytes([buf[src]]) * length
        elif command & 0xFE == 0xB2:
            buf[dst - length:dst] = buf[src - length:src]
            src -= length
        else:
            raise FormatError(f"bad EXEPACK command {command:#04x}")
        dst -= length
        if command & 1:
            break

    # Sixteen groups of offsets, one per 64 KB of image, follow the stub's message.
    message = module.find(b"Packed file is corrupt", head)
    if message < 0:
        raise FormatError("no EXEPACK relocation table")
    at = message + len(b"Packed file is corrupt")
    relocs = []
    for group in range(16):
        (count,) = struct.unpack_from("<H", module, at)
        relocs += [(group * 0x1000, off) for off in struct.unpack_from(f"<{count}H", module, at + 2)]
        at += 2 + 2 * count

    memory = max(dest_paras * 16, ss * 16 + sp, len(module) + min_alloc * 16)
    return bytes(buf), (cs, ip, ss, sp), relocs, (memory + 15) // 16


def find_stubs(image, ovr):
    """Return [(stub_seg, file_offset, code_size, fixup_size, entry_offsets)] in GAME.OVR order."""
    if ovr[:4] != b"FBOV" or struct.unpack_from("<I", ovr, 4)[0] != len(ovr) - 8:
        raise FormatError("GAME.OVR has no FBOV header")
    stubs = []
    at = 0
    while at + STUB_HEADER <= len(image):
        if image[at:at + 2] == b"\xcd\x3f":
            offset, code, fixups, count = struct.unpack_from("<IHHH", image, at + 4)
            table = at + STUB_HEADER
            if (0 < count and offset + code + fixups <= len(ovr) and fixups % 2 == 0
                    and table + count * ENTRY_SIZE <= len(image)
                    and all(image[table + i * ENTRY_SIZE:table + i * ENTRY_SIZE + 2] == b"\xcd\x3f"
                            for i in range(count))):
                entries = [struct.unpack_from("<H", image, table + i * ENTRY_SIZE + 2)[0]
                           for i in range(count)]
                stubs.append((at // 16, offset, code, fixups, entries))
                at = (table + count * ENTRY_SIZE + 15) & ~15
                continue
        at += 16
    stubs.sort(key=lambda stub: stub[1])
    expected = 8
    for seg, offset, code, fixups, _ in stubs:
        if offset != expected:
            raise FormatError(f"stub {seg:04x} starts at GAME.OVR {offset:#x}, expected {expected:#x}")
        expected += code + fixups
    if expected != len(ovr):
        raise FormatError(f"stubs cover GAME.OVR up to {expected:#x} of {len(ovr):#x}")
    return stubs


def merge(exe, ovr):
    """Return (merged EXE bytes, map text)."""
    image, (cs, ip, ss, sp), relocs, memory_paras = unexepack(exe)
    stubs = find_stubs(image, ovr)
    out = bytearray(image) + bytes(memory_paras * 16 - len(image))
    lines = [
        f"entry {cs:04x}:{ip:04x}  stack {ss:04x}:{sp:04x}  root image {len(image):#x} bytes",
        f"overlays from segment {memory_paras:04x}; addresses are relative to the load segment",
        "",
    ]
    for number, (stub, offset, code, fixups, entries) in enumerate(stubs):
        seg = len(out) // 16
        out += ovr[offset:offset + code]
        out += bytes(-len(out) % 16)
        relocs += [(seg, off) for off in struct.unpack_from(f"<{fixups // 2}H", ovr, offset + code)]
        lines.append(f"overlay {number:2}  stub {stub:04x}  loaded {seg:04x}  "
                     f"GAME.OVR {offset:#07x}  code {code:#06x}  fixups {fixups // 2}")
        for i, entry in enumerate(entries):
            slot = stub * 16 + STUB_HEADER + i * ENTRY_SIZE
            out[slot:slot + ENTRY_SIZE] = struct.pack("<BHH", 0xEA, entry, seg)
            relocs.append((stub, slot - stub * 16 + 3))
            lines.append(f"    {stub:04x}:{slot - stub * 16:04x} -> {seg:04x}:{entry:04x}")
    if len(relocs) > 0xFFFF:
        raise FormatError("too many relocations for one MZ header")

    header_paras = (0x1C + 4 * len(relocs) + 15) // 16
    size = header_paras * 16 + len(out)
    header = struct.pack("<2s13H", b"MZ", size % 512, (size + 511) // 512, len(relocs),
                         header_paras, 0, 0xFFFF, ss, sp, 0, ip, cs, 0x1C, 0)
    table = b"".join(struct.pack("<HH", off, seg) for seg, off in relocs)
    merged = (header + table).ljust(header_paras * 16, b"\0") + out
    return merged, "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=pathlib.Path, help="START.EXE")
    parser.add_argument("ovr", type=pathlib.Path, help="GAME.OVR")
    parser.add_argument("output", type=pathlib.Path, help="merged EXE to write")
    parser.add_argument("--map", type=pathlib.Path, help="write the overlay map here")
    args = parser.parse_args()
    try:
        merged, overlay_map = merge(args.exe.read_bytes(), args.ovr.read_bytes())
    except (FormatError, struct.error) as error:
        sys.exit(f"ovrmerge: {error}")
    args.output.write_bytes(merged)
    if args.map:
        args.map.write_text(overlay_map)


if __name__ == "__main__":
    main()

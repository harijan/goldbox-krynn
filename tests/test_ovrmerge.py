"""Exercise tools/ovrmerge.py with a hand-built EXEPACK root and overlay (stdlib only)."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import ovrmerge  # noqa: E402

# One overlaid routine: push bp; mov bp,sp; call far 0002:0000; pop bp; retf.
CODE = bytes.fromhex("5589e5") + bytes.fromhex("9a00000200") + bytes.fromhex("5dcb")
FIXUPS = struct.pack("<H", 6)


def build_exe():
    """An 80-byte root image: 32 zero bytes, then a stub at segment 2 with one entry."""
    stub = struct.pack("<2sHIHHH", b"\xcd\x3f", 0, 8, len(CODE), len(FIXUPS), 1)
    stub = stub.ljust(ovrmerge.STUB_HEADER, b"\0") + b"\xcd\x3f\x00\x00\x00"
    tail = stub.ljust(48, b"\0")
    # Commands run backwards: copy the tail into place, then zero-fill the rest.
    packed = b"\x00" + struct.pack("<H", 32) + b"\xb1" + tail + struct.pack("<H", len(tail)) + b"\xb2"
    packed = packed.ljust(-(-len(packed) // 16) * 16, b"\xff")
    header = struct.pack("<8H", 0x10, 0, 0, 0, 0x100, 0, 5, 1) + b"RB"
    relocs = struct.pack("<HH", 1, 0x10) + struct.pack("<H", 0) * 15
    module = packed + header + b"Packed file is corrupt" + relocs
    size = 32 + len(module)
    mz = struct.pack("<2s13H", b"MZ", size % 512, (size + 511) // 512, 0, 2, 0, 0xFFFF,
                     0, 0, 0, 0, len(packed) // 16, 0x1C, 0)
    return mz.ljust(32, b"\0") + module


def build_ovr():
    body = CODE + FIXUPS
    return b"FBOV" + struct.pack("<I", len(body)) + body


def parse(merged):
    last, pages, count, header_paras = struct.unpack_from("<4H", merged, 2)
    cs, = struct.unpack_from("<H", merged, 22)
    relocs = [struct.unpack_from("<HH", merged, 0x1C + 4 * i)[::-1] for i in range(count)]
    return (pages - 1) * 512 + (last or 512), merged[header_paras * 16:], relocs, cs


class MergeTests(unittest.TestCase):
    def test_unpacks_root(self):
        image, (cs, ip, ss, sp), relocs, memory = ovrmerge.unexepack(build_exe())
        self.assertEqual(image[:32], bytes(32))
        self.assertEqual(image[32:34], b"\xcd\x3f")
        self.assertEqual((cs, ip, ss, sp), (0, 0x10, 0, 0x100))
        self.assertEqual(relocs, [(0, 0x10)])
        self.assertGreaterEqual(memory * 16, 0x100)

    def test_merges_overlay(self):
        merged, overlay_map = ovrmerge.merge(build_exe(), build_ovr())
        size, image, relocs, cs = parse(merged)
        self.assertEqual(size, len(merged))
        self.assertEqual(cs, 0)
        seg = len(image) // 16 - 1
        self.assertEqual(image[seg * 16:seg * 16 + len(CODE)], CODE)
        # The stub entry becomes JMP FAR to the routine, relocated like the call's segment.
        self.assertEqual(image[0x40:0x45], struct.pack("<BHH", 0xEA, 0, seg))
        self.assertIn((2, 0x23), relocs)
        self.assertIn((seg, 6), relocs)
        self.assertIn((0, 0x10), relocs)
        self.assertIn(f"0002:0020 -> {seg:04x}:0000", overlay_map)

    def test_rejects_uncovered_overlay_bytes(self):
        ovr = build_ovr() + b"\0\0"
        ovr = ovr[:4] + struct.pack("<I", len(ovr) - 8) + ovr[8:]
        with self.assertRaisesRegex(ovrmerge.FormatError, "cover"):
            ovrmerge.merge(build_exe(), ovr)

    def test_rejects_missing_signature(self):
        with self.assertRaisesRegex(ovrmerge.FormatError, "FBOV"):
            ovrmerge.merge(build_exe(), b"XXXX" + build_ovr()[4:])


if __name__ == "__main__":
    unittest.main()

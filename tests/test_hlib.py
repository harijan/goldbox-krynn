"""Exercise tools/hlib.py on hand-built HLIB libraries, and on Dark Queen's
own when DQKAssets/ holds them (stdlib only)."""
import contextlib
import io
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import hlib  # noqa: E402

ASSETS = ROOT / "DQKAssets"


def library(records, kind=b"TILE", ids=None):
    """An HLIB library of RECORDS; IDS [(id, index)] adds the id map as
    record 0 (the indexes counting it)."""
    flags = 0
    if ids is not None:
        records = [struct.pack("<H", len(ids)) + b"".join(struct.pack("<HH", *p) for p in ids)] + \
            list(records)
        flags = 0x100
    offsets, at = [], 16 + 4 * (len(records) + 1)
    for r in records:
        offsets.append(at)
        at += len(r)
    offsets.append(at)
    return (b"HLIB" + struct.pack("<IHH4s", at, len(records), flags, kind) +
            struct.pack(f"<{len(offsets)}I", *offsets) + b"".join(records))


def tile(width, height, fmt, body, x=0, y=0):
    return struct.pack("<HhhBB", height, x, y, width // 4, fmt) + body


def planar(rows):
    """Rows of pixels stored plane by plane (pixel x in plane x % 4)."""
    width = len(rows[0])
    return bytes(row[c] for p in range(4) for row in rows for c in range(p, width, 4))


ROWS = [[1, 2, 3, 4, 5, 6, 7, 8], [9, 10, 11, 12, 13, 14, 15, 16]]
PALETTE = struct.pack("<HHHBB", 5, 1, 2, 0, 24) + bytes([10, 20, 30, 40, 50, 60]) + b"\x12"


class FormatTests(unittest.TestCase):
    def decode(self, record, lib=None):
        return hlib.picture(record, lib)[5]

    def test_raw_is_plane_by_plane(self):
        self.assertEqual(self.decode(tile(8, 2, 0x10, planar(ROWS))), ROWS)

    def test_transparent_raw(self):
        rows = [[0xFF if v == 3 else v for v in row] for row in ROWS]
        got = self.decode(tile(8, 2, 0x15, planar(rows)))
        self.assertEqual(got[0][2], None)
        self.assertEqual(got[1], ROWS[1])

    def test_masked(self):
        mask = [[0xFF if c < 2 else 0 for c in range(8)] for _ in range(2)]
        got = self.decode(tile(8, 2, 0x11, planar(mask) + planar(ROWS)))
        self.assertEqual(got[0][:3], [None, None, 3])
        self.assertEqual(got[1][2:], ROWS[1][2:])

    def test_packbits_rows_hold_their_plane_rows(self):
        # Each row: plane 0's two pixels, then plane 1's, 2's, 3's.
        flat = bytes(v for row in ROWS for p in range(4) for v in row[p::4])
        packed = bytes([len(flat) - 1]) + flat
        self.assertEqual(self.decode(tile(8, 2, 0x12, packed)), ROWS)
        run = bytes([257 - 16, 7])  # 16 times 7
        self.assertEqual(self.decode(tile(8, 2, 0x12, run)), [[7] * 8] * 2)

    def test_transparent_runs(self):
        # 4 planes x 2 rows of 2 pixels: plane 0 row 0 skips one, then copies one.
        planes = [b"\xff\x01\x05\x00"] + [b"\x02\x01\x01\x00"] * 7
        got = self.decode(tile(8, 2, 0x17, b"".join(planes)))
        self.assertEqual(got[0][0], None)
        self.assertEqual(got[0][4], 5)
        self.assertEqual(got[0][1], 1)

    def test_composite_draws_records(self):
        part = tile(4, 1, 0x10, bytes([9, 8, 7, 6]))
        # Record 0 at y 0, x 4, then at y 1, x 0.
        entries = struct.pack("<BBHH", 0, 2, 0, 4) + struct.pack("<BBHH", 0, 2, 1, 0)
        whole = tile(8, 2, 0x19, entries)
        lib = hlib.Library(library([part, whole]))
        got = self.decode(lib.records[1], lib)
        self.assertEqual(got[0], [None] * 4 + [9, 8, 7, 6])
        self.assertEqual(got[1], [9, 8, 7, 6] + [None] * 4)

    def test_unknown_format(self):
        with self.assertRaises(hlib.FormatError):
            hlib.picture(tile(4, 1, 0x13, bytes(4)))


class LibraryTests(unittest.TestCase):
    def test_ids_nesting_and_palettes(self):
        inner = library([PALETTE, tile(8, 2, 0x10, planar(ROWS))])
        data = library([b"data", inner], ids=[(7, 1), (9, 2)])
        lib = hlib.Library(data)
        self.assertEqual(lib.ids, {7: 1, 9: 2})
        self.assertEqual(lib.record(7), b"data")
        walked = list(hlib.walk(lib))
        self.assertEqual([w[0] for w in walked], [(7,), (9, 0), (9, 1)])
        record, colours, holder = hlib.find(lib, (9, 1))
        self.assertEqual(hlib.kind(record, holder), "picture")
        self.assertEqual(colours, {1: (10, 20, 30), 2: (40, 50, 60)})
        self.assertEqual(hlib.kind(b"HLIB" + bytes(12)), "library")

    def test_data_library_records_are_data(self):
        lib = hlib.Library(library([tile(4, 1, 0x10, bytes(4))], kind=b"DATA"))
        self.assertEqual(hlib.kind(lib.records[0], lib), "data")

    def test_bad_libraries(self):
        with self.assertRaises(hlib.FormatError):
            hlib.Library(b"DAX!" + bytes(20))
        bad = bytearray(library([b"abcd"]))
        struct.pack_into("<I", bad, 20, 999)  # the end past the data
        with self.assertRaises(hlib.FormatError):
            hlib.Library(bytes(bad))

    def test_command_line(self):
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "T.TLB"
            path.write_bytes(library([PALETTE, tile(8, 2, 0x10, planar(ROWS))]))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(hlib.main(["list", str(path)]), 0)
            self.assertIn("1          picture 8x2 at (0, 0) format 0x10", out.getvalue())
            png = pathlib.Path(d) / "p.png"
            self.assertEqual(hlib.main(["png", str(path), "1", str(png), "--scale", "2"]), 0)
            self.assertEqual(struct.unpack(">II", png.read_bytes()[16:24]), (16, 4))
            self.assertEqual(hlib.main(["extract", str(path), "0", str(png)]), 0)
            self.assertEqual(png.read_bytes(), PALETTE)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(hlib.main(["png", str(path), "0", str(png)]), 1)


@unittest.skipUnless((ASSETS / "DISK1/TITLE.TLB").exists(),
                     "DQKAssets/ does not hold The Dark Queen of Krynn's files")
class DarkQueenTests(unittest.TestCase):
    def test_every_picture_decodes(self):
        formats = {}
        for f in sorted(ASSETS.glob("DISK*/*.TLB")):
            for path, record, _, holder in hlib.walk(hlib.load(f)):
                if hlib.kind(record, holder) == "picture":
                    width, height, _, _, fmt, rows = hlib.picture(record, holder)
                    self.assertEqual((len(rows), len(rows[0])), (height, width), (f.name, path))
                    formats[fmt] = formats.get(fmt, 0) + 1
        self.assertEqual(formats, {0x10: 392, 0x11: 21, 0x12: 78, 0x15: 1162, 0x17: 197,
                                   0x19: 7})

    def test_title_logo_colours(self):
        # TITLE.TLB's second scene: the MicroMagic logo, 272x85, in
        # transparent runs, on its own palette.
        lib = hlib.load(ASSETS / "DISK1/TITLE.TLB")
        record, colours, holder = hlib.find(lib, (2, 1))
        width, height, _, _, fmt, rows = hlib.picture(record, holder)
        self.assertEqual((fmt, width, height), (0x17, 272, 85))
        used = {v for row in rows for v in row if v is not None}
        self.assertTrue(used <= set(colours), sorted(used - set(colours))[:8])


if __name__ == "__main__":
    unittest.main()

"""Exercise the native exporter with hand-built DAX inputs (stdlib only)."""
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "build/daximages"


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = pathlib.Path(self.temp.name)
        self.archive = self.folder / "TITLE.DAX"
        # Two 8x2 images, same ID; top row black, bottom row purple.
        image = struct.pack("<HHHHB", 2, 1, 0, 0, 1) + bytes(8)
        image += bytes(4) + bytes([0x55] * 4)
        packed = bytes([len(image) - 1]) + image
        directory = b"".join(
            struct.pack("<BIHH", 9, offset, len(image), len(packed))
            for offset in (0, len(packed))
        )
        self.archive.write_bytes(struct.pack("<H", len(directory)) + directory + packed * 2)

    def run_tool(self, *args):
        return subprocess.run([str(TOOL), *map(str, args)], capture_output=True, text=True)

    def test_duplicate_ids_export_separately_and_bmp_rows_are_correct(self):
        output = self.folder / "images"
        result = self.run_tool(self.archive, output)
        self.assertEqual(result.returncode, 0, result.stderr)
        images = sorted(output.glob("*.bmp"))
        self.assertEqual(len(images), 2)
        self.assertIn("entry-000-id-009", images[0].name)
        self.assertIn("entry-001-id-009", images[1].name)
        bmp = images[0].read_bytes()
        self.assertEqual(bmp[:2], b"BM")
        self.assertEqual(struct.unpack_from("<I", bmp, 2)[0], len(bmp))
        self.assertEqual(struct.unpack_from("<ii", bmp, 18), (8, 2))
        # Positive-height BMP stores the bottom scanline first, in BGR order.
        self.assertEqual(bmp[54:78], bytes([170, 0, 170]) * 8)
        self.assertEqual(bmp[78:102], bytes(24))
        html = (output / "index.html").read_text()
        self.assertIn("Entry 0 · ID 9", html)
        self.assertIn("Entry 1 · ID 9", html)
        for path in images:
            self.assertIn(path.name, html)

    def test_entry_and_frame_selection(self):
        output = self.folder / "selected"
        result = self.run_tool("--entry", 1, "--frame", 0, self.archive, output)
        self.assertEqual(result.returncode, 0, result.stderr)
        images = list(output.glob("*.bmp"))
        self.assertEqual(len(images), 1)
        self.assertIn("entry-001", images[0].name)

    def test_invalid_selection_and_truncated_image_fail(self):
        for flags in (("--entry", "2"), ("--entry", "-1"),
                      ("--frame", "0"), ("--entry", "0", "--frame", "1")):
            with self.subTest(flags=flags):
                result = self.run_tool(*flags, self.archive, self.folder / "bad")
                self.assertNotEqual(result.returncode, 0)
        # Archive and compression are valid; the image header claims too many rows.
        damaged = bytearray(self.archive.read_bytes())
        damaged[21] = 3
        self.archive.write_bytes(damaged)
        output = self.folder / "truncated"
        result = self.run_tool("--entry", 0, self.archive, output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("malformed image", result.stderr)
        self.assertEqual(list(output.glob("*.bmp")), [])

    def test_contact_sheet_escapes_input_filename(self):
        directory = self.folder / "<pictures>&"
        directory.mkdir()
        path = directory / "TITLE.DAX"
        path.write_bytes(self.archive.read_bytes())
        output = self.folder / "escaped"
        result = self.run_tool(path, output)
        self.assertEqual(result.returncode, 0, result.stderr)
        html = (output / "index.html").read_text()
        self.assertIn("&lt;pictures&gt;&amp;", html)
        self.assertNotIn("<pictures>", html)

    def test_supplied_graphics_archives_export_every_frame(self):
        # Counts established independently from the decoded archive layouts.
        expected = {
            "TITLE": 4, "BIGPIC1": 4, "BIGPIC2": 2, "BIGPIC3": 3,
            "PIC1": 31, "PIC2": 50, "PIC3": 44,
            "SPRIT1": 15, "SPRIT2": 54, "SPRIT3": 39,
            "CPIC1": 52, "CPIC2": 60, "CPIC3": 44,
            "CBODY": 128, "CHEAD": 56, "BODY2": 1, "BODY3": 1,
            "HEAD2": 2, "HEAD3": 2, "COMSPR": 26, "CURSOR": 1,
            "SKY": 3, "TILES": 48, "8X8D1": 433, "8X8D2": 630, "8X8D3": 630,
        }
        for name, count in expected.items():
            with self.subTest(archive=name):
                output = self.folder / name
                result = self.run_tool(ROOT / "Assets" / f"{name}.DAX", output)
                self.assertEqual(result.returncode, 1 if name == "8X8D1" else 0, result.stderr)
                if name == "8X8D1":
                    self.assertIn("entry 0 ID 201", result.stderr)
                paths = list(output.glob("*.bmp"))
                self.assertEqual(len(paths), count)
                sheet = (output / "index.html").read_text()
                self.assertEqual(sheet.count("<figure>"), count)
                for path in paths:
                    self.assertIn(path.name, sheet)
                    bmp = path.read_bytes()
                    width, height = struct.unpack_from("<ii", bmp, 18)
                    stride = (width * 3 + 3) // 4 * 4
                    self.assertEqual(len(bmp), 54 + stride * height)


class ComposeTextTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = pathlib.Path(self.temp.name) / "text.bmp"

    def compose(self, *commands):
        tool = ROOT / "build/daxcompose"
        return subprocess.run([str(tool), str(self.output), *map(str, commands)],
                              capture_output=True, text=True)

    def pixel(self, x, y):
        bmp = self.output.read_bytes()
        offset = 54 + (199 - y) * 320 * 3 + x * 3
        blue, green, red = bmp[offset:offset + 3]
        return red, green, blue

    def test_text_draws_game_font_glyphs(self):
        font = ROOT / "Assets/8X8D1.DAX"
        # "A" (glyph 1) has row 0 = 0x0f: four background, then four foreground pixels.
        result = self.compose("text", font, 0, 1, 2, 14, 1, "a",
                              "glyph", font, 0, 32, 2, 38, 24, 15, 4)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.pixel(8 + 3, 16), (0, 0, 170))
        self.assertEqual(self.pixel(8 + 4, 16), (255, 255, 85))
        self.assertEqual(self.pixel(319, 199), (170, 0, 0))
        self.assertEqual(self.pixel(303, 199), (0, 0, 0))

    def test_text_rejects_bad_arguments(self):
        font = ROOT / "Assets/8X8D1.DAX"
        for command in (("text", font, 0, 0, 0, 16, 0, "A"),
                        ("glyph", font, 0, 256, 1, 0, 0, 1, 0),
                        ("text", font, 99, 0, 0, 1, 0, "A"),
                        ("text", ROOT / "Assets/TITLE.DAX", 0, 0, 0, 1, 0, "A")):
            with self.subTest(command=command[:3]):
                self.assertNotEqual(self.compose(*command).returncode, 0)


if __name__ == "__main__":
    unittest.main()

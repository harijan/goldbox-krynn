"""Exercise the DOSBox-X harness's RGB screens (cokbox.RGBScreen, Dark Queen's
256-colour screenshots) and checklib.masked_diff on them, and the Dark Queen
fight checks' page flip at a trap (dqk_fight_lib.call_then_spin), without
DOSBox-X (stdlib only)."""
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/dosbox"))
sys.path.insert(0, str(ROOT / "tools/dosbox/checks"))
import checklib  # noqa: E402
import cokbox  # noqa: E402
import dqk_fight_lib  # noqa: E402


def screen(width, height, colour=(10, 20, 30), changes=()):
    """An RGBScreen of COLOUR with CHANGES [(x, y, (r, g, b))]."""
    pixels = bytearray(bytes(colour) * (width * height))
    for x, y, rgb in changes:
        i = 3 * (y * width + x)
        pixels[i:i + 3] = bytes(rgb)
    return cokbox.RGBScreen(width, height, pixels)


class RGBScreenTests(unittest.TestCase):
    def test_diff_counts_pixels_not_bytes(self):
        a = screen(8, 4)
        b = screen(8, 4, changes=[(1, 1, (10, 20, 31)), (6, 3, (0, 0, 0))])
        self.assertEqual(a.diff(a), (0, None))
        self.assertEqual(a.diff(b), (2, (1, 1, 6, 3)))
        self.assertEqual(b.at(6, 3), (0, 0, 0))
        self.assertEqual(len(b.region(1, 1, 2, 1)), 6)

    def test_masked_diff(self):
        a = screen(8, 4)
        # (1, 1) differs in its blue only.
        b = screen(8, 4, changes=[(1, 1, (10, 20, 31)), (6, 3, (2, 2, 2))])
        self.assertEqual(checklib.masked_diff(a, b), (2, (1, 1, 6, 3)))
        self.assertEqual(checklib.masked_diff(a, b, [(5, 2, 3, 2, "cursor")]), (1, (1, 1, 1, 1)))
        self.assertEqual(checklib.masked_diff(a, b, [(0, 0, 8, 4)]), (0, None))

    def test_mixed_kinds_refused(self):
        rgb = screen(4, 2)
        indexed = cokbox.Screen(4, 2, bytes(8))
        for x, y in ((rgb, indexed), (indexed, rgb)):
            with self.assertRaises(cokbox.BoxError):
                checklib.masked_diff(x, y)
            with self.assertRaises(cokbox.BoxError):
                x.diff(y)

    def test_save_png_highlight_and_load(self):
        a = screen(8, 4, colour=(90, 60, 30))
        b = screen(8, 4, colour=(90, 60, 30), changes=[(2, 1, (255, 0, 0))])
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "diff.png"
            a.save_png(path, highlight=b)
            back = cokbox.RGBScreen.load(path, size=(8, 4))
        self.assertEqual(back.at(2, 1), (255, 0, 0))     # the other's colour where they differ
        self.assertEqual(back.at(0, 0), (30, 20, 10))    # darkened where they agree
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "plain.png"
            b.save_png(path)
            self.assertEqual(cokbox.RGBScreen.load(path, size=(8, 4)).diff(b), (0, None))

    def test_scaled_down_from_a_doubled_screenshot(self):
        rows = [[(y, x, 7) for x in range(16) for _ in range(2)] for y in range(4) for _ in range(2)]
        s = cokbox.RGBScreen.from_rows(32, 8, rows, size=(16, 4))
        self.assertEqual((s.width, s.height), (16, 4))
        self.assertEqual(s.at(5, 3), (3, 5, 7))


class FlipTests(unittest.TestCase):
    def test_call_then_spin(self):
        # The keys flow's tail spins at 5045:066a's lcall 148f:2ab8 (5 bytes): each copy
        # gets lcall 19a4:06d3 (the real segment, little-endian) and the spin after it.
        writes = dqk_fight_lib.call_then_spin([0x51000, 0x62010], 0x09F4, 0x06D3)
        call = bytes([0x9A, 0xD3, 0x06, 0xF4, 0x09, 0xEB, 0xFE])
        self.assertEqual(writes, [(0x51000, call), (0x62010, call)])
        self.assertEqual(dqk_fight_lib.FLIP, (0x19A4, 0x06D3))
        self.assertEqual(dqk_fight_lib.DS_BEGUN, 0x1CB5)


if __name__ == "__main__":
    unittest.main()

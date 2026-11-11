import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

try:
    import capstone  # noqa: F401
    import match_functions as mf
except ImportError:  # capstone is only needed for this offline tool
    mf = None


@unittest.skipIf(mf is None, "capstone is not installed")
class WindowTest(unittest.TestCase):
    def test_follows_the_block_the_function_moved_with(self):
        # two source files: one moved by +0x1000, the other by +0x900000
        anchors = [(0x1000, 0x2000), (0x1400, 0x2400), (0x1800, 0x2800),
                   (0x900000, 0x1200000), (0x900400, 0x1200400), (0x900800, 0x1200800)]
        lo, hi = mf.window(anchors, 0x1600)
        self.assertTrue(lo <= 0x2600 <= hi)
        self.assertLess(hi - lo, 0x100000)
        lo, hi = mf.window(anchors, 0x900600)
        self.assertTrue(lo <= 0x1200600 <= hi)

    def test_no_window_without_two_agreeing_anchors(self):
        self.assertIsNone(mf.window([(0x1000, 0x5000)], 0x1200))
        self.assertIsNone(mf.window([], 0x1200))

    def test_similarity(self):
        a = ["push rbp", "mov rbp,rsp", "call i", "ret "]
        self.assertEqual(mf.similarity(a, a), 1.0)
        self.assertLess(mf.similarity(a, ["xor eax,eax", "ret "]), 0.5)


if __name__ == "__main__":
    unittest.main()

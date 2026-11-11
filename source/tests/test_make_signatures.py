import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

try:
    import capstone  # noqa: F401
    import make_signatures as ms
except ImportError:  # capstone is only needed for this offline tool
    ms = None


@unittest.skipIf(ms is None, "capstone is not installed")
class MakeSignaturesTest(unittest.TestCase):
    def pattern(self, code: bytes):
        return list(ms.function_pattern(code, 0x1000))[-1]

    def test_relative_call_and_rip_displacement_are_wildcards(self):
        # mov rax, [rip+0x11223344]; call rel32; ret
        code = bytes.fromhex("488b0544332211" "e8aabbccdd" "c3")
        self.assertEqual(ms.to_text(self.pattern(code)), "48 8B 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? C3")

    def test_struct_offsets_and_short_branches(self):
        # mov rcx, [rcx+0x778]; test rcx, rcx; je +5; ret
        code = bytes.fromhex("488b8978070000" "4885c9" "7405" "c3")
        self.assertEqual(ms.to_text(self.pattern(code)), "48 8B 89 78 07 00 00 48 85 C9 74 ?? C3")

    def test_unique_match_in_text(self):
        text = b"\xcc" * 32 + bytes.fromhex("4157415648") + b"\xcc" * 8 + bytes.fromhex("4157415649")
        self.assertEqual(ms.find_all(text, list(bytes.fromhex("4157415648"))), [32])
        self.assertEqual(ms.find_all(text, [0x41, 0x57, 0x41, 0x56, None]), [32, 45])

    def test_short_name(self):
        self.assertEqual(ms.short_name("?getFov@LevelRendererPlayer@@QEBAMM_N@Z"), "LevelRendererPlayer::getFov")
        self.assertEqual(ms.short_name("UIScene::render"), "UIScene::render")


if __name__ == "__main__":
    unittest.main()

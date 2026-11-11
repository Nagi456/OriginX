import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import make_rva_table as mrt  # noqa: E402


def fake_image(functions, relocs=()):
    """A PE32+ image as laid out in memory: headers, .pdata at 0x3000, .reloc at 0x4000, code at 0x1000."""
    data = bytearray(0x5000)
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<I", data, 0x80 + 8, 0x6AD79EE4)
    opt = 0x80 + 24
    struct.pack_into("<H", data, opt, 0x20B)
    struct.pack_into("<I", data, opt + 56, len(data))
    dirs = opt + 112
    struct.pack_into("<II", data, dirs + 3 * 8, 0x3000, 12 * len(functions))
    for i, (rva, code) in enumerate(functions):
        struct.pack_into("<III", data, 0x3000 + 12 * i, rva, rva + len(code), 0)
        data[rva:rva + len(code)] = code
    entries = [(10 << 12) | (r & 0xFFF) for r in relocs]
    if len(entries) % 2:
        entries.append(0)
    block = 8 + 2 * len(entries)
    struct.pack_into("<II", data, 0x4000, 0x1000, block)
    for i, e in enumerate(entries):
        struct.pack_into("<H", data, 0x4008 + 2 * i, e)
    struct.pack_into("<II", data, dirs + 5 * 8, 0x4000, block if relocs else 0)
    return bytes(data)


class MakeRvaTableTest(unittest.TestCase):
    def run_make(self, image, rows):
        with tempfile.TemporaryDirectory() as tmp:
            matches = Path(tmp) / "matched.txt"
            matches.write_text("# header\n" + "".join("\t".join(r) + "\n" for r in rows), encoding="utf-8")
            path = Path(tmp) / "game.image"
            path.write_bytes(image)
            return mrt.make([(path, mrt.read_matches(matches))])

    def setUp(self):
        self.saved = mrt.NAMES
        mrt.NAMES = {"A": ["?a@@YAXXZ"], "B": ["?b@@YAXXZ", "?b2@@YAXXZ"]}

    def tearDown(self):
        mrt.NAMES = self.saved

    def test_rows_bytes_and_masks(self):
        code_a = bytes(range(0x40, 0x60))
        code_b = b"\xe9\x11\x22\x33\x44" + bytes(range(0x70, 0x90))
        image = fake_image([(0x1000, code_a), (0x1100, code_b)], relocs=[0x1004])
        text = self.run_make(image, [
            ["A", "?a", "0x1000", "sure", "1", "0", "how", ""],
            ["B", "?b", "0x1100", "sure", "1", "0", "how", ""],
            ["C", "?c", "0x1100", "sure", "1", "0", "how", ""],            # not a known label
            ["A", "?a", "0x1100", "unsure", "1", "0", "how", ""],          # unsure
        ])
        self.assertIn("MYCLIENT_RVA_EXE(0, 0x6AD79EE4u, 0x5000u)", text)
        self.assertEqual(sum(l.startswith("MYCLIENT_RVA_ROW(") for l in text.splitlines()), 2)
        row_a = next(l for l in text.splitlines() if l.startswith('MYCLIENT_RVA_ROW(0, "A"'))
        self.assertIn('0x1000u, "\\x40\\x41', row_a)
        # bytes 4..11 are relocated (DIR64 at 0x1004): not compared
        self.assertIn('"\\x01\\x01\\x01\\x01\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x01', row_a)
        row_b = next(l for l in text.splitlines() if l.startswith('MYCLIENT_RVA_ROW(0, "B"'))
        self.assertIn('("?b@@YAXXZ", "?b2@@YAXXZ")', row_b)
        # hooked when dumped: the jmp's 5 bytes are not compared
        self.assertIn('"\\x00\\x00\\x00\\x00\\x00\\x01', row_b)

    def test_rows_with_a_levilamina_answer_are_left_out(self):
        image = fake_image([(0x1000, bytes(range(32)))])
        text = self.run_make(image, [["A", "?a", "0x1000", "sure", "1", "0", "how", "right"],
                                     ["B", "?b", "0x1000", "sure", "1", "0", "how", ""]])
        self.assertNotIn('"A"', text)
        self.assertIn('"B"', text)

    def test_hooked_functions_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            table = Path(tmp) / "hooked_functions.txt"
            table.write_text("# header\n0x1000\tA\t?a\n0x1100\t(LeviLamina)\tLeviLamina.dll+0x10\n", encoding="utf-8")
            self.assertEqual(mrt.read_hooked(table), [("A", 0x1000)])

if __name__ == "__main__":
    unittest.main()

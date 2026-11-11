import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from list_game_symbols import game_symbols  # noqa: E402


class ListGameSymbolsTest(unittest.TestCase):
    def test_joins_adjacent_literals_and_skips_other_strings(self):
        source = '''
inline constexpr const char* A = "?getEyePos@Actor@@QEBA?AVVec3@@XZ";
inline constexpr const char* B =
    "?_receivePacket@BatchedNetworkPeer@@UEAA"
    "XZ";
log.info("? not a symbol {}", x);
// "?commented@Out@@QEAAXXZ"
const char* plain = "hello";
'''
        self.assertEqual(game_symbols([source]),
                         ["?_receivePacket@BatchedNetworkPeer@@UEAAXZ", "?getEyePos@Actor@@QEBA?AVVec3@@XZ"])

    def test_the_real_sources_give_a_list(self):
        client = Path(__file__).resolve().parents[1] / "client"
        names = game_symbols(p.read_text(encoding="utf-8", errors="replace")
                             for p in client.iterdir() if p.suffix in (".cpp", ".h"))
        self.assertGreater(len(names), 50)
        self.assertIn("?getHealth@ActorAttribute@@YAHAEBVEntityContext@@@Z", names)


if __name__ == "__main__":
    unittest.main()

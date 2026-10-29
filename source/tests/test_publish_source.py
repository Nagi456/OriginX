import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from publish_source import excluded, publish  # noqa: E402


def git(cwd, *args):
    subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True)


class PublishSourceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.root, self.target = base / "root", base / "target"
        self.root.mkdir()
        self.target.mkdir()
        git(self.root, "init", "-q")
        git(self.target, "init", "-q")
        for rel in ["source/MyClient.cpp", "source/docs/plan.md", "CLAUDE.md",
                    "mods/MyClientData/a.cpp", "source/site/index.html",
                    "source/launcher/vendor/gamelaunchhelper/LICENSE-GPL.md"]:
            (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.root / rel).write_text(rel)
        (self.root / "untracked.txt").write_text("x")
        git(self.root, "add", "source", "CLAUDE.md", "mods")

    def tearDown(self):
        self.tmp.cleanup()

    def test_excluded(self):
        self.assertTrue(excluded("CLAUDE.md"))
        self.assertTrue(excluded("source/docs/x.md"))
        self.assertFalse(excluded("source/MyClient.cpp"))
        self.assertFalse(excluded("source/CLAUDE.md.txt"))

    def test_copies_only_published_tracked_files(self):
        (self.target / "old.txt").write_text("old")
        files = publish(self.root, self.target)
        self.assertEqual(files, ["mods/MyClientData/a.cpp", "source/MyClient.cpp",
                                 "source/launcher/vendor/gamelaunchhelper/LICENSE-GPL.md"])
        self.assertTrue((self.target / "LICENSE").exists())
        self.assertTrue((self.target / "source/MyClient.cpp").exists())
        self.assertTrue((self.target / "README.md").exists())
        self.assertTrue((self.target / ".git").exists())
        for gone in ["CLAUDE.md", "source/docs", "source/site", "untracked.txt", "old.txt"]:
            self.assertFalse((self.target / gone).exists(), gone)

    def test_refuses_a_folder_without_git(self):
        plain = Path(self.tmp.name) / "plain"
        plain.mkdir()
        with self.assertRaises(SystemExit):
            publish(self.root, plain)


if __name__ == "__main__":
    unittest.main()

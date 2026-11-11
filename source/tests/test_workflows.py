"""Guards for the release-channel workflows (docs/superpowers/specs/2026-11-09-release-channels-design.md)."""
import unittest
from pathlib import Path

WORKFLOWS = Path(__file__).resolve().parents[2] / ".github/workflows"


class BuildDllChannelTest(unittest.TestCase):
    def setUp(self):
        self.text = (WORKFLOWS / "build-dll.yml").read_text(encoding="utf-8")

    def test_the_test_channel_list_is_merged_by_file_not_by_what_the_tool_prints(self):
        # build-dll runs in Git Bash on windows-latest, where Python prints "ok\r\n": $(...) keeps the \r, so a
        # string comparison with "ok" would silently skip the merge and drop the other game versions.
        self.assertNotIn('"$fetched" = "ok"', self.text)
        self.assertIn('[ -s "$RUNNER_TEMP/previous-test.json" ]', self.text)

    def test_a_test_build_never_commits_back_to_the_branch(self):
        self.assertIn("inputs.publish && inputs.channel != 'test'", self.text)


if __name__ == "__main__":
    unittest.main()

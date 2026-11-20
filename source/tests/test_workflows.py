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



class AgentWorkflowTest(unittest.TestCase):
    """ci.yml / build-release.yml (docs/release.md, docs/branch-and-deploy.md)."""

    def read(self, name):
        return (WORKFLOWS / name).read_text(encoding="utf-8")

    def test_ci_never_publishes(self):
        ci = self.read("ci.yml")
        self.assertIn("publish: false", ci)
        self.assertNotIn("publish: true", ci)
        self.assertNotIn("publish_r2.py", ci)

    def test_the_release_workflow_publishes_one_game_after_the_other_from_the_plan(self):
        release = self.read("build-release.yml")
        self.assertIn("max-parallel: 1", release)
        self.assertIn("needs.plan.outputs.publish == 'true'", release)
        self.assertTrue(release.startswith("#") and "name: build-release" in release)   # build-* : the Worker's OIDC rule

    def test_publishers_share_one_concurrency_group(self):
        for name in ("build-dll.yml", "build-launcher.yml", "promote-build.yml"):
            self.assertIn("originx-publish", self.read(name), name)

    def test_a_stable_publish_starts_from_the_branch_head(self):
        dll = self.read("build-dll.yml")
        self.assertIn('git merge -q --ff-only FETCH_HEAD', dll)
        # the new stable list is built on the live one, never on a branch's possibly old copy
        self.assertIn('--catalog "$RUNNER_TEMP/live-stable.json"', dll)
        self.assertNotIn("--catalog launcher/catalog.json", dll)

    def test_the_worker_lists_the_release_branches(self):
        wrangler = (WORKFLOWS.parents[1] / "wrangler.jsonc").read_text(encoding="utf-8")
        self.assertIn('"RELEASE_REFS": "refs/heads/main"', wrangler)   # main only (docs/branch-and-deploy.md)


if __name__ == "__main__":
    unittest.main()

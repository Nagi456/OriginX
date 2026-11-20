import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_client
import release_request


class StandaloneBuilds(unittest.TestCase):
    def test_borrowed_layouts_require_standalone(self):
        borrowed = {"gameRelease": "26.45.1", "layoutsFrom": {"game": "26.51.1"}}
        with self.assertRaisesRegex(ValueError, "standalone"):
            build_client.validate_build_mode(borrowed, False)
        build_client.validate_build_mode(borrowed, True)

    def test_existing_sdk_builds_keep_both_modes(self):
        for mode in (False, True):
            build_client.validate_build_mode({"gameRelease": "26.51.1"}, mode)

    def test_build_rejects_wrong_mode_before_preparing_sdk(self):
        args = SimpleNamespace(game="26.45.1", standalone=False)
        with patch.object(build_client, "prepare", side_effect=AssertionError("must not prepare/download SDK")):
            with self.assertRaisesRegex(SystemExit, "standalone"):
                build_client.build(args)

    def test_ci_does_not_request_a_nonexistent_2645_loader(self):
        variants = release_request.build_variants()
        actual = {(v["game"], v["standalone"]) for v in variants}
        self.assertEqual(actual, {("26.45.1", True), ("26.51.1", False), ("26.51.1", True),
                                  ("26.60.5", False), ("26.60.5", True)})
        self.assertEqual(len(variants), len(actual))


if __name__ == "__main__":
    unittest.main()

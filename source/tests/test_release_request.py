import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import release_request as rr  # noqa: E402

CATALOG = {"sequence": 91, "versions": [
    {"id": "26.51.1", "available": True, "revision": 56},
    {"id": "26.60.5", "available": True, "revision": 56},
    {"id": "26.62.1", "available": False, "revision": 0},
]}
GAMES = ["26.51.1", "26.60.5"]


def request(**over):
    base = {"channel": "test", "revision": 58, "testNumber": 1, "games": ["26.51.1", "26.60.5"]}
    base.update(over)
    return {k: v for k, v in base.items() if v is not None}


class ValidateTest(unittest.TestCase):
    def test_a_good_test_and_stable_request(self):
        self.assertEqual(rr.validate(request(), CATALOG, GAMES), [])
        self.assertEqual(rr.validate(request(channel="stable", testNumber=None), CATALOG, GAMES), [])

    def test_problems(self):
        cases = [
            (request(channel="beta"), "channel"),
            (request(revision="58"), "revision"),
            (request(revision=True), "revision"),
            (request(games=[]), "games"),
            (request(games=["26.51.1", "26.51.1"]), "twice"),
            (request(games=["26.62.1"]), "not supported"),
            (request(games=["1.26.51.1"]), "three parts"),
            (request(testNumber=None), "testNumber"),
            (request(testNumber=0), "testNumber"),
            (request(channel="stable"), "channel test only"),
            (request(standalone="yes"), "standalone"),
            (request(extra=1), "unknown keys"),
            (request(channel="stable", testNumber=None, revision=56), "larger than the published r56"),
        ]
        for req, word in cases:
            problems = rr.validate(req, CATALOG, GAMES)
            self.assertTrue(any(word in p for p in problems), (req, problems))

    def test_supported_games_come_from_sigs_and_locks(self):
        self.assertIn("26.51.1", rr.supported_games())
        self.assertIn("26.60.5", rr.supported_games())


class PlanTest(unittest.TestCase):
    def test_test_requests_publish_from_the_pull_request_only_when_the_file_changed(self):
        self.assertTrue(rr.plan(request(), "pull_request", True, CATALOG)["publish"])
        self.assertFalse(rr.plan(request(), "pull_request", False, CATALOG)["publish"])
        self.assertFalse(rr.plan(request(), "push", True, CATALOG)["publish"])

    def test_stable_requests_publish_only_when_merged_into_main(self):
        stable = request(channel="stable", testNumber=None)
        self.assertFalse(rr.plan(stable, "pull_request", True, CATALOG)["publish"])
        self.assertTrue(rr.plan(stable, "push", True, CATALOG)["publish"])
        self.assertFalse(rr.plan(stable, "push", False, CATALOG)["publish"])

    def test_the_matrix_and_sequences(self):
        stable = rr.plan(request(channel="stable", testNumber=None), "push", True, CATALOG)
        self.assertEqual([(m["game"], m["revision"], m["sequence"], m["test_number"]) for m in stable["matrix"]],
                         [("26.51.1", "58", "92", ""), ("26.60.5", "58", "93", "")])
        test = rr.plan(request(standalone=True), "pull_request", True, CATALOG)
        self.assertEqual([(m["sequence"], m["test_number"], m["standalone"]) for m in test["matrix"]],
                         [("0", "1", True), ("0", "1", True)])
        self.assertIn("myclient-26.60.5-r58-test1", test["summary"])

    def test_no_file_means_no_publish(self):
        import io
        import contextlib
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(rr.main(["plan", "--event", "push", "--file", "/nonexistent/request.json"]), 0)
        self.assertIn('"publish": false', out.getvalue())


if __name__ == "__main__":
    unittest.main()

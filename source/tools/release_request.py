#!/usr/bin/env python3
"""release_request: reads release/request.json (the publish request an agent puts in its pull request) and says
what .github/workflows/build-release.yml publishes (docs/release.md "公開の依頼ファイル").

  {
    "channel": "test",          # test = the moderators' channel (published from the pull request)
                                #  stable = everyone (published when the pull request is merged into main)
    "revision": 58,             # the tag: myclient-<game>-r<revision>[-test<testNumber>]
    "testNumber": 1,            # channel test only: N of -testN (a new number for every new test build)
    "games": ["26.51.1", "26.60.5"],
    "standalone": false,        # true = the DLL without LeviLamina (launcherApi 2)
    "note": "why (shown in the run)"
  }

The catalog sequence is not in the file: it is the live stable/catalog.json's + 1 for the first game, + 2 for the next.

Usage:
  python tools/release_request.py check [FILE]                     # validate (also against the live stable catalog)
  python tools/release_request.py plan --event EVENT [--changed]   # for the workflow: prints the plan as JSON
EVENT is pull_request, push (to main) or workflow_dispatch. --changed: the file changed in the pushed commits.
"""
import argparse
import json
import re
import sys
import urllib.request
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parents[1]
DEFAULT_FILE = ROOT / "release" / "request.json"
CATALOG_URL = "https://originx.originx-client.workers.dev/dl/stable/catalog.json"
KEYS = {"channel", "revision", "testNumber", "games", "standalone", "note"}


def supported_games():
    """Three-part game versions (26.51.1) of sigs.json "supported" that have a build lock."""
    sigs = json.loads((TOOLS / "sigs.json").read_text(encoding="utf-8"))
    out = []
    for build in sigs["supported"]:
        parts = build["version"].split(".")       # 1.26.51.1
        game = ".".join(parts[1:])
        lock = TOOLS / "client-sdk.lock.json" if game == "26.51.1" else TOOLS / "sdks" / (game + ".lock.json")
        if lock.exists():
            out.append(game)
    return out


def build_variants():
    """CI builds borrowed-layout SDKs only in standalone mode; existing SDKs retain both modes."""
    from prepare_client_sdk import load_lock
    out = []
    for game in supported_games():
        modes = [True] if load_lock(game).get("layoutsFrom") else [False, True]
        out.extend({"game": game, "standalone": mode} for mode in modes)
    return out


def live_catalog(fetch=None):
    if fetch:
        return fetch()
    # Python's default User-Agent is refused by Cloudflare (403, 2026-11-11); the workflow and a person get the same answer.
    request = urllib.request.Request(CATALOG_URL, headers={"User-Agent": "originx-release-request/1"})
    with urllib.request.urlopen(request, timeout=30) as res:
        return json.loads(res.read().decode("utf-8"))


def validate(request, catalog, games_ok):
    """A list of problems (empty = fine)."""
    problems = []
    if not isinstance(request, dict):
        return ["the file must be one JSON object"]
    extra = set(request) - KEYS
    if extra:
        problems.append("unknown keys: " + ", ".join(sorted(extra)))
    channel = request.get("channel")
    if channel not in ("test", "stable"):
        problems.append('channel must be "test" or "stable"')
    revision = request.get("revision")
    if not isinstance(revision, int) or isinstance(revision, bool) or revision <= 0:
        problems.append("revision must be a positive integer")
    games = request.get("games")
    if not isinstance(games, list) or not games or not all(isinstance(g, str) for g in games):
        problems.append('games must be a non-empty list like ["26.51.1"]')
        games = []
    if len(set(games)) != len(games):
        problems.append("a game is listed twice")
    for game in games:
        if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", game):
            problems.append(f"{game}: three parts, e.g. 26.51.1")
        elif game not in games_ok:
            problems.append(f"{game}: not supported (tools/sigs.json supported + a build lock): {', '.join(games_ok)}")
    if not isinstance(request.get("standalone", False), bool):
        problems.append("standalone must be true or false")
    test_number = request.get("testNumber")
    if channel == "test":
        if not isinstance(test_number, int) or isinstance(test_number, bool) or test_number <= 0:
            problems.append("channel test needs testNumber (a positive integer, new for every test build)")
    elif test_number is not None:
        problems.append("testNumber is for channel test only")
    if channel == "stable" and isinstance(revision, int) and catalog:
        for entry in catalog.get("versions", []):
            if entry.get("id") in games and entry.get("available") and revision <= int(entry.get("revision", 0)):
                problems.append(f"{entry['id']}: revision must be larger than the published r{entry['revision']}")
    return problems


def plan(request, event, changed, catalog):
    """What the workflow does: {"publish", "channel", "matrix", "summary"}."""
    channel = request["channel"]
    if event == "pull_request":
        publish = channel == "test" and changed
        why = ("test build from this pull request" if publish else
               "stable: published when this pull request is merged into main" if channel == "stable" else
               "release/request.json did not change in this push: nothing new to publish")
    elif event == "push":
        publish = channel == "stable" and changed
        why = "stable release (merged into main)" if publish else "a test request is published from its pull request"
    elif event == "workflow_dispatch":
        publish, why = True, "started by hand"
    else:
        publish, why = False, f"event {event}: nothing to do"
    sequence = int(catalog.get("sequence", 0)) if catalog else 0
    matrix = []
    for i, game in enumerate(request["games"]):
        matrix.append({
            "game": game,
            "standalone": bool(request.get("standalone", False)),
            "revision": str(request["revision"]),
            "sequence": str(sequence + 1 + i) if channel == "stable" else "0",
            "test_number": str(request.get("testNumber", "")) if channel == "test" else "",
        })
    tags = [f"myclient-{m['game']}-r{m['revision']}" + (f"-test{m['test_number']}" if channel == "test" else "") for m in matrix]
    summary = f"{'PUBLISH' if publish else 'no publish'} ({why}): channel {channel}, " + ", ".join(tags)
    if request.get("note"):
        summary += " — " + str(request["note"])
    return {"publish": publish, "channel": channel, "matrix": matrix, "summary": summary}


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("file", nargs="?", default=str(DEFAULT_FILE))
    p = sub.add_parser("plan")
    p.add_argument("--file", default=str(DEFAULT_FILE))
    p.add_argument("--event", required=True)
    p.add_argument("--changed", action="store_true")
    args = ap.parse_args(argv)
    path = Path(args.file)
    if not path.exists():
        if args.cmd == "plan":
            print(json.dumps({"publish": False, "channel": "", "matrix": [], "summary": f"no {path.name}: nothing to publish"}))
            return 0
        print(f"no {path}", file=sys.stderr)
        return 1
    try:
        request = load(path)
    except json.JSONDecodeError as e:
        print(f"{path}: not JSON ({e})", file=sys.stderr)
        return 1
    catalog = live_catalog()
    problems = validate(request, catalog, supported_games())
    if problems:
        for line in problems:
            print(f"{path.name}: {line}", file=sys.stderr)
        return 1
    if args.cmd == "check":
        print(plan(request, "workflow_dispatch", True, catalog)["summary"].replace("PUBLISH (started by hand)", "ok"))
        return 0
    print(json.dumps(plan(request, args.event, args.changed, catalog)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

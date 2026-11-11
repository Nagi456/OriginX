#!/usr/bin/env python3
"""new_version_lock: the build lock of a Minecraft version that has no LeviLamina release (docs/version-update.md).

MyClientData.dll is compiled against LeviLamina's headers (the game's class layouts) of one game version, pinned in
tools/sdks/<game>.lock.json (tools/client-sdk.lock.json for 26.51.1). For a new game version LeviLamina may never
publish headers (26.45 / 26.62 / 26.63 never got them). The standalone DLL (build_client.py --standalone) does not use
LeviLamina at run time, so it can be built with the nearest older version's headers: this writes a lock that is a copy
of FROM's with the game version changed and a note that the layouts were NOT made for this version.

What that means: every class member the DLL reads (SDK headers) is assumed to be where it was in FROM. That is the
part of a version update no tool checks; docs/version-update.md "構造ごと変わった場合" says what to look at.

Usage:
  python tools/new_version_lock.py 26.70.1 --from 26.60.5
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_client_sdk import lock_path  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game", help="the new game version, three parts as the launcher shows it (e.g. 26.70.1)")
    ap.add_argument("--from", dest="source", required=True, help="the supported version whose headers are reused")
    args = ap.parse_args(argv)
    if not re.fullmatch(r"[1-9][0-9]?\.[0-9]{1,2}\.[0-9]{1,2}", args.game):
        print("game version: three parts, e.g. 26.70.1", file=sys.stderr)
        return 1
    out = Path(__file__).resolve().parent / "sdks" / (args.game + ".lock.json")
    if out.exists():
        print(f"{out} exists already; nothing written", file=sys.stderr)
        return 1
    lock = json.loads(lock_path(args.source).read_text(encoding="utf-8"))
    major, minor, patch = args.game.split(".")
    lock["gameVersion"] = f"1.{major}.{minor}"
    lock["gameRelease"] = args.game
    lock["layoutsFrom"] = {"game": args.source, "note": "LeviLamina has no headers for this game version: the class layouts "
                           "are those of " + args.source + " (unchecked for " + args.game + "). Build with --standalone."}
    out.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} (headers of {args.source}); build: python tools/build_client.py --game {args.game} --standalone")
    return 0


if __name__ == "__main__":
    sys.exit(main())

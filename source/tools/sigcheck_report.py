#!/usr/bin/env python3
"""sigcheck_report: reads logs/sigcheck.log (written by MyClientData.dll at start, client/GameSigs.cpp) and says what
is broken and the exact next command (docs/version-update.md, step b).

Usage:
  python tools/sigcheck_report.py [LOG] [--old GAME] [--new GAME] [--new-version 1.26.70.1]
LOG defaults to %LOCALAPPDATA%\\MyClient\\logs\\sigcheck.log. --old is the newest supported build's game
(Minecraft.Windows.exe or memory image; docs/version-update.md says where the old ones are kept), --new the game
that failed. Without them the commands are printed with placeholders.
"""
import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_sigs  # noqa: E402


def parse(text):
    """(header line, rows, summary line). A row: result, name, fatal, where, pattern, purpose."""
    header, summary, rows = "", "", []
    for line in text.splitlines():
        body = line[24:] if len(line) > 24 and line[4] == "-" and line[10] == " " else line   # drop the timestamp
        if body.startswith("sigcheck:"):
            header, rows, summary = body, [], ""   # a log holds one run; the newest wins if two are pasted together
        elif body.startswith("summary:"):
            summary = body
        else:
            cols = body.split("\t")
            if len(cols) >= 6 and cols[0] in ("OK", "NG", "MULTI", "N/A"):
                rows.append({"result": cols[0], "name": cols[1], "fatal": cols[2] == "FATAL", "where": cols[3],
                             "pattern": cols[4], "purpose": cols[5]})
    return header, rows, summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default = Path(os.environ.get("LOCALAPPDATA", "")) / "MyClient" / "logs" / "sigcheck.log"
    ap.add_argument("log", nargs="?", default=str(default))
    ap.add_argument("--old", default="<OLD Minecraft.Windows.exe>")
    ap.add_argument("--new", default="<NEW Minecraft.Windows.exe>")
    ap.add_argument("--new-version", default="<new version, 4 parts>")
    args = ap.parse_args(argv)
    path = Path(args.log)
    if not path.exists():
        print(f"no {path}. docs/version-update.md step (a): no log means the DLL never ran its check -> look at startup.log")
        return 1
    header, rows, summary = parse(path.read_text(encoding="utf-8", errors="replace"))
    if not header:
        print(f"{path} has no sigcheck run in it (the DLL stopped before the check) -> look at startup.log")
        return 1
    doc = make_sigs.load()
    newest = doc["supported"][-1]["version"]
    print(header)
    print(summary or "(no summary line: the check did not finish -> the game stopped during it; see startup.log)")
    bad = [r for r in rows if r["result"] in ("NG", "MULTI")]
    unsupported = "none (not supported)" in header
    fatal = [r for r in bad if r["fatal"]]
    print()
    if not bad and not unsupported:
        print("Every signature is OK. If a feature still does not work, it is not an address problem: docs/testing.md.")
        return 0
    if unsupported:
        print("This game build is not in tools/sigs.json: it is a new Minecraft version (or one not supported yet).")
        print(f"OK below = the {newest} pattern still matches once; NG = not found; MULTI = found more than once.\n")
    print(f"{len(bad)} to fix ({len(fatal)} fatal: MyClient cannot start until they are fixed). Fatal first:")
    for r in sorted(bad, key=lambda r: (not r["fatal"], r["name"])):
        print(f"  {r['result']:5s} {'FATAL' if r['fatal'] else '     '} {r['name']}")
        print(f"        {r['purpose']}")
    names = " ".join(f'--name "{r["name"]}"' for r in sorted(bad, key=lambda r: (not r["fatal"], r["name"])))
    print("\nNext (docs/version-update.md steps c-e):")
    print(f"  1. python tools/make_sigs.py --scan {args.new} --json scan.json")
    print(f"  2. python tools/findsig.py --old-version {newest} --old {args.old} --new {args.new} --new-version {args.new_version} "
          f"--from-sigcheck \"{path}\" --out findsig.json")
    print(f"     (or by name: {names[:400]}{' ...' if len(names) > 400 else ''})")
    print("  3. CONFIRMED ones: add --apply to step 2. Others: docs/version-update.md step d, then edit tools/sigs.json by hand.")
    if unsupported:
        print(f"  4. python tools/make_sigs.py --add-build {args.new_version} {args.new}   (when every fatal one has an entry)")
    print("  then: build (python tools/build_client.py --game <GAME>), start the game, run this report again until it is all OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

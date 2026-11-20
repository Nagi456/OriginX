#!/usr/bin/env python3
"""Make stable/versions.json, the list of released Minecraft versions the launcher's version switch reads
(launcher/GameVersions.h VersionDbUrl, design 2026-11-20-manifest-signing-design.md §6-1).

It is LiteLDev's public list (the one LeviLauncher reads), copied byte for byte so the launcher parses it as
before. Then, on the author's PC:

  python tools/make_versions_list.py out/versions.json
  python tools/signing.py sign --key <key> --kind versions/stable out/versions.json
  python tools/publish_r2.py list stable/versions.json out/versions.json

A new Minecraft version can be switched to only after this has been done again.
"""
import argparse
import json
import sys
import urllib.request
from pathlib import Path

SOURCE = 'https://raw.githubusercontent.com/LiteLDev/minecraft-windows-gdk-version-db/refs/heads/main/historical_versions.json'
LIMIT = 1024 * 1024   # what the launcher accepts


def check(data):
    """The bytes, once they look like the list the launcher reads (GameVersions.h ParseVersionDb)."""
    if len(data) > LIMIT:
        raise ValueError(f'the list is {len(data)} bytes; the launcher takes at most {LIMIT}')
    doc = json.loads(data)
    if not isinstance(doc, dict) or not isinstance(doc.get('releaseVersions'), list) or not doc['releaseVersions']:
        raise ValueError('releaseVersions is missing or empty')
    return data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('out', type=Path)
    parser.add_argument('--source', default=SOURCE)
    args = parser.parse_args(argv)
    with urllib.request.urlopen(args.source, timeout=60) as response:
        data = check(response.read(LIMIT + 1))
    args.out.write_bytes(data)
    count = len(json.loads(data)['releaseVersions'])
    print(f'wrote {args.out}: {len(data)} bytes, {count} release versions. Sign it with --kind versions/stable.')


if __name__ == '__main__':
    sys.exit(main())

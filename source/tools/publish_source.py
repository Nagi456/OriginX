"""Copies the source of what OriginX distributes into a checkout of the public
repository (github.com/Nagi456/OriginX), without the development notes.

Only files tracked by git are copied. Everything in the target except .git is
replaced, so files deleted here disappear there too.

Usage: python3 tools/publish_source.py <checkout of Nagi456/OriginX>
Then commit and push in that checkout.
"""
import shutil
import subprocess
import sys
from pathlib import Path

# Paths (relative to the repository root) that are not published: notes,
# plans, Claude's settings, the website and the built programs.
EXCLUDE = [
    ".claude/",
    ".github/workflows/sdk-search.yml",
    ".graphifyignore",
    "graphify-out/",
    "CLAUDE.md",
    "VERIFY.md",
    "BUILD-README.txt",
    "SHA256SUMS.txt",
    "MyClient.exe",
    "MyClientLauncher.exe",
    "mods/MyClientData/MyClientData.dll",
    "source/docs/",
    "source/site/",
]

README = """# OriginX

Source code of OriginX, an unofficial client for Minecraft: Bedrock Edition
(Windows). OriginX is not affiliated with Mojang Studios or Microsoft.

- `source/` — the HUD (`MyClient.cpp`), the launcher (`source/launcher/`) and tests
- `mods/MyClientData/` — the DLL loaded by LeviLamina
- `THIRD_PARTY_NOTICES.md`, `licenses/` — third-party code and its licenses

## License

OriginX is licensed under the GNU General Public License v3 (`LICENSE`).
The launcher embeds gamelaunchhelper (GPLv3, `source/launcher/vendor/gamelaunchhelper`).
This repository is updated with every release.

## Name and logo

The name "OriginX" and the OriginX logo are not licensed under the GPL. If you
distribute a modified version, give it a different name and logo, and do not
present it as the official OriginX. The only official downloads are on
https://originx.pages.dev.
"""

# The full GPLv3 text, published as LICENSE.
LICENSE = "source/launcher/vendor/gamelaunchhelper/LICENSE-GPL.md"


def excluded(path):
    return any(path == e or (e.endswith("/") and path.startswith(e)) for e in EXCLUDE)


def published_files(root):
    out = subprocess.run(["git", "ls-files", "-z"], cwd=root, check=True,
                         capture_output=True).stdout.decode("utf-8")
    return sorted(p for p in out.split("\0") if p and not excluded(p))


def publish(root, target):
    root, target = Path(root), Path(target)
    if not (target / ".git").exists():
        raise SystemExit(f"{target} is not a git checkout")
    for item in target.iterdir():
        if item.name == ".git":
            continue
        shutil.rmtree(item) if item.is_dir() and not item.is_symlink() else item.unlink()
    files = published_files(root)
    for rel in files:
        dest = target / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / rel, dest)
    (target / "README.md").write_text(README, encoding="utf-8")
    shutil.copy2(root / LICENSE, target / "LICENSE")
    return files


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    root = Path(__file__).resolve().parents[2]
    files = publish(root, sys.argv[1])
    print(f"Copied {len(files)} files to {sys.argv[1]}")


if __name__ == "__main__":
    main()

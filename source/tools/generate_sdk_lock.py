"""Generate tools/sdks/<game>.lock.json for another LeviLamina client series.

The lock pins the LeviLamina SDK commit, the official client runtime zip and the
header-only dependencies that LeviLamina's xmake.lua asks for.  It only reads
public GitHub data; it never touches a Minecraft installation.

Example:
    python tools/generate_sdk_lock.py --game 26.40.5 --tag v26.40.6
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import urllib.request

REPO = Path(__file__).resolve().parent.parent
BASE = json.loads((REPO / "tools/client-sdk.lock.json").read_text())
BASE_TAG = "v" + BASE["loaderVersion"]
XMAKE_NAME = {"json": "nlohmann_json", "pcg": "pcg_cpp"}  # lock name -> xmake name
NOT_IN_XMAKE = {"debug_assert"}  # transitive dependency of type_safe


def fetch(url, timeout=180):
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return response.read()


def requires(tag):
    text = fetch(f"https://raw.githubusercontent.com/LiteLDev/LeviLamina/{tag}/xmake.lua").decode()
    found = {}
    for name, version in re.findall(r'add_requires\("([\w\-]+)(?:\[[^\]]*\])?\s+([^"]+)"\)', text):
        found[name] = version.strip()
    return found, text


def tag_commit(tag):
    out = subprocess.check_output(["git", "ls-remote", "--tags",
                                   "https://github.com/LiteLDev/LeviLamina", tag + "^{}", tag], text=True)
    lines = [line.split() for line in out.splitlines()]
    peeled = [line[0] for line in lines if line[1].endswith("^{}")]
    return (peeled or [line[0] for line in lines])[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, help="game version this lock targets, e.g. 26.40.5")
    parser.add_argument("--tag", required=True, help="LeviLamina tag with a client build, e.g. v26.40.6")
    args = parser.parse_args()

    wanted, text = requires(args.tag)
    base, _ = requires(BASE_TAG)
    commit = tag_commit(args.tag)
    sdk_url = f"https://codeload.github.com/LiteLDev/LeviLamina/tar.gz/{commit}"
    runtime_url = (f"https://github.com/LiteLDev/LeviLamina/releases/download/{args.tag}/"
                   f"levilamina-{args.tag}-client-release-windows-x64.zip")
    print("SDK commit", commit, flush=True)
    sdk_data = fetch(sdk_url)
    with tempfile.NamedTemporaryFile(suffix=".tar.gz") as tmp:
        tmp.write(sdk_data)
        tmp.flush()
        with tarfile.open(tmp.name) as archive:
            if not any("/src-client/" in member.name for member in archive.getmembers()):
                raise SystemExit(args.tag + " has no src-client/ (not a client-capable SDK)")
    runtime = fetch(runtime_url)
    client = re.search(r'bedrockdata\s+(v[\w.\-]+-client[\w.\-]*)', text)
    headers = []
    for header in BASE["headers"]:
        name = header["name"]
        key = XMAKE_NAME.get(name, name)
        if name in NOT_IN_XMAKE or wanted.get(key) == base.get(key):
            headers.append(header)  # identical pin: reuse verified checksum
            continue
        if key not in wanted:
            raise SystemExit(f"{name}: not required by {args.tag}; review manually")
        if re.fullmatch(r"[0-9a-f]{40}", header["ref"]):
            raise SystemExit(f"{name}: commit-pinned dependency changed "
                             f"({base.get(key)} -> {wanted[key]}); pin a commit by hand")
        ref = wanted[key]
        if header["ref"].startswith("v") and not ref.startswith("v"):
            ref = "v" + ref
        url = f"https://codeload.github.com/{header['repo']}/tar.gz/{ref}"
        print("changed dependency", name, header["ref"], "->", ref, flush=True)
        headers.append({**header, "ref": ref, "url": url, "sha256": hashlib.sha256(fetch(url)).hexdigest()})
    # xmake requires something this lock has no header entry for?  Report it.
    known = {XMAKE_NAME.get(h["name"], h["name"]) for h in BASE["headers"]}
    header_only = {"ctre", "entt", "expected-lite", "fmt", "gsl", "glm", "leveldb", "magic_enum",
                   "nlohmann_json", "rapidjson", "type_safe", "parallel-hashmap", "concurrentqueue",
                   "stb", "pcg_cpp", "pfr"}
    missing = (header_only & set(wanted)) - known
    if missing:
        raise SystemExit("Unmapped dependencies: " + ", ".join(sorted(missing)))

    major, minor = args.game.split(".")[:2]
    lock = {
        "schemaVersion": 1,
        "gameVersion": "1." + major + "." + minor,
        "gameRelease": args.game,
        "loaderVersion": args.tag.lstrip("v"),
        "runtimeData": client.group(1) if client else None,
        "sdk": {"url": sdk_url, "sha256": hashlib.sha256(sdk_data).hexdigest(), "commit": commit},
        "runtime": {"url": runtime_url, "sha256": hashlib.sha256(runtime).hexdigest()},
        "headers": headers,
    }
    out = REPO / "tools/sdks" / (args.game + ".lock.json")
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(lock, indent=2) + "\n")
    print("Wrote", out)


if __name__ == "__main__":
    main()

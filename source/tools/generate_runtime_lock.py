"""Generate a launcher runtime.lock.json for another game version.

Starts from the verified 26.51.1 lock and swaps the version-specific assets
(LeviLamina, PreLoader, bedrock-runtime-data), re-downloading each pinned
archive to record its size, SHA256 and the exact files it installs.

Example:
    python tools/generate_runtime_lock.py --game 26.40.5 --loader 26.40.6 \
        --preloader 1.16.2 --runtime-data 26.40.5-client.9 --out build/runtime-26.40.5.json
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile

REPO = Path(__file__).resolve().parent.parent
BASE = json.loads((REPO / "launcher/runtime.lock.json").read_text())


def fetch(url):
    with urllib.request.urlopen(url, timeout=300) as response:
        return response.read()


def installed(asset, data):
    files = {}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        assert archive.testzip() is None
        for placement in asset["placements"]:
            source = asset["archivePrefix"] + placement["src"].rstrip("/")
            destination = placement["dest"].rstrip("/")
            if placement["type"] == "file":
                files[destination] = archive.read(source)
            else:
                for name in archive.namelist():
                    if name.startswith(source + "/") and not name.endswith("/"):
                        files[destination + "/" + name[len(source) + 1:]] = archive.read(name)
    return {k: hashlib.sha256(v).hexdigest() for k, v in files.items()}


def retarget(asset, version, url, manifest_url):
    new = dict(asset)
    cache = asset["cacheFile"].replace(asset["version"], version)
    data = fetch(url)
    new.update(version=version, url=url, cacheFile=cache, bytes=len(data),
               sha256=hashlib.sha256(data).hexdigest(),
               sourceManifest=manifest_url,
               sourceManifestSha256=hashlib.sha256(fetch(manifest_url)).hexdigest())
    new["installedFiles"] = installed(new, data)
    return new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True)
    parser.add_argument("--loader", required=True)
    parser.add_argument("--preloader", required=True)
    parser.add_argument("--runtime-data", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    major, minor, patch = map(int, args.game.split("."))
    lock = json.loads(json.dumps(BASE))
    lock["gameVersion"] = f"1.{major}.{minor}.{patch:02d}"
    lock["loaderVersion"] = args.loader
    raw = "https://raw.githubusercontent.com/LiteLDev/{0}/v{1}/tooth.json"
    for index, asset in enumerate(lock["assets"]):
        name = asset["name"]
        if name == "LeviLamina":
            lock["assets"][index] = retarget(asset, args.loader,
                f"https://github.com/LiteLDev/LeviLamina/releases/download/v{args.loader}/levilamina-v{args.loader}-client-release-windows-x64.zip",
                raw.format("LeviLamina", args.loader))
        elif name == "PreLoader":
            lock["assets"][index] = retarget(asset, args.preloader,
                f"https://github.com/LiteLDev/PreLoader/releases/download/v{args.preloader}/preloader-v{args.preloader}-windows-x64.zip",
                raw.format("PreLoader", args.preloader))
        elif name == "bedrock-runtime-data":
            v = args.runtime_data
            lock["assets"][index] = retarget(asset, v,
                f"https://github.com/LiteLDev/bedrock-runtime-data/releases/download/v{v}/bedrock-runtime-data-v{v}-windows-x64.zip",
                raw.format("bedrock-runtime-data", v))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(lock, indent=2) + "\n")
    print("Wrote", args.out)


if __name__ == "__main__":
    main()

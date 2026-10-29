"""Verify the exact official dependency archives used by standalone setup.

Archives are a build verification cache, never bundled in the client ZIP.
Pass --download to fetch missing cache entries from their pinned URLs.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile
import urllib.request
import zipfile

REPO = Path(__file__).resolve().parents[1]


def verify(cache, readobj, download):
    lock = json.loads((REPO / "launcher/runtime.lock.json").read_text())
    expected = {"LeviLamina": "26.51.6", "PreLoader": "1.16.3", "PeEditor": "3.9.1",
                "bedrock-runtime-data": "26.51.1-client.7", "CrashLogger": "1.3.3", "levilamina-loc": "1.6.4"}
    assert {a["name"]: a["version"] for a in lock["assets"]} == expected
    assert len(lock["assets"]) == 6 and lock["schemaVersion"] == 1
    cache.mkdir(parents=True, exist_ok=True)
    installed = {}
    for asset in lock["assets"]:
        path = cache / asset["cacheFile"]
        if not path.exists() and download:
            with urllib.request.urlopen(asset["url"], timeout=120) as response:
                path.write_bytes(response.read())
        data = path.read_bytes()
        assert len(data) == asset["bytes"]
        assert hashlib.sha256(data).hexdigest() == asset["sha256"], path
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            assert archive.testzip() is None
            seen = set()
            for info in archive.infolist():
                name = info.filename.replace("\\", "/").rstrip("/")
                assert name and not name.startswith("/") and ":" not in name
                assert not any(p in ("..", ".", "") for p in name.split("/"))
                assert name.casefold() not in seen
                seen.add(name.casefold())
                assert info.external_attr >> 16 & 0o170000 in (0, 0o100000, 0o040000)
            files = {}
            for placement in asset["placements"]:
                source = asset["archivePrefix"] + placement["src"].rstrip("/")
                destination = placement["dest"].rstrip("/")
                if placement["type"] == "file":
                    files[destination] = archive.read(source)
                else:
                    assert placement["type"] == "dir"
                    for name in archive.namelist():
                        if name.startswith(source + "/") and not name.endswith("/"):
                            files[destination + "/" + name[len(source) + 1:]] = archive.read(name)
            assert {name: hashlib.sha256(value).hexdigest() for name, value in files.items()} == asset["installedFiles"]
            for name, value in files.items():
                assert not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts
                assert name not in installed
                installed[name] = value
    assert json.loads(installed["mods/LeviLamina/manifest.json"])["version"] == "26.51.6"
    sdk_lock = json.loads((REPO / "tools/client-sdk.lock.json").read_text())
    assert next(a["sha256"] for a in lock["assets"] if a["name"] == "LeviLamina") == sdk_lock["runtime"]["sha256"]
    with tempfile.TemporaryDirectory(prefix="myclient-runtime-inspection-") as temporary:
        outputs = {}
        for name in ("PreLoader.dll", "PeEditor.exe", "mods/LeviLamina/LeviLamina.dll", "mods/LeviLamina/CrashLogger.exe"):
            path = Path(temporary) / Path(name).name
            path.write_bytes(installed[name])
            info = subprocess.check_output([readobj, "--file-headers", "--coff-imports", "--coff-exports", str(path)], text=True)
            assert "COFF-x86-64" in info, name
            assert not re.search(r"Name: .*LeviLauncher", info, re.IGNORECASE)
            outputs[name] = info
        assert "pl_resolve_symbol" in outputs["PreLoader.dll"]
        assert "PreLoader.dll" in outputs["mods/LeviLamina/LeviLamina.dll"]
    for name, digest in lock["xmlDependency"]["files"].items():
        assert hashlib.sha256((REPO / "launcher/vendor/tinyxml2" / name).read_bytes()).hexdigest() == digest
    print("PASS: 6 pinned official archives, sizes/SHA256/CRC, safe paths, exact file placements, matching SDK loader, x64 binaries and PreLoader anchor/dependency, TinyXML2 source hashes.")
    print("No Minecraft/Windows startup was performed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=REPO / "build/runtime-cache")
    parser.add_argument("--readobj", default="llvm-readobj")
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    verify(args.cache, args.readobj, args.download)

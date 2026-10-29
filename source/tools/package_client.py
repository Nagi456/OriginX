"""Package the game mod, matching HUD, licenses and rebuildable source."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from prepare_client_sdk import LOCK, REPO


def package(sdk, output):
    output.mkdir(parents=True, exist_ok=True)
    files = {
        "README-ja.md": REPO / "README.md",
        "UPDATES-ja.md": REPO / "launcher/UPDATES.md",
        "MyClient.exe": REPO / "build/MyClient.exe",
        "MyClientLauncher.exe": REPO / "build/MyClientLauncher.exe",
        "PrepareRuntime.ps1": REPO / "launcher/PrepareRuntime.ps1",
        "runtime.lock.json": REPO / "launcher/runtime.lock.json",
        "catalog.json": REPO / "launcher/catalog.json",
        "THIRD_PARTY_NOTICES.md": REPO / "THIRD_PARTY_NOTICES.md",
        "licenses/nlohmann-json/LICENSE.MIT": REPO / "launcher/vendor/nlohmann/LICENSE.MIT",
        "licenses/tinyxml2/LICENSE.txt": REPO / "launcher/vendor/tinyxml2/LICENSE.txt",
        "mods/MyClientData/MyClientData.dll": REPO / "build/client/MyClientData.dll",
        "mods/MyClientData/manifest.json": REPO / "build/client/manifest.json",
        "mods/MyClientData/THIRD_PARTY_NOTICES.md": REPO / "THIRD_PARTY_NOTICES.md",
    }
    # Every top-level header/source (a hand-kept list went stale and left out headers MyClient.cpp needs),
    # plus the resource script and icons, so the bundled source rebuilds the HUD as shipped.
    sources = sorted(p.name for p in REPO.iterdir() if p.is_file() and p.suffix in (".h", ".cpp", ".rc"))
    sources += ["build.sh", "build.bat", "README.md", "THIRD_PARTY_NOTICES.md"]
    sources += [str(p.relative_to(REPO)) for p in (REPO / "assets").iterdir() if p.is_file()]
    sources += [str(p.relative_to(REPO)) for folder in ("client", "tests", "tools", "launcher")
                for p in (REPO / folder).rglob("*")
                if p.is_file() and (p.suffix in (".h", ".hpp", ".cpp", ".json", ".py", ".ps1", ".md") or p.name in ("LICENSE.MIT", "LICENSE.txt"))]
    for name in sources:
        files["source/" + name] = REPO / name
    for folder in [sdk / "levilamina", *[sdk / "headers" / h["name"] for h in LOCK["headers"]]]:
        licenses = [p for p in folder.iterdir() if p.is_file() and
                    p.name.lower().startswith(("license", "copying", "copyright"))]
        if not licenses:
            raise RuntimeError("Missing upstream license: " + folder.name)
        for path in licenses:
            files[f"mods/MyClientData/licenses/{folder.name}/{path.name}"] = path
    catalog = json.loads((REPO / "launcher/catalog.json").read_text())
    bundled = [release for release in catalog["versions"] if release["available"]]
    assert len(bundled) == 1 and bundled[0]["id"] == "26.51.1", "This bundle contains only the tested 26.51 adapter"
    for name, expected in bundled[0]["files"].items():
        data = files[name].read_bytes()
        assert len(data) == expected["bytes"] and hashlib.sha256(data).hexdigest() == expected["sha256"], name
    checksums = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in files.items()}
    manifest = "".join(f"{value}  {name}\n" for name, value in sorted(checksums.items()))
    archive = output / "MyClient-26.51-hive-x64.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as package_file:
        for name, path in sorted(files.items()):
            package_file.write(path, name)
        package_file.writestr("SHA256SUMS.txt", manifest)
    with zipfile.ZipFile(archive) as package_file:
        assert package_file.testzip() is None
        assert set(package_file.namelist()) == set(files) | {"SHA256SUMS.txt"}
        for name, value in checksums.items():
            assert hashlib.sha256(package_file.read(name)).hexdigest() == value, name
        assert not any(name.endswith(("LeviLamina.dll", "MyClientBridge.dll", ".lib", ".obj"))
                       for name in package_file.namelist())
        forbidden = {"minecraft.windows.exe", "levilauncher.exe", "levilauncher.dll", "preloader.dll",
                     "peeditor.exe", "lip.exe", "lipd.exe", "bedrock_runtime_data"}
        assert not any(Path(name).name.lower() in forbidden for name in package_file.namelist())
        assert {"MyClientLauncher.exe", "MyClient.exe", "PrepareRuntime.ps1", "runtime.lock.json",
                "mods/MyClientData/MyClientData.dll", "README-ja.md", "catalog.json", "UPDATES-ja.md"} <= set(package_file.namelist())
    print("PASS: ZIP CRC, file inventory, all SHA256 entries and included upstream licenses.")
    print(json.dumps({"archive": str(archive), "bytes": archive.stat().st_size,
                      "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, default=REPO / "build/client-sdk")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    package(args.sdk_root.resolve(), args.output.resolve())

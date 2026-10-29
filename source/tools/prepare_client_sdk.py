"""Download the pinned public client SDK. Does not install/modify Minecraft."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.request
import zipfile

REPO = Path(__file__).resolve().parent.parent
DEFAULT_GAME = "26.51.1"


def lock_path(game=DEFAULT_GAME):
    """26.51.1 keeps the original lock file; other versions live in tools/sdks/."""
    if game == DEFAULT_GAME:
        return REPO / "tools/client-sdk.lock.json"
    path = REPO / "tools/sdks" / (game + ".lock.json")
    if not path.exists():
        known = ", ".join(available_games())
        raise SystemExit(f"No SDK lock for {game}. Available: {known}. "
                         "Create one with tools/generate_sdk_lock.py (needs a LeviLamina client release).")
    return path


def available_games():
    return sorted({DEFAULT_GAME, *(p.name[:-len(".lock.json")] for p in (REPO / "tools/sdks").glob("*.lock.json"))})


def load_lock(game=DEFAULT_GAME):
    return json.loads(lock_path(game).read_text())


def default_sdk_root(game=DEFAULT_GAME):
    return REPO / ("build/client-sdk" if game == DEFAULT_GAME else "build/client-sdk-" + game)


LOCK = load_lock()


def prepare_one(root, name, spec, zip_archive=False):
    cache = root / "downloads"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / (spec["sha256"] + (".zip" if zip_archive else ".tar.gz"))
    if not archive.exists():
        with urllib.request.urlopen(spec["url"], timeout=120) as response:
            with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
                temporary = Path(output.name)
                shutil.copyfileobj(response, output)
        try:
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != spec["sha256"]:
                raise RuntimeError("Download checksum mismatch: " + name)
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
        raise RuntimeError("Cached checksum mismatch: " + str(archive))

    destination = root / name
    marker = destination / ".archive-sha256"
    if marker.exists() and marker.read_text().strip() == spec["sha256"]:
        return
    # Extract only regular files/directories, rejecting links and escaping paths.
    with tempfile.TemporaryDirectory(dir=root) as staging:
        stage = Path(staging)
        if zip_archive:
            with zipfile.ZipFile(archive) as source:
                for member in source.infolist():
                    path = stage / member.filename
                    if not path.resolve().is_relative_to(stage.resolve()):
                        raise RuntimeError("Unsafe archive path")
                    if not member.is_dir():
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(source.read(member))
        else:
            with tarfile.open(archive) as source:
                for member in source.getmembers():
                    # GitHub tarballs have one enclosing directory.
                    relative = "/".join(member.name.split("/")[1:])
                    if not relative or member.isdir():
                        continue
                    if not member.isfile():
                        continue
                    path = stage / relative
                    if not path.resolve().is_relative_to(stage.resolve()):
                        raise RuntimeError("Unsafe archive path")
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with source.extractfile(member) as data, path.open("wb") as output:
                        shutil.copyfileobj(data, output)
        (stage / ".archive-sha256").write_text(spec["sha256"] + "\n")
        if destination.exists():
            shutil.rmtree(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(stage, destination)
    print("Prepared " + name, flush=True)


def prepare(root, game=DEFAULT_GAME):
    root.mkdir(parents=True, exist_ok=True)
    lock = load_lock(game)
    jobs = [("levilamina", lock["sdk"], False), ("runtime", lock["runtime"], True)]
    jobs += [("headers/" + h["name"], h, False) for h in lock["headers"]]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        futures = [executor.submit(prepare_one, root, *job) for job in jobs]
        for future in futures:
            future.result()
    # Upstream xmake installs pcg headers under the pcg_cpp prefix.
    alias = root / "aliases/pcg_cpp"
    if alias.is_symlink():
        alias.unlink()
    shutil.copytree(root / "headers/pcg/include", alias, dirs_exist_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=DEFAULT_GAME, help="game version (default %(default)s)")
    parser.add_argument("--list", action="store_true", help="list game versions that have an SDK lock")
    parser.add_argument("--sdk-root", type=Path, default=None)
    args = parser.parse_args()
    if args.list:
        print("\n".join(available_games()))
    else:
        prepare((args.sdk_root or default_sdk_root(args.game)).resolve(), args.game)

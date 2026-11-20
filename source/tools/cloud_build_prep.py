"""Cloud-container helper for cloud_build.sh: fill build/client-sdk[-<game>] from git clones (codeload.github.com is
blocked in the cloud, git clone works), then run build_client.py with the download step skipped for what is in place.

  python cloud_build_prep.py <game> [build_client.py args...]
"""
import hashlib
import json
import runpy
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import prepare_client_sdk as p  # noqa: E402


def marked(dest, sha):
    m = dest / ".archive-sha256"
    return m.exists() and m.read_text().strip() == sha


def clone(root, name, spec, repo, ref):
    dest = root / name
    if marked(dest, spec["sha256"]):
        return
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "c"
        subprocess.run(["git", "clone", "-q", "--filter=blob:none", "--no-checkout", f"https://github.com/{repo}.git", str(c)], check=True)
        subprocess.run(["git", "-C", str(c), "checkout", "-q", ref], check=True)
        shutil.rmtree(c / ".git")
        if dest.exists():
            shutil.rmtree(dest)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(c, dest)
    (dest / ".archive-sha256").write_text(spec["sha256"] + "\n")
    print("cloned " + name, flush=True)


def populate(game):
    lock = p.load_lock(game)
    root = p.default_sdk_root(game)
    clone(root, "levilamina", lock["sdk"], "LiteLDev/LeviLamina", lock["sdk"]["commit"])
    for h in lock["headers"]:
        clone(root, "headers/" + h["name"], h, h["repo"], h["ref"])
    rt = lock["runtime"]
    dest = root / "runtime"
    if not marked(dest, rt["sha256"]):
        with tempfile.TemporaryDirectory() as tmp:
            z = Path(tmp) / "rt.zip"
            urllib.request.urlretrieve(rt["url"], z)
            if hashlib.sha256(z.read_bytes()).hexdigest() != rt["sha256"]:
                raise SystemExit("runtime checksum mismatch")
            if dest.exists():
                shutil.rmtree(dest)
            zipfile.ZipFile(z).extractall(dest)
        (dest / ".archive-sha256").write_text(rt["sha256"] + "\n")
        print("runtime ok", flush=True)


if __name__ == "__main__":
    game = sys.argv[1]
    populate(game)
    orig = p.prepare_one

    def one(root, name, spec, zip_archive=False):
        if marked(root / name, spec["sha256"]):
            return
        return orig(root, name, spec, zip_archive)

    p.prepare_one = one
    sys.argv = ["build_client.py", "--game", game] + sys.argv[2:]
    runpy.run_path(str(TOOLS / "build_client.py"), run_name="__main__")

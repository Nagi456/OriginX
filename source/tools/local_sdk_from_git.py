"""Lay out the client SDK root from git clones, for a machine where GitHub source tarballs (codeload) are blocked
but git and release downloads work (the cloud sessions, 2026-11-10). CI keeps using prepare_client_sdk.py.

The lock's commits/tags are used as they are; the trees are not checked against the tarball sha256 (only marked
with it), the runtime zip is downloaded and checked. Usage: local_sdk_from_git.py <clone-cache-dir> <game>"""
import hashlib, json, shutil, subprocess, sys, urllib.request, zipfile
from pathlib import Path
REPO = Path(__file__).resolve().parent.parent; sys.path.insert(0, str(REPO / 'tools'))
import prepare_client_sdk as P
GIT = Path(sys.argv[1]); game = sys.argv[2]
lock = P.load_lock(game); root = P.default_sdk_root(game).resolve(); root.mkdir(parents=True, exist_ok=True)
def tree(name, repo, ref, sha):
    dest = root / name
    if (dest / '.archive-sha256').exists() and (dest / '.archive-sha256').read_text().strip() == sha: return
    src = GIT / (repo.replace('/', '__') + '@' + ref)
    if not (src / '.git').exists():
        src.mkdir(parents=True, exist_ok=True)
        run = lambda *a: subprocess.run(['git', '-C', str(src), *a], check=True, capture_output=True)
        run('init', '-q'); run('remote', 'add', 'origin', 'https://github.com/' + repo)
        run('fetch', '-q', '--depth', '1', 'origin', ref); run('checkout', '-q', 'FETCH_HEAD')
    if dest.exists(): shutil.rmtree(dest)
    shutil.copytree(src, dest, ignore=shutil.ignore_patterns('.git'), symlinks=False, ignore_dangling_symlinks=True)
    (dest / '.archive-sha256').write_text(sha + '\n'); print('prepared', name, flush=True)
tree('levilamina', 'LiteLDev/LeviLamina', lock['sdk']['commit'], lock['sdk']['sha256'])
for h in lock['headers']: tree('headers/' + h['name'], h['repo'], h['ref'], h['sha256'])
rt = lock['runtime']; dest = root / 'runtime'
if not ((dest / '.archive-sha256').exists() and (dest / '.archive-sha256').read_text().strip() == rt['sha256']):
    z = GIT / (rt['sha256'] + '.zip')
    if not z.exists(): subprocess.run(['curl', '-sL', '-o', str(z), rt['url']], check=True)
    assert hashlib.sha256(z.read_bytes()).hexdigest() == rt['sha256'], 'runtime checksum'
    if dest.exists(): shutil.rmtree(dest)
    with zipfile.ZipFile(z) as f: f.extractall(dest)
    (dest / '.archive-sha256').write_text(rt['sha256'] + '\n'); print('prepared runtime', flush=True)
shutil.copytree(root / 'headers/pcg/include', root / 'aliases/pcg_cpp', dirs_exist_ok=True)
print('SDK root ready:', root)

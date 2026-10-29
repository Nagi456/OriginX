"""Create immutable GitHub release assets and a catalog; never publishes remotely."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import re
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def package(sdk: Path, output: Path, revision=1, sequence=1, catalog_path=None, package_dir=None, version="26.51.1"):
    if not 0 < revision <= 1_000_000_000 or not 0 < sequence <= 1_000_000_000:
        raise ValueError("Revision/sequence out of range")
    if not re.fullmatch(r"[1-9][0-9]?\.[0-9]{1,2}\.[0-9]{1,2}", version):
        raise ValueError("Invalid game version")
    output.mkdir(parents=True, exist_ok=True)
    if package_dir:
        if package_dir.is_symlink() or any(p.is_symlink() for p in package_dir.rglob('*')):
            raise ValueError('Linked package paths are not supported')
        files = {p.relative_to(package_dir).as_posix(): p for p in package_dir.rglob('*') if p.is_file()}
    else:
        files = {
            'runtime.lock.json': ROOT / 'launcher/runtime.lock.json',
            'mods/MyClientData/MyClientData.dll': ROOT / 'build/client/MyClientData.dll',
            'mods/MyClientData/manifest.json': ROOT / 'build/client/manifest.json',
            'mods/MyClientData/THIRD_PARTY_NOTICES.md': ROOT / 'THIRD_PARTY_NOTICES.md',
        }
        lock = json.loads((ROOT / 'tools/client-sdk.lock.json').read_text(encoding='utf-8'))
        for folder in [sdk / 'levilamina', *[sdk / 'headers' / h['name'] for h in lock['headers']]]:
            licenses = [p for p in folder.iterdir() if p.is_file() and p.name.lower().startswith(('license', 'copying', 'copyright'))]
            if not licenses:
                raise ValueError(f'Missing licenses: {folder.name}')
            for path in licenses:
                files[f'mods/MyClientData/licenses/{folder.name}/{path.name}'] = path
    for name in files:
        allowed = name in {'runtime.lock.json', 'mods/MyClientData/MyClientData.dll',
                           'mods/MyClientData/manifest.json', 'mods/MyClientData/THIRD_PARTY_NOTICES.md'}
        if not allowed and not re.fullmatch(r'mods/MyClientData/licenses/[A-Za-z0-9_-]+/[A-Za-z0-9_-]+(\.(txt|md|MIT|LESSER|spdx))?', name):
            raise ValueError(f'Unexpected package file: {name}')
    runtime = json.loads(files['runtime.lock.json'].read_text(encoding='utf-8'))
    major, minor, patch = map(int, version.split('.'))
    if version != f'{major}.{minor}.{patch}' or runtime['gameVersion'] != f'1.{major}.{minor}.{patch:02d}':
        raise ValueError('Prepared runtime does not match the requested game version')
    loader = runtime['loaderVersion']
    mod = json.loads(files['mods/MyClientData/manifest.json'].read_text(encoding='utf-8'))
    if mod['name'] != 'MyClientData' or mod['entry'] != 'MyClientData.dll' or mod['type'] != 'native' or mod.get('dependencies'):
        raise ValueError('Prepared mod does not match the loader')
    dll = files['mods/MyClientData/MyClientData.dll'].read_bytes()
    pe = struct.unpack_from('<I', dll, 60)[0]
    if dll[:2] != b'MZ' or dll[pe:pe+6] != b'PE\0\0\x64\x86' or 'Local\\MyClient.Bridge.v2.'.encode('utf-16-le') not in dll:
        raise ValueError('Expected Windows x64 mod with bridge ABI v2')
    tag = f'myclient-{version}-r{revision}'
    assets = {}
    staging = output / tag
    for relative, path in files.items():
        asset = relative.replace('/', '__')
        assets[relative] = {'url': f'https://github.com/Nagi456/MyClient/releases/download/{tag}/{asset}',
                            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'bytes': path.stat().st_size}
    previous = json.loads(catalog_path.read_text(encoding='utf-8')) if catalog_path else None
    versions = []
    identifiers = [v['id'] for v in previous['versions']] if previous else ['26.45.1', '26.50.4', '26.51.1', '26.52.3']
    if version not in identifiers:
        identifiers.append(version)
    for item in identifiers:
        if item == version:
            versions.append({'id': item, 'available': True, 'loader': loader,
                             'revision': revision, 'launcherApi': 1, 'bridgeAbi': 2,
                             'platform': 'gdk-release-x64', 'files': assets})
        elif previous:
            versions.append(next(v for v in previous['versions'] if v['id'] == item))
        else:
            versions.append({'id': item, 'available': False, 'reason': '対応するSDK・DLLが未準備です。この版はまだ起動できません。'})
    catalog = {'schemaVersion': 1, 'sequence': sequence, 'versions': versions}
    if previous:
        if sequence < previous['sequence'] or (sequence == previous['sequence'] and catalog != previous):
            raise ValueError('Increment sequence when changing the catalog')
        for old in previous['versions']:
            if old['id'] == version and old['available']:
                if revision < old['revision'] or (revision == old['revision'] and assets != old['files']):
                    raise ValueError('Increment revision when changing published package files')
    staging.mkdir(exist_ok=True)
    expected_names = {name.replace('/', '__') for name in files}
    if any(p.name not in expected_names for p in staging.iterdir()):
        raise ValueError('Unexpected files in release output; use a clean output directory')
    for relative, path in files.items():
        asset_path = staging / relative.replace('/', '__')
        if asset_path.exists() and asset_path.read_bytes() != path.read_bytes():
            raise ValueError('Release asset already exists with different content; increment revision')
    for relative, path in files.items():
        shutil.copy2(path, staging / relative.replace('/', '__'))
    encoded = json.dumps(catalog, ensure_ascii=False, indent=2) + '\n'
    (ROOT / 'launcher/catalog.json').write_text(encoded, encoding='utf-8', newline='\n')
    (output / 'catalog.json').write_text(encoded, encoding='utf-8', newline='\n')
    archive = output / 'MyClient-update-release-assets.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(output / 'catalog.json', 'client-catalog/catalog.json')
        for file in sorted(staging.iterdir()):
            z.write(file, f'{tag}/{file.name}')
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for file in assets.values():
            name = tag + '/' + file['url'].rsplit('/', 1)[1]
            data = z.read(name)
            assert len(data) == file['bytes'] and hashlib.sha256(data).hexdigest() == file['sha256']
    print(f'PASS: update asset inventory, CRC and SHA256: {archive}')
    print('Prepared locally. Nothing has been published to GitHub.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--revision', type=int, default=1)
    parser.add_argument('--sequence', type=int, default=1)
    parser.add_argument('--catalog', type=Path, help='Previous published catalog; required when issuing updates')
    parser.add_argument('--package-dir', type=Path, help='Prepared, tested mod/runtime package for another game version')
    parser.add_argument('--game-version', default='26.51.1')
    args = parser.parse_args()
    package(args.sdk_root.resolve(), args.output.resolve(), args.revision, args.sequence,
            args.catalog, args.package_dir, args.game_version)

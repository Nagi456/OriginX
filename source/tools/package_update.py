"""Create immutable release assets and a catalog; never publishes remotely (tools/publish_r2.py uploads them).

The catalog's URLs point at OriginX's public R2 downloads: <base-url>/<tag>/<asset>."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import re
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BASE_URL = 'https://originx.originx-client.workers.dev/dl/stable'
HUD = 'hud/MyClient.exe'


def require_x64_exe(data, what):
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError(f'{what} is not a Windows program')
    pe = struct.unpack_from('<I', data, 60)[0]
    if data[pe:pe+6] != b'PE\0\0\x64\x86':
        raise ValueError(f'{what} is not a Windows x64 program')


def _imports_levilamina(data):
    """The PE's import table names LeviLamina.dll or PreLoader.dll (a section-mapped read of the import directory)."""
    try:
        return _import_names_have_loader(data)
    except (struct.error, ValueError):
        raise ValueError('The DLL\'s import table cannot be read')


def _import_names_have_loader(data):
    pe = struct.unpack_from('<I', data, 60)[0]
    count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, pe + 24 + optional + i * 40 + 8) for i in range(count)]   # vsize, rva, rawsize, raw

    def offset(rva):
        for vsize, start, rawsize, raw in sections:
            if start <= rva < start + max(vsize, rawsize):
                return raw + rva - start
        return None
    at = offset(struct.unpack_from('<I', data, pe + 24 + 112 + 8)[0])   # data directory 1 (imports) of a PE32+
    while at is not None and at + 20 <= len(data):
        name_rva = struct.unpack_from('<I', data, at + 12)[0]
        if not name_rva:
            break
        name_at = offset(name_rva)
        if name_at is None:
            break
        name = data[name_at:data.index(b'\0', name_at)].decode('ascii', 'replace').lower()
        if name in ('levilamina.dll', 'preloader.dll'):
            return True
        at += 20
    return False


def package(sdk: Path, output: Path, revision=1, sequence=1, catalog_path=None, package_dir=None, version="26.51.1",
            hud=None, base_url=BASE_URL, channel="stable", test_number=None, standalone=False):
    """channel="stable" (default) prepares a public release. channel="test" prepares a build for the moderators'
    test channel (docs/superpowers/specs/2026-11-09-release-channels-design.md): the tag gets -test<test_number>, the
    files are named by R2 key (test/<tag>/<asset>), the entry also goes to <tag>/entry.json, the list (catalog_path =
    the channel's previous list) keeps the other game versions and its sequence is the previous one + 1, and the
    stable launcher/catalog.json is not touched.
    standalone=True packages the DLL that runs without LeviLamina (tools/build_client.py --standalone): no
    runtime.lock.json, "loader": "standalone" and "launcherApi": 2, which launchers before build 27 list as
    "a newer launcher is needed" (launcher/Updates.h)."""
    if channel not in ("stable", "test"):
        raise ValueError("Unknown channel")
    if channel == "test" and (not isinstance(test_number, int) or not 0 < test_number <= 1_000_000):
        raise ValueError("A test build needs test_number (a positive integer)")
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
        # 26.51.1 keeps the original paths; another version has its own runtime lock, build folder and SDK lock.
        default = version == '26.51.1'
        build = ROOT / (('build/client' if default else f'build/client-{version}') + ('-standalone' if standalone else ''))
        files = {
            'runtime.lock.json': ROOT / ('launcher/runtime.lock.json' if default else f'launcher/runtime-{version}.lock.json'),
            'mods/MyClientData/MyClientData.dll': build / 'MyClientData.dll',
            'mods/MyClientData/manifest.json': build / 'manifest.json',
            'mods/MyClientData/THIRD_PARTY_NOTICES.md': ROOT / 'THIRD_PARTY_NOTICES.md',
        }
        lock = json.loads((ROOT / ('tools/client-sdk.lock.json' if default else f'tools/sdks/{version}.lock.json')).read_text(encoding='utf-8'))
        for folder in [sdk / 'levilamina', *[sdk / 'headers' / h['name'] for h in lock['headers']]]:
            licenses = [p for p in folder.iterdir() if p.is_file() and p.name.lower().startswith(('license', 'copying', 'copyright'))]
            if not licenses:
                raise ValueError(f'Missing licenses: {folder.name}')
            for path in licenses:
                files[f'mods/MyClientData/licenses/{folder.name}/{path.name}'] = path
        files['mods/MyClientData/licenses/minhook/LICENSE.txt'] = ROOT / 'client/vendor/minhook/LICENSE.txt'   # compiled into the DLL
    if standalone:
        files.pop('runtime.lock.json', None)   # no loader is installed: nothing to lock
    if hud:
        files[HUD] = Path(hud)
    for name in files:
        allowed = name in {'runtime.lock.json', 'mods/MyClientData/MyClientData.dll',
                           'mods/MyClientData/manifest.json', 'mods/MyClientData/THIRD_PARTY_NOTICES.md', HUD}
        if not allowed and not re.fullmatch(r'mods/MyClientData/licenses/[A-Za-z0-9_-]+/[A-Za-z0-9_-]+(\.(txt|md|MIT|LESSER|spdx))?', name):
            raise ValueError(f'Unexpected package file: {name}')
    major, minor, patch = map(int, version.split('.'))
    if version != f'{major}.{minor}.{patch}':
        raise ValueError('Invalid game version')
    if standalone:
        loader = 'standalone'
    else:
        runtime = json.loads(files['runtime.lock.json'].read_text(encoding='utf-8'))
        if runtime['gameVersion'] != f'1.{major}.{minor}.{patch:02d}':
            raise ValueError('Prepared runtime does not match the requested game version')
        loader = runtime['loaderVersion']
    mod = json.loads(files['mods/MyClientData/manifest.json'].read_text(encoding='utf-8'))
    if mod['name'] != 'MyClientData' or mod['entry'] != 'MyClientData.dll' or mod['type'] != 'native' or mod.get('dependencies'):
        raise ValueError('Prepared mod does not match the loader')
    dll = files['mods/MyClientData/MyClientData.dll'].read_bytes()
    pe = struct.unpack_from('<I', dll, 60)[0]
    if dll[:2] != b'MZ' or dll[pe:pe+6] != b'PE\0\0\x64\x86' or 'Local\\MyClient.Bridge.v2.'.encode('utf-16-le') not in dll:
        raise ValueError('Expected Windows x64 mod with bridge ABI v2')
    if standalone and _imports_levilamina(dll):
        raise ValueError('The standalone DLL imports LeviLamina')
    if HUD in files:
        require_x64_exe(files[HUD].read_bytes(), 'The HUD')
    if channel == 'stable' and not re.fullmatch(r'https://[A-Za-z0-9.-]+(/[A-Za-z0-9._-]+)*', base_url):
        raise ValueError('Invalid base URL')
    tag = f'myclient-{version}-r{revision}' + (f'-test{test_number}' if channel == 'test' else '')
    assets = {}
    staging = output / tag
    for relative, path in files.items():
        asset = relative.replace('/', '__')
        # a test build names its files by R2 key: the Worker turns the key into a signed link for those allowed
        url = f'test/{tag}/{asset}' if channel == 'test' else f'{base_url}/{tag}/{asset}'
        assets[relative] = {'url': url,
                            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'bytes': path.stat().st_size}
    previous = json.loads(catalog_path.read_text(encoding='utf-8')) if catalog_path else None
    entry = {'id': version, 'available': True, 'loader': loader,
             'revision': revision, 'launcherApi': 2 if standalone else 1, 'bridgeAbi': 2,
             'platform': 'gdk-release-x64', 'files': assets}
    versions = []
    if channel == 'test':
        # the channel's list holds only builds that exist: the other game versions stay, this one is replaced
        versions = [dict(v) for v in previous['versions']] if previous else []
        index = next((i for i, v in enumerate(versions) if v['id'] == version), None)
        if index is None:
            versions.append(entry)
        else:
            versions[index] = entry
        sequence = previous['sequence'] + 1 if previous else 1
    else:
        identifiers = [v['id'] for v in previous['versions']] if previous else ['26.45.1', '26.50.4', '26.51.1', '26.52.3']
        if version not in identifiers:
            identifiers.append(version)
        for item in identifiers:
            if item == version:
                versions.append(entry)
            elif previous:
                versions.append(next(v for v in previous['versions'] if v['id'] == item))
            else:
                versions.append({'id': item, 'available': False, 'reason': '対応するSDK・DLLが未準備です。この版はまだ起動できません。'})
    catalog = {'schemaVersion': 1, 'sequence': sequence, 'versions': versions}
    if previous and channel == 'stable':
        if sequence < previous['sequence'] or (sequence == previous['sequence'] and catalog != previous):
            raise ValueError('Increment sequence when changing the catalog')
        for old in previous['versions']:
            if old['id'] == version and old['available']:
                if revision < old['revision'] or (revision == old['revision'] and assets != old['files']):
                    raise ValueError('Increment revision when changing published package files')
    staging.mkdir(exist_ok=True)
    expected_names = {name.replace('/', '__') for name in files} | ({'entry.json'} if channel == 'test' else set())
    if any(p.name not in expected_names for p in staging.iterdir()):
        raise ValueError('Unexpected files in release output; use a clean output directory')
    for relative, path in files.items():
        asset_path = staging / relative.replace('/', '__')
        if asset_path.exists() and asset_path.read_bytes() != path.read_bytes():
            raise ValueError('Release asset already exists with different content; increment revision')
    for relative, path in files.items():
        shutil.copy2(path, staging / relative.replace('/', '__'))
    if channel == 'test':
        (staging / 'entry.json').write_text(json.dumps(entry, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
    encoded = json.dumps(catalog, ensure_ascii=False, indent=2) + '\n'
    if channel == 'stable':
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
    print('Prepared locally. Nothing has been published (tools/publish_r2.py release <output> uploads it).')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--revision', type=int, default=1)
    parser.add_argument('--sequence', type=int, default=1)
    parser.add_argument('--catalog', type=Path, help='Previous published catalog; required when issuing updates')
    parser.add_argument('--package-dir', type=Path, help='Prepared, tested mod/runtime package for another game version')
    parser.add_argument('--game-version', default='26.51.1')
    parser.add_argument('--hud', type=Path, help='MyClient.exe built for this release (shipped as hud/MyClient.exe)')
    parser.add_argument('--base-url', default=BASE_URL)
    parser.add_argument('--channel', choices=('stable', 'test'), default='stable')
    parser.add_argument('--test-number', type=int, help='required for --channel test: the tag ends in -test<N>')
    parser.add_argument('--standalone', action='store_true', help='package the DLL that runs without LeviLamina (launcherApi 2)')
    args = parser.parse_args()
    package(args.sdk_root.resolve(), args.output.resolve(), args.revision, args.sequence,
            args.catalog, args.package_dir, args.game_version, args.hud, args.base_url,
            channel=args.channel, test_number=args.test_number, standalone=args.standalone)

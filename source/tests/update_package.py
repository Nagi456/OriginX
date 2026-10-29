"""Release publisher regression: immutable revisions and catalog preservation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('publisher', ROOT / 'tools/package_update.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


def reject(action):
    try:
        action()
    except (ValueError, KeyError):
        return
    raise AssertionError('Expected rejection')


with tempfile.TemporaryDirectory(prefix='myclient-publisher-') as temp:
    root = Path(temp)
    source = root / 'package'
    (source / 'mods/MyClientData').mkdir(parents=True)
    shutil.copy2(ROOT / 'build/client/MyClientData.dll', source / 'mods/MyClientData/MyClientData.dll')
    shutil.copy2(ROOT / 'build/client/manifest.json', source / 'mods/MyClientData/manifest.json')
    shutil.copy2(ROOT / 'launcher/runtime.lock.json', source / 'runtime.lock.json')
    (root / 'launcher').mkdir()
    publisher.ROOT = root  # Keep test catalogs out of the repository.
    output = root / 'output'
    publisher.package(root, output, package_dir=source)
    initial = (output / 'catalog.json').read_bytes()
    prior = root / 'prior.json'
    prior.write_bytes(initial)
    publisher.package(root, output, catalog_path=prior, package_dir=source)  # Reproducible same revision.
    assert (output / 'catalog.json').read_bytes() == initial
    dll = source / 'mods/MyClientData/MyClientData.dll'
    dll.write_bytes(dll.read_bytes() + b'fixture-only-new-build')
    before = {p.relative_to(output): hashlib.sha256(p.read_bytes()).hexdigest() for p in output.rglob('*') if p.is_file()}
    reject(lambda: publisher.package(root, output, sequence=2, catalog_path=prior, package_dir=source))
    after = {p.relative_to(output): hashlib.sha256(p.read_bytes()).hexdigest() for p in output.rglob('*') if p.is_file()}
    assert before == after  # A rejected publish must not alter prepared assets.
    publisher.package(root, output, revision=2, sequence=2, catalog_path=prior, package_dir=source)
    new = json.loads((output / 'catalog.json').read_bytes())
    assert new['sequence'] == 2 and new['versions'][2]['revision'] == 2
    assert [v['id'] for v in new['versions']] == ['26.45.1', '26.50.4', '26.51.1', '26.52.3']
    assert [v['available'] for v in new['versions']] == [False, False, True, False]
    with zipfile.ZipFile(output / 'MyClient-update-release-assets.zip') as z:
        assert z.testzip() is None
        assert all(not name.startswith('myclient-26.51.1-r1/') for name in z.namelist())
    reject(lambda: publisher.package(root, output, revision=3, sequence=3, package_dir=source, version='26.52.3'))
    (source / 'mods/MyClientData/extra.dll').write_text('unregistered DLL')
    reject(lambda: publisher.package(root, output, revision=3, sequence=3, package_dir=source))
print('PASS: revision/catalog increments, same-revision rejection preserves outputs, prior versions retained, runtime mismatch and extra DLL rejection.')

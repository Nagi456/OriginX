"""Signatures on the update lists: the launcher's check (manifest_signature.cpp) and tools/signing.py agree."""
import base64
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'tools'))
import signing  # noqa: E402


@unittest.skipUnless(shutil.which('g++'), 'C++ compiler required')
class LauncherCheck(unittest.TestCase):
    def test_launcher_check(self):
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'manifest_signature'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(HERE.parent),
                            '-I', str(HERE.parent / 'launcher' / 'vendor'), str(HERE / 'manifest_signature.cpp'), '-o', str(exe)], check=True)
            self.assertIn('PASS', subprocess.check_output([str(exe)], text=True))


class SigningTool(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_keygen_sign_verify(self):
        key_path = self.dir / 'k1.key'
        pub = signing.keygen(1, key_path, 'correct horse')
        self.assertEqual((self.dir / 'k1.pub').read_text().split(), ['1', pub])
        self.assertNotIn(pub, key_path.read_text())          # the key file holds the encrypted PEM only
        self.assertIn('ENCRYPTED PRIVATE KEY', key_path.read_text())
        with self.assertRaises(FileExistsError):
            signing.keygen(1, key_path, 'again')
        with self.assertRaises(Exception):
            signing.load_key(key_path, 'wrong')
        key_id, key = signing.load_key(key_path, 'correct horse')
        data = b'{"schemaVersion":1,"sequence":1,"versions":[]}\n'
        sig = signing.sign_bytes('catalog/stable', data, key_id, key)
        keys = {1: bytes.fromhex(pub)}
        self.assertIsNone(signing.verify_bytes('catalog/stable', data, sig, keys))
        self.assertEqual(signing.verify_bytes('catalog/early', data, sig, keys), 'mismatch')
        self.assertEqual(signing.verify_bytes('catalog/stable', data + b' ', sig, keys), 'mismatch')
        self.assertEqual(signing.verify_bytes('catalog/stable', data, sig, {}), 'unknown key 1')
        self.assertEqual(signing.verify_bytes('catalog/stable', data, b'{}', keys), 'malformed')
        with self.assertRaises(ValueError):
            signing.keygen(2, self.dir / 'k2.key', '')

    def test_cli_sign_then_verify_against_a_keys_header(self):
        key_path = self.dir / 'k1.key'
        pub = signing.keygen(1, key_path, 'pw')
        header = self.dir / 'SigningKeys.h'
        header.write_text('inline constexpr std::array<SigningKey, 1> StableSigningKeys{{\n    ' + signing.cpp_entry(1, pub) + ',\n}};\n')
        self.assertEqual(signing.embedded_keys(header), {1: bytes.fromhex(pub)})
        listing = self.dir / 'launcher.json'
        listing.write_text('{"schemaVersion":1,"build":41,"sha256":"' + 'a' * 64 + '"}\n')
        with mock.patch('getpass.getpass', return_value='pw'):
            signing.main(['sign', '--key', str(key_path), '--kind', 'launcher/stable', str(listing)])
        signing.main(['verify', '--kind', 'launcher/stable', '--keys', str(header), str(listing)])
        with self.assertRaises(SystemExit):
            signing.main(['verify', '--kind', 'catalog/stable', '--keys', str(header), str(listing)])

    def test_embed_fills_the_empty_tables_once(self):
        header = self.dir / 'SigningKeys.h'
        header.write_text(signing.EMPTY_STABLE + '\n' + signing.EMPTY_TEST + '\n')
        k1 = signing.keygen(1, self.dir / 'a.key', 'pw')
        k2 = signing.keygen(2, self.dir / 'b.key', 'pw')
        seed, t1 = signing.keygen_test()
        self.assertEqual(len(base64.b64decode(seed)), 32)
        self.assertFalse(any(p.name.startswith('10') for p in self.dir.iterdir()))   # no file for the test key
        with self.assertRaises(ValueError):
            signing.embed_keys({1: k1}, {101: t1}, header)
        with self.assertRaises(ValueError):
            signing.embed_keys({1: k1, 2: k2}, {}, header)
        with self.assertRaises(ValueError):
            signing.embed_keys({1: k1, 2: k1}, {101: t1}, header)
        with self.assertRaises(ValueError):
            signing.embed_keys({1: k1, 2: k2}, {101: k1}, header)   # a stable key reused as the test key
        with self.assertRaises(ValueError):
            signing.embed_keys({1: k1, 101: k2}, {102: t1}, header)  # a test id in the stable table
        with self.assertRaises(ValueError):
            signing.embed_keys({1: k1, 2: k2}, {3: t1}, header)      # a stable id in the test table
        with self.assertRaises(ValueError):
            signing.embed_keys({1: k1, 2: 'ab' * 31}, {101: t1}, header)
        signing.main(['embed', '--pub', f'1={k1}', '--pub', f'2={k2}', '--test-pub', f'101={t1}', '--keys', str(header)])
        self.assertEqual(signing.embedded_keys(header), {1: bytes.fromhex(k1), 2: bytes.fromhex(k2), 101: bytes.fromhex(t1)})
        text = header.read_text(encoding='utf-8')
        self.assertIn('std::array<SigningKey, 2> StableSigningKeys', text)
        self.assertIn('std::array<SigningKey, 1> TestSigningKeys', text)
        self.assertEqual(signing.fingerprint(k1), k1[:16])
        self.assertEqual(signing.fingerprint(bytes.fromhex(k1)), k1[:16])
        with self.assertRaises(ValueError):
            signing.embed_keys({1: k1, 2: k2}, {101: t1}, header)

    def test_kinds_are_tied_to_their_table(self):
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        key = Ed25519PrivateKey.generate()
        pub = bytes.fromhex(signing.public_hex(key))
        data = b'{"schemaVersion":1,"sequence":1}\n'
        as_test = signing.sign_bytes('catalog/test', data, 101, key)
        as_stable = signing.sign_bytes('catalog/stable', data, 1, key)
        keys = {1: pub, 101: pub}
        self.assertIsNone(signing.verify_bytes('catalog/test', data, as_test, keys))
        self.assertIsNone(signing.verify_bytes('catalog/stable', data, as_stable, keys))
        self.assertEqual(signing.verify_bytes('catalog/test', data, signing.sign_bytes('catalog/test', data, 1, key), keys), 'unknown key 1')
        self.assertEqual(signing.verify_bytes('catalog/stable', data, signing.sign_bytes('catalog/stable', data, 101, key), keys), 'unknown key 101')
        listing = self.dir / 'catalog.json'
        listing.write_bytes(data)
        with self.assertRaises(SystemExit):
            signing.main(['sign', '--key', str(self.dir / 'none.key'), '--kind', 'catalog/test', str(listing)])

    def test_the_repository_header_parses(self):
        self.assertIsInstance(signing.embedded_keys(), dict)


if __name__ == '__main__':
    unittest.main()

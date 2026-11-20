#!/usr/bin/env python3
"""Ed25519 signatures on OriginX update lists (docs/signing.md).

Run on the author's PC only. A stable key (K1, K2: ids 1..100) never leaves it (never CI, the Worker, R2, Git or a
cloud session). The test key (T1: id 101..) lives only in the Worker's Secret TEST_MANIFEST_KEY and signs catalog/test
alone (docs/superpowers/specs/2026-11-20-test-signing-key-design.md).

  keygen  --key-id N --out C:\\originx-signing\\k1.key   make a stable key (asks a passphrase twice), print the public half
  keygen  --test [--key-id 101]                         make a test key: print its seed once for the Worker, keep no file
  embed   --pub 1=HEX --pub 2=HEX --test-pub 101=HEX    write the public keys into launcher/SigningKeys.h
  check   --key FILE                                    the key opens with its passphrase; print its id and public half
  sign    --key FILE --kind KIND LIST                   write LIST.sig
  verify  --kind KIND LIST [--keys launcher/SigningKeys.h]   check LIST.sig against the launcher's public keys

KIND is catalog/stable, catalog/test, catalog/early, launcher/stable, launcher/test or versions/stable. The signed bytes are
"OriginX-manifest-v1\\n" + KIND + "\\n" + the exact bytes of LIST (launcher/ManifestSignature.h).
Needs `pip install cryptography`.
"""
import argparse
import base64
import getpass
import hashlib
import json
import re
import sys
from pathlib import Path

KINDS = ('catalog/stable', 'catalog/test', 'catalog/early', 'launcher/stable', 'launcher/test', 'versions/stable')
FIRST_TEST_KEY = 101
TEST_KINDS = ('catalog/test',)   # signed by the Worker with a test key; every other kind only by a stable key
KEYS_HEADER = Path(__file__).resolve().parent.parent / 'launcher' / 'SigningKeys.h'


def message(kind, data):
    if kind not in KINDS:
        raise ValueError(f'unknown kind {kind!r}; one of {", ".join(KINDS)}')
    return b'OriginX-manifest-v1\n' + kind.encode('ascii') + b'\n' + data


def _crypto():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
    except ImportError:
        sys.exit('cryptography is not installed: pip install cryptography')
    return serialization, Ed25519PrivateKey, Ed25519PublicKey


def public_hex(private_key):
    serialization, _, _ = _crypto()
    return private_key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()


def cpp_entry(key_id, pub_hex):
    data = bytes.fromhex(pub_hex)
    return '{%d, {%s}}' % (key_id, ', '.join(f'0x{b:02x}' for b in data))


def keygen(key_id, out, passphrase):
    serialization, Ed25519PrivateKey, _ = _crypto()
    out = Path(out)
    if out.exists():
        raise FileExistsError(f'{out} exists; a key is never overwritten')
    if not passphrase:
        raise ValueError('a passphrase is required')
    key = Ed25519PrivateKey.generate()
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                            serialization.BestAvailableEncryption(passphrase.encode('utf-8')))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({'keyId': key_id, 'pem': pem.decode('ascii')}, indent=2) + '\n', encoding='ascii')
    pub = public_hex(key)
    out.with_suffix('.pub').write_text(f'{key_id} {pub}\n', encoding='ascii')
    return pub


def keygen_test(key_id=FIRST_TEST_KEY):
    """A test key: (seed base64 for the Worker Secret, public hex). Nothing is written to disk."""
    serialization, Ed25519PrivateKey, _ = _crypto()
    if key_id < FIRST_TEST_KEY:
        raise ValueError(f'test key ids start at {FIRST_TEST_KEY}')
    key = Ed25519PrivateKey.generate()
    seed = key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw, serialization.NoEncryption())
    return base64.b64encode(seed).decode('ascii'), public_hex(key)


def load_key(path, passphrase):
    serialization, _, _ = _crypto()
    stored = json.loads(Path(path).read_text(encoding='ascii'))
    key = serialization.load_pem_private_key(stored['pem'].encode('ascii'), passphrase.encode('utf-8'))
    return int(stored['keyId']), key


def sign_bytes(kind, data, key_id, key):
    signature = key.sign(message(kind, data))
    return (json.dumps({'schemaVersion': 1, 'keyId': key_id, 'signature': base64.b64encode(signature).decode('ascii')}) + '\n').encode('ascii')


def fingerprint(pub_hex):
    """The first 8 bytes of a public key as hex, for checking it by eye against the paper backup."""
    data = bytes.fromhex(pub_hex) if isinstance(pub_hex, str) else bytes(pub_hex)
    return data[:8].hex()


EMPTY_STABLE = 'inline constexpr std::array<SigningKey, 0> StableSigningKeys{};'
EMPTY_TEST = 'inline constexpr std::array<SigningKey, 0> TestSigningKeys{};'


def _check_pubs(pubs, test, seen):
    _, _, Ed25519PublicKey = _crypto()
    for key_id, pub in pubs.items():
        data = bytes.fromhex(pub)
        if key_id <= 0 or key_id > 0xFFFF or len(data) != 32:
            raise ValueError(f'key {key_id}: an id above 0 and 32 bytes of hex are needed')
        if (key_id >= FIRST_TEST_KEY) != test:
            raise ValueError(f'key {key_id}: stable key ids are 1..100, test key ids {FIRST_TEST_KEY} and up')
        if data in seen:
            raise ValueError('the same public key twice: every key must be a different key')
        seen.add(data)
        Ed25519PublicKey.from_public_bytes(data)   # raises on something that is not an Ed25519 key


def _table(name, pubs):
    return (f'inline constexpr std::array<SigningKey, {len(pubs)}> {name}{{{{\n' +
            ''.join(f'    {cpp_entry(k, pubs[k])},\n' for k in sorted(pubs)) + '}};')


def embed_keys(pubs, test_pubs, header=KEYS_HEADER):
    """Writes the stable keys (K1 and K2 at least) and the test keys (T1 at least) into launcher/SigningKeys.h in place
    of the two empty tables."""
    if len(pubs) < 2:
        raise ValueError('K1 and K2 are both needed')
    if len(test_pubs) < 1:
        raise ValueError('T1 is needed (--test-pub 101=HEX)')
    seen = set()
    _check_pubs(pubs, False, seen)
    _check_pubs(test_pubs, True, seen)
    header = Path(header)
    text = header.read_text(encoding='utf-8')
    if EMPTY_STABLE not in text or EMPTY_TEST not in text:
        raise ValueError(f'{header} has no empty key tables; change an existing table by hand (docs/signing.md §3)')
    text = text.replace(EMPTY_STABLE, _table('StableSigningKeys', pubs)).replace(EMPTY_TEST, _table('TestSigningKeys', test_pubs))
    header.write_text(text, encoding='utf-8', newline='\n')
    return embedded_keys(header)


def embedded_keys(header=KEYS_HEADER):
    """{keyId: public bytes} from launcher/SigningKeys.h."""
    text = Path(header).read_text(encoding='utf-8')
    keys = {}
    for key_id, body in re.findall(r'\{\s*(\d+)\s*,\s*\{([0-9a-fA-Fx,\s]+)\}\s*\}', text):
        data = bytes(int(b, 16) for b in body.replace(' ', '').replace('\n', '').split(',') if b)
        if len(data) == 32:
            keys[int(key_id)] = data
    return keys


def verify_bytes(kind, data, sig, keys):
    """None when good, else the reason (the same checks as launcher/ManifestSignature.h)."""
    _, _, Ed25519PublicKey = _crypto()
    try:
        parsed = json.loads(sig)
        key_id = parsed['keyId']
        raw = base64.b64decode(parsed['signature'], validate=True)
        if parsed['schemaVersion'] != 1 or not isinstance(key_id, int) or len(raw) != 64:
            return 'malformed'
    except (ValueError, KeyError, TypeError):
        return 'malformed'
    if key_id not in keys or (key_id >= FIRST_TEST_KEY) != (kind in TEST_KINDS):
        return f'unknown key {key_id}'
    try:
        Ed25519PublicKey.from_public_bytes(keys[key_id]).verify(raw, message(kind, data))
    except Exception:
        return 'mismatch'
    return None


def summary(kind, data):
    """What is being signed, so the author can see it before typing the passphrase."""
    line = f'{kind}: {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}'
    try:
        doc = json.loads(data)
        if kind.startswith('catalog/'):
            line += f", sequence {doc['sequence']}, " + ', '.join(f"{v['id']} r{v.get('revision', '?')}" for v in doc['versions'])
        elif kind.startswith('launcher/'):
            line += f", channel {doc.get('channel', 'stable')}, build {doc['build']}, exe sha256 {doc['sha256']}"
        elif kind == 'versions/stable':
            line += f", {len(doc.get('releaseVersions', []))} release versions"
    except (ValueError, KeyError, TypeError):
        line += ' (not the expected JSON)'
    return line


def ask_passphrase(twice):
    first = getpass.getpass('passphrase: ')
    if twice and getpass.getpass('again: ') != first:
        sys.exit('the passphrases differ')
    return first


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    gen = sub.add_parser('keygen')
    gen.add_argument('--key-id', type=int)
    gen.add_argument('--out', type=Path)
    gen.add_argument('--test', action='store_true', help='a test key for the Worker (no file, no passphrase)')
    chk = sub.add_parser('check')
    chk.add_argument('--key', type=Path, required=True)
    sig = sub.add_parser('sign')
    sig.add_argument('--key', type=Path, required=True)
    sig.add_argument('--kind', choices=KINDS, required=True)
    sig.add_argument('list', type=Path)
    emb = sub.add_parser('embed', help='put the public keys (not secret) into launcher/SigningKeys.h')
    emb.add_argument('--pub', action='append', required=True, metavar='ID=HEX', help='a stable key (K1, K2)')
    emb.add_argument('--test-pub', action='append', required=True, metavar='ID=HEX', help='a test key (T1)')
    emb.add_argument('--keys', type=Path, default=KEYS_HEADER)
    ver = sub.add_parser('verify')
    ver.add_argument('--kind', choices=KINDS, required=True)
    ver.add_argument('--keys', type=Path, default=KEYS_HEADER)
    ver.add_argument('list', type=Path)
    args = parser.parse_args(argv)

    if args.command == 'keygen' and args.test:
        key_id = args.key_id or FIRST_TEST_KEY
        seed, pub = keygen_test(key_id)
        print(f'test key {key_id}. Its secret is printed once here and kept nowhere else:')
        print(f'  {key_id}:{seed}')
        print('Put it into the Worker now (paste when asked, then clear this window):')
        print('  npx wrangler secret put TEST_MANIFEST_KEY')
        print(f'public key (not secret), for embed --test-pub:\n  {key_id}={pub}')
        print(f'fingerprint {fingerprint(pub)} (write it on the paper backup)')
    elif args.command == 'keygen':
        if not args.key_id or not args.out:
            parser.error('keygen needs --key-id and --out (or --test)')
        if args.key_id >= FIRST_TEST_KEY:
            parser.error(f'stable key ids are 1..100; a test key is made with --test')
        pub = keygen(args.key_id, args.out, ask_passphrase(True))
        print(f'key {args.key_id} written to {args.out} (keep it offline; never in Git, OneDrive or a chat)')
        print(f'public key (not secret), for launcher/SigningKeys.h:\n  {cpp_entry(args.key_id, pub)},')
        print(f'fingerprint {fingerprint(pub)} (write it on the paper backup)')
    elif args.command == 'embed':
        def parse(items):
            pubs = {}
            for item in items:
                key_id, _, pub = item.partition('=')
                pubs[int(key_id)] = pub.strip()
            return pubs
        keys = embed_keys(parse(args.pub), parse(args.test_pub), args.keys)
        print(f'{args.keys}: keys {sorted(keys)}')
        for key_id in sorted(keys):
            print(f'  key {key_id} fingerprint {fingerprint(keys[key_id])}')
    elif args.command == 'check':
        key_id, key = load_key(args.key, ask_passphrase(False))
        print(f'key {key_id} opens; public half {public_hex(key)}; fingerprint {fingerprint(public_hex(key))}')
    elif args.command == 'sign':
        if args.kind in TEST_KINDS:
            sys.exit(f'{args.kind} is signed by the Worker with the test key; publish the list without a .sig')
        data = args.list.read_bytes()
        print(summary(args.kind, data))
        key_id, key = load_key(args.key, ask_passphrase(False))
        signature = sign_bytes(args.kind, data, key_id, key)
        Path(str(args.list) + '.sig').write_bytes(signature)
        print(f'signed with key {key_id}: {args.list}.sig')
    else:
        data = args.list.read_bytes()
        problem = verify_bytes(args.kind, data, Path(str(args.list) + '.sig').read_bytes(), embedded_keys(args.keys))
        print(summary(args.kind, data))
        if problem:
            sys.exit(f'NOT VALID: {problem}')
        print('valid')


if __name__ == '__main__':
    main()

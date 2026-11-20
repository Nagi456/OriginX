// The Worker signs test/catalog.json with the test key T1 (docs/superpowers/specs/2026-11-20-test-signing-key-design.md).
// Only catalog/test: every other list is signed by a stable key on the author's PC, which never comes here.
// Secret TEST_MANIFEST_KEY = "<keyId>:<base64 of the 32-byte Ed25519 seed>", as tools/signing.py keygen --test prints
// it. The value is never logged. The bytes signed are the launcher's (launcher/ManifestSignature.h):
//   "OriginX-manifest-v1\n" + kind + "\n" + the list's exact bytes
// and the .sig is written in the same form as tools/signing.py writes it.

const FIRST_TEST_KEY = 101;
// PKCS#8 wrapper for a raw Ed25519 seed (RFC 8410).
const PKCS8_PREFIX = new Uint8Array([0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20]);

export interface TestKey { id: number; key: CryptoKey }

function fromBase64(text: string): Uint8Array | null {
  if (!/^[A-Za-z0-9+/]+={0,2}$/.test(text) || text.length % 4) return null;
  try {
    return Uint8Array.from(atob(text), (c) => c.charCodeAt(0));
  } catch {
    return null;
  }
}

function toBase64(bytes: Uint8Array): string {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s);
}

// Null when the secret is missing or malformed (the caller then stores nothing).
export async function loadTestKey(secret: string | undefined): Promise<TestKey | null> {
  if (typeof secret !== "string") return null;
  const match = /^(\d{3,5}):(\S+)$/.exec(secret.trim());
  if (!match) return null;
  const id = Number(match[1]);
  const seed = fromBase64(match[2]);
  if (id < FIRST_TEST_KEY || id > 0xffff || !seed || seed.length !== 32) return null;
  const pkcs8 = new Uint8Array(PKCS8_PREFIX.length + 32);
  pkcs8.set(PKCS8_PREFIX);
  pkcs8.set(seed, PKCS8_PREFIX.length);
  try {
    const key = await crypto.subtle.importKey("pkcs8", pkcs8, { name: "Ed25519" }, false, ["sign"]);
    return { id, key };
  } catch {
    return null;
  }
}

export function signedMessage(kind: string, bytes: ArrayBuffer): Uint8Array<ArrayBuffer> {
  const head = new TextEncoder().encode(`OriginX-manifest-v1\n${kind}\n`);
  const out = new Uint8Array(head.length + bytes.byteLength);
  out.set(head);
  out.set(new Uint8Array(bytes), head.length);
  return out;
}

// The .sig file for catalog/test, byte for byte what tools/signing.py sign_bytes writes.
export async function signTestCatalog(key: TestKey, bytes: ArrayBuffer): Promise<string> {
  const signature = new Uint8Array(await crypto.subtle.sign({ name: "Ed25519" }, key.key, signedMessage("catalog/test", bytes)));
  return `{"schemaVersion": 1, "keyId": ${key.id}, "signature": "${toBase64(signature)}"}\n`;
}

// The shape a channel list must have before the Worker puts its name to it.
export function testCatalogShape(bytes: ArrayBuffer): boolean {
  let doc: unknown;
  try {
    doc = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
  } catch {
    return false;
  }
  if (!doc || typeof doc !== "object" || Array.isArray(doc)) return false;
  const list = doc as Record<string, unknown>;
  return list.schemaVersion === 1 && Number.isSafeInteger(list.sequence) && (list.sequence as number) > 0 && Array.isArray(list.versions);
}

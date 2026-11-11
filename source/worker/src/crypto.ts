// base64url, HMAC-SHA256 and Discord's Ed25519 request signatures, all with
// WebCrypto so the Worker needs no packages.

const encoder = new TextEncoder();

export function b64url(bytes: Uint8Array): string {
  let text = "";
  for (const b of bytes) text += String.fromCharCode(b);
  return btoa(text).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export function fromB64url(s: string): Uint8Array {
  const text = atob(s.replace(/-/g, "+").replace(/_/g, "/"));
  return Uint8Array.from(text, (c) => c.charCodeAt(0));
}

function hmacKey(secret: string): Promise<CryptoKey> {
  return crypto.subtle.importKey("raw", encoder.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
}

export async function hmac(secret: string, data: string): Promise<string> {
  const sig = await crypto.subtle.sign("HMAC", await hmacKey(secret), encoder.encode(data));
  return b64url(new Uint8Array(sig));
}

export async function hmacEqual(secret: string, data: string, sig: string): Promise<boolean> {
  const want = await hmac(secret, data);
  if (sig.length !== want.length) return false;
  let diff = 0;
  for (let i = 0; i < want.length; i++) diff |= want.charCodeAt(i) ^ sig.charCodeAt(i);
  return diff === 0;
}

function fromHex(text: string): Uint8Array<ArrayBuffer> | null {
  if (text.length % 2 !== 0 || !/^[0-9a-fA-F]*$/.test(text)) return null;
  const out = new Uint8Array(text.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(text.slice(i * 2, i * 2 + 2), 16);
  return out;
}

// Discord signs timestamp + body. Requests more than five minutes away from
// now are refused so an old request cannot be replayed.
export async function verifyDiscord(
  publicKeyHex: string,
  signatureHex: string | null,
  timestamp: string | null,
  body: string,
  now: number,
): Promise<boolean> {
  if (!signatureHex || !timestamp) return false;
  const seconds = Number(timestamp);
  if (!Number.isFinite(seconds) || Math.abs(now / 1000 - seconds) > 300) return false;
  const key = fromHex(publicKeyHex);
  const sig = fromHex(signatureHex);
  if (!key || !sig) return false;
  try {
    const publicKey = await crypto.subtle.importKey("raw", key, { name: "Ed25519" }, false, ["verify"]);
    return await crypto.subtle.verify("Ed25519", publicKey, sig, encoder.encode(timestamp + body));
  } catch {
    return false;
  }
}

// The OriginX token the launcher keeps after logging in with Discord:
// v1.<base64url(JSON claims)>.<base64url(HMAC-SHA256)>. Nothing is stored on
// the server; changing SIGNING_KEY logs everyone out.

import { b64url, fromB64url, hmac, hmacEqual } from "./crypto";

export interface Claims {
  sub: string;
  iat: number;
  exp: number;
}

const LIFETIME = 90 * 86400;
const RENEW_UNDER = 30 * 86400;

export async function makeToken(secret: string, sub: string, now: number): Promise<string> {
  const iat = Math.floor(now / 1000);
  const payload = b64url(new TextEncoder().encode(JSON.stringify({ sub, iat, exp: iat + LIFETIME })));
  return `v1.${payload}.${await hmac(secret, "v1." + payload)}`;
}

export async function readToken(secret: string, token: string, now: number): Promise<Claims | null> {
  const parts = token.split(".");
  if (parts.length !== 3 || parts[0] !== "v1") return null;
  const [, payload, sig] = parts;
  if (!(await hmacEqual(secret, "v1." + payload, sig))) return null;
  try {
    const claims = JSON.parse(new TextDecoder().decode(fromB64url(payload)));
    if (typeof claims.sub !== "string" || typeof claims.iat !== "number" || typeof claims.exp !== "number") return null;
    if (claims.exp <= now / 1000) return null;
    return { sub: claims.sub, iat: claims.iat, exp: claims.exp };
  } catch {
    return null;
  }
}

export function needsRenewal(claims: Claims, now: number): boolean {
  return claims.exp - now / 1000 < RENEW_UNDER;
}

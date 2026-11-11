// GitHub Actions OIDC: a workflow of Nagi456/MyClient asks GitHub for a signed
// token (permissions: id-token: write) and sends it as the bearer. The Worker
// checks GitHub's RS256 signature against GitHub's published keys and that the
// token was made for this audience, by this repository (its numeric id, which
// survives renames), and is not expired. No shared secret is needed.

import { fromB64url } from "./crypto";

export const OIDC_ISSUER = "https://token.actions.githubusercontent.com";
export const OIDC_AUDIENCE = "originx-publish";
export const REPOSITORY_ID = "1403791744"; // Nagi456/MyClient
const JWKS_URL = OIDC_ISSUER + "/.well-known/jwks";

type Fetch = (input: string) => Promise<Response>;
interface Jwk { kid?: string; kty?: string; n?: string; e?: string }

let cached: { at: number; keys: Jwk[] } | null = null;
export function forgetKeys() {
  cached = null;
}

async function keys(fetchFn: Fetch, now: number, refresh: boolean): Promise<Jwk[]> {
  if (!refresh && cached && now - cached.at < 3600_000) return cached.keys;
  const res = await fetchFn(JWKS_URL);
  if (!res.ok) throw new Error(`jwks ${res.status}`);
  const body = (await res.json()) as { keys?: Jwk[] };
  cached = { at: now, keys: Array.isArray(body.keys) ? body.keys : [] };
  return cached.keys;
}

function part(text: string): Record<string, unknown> | null {
  try {
    const value = JSON.parse(new TextDecoder().decode(fromB64url(text)));
    return value && typeof value === "object" ? value : null;
  } catch {
    return null;
  }
}

// The claims of a good token, or null.
export async function verifyGithubOidc(token: string, now: number, fetchFn: Fetch = fetch): Promise<Record<string, unknown> | null> {
  const pieces = token.split(".");
  if (pieces.length !== 3) return null;
  const header = part(pieces[0]);
  const claims = part(pieces[1]);
  if (!header || !claims || header.alg !== "RS256" || typeof header.kid !== "string") return null;

  let jwk = (await keys(fetchFn, now, false)).find((k) => k.kid === header.kid);
  if (!jwk) jwk = (await keys(fetchFn, now, true)).find((k) => k.kid === header.kid); // GitHub rotated its keys
  if (!jwk || jwk.kty !== "RSA" || !jwk.n || !jwk.e) return null;
  const key = await crypto.subtle.importKey("jwk", { kty: "RSA", n: jwk.n, e: jwk.e, alg: "RS256", ext: true },
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["verify"]);
  let signature: Uint8Array<ArrayBuffer>;
  try {
    signature = new Uint8Array(fromB64url(pieces[2]));
  } catch {
    return null;
  }
  const signed = new TextEncoder().encode(pieces[0] + "." + pieces[1]);
  if (!(await crypto.subtle.verify("RSASSA-PKCS1-v1_5", key, signature, signed))) return null;

  const seconds = now / 1000;
  const aud = claims.aud;
  const audienceOk = aud === OIDC_AUDIENCE || (Array.isArray(aud) && aud.includes(OIDC_AUDIENCE));
  if (claims.iss !== OIDC_ISSUER || !audienceOk || claims.repository_id !== REPOSITORY_ID) return null;
  // Only the build workflows publish (build-dll, build-launcher) and the promote workflow moves a verified
  // test build to early/ (promote-build). What each may do is decided where it is used (publish.ts, promote.ts).
  const workflow = typeof claims.workflow_ref === "string" ? claims.workflow_ref.toLowerCase() : "";
  const prefix = "nagi456/myclient/.github/workflows/";
  if (!workflow.startsWith(prefix + "build-") && !workflow.startsWith(prefix + "promote-")) return null;
  if (typeof claims.exp !== "number" || claims.exp < seconds) return null;
  if (typeof claims.nbf === "number" && claims.nbf > seconds + 60) return null;
  return claims;
}

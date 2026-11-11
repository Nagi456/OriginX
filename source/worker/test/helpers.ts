import { b64url } from "../src/crypto";
import type { Env } from "../src/env";
import { OIDC_AUDIENCE, OIDC_ISSUER, REPOSITORY_ID } from "../src/github-oidc";

export const NOW = Date.UTC(2026, 9, 29, 12, 0, 0);

export function hex(bytes: ArrayBuffer | Uint8Array): string {
  return Array.from(new Uint8Array(bytes), (b) => b.toString(16).padStart(2, "0")).join("");
}

export async function discordKeys() {
  const pair = (await crypto.subtle.generateKey({ name: "Ed25519" }, true, ["sign", "verify"])) as CryptoKeyPair;
  const publicKey = hex(await crypto.subtle.exportKey("raw", pair.publicKey));
  async function sign(timestamp: string, body: string): Promise<string> {
    const sig = await crypto.subtle.sign("Ed25519", pair.privateKey, new TextEncoder().encode(timestamp + body));
    return hex(sig);
  }
  return { publicKey, sign };
}

export function memoryBucket(files: Record<string, string> = {}) {
  const gets: string[] = [];
  const puts: Record<string, Uint8Array> = {};
  return {
    gets,
    puts,
    async put(key: string, value: ArrayBuffer) {
      puts[key] = new Uint8Array(value);
      files[key] = new TextDecoder().decode(value);
    },
    async head(key: string) {
      if (!(key in files)) return null;
      return { size: new TextEncoder().encode(files[key]).length };
    },
    async get(key: string, options?: { range?: { offset: number; length: number } }) {
      gets.push(key);
      if (!(key in files)) return null;
      const bytes = new TextEncoder().encode(files[key]);
      const part = options?.range ? bytes.slice(options.range.offset, options.range.offset + options.range.length) : bytes;
      return {
        size: bytes.length,
        body: new Response(part).body as ReadableStream,
        async text() { return files[key]; },
      };
    },
  };
}

export function testEnv(overrides: Partial<Env> = {}): Env {
  return {
    GUILD_ID: "100",
    MEMBER_ROLE_ID: "200",
    SUPPORTER_ROLE_ID: "300",
    MODERATOR_ROLE_ID: "500",
    CLIENT_ID: "400",
    PUBLIC_KEY: "00".repeat(32),
    SITE_URL: "https://site.example",
    BOT_TOKEN: "test-token",
    CLIENT_SECRET: "client-secret",
    SIGNING_KEY: "signing-key-for-tests-0123456789abcdef",
    FILES: memoryBucket(),
    ASSETS: { fetch: async () => new Response("site") },
    ...overrides,
  };
}

// A fake fetch that answers from a list of handlers in order and records calls.
export interface Call { url: string; method: string; headers: Headers; body: string }

export function fakeFetch(...answers: Array<(call: Call) => Response | Promise<Response>>) {
  const calls: Call[] = [];
  const fn = async (input: RequestInfo | URL, init?: RequestInit) => {
    const req = new Request(input, init);
    const call = { url: req.url, method: req.method, headers: req.headers, body: await req.text() };
    calls.push(call);
    const answer = answers[Math.min(calls.length - 1, answers.length - 1)];
    return answer(call);
  };
  return { fn, calls };
}

export function json(value: unknown, status = 200, headers: Record<string, string> = {}) {
  return new Response(JSON.stringify(value), { status, headers: { "content-type": "application/json", ...headers } });
}

// A GitHub Actions OIDC token signer and the JWKS fetch that serves its public key.
const enc = (v: unknown) => b64url(new TextEncoder().encode(JSON.stringify(v)));

export async function oidcSigner(kid = "k1") {
  const pair = (await crypto.subtle.generateKey(
    { name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" },
    true, ["sign", "verify"])) as CryptoKeyPair;
  const jwk = { ...(await crypto.subtle.exportKey("jwk", pair.publicKey)), kid };
  const fetches: string[] = [];
  const fetchFn = async (url: string) => {
    fetches.push(url);
    return Response.json({ keys: [jwk] });
  };
  async function token(claims: Record<string, unknown> = {}, header: Record<string, unknown> = {}) {
    const body = {
      iss: OIDC_ISSUER, aud: OIDC_AUDIENCE, repository_id: REPOSITORY_ID, repository: "Nagi456/MyClient",
      workflow_ref: "Nagi456/MyClient/.github/workflows/build-dll.yml@refs/heads/x", run_id: "7",
      exp: NOW / 1000 + 300, nbf: NOW / 1000 - 10, ...claims,
    };
    const head = enc({ alg: "RS256", typ: "JWT", kid, ...header });
    const data = head + "." + enc(body);
    const sig = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", pair.privateKey, new TextEncoder().encode(data));
    return data + "." + b64url(new Uint8Array(sig));
  }
  return { token, fetchFn, fetches };
}


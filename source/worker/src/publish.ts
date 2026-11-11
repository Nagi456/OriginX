// Release files in R2.
//   GET /dl/stable/<path>: anyone may download what is published under stable/
//     (the launcher's update list, the DLL package and the HUD). The source is
//     public under GPLv3, so nothing here needs hiding; the launcher checks
//     every file against the SHA256 in the list anyway.
//   GET /dl/early|test/...: never; those channels go out only as signed /file links after a role check (status.ts).
//   PUT /publish/<key>: a workflow of Nagi456/MyClient uploads a file. The bearer
//     is its GitHub Actions OIDC token (github-oidc.ts; nothing to set up), or
//     the optional dashboard secret PUBLISH_KEY. "x-sha256: <hex>" of the body
//     is required and checked before storing.

import type { Env } from "./env";
import { hmac, hmacEqual } from "./crypto";
import { verifyGithubOidc } from "./github-oidc";
import { serveObject } from "./serve";

const SAFE_KEY = /^[A-Za-z0-9._-]+(\/[A-Za-z0-9._-]+)*$/;
const PUBLISH_PREFIXES = ["stable/", "early/", "test/", "launcher/"];
// The lists a workflow may read back to merge a new entry into them (GET /publish/<key>).
const READABLE_KEYS = ["test/catalog.json", "early/catalog.json"];
export const MAX_UPLOAD_BYTES = 64 * 1024 * 1024;

function text(body: string, status: number): Response {
  return new Response(body, { status, headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" } });
}

function goodKey(key: string): boolean {
  return key.length <= 512 && SAFE_KEY.test(key) && !key.split("/").some((part) => part === "." || part === "..");
}

export async function handleDownload(request: Request, env: Env): Promise<Response> {
  const key = new URL(request.url).pathname.replace(/^\/dl\//, "");
  if (!key.startsWith("stable/") || !goodKey(key)) return text("Not found", 404);
  // The update lists change in place; everything else sits under a new tag (or build) per release.
  const list = key.endsWith(".json") && key.split("/").length === 2;
  return serveObject(request, env, key, {
    "content-type": key.endsWith(".json") ? "application/json" : "application/octet-stream",
    "cache-control": list ? "no-cache" : "public, max-age=31536000, immutable",
  });
}

function hex(bytes: ArrayBuffer): string {
  return Array.from(new Uint8Array(bytes), (b) => b.toString(16).padStart(2, "0")).join("");
}

async function keyMatches(secret: string, given: string): Promise<boolean> {
  return hmacEqual(secret, "originx-publish", await hmac(given, "originx-publish"));
}

export function isPromoteWorkflow(who: string): boolean {
  return who.toLowerCase().includes("/.github/workflows/promote-");
}

export async function allowed(env: Env, bearer: string, now: number, fetchFn?: (url: string) => Promise<Response>): Promise<string | null> {
  if (bearer.split(".").length === 3) {
    const claims = await verifyGithubOidc(bearer, now, fetchFn);
    return claims ? `github ${String(claims.workflow_ref ?? claims.workflow ?? "?")} run ${String(claims.run_id ?? "?")}` : null;
  }
  const secret = env.PUBLISH_KEY;
  if (typeof secret === "string" && secret.length >= 32 && (await keyMatches(secret, bearer))) return "PUBLISH_KEY";
  return null;
}

export async function handlePublish(request: Request, env: Env, now = Date.now(), fetchFn?: (url: string) => Promise<Response>): Promise<Response> {
  const auth = request.headers.get("authorization") ?? "";
  const who = auth.startsWith("Bearer ") ? await allowed(env, auth.slice(7), now, fetchFn) : null;
  if (!who) return text("Unauthorized", 401);
  // The promote workflow only moves a verified test build (promote.ts); it never uploads files itself.
  if (isPromoteWorkflow(who)) return text("Forbidden", 403);

  const key = new URL(request.url).pathname.replace(/^\/publish\//, "");
  if (!goodKey(key) || !PUBLISH_PREFIXES.some((p) => key.startsWith(p))) return text("Bad key", 400);
  const want = (request.headers.get("x-sha256") ?? "").toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(want)) return text("x-sha256 is required", 400);
  const length = Number(request.headers.get("content-length") ?? "0");
  if (length > MAX_UPLOAD_BYTES) return text("Too large", 413);

  const body = await request.arrayBuffer();
  if (body.byteLength > MAX_UPLOAD_BYTES) return text("Too large", 413);
  const got = hex(await crypto.subtle.digest("SHA-256", body));
  if (got !== want) return text("SHA256 mismatch", 400);
  // A file of a test/early build never changes once it is stored (the same bytes again are fine, the lists are
  // replaced on purpose): promote copies exactly what was verified.
  if ((key.startsWith("test/") || key.startsWith("early/")) && !key.endsWith("/catalog.json")) {
    const existing = await env.FILES.get(key);
    if (existing && hex(await crypto.subtle.digest("SHA-256", await new Response(existing.body).arrayBuffer())) !== got) {
      return text("Already exists with other bytes", 409);
    }
  }
  await env.FILES.put(key, body);
  console.log("publish:", key, body.byteLength, got, "by", who);
  return Response.json({ key, bytes: body.byteLength, sha256: got }, { status: 201 });
}

// GET /publish/<key>: the same callers as PUT read back the last list of a channel (nothing else), so a new
// release can be merged into it instead of replacing the other game versions.
export async function handlePublishRead(request: Request, env: Env, now = Date.now(), fetchFn?: (url: string) => Promise<Response>): Promise<Response> {
  const auth = request.headers.get("authorization") ?? "";
  const who = auth.startsWith("Bearer ") ? await allowed(env, auth.slice(7), now, fetchFn) : null;
  if (!who) return text("Unauthorized", 401);
  const key = new URL(request.url).pathname.replace(/^\/publish\//, "");
  if (!goodKey(key) || !READABLE_KEYS.includes(key)) return text("Bad key", 400);
  const object = await env.FILES.get(key);
  if (!object) return text("Not found", 404);
  return new Response(await object.text(), { status: 200, headers: { "content-type": "application/json", "cache-control": "no-store" } });
}

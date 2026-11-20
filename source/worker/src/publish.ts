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
//     test/catalog.json is signed here with the test key (manifest-sign.ts): its .sig is written by the Worker
//     only, never uploaded.

import type { Env } from "./env";
import { hmac, hmacEqual } from "./crypto";
import { verifyGithubOidc } from "./github-oidc";
import { serveObject } from "./serve";
import { loadTestKey, signTestCatalog, testCatalogShape } from "./manifest-sign";

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
  // A list's signature (<list>.json.sig) changes with it.
  const list = (key.endsWith(".json") || key.endsWith(".json.sig")) && key.split("/").length === 2;
  return serveObject(request, env, key, {
    "content-type": key.endsWith(".json") || key.endsWith(".json.sig") ? "application/json" : "application/octet-stream",
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

// Who is calling, and from which git ref (null = the PUBLISH_KEY, which is not tied to a branch).
export interface Caller { who: string; ref: string | null }

// The git ref a workflow run was started on. In a reusable workflow, the token's ref and workflow_ref are the caller's.
function refOf(claims: Record<string, unknown>): string {
  if (typeof claims.ref === "string" && claims.ref) return claims.ref;
  const workflow = typeof claims.workflow_ref === "string" ? claims.workflow_ref : "";
  const at = workflow.indexOf("@");
  return at >= 0 ? workflow.slice(at + 1) : "";
}

export async function caller(env: Env, bearer: string, now: number, fetchFn?: (url: string) => Promise<Response>): Promise<Caller | null> {
  if (bearer.split(".").length === 3) {
    const claims = await verifyGithubOidc(bearer, now, fetchFn);
    if (!claims) return null;
    const ref = refOf(claims);
    return { who: `github ${String(claims.workflow_ref ?? claims.workflow ?? "?")} run ${String(claims.run_id ?? "?")} ref ${ref || "?"}`, ref };
  }
  const secret = env.PUBLISH_KEY;
  if (typeof secret === "string" && secret.length >= 32 && (await keyMatches(secret, bearer))) return { who: "PUBLISH_KEY", ref: null };
  return null;
}

export async function allowed(env: Env, bearer: string, now: number, fetchFn?: (url: string) => Promise<Response>): Promise<string | null> {
  return (await caller(env, bearer, now, fetchFn))?.who ?? null;
}

// Releases everyone gets (stable/, early/, the site's launcher) come only from the release branches (RELEASE_REFS,
// default main), so a workflow run on a work or pull-request branch can publish to test/ but never to the public.
export function releaseRefs(env: Env): string[] {
  const list = (env.RELEASE_REFS ?? "refs/heads/main").split(",").map((r) => r.trim()).filter(Boolean);
  return list.length ? list : ["refs/heads/main"];
}

export function mayRelease(env: Env, from: Caller): boolean {
  return from.ref === null || releaseRefs(env).includes(from.ref);
}

export async function handlePublish(request: Request, env: Env, now = Date.now(), fetchFn?: (url: string) => Promise<Response>): Promise<Response> {
  const auth = request.headers.get("authorization") ?? "";
  const from = auth.startsWith("Bearer ") ? await caller(env, auth.slice(7), now, fetchFn) : null;
  if (!from) return text("Unauthorized", 401);
  const who = from.who;
  // The promote workflow only moves a verified test build (promote.ts); it never uploads files itself.
  if (isPromoteWorkflow(who)) return text("Forbidden", 403);

  const key = new URL(request.url).pathname.replace(/^\/publish\//, "");
  if (!goodKey(key) || !PUBLISH_PREFIXES.some((p) => key.startsWith(p))) return text("Bad key", 400);
  if (!key.startsWith("test/") && !mayRelease(env, from)) {
    return text(`Forbidden: only ${releaseRefs(env).join(", ")} may publish outside test/ (this run: ${from.ref || "?"})`, 403);
  }
  // The test list's signature is the Worker's own (TEST_MANIFEST_KEY); one from outside is never stored.
  if (key === "test/catalog.json.sig") return text("Forbidden: the Worker signs test/catalog.json itself", 403);
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
  const list = /\/(catalog|launcher)\.json(\.sig)?$/.test(key) && key.split("/").length === 2;   // test/launcher.json too
  if ((key.startsWith("test/") || key.startsWith("early/")) && !list) {
    const existing = await env.FILES.get(key);
    if (existing && hex(await crypto.subtle.digest("SHA-256", await new Response(existing.body).arrayBuffer())) !== got) {
      return text("Already exists with other bytes", 409);
    }
  }
  if (key === "test/catalog.json") {
    if (!testCatalogShape(body)) return text("Not a channel list (schemaVersion 1, sequence, versions)", 400);
    const testKey = await loadTestKey(env.TEST_MANIFEST_KEY);
    if (!testKey) {
      console.log("publish: test/catalog.json refused: TEST_MANIFEST_KEY is missing or malformed");
      return text("The test signing key is not set up; nothing was stored", 500);
    }
    // The signature first: a list is never live without its own .sig (the launcher would refuse it).
    await env.FILES.put("test/catalog.json.sig", new TextEncoder().encode(await signTestCatalog(testKey, body)).buffer as ArrayBuffer);
    console.log("publish: test/catalog.json.sig signed with test key", testKey.id);
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

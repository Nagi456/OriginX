// POST /publish/promote {"tag": "myclient-26.51.1-r54-test1"}: moves a verified test build to the early channel
// by copying the very same bytes inside R2 (test/<tag>/ -> early/<tag>/), then writes early/catalog.json LAST, so
// the list never names a file that is not there yet. Only the promote- workflow (or PUBLISH_KEY) may call it.
//
// A tag's version entry sits in test/<tag>/entry.json (package_update.py --channel test writes it): test/catalog.json
// lists only the latest build of each game version, and promoting an older tag again is how the early channel is
// put back to it.

import type { Env } from "./env";
import { allowed, isPromoteWorkflow } from "./publish";

const TAG = /^myclient-[0-9]{1,2}\.[0-9]{1,2}\.[0-9]{1,2}-r[0-9]+-test[0-9]+$/;

type FileEntry = { url: string; sha256: string; bytes: number };
type VersionEntry = { id: string; files: Record<string, FileEntry>; [key: string]: unknown };
type Catalog = { schemaVersion: number; sequence: number; versions: VersionEntry[] };

export type PromoteResult =
  | { ok: true; tag: string; files: number; sequence: number }
  | { ok: false; status: 400 | 404 | 409 | 500; message: string };

function fail(status: 400 | 404 | 409 | 500, message: string): PromoteResult {
  return { ok: false, status, message };
}

function hex(bytes: ArrayBuffer): string {
  return Array.from(new Uint8Array(bytes), (b) => b.toString(16).padStart(2, "0")).join("");
}

async function readBytes(env: Env, key: string): Promise<ArrayBuffer | null> {
  const object = await env.FILES.get(key);
  return object ? await new Response(object.body).arrayBuffer() : null;
}

async function sha256(bytes: ArrayBuffer): Promise<string> {
  return hex(await crypto.subtle.digest("SHA-256", bytes));
}

function isEntry(value: unknown): value is VersionEntry {
  const v = value as VersionEntry | null;
  return !!v && typeof v === "object" && typeof v.id === "string" && !!v.files && typeof v.files === "object";
}

export async function promoteTag(env: Env, tag: string): Promise<PromoteResult> {
  if (!TAG.test(tag)) return fail(400, "bad tag");
  const entryBytes = await readBytes(env, `test/${tag}/entry.json`);
  if (!entryBytes) return fail(404, `test/${tag}/entry.json is not there`);
  let entry: VersionEntry;
  try {
    const parsed: unknown = JSON.parse(new TextDecoder().decode(entryBytes));
    if (!isEntry(parsed)) return fail(500, "entry.json is not a version entry");
    entry = parsed;
  } catch {
    return fail(500, "entry.json is not valid JSON");
  }

  // 1. Check every source file first: nothing is written unless all of them are there and match their SHA256.
  const sources: Array<{ name: string; from: string; to: string; bytes: ArrayBuffer; sha256: string }> = [];
  for (const [name, file] of Object.entries(entry.files)) {
    const from = file?.url;
    if (typeof from !== "string" || !from.startsWith(`test/${tag}/`) || from.split("/").length !== 3 || from.includes("..")) {
      return fail(500, `${name}: the entry names a file outside test/${tag}/`);
    }
    const bytes = await readBytes(env, from);
    if (!bytes) return fail(404, `${from} is not there`);
    const digest = await sha256(bytes);
    if (digest !== file.sha256 || bytes.byteLength !== file.bytes) return fail(409, `${from} does not match the SHA256 in its entry`);
    sources.push({ name, from, to: `early/${tag}/${from.split("/")[2]}`, bytes, sha256: digest });
  }

  // 2. Copy. An early/ file that already exists must hold the same bytes (then it is skipped), never other bytes.
  for (const source of sources) {
    const existing = await readBytes(env, source.to);
    if (existing) {
      if ((await sha256(existing)) !== source.sha256) return fail(409, `${source.to} already exists with other bytes`);
      continue;
    }
    await env.FILES.put(source.to, source.bytes);
    const stored = env.FILES.head ? await env.FILES.head(source.to) : null;
    if (env.FILES.head && (!stored || stored.size !== source.bytes.byteLength)) return fail(500, `${source.to} was not stored completely`);
  }
  await env.FILES.put(`early/${tag}/entry.json`, new TextEncoder().encode(JSON.stringify(entry)).buffer as ArrayBuffer);

  // 3. The early list, last: this game version's entry replaced, the others kept, sequence raised by one.
  let previous: Catalog = { schemaVersion: 1, sequence: 0, versions: [] };
  const listBytes = await readBytes(env, "early/catalog.json");
  if (listBytes) {
    try {
      const parsed = JSON.parse(new TextDecoder().decode(listBytes)) as Catalog;
      if (!Array.isArray(parsed.versions) || typeof parsed.sequence !== "number") return fail(500, "early/catalog.json is broken");
      previous = parsed;
    } catch {
      return fail(500, "early/catalog.json is broken");
    }
  }
  const promoted: VersionEntry = {
    ...entry,
    files: Object.fromEntries(sources.map((s) => [s.name, { ...entry.files[s.name], url: s.to }])),
  };
  const index = previous.versions.findIndex((v) => v.id === entry.id);
  const versions = [...previous.versions];
  if (index >= 0) versions[index] = promoted;
  else versions.push(promoted);
  const sequence = previous.sequence + 1;
  const list: Catalog = { schemaVersion: previous.schemaVersion ?? 1, sequence, versions };
  await env.FILES.put("early/catalog.json", new TextEncoder().encode(JSON.stringify(list, null, 2) + "\n").buffer as ArrayBuffer);
  console.log("promote:", tag, sources.length, "files, early sequence", sequence);
  return { ok: true, tag, files: sources.length, sequence };
}

export async function handlePromote(request: Request, env: Env, now = Date.now(), fetchFn?: (url: string) => Promise<Response>): Promise<Response> {
  const auth = request.headers.get("authorization") ?? "";
  const who = auth.startsWith("Bearer ") ? await allowed(env, auth.slice(7), now, fetchFn) : null;
  const text = (body: string, status: number) =>
    new Response(body, { status, headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" } });
  if (!who) return text("Unauthorized", 401);
  // Only the promote workflow (or the administrator's PUBLISH_KEY) may move a build; a build- workflow may not.
  if (!isPromoteWorkflow(who) && who !== "PUBLISH_KEY") return text("Forbidden", 403);

  let tag = "";
  try {
    const body = (await request.json()) as { tag?: unknown };
    if (typeof body.tag === "string") tag = body.tag;
  } catch {
    return text("A JSON body with a tag is required", 400);
  }
  if (!tag) return text("A JSON body with a tag is required", 400);

  const result = await promoteTag(env, tag);
  if (!result.ok) return text(result.message, result.status);
  return Response.json({ tag: result.tag, files: result.files, sequence: result.sequence }, { headers: { "cache-control": "no-store" } });
}

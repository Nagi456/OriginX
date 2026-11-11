import { beforeEach, describe, expect, it } from "vitest";
import { forgetKeys } from "../src/github-oidc";
import worker from "../src/index";
import { handlePromote, promoteTag } from "../src/promote";
import { NOW, hex, memoryBucket, oidcSigner, testEnv } from "./helpers";

beforeEach(() => forgetKeys());

const TAG = "myclient-26.51.1-r54-test1";
const TAG2 = "myclient-26.60.5-r54-test1";
const KEY = "publish-key-for-tests-0123456789abcdef";

async function sha(text: string) {
  return hex(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)));
}

// A test build as package_update.py --channel test writes it: files and an entry.json (the version's entry) under
// test/<tag>/. test/catalog.json only lists the latest build of each game version, so promote reads entry.json:
// that is what lets an older tag be promoted again to put the early channel back to it.
async function build(tag: string, id: string, contents: Record<string, string> = { "runtime.lock.json": `lock ${tag}`, "mods/MyClientData/MyClientData.dll": `dll ${tag}` }) {
  const files: Record<string, { url: string; sha256: string; bytes: number }> = {};
  const objects: Record<string, string> = {};
  for (const [name, text] of Object.entries(contents)) {
    const key = `test/${tag}/${name.replaceAll("/", "__")}`;
    objects[key] = text;
    files[name] = { url: key, sha256: await sha(text), bytes: new TextEncoder().encode(text).length };
  }
  const entry = { id, available: true, loader: "26.51.6", revision: 54, launcherApi: 1, bridgeAbi: 2, platform: "gdk-release-x64", files };
  objects[`test/${tag}/entry.json`] = JSON.stringify(entry);
  return { objects, entry };
}

async function world(...builds: Array<Awaited<ReturnType<typeof build>>>) {
  const files: Record<string, string> = {};
  for (const b of builds) Object.assign(files, b.objects);
  files["test/catalog.json"] = JSON.stringify({ schemaVersion: 1, sequence: 3, versions: builds.map((b) => b.entry) });
  const bucket = memoryBucket(files);
  return { env: testEnv({ PUBLISH_KEY: KEY, FILES: bucket }), bucket, files };
}

const early = (files: Record<string, string>) => JSON.parse(files["early/catalog.json"]);

describe("promoteTag", () => {
  it("copies the same bytes to early/ and writes early/catalog.json last", async () => {
    const b = await build(TAG, "26.51.1");
    const { env, bucket, files } = await world(b);
    const result = await promoteTag(env, TAG);
    expect(result).toEqual({ ok: true, tag: TAG, files: 2, sequence: 1 });
    expect(files[`early/${TAG}/runtime.lock.json`]).toBe(`lock ${TAG}`);
    expect(files[`early/${TAG}/mods__MyClientData__MyClientData.dll`]).toBe(`dll ${TAG}`);
    const keys = Object.keys(bucket.puts);
    expect(keys[keys.length - 1]).toBe("early/catalog.json");
    expect(keys.slice(0, -1).every((k) => k.startsWith(`early/${TAG}/`))).toBe(true);
    const entry = early(files).versions[0];
    expect(early(files).sequence).toBe(1);
    expect(entry.id).toBe("26.51.1");
    expect(entry.files["runtime.lock.json"]).toEqual({ url: `early/${TAG}/runtime.lock.json`, sha256: await sha(`lock ${TAG}`), bytes: new TextEncoder().encode(`lock ${TAG}`).length });
    expect(entry.revision).toBe(54);
  });

  it("keeps both game versions, replaces only the one promoted, and a later promote raises the sequence", async () => {
    const a = await build(TAG, "26.51.1"), c = await build(TAG2, "26.60.5");
    const { env, files } = await world(a, c);
    await promoteTag(env, TAG);
    expect(await promoteTag(env, TAG2)).toMatchObject({ ok: true, sequence: 2 });
    expect(early(files).versions.map((v: { id: string }) => v.id)).toEqual(["26.51.1", "26.60.5"]);
    // an earlier tag for the same game version puts the early channel back to it
    // (the test list does not name it any more: only entry.json under its tag does)
    const older = await build("myclient-26.51.1-r53-test2", "26.51.1");
    Object.assign(files, older.objects);
    expect(await promoteTag(env, "myclient-26.51.1-r53-test2")).toMatchObject({ ok: true, sequence: 3 });
    const now = early(files).versions;
    expect(now.map((v: { id: string }) => v.id).sort()).toEqual(["26.51.1", "26.60.5"]);
    expect(now.find((v: { id: string }) => v.id === "26.51.1").files["runtime.lock.json"].url).toBe("early/myclient-26.51.1-r53-test2/runtime.lock.json");
  });

  it("is repeatable: a second promote of the same tag finds the same bytes and succeeds", async () => {
    const { env } = await world(await build(TAG, "26.51.1"));
    expect(await promoteTag(env, TAG)).toMatchObject({ ok: true, sequence: 1 });
    expect(await promoteTag(env, TAG)).toMatchObject({ ok: true, sequence: 2 });
  });

  it("stops without touching early/ when a file does not match its SHA256, and when one is missing", async () => {
    const b = await build(TAG, "26.51.1");
    const bad = await world(b);
    bad.files[`test/${TAG}/mods__MyClientData__MyClientData.dll`] = "tampered";
    expect(await promoteTag(bad.env, TAG)).toMatchObject({ ok: false, status: 409 });
    expect(Object.keys(bad.bucket.puts)).toEqual([]);

    const missing = await world(await build(TAG, "26.51.1"));
    delete missing.files[`test/${TAG}/runtime.lock.json`];
    expect(await promoteTag(missing.env, TAG)).toMatchObject({ ok: false, status: 404 });
    expect(Object.keys(missing.bucket.puts)).toEqual([]);
  });

  it("refuses to overwrite an early/ file whose bytes differ", async () => {
    const { env, files, bucket } = await world(await build(TAG, "26.51.1"));
    files[`early/${TAG}/runtime.lock.json`] = "something else";
    expect(await promoteTag(env, TAG)).toMatchObject({ ok: false, status: 409 });
    expect(bucket.puts["early/catalog.json"]).toBeUndefined();
  });

  it("is 404 for a tag with no entry.json, 400 for a badly formed tag, 500 for a broken early list", async () => {
    const { env, files } = await world(await build(TAG, "26.51.1"));
    expect(await promoteTag(env, "myclient-26.51.1-r99-test9")).toMatchObject({ ok: false, status: 404 });
    for (const tag of ["myclient-26.51.1-r54", "../x", "myclient-26.51.1-r54-test1/../../stable", "", "myclient-26.51.1-r54-test1 "]) {
      expect(await promoteTag(env, tag), tag).toMatchObject({ ok: false, status: 400 });
    }
    files["early/catalog.json"] = "{";
    expect(await promoteTag(env, TAG)).toMatchObject({ ok: false, status: 500 });
  });
});

describe("POST /publish/promote", () => {
  async function post(env: ReturnType<typeof testEnv>, bearer: string | null, body: unknown = { tag: TAG }, fetchFn?: (url: string) => Promise<Response>) {
    const headers: Record<string, string> = { "content-type": "application/json" };
    if (bearer) headers.authorization = `Bearer ${bearer}`;
    return handlePromote(new Request("https://site.example/publish/promote", { method: "POST", headers, body: typeof body === "string" ? body : JSON.stringify(body) }), env, NOW, fetchFn);
  }

  it("401 without a key; 403 for a build- workflow; 200 for a promote- workflow and for PUBLISH_KEY", async () => {
    const { env, files } = await world(await build(TAG, "26.51.1"));
    const s = await oidcSigner();
    expect((await post(env, null)).status).toBe(401);
    expect((await post(env, await s.token(), { tag: TAG }, s.fetchFn)).status).toBe(403);
    const promote = { workflow_ref: "Nagi456/MyClient/.github/workflows/promote-build.yml@refs/heads/x" };
    const ok = await post(env, await s.token(promote), { tag: TAG }, s.fetchFn);
    expect(ok.status).toBe(200);
    expect(await ok.json()).toEqual({ tag: TAG, files: 2, sequence: 1 });
    expect(files["early/catalog.json"]).toBeDefined();
    expect((await post(env, KEY)).status).toBe(200);
  });

  it("400 for a body that is not JSON with a tag, and passes the failure status of the promote through", async () => {
    const { env } = await world(await build(TAG, "26.51.1"));
    expect((await post(env, KEY, "not json")).status).toBe(400);
    expect((await post(env, KEY, { nope: 1 })).status).toBe(400);
    const missing = await post(env, KEY, { tag: "myclient-26.51.1-r99-test9" });
    expect(missing.status).toBe(404);
    expect(await missing.text()).toContain("entry.json");
  });
});

describe("routing", () => {
  it("POST /publish/promote works through the router before the Discord secrets are set", async () => {
    const { env: base, files } = await world(await build(TAG, "26.51.1"));
    const env = { ...base, BOT_TOKEN: "" };
    const res = await worker.fetch(new Request("https://site.example/publish/promote", {
      method: "POST", headers: { authorization: `Bearer ${KEY}` }, body: JSON.stringify({ tag: TAG }),
    }), env, { waitUntil() {} });
    expect(res.status).toBe(200);
    expect(files["early/catalog.json"]).toBeDefined();
  });
});

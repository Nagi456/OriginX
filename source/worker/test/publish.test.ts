import { describe, expect, it } from "vitest";
import worker from "../src/index";
import { handleDownload, handlePublish, handlePublishRead } from "../src/publish";
import { NOW, hex, memoryBucket, oidcSigner, testEnv } from "./helpers";

const KEY = "publish-key-for-tests-0123456789abcdef";
const TEST_KEY = "101:AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";   // seed 00..1f, test key id 101
const ctx = { waitUntil() {} };

async function sha(text: string) {
  return hex(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)));
}

async function put(path: string, body: string, opts: { key?: string; digest?: string; env?: ReturnType<typeof testEnv> } = {}) {
  const env = opts.env ?? testEnv({ PUBLISH_KEY: KEY, FILES: memoryBucket() });
  const headers: Record<string, string> = { "x-sha256": opts.digest ?? (await sha(body)) };
  if (opts.key !== "") headers.authorization = `Bearer ${opts.key ?? KEY}`;
  const res = await handlePublish(new Request("https://site.example" + path, { method: "PUT", body, headers }), env);
  return { res, env };
}

describe("PUT /publish", () => {
  it("stores a file whose SHA256 matches", async () => {
    const { res, env } = await put("/publish/stable/myclient-26.51.1-r14/hud__MyClient.exe", "hud-bytes");
    expect(res.status).toBe(201);
    expect(await res.json()).toEqual({ key: "stable/myclient-26.51.1-r14/hud__MyClient.exe", bytes: 9, sha256: await sha("hud-bytes") });
    const bucket = env.FILES as ReturnType<typeof memoryBucket>;
    expect(new TextDecoder().decode(bucket.puts["stable/myclient-26.51.1-r14/hud__MyClient.exe"])).toBe("hud-bytes");
  });

  it("refuses a wrong or missing key", async () => {
    expect((await put("/publish/stable/catalog.json", "{}", { key: "wrong-key-wrong-key-wrong-key-wrong" })).res.status).toBe(401);
    expect((await put("/publish/stable/catalog.json", "{}", { key: "" })).res.status).toBe(401);
  });

  it("refuses keys when PUBLISH_KEY is missing or too short", async () => {
    const env = testEnv({ FILES: memoryBucket() });
    expect((await put("/publish/stable/catalog.json", "{}", { env })).res.status).toBe(401);
    const short = testEnv({ PUBLISH_KEY: "short", FILES: memoryBucket() });
    expect((await put("/publish/stable/catalog.json", "{}", { env: short, key: "short" })).res.status).toBe(401);
  });

  it("refuses a body over the size limit", async () => {
    const env = testEnv({ PUBLISH_KEY: KEY, FILES: memoryBucket() });
    const req = new Request("https://site.example/publish/stable/big", {
      method: "PUT", body: "x", headers: { authorization: `Bearer ${KEY}`, "x-sha256": await sha("x"), "content-length": String(65 * 1024 * 1024) },
    });
    expect((await handlePublish(req, env)).status).toBe(413);
  });

  it("refuses a body that does not match x-sha256", async () => {
    const { res, env } = await put("/publish/stable/catalog.json", "{}", { digest: "0".repeat(64) });
    expect(res.status).toBe(400);
    expect(Object.keys((env.FILES as ReturnType<typeof memoryBucket>).puts)).toEqual([]);
  });

  it("only writes under stable/, early/, test/ and launcher/ with plain names", async () => {
    for (const path of ["/publish/other/x", "/publish/stable/a%2Fb", "/publish/stable//x", "/publish/stable/a%20b"]) {
      expect((await put(path, "x")).res.status, path).toBe(400);
    }
    expect((await put("/publish/early/catalog.json", "{}")).res.status).toBe(201);
    expect((await put("/publish/test/myclient-26.51.1-r54-test1/x.dll", "dll")).res.status).toBe(201);
    expect((await put("/publish/beta/catalog.json", "{}")).res.status, "beta/ is gone").toBe(400);
    expect((await put("/publish/launcher/MyClientLauncher.exe", "exe")).res.status).toBe(201);
  });
});

describe("only the release branches publish outside test/ (RELEASE_REFS)", () => {
  let signer: Awaited<ReturnType<typeof oidcSigner>> | null = null;   // one key: the Worker caches GitHub's keys by kid
  async function oidcPut(path: string, claims: Record<string, unknown>, env = testEnv({ FILES: memoryBucket() })) {
    const s = signer ?? (signer = await oidcSigner("release-refs"));
    const body = "x";
    const headers = { authorization: `Bearer ${await s.token(claims)}`, "x-sha256": await sha(body) };
    return handlePublish(new Request("https://site.example" + path, { method: "PUT", body, headers }), env, NOW, s.fetchFn);
  }
  const main = { workflow_ref: "Nagi456/MyClient/.github/workflows/build-dll.yml@refs/heads/main", ref: "refs/heads/main" };
  const work = { workflow_ref: "Nagi456/MyClient/.github/workflows/build-release.yml@refs/heads/codex/a", ref: "refs/heads/codex/a" };
  const pr = { workflow_ref: "Nagi456/MyClient/.github/workflows/build-release.yml@refs/pull/3/merge", ref: "refs/pull/3/merge" };

  it("main may write stable/, early/ and launcher/ (default RELEASE_REFS)", async () => {
    for (const key of ["stable/catalog.json", "early/catalog.json", "launcher/MyClientLauncher.exe"])
      expect((await oidcPut("/publish/" + key, main)).status, key).toBe(201);
  });

  it("a work branch or a pull request may write test/ but nothing public", async () => {
    for (const from of [work, pr]) {
      expect((await oidcPut("/publish/test/myclient-26.51.1-r9-test1/a.dll", from)).status).toBe(201);
      for (const key of ["stable/catalog.json", "early/catalog.json", "launcher/MyClientLauncher.exe", "stable/launcher.json"]) {
        const res = await oidcPut("/publish/" + key, from);
        expect(res.status, key).toBe(403);
        expect(await res.text()).toContain("refs/heads/main");
      }
    }
  });

  it("RELEASE_REFS lists the allowed refs; the ref comes from workflow_ref when the claim is missing", async () => {
    const env = testEnv({ FILES: memoryBucket(), RELEASE_REFS: "refs/heads/main, refs/heads/claude/work" });
    expect((await oidcPut("/publish/stable/catalog.json", { workflow_ref: "Nagi456/MyClient/.github/workflows/build-dll.yml@refs/heads/claude/work" }, env)).status).toBe(201);
    expect((await oidcPut("/publish/stable/catalog.json", work, env)).status).toBe(403);
  });

  it("the PUBLISH_KEY is not tied to a branch", async () => {
    expect((await put("/publish/stable/catalog.json", "x")).res.status).toBe(201);
  });
});

describe("channel files are immutable", () => {
  const key = "test/myclient-26.51.1-r54-test1/runtime.lock.json";
  it("a file of a test/early build can be uploaded again only with the same bytes (409 otherwise)", async () => {
    const env = testEnv({ PUBLISH_KEY: KEY, FILES: memoryBucket() });
    expect((await put("/publish/" + key, "lock", { env })).res.status).toBe(201);
    expect((await put("/publish/" + key, "lock", { env })).res.status).toBe(201);
    const other = await put("/publish/" + key, "other bytes", { env });
    expect(other.res.status).toBe(409);
    expect(new TextDecoder().decode((env.FILES as ReturnType<typeof memoryBucket>).puts[key])).toBe("lock");
  });

  it("the lists (catalog.json) may be replaced, and stable/ is not affected by this rule", async () => {
    const env = testEnv({ PUBLISH_KEY: KEY, TEST_MANIFEST_KEY: TEST_KEY, FILES: memoryBucket() });
    expect((await put("/publish/test/catalog.json", '{"schemaVersion":1,"sequence":1,"versions":[]}', { env })).res.status).toBe(201);
    expect((await put("/publish/test/catalog.json", '{"schemaVersion":1,"sequence":2,"versions":[]}', { env })).res.status).toBe(201);
    expect((await put("/publish/early/catalog.json", '{"sequence":1}', { env })).res.status).toBe(201);
    expect((await put("/publish/early/catalog.json", '{"sequence":2}', { env })).res.status).toBe(201);
    expect((await put("/publish/stable/x/y", "1", { env })).res.status).toBe(201);
    expect((await put("/publish/stable/x/y", "2", { env })).res.status).toBe(201);
  });
  it("a list's signature (catalog.json.sig) may be replaced with its list; the test one only by the Worker", async () => {
    const env = testEnv({ PUBLISH_KEY: KEY, FILES: memoryBucket() });
    for (const channel of ["stable", "early"]) {
      expect((await put(`/publish/${channel}/catalog.json.sig`, '{"keyId":1}', { env })).res.status).toBe(201);
      expect((await put(`/publish/${channel}/catalog.json.sig`, '{"keyId":2}', { env })).res.status).toBe(201);
    }
    expect((await put("/publish/test/catalog.json.sig", '{"keyId":1}', { env })).res.status).toBe(403);
    expect((env.FILES as ReturnType<typeof memoryBucket>).puts["test/catalog.json.sig"]).toBeUndefined();
  });
});

describe("the Worker signs test/catalog.json (test key)", () => {
  const list = '{"schemaVersion":1,"sequence":3,"versions":[]}\n';
  it("stores the .sig made with TEST_MANIFEST_KEY next to the list", async () => {
    const env = testEnv({ PUBLISH_KEY: KEY, TEST_MANIFEST_KEY: TEST_KEY, FILES: memoryBucket() });
    expect((await put("/publish/test/catalog.json", list, { env })).res.status).toBe(201);
    const puts = (env.FILES as ReturnType<typeof memoryBucket>).puts;
    expect(new TextDecoder().decode(puts["test/catalog.json"])).toBe(list);
    // the same bytes tools/signing.py makes with this key (manifest-sign.test.ts)
    expect(new TextDecoder().decode(puts["test/catalog.json.sig"])).toBe(
      '{"schemaVersion": 1, "keyId": 101, "signature": "ZFINddlPbjTt/PY+J0N3/n/t++EXqmU5/Ye/idLpj4dTnp0+U4u5dnPHgebbJLmTYdPsTjRNpHnjLvazlgVHDg=="}\n');
  });
  it("stores nothing without the key (500) or for something that is not a channel list (400)", async () => {
    for (const secret of [undefined, "broken"]) {
      const env = testEnv({ PUBLISH_KEY: KEY, TEST_MANIFEST_KEY: secret, FILES: memoryBucket() });
      expect((await put("/publish/test/catalog.json", list, { env })).res.status).toBe(500);
      expect(Object.keys((env.FILES as ReturnType<typeof memoryBucket>).puts)).toEqual([]);
    }
    const env = testEnv({ PUBLISH_KEY: KEY, TEST_MANIFEST_KEY: TEST_KEY, FILES: memoryBucket() });
    expect((await put("/publish/test/catalog.json", '{"sequence":1}', { env })).res.status).toBe(400);
    expect(Object.keys((env.FILES as ReturnType<typeof memoryBucket>).puts)).toEqual([]);
  });
});

describe("GET /dl", () => {
  const files = { "stable/catalog.json": '{"a":1}', "stable/myclient-26.51.1-r14/x.dll": "dll", "beta/catalog.json": "{}", "launcher/MyClientLauncher.exe": "exe" };

  it("serves a list's .sig like the list: no-cache, as JSON", async () => {
    const res = await handleDownload(new Request("https://site.example/dl/stable/catalog.json.sig"), testEnv({ FILES: memoryBucket({ "stable/catalog.json.sig": '{"keyId":1}' }) }));
    expect(res.status).toBe(200);
    expect(res.headers.get("cache-control")).toBe("no-cache");
    expect(res.headers.get("content-type")).toBe("application/json");
    expect(await res.text()).toBe('{"keyId":1}');
  });

  it("serves the update list without long caching", async () => {
    const res = await handleDownload(new Request("https://site.example/dl/stable/catalog.json"), testEnv({ FILES: memoryBucket(files) }));
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("application/json");
    expect(res.headers.get("cache-control")).toBe("no-cache");
    expect(await res.text()).toBe('{"a":1}');
  });

  it("serves release files as immutable", async () => {
    const res = await handleDownload(new Request("https://site.example/dl/stable/myclient-26.51.1-r14/x.dll"), testEnv({ FILES: memoryBucket(files) }));
    expect(res.status).toBe(200);
    expect(res.headers.get("content-length")).toBe("3");
    expect(res.headers.get("cache-control")).toContain("immutable");
  });

  it("never serves beta or the launcher, and 404s missing files", async () => {
    const env = testEnv({ FILES: memoryBucket(files) });
    for (const path of ["/dl/beta/catalog.json", "/dl/launcher/MyClientLauncher.exe", "/dl/stable/a%2F..%2Fbeta", "/dl/stable/none.json"]) {
      expect((await handleDownload(new Request("https://site.example" + path), env)).status, path).toBe(404);
    }
  });

  it("works through the router even before the Discord secrets are set", async () => {
    const env = testEnv({ BOT_TOKEN: "", FILES: memoryBucket(files) });
    const res = await worker.fetch(new Request("https://site.example/dl/stable/catalog.json"), env, ctx);
    expect(res.status).toBe(200);
    const login = await worker.fetch(new Request("https://site.example/login"), env, ctx);
    expect(login.status).toBe(503);
  });
});

describe("GET /publish (a workflow reads the last list of a channel)", () => {
  const files = { "test/catalog.json": '{"sequence":2}', "early/catalog.json": '{"sequence":1}', "stable/catalog.json": "{}" };
  async function read(path: string, key: string | null = KEY, f: Record<string, string> = files) {
    const env = testEnv({ PUBLISH_KEY: KEY, FILES: memoryBucket(f) });
    const headers: Record<string, string> = key === null ? {} : { authorization: `Bearer ${key}` };
    return handlePublishRead(new Request("https://site.example" + path, { headers }), env);
  }

  it("returns test/catalog.json and early/catalog.json to an authorised caller, never cached", async () => {
    const res = await read("/publish/test/catalog.json");
    expect(res.status).toBe(200);
    expect(await res.text()).toBe('{"sequence":2}');
    expect(res.headers.get("cache-control")).toBe("no-store");
    expect(await (await read("/publish/early/catalog.json")).text()).toBe('{"sequence":1}');
  });

  it("is 401 without a valid key, 400 for any other key, 404 when the list is not there", async () => {
    expect((await read("/publish/test/catalog.json", null)).status).toBe(401);
    expect((await read("/publish/test/catalog.json", "wrong-key-wrong-key-wrong-key-wrong")).status).toBe(401);
    for (const path of ["/publish/stable/catalog.json", "/publish/test/other.json", "/publish/test/x/catalog.json", "/publish/test/../stable/catalog.json"]) {
      expect((await read(path)).status, path).toBe(400);
    }
    expect((await read("/publish/test/catalog.json", KEY, {})).status).toBe(404);
  });

  it("is reachable through the router without the Discord secrets", async () => {
    const env = testEnv({ BOT_TOKEN: "", PUBLISH_KEY: KEY, FILES: memoryBucket(files) });
    const res = await worker.fetch(new Request("https://site.example/publish/test/catalog.json", { headers: { authorization: `Bearer ${KEY}` } }), env, ctx);
    expect(res.status).toBe(200);
    expect(await res.text()).toBe('{"sequence":2}');
  });
});

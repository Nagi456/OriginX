import { describe, expect, it, vi } from "vitest";
import { handleFile } from "../src/files";
import { signedFileUrl } from "../src/links";
import { downloadName } from "../src/serve";
import { NOW, memoryBucket, testEnv } from "./helpers";

const KEY = "launcher/MyClientLauncher.exe";

async function setup(files: Record<string, string>) {
  const bucket = memoryBucket(files);
  const env = testEnv({ FILES: bucket });
  return { env, bucket, url: await signedFileUrl(env, KEY, 300, NOW) };
}

describe("GET /file", () => {
  it("serves a file with a good link", async () => {
    const { env, url } = await setup({ [KEY]: "exe-bytes" });
    const res = await handleFile(new Request(url), env, NOW);
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("application/octet-stream");
    expect(res.headers.get("content-length")).toBe("9");
    expect(res.headers.get("content-disposition")).toBe("attachment; filename=\"OriginXLauncher.exe\"; filename*=UTF-8''OriginXLauncher.exe");
    expect(res.headers.get("cache-control")).toBe("private, no-store");
    expect(await res.text()).toBe("exe-bytes");
  });

  it("rejects an expired link", async () => {
    const { env, bucket, url } = await setup({ [KEY]: "exe-bytes" });
    const res = await handleFile(new Request(url), env, NOW + 301 * 1000);
    expect(res.status).toBe(403);
    expect(bucket.gets.length).toBe(0);
  });

  it("rejects a link whose key was changed", async () => {
    const { env, bucket, url } = await setup({ [KEY]: "x", "beta/catalog.json": "{}" });
    const res = await handleFile(new Request(url.replace(KEY, "beta/catalog.json")), env, NOW);
    expect(res.status).toBe(403);
    expect(bucket.gets.length).toBe(0);
  });

  it("returns 404 when the file is not in R2", async () => {
    const { env, url } = await setup({});
    expect((await handleFile(new Request(url), env, NOW)).status).toBe(404);
  });
});

describe("serving files", () => {
  it("gives a Japanese file name without failing", async () => {
    const key = "launcher/ランチャー.exe";
    const env = testEnv({ FILES: memoryBucket({ [key]: "x" }) });
    const url = await signedFileUrl(env, key, 300, NOW);
    const res = await handleFile(new Request(url), env, NOW);
    expect(res.status).toBe(200);
    expect(res.headers.get("content-disposition")).toBe(
      `attachment; filename="_____.exe"; filename*=UTF-8''${encodeURIComponent("ランチャー.exe")}`);
  });

  it("keeps ? and # in a key as part of the name", async () => {
    const key = "launcher/a?b#c.exe";
    const env = testEnv({ FILES: memoryBucket({ [key]: "x" }) });
    const url = await signedFileUrl(env, key, 300, NOW);
    const res = await handleFile(new Request(url), env, NOW);
    expect(res.status).toBe(200);
  });

  it("answers a byte range", async () => {
    const { env, url } = await setup({ [KEY]: "0123456789" });
    const part = await handleFile(new Request(url, { headers: { range: "bytes=2-5" } }), env, NOW);
    expect(part.status).toBe(206);
    expect(part.headers.get("content-range")).toBe("bytes 2-5/10");
    expect(part.headers.get("content-length")).toBe("4");
    expect(await part.text()).toBe("2345");
    const tail = await handleFile(new Request(url, { headers: { range: "bytes=-3" } }), env, NOW);
    expect(await tail.text()).toBe("789");
    const past = await handleFile(new Request(url, { headers: { range: "bytes=20-" } }), env, NOW);
    expect(past.status).toBe(416);
    expect(past.headers.get("content-range")).toBe("bytes */10");
  });

  it("answers 503 when R2 fails", async () => {
    const bucket = memoryBucket({ [KEY]: "x" });
    bucket.get = async () => { throw new Error("R2 down"); };
    const env = testEnv({ FILES: bucket });
    vi.spyOn(console, "error").mockImplementation(() => {});
    const res = await handleFile(new Request(await signedFileUrl(env, KEY, 300, NOW)), env, NOW);
    expect(res.status).toBe(503);
  });
});

describe("downloadName", () => {
  it("gives the launcher the product's name and leaves other files alone", () => {
    expect(downloadName("stable/launcher/17/MyClientLauncher.exe")).toBe("OriginXLauncher.exe");
    expect(downloadName("stable/myclient-26.51.1-r29/MyClientData.dll")).toBe("MyClientData.dll");
  });
});

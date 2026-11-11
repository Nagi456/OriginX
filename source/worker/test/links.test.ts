import { describe, expect, it } from "vitest";
import { checkFileLink, signedFileUrl } from "../src/links";
import { NOW, testEnv } from "./helpers";

const env = testEnv();

function parts(url: string) {
  const u = new URL(url);
  return {
    key: decodeURIComponent(u.pathname.replace(/^\/file\//, "")),
    exp: u.searchParams.get("exp"),
    sig: u.searchParams.get("sig"),
    url: u,
  };
}

describe("signed file links", () => {
  it("signed link opens", async () => {
    const p = parts(await signedFileUrl(env, "launcher/MyClientLauncher.exe", 300, NOW));
    expect(p.url.origin).toBe("https://site.example");
    expect(p.exp).toBe(String(NOW / 1000 + 300));
    expect(await checkFileLink(env, p.key, p.exp, p.sig, NOW)).toBe(true);
  });

  it("rejects an expired link", async () => {
    const p = parts(await signedFileUrl(env, "launcher/x", 300, NOW));
    expect(await checkFileLink(env, p.key, p.exp, p.sig, NOW + 301 * 1000)).toBe(false);
  });

  it("rejects a link whose key was changed", async () => {
    const p = parts(await signedFileUrl(env, "launcher/x", 300, NOW));
    expect(await checkFileLink(env, "beta/x", p.exp, p.sig, NOW)).toBe(false);
  });

  it("rejects a missing exp or sig", async () => {
    const p = parts(await signedFileUrl(env, "launcher/x", 300, NOW));
    expect(await checkFileLink(env, p.key, null, p.sig, NOW)).toBe(false);
    expect(await checkFileLink(env, p.key, p.exp, null, NOW)).toBe(false);
  });

  it("keeps slashes in the key", async () => {
    const url = await signedFileUrl(env, "beta/26.51.1/1/MyClientData.dll", 1800, NOW);
    expect(new URL(url).pathname).toBe("/file/beta/26.51.1/1/MyClientData.dll");
  });
});

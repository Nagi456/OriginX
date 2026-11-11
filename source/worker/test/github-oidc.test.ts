import { beforeEach, describe, expect, it } from "vitest";
import { forgetKeys, verifyGithubOidc } from "../src/github-oidc";
import { handlePublish } from "../src/publish";
import { NOW, hex, memoryBucket, oidcSigner as signer, testEnv } from "./helpers";

beforeEach(() => forgetKeys());

describe("GitHub Actions OIDC", () => {
  it("accepts a token of this repository for this audience", async () => {
    const s = await signer();
    const claims = await verifyGithubOidc(await s.token(), NOW, s.fetchFn);
    expect(claims?.repository).toBe("Nagi456/MyClient");
    await verifyGithubOidc(await s.token(), NOW, s.fetchFn);
    expect(s.fetches.length).toBe(1); // keys are cached
  });

  it("refuses other repositories, audiences, issuers and expired tokens", async () => {
    const s = await signer();
    for (const claims of [{ repository_id: "1" }, { workflow_ref: "Nagi456/MyClient/.github/workflows/sdk-search.yml@refs/heads/x" }, { aud: "other" }, { iss: "https://evil.example" }, { exp: NOW / 1000 - 1 }, { nbf: NOW / 1000 + 600 }]) {
      expect(await verifyGithubOidc(await s.token(claims), NOW, s.fetchFn), JSON.stringify(claims)).toBeNull();
    }
  });

  it("accepts promote- workflows too, but only for the promote step (PUT /publish is refused with 403)", async () => {
    const s = await signer();
    const promote = { workflow_ref: "Nagi456/MyClient/.github/workflows/promote-build.yml@refs/heads/x" };
    expect((await verifyGithubOidc(await s.token(promote), NOW, s.fetchFn))?.workflow_ref).toContain("promote-build");
    const env = testEnv({ FILES: memoryBucket() });
    const req = (bearer: string) => new Request("https://site.example/publish/early/x/y", {
      method: "PUT", body: "x", headers: { authorization: `Bearer ${bearer}`, "x-sha256": "2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881" },
    });
    expect((await handlePublish(req(await s.token(promote)), env, NOW, s.fetchFn)).status).toBe(403);
    expect((await handlePublish(req(await s.token()), env, NOW, s.fetchFn)).status).toBe(201);
  });

  it("refuses a forged signature, another algorithm and an unknown key", async () => {
    const s = await signer();
    const other = await signer();
    const good = await s.token();
    const forged = good.split(".").slice(0, 2).join(".") + "." + (await other.token()).split(".")[2];
    expect(await verifyGithubOidc(forged, NOW, s.fetchFn)).toBeNull();
    expect(await verifyGithubOidc(await s.token({}, { alg: "none" }), NOW, s.fetchFn)).toBeNull();
    expect(await verifyGithubOidc(await s.token({}, { kid: "nope" }), NOW, s.fetchFn)).toBeNull();
    expect(await verifyGithubOidc("a.b", NOW, s.fetchFn)).toBeNull();
  });

  it("lets a workflow publish without any secret", async () => {
    const s = await signer();
    const env = testEnv({ FILES: memoryBucket() });
    const body = "catalog";
    const digest = hex(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(body)));
    const req = (bearer: string) => new Request("https://site.example/publish/stable/catalog.json", {
      method: "PUT", body, headers: { authorization: `Bearer ${bearer}`, "x-sha256": digest },
    });
    expect((await handlePublish(req(await s.token()), env, NOW, s.fetchFn)).status).toBe(201);
    expect((await handlePublish(req(await s.token({ repository_id: "2" })), env, NOW, s.fetchFn)).status).toBe(401);
  });
});

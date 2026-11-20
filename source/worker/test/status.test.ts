import { afterEach, describe, expect, it, vi } from "vitest";
import { channelAccess, handleStatus } from "../src/status";
import { makeToken, readToken } from "../src/token";
import { NOW, fakeFetch, json, memoryBucket, testEnv } from "./helpers";

afterEach(() => vi.unstubAllGlobals());

const DAY = 86400 * 1000;
const catalog = (channel: string) => JSON.stringify({
  schemaVersion: 1,
  sequence: 4,
  versions: [{
    id: "26.51.1", available: true, title: `26.51.1 ${channel}`,
    files: { "mods/MyClientData/MyClientData.dll": { url: `${channel}/myclient-26.51.1-r54-test1/mods__MyClientData__MyClientData.dll`, sha256: "ab", bytes: 10 } },
  }],
});
const BOTH = { "early/catalog.json": catalog("early"), "test/catalog.json": catalog("test") };

async function ask(member: Response, opts: { files?: Record<string, string>; tokenAt?: number; token?: string } = {}) {
  const env = testEnv({ FILES: memoryBucket(opts.files ?? BOTH) });
  vi.stubGlobal("fetch", fakeFetch(() => member).fn);
  const token = opts.token ?? (await makeToken(env.SIGNING_KEY, "42", opts.tokenAt ?? NOW));
  const res = await handleStatus(
    new Request("https://site.example/launcher/status", { headers: { authorization: `Bearer ${token}` } }), env, NOW);
  return { res, env, body: (await res.json()) as any };
}

const member = (roles: string[], extra: Record<string, unknown> = {}) =>
  json({ roles, nick: null, ...extra, user: { id: "42", username: "nagi" } });

describe("channelAccess", () => {
  it("lets only moderators into test, and creators, partners or developers (who are members) or moderators into early", () => {
    const r = { verified: true, moderator: false, creator: false, partner: false, developer: false };
    expect(channelAccess({ ...r, verified: false })).toEqual({ early: false, test: false });
    expect(channelAccess(r)).toEqual({ early: false, test: false });
    expect(channelAccess({ ...r, creator: true })).toEqual({ early: true, test: false });
    expect(channelAccess({ ...r, partner: true })).toEqual({ early: true, test: false });
    expect(channelAccess({ ...r, developer: true })).toEqual({ early: true, test: false });
    expect(channelAccess({ ...r, verified: false, partner: true })).toEqual({ early: false, test: false });
    expect(channelAccess({ ...r, verified: false, moderator: true })).toEqual({ early: true, test: true });
  });
});

describe("GET /launcher/status", () => {
  it("401 without a valid token", async () => {
    const { res, body } = await ask(member([]), { token: "v1.bad.token" });
    expect(res.status).toBe(401);
    expect(body).toEqual({ error: "login" });
  });

  it("not in the server gives no roles and no channels", async () => {
    const { res, body } = await ask(json({}, 404));
    expect(res.status).toBe(200);
    expect(body).toEqual({
      user: { id: "42", name: null, avatarUrl: null },
      roles: { verified: false, supporter: false, moderator: false, founder: false, supporterCape: false, creator: false, partner: false, developer: false, capes: [] },
      channels: { early: null, test: null },
      signedChannels: { early: null, test: null },
      testLauncher: null,
    });
  });

  it("a member without supporter gets no channels", async () => {
    const { body } = await ask(member(["200"]));
    expect(body).toMatchObject({ user: { id: "42", name: "nagi" }, roles: { verified: true, supporter: false, moderator: false }, channels: { early: null, test: null } });
    expect(body.token).toBeUndefined();
  });

  it("a supporter no longer gets early or test", async () => {
    const { body } = await ask(member(["200", "300"]));
    expect(body.roles.supporter).toBe(true);
    expect(body.channels).toEqual({ early: null, test: null });
  });

  it("a moderator gets early with signed urls", async () => {
    const { body, res } = await ask(member(["500"]));
    expect(res.headers.get("cache-control")).toBe("no-store");
    const file = body.channels.early.versions[0].files["mods/MyClientData/MyClientData.dll"];
    expect(file.url).toMatch(/^https:\/\/site\.example\/file\/early\/myclient-26\.51\.1-r54-test1\/mods__MyClientData__MyClientData\.dll\?exp=\d+&sig=/);
    expect(file.sha256).toBe("ab");
    expect(file.bytes).toBe(10);
  });

  it("a moderator gets both, and the old beta fields are gone", async () => {
    const { body } = await ask(member(["500"]));
    expect(body.roles).toEqual({ verified: false, supporter: false, moderator: true, founder: false, supporterCape: false, creator: false, partner: false, developer: false, capes: [] });
    expect(body.channels.test.versions[0].files["mods/MyClientData/MyClientData.dll"].url).toMatch(/\/file\/test\/myclient-26\.51\.1-r54-test1\//);
    expect(body.channels.early).not.toBeNull();
    expect(body.beta).toBeUndefined();
    expect(body.verified).toBeUndefined();
  });

  it("hands the stored bytes of each list and its .sig next to the rewritten list, for the launcher's signature check", async () => {
    const sig = '{"schemaVersion":1,"keyId":1,"signature":"AA=="}\n';
    const { body } = await ask(member(["500"]), { files: { ...BOTH, "test/catalog.json.sig": sig } });
    expect(body.signedChannels.test).toEqual({ catalog: btoa(catalog("test")), signature: sig });
    // no .sig in R2: nothing to check against (the launcher then refuses the list)
    expect(body.signedChannels.early).toBeNull();
    expect(body.channels.early).not.toBeNull();
  });

  it("hands the test launcher list, its .sig and a link to its exe to moderators only", async () => {
    const list = JSON.stringify({ schemaVersion: 1, channel: "test", build: 45, key: "test/launcher/45/MyClientLauncher.exe", sha256: "ab", bytes: 3 });
    const files = { ...BOTH, "test/launcher.json": list, "test/launcher.json.sig": "{}" };
    const { body } = await ask(member(["500"]), { files });
    expect(body.testLauncher.list).toBe(btoa(list));
    expect(body.testLauncher.signature).toBe("{}");
    expect(body.testLauncher.url).toMatch(/^https:\/\/site\.example\/file\/test\/launcher\/45\/MyClientLauncher\.exe\?exp=\d+&sig=/);
    expect((await ask(member(["200", "600"]), { files })).body.testLauncher).toBeNull();   // early only
    // no .sig, or a key that is not this build's exe: nothing
    expect((await ask(member(["500"]), { files: { ...BOTH, "test/launcher.json": list } })).body.testLauncher).toBeNull();
    const other = JSON.stringify({ schemaVersion: 1, channel: "test", build: 45, key: "stable/launcher/45/MyClientLauncher.exe" });
    expect((await ask(member(["500"]), { files: { ...files, "test/launcher.json": other } })).body.testLauncher).toBeNull();
  });

  it("gives no signed lists to an account without channels", async () => {
    const { body } = await ask(member(["200"]), { files: { ...BOTH, "test/catalog.json.sig": "{}" } });
    expect(body.signedChannels).toEqual({ early: null, test: null });
  });

  it("puts the member's icon url in the answer", async () => {
    const hash = "e".repeat(32);
    const { body } = await ask(member(["200"], { avatar: hash }));
    expect(body.user.avatarUrl).toBe(`https://cdn.discordapp.com/guilds/100/users/42/avatars/${hash}.png?size=64`);
  });

  it("gives null for a channel whose catalog is missing or broken, and still 200", async () => {
    for (const files of [{} as Record<string, string>, { "test/catalog.json": "{", "early/catalog.json": "{" }, { "test/catalog.json": '{"versions":3}', "early/catalog.json": '{"versions":3}' }]) {
      const { res, body } = await ask(member(["500"]), { files });
      expect(res.status).toBe(200);
      expect(body.roles.moderator).toBe(true);
      expect(body.channels).toEqual({ early: null, test: null });
    }
  });

  it("a broken test catalog does not hide a good early one", async () => {
    const { body } = await ask(member(["500"]), { files: { "test/catalog.json": "{", "early/catalog.json": catalog("early") } });
    expect(body.channels.test).toBeNull();
    expect(body.channels.early).not.toBeNull();
  });

  it("renews a token with under 30 days left", async () => {
    const { body, env } = await ask(member(["200"]), { tokenAt: NOW - 61 * DAY });
    const claims = await readToken(env.SIGNING_KEY, body.token, NOW);
    expect(claims?.sub).toBe("42");
    expect(claims?.exp).toBe(NOW / 1000 + 90 * 86400);
  });

  it("503 when Discord is down", async () => {
    vi.spyOn(console, "error").mockImplementation(() => {});
    const { res, body } = await ask(json({}, 500));
    expect(res.status).toBe(503);
    expect(body).toEqual({ error: "discord" });
  });
});

describe("cape roles in the status answer", () => {
  it("reports founder / supporter cape / creator and the capes they give, and ignores empty role ids", async () => {
    const env = testEnv({ FILES: memoryBucket(BOTH), FOUNDER_ROLE_ID: "601", SUPPORTER_CAPE_ROLE_ID: "602", CREATOR_ROLE_ID: "603" });
    vi.stubGlobal("fetch", fakeFetch(() => member(["200", "603"])).fn);
    const token = await makeToken(env.SIGNING_KEY, "42", NOW);
    const res = await handleStatus(new Request("https://site.example/launcher/status", { headers: { authorization: `Bearer ${token}` } }), env, NOW);
    const body = (await res.json()) as any;
    expect(body.roles.creator).toBe(true);
    expect(body.roles.founder).toBe(false);
    expect(body.roles.capes).toEqual([2, 4]);
    expect(body.channels.early).not.toBeNull();
    const { body: plain } = await ask(member(["200", ""]));
    expect(plain.roles.creator).toBe(false);
    expect(plain.roles.capes).toEqual([2]);
  });
});

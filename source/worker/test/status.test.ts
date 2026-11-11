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
  it("lets only moderators into test, and supporters (who are members) or moderators into early", () => {
    const f = (verified: boolean, supporter: boolean, moderator: boolean) => channelAccess({ verified, supporter, moderator });
    expect(f(false, false, false)).toEqual({ early: false, test: false });
    expect(f(true, false, false)).toEqual({ early: false, test: false });
    expect(f(true, true, false)).toEqual({ early: true, test: false });
    expect(f(false, true, false)).toEqual({ early: false, test: false });
    expect(f(false, false, true)).toEqual({ early: true, test: true });
    expect(f(true, true, true)).toEqual({ early: true, test: true });
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
      roles: { verified: false, supporter: false, moderator: false },
      channels: { early: null, test: null },
    });
  });

  it("a member without supporter gets no channels", async () => {
    const { body } = await ask(member(["200"]));
    expect(body).toMatchObject({ user: { id: "42", name: "nagi" }, roles: { verified: true, supporter: false, moderator: false }, channels: { early: null, test: null } });
    expect(body.token).toBeUndefined();
  });

  it("a supporter gets early with signed urls but not test", async () => {
    const { body, res } = await ask(member(["200", "300"]));
    expect(res.headers.get("cache-control")).toBe("no-store");
    expect(body.channels.test).toBeNull();
    const file = body.channels.early.versions[0].files["mods/MyClientData/MyClientData.dll"];
    expect(file.url).toMatch(/^https:\/\/site\.example\/file\/early\/myclient-26\.51\.1-r54-test1\/mods__MyClientData__MyClientData\.dll\?exp=\d+&sig=/);
    expect(file.sha256).toBe("ab");
    expect(file.bytes).toBe(10);
  });

  it("a moderator gets both, and the old beta fields are gone", async () => {
    const { body } = await ask(member(["500"]));
    expect(body.roles).toEqual({ verified: false, supporter: false, moderator: true });
    expect(body.channels.test.versions[0].files["mods/MyClientData/MyClientData.dll"].url).toMatch(/\/file\/test\/myclient-26\.51\.1-r54-test1\//);
    expect(body.channels.early).not.toBeNull();
    expect(body.beta).toBeUndefined();
    expect(body.verified).toBeUndefined();
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

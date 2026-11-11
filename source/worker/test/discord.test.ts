import { afterEach, describe, expect, it, vi } from "vitest";
import { DiscordError, addRole, exchangeCode, getMember, memberAvatarUrl, registerCommands } from "../src/discord";
import { fakeFetch, json, testEnv } from "./helpers";

const env = testEnv();
afterEach(() => vi.unstubAllGlobals());

describe("Discord API", () => {
  it("getMember returns roles and name", async () => {
    const f = fakeFetch(() => json({ roles: ["200"], nick: null, user: { id: "1", username: "nagi", global_name: "Nagi" } }));
    vi.stubGlobal("fetch", f.fn);
    expect(await getMember(env, "1")).toEqual({ roles: ["200"], name: "Nagi", avatarUrl: null });
    expect(f.calls[0].url).toBe("https://discord.com/api/v10/guilds/100/members/1");
  });

  it("getMember returns the server icon first, then the account icon", async () => {
    const guildHash = "a".repeat(32), userHash = "b".repeat(32);
    vi.stubGlobal("fetch", fakeFetch(() => json({ roles: [], avatar: guildHash, user: { id: "1", username: "n", avatar: userHash } })).fn);
    expect((await getMember(env, "1"))?.avatarUrl).toBe(`https://cdn.discordapp.com/guilds/100/users/1/avatars/${guildHash}.png?size=64`);
    vi.stubGlobal("fetch", fakeFetch(() => json({ roles: [], user: { id: "1", username: "n", avatar: userHash } })).fn);
    expect((await getMember(env, "1"))?.avatarUrl).toBe(`https://cdn.discordapp.com/avatars/1/${userHash}.png?size=64`);
  });

  it("getMember returns null for 404", async () => {
    vi.stubGlobal("fetch", fakeFetch(() => json({ message: "Unknown Member" }, 404)).fn);
    expect(await getMember(env, "1")).toBeNull();
  });

  it("retries once after 429", async () => {
    const f = fakeFetch(
      () => json({ retry_after: 0 }, 429, { "retry-after": "0" }),
      () => json({ roles: [], user: { id: "1", username: "nagi" } }),
    );
    vi.stubGlobal("fetch", f.fn);
    expect(await getMember(env, "1")).toEqual({ roles: [], name: "nagi", avatarUrl: null });
    expect(f.calls.length).toBe(2);
  });

  it("gives 503 after two 429s", async () => {
    vi.stubGlobal("fetch", fakeFetch(() => json({}, 429, { "retry-after": "0" })).fn);
    await expect(getMember(env, "1")).rejects.toMatchObject({ status: 503 });
  });

  it("addRole is false on 403", async () => {
    vi.stubGlobal("fetch", fakeFetch(() => json({ message: "Missing Permissions" }, 403)).fn);
    expect(await addRole(env, "1", "200")).toBe(false);
  });

  it("sends the bot token", async () => {
    const f = fakeFetch(() => new Response(null, { status: 204 }));
    vi.stubGlobal("fetch", f.fn);
    expect(await addRole(env, "1", "200")).toBe(true);
    expect(f.calls[0].method).toBe("PUT");
    expect(f.calls[0].url).toBe("https://discord.com/api/v10/guilds/100/members/1/roles/200");
    expect(f.calls[0].headers.get("authorization")).toBe("Bot test-token");
  });

  it("exchangeCode posts the form", async () => {
    const f = fakeFetch(() => json({ access_token: "user-token" }));
    vi.stubGlobal("fetch", f.fn);
    expect(await exchangeCode(env, "the-code", "https://site.example/callback")).toBe("user-token");
    const form = new URLSearchParams(f.calls[0].body);
    expect(f.calls[0].url).toBe("https://discord.com/api/v10/oauth2/token");
    expect(form.get("grant_type")).toBe("authorization_code");
    expect(form.get("code")).toBe("the-code");
    expect(form.get("redirect_uri")).toBe("https://site.example/callback");
    expect(form.get("client_secret")).toBe("client-secret");
  });

  it("exchangeCode fails with 400 when Discord refuses the code", async () => {
    vi.stubGlobal("fetch", fakeFetch(() => json({ error: "invalid_grant" }, 400)).fn);
    const err = await exchangeCode(env, "bad", "x").catch((e) => e);
    expect(err).toBeInstanceOf(DiscordError);
    expect(err.status).toBe(400);
  });

  it("registerCommands puts setup-verify with admin only", async () => {
    const f = fakeFetch(() => json([]));
    vi.stubGlobal("fetch", f.fn);
    await registerCommands(env);
    expect(f.calls[0].method).toBe("PUT");
    expect(f.calls[0].url).toBe("https://discord.com/api/v10/applications/400/guilds/100/commands");
    const [command] = JSON.parse(f.calls[0].body);
    expect(command).toMatchObject({ name: "setup-verify", default_member_permissions: "8", type: 1 });
  });
});

describe("memberAvatarUrl", () => {
  const h = "c".repeat(32);
  it("prefers the server icon, then the account icon, else null", () => {
    expect(memberAvatarUrl("G", "U", h, null)).toBe(`https://cdn.discordapp.com/guilds/G/users/U/avatars/${h}.png?size=64`);
    expect(memberAvatarUrl("G", "U", null, h)).toBe(`https://cdn.discordapp.com/avatars/U/${h}.png?size=64`);
    expect(memberAvatarUrl("G", "U", undefined, undefined)).toBeNull();
  });
  it("accepts an animated icon hash (a_ prefix) and ignores bad hashes", () => {
    expect(memberAvatarUrl("G", "U", null, "a_" + h)).toBe(`https://cdn.discordapp.com/avatars/U/a_${h}.png?size=64`);
    expect(memberAvatarUrl("G", "U", "../x", null)).toBeNull();
    expect(memberAvatarUrl("G", "U", null, "d".repeat(31))).toBeNull();
    expect(memberAvatarUrl("G", "U", null, "D".repeat(32))).toBeNull();
  });
});

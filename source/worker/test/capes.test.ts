import { describe, expect, it } from "vitest";
import { capesFor, markTier } from "../src/capes";
import { channelAccess } from "../src/status";

describe("capesFor", () => {
  it("gives nothing to non-members, Standard to members, and one cape per cape role", () => {
    expect(capesFor({ verified: false, founder: true, supporterCape: true, creator: true })).toEqual([]);
    expect(capesFor({ verified: true, founder: false, supporterCape: false, creator: false })).toEqual([2]);
    expect(capesFor({ verified: true, founder: true, supporterCape: true, creator: true })).toEqual([1, 2, 3, 4]);
    expect(capesFor({ verified: true, founder: false, supporterCape: true, creator: false })).toEqual([2, 3]);
    expect(capesFor({ verified: true, founder: false, supporterCape: false, creator: false, moderator: true })).toEqual([2, 5, 6]);
    expect(capesFor({ verified: false, founder: false, supporterCape: false, creator: false, moderator: true })).toEqual([]);
  });
  it("lets creators who are members into early", () => {
    expect(channelAccess({ verified: true, moderator: false, creator: true, partner: false, developer: false }).early).toBe(true);
    expect(channelAccess({ verified: false, moderator: false, creator: true, partner: false, developer: false }).early).toBe(false);
  });
});

import { capeRoleFor } from "../src/capes";
import { testEnv } from "./helpers";

describe("capeRoleFor", () => {
  const env = testEnv({ PUBLIC_LAUNCH_DATE: "2027-01-01", FOUNDER_ROLE_ID: "601", SUPPORTER_CAPE_ROLE_ID: "602" });
  it("is founder within 90 days of the launch day (and before it), supporter cape after", () => {
    expect(capeRoleFor(env, Date.UTC(2026, 11, 20))).toBe("601");
    expect(capeRoleFor(env, Date.UTC(2027, 2, 31, 23, 59))).toBe("601");
    expect(capeRoleFor(env, Date.UTC(2027, 3, 2, 0, 0))).toBe("602");
  });
  it("gives nothing without a launch day or without the role id", () => {
    expect(capeRoleFor(testEnv({ FOUNDER_ROLE_ID: "601", SUPPORTER_CAPE_ROLE_ID: "602" }), Date.UTC(2027, 0, 5))).toBeNull();
    expect(capeRoleFor(testEnv({ PUBLIC_LAUNCH_DATE: "2027-01-01", SUPPORTER_CAPE_ROLE_ID: "602" }), Date.UTC(2027, 0, 5))).toBeNull();
    expect(capeRoleFor(testEnv({ PUBLIC_LAUNCH_DATE: "nope", FOUNDER_ROLE_ID: "601" }), Date.UTC(2027, 0, 5))).toBeNull();
  });
});

import { afterEach, vi } from "vitest";
import { MAX_RECHECKS, handleCapeLookup, handleCapeRegister, xuidKey } from "../src/capes";
import { makeToken } from "../src/token";
import { NOW, fakeFetch, json, memoryBucket } from "./helpers";

describe("POST /capes/register and /capes/lookup", () => {
  afterEach(() => vi.unstubAllGlobals());
  const DAY = 86400 * 1000;
  // Discord answers each member lookup with these roles (by user id).
  function discord(rolesOf: Record<string, string[]>) {
    return fakeFetch((call) => {
      const id = call.url.split("/members/")[1];
      return id in rolesOf ? json({ roles: rolesOf[id], user: { username: "u" } }) : new Response(null, { status: 404 });
    });
  }
  const envOf = (files: Record<string, string> = {}) =>
    testEnv({ FILES: memoryBucket(files), FOUNDER_ROLE_ID: "601", SUPPORTER_CAPE_ROLE_ID: "602", CREATOR_ROLE_ID: "603" });
  async function call(handler: typeof handleCapeRegister, env: ReturnType<typeof envOf>, user: string | null, body: unknown, now = NOW) {
    const headers: Record<string, string> = { "content-type": "application/json" };
    if (user) headers.authorization = `Bearer ${await makeToken(env.SIGNING_KEY, user, now)}`;
    const res = await handler(new Request("https://site.example/capes/x", { method: "POST", headers, body: JSON.stringify(body) }), env, now);
    return { res, body: (await res.json()) as any };
  }

  it("registers a cape the member may wear and returns it by XUID, without Discord ids", async () => {
    vi.stubGlobal("fetch", discord({ "42": ["200", "603"], "7": ["200"] }).fn);
    const env = envOf();
    expect((await call(handleCapeRegister, env, "42", { xuid: "2535400000000001", cape: 4 })).res.status).toBe(200);
    const { res, body } = await call(handleCapeLookup, env, "7", { xuids: ["2535400000000001", "2535400000000002"] });
    expect(res.status).toBe(200);
    expect(body).toEqual({ capes: { "2535400000000001": 4 }, marks: { "2535400000000001": 4 } });
    expect(JSON.stringify(body)).not.toContain("42");
  });
  it("refuses a cape the member may not wear, a bad XUID, too many XUIDs, no login and non-members", async () => {
    vi.stubGlobal("fetch", discord({ "42": ["200"], "9": [] }).fn);
    const env = envOf();
    expect((await call(handleCapeRegister, env, "42", { xuid: "1", cape: 1 })).res.status).toBe(403);
    expect((await call(handleCapeRegister, env, "42", { xuid: "abc", cape: 2 })).res.status).toBe(400);
    expect((await call(handleCapeLookup, env, "42", { xuids: Array.from({ length: 65 }, (_, i) => String(i + 1)) })).res.status).toBe(400);
    expect((await call(handleCapeLookup, env, null, { xuids: ["1"] })).res.status).toBe(401);
    expect((await call(handleCapeLookup, env, "9", { xuids: ["1"] })).res.status).toBe(403);
  });
  it("keeps one XUID per account, and cape 0 removes the registration", async () => {
    vi.stubGlobal("fetch", discord({ "42": ["200"], "7": ["200"] }).fn);
    const env = envOf();
    await call(handleCapeRegister, env, "42", { xuid: "11", cape: 2 });
    await call(handleCapeRegister, env, "42", { xuid: "22", cape: 2 });
    expect((await call(handleCapeLookup, env, "7", { xuids: ["11", "22"] })).body).toEqual({ capes: { "22": 2 }, marks: { "22": 1 } });
    await call(handleCapeRegister, env, "42", { xuid: "22", cape: 0 });
    expect((await call(handleCapeLookup, env, "7", { xuids: ["22"] })).body).toEqual({ capes: {}, marks: { "22": 1 } });
  });
  it("drops a cape whose role is gone, once the check is a day old", async () => {
    const roles: Record<string, string[]> = { "42": ["200", "602"], "7": ["200"] };
    vi.stubGlobal("fetch", discord(roles).fn);
    const env = envOf();
    await call(handleCapeRegister, env, "42", { xuid: "33", cape: 3 });
    roles["42"] = ["200"];
    expect((await call(handleCapeLookup, env, "7", { xuids: ["33"] }, NOW + DAY / 2)).body).toEqual({ capes: { "33": 3 }, marks: { "33": 2 } });
    expect((await call(handleCapeLookup, env, "7", { xuids: ["33"] }, NOW + 2 * DAY)).body).toEqual({ capes: {}, marks: { "33": 1 } });
    roles["42"] = [];
    expect((await call(handleCapeLookup, env, "7", { xuids: ["33"] }, NOW + 4 * DAY)).body).toEqual({ capes: {}, marks: {} });
  });
  it("independently hides capes and marks and rejects invalid flags", async () => {
    vi.stubGlobal("fetch", discord({ "42": ["200"], "7": ["200"] }).fn);
    const env = envOf();
    await call(handleCapeRegister, env, "42", { xuid: "22", cape: 2, hideCape: true });
    expect((await call(handleCapeLookup, env, "7", { xuids: ["22"] })).body).toEqual({ capes: {}, marks: { "22": 1 } });
    await call(handleCapeRegister, env, "42", { xuid: "22", cape: 2, hideMark: true });
    expect((await call(handleCapeLookup, env, "7", { xuids: ["22"] })).body).toEqual({ capes: { "22": 2 }, marks: {} });
    expect((await call(handleCapeRegister, env, "42", { xuid: "22", cape: 2, hideMark: "false" })).res.status).toBe(400);
  });
  it("uses configured role ids, ignores empty ids and refreshes role tiers", async () => {
    const roles = { "42": ["200", "dev", "partner"], "7": ["200"] };
    vi.stubGlobal("fetch", discord(roles).fn);
    const env = testEnv({ FILES: memoryBucket({}), DEVELOPER_ROLE_ID: "dev", PARTNER_ROLE_ID: "partner" });
    await call(handleCapeRegister, env, "42", { xuid: "22", cape: 0 });
    expect((await call(handleCapeLookup, env, "7", { xuids: ["22"] })).body).toEqual({ capes: {}, marks: { "22": 7 } });
    roles["42"] = ["200", "partner"];
    expect((await call(handleCapeLookup, env, "7", { xuids: ["22"] }, NOW + 2 * DAY)).body.marks).toEqual({ "22": 5 });
    env.PARTNER_ROLE_ID = "";
    env.DEVELOPER_ROLE_ID = "";
    await call(handleCapeRegister, env, "42", { xuid: "22", cape: 0 });
    expect((await call(handleCapeLookup, env, "7", { xuids: ["22"] })).body.marks).toEqual({ "22": 1 });
  });
  it("returns a hidden mark only to its registered owner", async () => {
    vi.stubGlobal("fetch", discord({ "42": ["200", "603"], "7": ["200"] }).fn);
    const env = envOf();
    await call(handleCapeRegister, env, "42", { xuid: "22", cape: 0, hideMark: true });
    expect((await call(handleCapeLookup, env, "42", { xuids: ["22"] })).body).toEqual({ capes: {}, marks: { "22": 4 } });
    expect((await call(handleCapeLookup, env, "7", { xuids: ["22"] })).body).toEqual({ capes: {}, marks: {} });
  });
  it("writes only the registering player's own records (no shared registry to lose writes in)", async () => {
    vi.stubGlobal("fetch", discord({ "42": ["200"], "7": ["200"] }).fn);
    const env = envOf();
    await call(handleCapeRegister, env, "42", { xuid: "11", cape: 2 });
    const before = Object.keys((env.FILES as any).puts);
    await call(handleCapeRegister, env, "7", { xuid: "22", cape: 2 });
    const written = Object.keys((env.FILES as any).puts).filter((k) => !before.includes(k));
    expect(written.sort()).toEqual(["capes/user/7.json", "capes/xuid/22.json"]);
  });
  it("asks Discord again for at most MAX_RECHECKS stale registrations per lookup", async () => {
    const roles: Record<string, string[]> = { "7": ["200"] };
    for (let i = 0; i < 10; i++) roles[String(100 + i)] = ["200"];
    const f = discord(roles);
    vi.stubGlobal("fetch", f.fn);
    const env = envOf();
    for (let i = 0; i < 10; i++) await call(handleCapeRegister, env, String(100 + i), { xuid: String(500 + i), cape: 2 });
    const callsBefore = f.calls.length;
    await call(handleCapeLookup, env, "7", { xuids: Array.from({ length: 10 }, (_, i) => String(500 + i)) }, NOW + 2 * DAY);
    expect(f.calls.length - callsBefore).toBe(1 + MAX_RECHECKS);   // the asker + the rechecks
  });
});

describe("markTier", () => {
  const base = { verified: true, founder: false, supporterCape: false, creator: false };
  it("selects the highest role and requires membership", () => {
    expect(markTier(base)).toBe(1);
    expect(markTier({...base, supporter: true})).toBe(2);
    expect(markTier({...base, supporterCape: true})).toBe(2);
    expect(markTier({...base, founder: true, supporter: true})).toBe(3);
    expect(markTier({...base, founder: true, creator: true})).toBe(4);
    expect(markTier({...base, creator: true, partner: true})).toBe(5);
    expect(markTier({...base, partner: true, moderator: true})).toBe(6);
    expect(markTier({...base, moderator: true, developer: true})).toBe(7);
    expect(markTier({...base, verified: false, developer: true})).toBe(0);
  });
});

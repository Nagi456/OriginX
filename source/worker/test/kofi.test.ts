import { afterEach, describe, expect, it, vi } from "vitest";
import { RECORD_KEY, SUPPORT_DAYS, expireSupporters, handleKofi, loadRecords } from "../src/kofi";
import { NOW, fakeFetch, json, memoryBucket, testEnv } from "./helpers";

const DAY = 24 * 60 * 60 * 1000;
afterEach(() => vi.unstubAllGlobals());

function envWith(files: Record<string, string> = {}) {
  return testEnv({ KOFI_TOKEN: "kofi-token", FILES: memoryBucket(files) });
}

function payment(over: Record<string, unknown> = {}) {
  return {
    verification_token: "kofi-token",
    type: "Donation",
    kofi_transaction_id: "tx-1",
    from_name: "Nagi",
    discord_userid: "123456789012345678",
    ...over,
  };
}

function post(body: unknown, env = envWith(), now = NOW) {
  const form = new URLSearchParams({ data: JSON.stringify(body) });
  const req = new Request("https://site.example/kofi/webhook", {
    method: "POST",
    headers: { "content-type": "application/x-www-form-urlencoded" },
    body: form,
  });
  return handleKofi(req, env, now);
}

// Discord: the member lookup answers with these roles, role changes with 204.
function discord(roles: string[] = ["200"]) {
  return fakeFetch((call) =>
    call.method === "GET" ? json({ roles, user: { username: "nagi" } }) : new Response(null, { status: 204 }),
  );
}

describe("POST /kofi/webhook", () => {
  it("gives the role for 30 days", async () => {
    const f = discord();
    vi.stubGlobal("fetch", f.fn);
    const env = envWith();
    const res = await post(payment(), env);
    expect(res.status).toBe(200);
    expect(f.calls[0].method).toBe("GET");
    expect(f.calls[1].method).toBe("PUT");
    expect(f.calls[1].url).toContain("/guilds/100/members/123456789012345678/roles/300");
    const records = await loadRecords(env);
    expect(records.users["123456789012345678"]).toEqual({ until: NOW + SUPPORT_DAYS * DAY, granted: true, name: "Nagi" });
  });

  it("adds a second donation to the days left", async () => {
    vi.stubGlobal("fetch", discord().fn);
    const env = envWith();
    await post(payment(), env);
    await post(payment({ kofi_transaction_id: "tx-2" }), env, NOW + 10 * DAY);
    const records = await loadRecords(env);
    expect(records.users["123456789012345678"].until).toBe(NOW + 60 * DAY);
  });

  it("counts Ko-fi's retry of the same payment once", async () => {
    vi.stubGlobal("fetch", discord().fn);
    const env = envWith();
    await post(payment(), env);
    expect(await (await post(payment(), env)).text()).toBe("already counted");
    expect((await loadRecords(env)).users["123456789012345678"].until).toBe(NOW + 30 * DAY);
  });

  it("rejects a wrong verification token", async () => {
    const res = await post(payment({ verification_token: "nope-token" }));
    expect(res.status).toBe(401);
  });

  it("is closed without KOFI_TOKEN", async () => {
    const env = testEnv();
    const req = new Request("https://site.example/kofi/webhook", { method: "POST", body: new URLSearchParams({ data: "{}" }) });
    expect((await handleKofi(req, env, NOW)).status).toBe(503);
  });

  it("answers 400 to a body that is not Ko-fi's", async () => {
    const req = new Request("https://site.example/kofi/webhook", {
      method: "POST",
      headers: { "content-type": "application/x-www-form-urlencoded" },
      body: "data=%7B",
    });
    expect((await handleKofi(req, envWith(), NOW)).status).toBe(400);
  });

  it("ignores payments that are not donations, and donations without Discord", async () => {
    const f = fakeFetch(() => new Response(null, { status: 204 }));
    vi.stubGlobal("fetch", f.fn);
    vi.spyOn(console, "log").mockImplementation(() => {});
    const env = envWith();
    expect(await (await post(payment({ type: "Shop Order" }), env)).text()).toBe("ignored");
    expect(await (await post(payment({ discord_userid: "" }), env)).text()).toBe("no discord");
    expect(f.calls.length).toBe(0);
    expect(Object.keys((await loadRecords(env)).users)).toEqual([]);
  });

  it("marks someone who already had the role, and keeps it on their next donation", async () => {
    const f = discord(["200", "300"]);
    vi.stubGlobal("fetch", f.fn);
    vi.spyOn(console, "log").mockImplementation(() => {});
    const env = envWith();
    await post(payment(), env);
    expect((await loadRecords(env)).users["123456789012345678"]).toEqual({ until: NOW + 30 * DAY, granted: true, name: "Nagi", keep: true });
    // The second donation does not look again (the role is now there either way).
    await post(payment({ kofi_transaction_id: "tx-2" }), env);
    expect(f.calls.filter((c) => c.method === "GET").length).toBe(1);
    expect((await loadRecords(env)).users["123456789012345678"].keep).toBe(true);
  });

  it("does not mark someone who did not have the role", async () => {
    vi.stubGlobal("fetch", discord(["200"]).fn);
    vi.spyOn(console, "log").mockImplementation(() => {});
    const env = envWith();
    await post(payment(), env);
    expect((await loadRecords(env)).users["123456789012345678"].keep).toBeUndefined();
  });

  it("keeps the donation when Discord fails, for the cron to retry", async () => {
    vi.stubGlobal("fetch", fakeFetch(() => new Response("x", { status: 500 })).fn);
    vi.spyOn(console, "error").mockImplementation(() => {});
    const env = envWith();
    expect((await post(payment(), env)).status).toBe(200);
    expect((await loadRecords(env)).users["123456789012345678"].granted).toBe(false);
  });
});

describe("hourly supporter check", () => {
  const records = (users: Record<string, unknown>) => ({ [RECORD_KEY]: JSON.stringify({ users, seen: [] }) });

  it("takes the role back when the days are over and keeps the others", async () => {
    const f = fakeFetch(() => new Response(null, { status: 204 }));
    vi.stubGlobal("fetch", f.fn);
    vi.spyOn(console, "log").mockImplementation(() => {});
    const env = envWith(records({ "11111": { until: NOW - 1, granted: true }, "22222": { until: NOW + DAY, granted: true } }));
    expect(await expireSupporters(env, NOW)).toEqual({ removed: 1, granted: 0 });
    expect(f.calls.length).toBe(1);
    expect(f.calls[0].method).toBe("DELETE");
    expect(f.calls[0].url).toContain("/members/11111/roles/300");
    expect(Object.keys((await loadRecords(env)).users)).toEqual(["22222"]);
  });

  it("keeps a role that was given by hand before the donation", async () => {
    const f = fakeFetch(() => new Response(null, { status: 204 }));
    vi.stubGlobal("fetch", f.fn);
    vi.spyOn(console, "log").mockImplementation(() => {});
    const env = envWith(records({ "11111": { until: NOW - 1, granted: true, keep: true } }));
    expect(await expireSupporters(env, NOW)).toEqual({ removed: 0, granted: 0 });
    expect(f.calls.length).toBe(0);
    expect((await loadRecords(env)).users).toEqual({});
  });

  it("forgets someone who left the server", async () => {
    vi.stubGlobal("fetch", fakeFetch(() => new Response("{}", { status: 404 })).fn);
    vi.spyOn(console, "log").mockImplementation(() => {});
    const env = envWith(records({ "11111": { until: NOW - 1, granted: true } }));
    await expireSupporters(env, NOW);
    expect((await loadRecords(env)).users).toEqual({});
  });

  it("keeps the record when the bot may not remove the role", async () => {
    vi.stubGlobal("fetch", fakeFetch(() => new Response("{}", { status: 403 })).fn);
    vi.spyOn(console, "error").mockImplementation(() => {});
    const env = envWith(records({ "11111": { until: NOW - 1, granted: true } }));
    expect(await expireSupporters(env, NOW)).toEqual({ removed: 0, granted: 0 });
    expect(Object.keys((await loadRecords(env)).users)).toEqual(["11111"]);
  });

  it("gives the role it could not give before", async () => {
    const f = fakeFetch(() => new Response(null, { status: 204 }));
    vi.stubGlobal("fetch", f.fn);
    const env = envWith(records({ "11111": { until: NOW + DAY, granted: false } }));
    expect(await expireSupporters(env, NOW)).toEqual({ removed: 0, granted: 1 });
    expect(f.calls[0].method).toBe("PUT");
    expect((await loadRecords(env)).users["11111"].granted).toBe(true);
  });
});

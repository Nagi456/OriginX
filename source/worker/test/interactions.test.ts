import { afterEach, beforeAll, describe, expect, it, vi } from "vitest";
import { handleInteraction } from "../src/interactions";
import type { Env } from "../src/env";
import { NOW, discordKeys, fakeFetch, json, testEnv } from "./helpers";

let keys: Awaited<ReturnType<typeof discordKeys>>;
let env: Env;
beforeAll(async () => {
  keys = await discordKeys();
  env = testEnv({ PUBLIC_KEY: keys.publicKey });
});
afterEach(() => vi.unstubAllGlobals());

async function send(body: unknown, signature?: string) {
  const text = JSON.stringify(body);
  const ts = String(NOW / 1000);
  const waits: Promise<unknown>[] = [];
  const req = new Request("https://site.example/discord/interactions", {
    method: "POST",
    headers: { "x-signature-ed25519": signature ?? (await keys.sign(ts, text)), "x-signature-timestamp": ts },
    body: text,
  });
  const res = await handleInteraction(req, env, { waitUntil: (p) => waits.push(p) }, NOW);
  await Promise.all(waits);
  return { res, waits, body: res.status === 200 ? await res.json() : null };
}

const member = (roles: string[], permissions = "0") => ({ roles, permissions, user: { id: "42", username: "u" } });

describe("POST /discord/interactions", () => {
  it("rejects a bad signature with 401", async () => {
    const { res } = await send({ type: 1 }, "00".repeat(64));
    expect(res.status).toBe(401);
  });

  it("answers PING with PONG and registers the commands", async () => {
    const f = fakeFetch(() => json([]));
    vi.stubGlobal("fetch", f.fn);
    const { body, waits } = await send({ type: 1 });
    expect(body).toEqual({ type: 1 });
    expect(waits.length).toBe(1);
    expect(f.calls[0].url).toContain("/applications/400/guilds/100/commands");
  });

  it("setup-verify posts the button for an admin", async () => {
    const { body } = await send({ type: 2, data: { name: "setup-verify" }, member: member([], "8") });
    expect(body.type).toBe(4);
    expect(body.data.flags).toBeUndefined();
    expect(body.data.components[0].components[0].custom_id).toBe("originx-verify");
  });

  it("setup-verify refuses a non-admin", async () => {
    const { body } = await send({ type: 2, data: { name: "setup-verify" }, member: member([], "0") });
    expect(body.data.flags).toBe(64);
    expect(body.data.components).toBeUndefined();
  });

  it("verify adds the member role", async () => {
    const f = fakeFetch(() => new Response(null, { status: 204 }));
    vi.stubGlobal("fetch", f.fn);
    const { body } = await send({ type: 3, data: { custom_id: "originx-verify" }, member: member([]) });
    expect(f.calls[0].method).toBe("PUT");
    expect(f.calls[0].url).toContain("/members/42/roles/200");
    expect(body.data.flags).toBe(64);
    expect(body.data.content).toContain("https://site.example/download/");
  });

  it("verify says already verified", async () => {
    const f = fakeFetch(() => new Response(null, { status: 204 }));
    vi.stubGlobal("fetch", f.fn);
    const { body } = await send({ type: 3, data: { custom_id: "originx-verify" }, member: member(["200"]) });
    expect(f.calls.length).toBe(0);
    expect(body.data.content).toContain("already verified");
  });

  it("tells the user when the role cannot be added", async () => {
    vi.stubGlobal("fetch", fakeFetch(() => json({ message: "Missing Permissions" }, 403)).fn);
    vi.spyOn(console, "error").mockImplementation(() => {});
    const { body } = await send({ type: 3, data: { custom_id: "originx-verify" }, member: member([]) });
    expect(body.type).toBe(4);
    expect(body.data.flags).toBe(64);
    expect(body.data.content).toContain("Verification failed");
  });

  it("does not wait out a rate limit inside the 3-second deadline", async () => {
    const f = fakeFetch(() => json({ retry_after: 4 }, 429, { "retry-after": "4" }));
    vi.stubGlobal("fetch", f.fn);
    vi.spyOn(console, "error").mockImplementation(() => {});
    const started = Date.now();
    const { body } = await send({ type: 3, data: { custom_id: "originx-verify" }, member: member([]) });
    expect(Date.now() - started).toBeLessThan(1000);
    expect(f.calls.length).toBe(1);
    expect(body.data.flags).toBe(64);
    expect(body.data.content).toContain("Verification failed");
  });
});

import { describe, expect, it } from "vitest";
import { b64url, fromB64url, hmac, hmacEqual, verifyDiscord } from "../src/crypto";
import { NOW, discordKeys } from "./helpers";

describe("base64url", () => {
  it("b64url round trip", () => {
    const all = Uint8Array.from({ length: 256 }, (_, i) => i);
    const text = b64url(all);
    expect(text).not.toMatch(/[+/=]/);
    expect(Array.from(fromB64url(text))).toEqual(Array.from(all));
  });
});

describe("hmac", () => {
  it("hmac matches and rejects", async () => {
    const sig = await hmac("k", "a");
    expect(await hmacEqual("k", "a", sig)).toBe(true);
    expect(await hmacEqual("k", "b", sig)).toBe(false);
    expect(await hmacEqual("other", "a", sig)).toBe(false);
    expect(await hmacEqual("k", "a", "!!")).toBe(false);
    expect(await hmacEqual("k", "a", "")).toBe(false);
  });
});

describe("verifyDiscord", () => {
  const ts = String(NOW / 1000);
  const body = '{"type":1}';

  it("verifyDiscord accepts a good signature", async () => {
    const keys = await discordKeys();
    expect(await verifyDiscord(keys.publicKey, await keys.sign(ts, body), ts, body, NOW)).toBe(true);
  });

  it("rejects a changed body", async () => {
    const keys = await discordKeys();
    expect(await verifyDiscord(keys.publicKey, await keys.sign(ts, body), ts, '{"type":2}', NOW)).toBe(false);
  });

  it("rejects a missing header", async () => {
    const keys = await discordKeys();
    const sig = await keys.sign(ts, body);
    expect(await verifyDiscord(keys.publicKey, null, ts, body, NOW)).toBe(false);
    expect(await verifyDiscord(keys.publicKey, sig, null, body, NOW)).toBe(false);
  });

  it("rejects a timestamp older than 5 minutes", async () => {
    const keys = await discordKeys();
    const old = String(NOW / 1000 - 301);
    expect(await verifyDiscord(keys.publicKey, await keys.sign(old, body), old, body, NOW)).toBe(false);
  });

  it("rejects a signature that is not hex", async () => {
    const keys = await discordKeys();
    expect(await verifyDiscord(keys.publicKey, "zz".repeat(64), ts, body, NOW)).toBe(false);
  });
});

import { describe, expect, it } from "vitest";
import { b64url, fromB64url } from "../src/crypto";
import { makeToken, needsRenewal, readToken } from "../src/token";
import { NOW } from "./helpers";

const SECRET = "secret-for-tokens";
const DAY = 86400 * 1000;

describe("OriginX tokens", () => {
  it("round trip", async () => {
    const token = await makeToken(SECRET, "123", NOW);
    const claims = await readToken(SECRET, token, NOW);
    expect(claims).toEqual({ sub: "123", iat: NOW / 1000, exp: NOW / 1000 + 7776000 });
  });

  it("rejects a changed payload", async () => {
    const [v, payload, sig] = (await makeToken(SECRET, "123", NOW)).split(".");
    const claims = JSON.parse(new TextDecoder().decode(fromB64url(payload)));
    claims.sub = "999";
    const forged = [v, b64url(new TextEncoder().encode(JSON.stringify(claims))), sig].join(".");
    expect(await readToken(SECRET, forged, NOW)).toBeNull();
  });

  it("rejects another secret", async () => {
    expect(await readToken("other", await makeToken(SECRET, "123", NOW), NOW)).toBeNull();
  });

  it("rejects an expired token", async () => {
    const token = await makeToken(SECRET, "123", NOW);
    expect(await readToken(SECRET, token, NOW + 90 * DAY + 1000)).toBeNull();
  });

  it("rejects garbage", async () => {
    const notJson = "v1." + b64url(new TextEncoder().encode("hello")) + ".x";
    for (const bad of ["", "v1.a", "v2.x.y", notJson, "v1.@@@.@@@"]) {
      expect(await readToken(SECRET, bad, NOW)).toBeNull();
    }
  });

  it("needsRenewal is true under 30 days and false above", async () => {
    const claims = (await readToken(SECRET, await makeToken(SECRET, "123", NOW), NOW))!;
    expect(needsRenewal(claims, NOW + 59 * DAY)).toBe(false);
    expect(needsRenewal(claims, NOW + 61 * DAY)).toBe(true);
  });
});

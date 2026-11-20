import { describe, expect, it } from "vitest";
import { loadTestKey, signTestCatalog, testCatalogShape } from "../src/manifest-sign";

const SEED_00_1F = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
const enc = (s: string) => new TextEncoder().encode(s).buffer as ArrayBuffer;
const fromHex = (h: string) => Uint8Array.from(h.match(/../g)!, (b) => parseInt(b, 16));
const b64 = (bytes: Uint8Array) => btoa(String.fromCharCode(...bytes));

describe("test catalog signing", () => {
  it("is the standard Ed25519 (RFC 8032 §7.1 test 1)", async () => {
    const key = await loadTestKey("101:" + b64(fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")));
    expect(key).not.toBeNull();
    const sig = new Uint8Array(await crypto.subtle.sign({ name: "Ed25519" }, key!.key, new Uint8Array(0)));
    expect(Array.from(sig, (b) => b.toString(16).padStart(2, "0")).join("")).toBe(
      "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
  });

  it("writes the same .sig as tools/signing.py (seed 00..1f, key 101)", async () => {
    const key = await loadTestKey("101:" + SEED_00_1F);
    const sig = await signTestCatalog(key!, enc('{"schemaVersion":1,"sequence":3,"versions":[]}\n'));
    expect(sig).toBe('{"schemaVersion": 1, "keyId": 101, "signature": "ZFINddlPbjTt/PY+J0N3/n/t++EXqmU5/Ye/idLpj4dTnp0+U4u5dnPHgebbJLmTYdPsTjRNpHnjLvazlgVHDg=="}\n');
  });

  it("refuses a missing or malformed secret, and stable key ids", async () => {
    for (const secret of [undefined, "", SEED_00_1F, "1:" + SEED_00_1F, "100:" + SEED_00_1F, "101:AAEC", "101:not*base64", "70000:" + SEED_00_1F]) {
      expect(await loadTestKey(secret)).toBeNull();
    }
    expect((await loadTestKey(" 102:" + SEED_00_1F + "\n"))?.id).toBe(102);
  });

  it("only signs something shaped like a channel list", () => {
    expect(testCatalogShape(enc('{"schemaVersion":1,"sequence":3,"versions":[]}'))).toBe(true);
    for (const bad of ["{", "[]", '{"schemaVersion":2,"sequence":3,"versions":[]}', '{"schemaVersion":1,"sequence":0,"versions":[]}',
      '{"schemaVersion":1,"sequence":"3","versions":[]}', '{"schemaVersion":1,"sequence":3}']) {
      expect(testCatalogShape(enc(bad))).toBe(false);
    }
  });
});

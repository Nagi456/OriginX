import { afterEach, describe, expect, it, vi } from "vitest";
import { handleCallback, handleLauncherLogin, handleLogin } from "../src/oauth";
import { readToken } from "../src/token";
import { NOW, fakeFetch, json, testEnv } from "./helpers";

const env = testEnv();
afterEach(() => vi.unstubAllGlobals());

function cookieOf(res: Response): string {
  return (res.headers.get("set-cookie") ?? "").split(";")[0];
}

async function startLogin(path: string) {
  const res = await (path.startsWith("/launcher")
    ? handleLauncherLogin(new Request("https://site.example" + path), env, NOW)
    : handleLogin(new Request("https://site.example" + path), env, NOW));
  const state = new URL(res.headers.get("location")!).searchParams.get("state")!;
  return { res, state, cookie: cookieOf(res) };
}

function callback(query: string, cookie?: string, now = NOW) {
  const headers = cookie ? { cookie } : undefined;
  return handleCallback(new Request("https://site.example/callback?" + query, { headers }), env, now);
}

// Discord answers for: token exchange, /users/@me, guild member lookup.
function discordSays(member: Response) {
  const f = fakeFetch(() => json({ access_token: "user-token" }), () => json({ id: "42" }), () => member);
  vi.stubGlobal("fetch", f.fn);
  return f;
}

describe("login", () => {
  it("login redirects to Discord with a state cookie", async () => {
    const { res, state } = await startLogin("/login");
    expect(res.status).toBe(302);
    const to = new URL(res.headers.get("location")!);
    expect(to.origin + to.pathname).toBe("https://discord.com/oauth2/authorize");
    expect(to.searchParams.get("client_id")).toBe("400");
    expect(to.searchParams.get("scope")).toBe("identify");
    expect(to.searchParams.get("redirect_uri")).toBe("https://site.example/callback");
    expect(state).toMatch(/^[0-9a-f]{32}$/);
    const setCookie = res.headers.get("set-cookie")!;
    expect(setCookie).toMatch(new RegExp(`^__Host-ox_oauth_${state}=`));
    for (const part of ["Path=/", "Secure", "HttpOnly", "SameSite=Lax", "Max-Age=600"]) expect(setCookie).toContain(part);
  });

  it("launcher login refuses a bad port or state", async () => {
    const good = "a".repeat(32);
    for (const q of [`port=80&state=${good}`, `port=abc&state=${good}`, `port=70000&state=${good}`, "port=5000&state=xyz"]) {
      const res = await handleLauncherLogin(new Request("https://site.example/launcher/login?" + q), env, NOW);
      expect(res.status).toBe(400);
    }
  });
});

describe("callback", () => {
  it("callback rejects a missing or changed cookie", async () => {
    const { state, cookie } = await startLogin("/login");
    expect((await callback(`code=c&state=${state}`)).status).toBe(400);
    const changed = cookie.replace(/.$/, (c) => (c === "A" ? "B" : "A"));
    expect((await callback(`code=c&state=${state}`, changed)).status).toBe(400);
  });

  it("callback rejects a state mismatch", async () => {
    const { cookie } = await startLogin("/login");
    expect((await callback(`code=c&state=${"b".repeat(32)}`, cookie)).status).toBe(400);
  });

  it("callback rejects an expired cookie", async () => {
    const { state, cookie } = await startLogin("/login");
    expect((await callback(`code=c&state=${state}`, cookie, NOW + 601 * 1000)).status).toBe(400);
  });

  it("callback shows the retry page when the user cancels on Discord", async () => {
    const { state, cookie } = await startLogin("/login");
    expect((await callback(`error=access_denied&state=${state}`, cookie)).status).toBe(400);
  });

  it("sends a verified member to the signed launcher link", async () => {
    const { state, cookie } = await startLogin("/login");
    discordSays(json({ roles: ["200"], user: { id: "42", username: "u" } }));
    const res = await callback(`code=c&state=${state}`, cookie);
    expect(res.status).toBe(302);
    expect(res.headers.get("location")).toMatch(/^https:\/\/site\.example\/file\/launcher\/MyClientLauncher\.exe\?exp=\d+&sig=/);
  });

  it("sends a non-member to need=verify", async () => {
    const { state, cookie } = await startLogin("/login");
    discordSays(json({ message: "Unknown Member" }, 404));
    const res = await callback(`code=c&state=${state}`, cookie);
    expect(res.headers.get("location")).toBe("https://site.example/download/?need=verify");
  });

  it("sends a member without the role to need=verify", async () => {
    const { state, cookie } = await startLogin("/login");
    discordSays(json({ roles: ["999"], user: { id: "42", username: "u" } }));
    const res = await callback(`code=c&state=${state}`, cookie);
    expect(res.headers.get("location")).toBe("https://site.example/download/?need=verify");
  });

  it("sends an English visitor to the English page", async () => {
    const { state, cookie } = await startLogin("/login?lang=en");
    discordSays(json({ message: "Unknown Member" }, 404));
    const res = await callback(`code=c&state=${state}`, cookie);
    expect(res.headers.get("location")).toBe("https://site.example/en/download/?need=verify");
  });

  it("gives the launcher a token on 127.0.0.1", async () => {
    const lstate = "c".repeat(32);
    const { state, cookie } = await startLogin(`/launcher/login?port=50123&state=${lstate}`);
    const f = fakeFetch(() => json({ access_token: "user-token" }), () => json({ id: "42" }));
    vi.stubGlobal("fetch", f.fn);
    const res = await callback(`code=c&state=${state}`, cookie);
    const to = new URL(res.headers.get("location")!);
    expect(to.origin + to.pathname).toBe("http://127.0.0.1:50123/callback");
    expect(to.searchParams.get("state")).toBe(lstate);
    const claims = await readToken(env.SIGNING_KEY, to.searchParams.get("token")!, NOW);
    expect(claims?.sub).toBe("42");
    expect(f.calls.length).toBe(2);
  });

  it("answers 503 when Discord is down", async () => {
    const { state, cookie } = await startLogin("/login");
    vi.stubGlobal("fetch", fakeFetch(() => json({}, 502)).fn);
    expect((await callback(`code=c&state=${state}`, cookie)).status).toBe(503);
  });

  it("two logins in two tabs both work", async () => {
    const first = await startLogin("/login");
    const second = await startLogin("/login");
    expect(first.cookie.split("=")[0]).not.toBe(second.cookie.split("=")[0]);
    discordSays(json({ roles: ["200"], user: { id: "42", username: "u" } }));
    const both = `${first.cookie}; ${second.cookie}`;
    const res = await callback(`code=c&state=${first.state}`, both);
    expect(res.status).toBe(302);
    expect(res.headers.get("set-cookie")).toMatch(new RegExp(`^__Host-ox_oauth_${first.state}=;`));
  });

  it("clears the cookie on every callback answer", async () => {
    const { state, cookie } = await startLogin("/login");
    discordSays(json({ roles: ["200"], user: { id: "42", username: "u" } }));
    for (const res of [await callback("code=c&state=x"), await callback(`code=c&state=${state}`, cookie)]) {
      expect(res.headers.get("set-cookie")).toContain("Max-Age=0");
    }
  });

  it("treats a rejected client secret as a server problem and logs it", async () => {
    const { state, cookie } = await startLogin("/login");
    vi.stubGlobal("fetch", fakeFetch(() => json({ error: "invalid_client" }, 401)).fn);
    const logged = vi.spyOn(console, "error").mockImplementation(() => {});
    const res = await callback(`code=c&state=${state}`, cookie);
    expect(res.status).toBe(503);
    expect(logged).toHaveBeenCalled();
  });
});

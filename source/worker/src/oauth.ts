// Logging in with Discord, for the site (download the launcher) and for the
// launcher (get an OriginX token). Both come back to the one redirect URI,
// /callback; a signed cookie remembers which login it was. The cookie is named
// after the login's state, so two logins in two tabs do not overwrite each other.

import type { Env } from "./env";
import { b64url, fromB64url, hmac, hmacEqual } from "./crypto";
import { DiscordError, exchangeCode, getMember, getUserId } from "./discord";
import { makeToken } from "./token";
import { signedFileUrl } from "./links";

const COOKIE = "__Host-ox_oauth";
const COOKIE_SECONDS = 600;
const LAUNCHER_KEY = "launcher/MyClientLauncher.exe";

type Start = { flow: "site"; lang: "ja" | "en" } | { flow: "launcher"; port: number; lstate: string };
type Pending = Start & { state: string; exp: number };

function randomHex(bytes: number): string {
  return Array.from(crypto.getRandomValues(new Uint8Array(bytes)), (b) => b.toString(16).padStart(2, "0")).join("");
}

function redirectUri(env: Env): string {
  return `${env.SITE_URL}/callback`;
}

const STATE = /^[0-9a-f]{32}$/;

function cookieName(state: string): string {
  return STATE.test(state) ? `${COOKIE}_${state}` : COOKIE;
}

function clearCookie(state = ""): string {
  return `${cookieName(state)}=; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=0`;
}

function page(status: number, html: string, cookie = clearCookie()): Response {
  const body = `<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>OriginX</title>` +
    `<body style="background:#05060a;color:#e8ecf4;font-family:sans-serif;padding:32px;line-height:1.7">${html}</body>`;
  return new Response(body, {
    status,
    headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store", "set-cookie": cookie },
  });
}

function redirect(location: string, cookie = clearCookie()): Response {
  return new Response(null, { status: 302, headers: { location, "set-cookie": cookie, "cache-control": "no-store" } });
}

function loginAgain(env: Env, cookie: string): Response {
  return page(400, `<p>ログインをやり直してください。<br>Please log in again.</p><p><a style="color:#227cff" href="${env.SITE_URL}/download/">${env.SITE_URL}/download/</a></p>`, cookie);
}

async function startLogin(env: Env, pending: Start, now: number): Promise<Response> {
  const state = randomHex(16);
  const json = JSON.stringify({ ...pending, state, exp: Math.floor(now / 1000) + COOKIE_SECONDS });
  const value = `${b64url(new TextEncoder().encode(json))}.${await hmac(env.SIGNING_KEY, "oauth\n" + json)}`;
  const to = new URL("https://discord.com/oauth2/authorize");
  to.searchParams.set("client_id", env.CLIENT_ID);
  to.searchParams.set("response_type", "code");
  to.searchParams.set("scope", "identify");
  to.searchParams.set("redirect_uri", redirectUri(env));
  to.searchParams.set("state", state);
  return redirect(to.toString(), `${cookieName(state)}=${value}; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=${COOKIE_SECONDS}`);
}

export function handleLogin(request: Request, env: Env, now: number): Promise<Response> {
  const lang = new URL(request.url).searchParams.get("lang") === "en" ? "en" : "ja";
  return startLogin(env, { flow: "site", lang }, now);
}

export async function handleLauncherLogin(request: Request, env: Env, now: number): Promise<Response> {
  const query = new URL(request.url).searchParams;
  const portText = query.get("port") ?? "";
  const port = /^\d{1,5}$/.test(portText) ? Number(portText) : 0;
  const lstate = query.get("state") ?? "";
  if (port < 1024 || port > 65535 || !/^[0-9a-f]{32,128}$/.test(lstate)) {
    return page(400, "<p>ランチャーからやり直してください。<br>Please start again from the launcher.</p>");
  }
  return startLogin(env, { flow: "launcher", port, lstate }, now);
}

async function readPending(request: Request, env: Env, now: number, state: string): Promise<Pending | null> {
  if (!STATE.test(state)) return null;
  const name = cookieName(state);
  const cookies = request.headers.get("cookie") ?? "";
  const raw = cookies.split(/;\s*/).find((c) => c.startsWith(name + "="))?.slice(name.length + 1);
  if (!raw) return null;
  const [payload, sig] = raw.split(".");
  if (!payload || !sig) return null;
  try {
    const json = new TextDecoder().decode(fromB64url(payload));
    if (!(await hmacEqual(env.SIGNING_KEY, "oauth\n" + json, sig))) return null;
    const pending = JSON.parse(json) as Pending;
    return pending.exp > now / 1000 ? pending : null;
  } catch {
    return null;
  }
}

export async function handleCallback(request: Request, env: Env, now: number): Promise<Response> {
  const query = new URL(request.url).searchParams;
  const state = query.get("state") ?? "";
  const clear = clearCookie(state);
  const pending = await readPending(request, env, now, state);
  const code = query.get("code");
  if (!pending || !code || state !== pending.state) return loginAgain(env, clear);

  try {
    const userId = await getUserId(await exchangeCode(env, code, redirectUri(env)));

    if (pending.flow === "launcher") {
      const token = await makeToken(env.SIGNING_KEY, userId, now);
      return redirect(`http://127.0.0.1:${pending.port}/callback?state=${pending.lstate}&token=${encodeURIComponent(token)}`, clear);
    }

    const member = await getMember(env, userId);
    if (!member || !member.roles.includes(env.MEMBER_ROLE_ID)) {
      return redirect(`${env.SITE_URL}${pending.lang === "en" ? "/en" : ""}/download/?need=verify`, clear);
    }
    return redirect(await signedFileUrl(env, LAUNCHER_KEY, 300, now), clear);
  } catch (e) {
    if (e instanceof DiscordError && e.status === 400) {
      console.error("login callback: Discord refused the login", e.message);
      return loginAgain(env, clear);
    }
    console.error("login callback failed", e);
    return page(503, "<p>Discord に接続できませんでした。少し待ってからやり直してください。<br>Could not reach Discord. Please try again in a moment.</p>", clear);
  }
}

// The OriginX Worker: the API paths below run here; every other path is the
// static site (wrangler.jsonc sends only these paths to the Worker first).

import type { Ctx, Env } from "./env";
import { handleFile } from "./files";
import { handleInteraction } from "./interactions";
import { expireSupporters, handleKofi } from "./kofi";
import { handleDownload, handlePublish, handlePublishRead } from "./publish";
import { handlePromote } from "./promote";
import { handleCallback, handleLauncherLogin, handleLogin } from "./oauth";
import { handleStatus } from "./status";

// The secrets live in the dashboard. Until all three are there (and the
// signing key is long enough to be a key), the API stays closed instead of
// signing with an empty or guessable key.
function secretsReady(env: Env): boolean {
  return !!env.BOT_TOKEN && !!env.CLIENT_SECRET && typeof env.SIGNING_KEY === "string" && env.SIGNING_KEY.length >= 32;
}

const API_PATHS = /^\/(discord\/|login$|launcher\/|callback$|file\/)/;

async function route(request: Request, env: Env, ctx: Ctx): Promise<Response> {
  const { pathname } = new URL(request.url);
  const get = request.method === "GET" || request.method === "HEAD";
  // Release files work without the Discord secrets (publishing has its own key).
  if (get && pathname.startsWith("/dl/")) return handleDownload(request, env);
  if (request.method === "PUT" && pathname.startsWith("/publish/")) return handlePublish(request, env);
  if (request.method === "POST" && pathname === "/publish/promote") return handlePromote(request, env);
  if (get && pathname.startsWith("/publish/")) return handlePublishRead(request, env);
  // Ko-fi needs only the bot token and its own KOFI_TOKEN (checked inside).
  if (request.method === "POST" && pathname === "/kofi/webhook") {
    if (!env.BOT_TOKEN) return new Response("not ready", { status: 503 });
    return handleKofi(request, env, Date.now());
  }
  if (API_PATHS.test(pathname) && !secretsReady(env)) {
    console.error("the Worker's secrets are not set (BOT_TOKEN, CLIENT_SECRET, SIGNING_KEY of 32+ characters)");
    return new Response("OriginX のサーバーは準備中です。/ OriginX is not ready yet.", {
      status: 503,
      headers: { "content-type": "text/plain; charset=utf-8" },
    });
  }
  const now = Date.now();
  if (request.method === "POST" && pathname === "/discord/interactions") return handleInteraction(request, env, ctx, now);
  if (get && pathname === "/login") return handleLogin(request, env, now);
  if (get && pathname === "/launcher/login") return handleLauncherLogin(request, env, now);
  if (get && pathname === "/callback") return handleCallback(request, env, now);
  if (get && pathname === "/launcher/status") return handleStatus(request, env, now);
  if (get && pathname.startsWith("/file/")) return handleFile(request, env, now);
  return env.ASSETS.fetch(request);
}

export default {
  async fetch(request: Request, env: Env, ctx: Ctx): Promise<Response> {
    try {
      return await route(request, env, ctx);
    } catch (e) {
      console.error("unhandled error", new URL(request.url).pathname, e);
      return new Response("Something went wrong. / エラーが起きました。", {
        status: 500,
        headers: { "content-type": "text/plain; charset=utf-8" },
      });
    }
  },

  // Hourly (wrangler.jsonc triggers): Ko-fi supporter days that ran out.
  async scheduled(_event: unknown, env: Env, ctx: Ctx): Promise<void> {
    if (!env.BOT_TOKEN) return;
    ctx.waitUntil(expireSupporters(env, Date.now()).then(() => undefined));
  },
};

// GET /launcher/status: the launcher asks with its OriginX token which roles the
// user has. Anyone allowed gets the catalog of each release channel they may use
// (early = supporters and moderators, test = moderators only) with signed download links.

import type { Env } from "./env";
import { DiscordError, getMember } from "./discord";
import { signedFileUrl } from "./links";
import { makeToken, needsRenewal, readToken } from "./token";

function answer(body: unknown, status = 200): Response {
  return Response.json(body, { status, headers: { "cache-control": "no-store" } });
}

// The catalog keeps R2 keys in each file's url; swap them for links that work
// for 30 minutes. Anything that is not the expected shape gives null.
export async function rewriteCatalog(catalog: unknown, env: Env, now: number): Promise<unknown | null> {
  if (!catalog || typeof catalog !== "object") return null;
  const versions = (catalog as { versions?: unknown }).versions;
  if (!Array.isArray(versions)) return null;
  for (const version of versions) {
    const files = version?.files;
    if (!files || typeof files !== "object") continue;
    for (const file of Object.values(files) as Array<{ url?: unknown }>) {
      if (file && typeof file.url === "string") file.url = await signedFileUrl(env, file.url, 1800, now);
    }
  }
  return catalog;
}

// Who may use which release channel (verified = has the member role).
export function channelAccess(roles: { verified: boolean; supporter: boolean; moderator: boolean }): { early: boolean; test: boolean } {
  return { early: (roles.verified && roles.supporter) || roles.moderator, test: roles.moderator };
}

export async function readChannelCatalog(env: Env, channel: "early" | "test", now: number): Promise<unknown | null> {
  // R2 failing (or a broken list) only means no catalog for this channel this time, not a failed answer.
  try {
    const object = await env.FILES.get(`${channel}/catalog.json`);
    if (!object) return null;
    return await rewriteCatalog(JSON.parse(await object.text()), env, now);
  } catch (e) {
    console.error(`status: ${channel} catalog unavailable`, e);
    return null;
  }
}

export async function handleStatus(request: Request, env: Env, now: number): Promise<Response> {
  const auth = request.headers.get("authorization") ?? "";
  const claims = auth.startsWith("Bearer ") ? await readToken(env.SIGNING_KEY, auth.slice(7), now) : null;
  if (!claims) return answer({ error: "login" }, 401);

  let member;
  try {
    member = await getMember(env, claims.sub);
  } catch (e) {
    console.error("status: member lookup failed", claims.sub, e instanceof DiscordError ? e.status : e);
    return answer({ error: "discord" }, 503);
  }

  const roles = {
    verified: !!member?.roles.includes(env.MEMBER_ROLE_ID),
    supporter: !!member?.roles.includes(env.SUPPORTER_ROLE_ID),
    moderator: !!member?.roles.includes(env.MODERATOR_ROLE_ID),
  };
  const access = channelAccess(roles);
  const body: Record<string, unknown> = {
    user: { id: claims.sub, name: member?.name ?? null, avatarUrl: member?.avatarUrl ?? null },
    roles,
    channels: {
      early: access.early ? await readChannelCatalog(env, "early", now) : null,
      test: access.test ? await readChannelCatalog(env, "test", now) : null,
    },
  };
  if (needsRenewal(claims, now)) body.token = await makeToken(env.SIGNING_KEY, claims.sub, now);
  return answer(body);
}

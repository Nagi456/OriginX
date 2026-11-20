// GET /launcher/status: the launcher asks with its OriginX token which roles the
// user has. Anyone allowed gets the catalog of each release channel they may use
// (early = creators, partners, developers and moderators, test = moderators only) with signed download links.

import type { Env } from "./env";
import { capesFor, hasRole } from "./capes";
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
// Early builds are for creators and partners (2026-11-17 owner decision; supporters no longer get them).
export function channelAccess(roles: { verified: boolean; moderator: boolean; creator: boolean; partner: boolean; developer: boolean }): { early: boolean; test: boolean } {
  return { early: (roles.verified && (roles.creator || roles.partner || roles.developer)) || roles.moderator, test: roles.moderator };
}

// The list as stored in R2 (rewritten for the answer) and, for the launcher's signature check, its exact bytes and
// its .sig (design 2026-11-20-manifest-signing-design.md §3). signed is null when either is missing.
export async function readChannelCatalog(env: Env, channel: "early" | "test", now: number): Promise<{ catalog: unknown | null; signed: { catalog: string; signature: string } | null }> {
  // R2 failing (or a broken list) only means no catalog for this channel this time, not a failed answer.
  try {
    const object = await env.FILES.get(`${channel}/catalog.json`);
    if (!object) return { catalog: null, signed: null };
    const bytes = new Uint8Array(await new Response(object.body).arrayBuffer());
    const sig = await env.FILES.get(`${channel}/catalog.json.sig`);
    const signature = sig ? await sig.text() : null;
    const catalog = await rewriteCatalog(JSON.parse(new TextDecoder().decode(bytes)), env, now);
    return { catalog, signed: catalog && signature ? { catalog: base64(bytes), signature } : null };
  } catch (e) {
    console.error(`status: ${channel} catalog unavailable`, e);
    return { catalog: null, signed: null };
  }
}

// The test channel's launcher (kind launcher/test): the signed list as stored in R2 and its .sig, plus a 30-minute link to
// the exe it names. Only for accounts that may use test; the launcher checks the signature and that the list says
// channel "test" before it uses it. Null when anything is missing or not the expected shape.
export async function readTestLauncher(env: Env, now: number): Promise<{ list: string; signature: string; url: string } | null> {
  try {
    const object = await env.FILES.get("test/launcher.json");
    const sig = await env.FILES.get("test/launcher.json.sig");
    if (!object || !sig) return null;
    const bytes = new Uint8Array(await new Response(object.body).arrayBuffer());
    const doc = JSON.parse(new TextDecoder().decode(bytes)) as { key?: unknown; build?: unknown };
    if (typeof doc.key !== "string" || typeof doc.build !== "number" || doc.key !== `test/launcher/${doc.build}/MyClientLauncher.exe`) return null;
    return { list: base64(bytes), signature: await sig.text(), url: await signedFileUrl(env, doc.key, 1800, now) };
  } catch (e) {
    console.error("status: test launcher unavailable", e);
    return null;
  }
}

function base64(bytes: Uint8Array): string {
  let text = "";
  for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(text);
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
    founder: hasRole(member?.roles, env.FOUNDER_ROLE_ID),
    supporterCape: hasRole(member?.roles, env.SUPPORTER_CAPE_ROLE_ID),
    creator: hasRole(member?.roles, env.CREATOR_ROLE_ID),
    partner: hasRole(member?.roles, env.PARTNER_ROLE_ID),
    developer: hasRole(member?.roles, env.DEVELOPER_ROLE_ID),
  };
  const access = channelAccess(roles);
  const body: Record<string, unknown> = {
    user: { id: claims.sub, name: member?.name ?? null, avatarUrl: member?.avatarUrl ?? null },
    roles: { ...roles, capes: capesFor(roles) },
  };
  const early = access.early ? await readChannelCatalog(env, "early", now) : { catalog: null, signed: null };
  const test = access.test ? await readChannelCatalog(env, "test", now) : { catalog: null, signed: null };
  body.channels = { early: early.catalog, test: test.catalog };
  body.signedChannels = { early: early.signed, test: test.signed };
  body.testLauncher = access.test ? await readTestLauncher(env, now) : null;
  if (needsRenewal(claims, now)) body.token = await makeToken(env.SIGNING_KEY, claims.sub, now);
  return answer(body);
}

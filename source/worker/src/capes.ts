// OriginX capes (docs/superpowers/specs/2026-11-12-capes-design.md): which capes a Discord member may wear.
import type { Env } from "./env";
import { getMember } from "./discord";
import { readToken } from "./token";
// 1 = Founding (founder role), 2 = Standard (every member), 3 = Supporter (supporter cape role), 4 = Creator,
// 5 = Moderator and 6 = Nebula (moderator role).

export type CapeId = 1 | 2 | 3 | 4 | 5 | 6;
export interface CapeRoles { verified: boolean; founder: boolean; supporterCape: boolean; creator: boolean; moderator?: boolean; supporter?: boolean; partner?: boolean; developer?: boolean }

export function markTier(roles: CapeRoles): number {
  if (!roles.verified) return 0;
  if (roles.developer) return 7;
  if (roles.moderator) return 6;
  if (roles.partner) return 5;
  if (roles.creator) return 4;
  if (roles.founder) return 3;
  if (roles.supporter || roles.supporterCape) return 2;
  return 1;
}

export function capesFor(roles: CapeRoles): CapeId[] {
  if (!roles.verified) return [];
  const out: CapeId[] = [];
  if (roles.founder) out.push(1);
  out.push(2);
  if (roles.supporterCape) out.push(3);
  if (roles.creator) out.push(4);
  if (roles.moderator) out.push(5, 6);   // Moderator and Nebula: moderators only
  return out;
}

// A role the member has; an empty or unset role id is never had (the role is not made yet).
export function hasRole(roles: string[] | undefined, id: string | undefined): boolean {
  return !!id && !!roles?.includes(id);
}

const DAY_MS = 24 * 60 * 60 * 1000;
export const FOUNDER_DAYS = 90;

// The cape role a Ko-fi donation at `donatedAt` earns: founder before the launch day or within FOUNDER_DAYS of it (UTC),
// supporter cape after. null when the launch day or that role id is not set (given by hand then).
export function capeRoleFor(env: Env, donatedAt: number): string | null {
  const day = env.PUBLIC_LAUNCH_DATE ?? "";
  if (!/^\d{4}-\d{2}-\d{2}$/.test(day)) return null;
  const launch = Date.parse(`${day}T00:00:00Z`);
  if (Number.isNaN(launch)) return null;
  const founder = donatedAt < launch + FOUNDER_DAYS * DAY_MS;
  const id = founder ? env.FOUNDER_ROLE_ID : env.SUPPORTER_CAPE_ROLE_ID;
  return id ? id : null;
}

// --- Who wears which cape (OriginX users see each other's capes; nobody else) -----------------------------
// POST /capes/register {xuid, cape}: the logged-in member says which cape they wear (0 = none) for their Xbox
// account. POST /capes/lookup {xuids}: the capes of up to MAX_LOOKUP players. Answers hold XUIDs and cape ids only.
export const MAX_LOOKUP = 64;
export const MAX_RECHECKS = 5;   // Discord member lookups a single cape lookup may make (subrequest and rate limits)
const RECHECK_MS = DAY_MS;   // a registration's roles are asked again after a day

// One R2 object per XUID and per account (review I1: a single shared registry lost writes when two members registered
// at once). "{}" marks a removed record (the bucket binding has no delete here).
interface CapeEntry { discordId: string; cape: CapeId | 0; checkedAt: number; hideCape?: boolean; hideMark?: boolean; markTier?: number }
export const xuidKey = (xuid: string) => `capes/xuid/${xuid}.json`;
const userKey = (id: string) => `capes/user/${id}.json`;
async function readJson<T>(env: Env, key: string): Promise<T | null> {
  const obj = await env.FILES.get(key);
  if (!obj) return null;
  try {
    const value = JSON.parse(await obj.text()) as T & object;
    return value && Object.keys(value).length ? value : null;
  } catch {
    return null;
  }
}
async function writeJson(env: Env, key: string, value: unknown): Promise<void> {
  await env.FILES.put(key, new TextEncoder().encode(JSON.stringify(value ?? {})).buffer as ArrayBuffer);
}
const readEntry = (env: Env, xuid: string) => readJson<CapeEntry>(env, xuidKey(xuid));

function reply(body: unknown, status = 200): Response {
  return Response.json(body, { status, headers: { "cache-control": "no-store" } });
}
const XUID = /^\d{1,20}$/;

function rolesOf(env: Env, roles: string[] | undefined): CapeRoles {
  return {
    verified: hasRole(roles, env.MEMBER_ROLE_ID),
    supporter: hasRole(roles, env.SUPPORTER_ROLE_ID),
    partner: hasRole(roles, env.PARTNER_ROLE_ID),
    developer: hasRole(roles, env.DEVELOPER_ROLE_ID),
    founder: hasRole(roles, env.FOUNDER_ROLE_ID),
    supporterCape: hasRole(roles, env.SUPPORTER_CAPE_ROLE_ID),
    creator: hasRole(roles, env.CREATOR_ROLE_ID),
    moderator: hasRole(roles, env.MODERATOR_ROLE_ID),
  };
}

// The caller's Discord id from the OriginX token, or a Response to send back.
async function caller(request: Request, env: Env, now: number): Promise<string | Response> {
  const auth = request.headers.get("authorization") ?? "";
  const claims = auth.startsWith("Bearer ") ? await readToken(env.SIGNING_KEY, auth.slice(7), now) : null;
  return claims ? claims.sub : reply({ error: "login" }, 401);
}
async function bodyOf(request: Request): Promise<Record<string, unknown> | null> {
  try {
    const body = (await request.json()) as unknown;
    return body && typeof body === "object" ? (body as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

export async function handleCapeRegister(request: Request, env: Env, now: number): Promise<Response> {
  const who = await caller(request, env, now);
  if (who instanceof Response) return who;
  const body = await bodyOf(request);
  const xuid = body?.xuid, cape = body?.cape;
  if (typeof xuid !== "string" || !XUID.test(xuid) || typeof cape !== "number" || !Number.isInteger(cape)) return reply({ error: "bad request" }, 400);
  if ((body?.hideCape !== undefined && typeof body.hideCape !== "boolean") || (body?.hideMark !== undefined && typeof body.hideMark !== "boolean")) return reply({ error: "bad request" }, 400);
  let member;
  try { member = await getMember(env, who); } catch { return reply({ error: "discord" }, 503); }
  const allowed = capesFor(rolesOf(env, member?.roles));
  if (!rolesOf(env, member?.roles).verified) return reply({ error: "members only" }, 403);
  if (cape !== 0 && !allowed.includes(cape as CapeId)) return reply({ error: "not yours" }, 403);
  const previous = (await readJson<{ xuid: string }>(env, userKey(who)))?.xuid;
  if (previous && previous !== xuid) {
    const old = await readEntry(env, previous);
    if (old?.discordId === who) await writeJson(env, xuidKey(previous), {});
  }
  const other = await readEntry(env, xuid);
  if (other && other.discordId !== who) await writeJson(env, userKey(other.discordId), {});
  await writeJson(env, xuidKey(xuid), { discordId: who, cape, checkedAt: now,
    hideCape: body?.hideCape ?? false, hideMark: body?.hideMark ?? false, markTier: markTier(rolesOf(env, member?.roles)) });
  await writeJson(env, userKey(who), { xuid });
  return reply({ ok: true });
}

export async function handleCapeLookup(request: Request, env: Env, now: number): Promise<Response> {
  const who = await caller(request, env, now);
  if (who instanceof Response) return who;
  const body = await bodyOf(request);
  const xuids = body?.xuids;
  if (!Array.isArray(xuids) || xuids.length < 1 || xuids.length > MAX_LOOKUP || !xuids.every((x) => typeof x === "string" && XUID.test(x)))
    return reply({ error: "bad request" }, 400);
  let asker;
  try { asker = await getMember(env, who); } catch { return reply({ error: "discord" }, 503); }
  if (!rolesOf(env, asker?.roles).verified) return reply({ error: "members only" }, 403);
  const capes: Record<string, number> = {};
  const marks: Record<string, number> = {};
  let rechecks = 0;
  for (const xuid of xuids as string[]) {
    const entry = await readEntry(env, xuid);
    if (!entry) continue;
    if ((now - entry.checkedAt > RECHECK_MS || entry.markTier === undefined) && rechecks < MAX_RECHECKS) {
      rechecks++;
      try {
        const member = await getMember(env, entry.discordId);
        if (!rolesOf(env, member?.roles).verified) {
          await writeJson(env, xuidKey(xuid), {});
          await writeJson(env, userKey(entry.discordId), {});
          continue;
        }
        if (entry.cape !== 0 && !capesFor(rolesOf(env, member?.roles)).includes(entry.cape)) entry.cape = 0;
        entry.markTier = markTier(rolesOf(env, member?.roles));
        await writeJson(env, xuidKey(xuid), { ...entry, checkedAt: now });
      } catch {
        // Discord cannot be asked now: keep what was registered.
      }
    }
    if (entry.cape && !entry.hideCape) capes[xuid] = entry.cape;
    if ((!entry.hideMark || entry.discordId === who) && Number.isInteger(entry.markTier) && entry.markTier! >= 1 && entry.markTier! <= 15) marks[xuid] = entry.markTier!;
  }
  return reply({ capes, marks });
}

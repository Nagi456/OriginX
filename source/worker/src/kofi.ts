// Ko-fi one-off donations give the supporter role for 30 days.
//
// POST /kofi/webhook: Ko-fi sends each payment as a form field `data` holding
// JSON. We check its verification token (secret KOFI_TOKEN), add 30 days to
// the donor's time (a second donation adds to what is left), and give the
// role. Ko-fi only knows the donor's Discord id when they linked Discord on
// Ko-fi, so a donation without one is logged and skipped.
//
// The hourly cron (scheduled) takes the role back once the time is up, and
// gives it again to anyone whose role could not be added at donation time.
// Roles given by hand (no record here) are never touched. Someone who already
// had the role before their first donation is marked `keep`: their days are
// still counted, but the role stays when the days are over.

import type { Env } from "./env";
import { addRole, getMember, removeRole } from "./discord";

export const SUPPORT_DAYS = 30;
const DAY = 24 * 60 * 60 * 1000;
export const RECORD_KEY = "supporters/records.json";
const SEEN_LIMIT = 200;

export interface SupporterRecord {
  until: number; // ms since 1970: the role goes after this
  granted: boolean; // the role was added on Discord
  name?: string;
  keep?: boolean; // had the role before donating (given by hand): never removed
}

export interface Records {
  users: Record<string, SupporterRecord>;
  seen: string[]; // recent Ko-fi transaction ids, so Ko-fi's retries count once
}

export async function loadRecords(env: Env): Promise<Records> {
  const obj = await env.FILES.get(RECORD_KEY);
  if (!obj) return { users: {}, seen: [] };
  try {
    const parsed = JSON.parse(await obj.text()) as Partial<Records>;
    return {
      users: parsed.users && typeof parsed.users === "object" ? parsed.users : {},
      seen: Array.isArray(parsed.seen) ? parsed.seen : [],
    };
  } catch {
    console.error("Ko-fi: supporters/records.json is broken, starting over");
    return { users: {}, seen: [] };
  }
}

async function saveRecords(env: Env, records: Records): Promise<void> {
  const bytes = new TextEncoder().encode(JSON.stringify(records));
  await env.FILES.put(RECORD_KEY, bytes.buffer as ArrayBuffer);
}

function sameText(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

interface KofiPayment {
  verification_token?: unknown;
  type?: unknown;
  kofi_transaction_id?: unknown;
  message_id?: unknown;
  from_name?: unknown;
  discord_userid?: unknown;
}

const text = (body: string, status = 200) =>
  new Response(body, { status, headers: { "content-type": "text/plain; charset=utf-8" } });

export async function handleKofi(request: Request, env: Env, now: number): Promise<Response> {
  if (!env.KOFI_TOKEN) {
    console.error("Ko-fi: the KOFI_TOKEN secret is not set");
    return text("not ready", 503);
  }
  let payment: KofiPayment;
  try {
    const form = await request.formData();
    const data = form.get("data");
    payment = JSON.parse(typeof data === "string" ? data : "") as KofiPayment;
  } catch {
    return text("bad request", 400);
  }
  if (typeof payment.verification_token !== "string" || !sameText(payment.verification_token, env.KOFI_TOKEN)) {
    return text("unauthorized", 401);
  }
  // Only one-off donations count; anything else is acknowledged and ignored.
  if (payment.type !== "Donation") return text("ignored");
  const userId = typeof payment.discord_userid === "string" && /^\d{5,25}$/.test(payment.discord_userid) ? payment.discord_userid : null;
  const name = typeof payment.from_name === "string" ? payment.from_name.slice(0, 64) : undefined;
  if (!userId) {
    console.log(`Ko-fi: a donation from "${name ?? "?"}" without a linked Discord account, no role given`);
    return text("no discord");
  }
  const txId = String(payment.kofi_transaction_id ?? payment.message_id ?? "");

  const records = await loadRecords(env);
  if (txId && records.seen.includes(txId)) return text("already counted");
  const old = records.users[userId];
  const until = Math.max(now, old?.until ?? 0) + SUPPORT_DAYS * DAY;
  // A first donation from someone who already has the role: it was given by
  // hand, so remember not to take it back later.
  let keep = old?.keep === true;
  if (!old) {
    try {
      const member = await getMember(env, userId);
      keep = member?.roles.includes(env.SUPPORTER_ROLE_ID) ?? false;
    } catch (e) {
      console.error(`Ko-fi: could not check whether ${userId} already had the supporter role`, e);
    }
  }
  const record: SupporterRecord = { until, granted: false, name };
  if (keep) record.keep = true;
  records.users[userId] = record;
  if (txId) records.seen = [...records.seen, txId].slice(-SEEN_LIMIT);
  // Save before talking to Discord, so a Discord failure still leaves the
  // donation counted (the cron gives the role later).
  await saveRecords(env, records);

  try {
    record.granted = await addRole(env, userId, env.SUPPORTER_ROLE_ID);
    if (!record.granted) console.error(`Ko-fi: could not give ${userId} the supporter role (not in the server, or the bot's role is too low)`);
  } catch (e) {
    console.error(`Ko-fi: giving ${userId} the supporter role failed, the cron will retry`, e);
  }
  if (record.granted) await saveRecords(env, records);
  console.log(`Ko-fi: ${userId} is a supporter until ${new Date(until).toISOString()}${keep ? " (had the role before, it stays after that)" : ""}`);
  return text("ok");
}

// The hourly job: take back roles whose time is up, retry roles not yet given.
export async function expireSupporters(env: Env, now: number): Promise<{ removed: number; granted: number }> {
  const records = await loadRecords(env);
  let removed = 0;
  let granted = 0;
  let changed = false;
  for (const [userId, record] of Object.entries(records.users)) {
    try {
      if (record.until <= now && record.keep) {
        // The role was theirs before they donated: only forget the days.
        delete records.users[userId];
        changed = true;
        console.log(`Ko-fi: ${userId}'s 30 supporter days are over, role kept (it was given by hand)`);
      } else if (record.until <= now) {
        // False means they left the server: nothing left to take back.
        await removeRole(env, userId, env.SUPPORTER_ROLE_ID);
        delete records.users[userId];
        removed++;
        changed = true;
        console.log(`Ko-fi: ${userId}'s 30 supporter days are over, role removed`);
      } else if (!record.granted) {
        if (await addRole(env, userId, env.SUPPORTER_ROLE_ID)) {
          record.granted = true;
          granted++;
          changed = true;
        }
      }
    } catch (e) {
      console.error(`Ko-fi: updating ${userId}'s supporter role failed, next hour tries again`, e);
    }
  }
  if (changed) await saveRecords(env, records);
  return { removed, granted };
}

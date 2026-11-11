// Download links for files in R2: /file/<key>?exp=<seconds>&sig=<HMAC>.
// The key is part of what is signed, so a link cannot be pointed at another file.

import type { Env } from "./env";
import { hmac, hmacEqual } from "./crypto";

function signed(key: string, exp: string): string {
  return `file\n${key}\n${exp}`;
}

export async function signedFileUrl(env: Env, key: string, ttlSeconds: number, now: number): Promise<string> {
  const exp = String(Math.floor(now / 1000) + ttlSeconds);
  const sig = await hmac(env.SIGNING_KEY, signed(key, exp));
  // Each part of the key on its own, so ? # % and the like stay part of the name.
  return `${env.SITE_URL}/file/${key.split("/").map(encodeURIComponent).join("/")}?exp=${exp}&sig=${sig}`;
}

export async function checkFileLink(
  env: Env,
  key: string,
  exp: string | null,
  sig: string | null,
  now: number,
): Promise<boolean> {
  if (!exp || !sig || !/^\d+$/.test(exp)) return false;
  if (Number(exp) < now / 1000) return false;
  return hmacEqual(env.SIGNING_KEY, signed(key, exp), sig);
}

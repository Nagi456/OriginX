// The few Discord API calls the Worker makes: read a member's roles, give or
// take a role, finish a login, and register the /setup-verify command.

import type { Env } from "./env";

const API = "https://discord.com/api/v10";

export class DiscordError extends Error {
  constructor(public status: number, message: string) {
    super(message);
  }
}

// Discord rate limits answer 429 with Retry-After. Wait once (at most five
// seconds) and try again; a second 429 is reported as 503. Callers that must
// answer Discord within three seconds pass retry = false.
async function call(url: string, init: RequestInit, retry = true): Promise<Response> {
  for (let attempt = 0; ; attempt++) {
    const res = await fetch(url, init);
    if (res.status !== 429) return res;
    if (attempt === 1 || !retry) throw new DiscordError(503, "Discord is rate limiting us");
    const after = Number(res.headers.get("retry-after") ?? "1");
    const wait = Math.min(Number.isFinite(after) && after >= 0 ? after : 1, 5);
    await new Promise((resolve) => setTimeout(resolve, wait * 1000));
  }
}

export function discordFetch(env: Env, path: string, init: RequestInit = {}, retry = true): Promise<Response> {
  const headers = new Headers(init.headers);
  headers.set("authorization", `Bot ${env.BOT_TOKEN}`);
  return call(API + path, { ...init, headers }, retry);
}

// Discord's icon hashes are 32 hex digits; an animated icon starts with "a_". Anything else is not trusted.
const ICON_HASH = /^(a_)?[0-9a-f]{32}$/;

// The member's server icon, else the account's icon, as a 64px PNG on Discord's CDN (an animated icon is
// served as a still picture for .png). null when there is none, so the launcher shows the name's first letter.
export function memberAvatarUrl(
  guildId: string,
  userId: string,
  memberAvatar: string | null | undefined,
  userAvatar: string | null | undefined,
): string | null {
  if (memberAvatar && ICON_HASH.test(memberAvatar)) {
    return `https://cdn.discordapp.com/guilds/${guildId}/users/${userId}/avatars/${memberAvatar}.png?size=64`;
  }
  if (userAvatar && ICON_HASH.test(userAvatar)) {
    return `https://cdn.discordapp.com/avatars/${userId}/${userAvatar}.png?size=64`;
  }
  return null;
}

export async function getMember(env: Env, userId: string): Promise<{ roles: string[]; name: string; avatarUrl: string | null } | null> {
  const res = await discordFetch(env, `/guilds/${env.GUILD_ID}/members/${userId}`);
  if (res.status === 404) return null;
  if (!res.ok) throw new DiscordError(res.status, `member lookup failed (${res.status})`);
  const member = (await res.json()) as {
    roles: string[];
    nick?: string | null;
    avatar?: string | null;
    user: { username: string; global_name?: string | null; avatar?: string | null };
  };
  return {
    roles: member.roles,
    name: member.nick ?? member.user.global_name ?? member.user.username,
    avatarUrl: memberAvatarUrl(env.GUILD_ID, userId, member.avatar, member.user.avatar),
  };
}

export async function addRole(env: Env, userId: string, roleId: string, retry = true): Promise<boolean> {
  const res = await discordFetch(env, `/guilds/${env.GUILD_ID}/members/${userId}/roles/${roleId}`, { method: "PUT" }, retry);
  if (res.status === 204 || res.ok) return true;
  if (res.status === 403 || res.status === 404) return false;
  throw new DiscordError(res.status, `adding a role failed (${res.status})`);
}

// Take a role away. False when the member is not in the server (nothing to
// take); a 403 (the bot's role is below it) is an error so callers retry.
export async function removeRole(env: Env, userId: string, roleId: string): Promise<boolean> {
  const res = await discordFetch(env, `/guilds/${env.GUILD_ID}/members/${userId}/roles/${roleId}`, { method: "DELETE" });
  if (res.status === 204 || res.ok) return true;
  if (res.status === 404) return false;
  throw new DiscordError(res.status, `removing a role failed (${res.status})`);
}

export async function exchangeCode(env: Env, code: string, redirectUri: string): Promise<string> {
  const res = await call(`${API}/oauth2/token`, {
    method: "POST",
    headers: { "content-type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      client_id: env.CLIENT_ID,
      client_secret: env.CLIENT_SECRET,
      grant_type: "authorization_code",
      code,
      redirect_uri: redirectUri,
    }),
  });
  // 401 means our client id or secret is wrong, not the user's code: report it
  // as a server problem so it is logged instead of asking the user to retry.
  if (res.status === 401) throw new DiscordError(503, "Discord rejected the client id or secret");
  if (!res.ok) throw new DiscordError(res.status >= 500 ? 503 : 400, `code exchange failed (${res.status})`);
  const body = (await res.json()) as { access_token?: string };
  if (!body.access_token) throw new DiscordError(400, "code exchange gave no token");
  return body.access_token;
}

export async function getUserId(accessToken: string): Promise<string> {
  const res = await call(`${API}/users/@me`, { headers: { authorization: `Bearer ${accessToken}` } });
  if (!res.ok) throw new DiscordError(res.status >= 500 ? 503 : 400, `user lookup failed (${res.status})`);
  return ((await res.json()) as { id: string }).id;
}

export async function registerCommands(env: Env): Promise<void> {
  const res = await discordFetch(env, `/applications/${env.CLIENT_ID}/guilds/${env.GUILD_ID}/commands`, {
    method: "PUT",
    headers: { "content-type": "application/json" },
    body: JSON.stringify([
      { name: "setup-verify", description: "Post the OriginX verify button in this channel", default_member_permissions: "8", type: 1 },
    ]),
  });
  if (!res.ok) throw new DiscordError(res.status, `registering commands failed (${res.status})`);
}

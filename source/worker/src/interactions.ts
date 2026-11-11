// POST /discord/interactions: Discord sends slash commands and button presses
// here. /setup-verify posts the verify button; the button gives the member role.

import type { Ctx, Env } from "./env";
import { verifyDiscord } from "./crypto";
import { DiscordError, addRole, registerCommands } from "./discord";

const PING = 1;
const COMMAND = 2;
const BUTTON = 3;
const PONG = 1;
const MESSAGE = 4;
const ONLY_YOU = 64;
const ADMINISTRATOR = 1n << 3n;

interface Interaction {
  type: number;
  data?: { name?: string; custom_id?: string };
  member?: { roles?: string[]; permissions?: string; user?: { id: string } };
}

function reply(data: Record<string, unknown>): Response {
  return Response.json({ type: MESSAGE, data });
}

function privateReply(content: string): Response {
  return reply({ content, flags: ONLY_YOU });
}

function isAdmin(permissions: string | undefined): boolean {
  try {
    return (BigInt(permissions ?? "0") & ADMINISTRATOR) !== 0n;
  } catch {
    return false;
  }
}

async function verify(env: Env, interaction: Interaction): Promise<Response> {
  const download = `${env.SITE_URL}/download/`;
  const userId = interaction.member?.user?.id;
  if (!userId) return privateReply("この中のサーバーで押してください。/ Please use this inside the server.");
  if (interaction.member?.roles?.includes(env.MEMBER_ROLE_ID)) {
    return privateReply(`もう認証済みです。/ You are already verified.\n${download}`);
  }
  let added = false;
  try {
    added = await addRole(env, userId, env.MEMBER_ROLE_ID, false);
  } catch (e) {
    console.error("verify: adding the role failed", userId, e);
  }
  if (!added) {
    console.error("verify: the bot could not give the member role to", userId);
    return privateReply("認証できませんでした。サーバーの管理者に連絡してください。/ Verification failed. Please contact a server admin.");
  }
  return privateReply(`認証しました。サイトからダウンロードできます。/ Verified. You can now download OriginX.\n${download}`);
}

export async function handleInteraction(request: Request, env: Env, ctx: Ctx, now: number): Promise<Response> {
  const body = await request.text();
  const ok = await verifyDiscord(
    env.PUBLIC_KEY,
    request.headers.get("x-signature-ed25519"),
    request.headers.get("x-signature-timestamp"),
    body,
    now,
  );
  if (!ok) return new Response("invalid request signature", { status: 401 });

  let interaction: Interaction;
  try {
    interaction = JSON.parse(body);
  } catch {
    return new Response("bad request", { status: 400 });
  }

  if (interaction.type === PING) {
    // Discord pings when the Interactions Endpoint URL is saved; that is when
    // the /setup-verify command gets registered.
    ctx.waitUntil(registerCommands(env).catch((e: DiscordError) => console.error("registering commands failed", e)));
    return Response.json({ type: PONG });
  }

  if (interaction.type === COMMAND && interaction.data?.name === "setup-verify") {
    if (!isAdmin(interaction.member?.permissions)) {
      return privateReply("管理者だけが使えます。/ Only administrators can use this.");
    }
    return reply({
      content: "OriginX を使うには、下のボタンで認証してください。\nPress the button below to verify and use OriginX.",
      components: [{ type: 1, components: [{ type: 2, style: 3, custom_id: "originx-verify", label: "認証する / Verify" }] }],
    });
  }

  if (interaction.type === BUTTON && interaction.data?.custom_id === "originx-verify") {
    return verify(env, interaction);
  }

  return new Response("unknown interaction", { status: 400 });
}

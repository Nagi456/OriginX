// What the Worker gets from wrangler.jsonc (vars and bindings) and from the
// dashboard (secrets).

export interface R2Like {
  get(
    key: string,
    options?: { range?: { offset: number; length: number } },
  ): Promise<{ body: ReadableStream; size: number; text(): Promise<string> } | null>;
  head?(key: string): Promise<{ size: number } | null>;
  put(key: string, value: ArrayBuffer): Promise<unknown>;
}

export interface Env {
  GUILD_ID: string;
  MEMBER_ROLE_ID: string;
  SUPPORTER_ROLE_ID: string;
  MODERATOR_ROLE_ID: string;
  CLIENT_ID: string;
  PUBLIC_KEY: string;
  SITE_URL: string;
  BOT_TOKEN: string;
  CLIENT_SECRET: string;
  SIGNING_KEY: string;
  PUBLISH_KEY?: string;
  KOFI_TOKEN?: string;
  FILES: R2Like;
  ASSETS: { fetch(request: Request): Promise<Response> };
}

export interface Ctx {
  waitUntil(promise: Promise<unknown>): void;
}

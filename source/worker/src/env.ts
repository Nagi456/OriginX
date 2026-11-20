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
  // Cape roles (capes.ts). Empty or unset: nobody has the role yet.
  FOUNDER_ROLE_ID?: string;
  SUPPORTER_CAPE_ROLE_ID?: string;
  CREATOR_ROLE_ID?: string;
  PARTNER_ROLE_ID?: string;
  DEVELOPER_ROLE_ID?: string;
  // The public launch day (ISO date, e.g. 2027-01-01): founder within 90 days of it, supporter cape after. Empty: not given automatically.
  PUBLIC_LAUNCH_DATE?: string;
  CLIENT_ID: string;
  PUBLIC_KEY: string;
  SITE_URL: string;
  BOT_TOKEN: string;
  CLIENT_SECRET: string;
  SIGNING_KEY: string;
  PUBLISH_KEY?: string;
  // "<keyId>:<base64 seed>" of the test signing key (manifest-sign.ts). Signs test/catalog.json only.
  TEST_MANIFEST_KEY?: string;
  // Comma-separated git refs whose workflows may write outside test/ (stable/, early/, launcher/). Default refs/heads/main.
  RELEASE_REFS?: string;
  KOFI_TOKEN?: string;
  FILES: R2Like;
  ASSETS: { fetch(request: Request): Promise<Response> };
}

export interface Ctx {
  waitUntil(promise: Promise<unknown>): void;
}

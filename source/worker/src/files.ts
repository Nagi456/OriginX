// GET /file/<key>?exp=&sig=: hands out a file from R2 when the link is signed
// and not expired.

import type { Env } from "./env";
import { checkFileLink } from "./links";
import { serveObject } from "./serve";

export async function handleFile(request: Request, env: Env, now: number): Promise<Response> {
  const url = new URL(request.url);
  let key: string;
  try {
    key = decodeURIComponent(url.pathname.replace(/^\/file\//, ""));
  } catch {
    return new Response("Not found", { status: 404 });
  }
  if (!(await checkFileLink(env, key, url.searchParams.get("exp"), url.searchParams.get("sig"), now))) {
    return new Response("リンクの期限が切れました。もう一度ダウンロードしてください。\nThis link has expired. Please download again.\n", {
      status: 403,
      headers: { "content-type": "text/plain; charset=utf-8" },
    });
  }
  return serveObject(request, env, key, { "content-type": "application/octet-stream", "cache-control": "private, no-store" });
}

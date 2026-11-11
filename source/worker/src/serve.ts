// Sending a file from R2 (/file and /dl): a filename header that works for any
// name, byte ranges (resuming a download) and a 503 instead of a 500 when R2
// itself fails.

import type { Env } from "./env";

// The name a downloaded file is saved as. The launcher is built and stored as
// MyClientLauncher.exe (the self-update and the build scripts use that key),
// but people should get it under the product's name.
export function downloadName(key: string): string {
  const name = key.slice(key.lastIndexOf("/") + 1);
  return name === "MyClientLauncher.exe" ? "OriginXLauncher.exe" : name;
}

// `filename` is the ASCII fallback; `filename*` carries the real (UTF-8) name.
// A header value with non-Latin-1 characters would make the Response throw.
export function contentDisposition(name: string): string {
  const ascii = name.replace(/[^\x20-\x7e]|["\\]/g, "_") || "download";
  return `attachment; filename="${ascii}"; filename*=UTF-8''${encodeURIComponent(name)}`;
}

// "bytes=a-b", "bytes=a-" or "bytes=-n" for a file of `size` bytes; null for
// anything else (several ranges included), which is answered with the whole
// file. `false` means a range that starts past the end (416).
export function parseRange(header: string | null, size: number): { offset: number; length: number } | null | false {
  if (!header) return null;
  const m = /^bytes=(\d*)-(\d*)$/.exec(header.trim());
  if (!m || (m[1] === "" && m[2] === "")) return null;
  if (m[1] === "") {
    const suffix = Math.min(Number(m[2]), size);
    return suffix > 0 ? { offset: size - suffix, length: suffix } : false;
  }
  const start = Number(m[1]);
  const end = m[2] === "" ? size - 1 : Math.min(Number(m[2]), size - 1);
  if (start >= size || end < start) return false;
  return { offset: start, length: end - start + 1 };
}

function unavailable(): Response {
  return new Response("Download is unavailable for a moment. Please try again. / 一時的にダウンロードできません。少し待ってからもう一度お試しください。\n", {
    status: 503,
    headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store", "retry-after": "30" },
  });
}

export async function serveObject(request: Request, env: Env, key: string, headers: Record<string, string>): Promise<Response> {
  const name = downloadName(key);
  try {
    const wanted = request.headers.get("range");
    let range: { offset: number; length: number } | null | false = null;
    if (wanted && env.FILES.head) {
      const info = await env.FILES.head(key);
      if (!info) return new Response("Not found", { status: 404 });
      range = parseRange(wanted, info.size);
      if (range === false) {
        return new Response(null, { status: 416, headers: { "content-range": `bytes */${info.size}` } });
      }
    }
    const object = await env.FILES.get(key, range ? { range } : undefined);
    if (!object) return new Response("Not found", { status: 404 });
    const out: Record<string, string> = {
      ...headers,
      "content-disposition": contentDisposition(name),
      "accept-ranges": "bytes",
    };
    const body = request.method === "HEAD" ? null : object.body;
    if (range) {
      out["content-length"] = String(range.length);
      out["content-range"] = `bytes ${range.offset}-${range.offset + range.length - 1}/${object.size}`;
      return new Response(body, { status: 206, headers: out });
    }
    out["content-length"] = String(object.size);
    return new Response(body, { headers: out });
  } catch (e) {
    console.error("R2 failed", key, e);
    return unavailable();
  }
}

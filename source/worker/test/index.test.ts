import { afterEach, describe, expect, it, vi } from "vitest";

vi.mock("../src/interactions", () => ({ handleInteraction: vi.fn(async () => new Response("interactions")) }));
vi.mock("../src/oauth", () => ({
  handleLogin: vi.fn(async () => new Response("login")),
  handleLauncherLogin: vi.fn(async () => new Response("launcher-login")),
  handleCallback: vi.fn(async () => new Response("callback")),
}));
vi.mock("../src/files", () => ({ handleFile: vi.fn(async () => new Response("file")) }));
vi.mock("../src/status", () => ({ handleStatus: vi.fn(async () => new Response("status")) }));

import worker from "../src/index";
import { handleLogin } from "../src/oauth";
import { testEnv } from "./helpers";

const ctx = { waitUntil() {} };
afterEach(() => vi.restoreAllMocks());

function go(method: string, path: string, env = testEnv()) {
  return worker.fetch(new Request("https://site.example" + path, { method }), env, ctx);
}

describe("routing", () => {
  it("routes each API path", async () => {
    const routes: Array<[string, string, string]> = [
      ["POST", "/discord/interactions", "interactions"],
      ["GET", "/login", "login"],
      ["GET", "/login?lang=en", "login"],
      ["GET", "/launcher/login", "launcher-login"],
      ["GET", "/callback", "callback"],
      ["GET", "/file/launcher/x.exe", "file"],
      ["GET", "/launcher/status", "status"],
    ];
    for (const [method, path, want] of routes) {
      expect(await (await go(method, path)).text()).toBe(want);
    }
  });

  it("passes other paths to the site", async () => {
    const env = testEnv({ ASSETS: { fetch: async () => new Response("the site page") } });
    expect(await (await go("GET", "/download/", env)).text()).toBe("the site page");
  });

  it("answers 500 without details when a handler throws", async () => {
    vi.mocked(handleLogin).mockRejectedValueOnce(new Error("secret detail"));
    const logged = vi.spyOn(console, "error").mockImplementation(() => {});
    const res = await go("GET", "/login");
    expect(res.status).toBe(500);
    const text = await res.text();
    expect(text).toBe("Something went wrong. / エラーが起きました。");
    expect(text).not.toContain("secret");
    expect(logged).toHaveBeenCalled();
  });

  it("refuses the API until the secrets are set", async () => {
    for (const missing of ["BOT_TOKEN", "CLIENT_SECRET", "SIGNING_KEY"] as const) {
      const env = testEnv({ [missing]: undefined as unknown as string });
      const res = await go("GET", "/login", env);
      expect(res.status).toBe(503);
      expect(await (await go("GET", "/", env)).text()).toBe("site");
    }
    const short = await go("GET", "/login", testEnv({ SIGNING_KEY: "short" }));
    expect(short.status).toBe(503);
  });
});

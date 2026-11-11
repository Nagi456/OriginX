#pragma once
// The Windows side of the release channels (docs/superpowers/specs/2026-11-09-release-channels-design.md):
// the saved Discord login, the browser login, the /launcher/status call and the account's icon. The decisions
// (which links are allowed, what a status means, what Launch does) are in Channels.h and tested there.
// Include this before <windows.h> (it brings winsock2.h).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wincodec.h>
#include "Channels.h"
#include <atomic>
#include <fstream>

namespace MyClientLauncher {
inline fs::path AccountFile(const fs::path& dir) { return dir / "account.json"; }
inline fs::path AvatarFile(const fs::path& dir) { return dir / "account_avatar.png"; }

// account.json holds the login key: it is never written to the log, and logging out deletes it.
inline StoredAccount LoadAccount(const fs::path& dir)
{
    std::ifstream input(AccountFile(dir), std::ios::binary);
    if (!input) return {};
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.size() > 64 * 1024) return {};
    return ParseAccount(text);
}
inline void SaveAccount(const fs::path& dir, const StoredAccount& account)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    const auto temporary = AccountFile(dir).string() + ".new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << SerializeAccount(account);
        if (!out) throw std::runtime_error("ログイン情報を保存できません。");
    }
    fs::rename(temporary, AccountFile(dir), ec);
    if (ec)
    {
        fs::remove(temporary, ec);
        throw std::runtime_error("ログイン情報を保存できません。");
    }
}
inline void DeleteAccount(const fs::path& dir)
{
    std::error_code ignored;
    fs::remove(AccountFile(dir), ignored);
    fs::remove(AvatarFile(dir), ignored);
}

struct HttpAnswer { DWORD status = 0; std::string body; };
// One HTTPS GET (certificates checked by Windows, redirects never to HTTP). A failure to talk to the server throws;
// any HTTP status comes back in the answer. The body is cut off with an error past maxBytes.
inline HttpAnswer HttpsGet(const std::string& url, const std::string& bearer, size_t maxBytes, DWORD timeoutMs)
{
    const std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof parts;
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("サーバーのURLが不正です。");
    struct HttpHandle { HINTERNET value; ~HttpHandle() { if (value) WinHttpCloseHandle(value); } };
    HttpHandle session{WinHttpOpen(L"MyClient/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value || !WinHttpSetTimeouts(session.value, timeoutMs, timeoutMs, timeoutMs, timeoutMs))
        throw std::runtime_error("通信を開始できません。");
    HttpHandle connection{WinHttpConnect(session.value, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(), parts.nPort, 0)};
    if (!connection.value) throw std::runtime_error("サーバーに接続できません。接続を確認してください。");
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) + std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HttpHandle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    std::wstring headers = L"Cache-Control: no-cache\r\n";
    if (!bearer.empty())
    {
        // the key goes into a header: only its plain characters (LoginToken / StoredAccount check them) are allowed
        if (bearer.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._") != std::string::npos)
            throw std::runtime_error("ログインの鍵が不正です。もう一度 Discord でログインしてください。");
        headers += L"Authorization: Bearer " + std::wstring(bearer.begin(), bearer.end()) + L"\r\n";
    }
    if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect) ||
        !WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("サーバーに接続できません。接続を確認してください。");
    HttpAnswer answer;
    DWORD size = sizeof answer.status;
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &answer.status, &size, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("サーバーの応答を読み取れません。");
    char buffer[16384];
    for (;;)
    {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof buffer, &read)) throw std::runtime_error("通信が中断されました。");
        if (!read) break;
        if (answer.body.size() + read > maxBytes) throw std::runtime_error("サーバーの応答が大きすぎます。");
        answer.body.append(buffer, read);
    }
    return answer;
}

// GET /launcher/status with the saved login key. The messages are what the user is shown when Launch falls back
// to the stable channel (Channels.h PlanLaunch).
inline AccountStatus FetchStatus(const std::string& token)
{
    if (token.empty()) throw std::runtime_error("ログインしていません。Discord でログインしてください。");
    const auto answer = HttpsGet(std::string(OriginXHost) + "/launcher/status", token, 512 * 1024, 15000);
    if (answer.status == 401) throw std::runtime_error("ログインの期限が切れました。もう一度 Discord でログインしてください。");
    if (answer.status == 503) throw std::runtime_error("Discordに接続できませんでした。しばらくしてからもう一度お試しください。");
    if (answer.status != 200) throw std::runtime_error("サーバーの応答が正しくありません。HTTP " + std::to_string(answer.status));
    const Json body = Json::parse(answer.body, nullptr, false);
    if (body.is_discarded()) throw std::runtime_error("サーバーの応答を読み取れませんでした。");
    return ParseStatus(body);
}

// The account's icon (Discord's CDN, 64px PNG, at most 128 KiB, a PNG by its signature) saved to dest.
// false, and no file, for anything else: the launcher then shows the first letter of the name.
inline bool FetchAvatar(const std::string& url, const fs::path& dest)
{
    std::error_code ec;
    if (!AvatarUrl(url)) { fs::remove(dest, ec); return false; }
    try
    {
        const auto answer = HttpsGet(url, "", MaxAvatarBytes, 10000);
        if (answer.status != 200 || !LooksLikePng(answer.body)) { fs::remove(dest, ec); return false; }
        fs::create_directories(dest.parent_path(), ec);
        const auto temporary = dest.string() + ".new";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(answer.body.data(), static_cast<std::streamsize>(answer.body.size()));
            if (!out) { fs::remove(temporary, ec); return false; }
        }
        fs::rename(temporary, dest, ec);
        if (ec) { fs::remove(temporary, ec); return false; }
        return true;
    }
    catch (const std::exception&) { fs::remove(dest, ec); return false; }
}

// A 32bpp premultiplied BGRA bitmap of the icon, `pixels` square and cut to a circle (draw it with AlphaBlend).
// nullptr when the file cannot be decoded.
inline HBITMAP LoadAvatarBitmap(const fs::path& png, int pixels)
{
    if (pixels < 8 || pixels > 512 || !fs::is_regular_file(png)) return nullptr;
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory) return nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    HBITMAP bitmap = nullptr;
    do
    {
        if (FAILED(factory->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) break;
        if (FAILED(decoder->GetFrame(0, &frame)) || !frame) break;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) || !scaler ||
            FAILED(scaler->Initialize(frame, pixels, pixels, WICBitmapInterpolationModeFant))) break;
        if (FAILED(factory->CreateFormatConverter(&converter)) || !converter ||
            FAILED(converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof info.bmiHeader;
        info.bmiHeader.biWidth = pixels;
        info.bmiHeader.biHeight = -pixels;   // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits) { bitmap = nullptr; break; }
        const UINT stride = static_cast<UINT>(pixels) * 4;
        if (FAILED(converter->CopyPixels(nullptr, stride, stride * static_cast<UINT>(pixels), static_cast<BYTE*>(bits))))
        {
            DeleteObject(bitmap);
            bitmap = nullptr;
            break;
        }
        // the circle: pixels inside keep their value, pixels outside become transparent, the edge is one pixel soft
        auto* data = static_cast<uint32_t*>(bits);
        const double centre = (pixels - 1) / 2.0, radius = pixels / 2.0;
        for (int y = 0; y < pixels; ++y)
            for (int x = 0; x < pixels; ++x)
            {
                const double distance = std::sqrt((x - centre) * (x - centre) + (y - centre) * (y - centre));
                const double coverage = std::min(1.0, std::max(0.0, radius - distance));
                if (coverage >= 1.0) continue;
                uint32_t& pixel = data[y * pixels + x];
                const auto channel = [&](int shift) { return static_cast<uint32_t>(((pixel >> shift) & 0xFF) * coverage + 0.5); };
                pixel = (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
            }
    } while (false);
    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    factory->Release();
    return bitmap;
}

// The Discord login: the browser opens OriginX's login page, and when it is done it returns to this PC's own
// loopback address (127.0.0.1, a free port) with the login key. Only a return with the state made here is taken.
// Returns the key; throws on a timeout (120 s), a cancel or a failure. Run it on a thread: it waits.
inline std::string LoginInBrowser(std::atomic<bool>& cancel)
{
    struct Sockets {
        bool started = false;
        SOCKET listener = INVALID_SOCKET;
        ~Sockets() { if (listener != INVALID_SOCKET) closesocket(listener); if (started) WSACleanup(); }
    } sockets;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("通信を開始できません。");
    sockets.started = true;
    sockets.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockets.listener == INVALID_SOCKET) throw std::runtime_error("ログインの待ち受けを開始できません。");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // this PC only
    address.sin_port = 0;
    int length = sizeof address;
    if (bind(sockets.listener, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || listen(sockets.listener, 4) != 0 ||
        getsockname(sockets.listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        throw std::runtime_error("ログインの待ち受けを開始できません。");
    const unsigned port = ntohs(address.sin_port);
    if (port < 1024) throw std::runtime_error("ログインの待ち受けを開始できません。");

    unsigned char random[16];
    if (BCryptGenRandom(nullptr, random, sizeof random, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) throw std::runtime_error("ログインの準備に失敗しました。");
    std::string state;
    for (unsigned char byte : random) { char two[3]; snprintf(two, sizeof two, "%02x", byte); state += two; }

    const std::string url = std::string(OriginXHost) + "/launcher/login?port=" + std::to_string(port) + "&state=" + state;
    const std::wstring wideUrl(url.begin(), url.end());
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        throw std::runtime_error("ブラウザーを開けません。");

    const ULONGLONG deadline = GetTickCount64() + 120000;
    while (GetTickCount64() < deadline)
    {
        if (cancel) throw std::runtime_error("ログインを中止しました。");
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(sockets.listener, &readable);
        timeval wait{0, 500000};
        if (select(0, &readable, nullptr, nullptr, &wait) <= 0) continue;
        SOCKET client = accept(sockets.listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        DWORD timeout = 3000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        std::string request;
        char buffer[2048];
        while (request.size() < 8192 && request.find("\r\n") == std::string::npos)
        {
            const int got = recv(client, buffer, sizeof buffer, 0);
            if (got <= 0) break;
            request.append(buffer, static_cast<size_t>(got));
        }
        const std::string line = request.substr(0, request.find("\r\n"));
        const auto token = LoginToken(line, state);
        const std::string page = token
            ? "<!doctype html><meta charset=utf-8><title>OriginX</title><p>ログインできました。このタブは閉じて、ランチャーに戻ってください。<br>Logged in. You can close this tab."
            : "<!doctype html><meta charset=utf-8><title>OriginX</title><p>ログインの確認ができませんでした。ランチャーからやり直してください。";
        const std::string reply = std::string(token ? "HTTP/1.1 200 OK" : "HTTP/1.1 400 Bad Request") +
            "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
            std::to_string(page.size()) + "\r\n\r\n" + page;
        send(client, reply.data(), static_cast<int>(reply.size()), 0);
        shutdown(client, SD_BOTH);
        closesocket(client);
        if (token) return *token;
    }
    throw std::runtime_error("ログインがタイムアウトしました。もう一度お試しください。");
}
}

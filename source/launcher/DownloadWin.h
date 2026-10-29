#pragma once
#include <winhttp.h>
#include "Updates.h"
#include "GameVersions.h"
#include <atomic>
namespace MyClientLauncher {
// HTTPS and normal Windows certificate verification; only our release assets and the
// public version list are accepted as initial URLs. Redirects must remain HTTPS.
inline void DownloadHttps(const std::string& url, const fs::path& destination, uint64_t limit)
{
    if (!ReleaseUrl(url) && url != VersionDbUrl) throw std::runtime_error("許可されていない更新配布先です。");
    const std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof parts;
    parts.dwHostNameLength = parts.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("更新URLが不正です。");
    struct HttpHandle { HINTERNET value; ~HttpHandle() { if (value) WinHttpCloseHandle(value); } };
    HttpHandle session{WinHttpOpen(L"MyClient/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value || !WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 15000))
        throw std::runtime_error("更新通信を開始できません。");
    HttpHandle connection{WinHttpConnect(session.value, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(), parts.nPort, 0)};
    if (!connection.value) throw std::runtime_error("更新配布先に接続できません。");
    HttpHandle request{WinHttpOpenRequest(connection.value, L"GET", std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect) ||
        !WinHttpSendRequest(request.value, L"Cache-Control: no-cache\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) throw std::runtime_error("更新一覧またはDLLを取得できません。接続を確認してください。");
    DWORD status = 0, size = sizeof status;
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200)
        throw std::runtime_error("更新配布先から取得できません。HTTP " + std::to_string(status) + "（未公開の場合もあります）。");
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("ダウンロード保存先を開けません。");
    uint64_t total = 0;
    const auto start = GetTickCount64();
    char buffer[65536];
    for (;;)
    {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof buffer, &read)) throw std::runtime_error("ダウンロードが中断されました。");
        if (!read) break;
        total += read;
        if (total > limit || GetTickCount64() - start > 300000) throw std::runtime_error("ダウンロードのサイズまたは時間が上限を超えました。");
        out.write(buffer, read);
        if (!out) throw std::runtime_error("更新ファイルの書き込みに失敗しました。");
    }
    out.close();
    if (!out || total == 0) throw std::runtime_error("更新ファイルの保存に失敗しました。");
}

// A Minecraft package from the Xbox CDN (several GB). HTTP is allowed because the links in the
// database are HTTP; Windows refuses the package at install time unless Microsoft signed it.
inline void DownloadGamePackage(const std::string& url, const fs::path& destination,
                                std::atomic<unsigned long long>& done, std::atomic<unsigned long long>& total)
{
    if (!GamePackageUrl(url)) throw std::runtime_error("許可されていない配布先です: " + url);
    const std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof parts;
    parts.dwHostNameLength = parts.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) throw std::runtime_error("配布先のURLが不正です。");
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    struct HttpHandle { HINTERNET value; ~HttpHandle() { if (value) WinHttpCloseHandle(value); } };
    HttpHandle session{WinHttpOpen(L"MyClient/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value || !WinHttpSetTimeouts(session.value, 15000, 15000, 30000, 60000))
        throw std::runtime_error("通信を開始できません。");
    HttpHandle connection{WinHttpConnect(session.value, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(), parts.nPort, 0)};
    if (!connection.value) throw std::runtime_error("配布先に接続できません。");
    HttpHandle request{WinHttpOpenRequest(connection.value, L"GET", std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength).c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.value || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("配布先から応答がありません（Windowsエラー " + std::to_string(GetLastError()) + "）。");
    DWORD status = 0, size = sizeof status;
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200)
        throw std::runtime_error("配布先から取得できません。HTTP " + std::to_string(status));
    wchar_t lengthText[32]{};
    DWORD lengthSize = sizeof lengthText;
    unsigned long long expected = 0;
    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX))
        expected = wcstoull(lengthText, nullptr, 10);
    if (expected < 100ull * 1024 * 1024 || expected > 40ull * 1024 * 1024 * 1024)
        throw std::runtime_error("配布ファイルの大きさが想定外です（" + std::to_string(expected) + " バイト）。");
    // The package is written here, then Windows unpacks it next to the game: room for both.
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(destination.parent_path().c_str(), &freeBytes, nullptr, nullptr) && freeBytes.QuadPart < expected * 2 + (1ull << 30))
        throw std::runtime_error("空き容量が足りません。約 " + std::to_string((expected * 2 >> 30) + 1) + " GB 必要です。");
    total = expected;
    done = 0;
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("ダウンロード保存先を開けません。");
    std::vector<char> buffer(1 << 20);
    unsigned long long received = 0;
    for (;;)
    {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            throw std::runtime_error("ダウンロードが中断されました（Windowsエラー " + std::to_string(GetLastError()) + "）。");
        if (!read) break;
        received += read;
        if (received > expected) throw std::runtime_error("配布ファイルが予告より大きいため中止しました。");
        out.write(buffer.data(), read);
        if (!out) throw std::runtime_error("ダウンロードの書き込みに失敗しました（空き容量を確認してください）。");
        done = received;
    }
    out.close();
    if (!out || received != expected) throw std::runtime_error("ダウンロードが途中で終わりました。");
}
}

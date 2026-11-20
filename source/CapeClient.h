#pragma once
// OriginX capes, HUD side (docs/superpowers/specs/2026-11-12-capes-design.md): what is said to the Worker
// (POST /capes/register, POST /capes/lookup, GET /launcher/status) and what is read back. Pure parts are unit-tested
// in tests/cape_client.cpp; the HTTPS calls are below, Windows only. The login key (account.json) stays in memory.
#include <algorithm>
#include "launcher/vendor/nlohmann/json.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace CapeClient {
constexpr uint64_t AskAgainMs = 60000;   // a player whose cape was asked within this is not asked again
// Per-XUID cache lifetime is separate from the HUD->DLL heartbeat lease.
constexpr uint64_t CacheKeepMs = 120000;
inline bool CachedAnswerFresh(uint64_t at, uint64_t now) {
    return at && at <= now && now - at <= CacheKeepMs;
}
constexpr size_t MaxLookup = 64;          // the Worker's limit per lookup
constexpr uint64_t RetryMs = 30000;       // a failed register / lookup waits this long (review I3)
inline bool EndsLogin(int status) { return status == 401; }
inline bool RetryDue(uint64_t failedAt, uint64_t now) { return !failedAt || (now >= failedAt && now - failedAt >= RetryMs); }

// The players to ask about now: not asked within AskAgainMs, at most MaxLookup, in the order given.
inline std::vector<std::string> DueForLookup(const std::vector<std::string>& present, const std::map<std::string, uint64_t>& askedAt, uint64_t now)
{
    std::vector<std::string> out;
    for (const auto& xuid : present) {
        if (out.size() >= MaxLookup) break;
        const auto it = askedAt.find(xuid);
        if (it != askedAt.end() && now >= it->second && now - it->second < AskAgainMs) continue;
        out.push_back(xuid);
    }
    return out;
}

inline std::string RegisterBody(std::string_view xuid, uint32_t cape, bool hideCape = false, bool hideMark = false)
{
    return "{\"xuid\":\"" + std::string(xuid) + "\",\"cape\":" + std::to_string(cape) + ",\"hideCape\":" + (hideCape ? "true" : "false") + ",\"hideMark\":" + (hideMark ? "true" : "false") + "}";
}
inline std::string LookupBody(const std::vector<std::string>& xuids)
{
    std::string out = "{\"xuids\":[";
    for (size_t i = 0; i < xuids.size(); ++i) out += (i ? ",\"" : "\"") + xuids[i] + "\"";
    return out + "]}";
}

namespace detail {
inline size_t After(std::string_view json, std::string_view key, size_t from = 0)
{
    const std::string quoted = "\"" + std::string(key) + "\"";
    size_t at = json.find(quoted, from);
    if (at == std::string_view::npos) return at;
    at = json.find(':', at + quoted.size());
    if (at == std::string_view::npos) return at;
    ++at;
    while (at < json.size() && (json[at] == ' ' || json[at] == '\n' || json[at] == '\r' || json[at] == '\t')) ++at;
    return at;
}
} // namespace detail

// Strict JSON shape; missing marks supports the old Worker, arrays are rejected.
inline bool ParseLookup(std::string_view text, std::map<std::string, uint32_t>& capes,
                        std::map<std::string, uint32_t>& marks)
{
    capes.clear(); marks.clear();
    const auto json = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("capes") || !json["capes"].is_object() ||
        (json.contains("marks") && !json["marks"].is_object())) return false;
    const auto read = [](const nlohmann::json& object, uint32_t maximum, auto& out) {
        for (auto it = object.begin(); it != object.end(); ++it) {
            const auto& key = it.key();
            if (key.empty() || key.size() > 20 || key.find_first_not_of("0123456789") != std::string::npos) continue;
            if (!it.value().is_number_unsigned()) continue;
            const auto value = it.value().template get<uint64_t>();
            if (value >= 1 && value <= maximum) out[key] = static_cast<uint32_t>(value);
        }
    };
    read(json["capes"], 6, capes);
    if (json.contains("marks")) read(json["marks"], 15, marks);
    return true;
}
// Existing HUD uses capes only until the next implementation stage.
inline bool ParseLookup(std::string_view text, std::map<std::string, uint32_t>& capes) {
    std::map<std::string, uint32_t> marks;
    return ParseLookup(text, capes, marks);
}

// The capes this account may wear: /launcher/status roles.capes ([2,4], ...). False when it is missing.
inline bool ParseOwned(std::string_view statusJson, std::vector<uint32_t>& capes)
{
    const size_t roles = detail::After(statusJson, "roles");
    if (roles == std::string_view::npos) return false;
    size_t at = detail::After(statusJson, "capes", roles);
    if (at == std::string_view::npos || at >= statusJson.size() || statusJson[at] != '[') return false;
    capes.clear();
    ++at;
    uint32_t id = 0;
    bool digits = false;
    for (; at < statusJson.size() && statusJson[at] != ']'; ++at) {
        const char c = statusJson[at];
        if (c >= '0' && c <= '9') { id = id * 10 + uint32_t(c - '0'); digits = true; }
        else if (c == ',') { if (digits && id >= 1 && id <= 6) capes.push_back(id); id = 0; digits = false; }
    }
    if (digits && id >= 1 && id <= 6) capes.push_back(id);
    return at < statusJson.size();
}

// The launcher's login key from account.json ("" when there is none). Nothing else of the file is kept.
inline std::string ReadToken(const std::filesystem::path& accountJson)
{
    std::ifstream in(accountJson, std::ios::binary);
    if (!in) return {};
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t at = detail::After(text, "token");
    if (at == std::string::npos || at >= text.size() || text[at] != '"') return {};
    const size_t end = text.find('"', at + 1);
    if (end == std::string::npos || end - at - 1 > 2048) return {};
    return text.substr(at + 1, end - at - 1);
}
} // namespace CapeClient

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
namespace CapeClient {
constexpr const wchar_t* WorkerHost = L"originx.originx-client.workers.dev";
// One HTTPS request to the Worker with the login key. status = HTTP status (0 = no answer).
inline bool Call(const wchar_t* method, const wchar_t* path, const std::string& token, const std::string& body, int& status, std::string& answer)
{
    status = 0;
    answer.clear();
    struct H { HINTERNET v; ~H() { if (v) WinHttpCloseHandle(v); } };
    H session{WinHttpOpen(L"OriginX/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.v || !WinHttpSetTimeouts(session.v, 5000, 5000, 8000, 8000)) return false;
    H connection{WinHttpConnect(session.v, WorkerHost, INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.v) return false;
    H request{WinHttpOpenRequest(connection.v, method, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.v) return false;
    std::wstring headers = L"Authorization: Bearer " + std::wstring(token.begin(), token.end()) + L"\r\n";
    if (!body.empty()) headers += L"Content-Type: application/json\r\n";
    if (!WinHttpSendRequest(request.v, headers.c_str(), static_cast<DWORD>(headers.size()), body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.v, nullptr))
        return false;
    DWORD code = 0, size = sizeof(code);
    WinHttpQueryHeaders(request.v, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
    status = static_cast<int>(code);
    for (DWORD available = 0; WinHttpQueryDataAvailable(request.v, &available) && available && answer.size() < 256 * 1024;) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.v, chunk.data(), available, &read) || !read) break;
        answer.append(chunk.data(), read);
    }
    return true;
}
} // namespace CapeClient
#endif

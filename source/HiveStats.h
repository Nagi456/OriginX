#pragma once
// Hive Statistic: per-player stats from The Hive's public API
// (GET https://api.playhive.com/v0/game/all/<game>/<name>). The pure helpers (parsing, level maths)
// work everywhere; the background fetcher is Windows-only (WinHTTP, HTTPS with normal certificate checks).
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace HiveStats {

struct Game { const char* id; const char* name; int maxLevel; double increment; int cap; double multiplier; };

// Level parameters as published with the API data (hive-bedrock-data). cap 0 = no cap.
constexpr Game kGames[] = {
    {"bed",    "Bed Wars",         100, 150, 52, 0.0},
    {"sky",    "Sky Wars",         100, 150, 52, 0.0},
    {"wars",   "Treasure Wars",    100, 150, 52, 0.0},
    {"bridge", "The Bridge",        20, 300,  0, 1.08},
    {"murder", "Murder Mystery",   100, 100, 82, 0.0},
    {"sg",     "Survival Games",    50, 150,  0, 0.0},
    {"ctf",    "Capture the Flag",  50, 150,  0, 0.0},
    {"dr",     "Deathrun",          75, 200, 42, 0.0},
    {"hide",   "Hide and Seek",    100, 100,  0, 0.0},
    {"ground", "Ground Wars",       20, 150,  0, 0.0},
};
// Which game a scoreboard title (colour codes, case, spaces ignored) belongs to; -1 = unknown.
// Returns an index into kGames.
inline int DetectGame(std::string_view text) {
    std::string t;
    for (size_t i = 0; i < text.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0xC2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA7) { i += 2; continue; } // section sign + code
        if (std::isalnum(c)) t += static_cast<char>(std::tolower(c));
    }
    struct Key { const char* word; int game; };
    static const Key keys[] = {
        {"bedwars", 0}, {"skywars", 1}, {"sky", 1}, {"treasurewars", 2}, {"treasure", 2},
        {"thebridge", 3}, {"bridge", 3}, {"murdermystery", 4}, {"murder", 4}, {"survivalgames", 5}, {"survival", 5},
        {"capturetheflag", 6}, {"ctf", 6}, {"deathrun", 7}, {"hideandseek", 8}, {"hide", 8}, {"groundwars", 9}, {"ground", 9},
    };
    for (const auto& k : keys) if (t.find(k.word) != std::string::npos) return k.game;
    return -1;
}
constexpr int kGameCount = static_cast<int>(sizeof(kGames) / sizeof(kGames[0]));

// prestige: how many times the player reset from max level (Bed Wars etc.). Each prestige starts the
// level (and the xp the API reports) again at 1, so the level alone hides it: compare prestige first.
struct Stats { double xp = 0; uint32_t played = 0, victories = 0, kills = 0, deaths = 0, prestige = 0; };

// Finds `"key" : <number>` in a flat JSON object. Returns false when absent or not a number.
inline bool ParseNumber(std::string_view json, std::string_view key, double& out)
{
    const std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string_view::npos)
    {
        size_t i = pos + needle.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) i++;
        if (i < json.size() && json[i] == ':')
        {
            i++;
            while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) i++;
            const std::string rest(json.substr(i, 32));
            char* end = nullptr;
            const double v = std::strtod(rest.c_str(), &end);
            if (end != rest.c_str() && std::isfinite(v)) { out = v; return true; }
        }
        pos += needle.size();
    }
    return false;
}

// A player's stats object. Needs at least one stat field; missing ones count as 0 (like the API wrapper).
inline bool ParseStats(std::string_view json, Stats& out)
{
    Stats s;
    double v = 0;
    bool any = false;
    if (ParseNumber(json, "xp", v))        { s.xp = v < 0 ? 0 : v; any = true; }
    if (ParseNumber(json, "played", v))    { s.played = v < 0 ? 0 : static_cast<uint32_t>(v); any = true; }
    if (ParseNumber(json, "victories", v)) { s.victories = v < 0 ? 0 : static_cast<uint32_t>(v); any = true; }
    if (ParseNumber(json, "kills", v))     { s.kills = v < 0 ? 0 : static_cast<uint32_t>(v); any = true; }
    if (ParseNumber(json, "deaths", v))    { s.deaths = v < 0 ? 0 : static_cast<uint32_t>(v); any = true; }
    if (ParseNumber(json, "prestige", v))  s.prestige = v < 0 ? 0 : static_cast<uint32_t>(v);   // only some games have it
    if (!any) return false;
    out = s;
    return true;
}

inline double Kdr(const Stats& s) { return s.deaths ? static_cast<double>(s.kills) / s.deaths : static_cast<double>(s.kills); }
// Ranking order: prestige first, then the level inside that prestige.
inline bool Higher(uint32_t prestigeA, double levelA, uint32_t prestigeB, double levelB)
{
    return prestigeA != prestigeB ? prestigeA > prestigeB : levelA > levelB;
}
inline double WinPercent(const Stats& s) { return s.played ? 100.0 * s.victories / s.played : 0.0; }

// Same maths as the API wrapper's calculateLevelFromXP (result floored to two decimals, 1..maxLevel).
inline double Level(const Game& g, double xp)
{
    double level = 1;
    if (g.multiplier > 0)   // The Bridge uses its own scheme
    {
        double increment = g.increment, levelIncrement = g.increment, total = g.increment, previous = 0;
        level = g.maxLevel;   // xp beyond the last threshold = max level
        for (int l = 1; l <= g.maxLevel; l++)
        {
            if (xp < total)
            {
                if (l == g.maxLevel) { level = l; break; }
                const double done = 1.0 - (total - xp) / (total - previous);
                level = std::floor((l + done) * 100) / 100;
                break;
            }
            previous = total;
            increment = std::floor(increment * g.multiplier);
            levelIncrement += increment;
            total += levelIncrement;
        }
    }
    else
    {
        const double inc = g.increment / 2;
        double raw = (inc + std::sqrt(inc * (inc + 4 * xp))) / (2 * inc);
        if (g.cap > 0 && raw > g.cap)
            raw = g.cap + (xp - (inc * std::pow(g.cap - 1, 2) + (g.cap - 1) * inc)) / ((g.cap - 1) * inc * 2);
        level = std::floor(raw * 100) / 100;
    }
    if (level < 1) level = 1;
    if (level > g.maxLevel) level = g.maxLevel;
    return level;
}

inline std::string UrlEncode(std::string_view s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

inline std::string Lower(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

enum class State { Pending, Done, Missing, Failed };
struct Entry { State state = State::Pending; Stats stats; };

#ifdef _WIN32
// One GET over HTTPS. status 0 = network failure.
inline bool HttpGet(const std::string& host, const std::string& path, int& status, std::string& body)
{
    status = 0; body.clear();
    struct H { HINTERNET v; ~H() { if (v) WinHttpCloseHandle(v); } };
    const std::wstring whost(host.begin(), host.end()), wpath(path.begin(), path.end());
    H session{WinHttpOpen(L"OriginX/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.v || !WinHttpSetTimeouts(session.v, 5000, 5000, 8000, 8000)) return false;
    H connection{WinHttpConnect(session.v, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.v) return false;
    H request{WinHttpOpenRequest(connection.v, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.v || !WinHttpSendRequest(request.v, L"Accept: application/json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.v, nullptr)) return false;
    DWORD code = 0, size = sizeof code;
    if (!WinHttpQueryHeaders(request.v, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX)) return false;
    status = static_cast<int>(code);
    char buffer[4096];
    for (;;)
    {
        DWORD read = 0;
        if (!WinHttpReadData(request.v, buffer, sizeof buffer, &read) || !read) break;
        body.append(buffer, read);
        if (body.size() > 65536) break;   // a player's stats are tiny
    }
    return true;
}
#endif

// Looks players up in the background, one request at a time with a short pause (the API is rate limited).
class Fetcher {
    struct Slot { Entry entry; std::chrono::steady_clock::time_point at; bool queued = false; };
    std::mutex mutex_;
    std::map<std::string, Slot> cache_;
    std::deque<std::pair<std::string, std::string>> queue_;   // key, game/name path
    std::atomic<bool> running_{false};

    static std::string Key(const std::string& game, const std::string& name) { return game + "/" + Lower(name); }

    void Work()
    {
        for (;;)
        {
            std::string key, game, name;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.empty()) { running_ = false; return; }
                key = queue_.front().first;
                const std::string& packed = queue_.front().second;
                const size_t slash = packed.find('/');
                game = packed.substr(0, slash);
                name = packed.substr(slash + 1);
                queue_.pop_front();
            }
            Entry result;
#ifdef _WIN32
            int status = 0;
            std::string body;
            const bool ok = HttpGet("api.playhive.com", "/v0/game/all/" + game + "/" + UrlEncode(name), status, body);
            if (ok && status == 200 && ParseStats(body, result.stats)) result.state = State::Done;
            else if (ok && (status == 404 || status == 200)) result.state = State::Missing;   // never played this game
            else { result.state = State::Failed; if (ok && status == 429) std::this_thread::sleep_for(std::chrono::seconds(3)); }
#else
            result.state = State::Failed;
#endif
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Slot& slot = cache_[key];
                slot.entry = result;
                slot.at = std::chrono::steady_clock::now();
                slot.queued = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    }

public:
    // Current knowledge about a player; starts a lookup when there is none (or it is old).
    Entry Get(const std::string& game, const std::string& name)
    {
        const std::string key = Key(game, name);
        const auto now = std::chrono::steady_clock::now();
        bool start = false;
        Entry out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Slot& slot = cache_[key];
            const auto age = now - slot.at;
            const bool stale = slot.at.time_since_epoch().count() != 0 &&
                age > (slot.entry.state == State::Done || slot.entry.state == State::Missing ? std::chrono::minutes(10) : std::chrono::seconds(30));
            if (!slot.queued && (slot.at.time_since_epoch().count() == 0 || stale))
            {
                slot.queued = true;
                queue_.emplace_back(key, game + "/" + name);
                start = !running_.exchange(true);
            }
            out = slot.entry;
        }
        if (start) std::thread([this] { Work(); }).detach();
        return out;
    }
    void Clear() { std::lock_guard<std::mutex> lock(mutex_); cache_.clear(); queue_.clear(); }
};

}

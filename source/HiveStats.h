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

// One table for every game: the API key, the HUD name, the level parameters and what the HUD shows.
// level false = no level in this game (Lvl "-"); kdr false = no kills/deaths (KDR "-").
// alias: another API key whose numbers are added to this game. Append new games at the end: the config stores
// the index. listed false = not one of the games of the table (author's decision 2026-11-19): it cannot be picked
// or detected and only counts in All Games. The official 13 are bed/dr/hide/murder/sg/sky/build/ground/drop/ctf/
// bridge/party/grav; sky is today's Sky Wars only; sky-classic, sky-kits and wars are ended modes (totals only);
// parkour joins the table only once the "ゲーム判定:" log tells it apart from the lobby (until then totals only).
struct Game { const char* id; const char* name; int maxLevel; double increment; int cap; double multiplier;
              bool level, kdr; const char* alias[2]; bool listed = true; };

// Level parameters as published with the API data (hive-bedrock-data). cap 0 = no cap.
constexpr Game kGames[] = {
    {"bed",     "Bed Wars",         100, 150, 52, 0.0,  true,  true,  {}},
    {"sky",     "Sky Wars",         100, 150, 52, 0.0,  true,  true,  {}},
    {"wars",    "Treasure Wars",    100, 150, 52, 0.0,  true,  true,  {}, false},
    {"bridge",  "The Bridge",        20, 300,  0, 1.08, true,  true,  {}},
    {"murder",  "Murder Mystery",   100, 100, 82, 0.0,  true,  true,  {}},
    {"sg",      "Survival Games",    50, 150,  0, 0.0,  true,  true,  {}},
    {"ctf",     "Capture the Flag",  50, 150,  0, 0.0,  true,  true,  {}},
    {"dr",      "Deathrun",          75, 200, 42, 0.0,  true,  true,  {}},
    {"hide",    "Hide and Seek",    100, 100,  0, 0.0,  true,  true,  {}},
    {"ground",  "Ground Wars",       20, 150,  0, 0.0,  true,  true,  {}},
    {"build",   "Just Build",         0,   0,  0, 0.0,  false, false, {}},
    {"party",   "Block Party",        0,   0,  0, 0.0,  false, false, {}},
    {"grav",    "Gravity",            0,   0,  0, 0.0,  false, false, {}},
    {"drop",    "Block Drop",         0,   0,  0, 0.0,  false, false, {}},
    {"parkour", "Parkour",            0,   0,  0, 0.0,  false, false, {}, false},
    {"sky-classic", "Sky Wars Classic", 100, 150, 52, 0.0, true, true, {}, false},
    {"sky-kits",    "Sky Wars Kits",    100, 150, 52, 0.0, true, true, {}, false},
};
constexpr int kAllGames = -1;   // the "All Games" pick: totals over every game
constexpr int kHidden = -2;     // the "Hide" pick: no table while the game is unknown
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
        {"bedwars", 0}, {"skywars", 1}, {"sky", 1},
        {"thebridge", 3}, {"bridge", 3}, {"murdermystery", 4}, {"murder", 4}, {"survivalgames", 5}, {"survival", 5},
        {"capturetheflag", 6}, {"ctf", 6}, {"deathrun", 7}, {"hideandseek", 8}, {"hide", 8}, {"groundwars", 9}, {"ground", 9},
    };
    for (const auto& k : keys) if (t.find(k.word) != std::string::npos) return k.game;
    return -1;
}
// prestige: how many times the player reset from max level (Bed Wars etc.). Each prestige starts the
// level (and the xp the API reports) again at 1, so the level alone hides it: compare prestige first.
struct Stats { double xp = 0; uint32_t played = 0, victories = 0, kills = 0, deaths = 0, prestige = 0; bool hasKd = false; };

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

inline uint32_t Count(double v) { return v < 0 ? 0 : v > 4e9 ? 4000000000u : static_cast<uint32_t>(v); }

// A player's stats object for one game. Needs at least one stat field; missing ones count as 0 (like the API
// wrapper). Hide and Seek and Murder Mystery have no "kills": their kill fields are added up instead.
inline bool ParseStats(std::string_view json, Stats& out, std::string_view game = {})
{
    Stats s;
    double v = 0;
    bool any = false;
    if (ParseNumber(json, "xp", v))        { s.xp = v < 0 ? 0 : v; any = true; }
    if (ParseNumber(json, "played", v))    { s.played = Count(v); any = true; }
    if (ParseNumber(json, "victories", v)) { s.victories = Count(v); any = true; }
    const char* const* kills = nullptr;
    static const char* const plain[] = {"kills", nullptr};
    static const char* const hide[] = {"hider_kills", "seeker_kills", nullptr};
    static const char* const murder[] = {"murders", "murderer_eliminations", nullptr};
    kills = game == "hide" ? hide : game == "murder" ? murder : plain;
    for (; *kills; ++kills)
        if (ParseNumber(json, *kills, v)) { s.kills += Count(v); s.hasKd = any = true; }
    if (ParseNumber(json, "deaths", v))    { s.deaths = Count(v); s.hasKd = any = true; }
    if (ParseNumber(json, "prestige", v))  s.prestige = Count(v);   // only some games have it
    if (!any) return false;
    out = s;
    return true;
}

inline void Add(Stats& to, const Stats& s)
{
    to.xp += s.xp; to.played += s.played; to.victories += s.victories;
    if (s.hasKd) { to.kills += s.kills; to.deaths += s.deaths; to.hasKd = true; }
    to.prestige = std::max(to.prestige, s.prestige);
}

// Calls f(key, value) for each member of the top-level JSON object (value as raw text). False on bad JSON.
template <class F> bool Members(std::string_view json, F&& f)
{
    size_t i = 0;
    const auto space = [&] { while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) i++; };
    const auto string = [&](std::string* out) {   // at a quote: skips the string
        if (i >= json.size() || json[i] != '"') return false;
        for (i++; i < json.size(); i++) {
            if (json[i] == '\\') { if (++i >= json.size()) return false; if (out) out->push_back(json[i]); continue; }
            if (json[i] == '"') { i++; return true; }
            if (out) out->push_back(json[i]);
        }
        return false;
    };
    space();
    if (i >= json.size() || json[i] != '{') return false;
    i++;
    for (;;) {
        space();
        if (i < json.size() && json[i] == '}') return true;
        std::string key;
        if (!string(&key)) return false;
        space();
        if (i >= json.size() || json[i] != ':') return false;
        i++; space();
        const size_t start = i;
        int depth = 0;
        while (i < json.size()) {
            const char c = json[i];
            if (c == '"') { if (!string(nullptr)) return false; continue; }
            if (c == '{' || c == '[') depth++;
            else if (c == '}' || c == ']') { if (depth == 0) break; depth--; }
            else if (c == ',' && depth == 0) break;
            i++;
        }
        if (i >= json.size() || depth != 0) return false;
        size_t end = i;
        while (end > start && (json[end - 1] == ' ' || json[end - 1] == '\n' || json[end - 1] == '\r' || json[end - 1] == '\t')) end--;
        f(std::string_view(key), json.substr(start, end - start));
        if (json[i] == ',') i++;
    }
}

constexpr int kGameCount = static_cast<int>(sizeof(kGames) / sizeof(kGames[0]));
// What the "Game" list offers: Hide, All Games and the listed games.
inline bool Pickable(int game) { return game == kHidden || game == kAllGames || (game >= 0 && game < kGameCount && kGames[game].listed); }
// Row of a pickable entry in that list (Hide is row 0).
inline int PickRow(int game)
{
    int row = 0;
    for (int g = kHidden; g < game; g++) if (Pickable(g)) row++;
    return row;
}

// Everything /v0/game/all/all/<name> says about one player, sliced per game of kGames.
struct Profile { Stats game[kGameCount]; bool played[kGameCount] = {}; };

inline bool ParseAll(std::string_view json, Profile& out)
{
    Profile p;
    bool any = false;
    const bool ok = Members(json, [&](std::string_view key, std::string_view value) {
        for (int g = 0; g < kGameCount; g++) {
            const Game& game = kGames[g];
            const bool match = key == game.id || (game.alias[0] && key == game.alias[0]) || (game.alias[1] && key == game.alias[1]);
            if (!match) continue;
            Stats s;
            if (!value.empty() && value.front() == '{' && ParseStats(value, s, game.id)) {
                if (p.played[g]) Add(p.game[g], s); else p.game[g] = s;
                p.played[g] = any = true;
            }
        }
    });
    if (!ok) return false;
    (void)any;
    out = p;
    return true;
}

// All Games: wins and plays of every game; kills and deaths only of games that have them. No level.
inline Stats Total(const Profile& p)
{
    Stats t;
    for (int g = 0; g < kGameCount; g++) if (p.played[g]) Add(t, p.game[g]);
    t.xp = 0; t.prestige = 0;
    return t;
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
    if (!g.level) return 0;
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
struct Entry { State state = State::Pending; Profile profile; };

// When a player may be looked up again (one /all/all request covers every game):
//  - never asked yet: now;
//  - after the game changed since the last answer, or 30 s after a failure;
//  - but never more than 3 requests per player in 60 minutes, and nothing at all while a 429's Retry-After runs.
class Limiter {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr int kPerHour = 3;
    struct Player { std::vector<Clock::time_point> sent; Clock::time_point answeredAt{}; unsigned generation = 0; bool answered = false, failed = false; };
    static bool Due(const Player& p, unsigned generation, Clock::time_point now, Clock::time_point blockedUntil)
    {
        if (now < blockedUntil) return false;
        int recent = 0;
        for (const auto& t : p.sent) if (now - t < std::chrono::minutes(60)) recent++;
        if (recent >= kPerHour) return false;
        if (p.sent.empty()) return true;
        if (!p.answered) return false;   // still in flight
        if (p.failed) return now - p.answeredAt >= std::chrono::seconds(30);
        return p.generation != generation;
    }
    static void Sent(Player& p, unsigned generation, Clock::time_point now)
    {
        p.sent.erase(std::remove_if(p.sent.begin(), p.sent.end(), [&](auto t) { return now - t >= std::chrono::minutes(60); }), p.sent.end());
        p.sent.push_back(now); p.generation = generation; p.answered = false;
    }
};

#ifdef _WIN32
// One GET over HTTPS. status 0 = network failure.
// `session`, when given, holds the open session while the request runs, so another thread can close it to
// abort the request at once (Fetcher::Stop); whoever takes it out of the slot closes it.
inline bool HttpGet(const std::string& host, const std::string& path, int& status, std::string& body, int* retryAfter = nullptr,
                    std::atomic<HINTERNET>* session = nullptr)
{
    status = 0; body.clear();
    if (retryAfter) *retryAfter = 0;
    struct H { HINTERNET v; ~H() { if (v) WinHttpCloseHandle(v); } };
    const std::wstring whost(host.begin(), host.end()), wpath(path.begin(), path.end());
    struct S { HINTERNET v; std::atomic<HINTERNET>* slot;
               ~S() { if (v && (!slot || slot->exchange(nullptr) == v)) WinHttpCloseHandle(v); } };
    S own{WinHttpOpen(L"OriginX/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), session};
    if (!own.v || !WinHttpSetTimeouts(own.v, 5000, 5000, 8000, 8000)) return false;
    if (session) session->store(own.v);
    H connection{WinHttpConnect(own.v, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.v) return false;
    H request{WinHttpOpenRequest(connection.v, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.v || !WinHttpSendRequest(request.v, L"Accept: application/json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.v, nullptr)) return false;
    DWORD code = 0, size = sizeof code;
    if (!WinHttpQueryHeaders(request.v, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX)) return false;
    status = static_cast<int>(code);
    if (retryAfter && status == 429) {
        DWORD seconds = 0, bytes = sizeof seconds;
        if (WinHttpQueryHeaders(request.v, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &seconds, &bytes, WINHTTP_NO_HEADER_INDEX))
            *retryAfter = static_cast<int>(std::min<DWORD>(seconds, 3600));
    }
    char buffer[4096];
    for (;;)
    {
        DWORD read = 0;
        if (!WinHttpReadData(request.v, buffer, sizeof buffer, &read) || !read) break;
        body.append(buffer, read);
        if (body.size() > 262144) break;   // every game of one player is a few KB
    }
    return true;
}
#endif

// Looks players up in the background, one request at a time with a short pause (the API is rate limited).
class Fetcher {
    struct Slot { Entry entry; Limiter::Player limit; bool queued = false; };
    std::mutex mutex_;
    std::map<std::string, Slot> cache_;
    // Keys (lower-case names) waiting, each with the game generation it was queued in. A key queued before
    // the game changed is dropped when it comes up (2026-11-20 report #12): it would spend the shared request
    // pace on the old match. Dropping it sends nothing, so the per-player limit and the cache are untouched;
    // the next Get queues it again for the new game.
    struct Queued { std::string key; unsigned generation; };
    std::deque<Queued> queue_;
    std::map<std::string, std::string> names_;   // key -> name as written
    std::atomic<bool> running_{false};
    // The worker is joined, never detached (2026-11-20, Cl#7): Stop at HUD exit ends it before the Fetcher goes.
    std::thread worker_;
    bool stopping_ = false;
#ifdef _WIN32
    std::atomic<HINTERNET> session_{nullptr};   // the request in flight, closed by Stop to end it at once
#endif
    unsigned generation_ = 0;
    Limiter::Clock::time_point blockedUntil_{};

    void Work()
    {
        for (;;)
        {
            std::string key, name;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_ || queue_.empty() || Limiter::Clock::now() < blockedUntil_) {
                    for (const auto& q : queue_) cache_[q.key].queued = false;
                    queue_.clear(); running_ = false; return;
                }
                const Queued next = queue_.front();
                queue_.pop_front();
                key = next.key;
                if (next.generation != generation_) { cache_[key].queued = false; continue; }
                name = names_[key];
                Limiter::Sent(cache_[key].limit, generation_, Limiter::Clock::now());
            }
            Entry result;
            int retryAfter = 0;
#ifdef _WIN32
            int status = 0;
            std::string body;
            const bool ok = HttpGet("api.playhive.com", "/v0/game/all/all/" + UrlEncode(name), status, body, &retryAfter, &session_);
            bool playedAny = false;
            if (ok && status == 200 && ParseAll(body, result.profile)) {
                for (bool p : result.profile.played) playedAny = playedAny || p;
                result.state = playedAny ? State::Done : State::Missing;
            }
            else if (ok && status == 404) result.state = State::Missing;   // no such player
            else result.state = State::Failed;
            if (ok && status == 429 && retryAfter <= 0) retryAfter = 60;
            if (!(ok && status == 429)) retryAfter = 0;
#else
            result.state = State::Failed;
#endif
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Slot& slot = cache_[key];
                const bool keepOld = result.state == State::Failed && slot.entry.state == State::Done;
                if (!keepOld) slot.entry = result;   // an earlier answer beats a failed refresh
                slot.limit.answered = true;
                slot.limit.failed = result.state == State::Failed;
                slot.limit.answeredAt = Limiter::Clock::now();
                slot.queued = false;
                if (retryAfter > 0) blockedUntil_ = Limiter::Clock::now() + std::chrono::seconds(retryAfter);   // stops every player
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    }

public:
    // Current knowledge about a player; starts a lookup when the limiter allows one.
    Entry Get(const std::string& name)
    {
        const std::string key = Lower(name);
        bool start = false;
        Entry out;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Slot& slot = cache_[key];
            if (!stopping_ && !slot.queued && Limiter::Due(slot.limit, generation_, Limiter::Clock::now(), blockedUntil_))
            {
                slot.queued = true;
                names_[key] = name;
                queue_.push_back({key, generation_});
                start = !running_.exchange(true);
            }
            out = slot.entry;
            // The last worker has returned (running_ was false): join it before starting the next one.
            if (start) { if (worker_.joinable()) worker_.join(); worker_ = std::thread([this] { Work(); }); }
        }
        return out;
    }
    // The match's game changed: everyone may be refreshed once (still within the per-player limit).
    void GameChanged() { std::lock_guard<std::mutex> lock(mutex_); generation_++; }
    // HUD exit: no new lookups, the one in flight is aborted, and the worker is joined.
    void Stop()
    {
        std::thread worker;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            worker = std::move(worker_);
        }
#ifdef _WIN32
        if (HINTERNET s = session_.exchange(nullptr)) WinHttpCloseHandle(s);
#endif
        if (worker.joinable()) worker.join();
    }
    ~Fetcher() { Stop(); }
};

}

#pragma once
// sigs at run time (2026-11-11): the tables tools/make_sigs.py writes from tools/sigs.json (client/GameSigs.inc), and
// the pattern work of the start-up self check (client/GameSigs.cpp writes logs/startup.log and logs/sigcheck.log).
// No Windows here: tests/game_sigs.cpp runs it on the host.
//
//   Build   a supported Minecraft build (exe PE timestamp + SizeOfImage), in sigs.json's "supported" order
//   Sig     one game function the DLL uses: what for, what stops working without it, fatal (MyClient does not start)
//   At      that function in one build: its RVA and its byte pattern ("48 8B ?? ..", ?? = may differ between builds)
//   Offset  a game-version-dependent number that is not a function (a base-class offset), per build
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace GameSigs {

struct Build { int index; const char* version; uint32_t timestamp, imageSize; };
struct Sig { int index; const char* name; bool fatal; const char* purpose; const char* breaks; const char* lastUpdated; };
struct At { int sig, build; uint32_t rva; const char* pattern; };
struct Offset { const char* name; int build; uint32_t value; };

#define MYCLIENT_SIG_BUILD(index, version, timestamp, size) {index, version, timestamp, size},
#define MYCLIENT_SIG(index, name, fatal, purpose, breaks, last)
#define MYCLIENT_SIG_AT(sig, build, rva, pattern)
#define MYCLIENT_SIG_OFFSET(name, build, value)
inline constexpr Build kBuilds[] = {
#include "GameSigs.inc"
};
#undef MYCLIENT_SIG_BUILD
#undef MYCLIENT_SIG
#define MYCLIENT_SIG_BUILD(index, version, timestamp, size)
#define MYCLIENT_SIG(index, name, fatal, purpose, breaks, last) {index, name, fatal, purpose, breaks, last},
inline constexpr Sig kSigs[] = {
#include "GameSigs.inc"
};
#undef MYCLIENT_SIG
#undef MYCLIENT_SIG_AT
#define MYCLIENT_SIG(index, name, fatal, purpose, breaks, last)
#define MYCLIENT_SIG_AT(sig, build, rva, pattern) {sig, build, rva, pattern},
inline constexpr At kAts[] = {
#include "GameSigs.inc"
};
#undef MYCLIENT_SIG_AT
#undef MYCLIENT_SIG_OFFSET
#define MYCLIENT_SIG_AT(sig, build, rva, pattern)
#define MYCLIENT_SIG_OFFSET(name, build, value) {name, build, value},
inline constexpr Offset kOffsets[] = {
#include "GameSigs.inc"
};
#undef MYCLIENT_SIG_BUILD
#undef MYCLIENT_SIG
#undef MYCLIENT_SIG_AT
#undef MYCLIENT_SIG_OFFSET
inline constexpr size_t kBuildCount = sizeof(kBuilds) / sizeof(kBuilds[0]);
inline constexpr size_t kSigCount = sizeof(kSigs) / sizeof(kSigs[0]);
inline constexpr size_t kAtCount = sizeof(kAts) / sizeof(kAts[0]);
inline constexpr size_t kOffsetCount = sizeof(kOffsets) / sizeof(kOffsets[0]);

// Index of the build with that timestamp and image size, or -1 (a game version sigs.json does not support).
inline int FindBuild(uint32_t timestamp, uint32_t imageSize)
{
    for (const Build& b : kBuilds)
        if (b.timestamp == timestamp && b.imageSize == imageSize) return b.index;
    return -1;
}

inline const At* AtFor(int sig, int build)
{
    for (const At& a : kAts)
        if (a.sig == sig && a.build == build) return &a;
    return nullptr;
}

// The newest build's entry of a signature (what a new game version is searched with).
inline const At* Newest(int sig)
{
    const At* best = nullptr;
    for (const At& a : kAts)
        if (a.sig == sig && (!best || a.build > best->build)) best = &a;
    return best;
}

inline bool OffsetFor(const char* name, int build, uint32_t& value)
{
    for (const Offset& o : kOffsets)
        if (o.build == build && name && std::strcmp(o.name, name) == 0) { value = o.value; return true; }
    return false;
}

// "48 8B ?? 05" -> {0x48, 0x8B, -1, 0x05}. False (and empty) on anything else.
inline bool ParsePattern(const char* text, std::vector<int>& out)
{
    out.clear();
    if (!text) return false;
    for (const char* p = text; *p;)
    {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (p[0] == '?') { out.push_back(-1); p += p[1] == '?' ? 2 : 1; continue; }
        auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        const int hi = hex(p[0]), lo = p[1] ? hex(p[1]) : -1;
        if (hi < 0 || lo < 0 || (p[2] && p[2] != ' ')) { out.clear(); return false; }
        out.push_back(hi * 16 + lo);
        p += 2;
    }
    return !out.empty();
}

inline bool MatchAt(const uint8_t* at, size_t available, const std::vector<int>& pattern)
{
    if (!at || pattern.empty() || available < pattern.size()) return false;
    for (size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] >= 0 && at[i] != static_cast<uint8_t>(pattern[i])) return false;
    return true;
}

// The same, for a function another module may have hooked first (LeviLamina hooks some, e.g. ClientLevel::_subTick,
// before MyClient runs): when the code starts with a 5-byte jump, the first 5 bytes are not compared (as
// client/GameRva.h BytesMatch does). `hooked` says it was.
inline bool MatchAtMaybeHooked(const uint8_t* at, size_t available, const std::vector<int>& pattern, bool& hooked)
{
    hooked = false;
    if (MatchAt(at, available, pattern)) return true;
    if (!at || available < pattern.size() || pattern.size() <= 5 || at[0] != 0xE9) return false;
    std::vector<int> tail = pattern;
    for (size_t i = 0; i < 5; ++i) tail[i] = -1;
    size_t fixed = 0;
    for (int b : tail) fixed += b >= 0;
    hooked = fixed >= 6 && MatchAt(at, available, tail);
    return hooked;
}

// Where the pattern matches in [code, code + size): the offsets, at most `limit` of them (2 is enough to tell
// "once" from "several"). Searches for the longest fixed run of the pattern first.
inline std::vector<size_t> Scan(const uint8_t* code, size_t size, const std::vector<int>& pattern, size_t limit = 2)
{
    std::vector<size_t> found;
    if (!code || pattern.empty() || size < pattern.size()) return found;
    size_t bestAt = 0, bestLen = 0;
    for (size_t i = 0; i < pattern.size();)
    {
        if (pattern[i] < 0) { ++i; continue; }
        size_t j = i;
        while (j < pattern.size() && pattern[j] >= 0) ++j;
        if (j - i > bestLen) { bestAt = i; bestLen = j - i; }
        i = j;
    }
    if (!bestLen) return found;
    std::vector<uint8_t> anchor(bestLen);
    for (size_t k = 0; k < bestLen; ++k) anchor[k] = static_cast<uint8_t>(pattern[bestAt + k]);
    const std::boyer_moore_horspool_searcher searcher(anchor.begin(), anchor.end());
    const uint8_t* end = code + size;
    for (const uint8_t* it = code; it < end;)
    {
        const auto hit = std::search(it, end, searcher);
        if (hit == end) break;
        const size_t start = static_cast<size_t>(hit - code);
        if (start >= bestAt && MatchAt(hit - bestAt, size - (start - bestAt), pattern))
        {
            found.push_back(start - bestAt);
            if (found.size() >= limit) break;
        }
        it = hit + 1;
    }
    return found;
}

// One line of logs/sigcheck.log (tab separated): result, name, fatal, where, pattern, purpose / what breaks.
inline std::string ReportLine(const char* result, const Sig& s, const std::string& where, const char* pattern)
{
    return std::string(result) + "\t" + s.name + "\t" + (s.fatal ? "FATAL" : "-") + "\t" + where + "\t" +
           (pattern ? pattern : "") + "\t" + s.purpose + " / NG: " + s.breaks;
}

} // namespace GameSigs

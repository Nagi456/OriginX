#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <array>
#include <type_traits>

namespace MyClientParty {
constexpr uint32_t Magic = 0x5059434d, Version = 1, MaxPlayers = 64, TagBytes = 33;
constexpr uint64_t LeaseMs = 1000;
enum class Status : uint32_t { Waiting, Disabled, OtherServer, Ready, Sent, UnknownFormat, Failed };
struct Config { uint64_t at; uint32_t enabled, count; char tags[MaxPlayers][TagBytes]; };
struct Shared {
    uint32_t magic, version, size, pid;
    Config config;
    uint64_t at;
    Status status;
    uint32_t sent;
};
static_assert(sizeof(Config) == 2128 && sizeof(Shared) == 2160);
static_assert(std::is_standard_layout_v<Shared> && std::is_trivially_copyable_v<Shared>);
inline bool Fresh(uint64_t at, uint64_t now, uint64_t lease = LeaseMs) {
    return at && at <= now && now - at <= lease;
}
inline std::string Lower(std::string_view value) {
    std::string out(value);
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return out;
}
inline bool ValidTag(std::string_view tag) {
    if (tag.empty() || tag.size() >= TagBytes || tag.front() == ' ' || tag.back() == ' ') return false;
    for (unsigned char c : tag)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != ' ' && c != '_' && c != '-' && c != '#') return false;
    return true;
}
inline bool Valid(const Shared& s, uint32_t pid) {
    return s.magic == Magic && s.version == Version && s.size == sizeof(Shared) && s.pid == pid;
}
inline bool Active(const Config& c, uint64_t now) {
    if (!Fresh(c.at, now) || c.enabled != 1 || !c.count || c.count > MaxPlayers) return false;
    for (uint32_t i = 0; i < c.count; ++i) {
        const auto* end = static_cast<const char*>(std::memchr(c.tags[i], 0, TagBytes));
        if (!end || !ValidTag(std::string_view(c.tags[i], end - c.tags[i]))) return false;
    }
    return true;
}
inline bool Registered(const Config& c, std::string_view tag, uint64_t now) {
    if (!ValidTag(tag) || !Active(c, now)) return false;
    for (uint32_t i = 0; i < c.count; ++i) if (Lower(c.tags[i]) == Lower(tag)) return true;
    return false;
}
inline bool HiveHost(std::string_view host) {
    auto h = Lower(host);
    if (!h.empty() && h.back() == '.') h.pop_back();
    return h == "geo.hivebedrock.network" || h == "hivebedrock.network" ||
        h == "as.hivebedrock.network" || h == "eu.hivebedrock.network" || h == "sg.hivebedrock.network" ||
        h == "ca.hivebedrock.network" || h == "us.hivebedrock.network" || h == "na.hivebedrock.network";
}
inline std::string Clean(std::string_view input) {
    if (input.size() > 1024) return {};
    std::string out;
    for (size_t i = 0; i < input.size(); ++i) {
        if (static_cast<unsigned char>(input[i]) == 0xc2 && i + 1 < input.size() &&
            static_cast<unsigned char>(input[i+1]) == 0xa7) {
            if (i + 2 >= input.size()) return {};
            const char c = input[i+2];
            if (std::string_view("0123456789abcdefklmnorABCDEFKLMNOR").find(c) == std::string_view::npos) return {};
            i += 2; continue;
        }
        if (static_cast<unsigned char>(input[i]) < 32) return {};
        out += input[i];
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
// Candidate sentences are deliberately anchored. The user's Japanese wording
// is approximate; live Hive localization has not yet been captured.
inline std::string Invite(std::string_view message, unsigned packetType, bool authored, bool localized) {
    if (authored || localized || (packetType != 0 && packetType != 6)) return {};
    const auto text = Clean(message);
    constexpr std::string_view suffixes[] = {
        "からパーティへの招待が来ています", "からパーティへの招待が来ています。",
        "からパーティーへの招待が来ています", "からパーティーへの招待が来ています。",
        " has invited you to their party!", " has invited you to join their party!",
        " has invited you to their party.", " has invited you to join their party.",
        // Wording seen on a real invite (English): "<Gamertag> wants you to join their party!"
        " wants you to join their party!", " wants you to join their party.", " wants you to join their party"
    };
    for (auto suffix : suffixes) {
        if (text.size() > suffix.size() && text.compare(text.size()-suffix.size(), suffix.size(), suffix) == 0) {
            auto tag = text.substr(0, text.size()-suffix.size());
            if (ValidTag(tag)) return tag;
        }
    }
    return {};
}
inline std::string AcceptCommand(std::string_view tag) {
    if (!ValidTag(tag)) return {};
    // Hive officially documents /party accept [player]. Quotes preserve spaces.
    return "/party accept \"" + std::string(tag) + "\"";
}
class Gate {
    struct Entry { std::string tag; uint64_t at = 0; };
    std::array<Entry, MaxPlayers> history{};
    uint64_t lastAt = 0;
public:
    void Reset() { for (auto& entry : history) entry = {}; lastAt = 0; }
    bool Allow(std::string_view tag, uint64_t now) const {
        if (!ValidTag(tag) || (lastAt && (now < lastAt || now-lastAt < 2000))) return false;
        for (const auto& entry : history)
            if (entry.at && entry.tag == Lower(tag) && (now < entry.at || now-entry.at < 30000)) return false;
        return true;
    }
    void Sent(std::string_view tag, uint64_t now) {
        auto* oldest = &history[0];
        for (auto& entry : history) {
            if (entry.tag == Lower(tag)) { oldest = &entry; break; }
            if (entry.at < oldest->at) oldest = &entry;
        }
        *oldest = {Lower(tag), now}; lastAt = now;
    }
};
}

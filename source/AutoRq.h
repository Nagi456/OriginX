#pragma once
// Auto RQ: when the local player dies on Hive, queue the next game with "/q <MODE>".
// Pure logic (no game or Windows types) so it can be tested on the host.
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
namespace MyClientAutoRq {
// Mode numbers are written by the HUD (Command.autoRq); 0 = off.
// `team`: the mode is played in teams, so Auto RQ waits until the whole team is out (everyone spectating)
// instead of firing on your own death. Hive only lets a party leader queue; a member's "/q" is refused.
struct Mode { const char* name; const char* command; bool team; };
constexpr Mode kModes[] = {
    {"Sky Wars Solo", "q SKY", false}, {"Sky Wars Duos", "q SKY-DUOS", true}, {"Sky Wars Squads", "q SKY-SQUADS", true},
    {"Sky Wars Mega", "q SKY-MEGA", true},
    {"Bed Wars Solo", "q BED", false}, {"Bed Wars Duos", "q BED-DUOS", true}, {"Bed Wars Squads", "q BED-SQUADS", true},
    {"Bed Wars Mega", "q BED-MEGA", true},
    {"Bed Wars Duels 1v1", "q BED-DUELS", false}, {"Bed Wars Duels 2v2", "q BED-DUELS-DUOS", true},
};
constexpr uint32_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);
// `mode` is 1-based on the wire. Returns nullptr when off / out of range.
inline const Mode* Find(uint32_t mode) { return mode >= 1 && mode <= kModeCount ? &kModes[mode - 1] : nullptr; }
// The command as the game's command packet wants it: with the leading slash.
inline std::string Command(uint32_t mode) { const auto* m = Find(mode); return m ? std::string("/") + m->command : std::string(); }

inline bool TeamMode(uint32_t mode) { const auto* m = Find(mode); return m && m->team; }

// --- Team elimination (team modes) ---------------------------------------------------------------
// Hive posts "<Colour> Team has been eliminated!" ("<色>チームは全滅した！") when a team is out. Auto RQ
// compares the Minecraft colour code of that team name with the colour of your own name tag, so the
// colour's spelling or language does not matter. Colour codes are "§" (UTF-8 C2 A7) + 0-9 / a-f;
// k-o (formatting) keep the colour, r resets it.
inline bool IsSection(std::string_view s, size_t i) {
    return i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xC2 && static_cast<unsigned char>(s[i + 1]) == 0xA7;
}
inline char ColorCode(char c) {
    if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ? c : 0;
}

// Visible text plus the colour code in effect for each of its bytes (0 = default colour).
struct Colored { std::string text; std::string colors; };
inline Colored Decode(std::string_view raw) {
    Colored out;
    char color = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (IsSection(raw, i)) {
            const char c = raw[i + 2];
            if (ColorCode(c)) color = ColorCode(c);
            else if (c == 'r' || c == 'R') color = 0;
            i += 2;
            continue;
        }
        out.text += raw[i];
        out.colors += color;
    }
    return out;
}

// The colour most of a name tag is drawn in (a rank icon in front of the name does not win). 0 = none.
inline char NameColor(std::string_view nametag) {
    const auto d = Decode(nametag);
    int counts[128] = {};
    char best = 0;
    for (size_t i = 0; i < d.text.size(); ++i) {
        const char c = d.colors[i];
        if (!c || d.text[i] == ' ' || d.text[i] == '[' || d.text[i] == ']') continue;
        if (++counts[static_cast<unsigned char>(c)] > (best ? counts[static_cast<unsigned char>(best)] : 0)) best = c;
    }
    return best;
}

struct Elimination { bool found = false; char color = 0; std::string team; std::string text; };
// Reads "<Colour> Team has been eliminated!" / "<色>チームは全滅した！": `color` is the colour code of
// the team's name (0 if the message carries none), `team` the name as written.
inline Elimination TeamEliminated(std::string_view raw) {
    Elimination e;
    const auto d = Decode(raw);
    e.text = d.text;
    std::string lower = d.text;
    for (auto& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    size_t name = std::string::npos, end = std::string::npos;
    if (const auto p = lower.find(" team has been eliminated"); p != std::string::npos) {
        end = p;
        name = lower.rfind(' ', p == 0 ? 0 : p - 1);
        name = name == std::string::npos ? 0 : name + 1;
    } else if (const auto j = lower.find("\xE3\x83\x81\xE3\x83\xBC\xE3\x83\xA0\xE3\x81\xAF\xE5\x85\xA8\xE6\xBB\x85");   // チームは全滅
               j != std::string::npos) {
        end = j;
        name = 0;
        while (name < j && (lower[name] == ' ' || lower[name] == '[')) ++name;
    }
    if (name == std::string::npos || name >= end) return e;
    e.found = true;
    e.team = d.text.substr(name, end - name);
    while (!e.team.empty() && (e.team.back() == ' ' || e.team.back() == ']')) e.team.pop_back();
    while (!e.team.empty() && e.team.front() == '[') e.team.erase(e.team.begin());
    e.color = d.colors[name];
    return e;
}

// End of a Hive game: "<player or team> is the WINNER!" (sent to everyone in the game, whoever won).
// Auto RQ queues on it so a game you win also leads to the next one.
inline bool GameOver(std::string_view raw) {
    std::string lower = Decode(raw).text;
    for (auto& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return lower.find(" is the winner") != std::string::npos;
}

// In a party, once every member is dead: "All members of your party are now spectating" (English game
// language). Your whole team is out, so Auto RQ queues at once (the party leader's "/q" takes everyone).
inline bool PartyAllSpectating(std::string_view raw) {
    std::string lower = Decode(raw).text;
    for (auto& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return lower.find("all members of your party are now spectating") != std::string::npos;
}

// Fires once per death. Alive -> dead edge (dead = no longer alive or became a spectator) while the lease is on.
// After firing it stays quiet until the player has been seen alive again (next game), and never fires twice
// within `cooldownMs`. A death seen on the very first sample (e.g. joining as a spectator) is not an edge.
class Gate {
    bool aliveSeen_ = false, fired_ = false;
    uint64_t lastFire_ = 0;
public:
    static constexpr uint64_t kCooldownMs = 8000;
    void Reset() { aliveSeen_ = false; fired_ = false; }
    bool Update(bool enabled, bool dead, uint64_t now) {
        if (!dead) { aliveSeen_ = true; fired_ = false; return false; }
        if (!enabled || !aliveSeen_ || fired_) return false;
        if (lastFire_ != 0 && now - lastFire_ < kCooldownMs) return false;
        fired_ = true; aliveSeen_ = false; lastFire_ = now;
        return true;
    }
};
}

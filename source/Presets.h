#pragma once
// Presets: named snapshots of which modules are ON, saved to myclient_presets.txt (UTF-8, one per line):
//   <name> TAB <key>=<0|1>,<key>=<0|1>,...
// Keys are stable ids chosen by the HUD (the module's number, or a name for Hive Utils' switches).
// Pure text handling, so it is unit-tested on the host.
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Presets {

constexpr size_t MaxPresets = 8, MaxNameBytes = 64;

struct Preset {
    std::string name;                                   // UTF-8
    std::vector<std::pair<std::string, bool>> states;   // key -> ON
};

// A name that fits in one line: tabs / line breaks become spaces, trimmed, cut to MaxNameBytes
// without splitting a UTF-8 character.
inline std::string CleanName(std::string name)
{
    for (auto& c : name)
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.size() > MaxNameBytes) {
        size_t cut = MaxNameBytes;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
        name.resize(cut);
    }
    return name;
}

inline std::string Serialize(const Preset& preset)
{
    std::string line = CleanName(preset.name) + "\t";
    for (size_t i = 0; i < preset.states.size(); ++i) {
        if (i) line += ',';
        line += preset.states[i].first + (preset.states[i].second ? "=1" : "=0");
    }
    return line;
}

// False for a line that is not a preset (empty name, no tab).
inline bool Parse(const std::string& line, Preset& out)
{
    const auto tab = line.find('\t');
    if (tab == std::string::npos) return false;
    out = {};
    out.name = CleanName(line.substr(0, tab));
    if (out.name.empty()) return false;
    std::string rest = line.substr(tab + 1);
    while (!rest.empty() && (rest.back() == '\r' || rest.back() == '\n')) rest.pop_back();
    size_t at = 0;
    while (at < rest.size()) {
        size_t comma = rest.find(',', at);
        if (comma == std::string::npos) comma = rest.size();
        const std::string item = rest.substr(at, comma - at);
        const auto eq = item.find('=');
        if (eq != std::string::npos && eq > 0 && eq + 1 < item.size())
            out.states.emplace_back(item.substr(0, eq), item[eq + 1] == '1');
        at = comma + 1;
    }
    return true;
}

inline int CountOn(const Preset& preset)
{
    int n = 0;
    for (const auto& [key, on] : preset.states) n += on ? 1 : 0;
    return n;
}

}  // namespace Presets

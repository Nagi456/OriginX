#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace OriginMarks {
inline constexpr unsigned MaxUsers = 64, NameMax = 48;
inline constexpr unsigned GlyphPage = 0xE1;
inline constexpr char Glyph[] = {char(0xEE), char(0x80 | ((GlyphPage & 15) << 2) | 3), char(0xB0), 0};
inline constexpr char Colors[] = "\0afec69d";
struct Row { char name[NameMax]{}; uint8_t tier{}; };
struct Table { std::array<Row, MaxUsers> rows{}; unsigned count{}; Row self{}; };
inline const Row* MatchMarkName(std::string_view text, const Table& table) {
    char plain[512];
    size_t size = 0;
    for (size_t i = 0; i < text.size();) {
        if (text.substr(i, 2) == "\xC2\xA7" && i + 2 < text.size()) { i += 3; continue; }
        if (size == sizeof plain) return nullptr;
        plain[size++] = text[i++];
    }
    const std::string_view name(plain, size);
    const std::string_view own(table.self.name);
    if (!own.empty() && own == name) return &table.self;
    const bool hasOwn = !own.empty() && name.find(own) != name.npos;
    const Row* best = hasOwn ? &table.self : nullptr;
    size_t length = hasOwn ? own.size() : 0;
    for (unsigned i = 0; i < table.count; ++i) {
        const auto& row = table.rows[i];
        const std::string_view candidate(row.name);
        if (candidate.empty()) continue;
        if (candidate == name) return &row;
        if (candidate.size() > length && name.find(candidate) != name.npos) {
            best = &row;
            length = candidate.size();
        }
    }
    return best;
}
inline bool Prefix(std::string_view text, unsigned tier, char (&out)[6]) {
    if (!tier || tier >= sizeof Colors - 1 || text.substr(0, 3) == Glyph) return false;
    out[0] = Glyph[0]; out[1] = Glyph[1]; out[2] = Glyph[2];
    out[3] = char(0xC2); out[4] = char(0xA7); out[5] = Colors[tier];
    return true;
}
inline Table tables[2];
inline std::atomic<uint32_t> current{0}, readers{0};
inline std::atomic<bool> visible{false};
inline std::atomic<uint64_t> statMarkAlloc{0};
// One producer. Readers pin before selecting the published table.
struct Read {
    Read() { ++readers; }
    ~Read() { --readers; }
    const Table& Get() const { static constexpr Table empty{}; return visible.load() ? tables[current.load()] : empty; }
};
inline void Publish(const Table& table) {
    if (!table.count && !table.self.tier) { visible.store(false); return; }
    const auto next = current.load() ^ 1u;
    if (readers.load()) return;
    tables[next] = table;
    current.store(next);
    visible.store(true);
}
}

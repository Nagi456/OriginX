#include "MarksPack.h"
#include <cassert>
#include <cstring>

int main() {
    OriginMarks::Table table;
    table.count = 2;
    std::strcpy(table.rows[0].name, "Ab");
    std::strcpy(table.rows[1].name, "Abc");
    assert(OriginMarks::MatchMarkName("Ab", table) == &table.rows[0]);
    assert(OriginMarks::MatchMarkName("[VIP] \xC2\xA7" "aAbc", table) == &table.rows[1]);
    assert(OriginMarks::MatchMarkName("A\xC2\xA7" "eb", table) == &table.rows[0]);
    assert(!OriginMarks::MatchMarkName("different", table));
    table.count = OriginMarks::MaxUsers;
    std::strcpy(table.rows.back().name, "Last");
    std::strcpy(table.self.name, "Self");
    table.self.tier = 1;
    assert(OriginMarks::MatchMarkName("Last", table) == &table.rows.back());
    assert(OriginMarks::MatchMarkName("Self", table) == &table.self);
    for (unsigned tier = 0; tier <= 15; ++tier) {
        char prefix[6]{};
        assert(OriginMarks::Prefix("Ab", tier, prefix) == (tier >= 1 && tier <= 7));
        if (tier >= 1 && tier <= 7) {
            assert(std::string_view(prefix, 3) == "\xEE\x87\xB0");
            assert(prefix[5] == " afec69d"[tier]);
            assert(!OriginMarks::Prefix(std::string_view(prefix, sizeof prefix), tier, prefix));
        }
    }
    assert(MarksPack::GlyphPath() == "font/glyph_E1.png");
    auto stack = MarksPack::WithEntry({}, true);
    assert(stack.size() == 1 && MarksPack::WithEntry(stack, true).size() == 1);
    assert(MarksPack::WithEntry(stack, false).empty());
}

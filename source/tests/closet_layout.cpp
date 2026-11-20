#include "../ClosetLayout.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    // Seven cards, including the last Nebula card, must fit inside every panel width.
    for (const auto width : {100.f, 416.f, 832.f, 1664.f}) {
        float previousRight = 24.f;
        for (int slot = 0; slot < 7; ++slot) {
            const int cape = slot == 1 ? 2 : slot == 2 ? 1 : slot;
            const auto card = ClosetLayout::CapeCard(24.f, 24.f + width, cape);
            assert(card.width > 0 && card.x >= previousRight);
            assert(card.x + card.width <= 24.f + width);
            assert(ClosetLayout::CapeSlot(cape) == slot);
            for (float scale : {0.4f, 0.75f, 1.f, 1.25f, 2.f}) {
                // Same rounding as GlassLayout::R, also used by the click handler.
                assert(std::lround(card.x * scale) >= std::lround(24.f * scale));
                assert(std::lround((card.x + card.width) * scale) <= std::lround((24.f + width) * scale));
            }
            previousRight = card.x + card.width;
        }
    }
    // The 880-wide panel's 24-unit page margins and 20-unit row margins stay clear.
    const auto first = ClosetLayout::CapeCard(24, 856, 0);
    const auto last = ClosetLayout::CapeCard(24, 856, 6);
    assert(first.x == 44 && std::fabs(last.x + last.width - 836) < 0.001f);
    assert(ClosetLayout::CapeCard(20, 20, 0).width == 0);
    assert(ClosetLayout::CapeCard(20, 10, 0).width == 0);
    std::puts("closet layout ok");
}

#pragma once
#include <algorithm>

namespace ClosetLayout {
inline int CapeSlot(int cape) { return cape == 2 ? 1 : cape == 1 ? 2 : cape; }
struct Card { float x, width; };
inline Card CapeCard(float left, float right, int cape)
{
    const float available = std::max(0.f, right - left);
    const float margin = std::min(20.f, available / 8);
    const float gap = std::min(10.f, available / 20);
    const float width = (available - 2 * margin - 6 * gap) / 7;
    return {left + margin + CapeSlot(cape) * (width + gap), width};
}
}

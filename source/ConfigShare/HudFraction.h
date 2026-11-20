// HUD positions in share codes are fractions of the game window x10000
// (docs/config-sharing-design.md §3.4). Windows-independent.
#pragma once

#include <cstdint>

namespace ConfigShare {

constexpr int32_t FractionScale = 10000;
constexpr int HudGrabMargin = 32;   // keep at least this many pixels on screen to drag

// Pixel -> fraction (0..10000). size <= 0 is unusable: the caller leaves the HUD
// category out, and this returns 0.
inline int32_t ToFraction(int px, int size)
{
    if (size <= 0) return 0;
    const int64_t f = (int64_t(px) * FractionScale * 2 + size) / (int64_t(size) * 2);   // round half up
    return f < 0 ? 0 : (f > FractionScale ? FractionScale : int32_t(f));
}

// Fraction -> pixel, kept inside [0, max(0, size - 32)].
inline int FromFraction(int32_t f, int size)
{
    if (size <= 0) return 0;
    if (f < 0) f = 0;
    if (f > FractionScale) f = FractionScale;
    int64_t px = (int64_t(f) * size * 2 + FractionScale) / (int64_t(FractionScale) * 2);
    const int hi = size > HudGrabMargin ? size - HudGrabMargin : 0;
    if (px > hi) px = hi;
    return int(px);
}

} // namespace ConfigShare

#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

// The classic player model (geometry.humanoid.custom / customSlim) as boxes, for the Closet's turning preview.
// Units are skin pixels of a 64-wide skin: y up from the feet, the player faces +z (the viewer, unturned),
// so -x is the player's right (the viewer's left).
namespace SkinModel {

struct Box {
    float x0, y0, z0, x1, y1, z1;   // extent, the outer layers already grown
    int u, v, w, h, d;              // where the box's net starts in the skin and its size in skin pixels
    bool mirror;                    // a 64x32 skin's left limb: the right limb's picture flipped
    bool outer;                     // hat, jacket, sleeves, trousers (see-through where the skin is)
};

// Base boxes first, then the outer layers. A 64x32 (legacy) skin has no outer layer but the hat.
inline std::vector<Box> Boxes(bool slim, bool modern) {
    const float aw = slim ? 3.0f : 4.0f;
    const int ai = slim ? 3 : 4;
    struct Part { float x0, y0, z0, x1, y1, z1; int u, v, w, h, d, ou, ov; bool mirror; float grow; };
    const Part parts[] = {
        {-4, 24, -4, 4, 32, 4, 0, 0, 8, 8, 8, 32, 0, false, 0.5f},                                          // head, hat
        {-4, 12, -2, 4, 24, 2, 16, 16, 8, 12, 4, modern ? 16 : -1, 32, false, 0.25f},                       // body, jacket
        {-4 - aw, 12, -2, -4, 24, 2, 40, 16, ai, 12, 4, modern ? 40 : -1, 32, false, 0.25f},                // right arm
        {4, 12, -2, 4 + aw, 24, 2, modern ? 32 : 40, modern ? 48 : 16, ai, 12, 4, modern ? 48 : -1, 48, !modern, 0.25f},   // left arm
        {-4, 0, -2, 0, 12, 2, 0, 16, 4, 12, 4, modern ? 0 : -1, 32, false, 0.25f},                          // right leg
        {0, 0, -2, 4, 12, 2, modern ? 16 : 0, modern ? 48 : 16, 4, 12, 4, modern ? 0 : -1, 48, !modern, 0.25f},  // left leg
    };
    std::vector<Box> out;
    for (const Part& p : parts)
        out.push_back({p.x0, p.y0, p.z0, p.x1, p.y1, p.z1, p.u, p.v, p.w, p.h, p.d, p.mirror, false});
    for (const Part& p : parts)
        if (p.ou >= 0)
            out.push_back({p.x0 - p.grow, p.y0 - p.grow, p.z0 - p.grow, p.x1 + p.grow, p.y1 + p.grow, p.z1 + p.grow,
                           p.ou, p.ov, p.w, p.h, p.d, p.mirror, true});
    return out;
}

// Where on the skin (in 64-wide skin pixels, fractional) face `axis` (0 x, 1 y, 2 z) / `sign` of the box is
// hit at (x, y, z). Minecraft's box net: top and bottom above, then right side, front, left side, back.
inline void FaceSpot(const Box& b, int axis, int sign, float x, float y, float z, float& u, float& v) {
    const auto frac = [](float t, float a, float c) { return std::clamp((t - a) / (c - a), 0.0f, 0.9999f); };
    float fx = frac(x, b.x0, b.x1);
    const float fy = frac(y, b.y0, b.y1), fz = frac(z, b.z0, b.z1);
    if (b.mirror) { fx = 0.9999f - fx; if (axis == 0) sign = -sign; }
    const float W = (float)b.w, H = (float)b.h, D = (float)b.d;
    if (axis == 2 && sign > 0)      { u = D + fx * W;              v = D + (1 - fy) * H; }   // front
    else if (axis == 2)             { u = 2 * D + W + (1 - fx) * W; v = D + (1 - fy) * H; }   // back
    else if (axis == 0 && sign < 0) { u = fz * D;                  v = D + (1 - fy) * H; }   // player's right
    else if (axis == 0)             { u = D + W + (1 - fz) * D;    v = D + (1 - fy) * H; }   // player's left
    else if (sign > 0)              { u = D + fx * W;              v = fz * D; }             // top
    else                            { u = D + W + fx * W;          v = fz * D; }             // bottom
    // Never past the face's last pixel (fy = 0 would land on the next row down).
    u = b.u + std::max(0.0f, u - 0.001f);
    v = b.v + std::max(0.0f, v - 0.001f);
}

// The colour 0x00RRGGBB at that spot of an RGBA skin (64 or a multiple wide, square or half as tall);
// false where the skin is see-through.
inline bool Texel(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, const Box& b, int axis, int sign,
                  float x, float y, float z, uint32_t& rgb) {
    const uint32_t k = w / 64;
    if (!k || rgba.size() < (size_t)w * h * 4) return false;
    float u = 0, v = 0;
    FaceSpot(b, axis, sign, x, y, z, u, v);
    const uint32_t px = (uint32_t)(u * k), py = (uint32_t)(v * k);   // HD skins: the finer pixel under the spot
    if (px >= w || py >= h) return false;
    const uint8_t* p = &rgba[((size_t)py * w + px) * 4];
    if (p[3] < 128) return false;
    rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return true;
}

}  // namespace SkinModel

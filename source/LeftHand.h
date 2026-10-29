#pragma once
// Left Hand (DLL part): the game places the item in your first-person hand in its own code
// (ItemInHandRenderer), not with the pack's animations, so the pack alone moved only the bare arm.
// The DLL mirrors the item's world matrix across the middle of the screen instead. Plain 4x4 maths on
// column-major float[16] (glm's layout: m[col * 4 + row]), so it is unit-tested without the game.
#include <cmath>

namespace MyClientLeftHand {

inline void Multiply(const float a[16], const float b[16], float out[16])
{
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    for (int i = 0; i < 16; ++i) out[i] = r[i];
}

// General 4x4 inverse; false when the matrix cannot be inverted (or is not finite).
inline bool Invert(const float m[16], float out[16])
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!std::isfinite(det) || std::fabs(det) < 1e-12f) return false;
    for (int i = 0; i < 16; ++i) {
        out[i] = inv[i] / det;
        if (!std::isfinite(out[i])) return false;
    }
    return true;
}

// world := V^-1 * S * V * world * S, S = mirror of X. In view space (V * world) this is S * (V * world) * S:
// the item moves to the other side of the screen with its turn mirrored, and the second S keeps the
// model's triangle winding, so nothing is drawn inside-out. False (world untouched) if `view` is singular.
inline bool MirrorInView(const float view[16], float world[16])
{
    float inverse[16];
    if (!Invert(view, inverse)) return false;
    float viewWorld[16];
    Multiply(view, world, viewWorld);
    // S * M * S: negate the elements where exactly one of row / column is X.
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            if ((c == 0) != (row == 0)) viewWorld[c * 4 + row] = -viewWorld[c * 4 + row];
    float result[16];
    Multiply(inverse, viewWorld, result);
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(result[i])) return false;
    for (int i = 0; i < 16; ++i) world[i] = result[i];
    return true;
}

// world := V^-1 * S * V * world: whatever is drawn with this world matrix, and everything multiplied in after it,
// lands mirrored across the middle of the screen. Unlike MirrorInView there is no second S: at the start of the
// first-person pass the world matrix lines up with the camera, and S (V W) S then pictures the same as V W.
// The triangle winding flips (det < 0). False (world untouched) if `view` is singular.
inline bool MirrorWorldInView(const float view[16], float world[16])
{
    float inverse[16];
    if (!Invert(view, inverse)) return false;
    float viewWorld[16];
    Multiply(view, world, viewWorld);
    for (int c = 0; c < 4; ++c) viewWorld[c * 4] = -viewWorld[c * 4];   // S * M: negate row 0
    float result[16];
    Multiply(inverse, viewWorld, result);
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(result[i])) return false;
    for (int i = 0; i < 16; ++i) world[i] = result[i];
    return true;
}

// projection := S * projection, S = mirror of clip-space X: everything drawn with it lands on the other side of
// the screen (the whole first-person pass: arm and item). Negates row 0 (m[col * 4 + 0]).
inline void MirrorProjection(float projection[16])
{
    for (int c = 0; c < 4; ++c) projection[c * 4] = -projection[c * 4];
}

}  // namespace MyClientLeftHand

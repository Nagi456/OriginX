#pragma once
// Per-pixel alpha for the overlay (used only while a translucent HUD background is switched on).
// Everything is drawn as usual on the colour key (magenta = see-through); this turns the finished
// picture into premultiplied BGRA for UpdateLayeredWindow:
//   key colour            -> fully transparent
//   inside a translucent rect, exactly that rect's fill colour -> `alpha` (the box behind a counter;
//                            its icon and text on top keep their own colours and stay solid)
//   anything else         -> solid
// Pure pixel work, so it is unit-tested on the host.
#include <cstdint>
#include <vector>

namespace OverlayAlpha {

struct Translucent { int left, top, right, bottom; uint32_t color; uint8_t alpha; };   // color = 0x00RRGGBB

constexpr uint32_t Key = 0x00FF00FFu;   // RGB(255,0,255) as stored in a 32-bit DIB

inline uint32_t Premultiply(uint32_t rgb, uint32_t a)
{
    const uint32_t r = ((rgb >> 16) & 0xFF) * a / 255, g = ((rgb >> 8) & 0xFF) * a / 255, b = (rgb & 0xFF) * a / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline void Convert(uint32_t* px, int width, int height, const std::vector<Translucent>& translucent)
{
    const size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; i++) {
        const uint32_t c = px[i] & 0x00FFFFFFu;
        px[i] = c == Key ? 0u : (0xFF000000u | c);
    }
    for (const Translucent& t : translucent) {
        const int left = t.left < 0 ? 0 : t.left, top = t.top < 0 ? 0 : t.top;
        const int right = t.right > width ? width : t.right, bottom = t.bottom > height ? height : t.bottom;
        const uint32_t solid = 0xFF000000u | (t.color & 0x00FFFFFFu);
        const uint32_t see = Premultiply(t.color & 0x00FFFFFFu, t.alpha);
        for (int y = top; y < bottom; y++)
            for (int x = left; x < right; x++) {
                uint32_t& p = px[static_cast<size_t>(y) * width + x];
                if (p == solid) p = see;
            }
    }
}

// Text drawn on a translucent box: its anti-aliased (greyscale) edge pixels are blends of the ink and the
// box colour, and left solid they would show the box colour as an opaque fringe around the letters. Run
// after Convert: inside each ink rect, a solid pixel on the line from the box colour to the ink becomes the
// ink at that coverage over the translucent box. Pixels off that line (icons, other drawing) stay solid.
struct Ink { int left, top, right, bottom; uint32_t color; };   // color = 0x00RRGGBB

inline void SoftenInk(uint32_t* px, int width, int height, const std::vector<Translucent>& translucent, const std::vector<Ink>& inks)
{
    for (const Ink& ink : inks) {
        const int left = ink.left < 0 ? 0 : ink.left, top = ink.top < 0 ? 0 : ink.top;
        const int right = ink.right > width ? width : ink.right, bottom = ink.bottom > height ? height : ink.bottom;
        const int ir = (ink.color >> 16) & 0xFF, ig = (ink.color >> 8) & 0xFF, ib = ink.color & 0xFF;
        for (int y = top; y < bottom; y++)
            for (int x = left; x < right; x++) {
                uint32_t& p = px[static_cast<size_t>(y) * width + x];
                if ((p >> 24) != 255) continue;
                const Translucent* box = nullptr;   // the top-most translucent box under the pixel
                for (size_t i = translucent.size(); i-- > 0;) {
                    const Translucent& t = translucent[i];
                    if (x >= t.left && x < t.right && y >= t.top && y < t.bottom) { box = &t; break; }
                }
                if (!box) continue;
                const int gr = (box->color >> 16) & 0xFF, gg = (box->color >> 8) & 0xFF, gb = box->color & 0xFF;
                const int dr = ir - gr, dg = ig - gg, db = ib - gb;
                const int length = dr * dr + dg * dg + db * db;
                if (length < 300) continue;   // ink too close to the box colour to tell apart
                const int pr = (p >> 16) & 0xFF, pg = (p >> 8) & 0xFF, pb = p & 0xFF;
                const float k = static_cast<float>((pr - gr) * dr + (pg - gg) * dg + (pb - gb) * db) / length;
                if (k >= 0.97f || k < -0.05f) continue;
                const float kk = k < 0 ? 0 : k;
                const float er = gr + kk * dr - pr, eg = gg + kk * dg - pg, eb = gb + kk * db - pb;
                if (er * er + eg * eg + eb * eb > 24 * 24) continue;   // not a blend of these two
                const float boxA = box->alpha / 255.0f, a = kk + (1 - kk) * boxA;
                const auto channel = [&](int inkC, int boxC) {
                    const float v = kk * inkC + (1 - kk) * boxA * boxC;
                    return static_cast<uint32_t>(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f));
                };
                const uint32_t alpha = static_cast<uint32_t>(a * 255 + 0.5f);
                p = (alpha << 24) | (channel(ir, gr) << 16) | (channel(ig, gg) << 8) | channel(ib, gb);
            }
    }
}

// Glass shapes: rounded boxes drawn hard-edged (a pixel is the box colour when at least half of it is
// inside), so Convert above can find them by colour. Their corners are smoothed afterwards here: a corner
// pixel that still holds exactly what the hard drawing left becomes the box blended with what lies under
// it (another glass shape or nothing). Pixels drawn over since (text, icons) are left alone.
struct Glass { int left, top, right, bottom, radius; uint32_t color; uint8_t alpha; };   // color = 0x00RRGGBB

// How much of pixel (x, y) the box covers, 0..255 (8x8 samples in the corners).
inline uint32_t GlassCoverage(const Glass& g, int x, int y)
{
    if (x < g.left || y < g.top || x >= g.right || y >= g.bottom) return 0;
    const int r = g.radius;
    const int cx = x < g.left + r ? g.left + r : (x >= g.right - r ? g.right - r : x);
    const int cy = y < g.top + r ? g.top + r : (y >= g.bottom - r ? g.bottom - r : y);
    if (r <= 0 || (cx == x && cy == y)) return 255;
    int inside = 0;
    for (int sy = 0; sy < 8; sy++)
        for (int sx = 0; sx < 8; sx++) {
            const float dx = x + (sx + 0.5f) / 8 - cx, dy = y + (sy + 0.5f) / 8 - cy;
            if (dx * dx + dy * dy <= static_cast<float>(r) * r) inside++;
        }
    return static_cast<uint32_t>(inside * 255 / 64);
}

// The finished (premultiplied) pixel under shapes [0, upto): hard = what Convert makes of the hard drawing.
inline uint32_t GlassStack(const std::vector<Glass>& shapes, size_t upto, int x, int y, bool hard)
{
    for (size_t i = upto; i-- > 0;) {
        const uint32_t a = GlassCoverage(shapes[i], x, y);
        if (a == 0 || (hard && a < 128)) continue;
        const uint32_t top = Premultiply(shapes[i].color & 0x00FFFFFFu, shapes[i].alpha);
        if (hard || a == 255) return top;
        const uint32_t under = GlassStack(shapes, i, x, y, false);
        uint32_t out = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const uint32_t t = (top >> shift) & 0xFF, u = (under >> shift) & 0xFF;
            out |= ((t * a + u * (255 - a)) / 255) << shift;
        }
        return out;
    }
    return 0;
}

inline void SmoothGlass(uint32_t* px, int width, int height, const std::vector<Glass>& shapes)
{
    for (const Glass& g : shapes) {
        const int r = g.radius;
        if (r <= 0) continue;
        for (int corner = 0; corner < 4; corner++) {
            const int left = (corner & 1) ? g.right - r : g.left, top = (corner & 2) ? g.bottom - r : g.top;
            for (int y = top < 0 ? 0 : top; y < top + r && y < height; y++)
                for (int x = left < 0 ? 0 : left; x < left + r && x < width; x++) {
                    const uint32_t a = GlassCoverage(g, x, y);
                    if (a == 0 || a == 255) continue;
                    uint32_t& p = px[static_cast<size_t>(y) * width + x];
                    if (p == GlassStack(shapes, shapes.size(), x, y, true))
                        p = GlassStack(shapes, shapes.size(), x, y, false);
                }
        }
    }
}

// Health Warning: a coloured glow along the screen edges, put under the finished (premultiplied) picture so
// the HUD stays on top. `alpha` is the glow at the very edge; it fades out over `band` pixels inwards
// (the corners, near two edges, are the strongest). Run after Convert / SmoothGlass.
inline uint32_t VignetteFade(int distance, int band)
{
    if (distance >= band) return 0;
    const uint32_t t = static_cast<uint32_t>((band - distance) * 255 / band);   // 255 at the edge
    return t * t / 255;   // eased: most of it hugs the edge
}

inline void Vignette(uint32_t* px, int width, int height, uint32_t rgb, uint8_t alpha, int band)
{
    if (!px || width <= 0 || height <= 0 || alpha == 0 || band <= 0) return;
    std::vector<uint32_t> fx(width), fy(height);
    for (int x = 0; x < width; x++) fx[x] = VignetteFade(x < width - 1 - x ? x : width - 1 - x, band);
    for (int y = 0; y < height; y++) fy[y] = VignetteFade(y < height - 1 - y ? y : height - 1 - y, band);
    for (int y = 0; y < height; y++) {
        const bool edgeRow = fy[y] != 0;
        for (int x = 0; x < width; x++) {
            if (!edgeRow && fx[x] == 0) {
                if (x < band || x >= width - band) continue;
                x = width - band - 1;   // the middle of a row has no glow (never moves backwards: x < width - band)
                continue;
            }
            // 1 - (1 - fx)(1 - fy): either edge lights it, both more
            const uint32_t f = 255 - (255 - fx[x]) * (255 - fy[y]) / 255;
            const uint32_t a = f * alpha / 255;
            if (a == 0) continue;
            uint32_t& p = px[static_cast<size_t>(y) * width + x];
            const uint32_t over = p >> 24, glow = Premultiply(rgb & 0x00FFFFFFu, a);
            uint32_t out = 0;
            for (int shift = 0; shift < 32; shift += 8)
                out |= (((p >> shift) & 0xFF) + ((glow >> shift) & 0xFF) * (255 - over) / 255) << shift;
            p = out;
        }
    }
}

}  // namespace OverlayAlpha

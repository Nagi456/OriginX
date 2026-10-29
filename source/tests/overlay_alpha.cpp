#include "../OverlayAlpha.h"
#include <cassert>
#include <cstdio>
using namespace OverlayAlpha;

int main()
{
    // 4x2: key, box colour, text white, box colour | key, key, box colour outside the rect, red
    const uint32_t box = 0x000E0E12;
    std::vector<uint32_t> px = {Key, box, 0x00FFFFFF, box,
                                Key, 0xAAFF00FF, box, 0x00FF0000};   // high byte garbage from GDI is ignored
    Convert(px.data(), 4, 2, {{0, 0, 4, 1, box, 153}});
    assert(px[0] == 0 && px[4] == 0 && px[5] == 0);                         // see-through
    assert(px[1] == Premultiply(box, 153) && px[3] == px[1]);               // translucent box
    assert((px[1] >> 24) == 153 && (px[1] & 0xFF) == (0x12 * 153 / 255));
    assert(px[2] == 0xFFFFFFFFu);                                           // text on the box stays solid
    assert(px[6] == (0xFF000000u | box));                                   // same colour outside a rect: solid
    assert(px[7] == 0xFFFF0000u);
    // a rect partly off-screen is clipped
    std::vector<uint32_t> one = {box};
    Convert(one.data(), 1, 1, {{-5, -5, 10, 10, box, 100}});
    assert((one[0] >> 24) == 100);
    // Glass: a 20x10 box with radius 4 over nothing; a corner pixel half covered gets a blend, the middle is
    // the box, text drawn on a corner is kept.
    {
        const uint32_t glass = 0x00E0E4EC;
        const std::vector<Glass> shapes = {{0, 0, 20, 10, 4, glass, 120}};
        assert(GlassCoverage(shapes[0], 10, 5) == 255 && GlassCoverage(shapes[0], 0, 0) == 0);
        const uint32_t edge = GlassCoverage(shapes[0], 1, 1);
        assert(edge > 0 && edge < 255);
        std::vector<uint32_t> g(20 * 10, Key);
        for (int y = 0; y < 10; y++)
            for (int x = 0; x < 20; x++)
                if (GlassCoverage(shapes[0], x, y) >= 128) g[y * 20 + x] = glass;
        g[1 * 20 + 18] = 0x00FFFFFF;   // "text" on the top-right corner
        Convert(g.data(), 20, 10, {{0, 0, 20, 10, glass, 120}});
        SmoothGlass(g.data(), 20, 10, shapes);
        assert(g[5 * 20 + 10] == Premultiply(glass, 120));
        assert((g[1 * 20 + 1] >> 24) == 120 * edge / 255);      // smooth corner over nothing
        assert(g[1 * 20 + 18] == 0xFFFFFFFFu);                   // text kept
        // A row on top of the panel: its corner blends into the panel's glass, not into nothing.
        const uint32_t row = 0x00F4F5F8;
        const std::vector<Glass> two = {{0, 0, 20, 10, 0, glass, 100}, {2, 2, 18, 8, 3, row, 200}};
        std::vector<uint32_t> h(20 * 10, glass);
        for (int y = 0; y < 10; y++)
            for (int x = 0; x < 20; x++)
                if (GlassCoverage(two[1], x, y) >= 128) h[y * 20 + x] = row;
        Convert(h.data(), 20, 10, {{0, 0, 20, 10, glass, 100}, {2, 2, 18, 8, row, 200}});
        SmoothGlass(h.data(), 20, 10, two);
        int partial = 0;
        for (int y = 2; y < 5; y++)
            for (int x = 2; x < 5; x++) {
                const uint32_t c = GlassCoverage(two[1], x, y), a = h[y * 20 + x] >> 24;
                if (c > 0 && c < 255) { partial++; assert(a > 100 && a < 200); }
            }
        assert(partial > 0);
    }
    {
        // Dark text on a light translucent box: core solid, an edge pixel half ink / half box gets
        // half coverage over the box, a pixel of another colour (an icon) stays solid.
        const uint32_t box = 0xDEE3ECu, inkC = 0x222634u;
        auto mix = [&](float k) {
            uint32_t out = 0;
            for (int sh = 0; sh < 24; sh += 8) {
                const float v = ((inkC >> sh) & 0xFF) * k + ((box >> sh) & 0xFF) * (1 - k);
                out |= static_cast<uint32_t>(v + 0.5f) << sh;
            }
            return out;
        };
        std::vector<uint32_t> t = {box, inkC, mix(0.5f), 0x10C020u, box};
        Convert(t.data(), 5, 1, {{0, 0, 5, 1, box, 96}});
        const std::vector<Translucent> boxes = {{0, 0, 5, 1, box, 96}};
        SoftenInk(t.data(), 5, 1, boxes, {{0, 0, 5, 1, inkC}});
        assert(t[0] == Premultiply(box, 96) && t[4] == t[0]);          // the box itself
        assert(t[1] == (0xFF000000u | inkC));                          // full ink: solid
        const uint32_t a = t[2] >> 24;
        assert(a > 150 && a < 185);                                    // 0.5 + 0.5 * 96/255 = 0.69
        assert(t[3] == (0xFF000000u | 0x10C020u));                     // not on the ink line: untouched
        std::vector<uint32_t> u = {mix(0.5f)};                         // outside any box: untouched
        Convert(u.data(), 1, 1, {});
        SoftenInk(u.data(), 1, 1, {}, {{0, 0, 1, 1, inkC}});
        assert(u[0] == (0xFF000000u | mix(0.5f)));
    }
    // Vignette: strongest at the edge, gone in the middle, under solid HUD pixels.
    {
        const int w = 40, h = 30;
        std::vector<uint32_t> v(w * h, 0u);
        v[15 * w + 0] = 0xFFFFFFFFu;   // solid HUD pixel on the left edge
        Vignette(v.data(), w, h, 0x00FF0000u, 200, 8);
        assert((v[15 * w + 1] >> 24) > (v[15 * w + 5] >> 24));       // fades inwards
        assert(v[15 * w + 20] == 0);                                   // the middle stays clear
        assert((v[0] >> 24) >= (v[15 * w + 1] >> 24));                 // a corner at least as strong
        assert(v[15 * w + 0] == 0xFFFFFFFFu);                          // solid stays solid
        assert(((v[15 * w + 1] >> 8) & 0xFF) == 0);                    // red only
        std::vector<uint32_t> tiny(3 * 3, 0u);
        Vignette(tiny.data(), 3, 3, 0x00FF0000u, 255, 50);             // band wider than the picture
        assert((tiny[4] >> 24) > 0);
    }
    std::puts("overlay alpha ok");
}

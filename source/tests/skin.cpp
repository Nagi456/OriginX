#include "../SkinChannel.h"
#include <cassert>
#include <cstdio>
using namespace MyClientSkin;

int main()
{
    // 64x64 RGBA skin: every pixel encodes its own position; the hat area (40..47, 8..15) is see-through
    // except one red pixel at (41, 9), half transparent.
    std::vector<uint8_t> skin(64 * 64 * 4);
    for (uint32_t y = 0; y < 64; y++)
        for (uint32_t x = 0; x < 64; x++) {
            uint8_t* p = &skin[(y * 64 + x) * 4];
            p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[2] = 7; p[3] = 255;
            if (x >= 40 && x < 48 && y >= 8 && y < 16) p[3] = 0;
        }
    uint8_t* hat = &skin[(9 * 64 + 41) * 4];
    hat[0] = 255; hat[1] = 0; hat[2] = 0; hat[3] = 128;

    std::vector<uint8_t> rgba;
    assert(ToRgba(4, skin.data(), skin.size(), 64, 64, rgba) && rgba == skin);
    assert(!ToRgba(4, skin.data(), skin.size() - 1, 64, 64, rgba) && rgba.empty());   // too few bytes
    assert(!ToRgba(9, skin.data(), skin.size(), 64, 64, rgba));                        // unknown format
    assert(!ToRgba(4, skin.data(), skin.size(), 64, 48, rgba));                        // odd shape
    assert(!ToRgba(4, skin.data(), skin.size(), 512, 512, rgba));                      // too big
    assert(ToRgba(4, skin.data(), skin.size(), 64, 32, rgba) && rgba.size() == 64 * 32 * 4);   // legacy

    std::vector<uint8_t> rgb(64 * 64 * 3);
    for (size_t i = 0; i < 64 * 64; i++) { rgb[i * 3] = 1; rgb[i * 3 + 1] = 2; rgb[i * 3 + 2] = 3; }
    assert(ToRgba(3, rgb.data(), rgb.size(), 64, 64, rgba) && rgba[0] == 1 && rgba[2] == 3 && rgba[3] == 255);

    std::vector<uint8_t> head;
    assert(Head(skin.data(), 64, 64, head) && head.size() == 8 * 8 * 4);
    assert(head[0] == 8 && head[1] == 8 && head[3] == 255);                          // face (8, 8)
    const uint8_t* h7 = &head[(7 * 8 + 7) * 4];
    assert(h7[0] == 15 && h7[1] == 15);                                              // face (15, 15)
    const uint8_t* mixed = &head[(1 * 8 + 1) * 4];                                   // face (9, 9) + hat
    assert(mixed[0] == (255 * 128 + 9 * 127) / 255 && mixed[1] == (9 * 127) / 255 && mixed[3] == 255);

    // HD skin (128 wide): the face is 16x16 at (16, 16).
    std::vector<uint8_t> hd(128 * 128 * 4, 0);
    hd[(16 * 128 + 16) * 4] = 99;
    assert(Head(hd.data(), 128, 128, head) && head.size() == 16 * 16 * 4 && head[0] == 99);
    assert(!Head(hd.data(), 100, 100, head));

    assert(Hash(skin, 64, 64) != Hash(hd, 128, 128));

    // Whose head can be read from the picture.
    assert(ClassicLayout(false, "geometry.humanoid.custom", ""));
    assert(ClassicLayout(false, "geometry.humanoid.customSlim", ""));
    assert(ClassicLayout(false, "", "{\"geometry\":{\"default\":\"geometry.humanoid.custom\"}}"));
    assert(ClassicLayout(false, "", ""));
    assert(!ClassicLayout(true, "geometry.humanoid.custom", ""));                 // persona
    assert(!ClassicLayout(false, "geometry.persona_1a2b", ""));
    assert(!ClassicLayout(false, "geometry.n3_frog.custom", ""));                 // a pack's own model
    assert(!ClassicLayout(false, "", "{\"geometry\":{\"default\":\"geometry.n3_frog\"}}"));
    std::puts("skin ok");
}

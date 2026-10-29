#include "../SkinModel.h"
#include <cassert>
#include <cstdio>
using namespace SkinModel;

int main()
{
    // Every pixel holds its own position: red = x, green = y.
    std::vector<uint8_t> skin(64 * 64 * 4);
    for (uint32_t y = 0; y < 64; y++)
        for (uint32_t x = 0; x < 64; x++) {
            uint8_t* p = &skin[(y * 64 + x) * 4];
            p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[2] = 0; p[3] = 255;
        }
    const auto at = [&](const Box& b, int axis, int sign, float x, float y, float z, int ex, int ey) {
        uint32_t rgb = 0;
        assert(Texel(skin, 64, 64, b, axis, sign, x, y, z, rgb));
        if ((int)(rgb >> 16) != ex || (int)((rgb >> 8) & 0xFF) != ey)
            std::printf("got (%u, %u), wanted (%d, %d)\n", rgb >> 16, (rgb >> 8) & 0xFF, ex, ey);
        assert((int)(rgb >> 16) == ex && (int)((rgb >> 8) & 0xFF) == ey);
    };

    const std::vector<Box> wide = Boxes(false, true);
    assert(wide.size() == 12 && !wide[0].outer && wide[6].outer);
    const Box& head = wide[0];
    at(head, 2, 1, -3.9f, 31.9f, 4, 8, 8);     // front, top-left as seen: the face's (8, 8)
    at(head, 2, 1, 3.9f, 24.1f, 4, 15, 15);    // front, bottom-right
    at(head, 2, -1, -3.9f, 31.9f, -4, 31, 8);  // back: the viewer's left from the front is the back's right
    at(head, 0, -1, -4, 31.9f, -3.9f, 0, 8);   // player's right side, back edge
    at(head, 0, 1, 4, 31.9f, 3.9f, 16, 8);     // player's left side, front edge
    at(head, 1, 1, -3.9f, 32, -3.9f, 8, 0);    // top, back-left
    at(head, 1, -1, -3.9f, 24, 3.9f, 16, 7);   // bottom
    at(wide[6], 2, 1, -3.9f, 31.9f, 4.5f, 40, 8);   // hat front
    at(wide[1], 2, 1, -3.9f, 23.9f, 2, 20, 20);     // body front
    at(wide[2], 2, 1, -7.9f, 23.9f, 2, 44, 20);     // right arm front
    at(wide[3], 2, 1, 4.1f, 23.9f, 2, 36, 52);      // left arm front (64x64 net)
    at(wide[5], 2, 1, 0.1f, 11.9f, 2, 20, 52);      // left leg front

    // Slim arms are 3 wide, the net 3 wide too: the arm's back starts at 40 + 2*4 + 3.
    const std::vector<Box> slim = Boxes(true, true);
    assert(slim[2].x0 == -7 && slim[2].x1 == -4 && slim[2].w == 3);
    at(slim[2], 2, -1, -4.1f, 23.9f, -2, 51, 20);

    // A 64x32 skin: only the hat as an outer layer, the left limbs mirror the right ones.
    const std::vector<Box> legacy = Boxes(false, false);
    assert(legacy.size() == 7 && legacy[6].outer);
    std::vector<uint8_t> old(skin.begin(), skin.begin() + 64 * 32 * 4);
    uint32_t rgb = 0;
    assert(Texel(old, 64, 32, legacy[3], 2, 1, 7.9f, 23.9f, 2, rgb) && (rgb >> 16) == 44 && ((rgb >> 8) & 0xFF) == 20);   // mirrored
    assert(Texel(old, 64, 32, legacy[3], 0, 1, 8, 23.9f, 1.9f, rgb) && (rgb >> 16) == 43);   // its outer side, front edge = the right arm's outer side, front edge

    // HD skin: the finer pixel under the spot.
    std::vector<uint8_t> hd(128 * 128 * 4, 0);
    for (size_t i = 0; i < 128 * 128; i++) hd[i * 4 + 3] = 255;
    uint8_t* fine = &hd[((size_t)17 * 128 + 17) * 4];   // inside the face's first pixel (16..17)
    fine[0] = 200;
    assert(Texel(hd, 128, 128, head, 2, 1, -3.3f, 31.3f, 4, rgb) && (rgb >> 16) == 200);

    // See-through spots and bad sizes.
    skin[(8 * 64 + 40) * 4 + 3] = 0;
    assert(!Texel(skin, 64, 64, wide[6], 2, 1, -4.4f, 32.4f, 4.5f, rgb));
    assert(!Texel(skin, 48, 64, head, 2, 1, 0, 28, 4, rgb));
    std::puts("skin_model: ok");
}

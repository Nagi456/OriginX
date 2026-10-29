#include "../SkinLibrary.h"
#include <cassert>
#include <cstdio>
using namespace SkinLibrary;

int main()
{
    // skins.json as the game ships it (with a comment and a persona-style skin that needs its own model).
    const std::string json = R"({
  // the default pack
  "skins": [
    { "localization_name": "Steve", "geometry": "geometry.humanoid.custom", "texture": "steve.png", "type": "free" },
    { "localization_name": "Alex", "geometry": "geometry.humanoid.customSlim", "texture": "textures/alex.png", "type": "free" },
    { "localization_name": "Robot", "geometry": "geometry.robot", "texture": "robot.png", "type": "free" },
    { "localization_name": "NoTexture", "geometry": "geometry.humanoid.custom", "type": "free" }
  ],
  "serialize_name": "Standard",
  "localization_name": "Standard"
})";
    std::string serialize, localization;
    const auto skins = ParseSkinsJson(json, &serialize, &localization);
    assert(skins.size() == 2);
    assert(skins[0].name == "Steve" && skins[0].texture == "steve.png" && !skins[0].slim);
    assert(skins[1].name == "Alex" && skins[1].texture == "textures/alex.png" && skins[1].slim);
    assert(serialize == "Standard" && localization == "Standard");
    assert(ParseSkinsJson("").empty() && ParseSkinsJson("{\"skins\": [").empty());

    const auto lang = ParseLang("\xEF\xBB\xBFskinpack.Standard=Default\r\n## comment\nskin.Standard.Steve=Steve the Miner\t#\nbroken line\n");
    assert(lang.at("skinpack.Standard") == "Default");
    assert(SkinName(lang, "Standard", "Steve") == "Steve the Miner");
    assert(SkinName(lang, "Standard", "Alex") == "Alex");

    // Front doll of a 64x64 skin: each pixel's red = x, green = y.
    std::vector<uint8_t> skin(64 * 64 * 4, 0);
    for (uint32_t y = 0; y < 64; y++)
        for (uint32_t x = 0; x < 64; x++) {
            uint8_t* p = &skin[(y * 64 + x) * 4];
            p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[3] = 255;
            if (x >= 54 && x < 56 && y >= 20 && y < 32) p[3] = 0;       // slim right arm
            if ((x >= 32 && y < 16) || (y >= 32 && y < 48) || (y >= 48 && (x < 16 || x >= 48))) p[3] = 0;   // empty outer layers
        }
    assert(MyClientSkin::LooksSlim(skin.data(), 64, 64));
    assert(!MyClientSkin::LooksSlim(skin.data(), 64, 32));
    auto doll = FrontDoll(skin.data(), 64, 64);
    assert(doll.size() == 16 * 32);
    assert(doll[0 * 16 + 4] == (0xFF000000u | (8u << 16) | (8u << 8)));        // face (8, 8) at the head's corner
    assert(doll[8 * 16 + 4] == (0xFF000000u | (20u << 16) | (20u << 8)));      // body (20, 20)
    assert(doll[8 * 16 + 12] == (0xFF000000u | (36u << 16) | (52u << 8)));     // own left arm (36, 52)
    assert(doll[0] == 0);                                                     // beside the head: empty
    skin[(9 * 64 + 41) * 4 + 3] = 255;                                        // one hat pixel over face (9, 9)
    doll = FrontDoll(skin.data(), 64, 64);
    assert(doll[1 * 16 + 5] == (0xFF000000u | (41u << 16) | (9u << 8)));
    assert(FrontDoll(skin.data(), 50, 50)[0] == 0);

    // Legacy 64x32: the left arm/leg mirror the right ones.
    doll = FrontDoll(skin.data(), 64, 32);
    assert(doll[8 * 16 + 12] == (0xFF000000u | (47u << 16) | (20u << 8)));     // mirrored (44 + 3, 20)

    MyClientSkin::Request r{};
    r.width = 64; r.height = 64; assert(MyClientSkin::UsableRequest(r));
    r.height = 48; assert(!MyClientSkin::UsableRequest(r));
    // The same skin found twice is listed once (the first stays); other arms or pixels are kept.
    {
        struct S { std::vector<uint8_t> rgba; uint32_t w, h; bool slim; int id; };
        std::vector<uint8_t> a(64 * 64 * 4, 7), b = a;
        b[100] = 8;
        std::vector<S> list{{a, 64, 64, false, 1}, {b, 64, 64, false, 2}, {a, 64, 64, false, 3}, {a, 64, 64, true, 4}, {b, 64, 64, false, 5}};
        RemoveSameSkins(list);
        assert(list.size() == 3 && list[0].id == 1 && list[1].id == 2 && list[2].id == 4);
    }
    std::puts("skin library ok");
}

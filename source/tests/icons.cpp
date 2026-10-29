#include "../ItemIcons.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
// Armor HUD icons: item id -> texture name, and the TGA reader used for the leather textures.
int main(int argc, char** argv)
{
    using namespace ItemIcons;
    assert(TextureName("minecraft:golden_helmet") == "gold_helmet" && TextureName("diamond_chestplate") == "diamond_chestplate");
    assert(TextureName("leather_boots") == "leather_boots" && TextureName("turtle_helmet") == "turtle_helmet");
    // 2x1 uncompressed 32-bit, bottom-up: first stored pixel is the bottom row.
    std::string tga(18, '\0');
    tga[2] = 2; tga[12] = 2; tga[14] = 1; tga[16] = 32;
    tga += std::string("\x10\x20\x30\xFF\x00\x00\x00\x00", 8);
    Image image;
    assert(DecodeTga(tga, image) && image.w == 2 && image.h == 1);
    assert(image.bgra[0] == 0xFF302010u && image.bgra[1] == 0u);
    // RLE: 3 copies of one half-transparent pixel (premultiplied: 0x80 alpha halves the colour).
    std::string rle(18, '\0');
    rle[2] = 10; rle[12] = 3; rle[14] = 1; rle[16] = 32; rle[17] = 0x20;
    rle += std::string("\x82\xFE\xFE\xFE\x80", 5);
    assert(DecodeTga(rle, image) && image.w == 3 && (image.bgra[2] >> 24) == 0x80 && (image.bgra[2] & 0xFF) == 0x7F);
    assert(!DecodeTga(std::string(10, '\0'), image));
    if (argc > 1) {   // optional: a real game TGA
        std::ifstream in(argv[1], std::ios::binary);
        const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(DecodeTga(data, image) && image.w == 16 && image.h == 16);
    }
    std::cout << "icons ok\n";
    return 0;
}

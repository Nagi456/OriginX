#include <algorithm>
#include "../SlotIcons.h"
#include <cassert>
#include <iostream>
// Shulker Preview icons: the pack JSON reader, item id -> textures, and the inventory cube.
int main()
{
    using namespace SlotIcons;
    const Json j = ParseJson(R"({ // comment
        "a": "x", "b": [ {"path": "p1"}, "p2" ], /* block */ "c": {"path": "q"}, "n": 1.5, "t": true, "e": {}, "z": [1,2,], })");
    assert(j.type == Json::Object && j.Get("a")->text == "x");
    assert(FirstPath(j.Get("b")) == "p1" && FirstPath(j.Get("c")) == "q" && FirstPath(j.Get("a")) == "x");
    assert(j.Get("n")->type == Json::Other && j.Get("e")->type == Json::Object && j.Get("z")->items.size() == 2);
    assert(ParseJson("{\"a\": ").type == Json::Null && ParseJson("").type == Json::Null);

    PackData pack;
    pack.itemTextures = ParseJson(R"({"apple": {"textures": "textures/items/apple"},
        "beef_cooked": {"textures": "textures/items/beef_cooked"}, "bucket_water": {"textures": ["textures/items/bucket_water"]},
        "cake": {"textures": "textures/items/cake"}})");
    pack.terrainTextures = ParseJson(R"({"deepslate": {"textures": "textures/blocks/deepslate/deepslate"},
        "furnace_top": {"textures": "textures/blocks/furnace_top"}, "furnace_side": {"textures": "textures/blocks/furnace_side"},
        "furnace_front": {"textures": "textures/blocks/furnace_front_off"}, "stone_slab": {"textures": ["textures/blocks/stone_slab_top"]},
        "torch_on": {"textures": "textures/blocks/torch_on"}, "leaves_oak": {"textures": "textures/blocks/leaves_oak"},
        "grass_carried_top": {"textures": "textures/blocks/grass_carried"}, "grass_top": {"textures": "textures/blocks/grass_top"}})");
    pack.blocks = ParseJson(R"({"deepslate": {"textures": "deepslate"},
        "furnace": {"textures": {"up": "furnace_top", "down": "furnace_top", "side": "furnace_side", "south": "furnace_front"}},
        "smooth_stone_slab": {"textures": "stone_slab"}, "torch": {"textures": "torch_on"}, "oak_leaves": {"textures": "leaves_oak"},
        "grass_block": {"textures": {"up": "grass_top", "side": "grass_top"}, "carried_textures": {"up": "grass_carried_top", "side": "grass_carried_top"}},
        "cake": {"textures": "deepslate"}, "seaLantern": {"textures": "deepslate"}})");
    Model m = Resolve(pack, "apple");
    assert(m.kind == Model::Flat && m.top == "textures/items/apple");
    assert(Resolve(pack, "cooked_beef").top == "textures/items/beef_cooked");
    assert(Resolve(pack, "water_bucket").top == "textures/items/bucket_water");
    assert(Resolve(pack, "cake").kind == Model::Flat);   // item icon wins over the block
    m = Resolve(pack, "deepslate");
    assert(m.kind == Model::Cube && m.top == "textures/blocks/deepslate/deepslate" && m.left == m.top && m.height == 1.0f);
    m = Resolve(pack, "furnace");
    assert(m.kind == Model::Cube && m.top == "textures/blocks/furnace_top" && m.left == "textures/blocks/furnace_front_off" && m.right == "textures/blocks/furnace_side");
    assert(Resolve(pack, "smooth_stone_slab").height == 0.5f);
    m = Resolve(pack, "torch");
    assert(m.kind == Model::Flat && m.top == "textures/blocks/torch_on");
    assert(Resolve(pack, "oak_leaves").tint == 0x48B518);
    assert(Resolve(pack, "grass_block").top == "textures/blocks/grass_carried");
    assert(Resolve(pack, "sea_lantern").kind == Model::Cube);   // blocks.json still says "seaLantern"
    // The grass block's side: a mask texture with an overlay colour; the colour goes where the alpha is, the rest stays.
    {
        PackData grass;
        grass.terrainTextures = ParseJson(R"({"grass_carried": {"textures": {"path": "textures/blocks/grass_side", "overlay_color": "#79c05a"}},
            "grass_carried_top": {"textures": "textures/blocks/grass_carried"}})");
        grass.blocks = ParseJson(R"({"grass": {"carried_textures": {"side": "grass_carried", "up": "grass_carried_top"}}})");
        const Model g = Resolve(grass, "grass_block");
        assert(g.left == "textures/blocks/grass_side" && g.overlay.at(g.left) == 0x0179C05Au && !g.overlay.count(g.top));
        Image side;
        side.w = 2; side.h = 1; side.bgra = {0xFFB2B2B2u, 0x00795539u};   // grey grass (mask on), dirt (mask off)
        Overlay(side, g.overlay.at(g.left));
        assert(side.bgra[0] == 0xFF54863Eu && side.bgra[1] == 0xFF795539u);
    }
    m = Resolve(pack, "netherite_ingot");   // unknown to the lists: texture named like the id
    assert(m.kind == Model::Flat && m.top == "textures/items/netherite_ingot" && m.left == "textures/blocks/netherite_ingot");

    // Cube: opaque red top, green left, blue right -> each face shows up where it should, corners stay clear.
    Image red, green, blue;
    red.w = red.h = green.w = green.h = blue.w = blue.h = 2;
    red.bgra.assign(4, 0xFFFF0000u); green.bgra.assign(4, 0xFF00FF00u); blue.bgra.assign(4, 0xFF0000FFu);
    const Image cube = RenderCube(red, green, blue, 32, 1.0f);
    assert(cube.w == 32 && cube.h == 32);
    const auto at = [&](int x, int y) { return cube.bgra[static_cast<size_t>(y) * 32 + x]; };
    assert(at(0, 0) == 0 && at(31, 0) == 0 && at(0, 31) == 0 && at(31, 31) == 0);
    assert(at(16, 8) == 0xFFFF0000u);                         // top face, full brightness
    assert(at(9, 20) == 0xFF00CC00u);                         // left face, 0.8
    assert(at(23, 20) == 0xFF000099u);                        // right face, 0.6
    const Image slab = RenderCube(red, green, blue, 32, 0.5f);
    assert(slab.bgra[static_cast<size_t>(4) * 32 + 16] == 0 && (slab.bgra[static_cast<size_t>(16) * 32 + 16] >> 24) == 0xFF);

    Image strip; strip.w = 2; strip.h = 4; strip.bgra = {1, 2, 3, 4, 5, 6, 7, 8};
    const Image frame = FirstFrame(strip);
    assert(frame.w == 2 && frame.h == 2 && frame.bgra.size() == 4 && frame.bgra[3] == 4);
    // Icon file names for ids whose key in item_texture.json differs (arrays and old names).
    {
        auto has = [](const std::string& id, const std::string& name) {
            const auto n = ItemFileNames(id);
            return std::find(n.begin(), n.end(), name) != n.end();
        };
        assert(has("golden_sword", "gold_sword") && has("wooden_pickaxe", "wood_pickaxe"));
        assert(has("dark_oak_boat", "boat_darkoak") && has("acacia_chest_boat", "acacia_chest_boat"));
        assert(has("oak_sign", "sign") && has("spruce_sign", "sign_spruce") && has("music_disc_cat", "record_cat"));
        assert(has("beef", "beef_raw") && has("bow", "bow_standby") && has("water_bucket", "bucket_water"));
        assert(has("red_bundle", "bundle_red") && has("red_harness", "harness/harness_red") && has("wheat_seeds", "seeds_wheat"));
        assert(has("pig_spawn_egg", "spawn_egg/spawn_egg_pig") && has("light_gray_dye", "dye_powder_silver"));
    }
    std::cout << "slot icons ok\n";
    return 0;
}

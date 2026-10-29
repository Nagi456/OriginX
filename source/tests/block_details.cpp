#include "../BlockDetails.h"
#include <cassert>
#include <cstdio>
#include <fstream>
using namespace BlockDetails;
// Block Details: tool from tags, the held item's verdict, names and where a block comes from.
int main()
{
    assert(Namespace("minecraft:diorite") == "minecraft" && Namespace("air_ship:engine") == "air_ship" && Namespace("dirt") == "minecraft");
    assert(Pretty("air_ship") == "Air Ship" && Pretty("polished_diorite") == "Polished Diorite" && Pretty("a--b_") == "A B");

    // Newer tags: pickaxe + iron tier -> iron pickaxe; several tiers listed -> the lowest.
    ToolInfo diamondOre = ToolFor("minecraft:is_pickaxe_item_destructible,minecraft:iron_tier_destructible,minecraft:diamond_tier_destructible");
    assert(diamondOre.tool == Tool::Pickaxe && diamondOre.tier == 2 && ToolItem(diamondOre) == "iron_pickaxe");
    // Older vanilla tags.
    assert(ToolItem(ToolFor("stone,wood_pick_diggable")) == "wood_pickaxe");
    assert(ToolItem(ToolFor("metal,iron_pick_diggable")) == "iron_pickaxe");
    assert(ToolItem(ToolFor("wood,minecraft:is_axe_item_destructible")) == "wood_axe");
    assert(ToolItem(ToolFor("dirt,fertilize_area")) == "wood_shovel");
    assert(ToolItem(ToolFor("minecraft:is_hoe_item_destructible,minecraft:is_shears_item_destructible")) == "wood_hoe");
    assert(ToolItem(ToolFor("minecraft:is_shears_item_destructible")) == "shears");
    assert(ToolFor("").tool == Tool::None && ToolItem(ToolFor("plant")).empty());

    // The held item: pickaxe tiers, "pickaxe" is not an axe.
    assert(HeldTier("minecraft:stone_pickaxe", Tool::Pickaxe) == 1 && HeldTier("minecraft:golden_pickaxe", Tool::Pickaxe) == 0);
    assert(HeldTier("minecraft:diamond_pickaxe", Tool::Axe) == -1 && HeldTier("minecraft:iron_axe", Tool::Axe) == 2);
    assert(HeldTier("", Tool::Pickaxe) == -1 && HeldTier("minecraft:shears", Tool::Shears) == 0);
    // Stone with a hand: red cross; with a wooden pickaxe: tick. Dirt by hand: no mark; with a shovel: tick.
    const ToolInfo stone = ToolFor("stone,minecraft:is_pickaxe_item_destructible");
    assert(Verdict(stone, "", true, false) == -1 && Verdict(stone, "minecraft:wooden_pickaxe", true, false) == 1);
    assert(Verdict(stone, "minecraft:dirt", true, true) == 1);   // the game says it harvests: trust it
    assert(Verdict(diamondOre, "minecraft:stone_pickaxe", true, false) == -1);
    const ToolInfo dirt = ToolFor("dirt");
    assert(Verdict(dirt, "", false, true) == 0 && Verdict(dirt, "minecraft:iron_shovel", false, true) == 1);

    // Names: from the game, through the .lang, or the id.
    std::map<std::string, std::string> lang{{"tile.stone.diorite.name", "Diorite"}};
    assert(DisplayName("閃緑岩", "minecraft:diorite", lang) == "閃緑岩");
    assert(DisplayName("@tile.stone.diorite", "minecraft:diorite", lang) == "Diorite");
    assert(DisplayName("@tile.unknown", "minecraft:polished_andesite", lang) == "Polished Andesite");
    assert(DisplayName("", "air_ship:engine_core", lang) == "Engine Core");

    // Where it comes from: packs found under a com.mojang folder (a behaviour pack and a world's resource pack).
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myclient_block_details_test";
    fs::remove_all(root);
    const fs::path bp = root / "behavior_packs" / "AirShipBP";
    fs::create_directories(bp / "blocks" / "parts");
    std::ofstream(bp / "manifest.json") << R"({ "format_version": 2, "header": { "name": "pack.name", "uuid": "x" } })";
    fs::create_directories(bp / "texts");
    std::ofstream(bp / "texts" / "en_US.lang") << "pack.name=Air Ship\n";
    std::ofstream(bp / "blocks" / "parts" / "engine.json") << R"({ "format_version": "1.20", "minecraft:block": { "description": { "identifier": "air_ship:engine" } } })";
    const fs::path rp = root / "minecraftWorlds" / "abc" / "resource_packs" / "Deco";
    fs::create_directories(rp);
    std::ofstream(rp / "manifest.json") << "{ \"header\": { \"name\": \"\xC2\xA7" "aDeco Blocks\" } }";
    std::ofstream(rp / "blocks.json") << R"({ "format_version": [1,1,0], "deco:lamp": { "textures": "lamp" }, "minecraft:stone": {} })";
    fs::create_directories(root / "minecraftpe");
    std::ofstream(root / "minecraftpe" / "options.txt") << "gfx_viewdistance:256\ngame_language:ja_JP\r\nother:1\n";
    const auto addons = ScanAddons({root});
    assert(addons.size() == 2 && addons.at("air_ship") == "Air Ship" && addons.at("deco") == "Deco Blocks");
    assert(SourceName("minecraft:diorite", addons) == "Minecraft" && SourceName("air_ship:engine", addons) == "Air Ship");
    assert(SourceName("other_mod:thing", addons) == "Other Mod");
    assert(GameLanguage({root}) == "ja_JP" && GameLanguage({root / "none"}) == "en_US");
    fs::remove_all(root);
    std::puts("PASS: Block Details tool, verdict, names and add-on sources");
}

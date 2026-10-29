#pragma once
// Block Details: the box at the top of the screen about the block under the crosshair (like Java's Jade / WAILA):
// its picture and name, where it comes from (Minecraft, or the add-on that adds it) and the tool that mines it.
// The DLL sends the block id, its name in the game's language, its tags and whether the held item harvests it
// (FeatureData.h Look); everything here is pure or plain std::filesystem, unit-tested in tests/block_details.cpp.
#include "SlotIcons.h"
#include "SkinLibrary.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace BlockDetails {

inline std::string Namespace(const std::string& id) {
    const size_t colon = id.find(':');
    return colon == std::string::npos ? std::string("minecraft") : id.substr(0, colon);
}
inline std::string PathOf(const std::string& id) {
    const size_t colon = id.find(':');
    return colon == std::string::npos ? id : id.substr(colon + 1);
}
// "air_ship" -> "Air Ship", "my-pack.blocks" -> "My Pack Blocks".
inline std::string Pretty(const std::string& name) {
    std::string out;
    bool upper = true;
    for (char c : name) {
        if (c == '_' || c == '-' || c == '.' || c == ' ') { if (!out.empty() && out.back() != ' ') out += ' '; upper = true; continue; }
        out += upper ? (char)std::toupper((unsigned char)c) : c;
        upper = false;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// --- The tool -------------------------------------------------------------------------------------------
enum class Tool { None, Pickaxe, Axe, Shovel, Hoe, Shears, Sword };
// Tiers in order: 0 wooden (anything), 1 stone, 2 iron, 3 diamond, 4 netherite.
struct ToolInfo { Tool tool = Tool::None; int tier = 0; };

inline std::vector<std::string> SplitTags(const std::string& tags) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= tags.size()) {
        size_t end = tags.find(',', start);
        if (end == std::string::npos) end = tags.size();
        if (end > start) out.push_back(tags.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

// From the block's tags: the newer "minecraft:is_<tool>_item_destructible" / "..._tier_destructible" ones that
// add-ons use too, and the older vanilla ones ("stone", "wood", "dirt", "iron_pick_diggable", ...). When a block
// lists several tiers, the lowest is what it needs.
inline ToolInfo ToolFor(const std::string& tags) {
    bool pick = false, axe = false, shovel = false, hoe = false, shears = false, sword = false;
    int tier = 99;
    const auto has = [](const std::string& s, const char* part) { return s.find(part) != std::string::npos; };
    for (const std::string& raw : SplitTags(tags)) {
        std::string t = raw;
        for (char& c : t) c = (char)std::tolower((unsigned char)c);
        const std::string bare = PathOf(t);
        if (has(t, "pickaxe") || has(t, "_pick_diggable") || bare == "stone" || bare == "metal") pick = true;
        else if (has(t, "is_axe_item") || bare == "wood" || bare == "log" || bare == "pumpkin") axe = true;
        else if (has(t, "shovel") || bare == "dirt" || bare == "sand" || bare == "gravel" || bare == "grass" || bare == "snow") shovel = true;
        else if (has(t, "hoe")) hoe = true;
        else if (has(t, "shears")) shears = true;
        else if (has(t, "sword")) sword = true;
        int level = -1;
        if (has(t, "stone_tier") || has(t, "stone_pick")) level = 1;
        else if (has(t, "iron_tier") || has(t, "iron_pick")) level = 2;
        else if (has(t, "diamond_tier") || has(t, "diamond_pick")) level = 3;
        else if (has(t, "netherite_tier") || has(t, "netherite_pick")) level = 4;
        if (level >= 0 && level < tier) tier = level;
        if (level >= 0) pick = true;
    }
    ToolInfo info;
    info.tool = pick ? Tool::Pickaxe : axe ? Tool::Axe : shovel ? Tool::Shovel : hoe ? Tool::Hoe : shears ? Tool::Shears
              : sword ? Tool::Sword : Tool::None;
    info.tier = info.tool == Tool::None || tier == 99 ? 0 : tier;
    return info;
}

inline const char* ToolWord(Tool tool) {
    switch (tool) {
    case Tool::Pickaxe: return "pickaxe";
    case Tool::Axe: return "axe";
    case Tool::Shovel: return "shovel";
    case Tool::Hoe: return "hoe";
    case Tool::Shears: return "shears";
    case Tool::Sword: return "sword";
    default: return "";
    }
}
// The picture that stands for the tool: the weakest one that does the job ("iron_pickaxe" for diamond ore). Named
// like the game's texture files (textures\items\wood_pickaxe.png: "wood", not the item id's "wooden").
inline std::string ToolItem(ToolInfo info) {
    if (info.tool == Tool::None) return {};
    if (info.tool == Tool::Shears) return "shears";
    static const char* tiers[] = {"wood", "stone", "iron", "diamond", "netherite"};
    return std::string(tiers[info.tier < 0 || info.tier > 4 ? 0 : info.tier]) + "_" + ToolWord(info.tool);
}
// The tier of a held tool item ("minecraft:golden_pickaxe" -> 0, like wood), or -1 when it is not that tool.
inline int HeldTier(const std::string& held, Tool tool) {
    const std::string path = PathOf(held);
    const std::string word = ToolWord(tool);
    if (word.empty() || path.size() < word.size() || path.compare(path.size() - word.size(), word.size(), word) != 0) return -1;
    if (tool == Tool::Axe && path.size() >= 7 && path.compare(path.size() - 7, 7, "pickaxe") == 0) return -1;
    if (tool == Tool::Shears) return path == "shears" ? 0 : -1;
    if (path.rfind("stone_", 0) == 0) return 1;
    if (path.rfind("iron_", 0) == 0) return 2;
    if (path.rfind("diamond_", 0) == 0) return 3;
    if (path.rfind("netherite_", 0) == 0) return 4;
    return 0;   // wooden, golden, an add-on's own
}
// The mark next to the tool: 1 = the held item mines / harvests it (green tick), -1 = it will not drop with what
// you hold (red cross), 0 = nothing to say (no tool needed and not holding it).
inline int Verdict(ToolInfo info, const std::string& held, bool requiresTool, bool canHarvest) {
    if (info.tool == Tool::None) return 0;
    const int tier = HeldTier(held, info.tool);
    if (requiresTool) return canHarvest || tier >= info.tier ? 1 : -1;
    return tier >= 0 ? 1 : 0;
}

// --- Where it comes from ----------------------------------------------------------------------------------
namespace fs = std::filesystem;
inline std::string ReadText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
// A pack's shown name: manifest.json header.name, through its texts\en_US.lang when it is a key ("pack.name").
inline std::string PackName(const fs::path& pack) {
    const SlotIcons::Json manifest = SlotIcons::ParseJson(ReadText(pack / "manifest.json"));
    const SlotIcons::Json* header = manifest.Get("header");
    const SlotIcons::Json* name = header ? header->Get("name") : nullptr;
    if (!name || name->type != SlotIcons::Json::String || name->text.empty()) return {};
    std::string text = name->text;
    if (text.find(' ') == std::string::npos && text.find('.') != std::string::npos) {
        const auto lang = SkinLibrary::ParseLang(ReadText(pack / "texts" / "en_US.lang"));
        if (const auto it = lang.find(text); it != lang.end() && !it->second.empty()) text = it->second;
    }
    // Formatting codes (§a) are for the game's screens.
    std::string clean;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((unsigned char)text[i] == 0xC2 && i + 2 < text.size() && (unsigned char)text[i + 1] == 0xA7) { i += 2; continue; }
        clean += text[i];
    }
    return clean;
}
// The namespaces of the blocks a pack adds: a resource pack's blocks.json keys, a behaviour pack's
// blocks\*.json "identifier"s. Vanilla's own namespace is left out.
inline std::vector<std::string> PackNamespaces(const fs::path& pack) {
    std::vector<std::string> out;
    const auto add = [&](const std::string& id) {
        if (id.find(':') == std::string::npos) return;
        const std::string ns = Namespace(id);
        if (ns.empty() || ns == "minecraft") return;
        for (const auto& known : out) if (known == ns) return;
        out.push_back(ns);
    };
    const SlotIcons::Json blocks = SlotIcons::ParseJson(ReadText(pack / "blocks.json"));
    for (const auto& member : blocks.members) add(member.first);
    std::error_code ec;
    const fs::path folder = pack / "blocks";
    int files = 0;
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end && files < 400; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".json") continue;
        ++files;
        const std::string text = ReadText(it->path());
        size_t at = text.find("\"identifier\"");
        if (at == std::string::npos) continue;
        at = text.find('"', text.find(':', at + 12) + 1);
        const size_t close = at == std::string::npos ? at : text.find('"', at + 1);
        if (close != std::string::npos) add(text.substr(at + 1, close - at - 1));
    }
    return out;
}
// namespace -> add-on name, from every pack folder under `roots` (com.mojang folders: behavior_packs,
// resource_packs, their development_ twins, and each world's own packs). The first pack found wins.
inline std::map<std::string, std::string> ScanAddons(const std::vector<fs::path>& roots) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    const auto scanFolder = [&](const fs::path& folder) {
        int packs = 0;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end && packs < 300; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            ++packs;
            const std::string name = PackName(it->path());
            if (name.empty()) continue;
            for (const std::string& ns : PackNamespaces(it->path())) out.emplace(ns, name);
        }
        ec.clear();
    };
    for (const fs::path& root : roots) {
        for (const char* kind : {"behavior_packs", "resource_packs", "development_behavior_packs", "development_resource_packs"})
            scanFolder(root / kind);
        int worlds = 0;
        for (fs::directory_iterator it(root / "minecraftWorlds", ec), end; !ec && it != end && worlds < 200; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            ++worlds;
            scanFolder(it->path() / "behavior_packs");
            scanFolder(it->path() / "resource_packs");
        }
        ec.clear();
    }
    return out;
}
// The line under the name: "Minecraft", the add-on's name, or its namespace made readable.
inline std::string SourceName(const std::string& id, const std::map<std::string, std::string>& addons) {
    const std::string ns = Namespace(id);
    if (ns == "minecraft") return "Minecraft";
    if (const auto it = addons.find(ns); it != addons.end()) return it->second;
    return Pretty(ns);
}

// --- The name -----------------------------------------------------------------------------------------------
// The game's language from minecraftpe\options.txt ("game_language:ja_JP"), else en_US.
inline std::string GameLanguage(const std::vector<fs::path>& roots) {
    for (const fs::path& root : roots) {
        const std::string text = ReadText(root / "minecraftpe" / "options.txt");
        const size_t at = text.find("game_language:");
        if (at == std::string::npos) continue;
        size_t end = text.find_first_of("\r\n", at);
        if (end == std::string::npos) end = text.size();
        const std::string lang = text.substr(at + 14, end - at - 14);
        if (!lang.empty()) return lang;
    }
    return "en_US";
}
// The shown name: what the DLL read in the game's language; for "@tile.stone.diorite" (the DLL could not ask the
// game) "<key>.name" from `lang`; else the id made readable ("minecraft:polished_diorite" -> "Polished Diorite").
inline std::string DisplayName(const std::string& fromGame, const std::string& id, const std::map<std::string, std::string>& lang) {
    if (!fromGame.empty() && fromGame[0] != '@') return fromGame;
    if (fromGame.size() > 1) {
        if (const auto it = lang.find(fromGame.substr(1) + ".name"); it != lang.end() && !it->second.empty()) return it->second;
    }
    return Pretty(PathOf(id));
}

} // namespace BlockDetails

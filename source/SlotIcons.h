#pragma once
// Inventory-style icons for any item id (Shulker Preview's popup). Like ItemIcons.h, every picture comes
// from the player's own Minecraft install: items use their flat texture (textures\item_texture.json),
// blocks are drawn as the small 3D cube the game shows in its inventory (blocks.json + terrain_texture.json).
// The pure parts (JSON reading, id -> textures, cube drawing) are unit-tested on the host.
#include "ItemIcons.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace SlotIcons {

// --- Minimal JSON reader (the game's pack files: objects, arrays, strings; comments allowed) ---------
struct Json {
    enum Type { Null, String, Other, Array, Object } type = Null;
    std::string text;                                     // String
    std::vector<Json> items;                              // Array
    std::vector<std::pair<std::string, Json>> members;    // Object
    const Json* Get(const std::string& key) const {
        if (type != Object) return nullptr;
        for (const auto& m : members) if (m.first == key) return &m.second;
        return nullptr;
    }
};

class JsonReader {
    const std::string& s_;
    size_t at_ = 0;
    int depth_ = 0;
    void Skip() {
        while (at_ < s_.size()) {
            const char c = s_[at_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++at_; continue; }
            if (c == '/' && at_ + 1 < s_.size() && s_[at_ + 1] == '/') { while (at_ < s_.size() && s_[at_] != '\n') ++at_; continue; }
            if (c == '/' && at_ + 1 < s_.size() && s_[at_ + 1] == '*') {
                const size_t end = s_.find("*/", at_ + 2);
                at_ = end == std::string::npos ? s_.size() : end + 2;
                continue;
            }
            break;
        }
    }
    bool ReadString(std::string& out) {
        if (at_ >= s_.size() || s_[at_] != '"') return false;
        ++at_;
        while (at_ < s_.size() && s_[at_] != '"') {
            char c = s_[at_++];
            if (c == '\\' && at_ < s_.size()) {
                c = s_[at_++];
                if (c == 'n') c = '\n'; else if (c == 't') c = '\t';
                else if (c == 'u') { at_ = std::min(s_.size(), at_ + 4); c = '?'; }   // ids and paths are ASCII
            }
            out += c;
        }
        if (at_ >= s_.size()) return false;
        ++at_;
        return true;
    }
    bool Value(Json& out) {
        Skip();
        if (at_ >= s_.size() || ++depth_ > 64) return false;
        bool ok = true;
        const char c = s_[at_];
        if (c == '"') { out.type = Json::String; ok = ReadString(out.text); }
        else if (c == '{' || c == '[') {
            const bool object = c == '{';
            out.type = object ? Json::Object : Json::Array;
            ++at_;
            Skip();
            if (at_ < s_.size() && s_[at_] == (object ? '}' : ']')) ++at_;
            else for (;;) {
                if (object) {
                    std::string key;
                    Skip();
                    if (!ReadString(key)) { ok = false; break; }
                    Skip();
                    if (at_ >= s_.size() || s_[at_++] != ':') { ok = false; break; }
                    out.members.emplace_back(std::move(key), Json{});
                    if (!Value(out.members.back().second)) { ok = false; break; }
                } else {
                    out.items.emplace_back();
                    if (!Value(out.items.back())) { ok = false; break; }
                }
                Skip();
                if (at_ < s_.size() && s_[at_] == ',') {
                    ++at_; Skip();
                    if (at_ < s_.size() && s_[at_] == (object ? '}' : ']')) { ++at_; break; }   // trailing comma
                    continue;
                }
                if (at_ < s_.size() && s_[at_] == (object ? '}' : ']')) { ++at_; break; }
                ok = false;
                break;
            }
        } else {   // number, true, false, null: not needed, skipped
            out.type = Json::Other;
            while (at_ < s_.size() && s_[at_] != ',' && s_[at_] != '}' && s_[at_] != ']' && !std::isspace(static_cast<unsigned char>(s_[at_]))) ++at_;
        }
        --depth_;
        return ok;
    }
public:
    explicit JsonReader(const std::string& s) : s_(s) {}
    bool Read(Json& out) { return Value(out); }
};

inline Json ParseJson(const std::string& text) {
    Json out;
    if (!JsonReader(text).Read(out)) return Json{};
    return out;
}

// --- Item id -> what to draw ---------------------------------------------------------------------
// Texture paths are relative to a resource pack ("textures/blocks/stone"), without extension.
struct Model {
    enum Kind { None, Flat, Cube } kind = None;
    std::string top, left, right;   // Flat uses `top`
    float height = 1.0f;            // Cube: 1 full block, 0.5 slab, 1/16 carpet
    uint32_t tint = 0;              // 0x00RRGGBB multiplied in (grey foliage textures), 0 = none
    // terrain_texture.json "overlay_color" by texture path (the grass block's side): the texture's alpha marks where
    // the colour goes, the rest is opaque (Overlay).
    std::map<std::string, uint32_t> overlay;
    std::vector<std::string> more;  // Flat: further texture paths to try when `top` / `left` are missing
};

// First path of a "textures" value: "path" | ["path", ...] | {"path": "..."} | [{"path": ...}, ...].
inline std::string FirstPath(const Json* v) {
    if (!v) return {};
    if (v->type == Json::String) return v->text;
    if (v->type == Json::Array) return v->items.empty() ? std::string() : FirstPath(&v->items[0]);
    if (v->type == Json::Object) return FirstPath(v->Get("path"));
    return {};
}

// "overlay_color" ("#79c05a") of the first entry of a "textures" value, 0 = none.
inline uint32_t FirstOverlay(const Json* v) {
    if (!v) return 0;
    if (v->type == Json::Array) return v->items.empty() ? 0 : FirstOverlay(&v->items[0]);
    if (v->type != Json::Object) return 0;
    const Json* color = v->Get("overlay_color");
    if (!color || color->type != Json::String || color->text.size() != 7 || color->text[0] != '#') return 0;
    uint32_t rgb = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = (char)std::tolower((unsigned char)color->text[i]);
        if (c >= '0' && c <= '9') rgb = rgb * 16 + (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') rgb = rgb * 16 + (uint32_t)(c - 'a' + 10);
        else return 0;
    }
    return rgb | 0x01000000u;   // the flag keeps black (#000000) apart from "none"
}

// The pack files the lookup needs, already parsed.
struct PackData {
    Json itemTextures;      // textures/item_texture.json -> texture_data
    Json terrainTextures;   // textures/terrain_texture.json -> texture_data
    Json blocks;            // blocks.json
};

inline bool Contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
inline bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
inline bool EndsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

// Item ids whose icon key in item_texture.json differs, most likely first.
inline std::vector<std::string> ItemKeys(const std::string& id) {
    std::vector<std::string> keys{id};
    const auto swapPrefix = [&](const char* prefix, const char* suffix) {
        if (StartsWith(id, prefix)) keys.push_back(id.substr(std::string(prefix).size()) + suffix);
    };
    const auto swapSuffix = [&](const std::string& suffix, const char* prefix) {
        if (EndsWith(id, suffix)) keys.push_back(prefix + id.substr(0, id.size() - suffix.size()));
    };
    swapPrefix("cooked_", "_cooked");
    swapPrefix("golden_", "_golden");
    if (StartsWith(id, "golden_")) keys.push_back("gold_" + id.substr(7));
    swapPrefix("enchanted_", "_enchanted");
    swapPrefix("written_", "_written");
    swapPrefix("writable_", "_writable");
    swapPrefix("glistering_", "_speckled");
    swapSuffix("_bucket", "bucket_");
    swapSuffix("_door", "door_");
    swapSuffix("_boat", "boat_");
    swapSuffix("_chest_boat", "chest_boat_");
    if (id == "wooden_door" || id == "oak_door") keys.push_back("door_wood");
    if (EndsWith(id, "_spawn_egg")) keys.push_back("spawn_egg");
    if (EndsWith(id, "_dye")) keys.push_back("dye_powder");
    return keys;
}

// Texture file names (without folder) an item id's icon may have in textures/items, most likely first. The item
// list keeps many icons under a shared key with an array of pictures ("axe": wood_axe, stone_axe, ...), or under an
// older name ("bow_standby", "record_cat", "boat_darkoak"), so the files are found by name (2026-10-28: the
// Shulker Preview and Block Details showed letters for these).
inline std::vector<std::string> ItemFileNames(const std::string& id) {
    std::vector<std::string> out;
    const auto add = [&](const std::string& n) { if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n); };
    std::string base = id;
    const auto replaceStart = [](std::string s, const char* from, const char* to) {
        return StartsWith(s, from) ? std::string(to) + s.substr(std::string(from).size()) : s;
    };
    base = replaceStart(base, "golden_", "gold_");
    base = replaceStart(base, "wooden_", "wood_");
    add(base);
    const auto suffix = [&](const std::string& end) -> std::string {   // "acacia_boat" with "_boat" -> "acacia"
        return EndsWith(id, end) ? id.substr(0, id.size() - end.size()) : std::string();
    };
    const auto wood = [](std::string w) { return w == "dark_oak" ? std::string("darkoak") : w; };
    if (auto w = suffix("_chest_boat"); !w.empty()) add(w + "_chest_boat");
    else if (auto w2 = suffix("_boat"); !w2.empty()) { add("boat_" + wood(w2)); add(w2 + "_boat"); }
    if (id == "bamboo_raft") add("bamboo_raft");
    if (auto w = suffix("_hanging_sign"); !w.empty()) add("sign_" + wood(w) + "_hanging");
    else if (auto w2 = suffix("_sign"); !w2.empty()) { add(w2 == "oak" ? "sign" : "sign_" + wood(w2)); add(w2 + "_sign"); }
    if (auto c = suffix("_bundle"); !c.empty()) add("bundle_" + c);
    if (auto c = suffix("_harness"); !c.empty()) add("harness/harness_" + c);
    if (auto c = suffix("_bed"); !c.empty()) add("bed_" + (c == "light_gray" ? std::string("silver") : c));
    if (auto c = suffix("_dye"); !c.empty()) add("dye_powder_" + (c == "light_gray" ? std::string("silver") : c));
    if (auto x = suffix("_spear"); !x.empty()) add("spear/" + replaceStart(replaceStart(x, "golden", "gold"), "wooden", "wood") + "_spear");
    if (auto x = suffix("_seeds"); !x.empty()) add("seeds_" + x);
    if (auto x = suffix("_bucket"); !x.empty()) {
        static const std::pair<const char*, const char*> buckets[] = {{"tropical_fish", "tropical"}, {"powder_snow", "powder_snow"}};
        std::string b = x;
        for (const auto& [from, to] : buckets) if (x == from) b = to;
        add("bucket_" + b);
    }
    if (id == "bucket") add("bucket_empty");
    if (auto x = suffix("_minecart"); !x.empty()) add("minecart_" + x);
    if (id == "minecart") add("minecart_normal");
    if (StartsWith(id, "music_disc_")) add("record_" + id.substr(11));
    if (StartsWith(id, "cooked_")) { add(id.substr(7) + "_cooked"); if (id == "cooked_cod") add("fish_cooked"); }
    static const std::pair<const char*, const char*> renamed[] = {
        {"beef", "beef_raw"}, {"chicken", "chicken_raw"}, {"mutton", "mutton_raw"}, {"porkchop", "porkchop_raw"},
        {"rabbit", "rabbit_raw"}, {"cod", "fish_raw"}, {"salmon", "fish_salmon_raw"}, {"tropical_fish", "fish_clownfish_raw"},
        {"pufferfish", "fish_pufferfish_raw"}, {"cooked_cod", "fish_cooked"}, {"cooked_salmon", "fish_salmon_cooked"},
        {"bow", "bow_standby"}, {"crossbow", "crossbow_standby"}, {"book", "book_normal"}, {"enchanted_book", "book_enchanted"},
        {"writable_book", "book_writable"}, {"written_book", "book_written"}, {"clock", "clock_item"}, {"compass", "compass_item"},
        {"recovery_compass", "recovery_compass_item"}, {"lodestone_compass", "lodestonecompass_item"},
        {"redstone", "redstone_dust"}, {"sugar_cane", "reeds"}, {"slime_ball", "slimeball"}, {"totem_of_undying", "totem"},
        {"lapis_lazuli", "dye_powder_blue"}, {"ink_sac", "dye_powder_black"}, {"cocoa_beans", "dye_powder_brown"},
        {"bone_meal", "dye_powder_white"}, {"glow_ink_sac", "dye_powder_glow"}, {"fishing_rod", "fishing_rod_uncast"},
        {"baked_potato", "potato_baked"}, {"poisonous_potato", "potato_poisonous"}, {"popped_chorus_fruit", "chorus_fruit_popped"},
        {"melon_slice", "melon"}, {"glistering_melon_slice", "melon_speckled"}, {"fermented_spider_eye", "spider_eye_fermented"},
        {"glass_bottle", "potion_bottle_empty"}, {"potion", "potion_bottle_drinkable"}, {"splash_potion", "potion_bottle_splash"},
        {"lingering_potion", "potion_bottle_lingering"}, {"empty_map", "map_empty"}, {"filled_map", "map_filled"},
        {"firework_rocket", "fireworks"}, {"firework_star", "fireworks_charge"}, {"fire_charge", "fireball"},
        {"turtle_scute", "turtle_shell_piece"}, {"scute", "turtle_shell_piece"}, {"enchanted_golden_apple", "apple_golden"},
        {"golden_apple", "apple_golden"}, {"golden_carrot", "carrot_golden"}, {"experience_bottle", "experience_bottle"},
        {"banner", "banner_pattern"}, {"oak_door", "door_wood"}, {"wooden_door", "door_wood"}, {"iron_door", "door_iron"},
        {"nether_brick", "netherbrick"}, {"chainmail_helmet", "chainmail_helmet"},
        {"command_block_minecart", "minecart_command_block"}, {"tnt_minecart", "minecart_tnt"}, {"chest_minecart", "minecart_chest"},
        {"hopper_minecart", "minecart_hopper"}, {"furnace_minecart", "minecart_furnace"}, {"bundle", "bundle"},
    };
    for (const auto& [from, to] : renamed) if (id == from) add(to);
    if (auto c = suffix("_door"); !c.empty()) add("door_" + c);
    if (EndsWith(id, "_spawn_egg")) add("spawn_egg/spawn_egg_" + id.substr(0, id.size() - 10));
    return out;
}

// Blocks the game shows as a flat picture in the inventory (plants, torches, rails, panes, ...).
inline bool FlatBlock(const std::string& id) {
    static const char* const parts[] = {
        "torch", "sapling", "rail", "ladder", "vine", "lantern", "_bars", "pane", "chain", "lever",
        "tulip", "dandelion", "poppy", "orchid", "allium", "azure_bluet", "oxeye_daisy", "cornflower",
        "lily_of_the_valley", "wither_rose", "sunflower", "lilac", "rose_bush", "peony", "fern",
        "short_grass", "tall_grass", "dead_bush", "cobweb", "web", "kelp", "seagrass", "roots", "propagule",
        "dripstone", "_bud", "amethyst_cluster", "tripwire_hook", "fungus", "sprouts", "glow_lichen",
        "sculk_vein", "pink_petals", "spore_blossom", "frogspawn", "eyeblossom", "bush", "firefly",
    };
    if (Contains(id, "mushroom") && !Contains(id, "block") && !Contains(id, "stem")) return true;
    if (Contains(id, "coral") && !Contains(id, "block")) return true;
    if (id == "sea_lantern") return false;   // a full block
    for (const char* p : parts) if (Contains(id, p)) return true;
    return false;
}

// Grey textures the game tints by biome; the inventory shows them in the default greens.
inline uint32_t FoliageTint(const std::string& id) {
    if (Contains(id, "spruce_leaves")) return 0x619961;
    if (Contains(id, "birch_leaves")) return 0x80A755;
    if (Contains(id, "leaves") && !Contains(id, "cherry") && !Contains(id, "azalea") && !Contains(id, "pale")) return 0x48B518;
    if (id == "vine" || Contains(id, "fern") || id == "short_grass" || id == "tall_grass" || id == "waterlily" || id == "lily_pad") return 0x48B518;
    return 0;
}

inline Model Resolve(const PackData& pack, const std::string& id) {
    Model m;
    // 1) Items with their own icon (also blocks the game draws flat, like cake or doors).
    for (const auto& key : ItemKeys(id))
        if (const Json* entry = pack.itemTextures.Get(key)) {
            const std::string path = FirstPath(entry->Get("textures"));
            if (!path.empty()) { m.kind = Model::Flat; m.top = path; return m; }
        }
    // 2) Blocks: the faces from blocks.json ("carried_textures" is what the inventory uses). A few blocks keep
    // their old name there.
    const Json* block = pack.blocks.Get(id);
    if (!block) {
        static const std::pair<const char*, const char*> renamed[] = {
            {"grass_block", "grass"}, {"sea_lantern", "seaLantern"}, {"iron_chain", "chain"}, {"trip_wire", "tripWire"}};
        for (const auto& [now, old] : renamed)
            if (id == now) block = pack.blocks.Get(old);
    }
    if (block) {
        const Json* textures = block->Get("carried_textures");
        if (!textures) textures = block->Get("textures");
        const auto terrain = [&](const Json* key) -> std::string {
            if (!key || key->type != Json::String) return {};
            const Json* entry = pack.terrainTextures.Get(key->text);
            if (!entry) return std::string();
            const std::string path = FirstPath(entry->Get("textures"));
            if (const uint32_t color = FirstOverlay(entry->Get("textures"))) m.overlay[path] = color;
            return path;
        };
        std::string up, south, east, side;
        if (textures && textures->type == Json::String) up = south = east = side = terrain(textures);
        else if (textures && textures->type == Json::Object) {
            side = terrain(textures->Get("side"));
            up = terrain(textures->Get("up"));
            south = terrain(textures->Get("south"));
            east = terrain(textures->Get("east"));
            if (side.empty()) side = !south.empty() ? south : terrain(textures->Get("north"));
            if (up.empty()) up = side;
            if (south.empty()) south = side;
            if (east.empty()) east = side;
        }
        if (!up.empty() || !side.empty()) {
            m.tint = FoliageTint(id);
            if (FlatBlock(id)) { m.kind = Model::Flat; m.top = !side.empty() ? side : up; return m; }
            m.kind = Model::Cube;
            m.top = up.empty() ? side : up;
            m.left = south.empty() ? m.top : south;
            m.right = east.empty() ? m.top : east;
            if (Contains(id, "_slab")) m.height = 0.5f;
            else if (Contains(id, "carpet") || Contains(id, "pressure_plate") || id == "moss_carpet") m.height = 1.0f / 16;
            else if (Contains(id, "snow_layer")) m.height = 2.0f / 16;
            return m;
        }
    }
    // 3) A texture file with the id's own name (tried by the loader in this order), then the names its icon file
    // is known to have (ItemFileNames).
    m.kind = Model::Flat;
    m.top = "textures/items/" + id;
    m.left = "textures/blocks/" + id;   // second choice for Flat
    for (const auto& name : ItemFileNames(id)) m.more.push_back("textures/items/" + name);
    return m;
}

// --- Pictures ------------------------------------------------------------------------------------
using ItemIcons::Image;

// The first square frame of a texture (animated textures are vertical strips).
inline Image FirstFrame(const Image& in) {
    if (in.h <= in.w || in.w <= 0) return in;
    Image out;
    out.w = out.h = in.w;
    out.bgra.assign(in.bgra.begin(), in.bgra.begin() + static_cast<size_t>(in.w) * in.w);
    return out;
}

// Overlay colour (terrain_texture.json): where the texture's alpha is set the colour is multiplied in (by that
// alpha), and the whole picture becomes opaque (the alpha was only the mask).
inline void Overlay(Image& image, uint32_t color) {
    if (!(color & 0x01000000u)) return;
    const uint32_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    const auto mix = [](uint32_t c, uint32_t k, uint32_t a) { return (c * (255 - a) + c * k / 255 * a) / 255; };
    for (auto& p : image.bgra) {
        const uint32_t a = p >> 24;
        p = 0xFF000000u | (mix((p >> 16) & 0xFF, r, a) << 16) | (mix((p >> 8) & 0xFF, g, a) << 8) | mix(p & 0xFF, b, a);
    }
}

inline void Tint(Image& image, uint32_t rgb) {
    if (!rgb) return;
    const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    for (auto& p : image.bgra)
        p = (p & 0xFF000000u) | ((((p >> 16) & 0xFF) * r / 255) << 16) | ((((p >> 8) & 0xFF) * g / 255) << 8) | ((p & 0xFF) * b / 255);
}

// Nearest texel at (u, v) in [0, 1), premultiplied, darkened by `shade`.
inline uint32_t Sample(const Image& t, float u, float v, float shade) {
    if (t.w <= 0 || t.h <= 0) return 0;
    int x = static_cast<int>(u * t.w), y = static_cast<int>(v * t.h);
    x = std::min(std::max(x, 0), t.w - 1);
    y = std::min(std::max(y, 0), t.h - 1);
    const uint32_t p = t.bgra[static_cast<size_t>(y) * t.w + x];
    const auto c = [&](int sh) { return static_cast<uint32_t>(((p >> sh) & 0xFF) * shade + 0.5f) << sh; };
    return (p & 0xFF000000u) | c(16) | c(8) | c(0);
}

// The inventory's 3D block: top face plus the two front faces (left lighter than right), in a
// size x size picture, 3x3 supersampled. `height` < 1 draws a slab / carpet (bottom part of the sides).
inline Image RenderCube(const Image& top, const Image& left, const Image& right, int size, float height) {
    Image out;
    if (size <= 0) return out;
    out.w = out.h = size;
    out.bgra.assign(static_cast<size_t>(size) * size, 0);
    height = std::min(std::max(height, 1.0f / 16), 1.0f);
    const float a = size * 0.45f;                  // half width of the cube
    const float full = a * 1.14f;                   // side height of a full block
    const float h = full * height;
    const float cx = size * 0.5f;
    const float y0 = (size - (a + full)) * 0.5f + (full - h);   // top corner of the (lowered) top face
    const int ss = 3;
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px) {
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx) {
                    const float x = px + (sx + 0.5f) / ss, y = py + (sy + 0.5f) / ss;
                    uint32_t p = 0;
                    const float dx = x - cx, dy = y - y0;
                    const float u = (dx / a + 2 * dy / a) * 0.5f, v = (2 * dy / a - dx / a) * 0.5f;
                    if (u >= 0 && u < 1 && v >= 0 && v < 1) p = Sample(top, u, v, 1.0f);
                    else if (x >= cx - a && x < cx) {                       // left face
                        const float s = (x - (cx - a)) / a;
                        const float t = (y - (y0 + a * 0.5f) - s * a * 0.5f) / h;
                        if (t >= 0 && t < 1) p = Sample(left, s, (1 - height) + t * height, 0.8f);
                    } else if (x >= cx && x < cx + a) {                     // right face
                        const float s = (x - cx) / a;
                        const float t = (y - (y0 + a) + s * a * 0.5f) / h;
                        if (t >= 0 && t < 1) p = Sample(right, s, (1 - height) + t * height, 0.6f);
                    }
                    for (int c = 0; c < 4; ++c) sum[c] += (p >> (c * 8)) & 0xFF;
                }
            uint32_t p = 0;
            for (int c = 0; c < 4; ++c) p |= (sum[c] / (ss * ss)) << (c * 8);
            out.bgra[static_cast<size_t>(py) * size + px] = p;
        }
    return out;
}

#ifdef _WIN32
namespace fs = std::filesystem;

inline std::string ReadFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Icons by item id and size for one game folder and pack list (rebuilt when either changes). Misses are remembered too.
class Cache {
    std::wstring gameDir_;
    std::vector<fs::path> packs_, roots_;
    PackData data_;
    std::map<std::string, std::unique_ptr<Image>> textures_;   // by pack path; null = missing
    std::map<std::string, ItemIcons::Icon> icons_;
    std::vector<std::string> misses_;   // "id: paths tried" for every id without a picture (icons_scan.txt)

    void Reset(const std::wstring& gameDir, const std::vector<fs::path>& packs) {
        Clear();
        gameDir_ = gameDir;
        packs_ = packs;
        // Active packs first, then the game's packs newest first (vanilla_1.xx carry newer content), base "vanilla" last.
        roots_.assign(packs.begin(), packs.end());
        const fs::path all = fs::path(gameDir) / L"data" / L"resource_packs";
        std::error_code ec;
        std::vector<fs::path> others;
        for (fs::directory_iterator it(all, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec) && it->path().filename() != L"vanilla") others.push_back(it->path());
        std::sort(others.rbegin(), others.rend());
        roots_.insert(roots_.end(), others.begin(), others.end());
        roots_.push_back(all / L"vanilla");
        // The lists of every pack, merged in that order (Json::Get returns the first match, so earlier packs win).
        data_ = {};
        data_.itemTextures.type = data_.terrainTextures.type = data_.blocks.type = Json::Object;
        const auto merge = [](Json& into, const Json* from) {
            if (from && from->type == Json::Object) into.members.insert(into.members.end(), from->members.begin(), from->members.end());
        };
        for (const auto& root : roots_) {
            const Json items = ParseJson(ReadFile(root / L"textures" / L"item_texture.json"));
            const Json terrain = ParseJson(ReadFile(root / L"textures" / L"terrain_texture.json"));
            const Json blocks = ParseJson(ReadFile(root / L"blocks.json"));
            merge(data_.itemTextures, items.Get("texture_data"));
            merge(data_.terrainTextures, terrain.Get("texture_data"));
            merge(data_.blocks, &blocks);
        }
    }

    // A texture by pack path ("textures/blocks/stone"), active packs first; nullptr when no pack has it.
    // straight: not premultiplied, for an overlay mask (the colour under its see-through pixels is used).
    const Image* Texture(const std::string& path, bool straight = false) {
        if (path.empty()) return nullptr;
        const std::string key = straight ? "~" + path : path;
        const auto known = textures_.find(key);
        if (known != textures_.end()) return known->second.get();
        auto& slot = textures_[key];
        const std::wstring rel(path.begin(), path.end());
        std::error_code ec;
        for (const auto& root : roots_) {
            const std::wstring base = (root / rel).wstring();
            Image image;
            if (fs::is_regular_file(base + L".png", ec) && ItemIcons::DecodeWic(base + L".png", image, !straight)) {}
            else if (fs::is_regular_file(base + L".tga", ec) && ItemIcons::DecodeTga(ReadFile(base + L".tga"), image, !straight)) {}
            else continue;
            slot = std::make_unique<Image>(FirstFrame(image));
            break;
        }
        return slot.get();
    }

    Image Build(const std::string& id, int size) {
        const Model m = Resolve(data_, id);
        if (m.kind == Model::Cube) {
            const auto face = [&](const std::string& path) { return Texture(path, m.overlay.count(path) != 0); };
            const Image* top = face(m.top);
            const Image* left = face(m.left);
            const Image* right = face(m.right);
            if (!top && !left && !right) return {};
            const auto overlaid = [&](const Image* image, const std::string& path) {
                Image out = *image;
                if (const auto it = m.overlay.find(path); it != m.overlay.end()) Overlay(out, it->second);
                return out;
            };
            Image t = top ? overlaid(top, m.top) : (left ? overlaid(left, m.left) : overlaid(right, m.right));
            Image l = left ? overlaid(left, m.left) : t, r = right ? overlaid(right, m.right) : t;
            Tint(t, m.tint); Tint(l, m.tint); Tint(r, m.tint);
            return RenderCube(t, l, r, size, m.height);
        }
        if (m.kind == Model::Flat) {
            const Image* flat = Texture(m.top);
            if (!flat) flat = Texture(m.left);
            for (size_t i = 0; !flat && i < m.more.size(); i++) flat = Texture(m.more[i]);
            if (!flat) return {};
            Image out = *flat;
            Tint(out, m.tint);
            return out;
        }
        return {};
    }
public:
    ~Cache() { Clear(); }
    const std::vector<std::string>& Misses() const { return misses_; }
    const std::vector<fs::path>& Roots() const { return roots_; }
    void Clear() {
        for (auto& [key, icon] : icons_) if (icon.bitmap) DeleteObject(icon.bitmap);
        icons_.clear();
        textures_.clear();
        misses_.clear();
    }
    // nullptr when the game has no picture for this item (the popup then shows a placeholder).
    const ItemIcons::Icon* Get(const std::wstring& gameDir, const std::vector<fs::path>& activePacks, const std::string& id, int size) {
        if (gameDir.empty() || id.empty() || size <= 0) return nullptr;
        if (gameDir != gameDir_ || activePacks != packs_) Reset(gameDir, activePacks);
        auto& icon = icons_[id + "|" + std::to_string(size)];
        if (!icon.tried) {
            icon.tried = true;
            icon.image = Build(id, size);
            if (icon.image.bgra.empty() && misses_.size() < 200) {
                const Model m = Resolve(data_, id);
                std::string line = id + (m.kind == Model::Cube ? " (block):" : ":");
                for (const std::string* p : {&m.top, &m.left, &m.right}) if (!p->empty()) line += " " + *p;
                for (const auto& p : m.more) line += " " + p;
                if (std::find(misses_.begin(), misses_.end(), line) == misses_.end()) misses_.push_back(line);
            }
        }
        return ItemIcons::MakeBitmap(icon) ? &icon : nullptr;
    }
};
#endif

}  // namespace SlotIcons

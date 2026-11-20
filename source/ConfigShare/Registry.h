// Fields that may be shared between players (docs/config-sharing-design.md §3).
// The ids are a file format: never renumber or reuse one. FieldIds.md must list
// exactly the same rows (checked by tests/config_share.cpp).
// Windows-independent: do not include <windows.h> here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ConfigShare {

enum class Category : uint8_t { Modules = 1, Hud = 2, Theme = 4, Lite = 8 };
constexpr uint8_t AllCategories = 1 | 2 | 4 | 8;

enum class Kind : uint8_t { Bool, Int };

enum : uint8_t { EffectLite = 1, EffectRenderDistance = 2 };

struct Field {
    uint16_t id;
    const char* key;
    Category category;
    Kind kind;
    int32_t min, max;
    int32_t step;     // values snap to min + k*step (8 for the Lite distances, 2 for render distance)
    uint8_t effects;
};

constexpr int SchemaVersion = 1;

inline constexpr Field kFields[] = {
    {1, "mod.keystrokes", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {2, "mod.cps", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {3, "mod.armor", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {4, "mod.arrows", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {5, "mod.fps", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {7, "mod.block_outline", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {8, "mod.zoom", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {9, "mod.resources", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {10, "mod.fullbright", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {11, "mod.nametag", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {12, "mod.ping", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {13, "mod.textures", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {14, "mod.feature_list", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {15, "mod.shulker", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {16, "mod.hive", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {17, "mod.tab_list", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {18, "mod.hive_stats", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {19, "mod.chunk_border", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {20, "mod.hitbox", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {21, "mod.nick", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {22, "mod.replay", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {23, "mod.menu_sounds", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {24, "mod.lite", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {25, "mod.fast_inventory", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {26, "mod.left_hand", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {27, "mod.hit_effect", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {28, "mod.frame_gen", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {29, "mod.day_timer", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {30, "mod.debug_menu", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {31, "mod.block_details", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {32, "mod.health_warning", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {33, "mod.coordinates", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {34, "mod.timer", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {35, "mod.cps_limiter", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {36, "mod.item_counter", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {37, "mod.pack_display", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {38, "mod.java_chat", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {39, "mod.minimap", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {40, "mod.combo", Category::Modules, Kind::Bool, 0, 1, 1, 0},
    {104, "hud.keystrokes.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {105, "hud.keystrokes.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {106, "hud.keystrokes.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {108, "hud.cps.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {109, "hud.cps.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {110, "hud.cps.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {112, "hud.armor.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {113, "hud.armor.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {114, "hud.armor.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {116, "hud.arrows.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {117, "hud.arrows.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {118, "hud.arrows.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {120, "hud.fps.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {121, "hud.fps.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {122, "hud.fps.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {124, "hud.resources.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {125, "hud.resources.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {126, "hud.resources.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {128, "hud.ping.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {129, "hud.ping.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {130, "hud.ping.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {132, "hud.feature_list.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {133, "hud.feature_list.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {134, "hud.feature_list.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {136, "hud.shulker.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {137, "hud.shulker.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {138, "hud.shulker.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {140, "hud.tab_list.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {141, "hud.tab_list.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {142, "hud.tab_list.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {144, "hud.hive_stats.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {145, "hud.hive_stats.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {146, "hud.hive_stats.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {148, "hud.day_timer.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {149, "hud.day_timer.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {150, "hud.day_timer.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {152, "hud.block_details.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {153, "hud.block_details.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {154, "hud.block_details.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {156, "hud.coordinates.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {157, "hud.coordinates.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {158, "hud.coordinates.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {160, "hud.timer.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {161, "hud.timer.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {162, "hud.timer.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {164, "hud.pack_display.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {165, "hud.pack_display.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {166, "hud.pack_display.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {168, "hud.java_chat.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {169, "hud.java_chat.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {170, "hud.java_chat.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {172, "hud.minimap.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {173, "hud.minimap.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {174, "hud.minimap.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {176, "hud.combo.x", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {177, "hud.combo.y", Category::Hud, Kind::Int, 0, 10000, 1, 0},
    {178, "hud.combo.scale", Category::Hud, Kind::Int, 50, 300, 1, 0},
    {300, "theme.dark", Category::Theme, Kind::Bool, 0, 1, 1, 0},
    {301, "theme.accent", Category::Theme, Kind::Int, 0, 7, 1, 0},
    {302, "theme.light", Category::Theme, Kind::Int, 0, 6, 1, 0},
    {303, "theme.dark_tint", Category::Theme, Kind::Int, 0, 7, 1, 0},
    {304, "theme.shadow", Category::Theme, Kind::Int, 0, 100, 1, 0},
    {305, "theme.opacity", Category::Theme, Kind::Int, 20, 100, 1, 0},
    {306, "theme.custom0.h", Category::Theme, Kind::Int, 0, 359, 1, 0},
    {307, "theme.custom0.s", Category::Theme, Kind::Int, 0, 100, 1, 0},
    {308, "theme.custom0.v", Category::Theme, Kind::Int, 0, 100, 1, 0},
    {309, "theme.custom1.h", Category::Theme, Kind::Int, 0, 359, 1, 0},
    {310, "theme.custom1.s", Category::Theme, Kind::Int, 0, 100, 1, 0},
    {311, "theme.custom1.v", Category::Theme, Kind::Int, 0, 100, 1, 0},
    {312, "theme.custom2.h", Category::Theme, Kind::Int, 0, 359, 1, 0},
    {313, "theme.custom2.s", Category::Theme, Kind::Int, 0, 100, 1, 0},
    {314, "theme.custom2.v", Category::Theme, Kind::Int, 0, 100, 1, 0},
    {400, "lite.clouds", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {401, "lite.sky", Category::Lite, Kind::Bool, 0, 1, 1, EffectLite},
    {402, "lite.fog", Category::Lite, Kind::Bool, 0, 1, 1, EffectLite},
    {403, "lite.weather", Category::Lite, Kind::Bool, 0, 1, 1, EffectLite},
    {404, "lite.no_shadows", Category::Lite, Kind::Bool, 0, 1, 1, EffectLite},
    {405, "lite.no_particles", Category::Lite, Kind::Bool, 0, 1, 1, EffectLite},
    {406, "lite.no_crit", Category::Lite, Kind::Bool, 0, 1, 1, EffectLite},
    {407, "lite.no_potion", Category::Lite, Kind::Bool, 0, 1, 1, EffectLite},
    {408, "lite.entity_limit", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {409, "lite.entity_distance", Category::Lite, Kind::Int, 8, 128, 8, 0},
    {410, "lite.cull_mobs", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {411, "lite.cull_items", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {412, "lite.cull_misc", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {413, "lite.player_limit", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {414, "lite.player_distance", Category::Lite, Kind::Int, 8, 128, 8, 0},
    {415, "lite.no_name_tags", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {416, "lite.no_sky_full", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {417, "lite.cull_behind", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {418, "lite.block_entity_limit", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {419, "lite.block_entity_distance", Category::Lite, Kind::Int, 8, 128, 8, 0},
    {420, "lite.vertical_limit", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {421, "lite.vertical_rows", Category::Lite, Kind::Int, 1, 8, 1, 0},
    {422, "lite.render", Category::Lite, Kind::Bool, 0, 1, 1, EffectRenderDistance},
    {423, "lite.render_distance", Category::Lite, Kind::Int, 4, 48, 2, EffectRenderDistance},
    {424, "lite.fps_limit", Category::Lite, Kind::Bool, 0, 1, 1, 0},
    {425, "lite.fps_limit_code", Category::Lite, Kind::Int, 1, 6, 1, 0},
};

inline constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

inline const Field* FindById(uint16_t id)
{
    for (const auto& f : kFields)
        if (f.id == id) return &f;
    return nullptr;
}

inline const Field* FindByKey(std::string_view key)
{
    for (const auto& f : kFields)
        if (key == f.key) return &f;
    return nullptr;
}

// Id range owned by each category (§3.2).
inline bool IdInCategoryRange(uint16_t id, Category c)
{
    switch (c) {
    case Category::Modules: return id >= 1 && id <= 99;
    case Category::Hud: return id >= 100 && id <= 299;
    case Category::Theme: return id >= 300 && id <= 399;
    case Category::Lite: return id >= 400 && id <= 499;
    }
    return false;
}

// Clamp into [min, max] and snap to the field's step. Returns true if the value changed.
inline bool ClampValue(const Field& f, int32_t& v)
{
    int32_t out = v < f.min ? f.min : (v > f.max ? f.max : v);
    if (f.step > 1) {
        const int32_t k = (out - f.min + f.step / 2) / f.step;
        out = f.min + k * f.step;
        if (out > f.max) out -= f.step;
    }
    const bool changed = out != v;
    v = out;
    return changed;
}

} // namespace ConfigShare

#pragma once
// "Lite" module: writes a tiny generated resource pack ("OriginX Lite") that hides clouds, sun/moon,
// fog, rain/snow and entity shadows, and can silence particle effects (critical hits, potion swirls, or
// every particle) by redefining them to emit nothing. Plain file handling only, so it is unit-tested. Stacked above the user's own pack.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace LitePack {

namespace fs = std::filesystem;

inline const char* kPackUuid = "0f1e7a3c-5d2b-4c8e-9a61-4f0b7c2d9e11";
inline const char* kModuleUuid = "6b9d1c42-83a7-4e55-b0f3-2a7e8c4d1f22";

struct Options {
    bool clouds = true, sky = true, fog = true, weather = true;
    bool fastUi = false, leftHand = false;
    bool noCrit = false, noPotion = false;   // No Critical Hit Effect / No Potion Effect
    bool noParticles = false, noShadows = false;   // No particles (every one the game defines) / No shadows
    // Particle identifiers to silence for those (found in the game's own pack, see MatchParticles).
    std::vector<std::string> critParticles, potionParticles, allParticles;
    // Hit Effect (enchanted weapons): the particle the DLL spawns, "" = not written. See EnchantedHitParticle.
    std::string enchantedHit;
};


// Particle groups a switch silences: identifiers containing one of these.
inline const std::vector<std::string>& CritKeys() { static const std::vector<std::string> k{"critical_hit", "crit_"}; return k; }
inline const std::vector<std::string>& PotionKeys() { static const std::vector<std::string> k{"mobspell", "mob_spell"}; return k; }
inline const std::vector<std::string>& AllKeys() { static const std::vector<std::string> k{""}; return k; }   // every identifier
// Used when the game's particle files cannot be read (game folder not known yet).
inline std::vector<std::string> DefaultCritParticles() { return {"minecraft:critical_hit_emitter"}; }
inline std::vector<std::string> DefaultPotionParticles() { return {"minecraft:mobspell_emitter"}; }

// The "identifier" of a particle definition file ("" if none).
inline std::string ParticleIdentifier(const std::string& json)
{
    const size_t key = json.find("\"identifier\"");
    if (key == std::string::npos) return {};
    const size_t open = json.find('"', json.find(':', key) + 1);
    const size_t close = open == std::string::npos ? open : json.find('"', open + 1);
    if (open == std::string::npos || close == std::string::npos) return {};
    return json.substr(open + 1, close - open - 1);
}

// Identifiers among `files` (particle JSON texts) that contain one of `keys` (case-insensitive), sorted.
inline std::vector<std::string> MatchParticles(const std::vector<std::string>& files, const std::vector<std::string>& keys)
{
    std::vector<std::string> out;
    for (const auto& text : files) {
        const std::string id = ParticleIdentifier(text);
        std::string lower = id;
        for (auto& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        for (const auto& key : keys)
            if (!id.empty() && lower.find(key) != std::string::npos) { out.push_back(id); break; }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// A particle definition with the same identifier that emits nothing (it replaces the game's own).
inline std::string SilentParticle(const std::string& id)
{
    return std::string("{\n  \"format_version\": \"1.10.0\",\n  \"particle_effect\": {\n    \"description\": {\n") +
           "      \"identifier\": \"" + id + "\",\n" +
           "      \"basic_render_parameters\": { \"material\": \"particles_alpha\", \"texture\": \"textures/particle/particles\" }\n    },\n" +
           "    \"components\": {\n" +
           "      \"minecraft:emitter_rate_instant\": { \"num_particles\": 0 },\n" +
           "      \"minecraft:emitter_lifetime_once\": { \"active_time\": 0 },\n" +
           "      \"minecraft:emitter_shape_point\": {},\n" +
           "      \"minecraft:particle_lifetime_expression\": { \"max_lifetime\": 0 },\n" +
           "      \"minecraft:particle_appearance_billboard\": { \"size\": [0, 0], \"facing_camera_mode\": \"rotate_xyz\" }\n" +
           "    }\n  }\n}\n";
}

// File name for a silenced particle: the identifier with anything but letters / digits / _ made '_'.
inline std::string ParticleFileName(const std::string& id)
{
    std::string name = "myclient_";
    for (char c : id) name += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    return name + ".json";
}

constexpr const char* kEnchantedHitId = "myclient:enchanted_hit";

// The object text of `key` inside `json` removed (with a following comma), when present.
inline std::string RemoveJsonMember(std::string json, const std::string& key)
{
    const size_t at = json.find("\"" + key + "\"");
    if (at == std::string::npos) return json;
    size_t i = json.find('{', at);
    if (i == std::string::npos) return json;
    int depth = 0;
    for (; i < json.size(); ++i) {
        if (json[i] == '{') ++depth;
        else if (json[i] == '}' && --depth == 0) break;
    }
    if (i >= json.size()) return json;
    size_t end = i + 1;
    while (end < json.size() && (json[end] == ' ' || json[end] == '\n' || json[end] == '\r' || json[end] == '\t')) ++end;
    if (end < json.size() && json[end] == ',') ++end;
    return json.erase(at, end - at);
}

// Java-style "enchanted hit" sparks: the game's own critical-hit particle (`vanillaCrit`, its JSON),
// renamed to kEnchantedHitId and tinted blue. "" when the source has no identifier or components.
inline std::string EnchantedHitParticle(const std::string& vanillaCrit)
{
    const std::string id = ParticleIdentifier(vanillaCrit);
    if (id.empty()) return {};
    std::string json = vanillaCrit;
    const size_t idAt = json.find("\"" + id + "\"");
    json.replace(idAt, id.size() + 2, std::string("\"") + kEnchantedHitId + "\"");
    json = RemoveJsonMember(json, "minecraft:particle_appearance_tinting");
    const size_t components = json.find("\"components\"");
    const size_t open = components == std::string::npos ? components : json.find('{', components);
    if (open == std::string::npos) return {};
    json.insert(open + 1, "\n      \"minecraft:particle_appearance_tinting\": { \"color\": [0.4, 0.75, 1.0, 1.0] },");
    return json;
}

inline uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0)
{
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline void Put32(std::string& s, uint32_t v)
{
    for (int i = 3; i >= 0; --i) s += static_cast<char>((v >> (i * 8)) & 0xFF);
}

inline void Chunk(std::string& png, const char* type, const std::string& body)
{
    Put32(png, static_cast<uint32_t>(body.size()));
    const std::string typed = std::string(type, 4) + body;
    png += typed;
    Put32(png, Crc32(reinterpret_cast<const uint8_t*>(typed.data()), typed.size()));
}

// Fully transparent RGBA PNG (uncompressed deflate blocks, so no zlib dependency).
inline std::string TransparentPng(int w, int h)
{
    std::string raw;
    raw.assign(static_cast<size_t>(h) * (1 + static_cast<size_t>(w) * 4), '\0');
    std::string z;
    z += '\x78'; z += '\x01';
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
    while (pos < raw.size() || raw.empty()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z += static_cast<char>(last ? 1 : 0);
        z += static_cast<char>(n & 0xFF); z += static_cast<char>((n >> 8) & 0xFF);
        z += static_cast<char>(~n & 0xFF); z += static_cast<char>((~n >> 8) & 0xFF);
        z.append(raw, pos, n);
        pos += n;
        if (last) break;
    }
    Put32(z, (b << 16) | a);
    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    Put32(ihdr, static_cast<uint32_t>(w)); Put32(ihdr, static_cast<uint32_t>(h));
    ihdr += '\x08'; ihdr += '\x06'; ihdr += '\0'; ihdr += '\0'; ihdr += '\0';
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", "");
    return png;
}

// Pack version [1, 0, patch]: the patch follows the content (1..999999), so every change is a pack the game
// has not seen yet. Under one unchanged version the game kept using what it had already loaded (Left Hand
// stayed mirrored after it was switched off, even after re-joining the world).
inline int ContentVersion(const Options& o)
{
    std::string key;
    for (bool b : {o.clouds, o.sky, o.fog, o.weather, o.fastUi, o.leftHand, o.noCrit, o.noPotion}) key += b ? '1' : '0';
    if (o.noParticles || o.noShadows) key += std::string("|x") + (o.noParticles ? '1' : '0') + (o.noShadows ? '1' : '0');
    if (o.noCrit) for (const auto& id : o.critParticles) key += "|c" + id;
    if (o.noPotion) for (const auto& id : o.potionParticles) key += "|p" + id;
    if (o.noParticles) for (const auto& id : o.allParticles) key += "|a" + id;
    key += "|e" + o.enchantedHit;
    return static_cast<int>(Crc32(reinterpret_cast<const uint8_t*>(key.data()), key.size()) % 999999u) + 1;
}

inline std::string Manifest(int patch = 0)
{
    const std::string version = "[1, 0, " + std::to_string(patch) + "]";
    return std::string("{\n  \"format_version\": 2,\n  \"header\": {\n    \"name\": \"OriginX Lite\",\n"
        "    \"description\": \"Generated by OriginX Client (Lite module)\",\n    \"uuid\": \"") + kPackUuid +
        "\",\n    \"version\": " + version + ",\n    \"min_engine_version\": [1, 16, 0]\n  },\n  \"modules\": [\n    {\n"
        "      \"type\": \"resources\",\n      \"uuid\": \"" + kModuleUuid + "\",\n      \"version\": " + version + "\n    }\n  ]\n}\n";
}

// Everything switched off: what the pack folder is reduced to while no module needs it.
inline Options NothingOn()
{
    Options o;
    o.clouds = o.sky = o.fog = o.weather = false;
    return o;
}

inline std::string FogJson()
{
    return "{\n  \"format_version\": \"1.16.100\",\n  \"minecraft:fog_settings\": {\n    \"description\": {\n"
           "      \"identifier\": \"minecraft:fog_default\"\n    },\n    \"distance\": {\n      \"air\": {\n"
           "        \"fog_start\": 0.999,\n        \"fog_end\": 1.0,\n        \"fog_color\": \"#c0d8ff\",\n"
           "        \"render_distance_type\": \"render\"\n      }\n    }\n  }\n}\n";
}

// Fast Inventory: the vanilla screen transitions (ui/_global_variables.json, 0.25-0.6 s) cut to 0.1 s.
// Only these variables are listed, so the rest of the game's global UI variables stay untouched.
inline const char* kFastUiSeconds = "0.1";
inline std::string FastUiJson()
{
    const std::string t = kFastUiSeconds;
    return "{\n"
           "  \"$transition_time_push_size\": " + t + ",\n"
           "  \"$transition_time_pop_size\": " + t + ",\n"
           "  \"$transition_time_push\": " + t + ",\n"
           "  \"$transition_time_pop\": " + t + ",\n"
           "  \"$container_transition_time_push\": " + t + ",\n"
           "  \"$container_transition_time_pop\": " + t + "\n"
           "}\n";
}

// Left Hand: Mojang's first-person player animations (bedrock-samples, animations/player_firstperson
// .animation.json) mirrored across the body: X positions and Y/Z rotations of the arm and item bones are
// negated, and each arm is moved to the other side (base pose, and the crossbow pose that replaces it).
// Same animation names, so the game uses these instead of the vanilla ones. Only your own first-person
// view changes. This moves the bare arm; held items are placed by the game's own code, and the DLL moves
// those (LeftHand.h, ItemInHandRenderer hook).
inline const char* kLeftHandAnimations = R"MCLH({"format_version":"1.8.0","animations":{"animation.player.first_person.attack_rotation":{"loop":true,"bones":{"rightarm":{"position":["-(math.clamp(-15.5 * math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 112.0), -7.0, 999.0) * math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 112.0))","math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * (1.0 - variable.attack_time) * 200.0) * 7.5 - variable.first_person_item_rotation_factor * variable.attack_time * 15.0 + variable.short_arm_offset_right","math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 120.0) * 1.75"],"rotation":["math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * (1.0 - variable.attack_time) * 280.0) * -60.0","-(math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * (1.0 - variable.attack_time) * 280.0) * 40.0)","-(math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * (1.0 - variable.attack_time) * 280.0) * 20.0)"]}}},"animation.player.first_person.attack_rotation_item":{"loop":true,"override_previous_animation":true,"bones":{"rightitem":{"position":["-(-math.sin(math.sqrt(variable.attack_time) * Math.Pi * 100) * 10)","math.sin(math.sqrt(variable.attack_time) * Math.Pi * 2 * 60) * 10","-math.sin(variable.attack_time * Math.Pi * 65) * 2"],"rotation":["-math.sin(math.sqrt(variable.attack_time) * Math.Pi * 20) * 25","-(-math.sin(math.sqrt(variable.attack_time) * 75.0) * 15.0)","-(-math.sin(math.sqrt(variable.attack_time) * 25) * 15)"]}}},"animation.player.first_person.base_pose":{"loop":true,"bones":{"body":{"rotation":["query.target_x_rotation","query.target_y_rotation",0.0]},"head":{"rotation":["query.target_x_rotation","query.target_y_rotation + 180",0.0]},"rightarm":{"position":[10.0,0.0,0.0]},"leftarm":{"position":[-10.0,0.0,0.0]}}},"animation.player.first_person.crossbow_equipped":{"loop":true,"override_previous_animation":true,"bones":{"leftarm":{"position":["(-(1.5 - variable.item_use_normalized * 3.5)) + -10.0","-3.799999952316284 + variable.short_arm_offset_left","8.25 - (1 - variable.item_use_normalized)"],"rotation":[165.0,60.0,-45.0],"scale":[0.4,0.4,0.4]},"rightitem":{"position":[0.0,2.0,"2.5 - query.item_remaining_use_duration('main_hand', 1.0) * 1.5"],"rotation":[-20.0,15.0,30.0],"scale":[1.0,"1.15 - query.item_remaining_use_duration('main_hand', 1.0) * 0.15",1.0]}}},"animation.player.first_person.crossbow_hold":{"loop":true,"bones":{"rightitem":{"position":["-(query.get_equipped_item_name('off_hand') == 'shield' ? -4.0 : 0.0)",0.0,0.0]}}},"animation.player.first_person.melee_spear_hold":{"loop":true,"override_previous_animation":false,"bones":{"rightitem":{"position":[1.5,2.0,3.5],"rotation":[45.0,-55.0,12.5]}}},"animation.player.first_person.melee_spear_use":{"loop":true,"override_previous_animation":false,"bones":{"rightitem":{"position":["-(-1.5 + v.fp_melee_spear_use_item_position_x)","2.0 + v.fp_melee_spear_use_item_position_y","3.5 + v.fp_melee_spear_use_item_position_z"],"rotation":[45.0,"-(55.0 + v.fp_melee_spear_use_item_rotation_y)","-(-12.5 + v.fp_melee_spear_use_item_rotation_z)"]}}},"animation.player.first_person.melee_spear_attack":{"loop":true,"override_previous_animation":false,"bones":{"rightitem":{"position":["-(-1.5 + v.fp_melee_spear_attack_item_position_x)","2.0 + v.fp_melee_spear_attack_item_position_y","3.5 + v.fp_melee_spear_attack_item_position_z"],"rotation":[45.0,"-(55.0 + v.fp_melee_spear_attack_item_rotation_y)","-(-12.5 + v.fp_melee_spear_attack_item_rotation_z)"]}}},"animation.player.first_person.breathing_bob":{"loop":true,"override_previous_animation":false,"bones":{"rightitem":{"position":[0.0,"variable.bob_animation * math.sin(q.life_time * 45.0) * 0.5",0.0]}}},"animation.player.first_person.empty_hand":{"loop":true,"bones":{"rightarm":{"position":[-13.5,-10.0,12.0],"rotation":["95.0 + variable.is_using_vr * 7.5","-(-45.0 + variable.is_using_vr * 7.5)","-(115.0 + variable.is_using_vr * -2.5)"]},"rightitem":{"position":[0.0,"q.get_default_bone_pivot('rightarm',1) - q.get_default_bone_pivot('rightitem',1) - 7.0","-q.get_default_bone_pivot('rightitem',2)"]},"leftitem":{"position":[0.0,"q.get_default_bone_pivot('leftarm',1) - q.get_default_bone_pivot('leftitem',1) - 7.0","-q.get_default_bone_pivot('leftitem',2)"]}}},"animation.player.first_person.map_hold":{"loop":true,"bones":{"leftarm":{"position":["-(-16.250 + variable.is_vertical_splitscreen * 7.0)","-10.75 - variable.map_angle * 8.0 + variable.is_vertical_splitscreen * 0.6 - variable.short_arm_offset_left","9.0 - variable.map_angle * 8.0 + variable.short_arm_offset_left"],"rotation":[40.0,20.0,155.0],"scale":[1.15,1.15,1.15]},"rightarm":{"position":["-(12.50 + variable.is_vertical_splitscreen * 1.75)","-7.5 - variable.map_angle * 8.0 + variable.is_vertical_splitscreen * 0.5 - variable.short_arm_offset_right","5.25 - variable.map_angle * 8.0 + variable.short_arm_offset_right"],"rotation":[77.5,-7.5,-160.0]}}},"animation.player.first_person.map_hold_attack":{"loop":true,"bones":{"leftarm":{"position":["-(math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 112.0) * -10.75)","math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * (1.0 - variable.attack_time) * 200.0) * 3.75 - variable.first_person_item_rotation_factor * variable.attack_time * 1.25 + variable.short_arm_offset_left","math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 120.0) * 5.75"],"rotation":["variable.map_angle * 90.0","-(-15.0 * math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * -100.0))",0.0]},"rightarm":{"position":["-(math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 112.0) * -6.25)","math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * (1.0 - variable.attack_time) * 200.0) * 1.75 + variable.short_arm_offset_right","math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 120.0) * 5.25"],"rotation":["variable.map_angle * 90.0",0.0,0.0]}}},"animation.player.first_person.map_hold_main_hand":{"loop":true,"bones":{"rightarm":{"position":["-(14.50 - variable.is_vertical_splitscreen * 0.75)","-8.25 + variable.short_arm_offset_right + math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * (1.0 - variable.attack_time) * 200.0) * 2.75 - variable.first_person_item_rotation_factor * variable.attack_time * 3.0 - variable.is_horizontal_splitscreen","11.5 + math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 120.0) * 3.5 + variable.is_horizontal_splitscreen * 3.5"],"rotation":[195.0,-182.5,5.0],"scale":[0.75,0.75,0.75]}}},"animation.player.first_person.map_hold_off_hand":{"loop":true,"bones":{"leftarm":{"position":["-(-14.50 + variable.is_horizontal_splitscreen * 2.0 + variable.is_vertical_splitscreen * 8.7)","-8.250 + variable.short_arm_offset_left","11.50 + variable.is_horizontal_splitscreen * 0.5"],"rotation":[195.0,-182.5,-2.5],"scale":[0.75,0.75,0.75]}}},"animation.player.first_person.swap_item":{"loop":true,"bones":{"leftarm":{"position":[0.0,"(query.get_equipped_item_name('off_hand') == 'filled_map' || query.get_equipped_item_name('off_hand') == 'shield') ? 0.0 : -10.0 * (1.0 - context.player_offhand_arm_height)",0.0]},"rightarm":{"position":[0.0,"-10.0 * (1.0 - variable.player_arm_height)",0.0]}}},"animation.player.first_person.shield_block":{"loop":true,"bones":{"leftarm":{"position":[0.0,"-10.0 * (1.0 - context.player_offhand_arm_height)",0.0]},"rightarm":{"position":[0.0,"query.get_equipped_item_name('off_hand') != 'shield' ? -10.0 * (1.0 - variable.player_arm_height) : 0.0",0.0]}}},"animation.player.first_person.vr_attack_rotation":{"loop":true,"bones":{"rightarm":{"position":["-(5.0 * math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 112.0))","(math.sin(variable.first_person_item_rotation_factor * (1.0 - variable.attack_time) * (1.0 - variable.attack_time) * 200.0) - 0.8) * 8.75 + 5.0","math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 120.0) * 15.0"],"rotation":["30.7 * math.sin(variable.first_person_item_rotation_factor * variable.attack_time * -180.0 - 45.0) * 1.5",0.0,"-(21.8 * math.sin(variable.first_person_item_rotation_factor * variable.attack_time * 200.0 + 30.0) * 1.25)"]}}},"animation.player.first_person.walk":{"loop":true,"bones":{"leftarm":{"position":["-(math.sin(-query.walk_distance * 180.0) * variable.hand_bob * 9.75)","-math.abs(math.cos(-query.walk_distance * 180.0)) * variable.hand_bob * 15.0 + variable.short_arm_offset_left",0.0]},"rightarm":{"position":["-(math.sin(-query.walk_distance * 180.0) * variable.hand_bob * 9.75)","-math.abs(math.cos(-query.walk_distance * 180.0)) * variable.hand_bob * 15.0 + variable.short_arm_offset_right",0.0]}}}}})MCLH";

inline bool WriteFile(const fs::path& path, const std::string& data)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    return static_cast<bool>(out);
}

inline void Remove(const fs::path& path)
{
    std::error_code ec;
    fs::remove(path, ec);
}

// Rewrites the pack folder to match the options. Returns false if a file could not be written.
inline bool Write(const fs::path& folder, const Options& o)
{
    bool ok = WriteFile(folder / "manifest.json", Manifest(ContentVersion(o)));
    const fs::path env = folder / "textures" / "environment";
    auto toggle = [&](bool on, const fs::path& p, int w, int h) {
        if (on) ok = WriteFile(p, TransparentPng(w, h)) && ok; else Remove(p);
    };
    toggle(o.clouds, env / "clouds.png", 256, 256);
    toggle(o.sky, env / "sun.png", 32, 32);
    toggle(o.sky, env / "moon_phases.png", 128, 64);
    toggle(o.weather, env / "weather" / "rain.png", 64, 64);
    toggle(o.weather, env / "weather" / "snow.png", 64, 64);
    toggle(o.noShadows, folder / "textures" / "misc" / "shadow.png", 32, 32);
    const fs::path fog = folder / "fogs" / "fog_default.json";
    if (o.fog) ok = WriteFile(fog, FogJson()) && ok; else Remove(fog);
    const fs::path hand = folder / "animations" / "player_firstperson.animation.json";
    if (o.leftHand) ok = WriteFile(hand, kLeftHandAnimations) && ok; else Remove(hand);
    const fs::path ui = folder / "ui" / "_global_variables.json";
    if (o.fastUi) ok = WriteFile(ui, FastUiJson()) && ok; else Remove(ui);
    // Silenced particles: the folder holds only what is switched on now.
    std::error_code ec;
    fs::remove_all(folder / "particles", ec);
    std::vector<std::string> silent;
    if (o.noCrit) silent.insert(silent.end(), o.critParticles.begin(), o.critParticles.end());
    if (o.noPotion) silent.insert(silent.end(), o.potionParticles.begin(), o.potionParticles.end());
    if (o.noParticles) silent.insert(silent.end(), o.allParticles.begin(), o.allParticles.end());
    std::sort(silent.begin(), silent.end());
    silent.erase(std::unique(silent.begin(), silent.end()), silent.end());
    for (const auto& id : silent)
        ok = WriteFile(folder / "particles" / ParticleFileName(id), SilentParticle(id)) && ok;
    if (!o.enchantedHit.empty())
        ok = WriteFile(folder / "particles" / ParticleFileName(kEnchantedHitId), o.enchantedHit) && ok;
    return ok;
}

}  // namespace LitePack

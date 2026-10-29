#include "../LitePack.h"
#include <cassert>
#include <iostream>
int main()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "lite_test";
    fs::remove_all(dir);
    LitePack::Options o;
    assert(LitePack::Write(dir, o));
    assert(fs::exists(dir / "manifest.json") && fs::exists(dir / "textures/environment/clouds.png"));
    assert(fs::exists(dir / "fogs/fog_default.json") && fs::exists(dir / "textures/environment/weather/rain.png"));
    const std::string png = LitePack::TransparentPng(16, 16);
    assert(png.compare(1, 3, "PNG") == 0);
    // CRC check value
    assert(LitePack::Crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u);
    o.clouds = false; o.fog = false;
    assert(LitePack::Write(dir, o));
    assert(!fs::exists(dir / "textures/environment/clouds.png") && !fs::exists(dir / "fogs/fog_default.json"));
    assert(fs::exists(dir / "textures/environment/sun.png"));
    // Fast Inventory: only the transition variables, 0.1 s, and removed again when switched off.
    assert(!fs::exists(dir / "ui/_global_variables.json"));
    o.fastUi = true;
    assert(LitePack::Write(dir, o));
    assert(fs::exists(dir / "ui/_global_variables.json"));
    const std::string ui = LitePack::FastUiJson();
    assert(ui.find("\"$container_transition_time_push\": 0.1") != std::string::npos);
    assert(ui.find("\"$transition_time_push_size\": 0.1") != std::string::npos);
    o.fastUi = false;
    assert(LitePack::Write(dir, o));
    assert(!fs::exists(dir / "ui/_global_variables.json"));
    // Left Hand: the mirrored first-person animations, valid JSON-ish and removed again when off.
    o.leftHand = true;
    assert(LitePack::Write(dir, o));
    assert(fs::exists(dir / "animations/player_firstperson.animation.json"));
    const std::string hand = LitePack::kLeftHandAnimations;
    assert(hand.front() == '{' && hand.back() == '}' && hand.find("animation.player.first_person.empty_hand") != std::string::npos);
    o.leftHand = false;
    assert(LitePack::Write(dir, o));
    assert(!fs::exists(dir / "animations/player_firstperson.animation.json"));
    // Pack version follows the content: switching a part on or off is a version the game has not loaded yet.
    {
        LitePack::Options a = LitePack::NothingOn(), b = a;
        b.leftHand = true;
        const int va = LitePack::ContentVersion(a), vb = LitePack::ContentVersion(b);
        assert(va != vb && va >= 1 && va <= 999999 && vb >= 1 && vb <= 999999);
        assert(LitePack::ContentVersion(b) == vb);
        assert(LitePack::Write(dir, b));
        std::ifstream in(dir / "manifest.json");
        const std::string m((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(m.find("\"version\": [1, 0, " + std::to_string(vb) + "]") != std::string::npos);
        assert(LitePack::Write(dir, LitePack::NothingOn()));
        assert(!fs::exists(dir / "animations/player_firstperson.animation.json") && !fs::exists(dir / "textures/environment/clouds.png"));
    }
    // Particles: identifiers found by keyword, silenced only while switched on
    {
        const std::vector<std::string> files = {
            R"({"format_version":"1.10.0","particle_effect":{"description":{"identifier":"minecraft:critical_hit_emitter","basic_render_parameters":{}}}})",
            R"({"particle_effect":{"description":{"identifier" : "minecraft:mobspell_emitter"}}})",
            R"({"particle_effect":{"description":{"identifier":"minecraft:evoker_spell"}}})",
            R"({"particle_effect":{"description":{"identifier":"minecraft:basic_crit_particle"}}})",
            "not json"};
        const auto crit = LitePack::MatchParticles(files, LitePack::CritKeys());
        const auto potion = LitePack::MatchParticles(files, LitePack::PotionKeys());
        assert(crit.size() == 2 && crit[0] == "minecraft:basic_crit_particle" && crit[1] == "minecraft:critical_hit_emitter");
        assert(potion.size() == 1 && potion[0] == "minecraft:mobspell_emitter");   // evoker_spell is not a potion
        assert(LitePack::ParticleIdentifier(LitePack::SilentParticle("minecraft:x")) == "minecraft:x");
        assert(LitePack::SilentParticle("a:b").find("\"num_particles\": 0") != std::string::npos);
        assert(LitePack::ParticleFileName("minecraft:critical_hit_emitter") == "myclient_minecraft_critical_hit_emitter.json");
        LitePack::Options p;
        p.critParticles = crit; p.potionParticles = potion;
        p.noCrit = true;
        assert(LitePack::Write(dir, p));
        assert(fs::exists(dir / "particles/myclient_minecraft_critical_hit_emitter.json"));
        assert(!fs::exists(dir / "particles/myclient_minecraft_mobspell_emitter.json"));
        p.noCrit = false; p.noPotion = true;
        assert(LitePack::Write(dir, p));
        assert(!fs::exists(dir / "particles/myclient_minecraft_critical_hit_emitter.json"));
        assert(fs::exists(dir / "particles/myclient_minecraft_mobspell_emitter.json"));
        p.noPotion = false;
        assert(LitePack::Write(dir, p));
        assert(!fs::exists(dir / "particles"));
        // No particles: every identifier in the game's files, once each (with a crit switch on as well).
        const auto all = LitePack::MatchParticles(files, LitePack::AllKeys());
        assert(all.size() == 4);
        p.allParticles = all; p.noParticles = true; p.noCrit = true;
        const int before = LitePack::ContentVersion(LitePack::Options{});
        assert(LitePack::ContentVersion(p) != before);
        assert(LitePack::Write(dir, p));
        size_t written = 0;
        for (const auto& e : fs::directory_iterator(dir / "particles")) { (void)e; ++written; }
        assert(written == 4 && fs::exists(dir / "particles/myclient_minecraft_evoker_spell.json"));
        p.noParticles = false; p.noCrit = false;
        assert(LitePack::Write(dir, p));
        assert(!fs::exists(dir / "particles"));
        // No shadows: a transparent shadow texture, removed again when switched off.
        p.noShadows = true;
        assert(LitePack::Write(dir, p) && fs::exists(dir / "textures/misc/shadow.png"));
        p.noShadows = false;
        assert(LitePack::Write(dir, p) && !fs::exists(dir / "textures/misc/shadow.png"));
    }
    // Hit Effect: the game's crit particle renamed and tinted blue (an existing tint is replaced)
    {
        const std::string crit = R"({"format_version":"1.10.0","particle_effect":{"description":{"identifier":"minecraft:critical_hit_emitter","basic_render_parameters":{"material":"particles_alpha","texture":"textures/particle/particles"}},"components":{"minecraft:emitter_rate_instant":{"num_particles":12},"minecraft:particle_appearance_tinting":{"color":[1,1,1,1]},"minecraft:particle_appearance_billboard":{"size":[0.1,0.1]}}}})";
        const std::string magic = LitePack::EnchantedHitParticle(crit);
        assert(LitePack::ParticleIdentifier(magic) == LitePack::kEnchantedHitId);
        assert(magic.find("[0.4, 0.75, 1.0, 1.0]") != std::string::npos && magic.find("[1,1,1,1]") == std::string::npos);
        assert(magic.find("\"num_particles\":12") != std::string::npos && magic.find("critical_hit_emitter") == std::string::npos);
        size_t tints = 0;
        for (size_t at = magic.find("particle_appearance_tinting"); at != std::string::npos; at = magic.find("particle_appearance_tinting", at + 1)) ++tints;
        assert(tints == 1);
        assert(LitePack::EnchantedHitParticle("no identifier").empty());
        LitePack::Options h;
        h.enchantedHit = magic;
        assert(LitePack::Write(dir, h));
        assert(fs::exists(dir / "particles/myclient_myclient_enchanted_hit.json"));
        h.enchantedHit.clear();
        assert(LitePack::Write(dir, h));
        assert(!fs::exists(dir / "particles"));
    }
    fs::remove_all(dir);
    std::cout << "lite ok\n";
}

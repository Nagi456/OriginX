// The one place that ties Registry ids to MyClient's settings variables
// (docs/config-sharing-design.md §5.4, task 5). Included by MyClient.cpp after the
// settings, ToggleFeature(), ApplyLite() and RequestRenderDistance() exist.
// UI thread only.
#pragma once

#include "HudFraction.h"
#include "Registry.h"
#include "Snapshot.h"

#include <cmath>

namespace ConfigShare {

// The size HUD x/y are measured in: the overlay window (it covers the whole game window).
// 0 x 0 when it is unusable — no game window, minimized, or an odd size while resizing —
// and the HUD category is then left out (§3.4).
inline void HudBaseSize(int& w, int& h)
{
    w = h = 0;
    if (!::overlayWindow || !::minecraftWindow || !IsWindow(::minecraftWindow) || IsIconic(::minecraftWindow)) return;
    RECT r{};
    if (!GetClientRect(::overlayWindow, &r)) return;
    const int cw = r.right - r.left, ch = r.bottom - r.top;
    if (cw < 160 || ch < 120 || cw > 16384 || ch > 16384) return;
    w = cw;
    h = ch;
}

inline bool HudAvailable()
{
    int w, h;
    HudBaseSize(w, h);
    return w > 0 && h > 0;
}

// Module ids (§3.3) -> the feature ToggleFeature() switches. 16 (Hive Utils' own switch) is
// written straight to hiveEnabled, like ApplyPreset() does.
inline DetailFeature ModuleFeature(uint16_t id)
{
    switch (id) {
    case 1: return DetailFeature::Keystrokes;
    case 2: return DetailFeature::CPS;
    case 3: return DetailFeature::Armor;
    case 4: return DetailFeature::Arrows;
    case 5: return DetailFeature::FPS;
    case 7: return DetailFeature::BlockOutline;
    case 8: return DetailFeature::Zoom;
    case 9: return DetailFeature::Resources;
    case 10: return DetailFeature::Fullbright;
    case 11: return DetailFeature::Nametag;
    case 12: return DetailFeature::Ping;
    case 13: return DetailFeature::Textures;
    case 14: return DetailFeature::FeatureList;
    case 15: return DetailFeature::ShulkerPreview;
    case 17: return DetailFeature::TabList;
    case 18: return DetailFeature::HiveStats;
    case 19: return DetailFeature::ChunkBorder;
    case 20: return DetailFeature::Hitbox;
    case 21: return DetailFeature::Nick;
    case 22: return DetailFeature::Replay;
    case 23: return DetailFeature::MenuSounds;
    case 24: return DetailFeature::Lite;
    case 25: return DetailFeature::FastInventory;
    case 26: return DetailFeature::LeftHand;
    case 27: return DetailFeature::HitEffect;
    case 28: return DetailFeature::FrameGen;
    case 29: return DetailFeature::DayTimer;
    case 30: return DetailFeature::DebugMenu;
    case 31: return DetailFeature::BlockDetails;
    case 32: return DetailFeature::HealthWarning;
    case 33: return DetailFeature::Coordinates;
    case 34: return DetailFeature::Timer;
    case 35: return DetailFeature::CpsLimiter;
    case 36: return DetailFeature::ItemCounter;
    case 37: return DetailFeature::PackDisplay;
    case 38: return DetailFeature::JavaChat;
    case 39: return DetailFeature::Minimap;
    case 40: return DetailFeature::Combo;
    default: return DetailFeature::None;
    }
}

inline std::wstring ModuleName(uint16_t id)
{
    if (id == 16) return L"Hive Utils";
    const DetailFeature f = ModuleFeature(id);
    return f == DetailFeature::None ? std::wstring() : FeatureTitle(f);
}

// Theme (300..314) and Lite (400..425) variables. Bools are read and written as 0 / 1.
inline bool* BoolVar(uint16_t id)
{
    switch (id) {
    case 300: return &::glassDark;
    case 400: return &::liteClouds;
    case 401: return &::liteSky;
    case 402: return &::liteFog;
    case 403: return &::liteWeather;
    case 404: return &::liteNoShadows;
    case 405: return &::liteNoParticles;
    case 406: return &::liteNoCrit;
    case 407: return &::liteNoPotion;
    case 408: return &::liteEntityLimit;
    case 410: return &::liteCullMobs;
    case 411: return &::liteCullItems;
    case 412: return &::liteCullMisc;
    case 413: return &::litePlayerLimit;
    case 415: return &::liteNoNameTags;
    case 416: return &::liteNoSkyFull;
    case 417: return &::liteCullBehind;
    case 418: return &::liteBlockEntityLimit;
    case 420: return &::liteVerticalLimit;
    case 422: return &::liteRenderEnabled;
    case 424: return &::liteFpsLimit;
    default: return nullptr;
    }
}

inline int* IntVar(uint16_t id)
{
    if (id >= 306 && id <= 314) {
        GlassHsv& c = ::glassCustom[(id - 306) / 3];
        const int part = (id - 306) % 3;
        return part == 0 ? &c.h : part == 1 ? &c.s : &c.v;
    }
    switch (id) {
    case 301: return &::glassAccentIndex;
    case 302: return &::glassLightIndex;
    case 303: return &::glassDarkIndex;
    case 304: return &::glassShadow;
    case 305: return &::glassOpacity;
    case 409: return &::liteEntityDistance;
    case 414: return &::litePlayerDistance;
    case 419: return &::liteBlockEntityDistance;
    case 421: return &::liteVerticalRows;
    case 423: return &::liteRenderDistance;
    case 425: return &::liteFpsLimitCode;
    default: return nullptr;
    }
}

// HUD ids: 100 + HudElementId*4 + {0 x, 1 y, 2 scale}.
inline HudElement* HudOf(uint16_t id, int& part)
{
    const int rel = int(id) - 100;
    part = rel % 4;
    return GetHudElement(static_cast<HudElementId>(rel / 4));
}

// Reads the current value of one registry field. False when it has no variable here.
inline bool ReadField(const Field& f, int w, int h, int32_t& out)
{
    switch (f.category) {
    case Category::Modules:
        if (f.id == 16) { out = ::hiveEnabled ? 1 : 0; return true; }
        if (ModuleFeature(f.id) == DetailFeature::None) return false;
        out = FeatureEnabled(ModuleFeature(f.id)) ? 1 : 0;
        return true;
    case Category::Hud: {
        int part;
        HudElement* e = HudOf(f.id, part);
        if (!e) return false;
        if (part == 0) out = ToFraction(e->x, w);
        else if (part == 1) out = ToFraction(e->y, h);
        else out = int32_t(std::lround(e->scale * 100.0f));
        ClampValue(f, out);
        return true;
    }
    case Category::Theme:
    case Category::Lite:
        if (bool* b = BoolVar(f.id)) { out = *b ? 1 : 0; return true; }
        if (int* i = IntVar(f.id)) { out = *i; ClampValue(f, out); return true; }
        return false;
    }
    return false;
}

// Every field of the chosen categories as it is now. HUD is left out while the
// window size is unusable.
inline Snapshot Capture(uint8_t categories)
{
    int w, h;
    HudBaseSize(w, h);
    if (w <= 0 || h <= 0) categories &= uint8_t(~uint8_t(Category::Hud));
    Snapshot s;
    for (const auto& f : kFields) {
        if (!(categories & uint8_t(f.category))) continue;
        int32_t v;
        if (ReadField(f, w, h, v)) s.values.push_back({f.id, v});
    }
    return s;
}

struct ApplyReport {
    int changed = 0;          // fields whose value changed
    int applyLiteCalls = 0;   // always 0 or 1 (§5.4 step 3)
    bool renderRequested = false;
};

// §5.4. Values in `s` were checked by DecodeCode (or come from Capture), and are clamped again
// here anyway. `undo` (optional) receives the values from before, for the chosen categories.
inline ApplyReport Apply(const Snapshot& s, uint8_t categories, Snapshot* undo)
{
    ApplyReport rep;
    int w, h;
    HudBaseSize(w, h);
    if (w <= 0 || h <= 0) categories &= uint8_t(~uint8_t(Category::Hud));
    if (undo) *undo = Capture(categories);

    bool liteFieldChanged = false, renderChanged = false;
    ::applyingPreset = true;     // no toast / sound per module
    ::deferLiteApply = true;     // ToggleFeature only marks liteApplyPending
    ::liteApplyPending = false;
    for (auto [id, value] : s.values) {
        const Field* f = FindById(id);
        if (!f || !(categories & uint8_t(f->category))) continue;
        ClampValue(*f, value);
        int32_t cur;
        if (!ReadField(*f, w, h, cur)) continue;
        if (f->category == Category::Hud) {
            int part;
            HudElement* e = HudOf(id, part);
            if (part == 0) { const int px = FromFraction(value, w); if (px != e->x) { e->x = px; ++rep.changed; } }
            else if (part == 1) { const int px = FromFraction(value, h); if (px != e->y) { e->y = px; ++rep.changed; } }
            else if (value != cur) { e->scale = float(value) / 100.0f; ++rep.changed; }
            continue;
        }
        if (value == cur) continue;
        ++rep.changed;
        if (f->category == Category::Modules) {
            if (id == 16) ::hiveEnabled = value != 0;
            else ToggleFeature(ModuleFeature(id));
            continue;
        }
        if (bool* b = BoolVar(id)) *b = value != 0;
        else if (int* i = IntVar(id)) *i = value;
        if (f->effects & EffectLite) liteFieldChanged = true;
        if (f->effects & EffectRenderDistance) renderChanged = true;
    }
    ::deferLiteApply = false;
    ::applyingPreset = false;

    if (::liteApplyPending || (liteFieldChanged && ::liteEnabled)) {
        ApplyLite();   // a failure keeps the settings; ApplyLite reports it as usual (liteStatus)
        rep.applyLiteCalls = 1;
    }
    ::liteApplyPending = false;
    if (renderChanged && ::liteEnabled) {
        RequestRenderDistance();
        rep.renderRequested = true;
    }
    SaveConfig();
    return rep;
}

} // namespace ConfigShare

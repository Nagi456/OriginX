#include "FeatureData.h"
#include <cstddef>
#include <cassert>
#include <iostream>
#include <limits>
using namespace MyClientFeatures;
int main() {
    {   // Health Warning: health rides in clockReserved; 0 (older DLLs) is "unknown", 0 health is known
        int points = -1;
        assert(!MyClientFeatures::DecodeHealth(0, points));
        assert(MyClientFeatures::DecodeHealth(MyClientFeatures::EncodeHealth(0), points) && points == 0);
        assert(MyClientFeatures::DecodeHealth(MyClientFeatures::EncodeHealth(7), points) && points == 7);
        assert(MyClientFeatures::DecodeHealth(MyClientFeatures::EncodeHealth(-3), points) && points == 0);
    }
    Command request{1000, 1, 'C', 0, 0};
    assert(WantsZoom(request, 1100, true, true, true));
    assert(!WantsZoom(request, 1251, true, true, true));
    assert(!WantsZoom(request, 999, true, true, true));
    assert(!WantsZoom(request, 1100, false, true, true));
    assert(!WantsZoom(request, 1100, true, false, true));
    assert(!WantsZoom(request, 1100, true, true, false));
    request.zoom = 0; assert(!WantsZoom(request, 1100, true, true, true));
    request.zoom = 2; assert(!WantsZoom(request, 1100, true, true, true));
    request.zoom = 1;
    for (uint32_t key : {0u, 255u, 0xffffffffu, 27u, unsigned('G')}) {
        request.key = key; assert(!WantsZoom(request, 1100, true, true, true));
    }
    assert(ZoomFov(90, false) == 90);
    assert(std::abs(ZoomFov(90, true) - 36.8699f) < .001f);
    assert(ZoomFov(70, true) < 70);
    assert(ZoomFactor(0) == 3.f && ZoomFactor(500) == 5.f && ZoomFactor(99999) == 3.f);
    // Entity render distance lives in bits 23..28 and leaves the zoom/hitbox/nick/replay bits alone.
    assert(EncodeEntityDistance(0, true) == 0 && EntityDistanceBlocks(0) == 0 && !EntityDistancePlayers(0));
    assert(EntityDistanceBlocks(EncodeEntityDistance(64, false)) == 64 && !EntityDistancePlayers(EncodeEntityDistance(64, false)));
    assert(EntityDistanceBlocks(EncodeEntityDistance(128, true)) == 128 && EntityDistancePlayers(EncodeEntityDistance(128, true)));
    assert(EntityDistanceBlocks(EncodeEntityDistance(500, false)) == 128 && EntityDistanceBlocks(EncodeEntityDistance(13, false)) == 8);
    assert((EncodeEntityDistance(128, true) & 0x7FFFFFu) == 0 && EntityDistanceBlocks(0x7FFFFFu) == 0);
    assert(ZoomFactor((EncodeEntityDistance(64, true) | 500u) & 0xFFFFu) == 5.f);
    // Render Distance rides in the upper bytes of the Auto RQ field; the low byte stays the mode.
    assert(AutoRqModeOf(EncodeAutoRqField(6, 12, 3)) == 6 && RenderChunksOf(EncodeAutoRqField(6, 12, 3)) == 12);
    assert(RenderCounterOf(EncodeAutoRqField(6, 12, 3)) == 3 && RenderCounterOf(EncodeAutoRqField(0, 12, 256)) == 0);
    assert(RenderChunksOf(EncodeAutoRqField(1, 0, 9)) == 0 && RenderChunksOf(EncodeAutoRqField(1, 2, 9)) == 4);
    assert(RenderChunksOf(EncodeAutoRqField(1, 200, 9)) == 48 && EncodeAutoRqField(3, 0, 0) == 3u);
    assert(RenderDistanceOption(12) == "gfx_viewdistance:192");
    assert(ZoomFov(90, true, 6.f) < ZoomFov(90, true, 3.f));
    assert(ZoomFov(0, true) == 0 && ZoomFov(200, true) == 200);
    assert(std::isnan(ZoomFov(std::numeric_limits<float>::quiet_NaN(), true)));
    Command bright{1000, 0, 'C', 1, 0};
    assert(WantsFullbright(bright, 1100));
    assert(!WantsFullbright(bright, 1251) && !WantsFullbright(bright, 999));  // lease expiry / future stamp
    bright.fullbright = 0; assert(!WantsFullbright(bright, 1100));
    bright.fullbright = 2; assert(!WantsFullbright(bright, 1100));
    assert(FullbrightScale(0.f, false) == 0.f && FullbrightScale(0.f, true) == 1.f);
    assert(FullbrightScale(0.4f, true) == 1.f && FullbrightScale(1.5f, true) == 1.5f);  // never lowers
    assert(FullbrightScale(0.4f, false) == 0.4f);
    assert(FullbrightScale(std::numeric_limits<float>::quiet_NaN(), true) == 1.f);
    Command tag{1000, 0, 0, 0, 1};
    assert(WantsNametag(tag, 1100) && !WantsNametag(tag, 1251) && !WantsNametag(tag, 999));  // lease expiry / future stamp
    tag.nametag = 0; assert(!WantsNametag(tag, 1100));
    tag.nametag = 2; assert(!WantsNametag(tag, 1100));
    assert(sizeof(Command) == 48 && offsetof(Command, autoRq) == 40 && offsetof(Command, nametag) == 20 && offsetof(Command, outline) == 24 &&
           offsetof(Command, outlineRgb) == 28 && offsetof(Command, texReload) == 32 && offsetof(Command, texturesOn) == 36);
    {   // texture reload requests: edge-triggered, primed by the first live command, ignore stale / switched-off commands
        Command c{5000, 0, 0, 0, 0, 0, 0, 7, 1};
        uint32_t seen = 0; bool primed = false;
        assert(!TextureReloadRequested(c, 5100, seen, primed) && primed && seen == 7);   // first sight: no reload
        assert(!TextureReloadRequested(c, 5100, seen, primed));                           // unchanged
        c.texReload = 8;
        assert(TextureReloadRequested(c, 5100, seen, primed) && seen == 8);               // one change = one request
        assert(!TextureReloadRequested(c, 5100, seen, primed));                           // not repeated
        c.texReload = 9; assert(!TextureReloadRequested(c, 5400, seen, primed) && seen == 8);   // stale lease: ignored, not consumed
        assert(TextureReloadRequested(c, 5100, seen, primed) && seen == 9);
        c.texReload = 10; c.texturesOn = 0; assert(!TextureReloadRequested(c, 5100, seen, primed));   // feature off: ignored
        c.texturesOn = 1; assert(TextureReloadRequested(c, 5100, seen, primed));
        c.texReload = 0; assert(TextureReloadRequested(c, 5100, seen, primed));           // HUD restarted its counter: still a change
    }
    Command outline{2000, 0, 0, 0, 0, 1, 0x50B4FF};
    assert(WantsOutline(outline, 2100) && !WantsOutline(outline, 2251) && !WantsOutline(outline, 1999));
    outline.outline = 0; assert(!WantsOutline(outline, 2100));
    assert(FlagOutline == 4 && FlagOutlineColored == 8);
    uint32_t outlineBits = WithFlag(WithFlag(0u, FlagOutline, true), FlagOutlineColored, true);
    assert(outlineBits == 12 && HasFlag(outlineBits, FlagOutline) && !HasFlag(outlineBits, FlagFullbright));
    uint32_t bits = 0;
    bits = WithFlag(bits, FlagNametag, true);
    assert(HasFlag(bits, FlagNametag) && !HasFlag(bits, FlagFullbright));
    bits = WithFlag(bits, FlagFullbright, true);
    assert(bits == 3 && HasFlag(bits, FlagFullbright) && HasFlag(bits, FlagNametag));
    bits = WithFlag(bits, FlagNametag, false);
    assert(bits == FlagFullbright);                       // an older DLL only ever sets bit 0
    assert(HasFlag(1u, FlagFullbright) && !HasFlag(1u, FlagNametag));
    assert(MakeArmor(false, 0, 0).state == Empty);
    assert(MakeArmor(true, 0, 0).state == NoDurability);
    auto armor = MakeArmor(true, 363, 120);
    assert(armor.state == Durable && armor.remaining == 243 && ValidArmor(armor));
    assert(MakeArmor(true, 363, 363).remaining == 0);
    assert(MakeArmor(true, 363, 364).state == Unknown);
    assert(MakeArmor(true, 363, -1).state == Unknown);
    assert(!ValidArmor({Durable, 500, 100}) && !ValidArmor({Empty, 1, 0}));
    assert(!Fresh(0, 100) && !Fresh(101, 100) && !Fresh(100, 2101) && Fresh(100, 2100));
    FrameRate fps; float value = 0; uint64_t at = 0;
    assert(!fps.Read(1000, value, at));
    for (uint64_t i = 0; i <= 60; ++i) fps.Frame(1000 + i * 1000 / 60);
    assert(fps.Read(2000, value, at) && std::abs(value - 60) < .01 && at == 2000);
    assert(!fps.Read(4001, value, at));
    fps.Frame(5000); assert(!fps.Read(5000, value, at));
    for (uint64_t i = 1; i <= 30; ++i) fps.Frame(5000 + i * 1000 / 30);
    assert(fps.Read(6000, value, at) && std::abs(value - 30) < .01);
    fps.Frame(100); assert(!fps.Read(100, value, at));
    fps.Reset(); assert(!fps.Read(100, value, at));
    Shared shared{}; shared.magic=Magic; shared.version=Version; shared.size=sizeof shared; shared.pid=123;
    assert(Valid(shared, 123) && !Valid(shared, 124));
    shared.version++; assert(!Valid(shared, 123));
    // HUD menu flag: its own bit of the autoRq field, untouched by Auto RQ / Render Distance / Left Hand.
    const uint32_t field = EncodeAutoRqField(10, 48, 255) | LeftHandBit;
    assert(!HudMenuOf(field) && HudMenuOf(field | HudMenuBit) && LeftHandOf(field | HudMenuBit));
    assert(!ShulkerHoverOf(field | HudMenuBit) && ShulkerHoverOf(field | ShulkerHoverBit) && !HudMenuOf(field | ShulkerHoverBit) && AutoRqModeOf(field | ShulkerHoverBit) == 10 && RenderCounterOf(field | ShulkerHoverBit) == 255);
    assert(AutoRqModeOf(field | HudMenuBit) == 10 && RenderChunksOf(field | HudMenuBit) == 48 && RenderCounterOf(field | HudMenuBit) == 255);
    // Optimization extras: kinds of actor, name tags and the players' own distance, each in its own bits.
    const uint32_t opt = EncodeOptimize(true, false, true, true, 64);
    assert((opt & CullMobsBit) && !(opt & CullItemsBit) && (opt & CullMiscBit) && (opt & HideNameTagsBit) && PlayerDistanceBlocks(opt) == 64);
    assert(PlayerDistanceBlocks(EncodeOptimize(false, false, false, false, 0)) == 0 && EncodeOptimize(false, false, false, false, 0) == 0);
    assert(PlayerDistanceBlocks(EncodeOptimize(false, false, false, false, 1000)) == EntityDistanceMax);
    assert((GameScreenKnown & GameMenuOpen) == 0 && (GameMenuOpen & GamePaused) == 0 && (GameScreenKnown & GamePaused) == 0);
    assert(((GameScreenKnown | GameMenuOpen | GamePaused) & (GameWorldKnown | GameInWorld)) == 0 && (GameWorldKnown & GameInWorld) == 0);
    assert(((GameScreenKnown | GameMenuOpen | GamePaused | GameWorldKnown | GameInWorld) & (GameTitleKnown | GameOnTitle)) == 0 && (GameTitleKnown & GameOnTitle) == 0);
    // Day Timer: night at 13000, morning at 24000 = 0; rounded up to whole seconds, any day number.
    assert(SecondsUntilNight(0) == 650 && SecondsUntilMorning(0) == 1200 && !IsNight(0));
    assert(SecondsUntilNight(12999) == 1 && IsNight(13000) && SecondsUntilMorning(13000) == 550);
    assert(SecondsUntilNight(13000) == 1200 && SecondsUntilMorning(23999) == 1 && SecondsUntilMorning(24000 * 5 + 1) == 1200);
    assert(TimeOfDay(-1) == 23999 && SecondsUntilMorning(-20) == 1);
    std::cout << "PASS: zoom lease/key/focus/menu release, fullbright lease/scale, FOV scaling, armor durability states, render FPS timing and staleness, extension ABI identity.\n";
}

#pragma once
#include <cstdint>
#include <string>
#include <cmath>
#include <algorithm>
#include <type_traits>

// Optional extension to bridge ABI 2. Older HUD/DLL pairs continue exchanging
// arrow/target data; feature commands and snapshots use a separate v1 mapping.
namespace MyClientFeatures {
constexpr uint32_t Magic = 0x4d434631;
constexpr uint32_t Version = 16;   // 10: armorItems (Armor HUD icons); 11: presentedFps / frameGen (Frame Generation); 12: optimize; 13: clock; 14: debug; 15: hitboxRgb; 16: look
constexpr uint64_t StaleMs = 2000, CommandLeaseMs = 250;
enum ArmorState : uint32_t { Unknown, Empty, NoDurability, Durable };
struct Armor { uint32_t state, remaining, maximum; };
// `nametag` used to be the unused `reserved` field: same size, so older DLLs just ignore it.
// outline / outlineRgb: Block Outline colour override (outlineRgb = 0xRRGGBB). Added with ABI version 3.
// texReload: a counter the HUD bumps each time it has rewritten the active texture-pack list; the DLL
// makes the game re-read the list when it changes (and only while texturesOn == 1). Added with version 5.
// autoRq: Auto RQ mode (0 = off, 1..10 = AutoRq.h kModes). Added with version 6.
struct Command { uint64_t at; uint32_t zoom, key, fullbright, nametag, outline, outlineRgb, texReload, texturesOn, autoRq, reserved; };
// fullbrightAvailable / fullbrightActive carry one bit per light/name feature so the layout
// (and the ABI version) stay the same; an older DLL only ever sets bit 0.
constexpr uint32_t FlagFullbright = 1, FlagNametag = 2, FlagOutline = 4, FlagOutlineColored = 8, FlagTextures = 16;
// Available: bit FlagOutline = the outline hook is installed.
// Active:    FlagOutline = the game ran the outline code recently, FlagOutlineColored = a colour call was overridden.
inline bool HasFlag(uint32_t value, uint32_t flag) { return (value & flag) != 0; }
inline uint32_t WithFlag(uint32_t value, uint32_t flag, bool on) { return on ? (value | flag) : (value & ~flag); }
struct Telemetry {
    uint64_t fpsAt, armorAt, zoomAt, fullbrightAt;
    float fps;
    uint32_t zoomActive;
    uint32_t fullbrightActive;    // bit 1 (FlagFullbright): game is using the boosted value; bit 2 (FlagNametag): own name tag forced.
    uint32_t fullbrightAvailable; // bit 1 / bit 2: that game hook was resolved and installed.
    Armor armor[4]; // Head, torso, legs, feet, in SDK slot order.
    // Local camera, written every rendered frame (Chunk Border). Eye position, yaw/pitch in degrees, vertical FOV.
    uint64_t camAt;
    float camX, camY, camZ, camYaw, camPitch, camFov;
    // Replay recorder status (the DLL refreshes it about every second while recording).
    uint64_t recAt, recBytes;
    uint32_t recPackets, recSeconds;
};
// Connected server, published by the DLL about every 2 s so the HUD can measure Ping itself.
// host is the resolved IP when the game knows it (otherwise the typed address), NUL-terminated.
// transport: 0 = RakNet (UDP, can be pinged), anything else is not pingable. Added with ABI version 4.
struct Server { uint64_t at; uint32_t port, transport; char host[64]; };
// How often the game ran each hooked function (cumulative since the DLL started). The HUD shows the
// per-second difference in the detail screens, so "the game never calls this" is visible on screen.
// outlineEcho: what the DLL currently holds for Block Outline: 0x01000000 = lease is fresh, low 24 bits = RGB.
struct Stats { uint64_t at; uint32_t lightBuild, lightSplit, lightCreate, nameTag, nameTagLocal, outline, tessColor, outlineEcho, texReloads, reserved; };
// Debug Menu (DLL -> HUD), written with the clock (same freshness, clockAt): the block the crosshair is on
// (lookFace 0..5 = down, up, north, south, west, east; NoLook = none), its id, the biome and the light where
// the player stands (lightKnown = 0 when the game build lacks those functions). Strings NUL-terminated.
constexpr uint32_t NoLook = 0xFF;
struct Debug {
    int32_t lookX, lookY, lookZ;
    uint32_t lookFace;
    char lookBlock[48];
    char biome[48];
    uint8_t skyLight, blockLight, lightKnown, reserved[5];
};
// Block Details (DLL -> HUD), written with the clock (clockAt) for the block in Debug.lookBlock: its name in the
// game's language (UTF-8, empty = unknown), its tags (','-separated, as the game keeps them: they say which tool
// mines it and from which tier) and the held item's id. requiresTool: it drops only with the right tool;
// canHarvest: the held item can harvest it (1) or not (0); known = 1 when name / tool data were read.
struct Look {
    char name[64];
    char tags[384];
    char held[48];
    uint8_t requiresTool, canHarvest, known, reserved[5];
};
struct Shared {
    uint32_t magic, version, size, pid;
    Command command;
    Telemetry telemetry;
    Server server;
    Stats stats;
    char recPath[264];   // file the Replay recorder is writing (UTF-8); empty when not recording
    char nick[32];   // Nick: the name shown above your own character (UTF-8, NUL-terminated); on when bit 21 of command.reserved is set
    // Armor HUD icons: item id of each armor slot without "minecraft:" (e.g. "diamond_helmet"), same order and
    // freshness (telemetry.armorAt) as telemetry.armor; empty for an empty slot. NUL-terminated.
    char armorItems[4][32];
    // Frame Generation (DLL -> HUD), refreshed with telemetry.fps: frames shown per second, generated ones included
    // (telemetry.fps stays the game's own frames), and the FrameGenState below.
    float presentedFps;
    uint32_t frameGen;
    // Optimization extras (HUD -> DLL), under command.at's lease like `reserved`: see EncodeOptimize. Version 12.
    // frameGenMultiplier (HUD -> DLL, same lease): frames shown per game frame, 2..FrameGenMaxMultiplier, while
    // Frame Generation is on (the 2 bits in command.autoRq only reach x3). 0 = older HUD: those bits decide.
    // It was the unused `optimizeReserved`, so the layout and Version stay the same.
    uint32_t optimize, frameGenMultiplier;
    // World clock (DLL -> HUD) for Day Timer, refreshed every game tick: Level::getTime() (time of day in ticks,
    // 20 per second; % 24000 = 0 at sunrise). clockAt = 0 outside a world. Version 13.
    uint64_t clockAt;
    int32_t clockTime;
    // Health Warning (DLL -> HUD), with the clock: HealthKnown | the local player's health in points (1 = half a
    // heart). Older DLLs leave it 0 (= unknown), so the layout and Version stay as they were.
    uint32_t clockReserved;
    Debug debug;   // version 14
    // Debug Menu (DLL -> HUD): the GPU the game draws with, found at its first presented frame (empty / 0 before).
    // gpuName: the adapter's name (UTF-8); gpuDirectX: 11 or 12; gpuFeatureLevel: the highest D3D feature level
    // (0xc200 = 12_2); gpuVideoMb: dedicated video memory; gpuVendor: PCI vendor id; gpuDriver: the user-mode
    // driver version as four 16-bit parts (high word first: 31.0.15.4633). Also version 14.
    char gpuName[128];
    uint32_t gpuDirectX, gpuFeatureLevel, gpuVideoMb, gpuVendor;
    uint64_t gpuDriver;
    // Hitbox colours (HUD -> DLL), one per kind (player, item, entity, mob): HitboxCustom | 0xRRGGBB, or 0 for the
    // kind's own colour. Version 15.
    uint32_t hitboxRgb[4];
    Look look;   // version 16
};
static_assert(sizeof(Armor) == 12 && sizeof(Command) == 48 && sizeof(Telemetry) == 152 &&
              sizeof(Server) == 80 && sizeof(Stats) == 48 && sizeof(Shared) == 1592 && sizeof(Debug) == 120 && sizeof(Look) == 504);
static_assert(std::is_trivially_copyable<Shared>::value);
// Hitbox colours by kind (player, item, entity, mob) and Shared.hitboxRgb.
constexpr uint32_t HitboxDefaultRgb[4] = {0xFF4646, 0xFFD23C, 0xEBEBEB, 0x5ADC78};
constexpr uint32_t HitboxCustom = 0x01000000;
inline uint32_t HitboxRgbOf(uint32_t field, uint32_t kind) {
    return (field & HitboxCustom) ? (field & 0xFFFFFFu) : HitboxDefaultRgb[kind & 3];
}
inline bool Fresh(uint64_t at, uint64_t now, uint64_t timeout = StaleMs) {
    return at != 0 && at <= now && now - at <= timeout;
}
inline bool Valid(const Shared& data, uint32_t pid) {
    return data.magic == Magic && data.version == Version && data.size == sizeof(Shared) && data.pid == pid;
}
inline bool ValidHostChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ':';
}
// DLL side: copies at most 63 characters; anything else in `host` is ignored by the HUD.
inline void SetServer(Server& server, const char* host, uint32_t port, uint32_t transport, uint64_t now) {
    server = {};
    server.at = now; server.port = port; server.transport = transport;
    for (size_t i = 0; host && host[i] && i < sizeof(server.host) - 1; ++i) server.host[i] = host[i];
}
// HUD side: the pingable endpoint, or false (stale, no server, not RakNet, odd characters).
inline bool PingTarget(const Server& server, uint64_t now, char (&host)[64], uint32_t& port) {
    if (!Fresh(server.at, now, 6000) || server.transport != 0 || server.port == 0 || server.port > 65535) return false;
    size_t length = 0;
    while (length < sizeof(server.host) && server.host[length]) ++length;
    if (length == 0 || length >= sizeof(server.host)) return false;
    for (size_t i = 0; i < length; ++i) if (!ValidHostChar(server.host[i])) return false;
    for (size_t i = 0; i < length; ++i) host[i] = server.host[i];
    host[length] = '\0'; port = server.port; return true;
}
// Day Timer: Minecraft's day is 24000 ticks; night falls at 13000 ("/time set night") and morning comes at
// 24000 = 0 (the time sleeping skips to). Seconds left until each, rounded up, at 20 ticks per second.
constexpr int32_t DayTicks = 24000, NightStartTick = 13000, TicksPerSecond = 20;
inline int32_t TimeOfDay(int32_t time) { return ((time % DayTicks) + DayTicks) % DayTicks; }
inline bool IsNight(int32_t time) { return TimeOfDay(time) >= NightStartTick; }
inline int32_t SecondsUntilNight(int32_t time) {
    const int32_t t = TimeOfDay(time);
    const int32_t ticks = t < NightStartTick ? NightStartTick - t : DayTicks - t + NightStartTick;
    return (ticks + TicksPerSecond - 1) / TicksPerSecond;
}
inline int32_t SecondsUntilMorning(int32_t time) {
    return (DayTicks - TimeOfDay(time) + TicksPerSecond - 1) / TicksPerSecond;
}
inline bool ValidArmor(const Armor& armor) {
    return armor.state <= Durable && (armor.state == Durable
        ? armor.maximum > 0 && armor.maximum <= 65535 && armor.remaining <= armor.maximum
        : armor.remaining == 0 && armor.maximum == 0);
}
inline Armor MakeArmor(bool occupied, int maximum, int damage) {
    if (!occupied) return {Empty, 0, 0};
    if (maximum < 0 || damage < 0 || maximum > 65535 || damage > maximum) return {};
    if (maximum == 0) return {NoDurability, 0, 0};
    return {Durable, static_cast<uint32_t>(maximum - damage), static_cast<uint32_t>(maximum)};
}
inline bool WantsZoom(const Command& command, uint64_t now, bool focused, bool playable, bool keyDown) {
    return command.zoom == 1 && command.key >= 1 && command.key <= 254 && command.key != 27 &&
        command.key != 'G' && Fresh(command.at, now, CommandLeaseMs) && focused && playable && keyDown;
}
inline bool WantsFullbright(const Command& command, uint64_t now) {
    // A lease, like Zoom: if the HUD stops refreshing, the game returns to normal.
    return command.fullbright == 1 && Fresh(command.at, now, CommandLeaseMs);
}
// True once per HUD request. The first live command only records the counter (so attaching to a game
// never triggers a reload); after that every change of texReload is one request.
inline bool TextureReloadRequested(const Command& command, uint64_t now, uint32_t& seen, bool& primed) {
    if (command.texturesOn != 1 || !Fresh(command.at, now, CommandLeaseMs)) return false;
    if (!primed) { primed = true; seen = command.texReload; return false; }
    if (command.texReload == seen) return false;
    seen = command.texReload;
    return true;
}
inline bool WantsOutline(const Command& command, uint64_t now) {
    return command.outline == 1 && Fresh(command.at, now, CommandLeaseMs);
}
inline bool WantsNametag(const Command& command, uint64_t now) {
    // Same lease as Full Bright: if the HUD stops refreshing, the game returns to normal.
    return command.nametag == 1 && Fresh(command.at, now, CommandLeaseMs);
}
inline float FullbrightScale(float original, bool active) {
    // Raise the game's own computed value only; never lower it, never write settings.
    if (!active) return original;
    if (!std::isfinite(original)) return 1.f;
    return std::max(original, 1.f);
}
// Render Distance (Optimization): the upper bytes of Command.autoRq carry a view-distance request.
// Bits 0..7 stay the Auto RQ mode; bits 8..15 = chunks (0 = leave the game's setting alone); bits 16..23 =
// a request counter: each change applies the distance once (the first counter seen is only remembered).
constexpr uint32_t RenderDistanceMin = 4, RenderDistanceMax = 48;
inline uint32_t EncodeAutoRqField(uint32_t autoRqMode, uint32_t chunks, uint32_t counter) {
    const uint32_t c = chunks == 0 ? 0u : std::clamp(chunks, RenderDistanceMin, RenderDistanceMax);
    return (autoRqMode & 0xFFu) | (c << 8) | ((counter & 0xFFu) << 16);
}
inline uint32_t AutoRqModeOf(uint32_t field) { return field & 0xFFu; }
inline uint32_t RenderChunksOf(uint32_t field) { return (field >> 8) & 0xFFu; }
inline uint32_t RenderCounterOf(uint32_t field) { return (field >> 16) & 0xFFu; }
// Bit 24: Left Hand - the DLL moves the item in your first-person hand to the left. Older DLLs read only bits 0..23.
constexpr uint32_t LeftHandBit = 1u << 24;
inline bool LeftHandOf(uint32_t field) { return (field & LeftHandBit) != 0; }
// Bit 25: the HUD's menu is open (it takes the focus, and the game opens its pause screen when it loses it).
constexpr uint32_t HudMenuBit = 1u << 25;
inline bool HudMenuOf(uint32_t field) { return (field & HudMenuBit) != 0; }
// Bit 26: Shulker Preview is on - the HUD draws its own popup over a hovered shulker box, so the DLL hides the game's tooltip there.
constexpr uint32_t ShulkerHoverBit = 1u << 26;
inline bool ShulkerHoverOf(uint32_t field) { return (field & ShulkerHoverBit) != 0; }
// Bits 27..31: Frame Generation. 27..28 = frames shown per game frame minus one (0 = off, 1 = x2, 2 = x3);
// 29..30 = how the extra frames are made (FrameGenMethod); bit 31 = no pacing (extra frames go out at once).
enum FrameGenMethod : uint32_t { FrameGenMotion = 0, FrameGenBlend = 1, FrameGenCopy = 2 };
enum FrameGenState : uint32_t { FrameGenOff = 0, FrameGenRunning = 1, FrameGenPaused = 2, FrameGenUnavailable = 3 };
inline uint32_t EncodeFrameGen(uint32_t multiplier, uint32_t method, bool pacing) {
    const uint32_t extra = multiplier >= 3 ? 2u : multiplier == 2 ? 1u : 0u;
    if (!extra) return 0;
    return (extra << 27) | ((method <= FrameGenCopy ? method : 0u) << 29) | (pacing ? 0u : 1u << 31);
}
inline uint32_t FrameGenMultiplierOf(uint32_t field) {   // 1 = off, 2 or 3
    const uint32_t extra = (field >> 27) & 3u;
    return extra == 1 ? 2u : extra == 2 ? 3u : 1u;
}
// Frames shown per game frame from the command bits and Shared.frameGenMultiplier: 1 = off.
constexpr uint32_t FrameGenMaxMultiplier = 10;
inline uint32_t FrameGenMultiplierOf(uint32_t field, uint32_t wide) {
    const uint32_t base = FrameGenMultiplierOf(field);
    if (base < 2) return 1;
    return wide >= 2 && wide <= FrameGenMaxMultiplier ? wide : base;
}
inline uint32_t FrameGenMethodOf(uint32_t field) {
    const uint32_t method = (field >> 29) & 3u;
    return method <= FrameGenCopy ? method : FrameGenMotion;
}
inline bool FrameGenPacingOf(uint32_t field) { return (field & (1u << 31)) == 0; }
// Stats.reserved (DLL -> HUD), refreshed every frame with Stats.at. Older DLLs leave it 0 (= nothing known).
// GameScreenKnown: the DLL reads the game's screens. GameMenuOpen: in a world with a game screen over play
// (chat, inventory, pause, signs...). GamePaused: that screen is the pause screen.
// GameWorldKnown: this DLL reports GameInWorld (older DLLs never set either). GameInWorld: a local player and
// level exist and no loading / saving / title screen is on top - checked every frame, so it drops at once.
// GameTitleKnown: this DLL reports GameOnTitle (older DLLs never set either). GameOnTitle: the title screen
// (start_screen, the one with Play / Settings) is on top - not the Play / Settings / Marketplace screens.
constexpr uint32_t GameScreenKnown = 1, GameMenuOpen = 2, GamePaused = 4, GameWorldKnown = 8, GameInWorld = 16,
                   GameTitleKnown = 32, GameOnTitle = 64;
// Shared.clockReserved: the player's health (see there).
constexpr uint32_t HealthKnown = 0x80000000u;
inline uint32_t EncodeHealth(int points) { return HealthKnown | static_cast<uint32_t>(points < 0 ? 0 : points > 0xFFFF ? 0xFFFF : points); }
inline bool DecodeHealth(uint32_t field, int& points) {
    if (!(field & HealthKnown)) return false;
    points = static_cast<int>(field & 0xFFFFu);
    return true;
}
// The game's options line for a distance in chunks (options.txt stores it in blocks).
inline std::string RenderDistanceOption(uint32_t chunks) { return "gfx_viewdistance:" + std::to_string(chunks * 16); }
// Entity render distance (Optimization): bits 23..27 of `reserved` hold the distance in steps of 8 blocks
// (0 = off, 1..16 = 8..128 blocks); bit 28 also hides far-away players. Older DLLs ignore these bits.
// Bit 29: Replay join buffer (the HUD always sets it) - the DLL keeps each session from StartGame so recording can start mid-game.
constexpr uint32_t EntityDistanceStep = 8, EntityDistanceMax = 128;
inline uint32_t EncodeEntityDistance(uint32_t blocks, bool players) {
    const uint32_t steps = std::min(blocks / EntityDistanceStep, EntityDistanceMax / EntityDistanceStep);
    return (steps << 23) | (steps && players ? (1u << 28) : 0u);
}
inline uint32_t EntityDistanceBlocks(uint32_t reserved) {
    return std::min((reserved >> 23) & 0x1Fu, EntityDistanceMax / EntityDistanceStep) * EntityDistanceStep;
}
inline bool EntityDistancePlayers(uint32_t reserved) { return EntityDistanceBlocks(reserved) != 0 && ((reserved >> 28) & 1u) != 0; }
// Shared.optimize: which kinds of actor the entity distance above hides (bit 0 mobs, bit 1 dropped items,
// bit 2 projectiles and everything else), bit 3 hides other actors' name tags, and bits 4..8 a separate
// distance for players in steps of 8 blocks (0 = players are always drawn).
constexpr uint32_t CullMobsBit = 1u, CullItemsBit = 2u, CullMiscBit = 4u, HideNameTagsBit = 8u;
inline uint32_t EncodeOptimize(bool mobs, bool items, bool misc, bool hideNameTags, uint32_t playerBlocks) {
    const uint32_t steps = std::min(playerBlocks / EntityDistanceStep, EntityDistanceMax / EntityDistanceStep);
    return (mobs ? CullMobsBit : 0u) | (items ? CullItemsBit : 0u) | (misc ? CullMiscBit : 0u) |
           (hideNameTags ? HideNameTagsBit : 0u) | (steps << 4);
}
inline uint32_t PlayerDistanceBlocks(uint32_t optimize) {
    return std::min((optimize >> 4) & 0x1Fu, EntityDistanceMax / EntityDistanceStep) * EntityDistanceStep;
}
// `reserved` carries the zoom factor x100 (wheel zoom). 0 (older HUD) means the classic 3x.
inline float ZoomFactor(uint32_t x100) { return x100 >= 110 && x100 <= 2000 ? x100 / 100.f : 3.f; }
inline float ZoomFov(float original, bool active, float factor = 3.f) {
    // Operate on the computed value, preserving settings and other FOV effects.
    return active && std::isfinite(original) && original > 1.f && original < 180.f
        ? std::max(1.f, static_cast<float>(std::atan(std::tan(original * 3.141592653589793 / 360.0) / static_cast<double>(factor)) * 360.0 / 3.141592653589793)) : original;
}
class FrameRate {
    uint64_t start_ = 0, last_ = 0, samples_ = 0, measuredAt_ = 0;
    float value_ = 0;
public:
    void Reset() { start_ = last_ = samples_ = measuredAt_ = 0; value_ = 0; }
    void Frame(uint64_t now) {
        if (!last_ || now < last_ || now - last_ > StaleMs) { Reset(); start_ = last_ = now; return; }
        last_ = now; ++samples_;
        if (now - start_ >= 1000) {
            value_ = static_cast<float>(samples_ * 1000.0 / (now - start_));
            measuredAt_ = now; start_ = now; samples_ = 0;
        }
    }
    bool Read(uint64_t now, float& value, uint64_t& at) const {
        if (!Fresh(measuredAt_, now) || !Fresh(last_, now) || !std::isfinite(value_) || value_ <= 0 || value_ > 10000) return false;
        value = value_; at = measuredAt_; return true;
    }
};
}

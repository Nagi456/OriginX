#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <functional>
#include "launcher/PidOption.h"
#include "SkinChannel.h"
#include "SkinLibrary.h"
#include "SkinModel.h"
#include "MinecraftDataProvider.h"
#include "AutoRq.h"
#include "ItemIcons.h"
#include "SlotIcons.h"
#include "Presets.h"
#include "ReplayLibrary.h"
#include "ReplayPlayer.h"
#include "OverlayAlpha.h"
#include "HudFrameChannel.h"
#include "PingMonitor.h"
#include "DebugScreen.h"
#include "BlockDetails.h"
#include <mutex>
#include <thread>
#include <psapi.h>
#include "HiveStats.h"
#include "ToggleSound.h"
#include "LitePack.h"
#include "TexturePacks.h"
#include "PackInstall.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ============================================================
// MyClient - current build
// External Windows overlay prototype for Minecraft Bedrock.
//
// Current features:
//   - Keystrokes HUD
//   - CPS Counter (LMB/RMB)
//   - Live armor durability HUD
//   - Arrow Counter (requires a game data adapter)
//   - Minecraft render FPS
//   - Block Outline foundation
//   - HUD editor (drag / wheel resize)
//   - Feature shortcuts
//   - Settings
//   - Hive party invitation detection/whitelist acceptance (live test pending)
//
// Minecraft data/event connection for Armor, Arrows, FPS and Zoom.
// 3D Block Outline projection
// is intentionally left as a connection layer.
//
// Block Outline currently has a data reception hook:
//     UpdateBlockOutlineData(...)
//     ClearBlockOutlineData()
//
// The actual Minecraft-side target block data will be connected
// later without changing the rest of the client structure.
// ============================================================


// ------------------------- Features --------------------------

// Everything starts OFF on a fresh install. Toggles are saved to
// myclient_config.txt (next to MyClient.exe) and restored on every start.
bool keystrokesEnabled = false;
bool cpsEnabled = false;
bool armorHudEnabled = false;
bool resourceBackground = false;   // Resource Counter: a dark box behind each row
bool arrowBackground = false;      // Arrow Counter: a dark box behind the row
bool resourceTranslucent = false;  // ...and that box see-through (switches the overlay to per-pixel alpha)
bool arrowTranslucent = false;
// Translucent boxes drawn this frame (device coordinates); see OverlayAlpha.h.
std::vector<OverlayAlpha::Translucent> translucentRects;
std::vector<OverlayAlpha::Ink> textInks;   // text on the glass pages (OverlayAlpha::SoftenInk)
bool inkTracking = false;                  // set while DrawMenu draws a glass page
std::vector<OverlayAlpha::Glass> glassShapes;   // glass boxes of the Mods page this frame; corners smoothed after Convert
bool overlayAlphaShown = false;     // the last frame went out with per-pixel alpha
DWORD overlayAlphaError = 0;        // Windows refused per-pixel alpha (UpdateLayeredWindow); solid boxes are used
bool armorShowDurability = true;   // Armor HUD: durability numbers next to / under the icons
bool armorHorizontal = true;       // Armor HUD: icons in a row (true) or a column (false)
bool arrowCounterEnabled = false;
bool resourceCounterEnabled = false;
bool pingEnabled = false;
// Feature List: shows the names of the features that are switched on
bool featureListEnabled = false;
// Shulker Preview: lists what the shulker boxes in your inventory contain
bool shulkerEnabled = false;
// Tab List: names of the players in this world / server
bool tabListEnabled = false;
bool chunkBorderEnabled = false;
bool replayEnabled = false;      // Replay Mod: record the game's incoming packets to a file
bool menuSoundsEnabled = true;   // Menu Sounds: a sound with the on/off toast
int toggleSoundMode = 0;         // which sound: 0 OriginX (default), 1 custom clip, 2 none
bool menuSoundOpen = false;      // the sound list of the Menu Sounds page is unfolded
bool nickEnabled = false;
std::wstring nickText = L"Nick";   // name shown above your own character (third person)
bool nickEditing = false;
bool hitboxEnabled = false;
int hitboxFilter = 1;           // bits: 1 players, 2 items, 4 entities, 8 mobs
int chunkBorderRadius = 2;      // chunks around you (1..3)
// Hive Statistic (part of Hive Utils): stats of the players in this match
bool hiveStatsEnabled = false;
bool hiveStatsAuto = true;     // pick the game from the scoreboard title
std::string hiveDetectedTitle;
int hiveDetectedGame = -1;
int hiveStatsGame = 0;        // index into HiveStats::kGames
// Hive Utils groups Party Auto Accept and Auto RQ. hiveEnabled is the group's master switch;
// the two sub-features keep their own flags (partyAutoAcceptEnabled / autoRqEnabled).
bool hiveEnabled = true;
int hiveSection = 0;          // details page tab: 0 = Party Auto Accept, 1 = Auto RQ

// Texture pack switching (Textures page)
bool texturesEnabled = false;
// Lite (FPS): generated resource pack hiding clouds / sun+moon / fog / rain+snow
bool liteEnabled = false;
// Presets: named snapshots of which modules are ON (myclient_presets.txt next to the config)
std::vector<Presets::Preset> presets;
bool presetsLoaded = false;
bool presetEditing = false;      // typing in the preset name box
bool applyingPreset = false;     // ToggleFeature stays quiet (no toast / sound per module) while a preset is applied
std::wstring presetNameText;
std::wstring presetStatus;
// Fast Inventory: inventory / container screens open with a 0.1 s transition (same generated pack)
bool fastInventoryEnabled = false;
// Left Hand: your first-person arm and item on the left (mirrored animations in the same pack)
bool leftHandEnabled = false;
bool hitEffectEnabled = false;      // Hit Effect: Java-style crit particles on every hit
bool hitEffectCrit = true;          // ...white crit sparks on every hit
bool hitEffectMagic = true;         // ...blue sparks with an enchanted weapon
bool dayTimerEnabled = false;       // Day Timer: time left until night / morning in a world with a day cycle
bool dayTimerBoth = false;          // ...show both lines (off: only the next one)
bool blockDetailsEnabled = false;   // Block Details: the block under the crosshair (name, source, tool) at the top
bool blockDetailsSource = true;     // ...the line saying where it comes from (Minecraft / the add-on)
bool blockDetailsTool = true;       // ...the tool that mines it, with a tick / cross for the held item
bool healthWarningEnabled = false;  // Health Warning: the screen edges glow red while your health is low
int healthWarningAt = 6;            // ...at or below this many points (1 = half a heart; 6 = 3 hearts)
int healthWarningStrength = 70;     // ...how strong the glow is (percent)
bool healthWarningPulse = true;     // ...it beats like a heart (faster the lower you are)
bool debugMenuEnabled = true;       // Debug Menu: its key (F3) shows the Java-style debug screen in a world
bool debugScreenShown = false;      // ...shown now (not saved, like Java's F3)
int debugTextSize = 100;            // ...its text size in percent (100 = 24 px at GUI scale 1)
// Frame Generation: the DLL shows made-up frames between the game's frames (x2 / x3)
bool frameGenEnabled = false;
int frameGenMultiplier = 2;         // frames shown per game frame, x2..x10 (MyClientFeatures::FrameGenMaxMultiplier)
int frameGenMethod = 0;             // MyClientFeatures::FrameGenMethod: 0 motion, 1 blend, 2 copy (test)
bool frameGenPacing = true;         // spread the extra frames over the game's frame time
bool liteClouds = true, liteSky = true, liteFog = true, liteWeather = true;
bool liteNoCrit = false, liteNoPotion = false;   // Optimization: No Critical Hit Effect / No Potion Effect
bool liteEntityLimit = false;      // Optimization: "No far entities" row
int liteEntityDistance = 64;       // entities farther than this are not drawn (8..128, steps of 8)
bool liteCullMobs = true, liteCullItems = true, liteCullMisc = true;   // ...which kinds that distance hides
bool litePlayerLimit = false;      // far-away players hidden too, at their own distance
int litePlayerDistance = 64;       // (8..128, steps of 8)
bool liteNoParticles = false, liteNoShadows = false, liteNoNameTags = false;
// Optimization > System: done by the HUD on the game's process (Windows calls), undone when switched off.
bool liteHighPriority = false, liteNoThrottle = false, liteGpuPriority = false;
int liteSliderDrag = -1;           // the Optimization page's slider being dragged (LiteRowKind::...), -1 = none
int liteScrollRow = 0;             // first row shown on the Optimization page
// Render Distance (Optimization): sets Minecraft's own view distance from here (in chunks).
bool liteRenderEnabled = false;
int liteRenderDistance = 12;
uint32_t renderRequestCounter = 0;   // bumped when a new distance should be applied in the game
std::wstring liteStatus;
void ApplyLite();
void RequestRenderDistance();
void RefreshTextures();
// Auto RQ: queue the next Hive game when you die or the game ends. mode = index into MyClientAutoRq::kModes
bool autoRqEnabled = false;
int autoRqMode = 0;
uint32_t texturesSeq = 0;            // bumped after every list rewrite; the DLL reloads when it changes
TexturePacks::State texState;
bool texScanned = false;
// global_resource_packs.json as last read / written here: a newer file was changed by the game's own settings.
std::filesystem::file_time_type texFileTime = std::filesystem::file_time_type::min();
ULONGLONG texFileCheckedAt = 0;
// Textures page: clicking packs edits a copy of the active stack; Done writes it (see ApplyPendingTextures).
std::vector<TexturePacks::Entry> texPendingStack;
bool texEditing = false;
std::wstring texSubpackFor;      // Textures page: folder of the pack whose subpack list is open ("" = closed)
int texScroll = 0;
int detailListScroll = 0;      // first visible row of the details page's feature list
bool hiveExpanded = true;      // Hive Utils group open in that list

std::wstring texStatus;
bool fpsEnabled = false;
bool partyAutoAcceptEnabled = false;
bool blockOutlineEnabled = false;
bool zoomEnabled = false;

// Full Bright: keeps the game's night-vision brightness on while enabled.
bool fullbrightEnabled = false;
bool nametagEnabled = false;

// True while the configured Zoom key is physically held.
// The DLL validates this heartbeat before applying a temporary FOV change.
bool zoomActive = false;

// Zoom factor x100 (300 = 3x). Changed with the mouse wheel while the Zoom key is held.
std::atomic<int> zoomFactorX100{300};
std::atomic<bool> zoomFactorDirty{false};
HHOOK wheelHook = nullptr;

LRESULT CALLBACK ZoomWheelHook(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == WM_MOUSEWHEEL && zoomActive)
    {
        const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        const short delta = static_cast<short>(HIWORD(info->mouseData));
        if (delta != 0)
        {
            double f = zoomFactorX100.load() / 100.0;
            f *= delta > 0 ? 1.15 : 1.0 / 1.15;
            f = f < 1.2 ? 1.2 : (f > 20.0 ? 20.0 : f);
            zoomFactorX100 = static_cast<int>(f * 100.0 + 0.5);
            zoomFactorDirty = true;
            return 1;   // keep the hotbar from scrolling while zooming
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

enum class MenuCategory
{
    Feature,
    HUD,
    Settings,
    Presets,
    Home,     // the radial first screen (DrawHome)
    Closet
};

MenuCategory selectedCategory =
    MenuCategory::Feature;

bool menuOpen = false;
bool hudEditMode = false;
ULONGLONG editorNoticeUntil = 0;   // shows "positions reset" in the HUD editor toolbar for a moment
bool gameActive = true; // Minecraft (or this overlay) is the foreground window


// ------------------------- Detail Feature -------------------

enum class DetailFeature
{
    None,
    Keystrokes,
    CPS,
    Armor,
    Arrows,
    FPS,
    PartyAutoAccept,
    BlockOutline,
    Zoom,
    Resources,
    Fullbright,
    Nametag,
    Ping,
    Textures,
    AutoRq,
    FeatureList,
    ShulkerPreview,
    HiveUtils,
    TabList,
    HiveStats,
    ChunkBorder,
    Hitbox,
    Nick,
    Replay,
    MenuSounds,
    Lite,
    FastInventory,
    LeftHand,
    HitEffect,
    FrameGen,
    MenuOpen,
    DayTimer,
    DebugMenu,
    BlockDetails,
    HealthWarning    // keep new features last: presets store these values
};

DetailFeature detailFeature =
    DetailFeature::None;

// The page whose content is on screen: Hive Utils shows the Party Auto Accept or the Auto RQ page.
DetailFeature ActiveDetail()
{
    if (detailFeature == DetailFeature::HiveUtils)
        return hiveSection == 0 ? DetailFeature::PartyAutoAccept
             : hiveSection == 1 ? DetailFeature::AutoRq
                                : DetailFeature::HiveStats;

    return detailFeature;
}


HWND minecraftWindow = nullptr;
HWND overlayWindow = nullptr;

// Almost-invisible window under the G menu. A colour-keyed layered window lets the
// mouse pass through its transparent pixels, so without this the game kept the
// pointer (hidden cursor, camera still turning) everywhere outside the menu.
HWND catcherWindow = nullptr;

bool previousGState = false;


// ------------------------- Shortcuts -------------------------

struct ShortcutSettings
{
    int keystrokes = 0;
    int cps = 0;
    int armor = 0;
    int arrows = 0;
    int fps = 0;
    int party = 0;
    int blockOutline = 0;
    int zoom = 'C';
    // Added with 0.5.0: every feature can have a key (0 = None).
    int resources = 0;
    int fullbright = 0;
    int nametag = 0;
    int ping = 0;
    int textures = 0;
    int autoRq = 0;
    int featureList = 0;
    int shulker = 0;
    int hive = 0;
    int tabList = VK_TAB;   // default key; changeable in the settings
    int hiveStats = 0;
    int chunkBorder = 0;
    int hitbox = 0;
    int nick = 0;
    int replay = 0;
    int menuSounds = 0;
    int lite = 0;
    int fastInventory = 0;
    int leftHand = 0;
    int hitEffect = 0;
    int frameGen = 0;
    int menuKey = 'G';   // Menu Open: opens / closes this menu (never None)
    int dayTimer = 0;
    int debugMenu = VK_F3;   // Debug Menu: shows / hides the debug screen (0 = no key)
    int blockDetails = 0;
    int healthWarning = 0;
};

ShortcutSettings shortcuts;

bool waitingForShortcut = false;
// Which feature the key being waited for belongs to (set together with waitingForShortcut).
DetailFeature shortcutFeature = DetailFeature::None;

struct BlockOutlineSettings
{
    COLORREF color = RGB(80, 180, 255);
    int lineWidth = 2;
    bool showOnlyWhenTargeted = true;
};

BlockOutlineSettings blockOutlineSettings;

// Colours offered in the Block Outline details screen (6 per row).
const COLORREF kOutlinePalette[12] =
{
    RGB(255,255,255), RGB(255, 60, 60), RGB(255,150, 40), RGB(255,220, 50),
    RGB(140,230, 60), RGB( 50,200,100), RGB( 60,220,230), RGB( 80,180,255),
    RGB( 60,100,255), RGB(150, 90,255), RGB(255,100,190), RGB(  0,  0,  0)
};

// The settings (details) page is laid out for x = 535. It is drawn shifted left by this much so it
// fills the page by itself (no feature list beside it); mouse coordinates are shifted back.
constexpr int kDetailShift = 119;
// Pages are laid out for a left sidebar, so nothing needs shifting.
constexpr int kContentShift = 0;
// Sidebar (panel coordinates): module filters, Edit HUD, Client Settings.
// Filter ids: 0 All, 1 HUD, 2 Utility.
int featureFilter = 0;
inline RECT SideItemRect(int i) { return RECT{22, 118 + i * 48, 174, 158 + i * 48}; }
const RECT kEditHudRect{22, 482, 174, 520};
const RECT kClientSettingsRect{22, 528, 174, 566};
const RECT kSearchRect{655, 26, 905, 66};

// Swatch rectangle for palette entry i, relative to the details origin (535, 70).
RECT OutlineSwatchRect(int i)
{
    const int col = i % 6;
    const int row = i / 6;
    const int x = 535 + col * 54;
    const int y = 70 + 138 + row * 42;
    return RECT{x, y, x + 46, y + 34};
}


// ------------------------- Minecraft Data -------------------

// Minecraft-specific state lives behind MinecraftDataProvider.
// The UI/HUD only consumes provider data, so provider updates do
// not require rewriting the rest of MyClient.

void UpdateBlockOutlineData(
    const MinecraftDataProvider::BlockOutlineData& data)
{
    MinecraftDataProvider::UpdateBlockOutlineData(data);
}

void UpdateBlockOutlineScreenData(
    const MinecraftDataProvider::BlockOutlineScreenData& data)
{
    MinecraftDataProvider::UpdateBlockOutlineScreenData(data);
}

void ClearBlockOutlineData()
{
    MinecraftDataProvider::ClearBlockOutlineData();
}

const MinecraftDataProvider::BlockOutlineData& GetCurrentBlockOutline()
{
    return MinecraftDataProvider::GetBlockOutlineData();
}

const MinecraftDataProvider::BlockOutlineScreenData& GetCurrentBlockOutlineScreen()
{
    return MinecraftDataProvider::GetBlockOutlineScreenData();
}


// ------------------------- HUD -------------------------------

struct HudElement
{
    int x;
    int y;
    float scale;
};


HudElement keystrokesHud{
    25,
    130,
    1.0f
};

HudElement cpsHud{
    25,
    235,
    1.0f
};

HudElement armorHud{
    25,
    315,
    1.0f
};

HudElement arrowHud{
    25,
    455,
    1.0f
};

HudElement fpsHud{
    25,
    505,
    1.0f
};

HudElement resourceHud{
    25,
    555,
    1.0f
};

HudElement pingHud{
    190,
    505,
    1.0f
};

HudElement dayTimerHud{
    190,
    455,
    1.0f
};

// Block Details: x / y is the top-left of a 300-wide area; the box is centred in it.
HudElement blockDetailsHud{
    810,
    12,
    1.0f
};

HudElement featureListHud{
    450,
    25,
    1.0f
};

HudElement shulkerHud{
    700,
    120,
    1.0f
};

HudElement tabListHud{
    1000,
    120,
    1.0f
};

HudElement hiveStatsHud{
    20,
    200,
    1.0f
};


enum class HudElementId
{
    None,
    Keystrokes,
    CPS,
    Armor,
    Arrows,
    FPS,
    Resources,
    Ping,
    FeatureList,
    ShulkerPreview,
    TabList,
    HiveStats,
    DayTimer,
    BlockDetails
};


HudElementId selectedHud =
    HudElementId::None;

HudElementId draggingHud =
    HudElementId::None;

POINT dragOffset{};


// ------------------------- Party settings -------------------

std::vector<std::wstring> partyPlayers;

std::wstring partyInput;

// Feature search box (top of the Feature page).
std::wstring searchQuery;
bool searchActive = false;

// Favourite features (DetailFeature values), saved in the config file.

// Mods page (glass panel): its tab, light or dark glass, and the pinned modules (Quick Access, in pin order).
enum class ModsTab { Quick = 0, Visual = 1, Hud = 2, Input = 3, Misc = 4 };
constexpr int kModsTabCount = 5;
ModsTab modsTab = ModsTab::Visual;
bool glassDark = false;
bool glassContent = false;   // drawing a Settings / Presets / module page inside the glass frame (glass colours)

struct GlassColors
{
    COLORREF border, panel, fill, text, muted, icon, track;
    uint8_t borderA, panelA, fillA;
};
// Look of the glass pages, chosen on the Settings page (saved as GLASS_LOOK): the accent colour, the tint of
// the light and of the dark glass (index 0 = the original colours), the panel's drop shadow and how opaque
// the glass is (percent).
const COLORREF kGlassAccents[] = { RGB(44,106,221), RGB(124,77,214), RGB(206,72,150), RGB(206,64,64),
                                   RGB(222,118,30), RGB(40,152,86), RGB(22,146,158) };
const COLORREF kGlassLightTints[] = { RGB(222,227,236), RGB(236,227,220), RGB(236,222,230), RGB(218,229,240),
                                      RGB(221,236,227), RGB(229,224,240) };
const COLORREF kGlassDarkTints[] = { RGB(17,19,27), RGB(24,28,72), RGB(9,9,11), RGB(10,34,38), RGB(32,20,48),
                                     RGB(13,32,20), RGB(44,16,18) };
constexpr int kGlassAccentCount = 7, kGlassLightCount = 6, kGlassDarkCount = 7;
int glassAccentIndex = 0;
int glassLightIndex = 0;
int glassDarkIndex = 0;
int glassShadow = 50;     // 0..100
int glassOpacity = 100;   // 20..100
// The rainbow button before each palette: a colour of your own (index = the palette's count), picked as
// hue 0..359 / saturation 0..100 / brightness 0..100. 0 accent, 1 light glass, 2 dark glass.
struct GlassHsv { int h, s, v; };
GlassHsv glassCustom[3] = { {219, 80, 87}, {219, 6, 93}, {229, 37, 11} };
int glassPickerOpen = -1;   // whose picker is open under its palette (-1 = none)

COLORREF HsvToRgb(const GlassHsv& c)
{
    const float h = (float)(((c.h % 360) + 360) % 360) / 60.0f, sat = std::clamp(c.s, 0, 100) / 100.0f, val = std::clamp(c.v, 0, 100) / 100.0f;
    const int sector = (int)h;
    const float f = h - sector, p = val * (1 - sat), q = val * (1 - sat * f), t = val * (1 - sat * (1 - f));
    float r = val, g = t, b = p;
    switch (sector)
    {
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    case 5: r = val; g = p; b = q; break;
    default: break;
    }
    return RGB((int)std::lround(r * 255), (int)std::lround(g * 255), (int)std::lround(b * 255));
}

COLORREF AddRgb(COLORREF c, int r, int g, int b)
{
    return RGB(std::clamp(GetRValue(c) + r, 0, 255), std::clamp(GetGValue(c) + g, 0, 255), std::clamp(GetBValue(c) + b, 0, 255));
}
COLORREF MixRgb(COLORREF a, COLORREF b, int percentB)
{
    const auto m = [&](int x, int y) { return (x * (100 - percentB) + y * percentB) / 100; };
    return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
}

// Glass colours are only used for glass (OverlayAlpha finds them by colour): text and icons use others.
COLORREF GlassAccent()
{
    return glassAccentIndex == kGlassAccentCount ? HsvToRgb(glassCustom[0]) : kGlassAccents[std::clamp(glassAccentIndex, 0, kGlassAccentCount - 1)];
}
COLORREF GlassAccentLight() { return MixRgb(GlassAccent(), RGB(255,255,255), 55); }   // a switch on an accent row
constexpr uint8_t kGlassAccentAlpha = 235;
GlassColors GlassTheme()
{
    if (glassDark)
    {
        const COLORREF p = glassDarkIndex == kGlassDarkCount ? HsvToRgb(glassCustom[2])
                                                             : kGlassDarkTints[std::clamp(glassDarkIndex, 0, kGlassDarkCount - 1)];
        return {AddRgb(p, 75, 79, 91), p, AddRgb(p, 29, 31, 39), RGB(236,238,245), RGB(150,156,172), RGB(210,214,226),
                RGB(86,92,110), 120, 168, 190};
    }
    const COLORREF p = glassLightIndex == kGlassLightCount ? HsvToRgb(glassCustom[1])
                                                           : kGlassLightTints[std::clamp(glassLightIndex, 0, kGlassLightCount - 1)];
    return {AddRgb(p, 28, 24, 17), p, AddRgb(p, 21, 18, 13), RGB(34,38,52), RGB(104,110,126), RGB(70,76,92),
            RGB(190,195,205), 150, 96, 172};
}

// HUD colours: each HUD module's text, box, border and (Keystrokes) pressed key, picked on its page
// (saved as HUD_COLORS). The index picks from HudPalette: 0 is the module's own colour ("Default"), the
// palette's size is a colour of your own (the rainbow button, hue / saturation / brightness).
enum HudPart { HudText, HudBox, HudBorder, HudPressed, kHudPartCount };
const COLORREF kHudInks[] = { RGB(255,255,255), RGB(190,192,200), RGB(255,215,80), RGB(255,150,60), RGB(255,95,90),
                              RGB(255,120,200), RGB(180,130,255), RGB(90,160,255), RGB(90,230,235), RGB(100,220,130) };
const COLORREF kHudBoxes[] = { RGB(6,6,8), RGB(48,52,62), RGB(18,26,58), RGB(40,20,60), RGB(60,16,26), RGB(14,46,26),
                               RGB(12,40,46), RGB(90,92,100), RGB(232,234,240) };
constexpr int kHudInkCount = 10, kHudBoxCount = 9;
struct HudLook
{
    int index[kHudPartCount] = {};
    GlassHsv custom[kHudPartCount] = { {0,0,100}, {230,30,12}, {0,0,100}, {210,68,86} };
    bool border = false;
    // The "Black background" / "Translucent" switches of the modules that had none (the Arrow and Resource
    // Counters keep theirs). box: -1 the module's own (a box, the Armor HUD none), 0 off, 1 on.
    int box = -1;
    bool see = false;
};
constexpr int kHudLookCount = (int)HudElementId::BlockDetails + 1;
HudLook hudLooks[kHudLookCount];

bool HudBoxOn(HudElementId id)
{
    if ((int)id <= 0 || (int)id >= kHudLookCount) return true;
    const int box = hudLooks[(int)id].box;
    return box < 0 ? id != HudElementId::Armor : box != 0;
}
// The box the module really draws (the counters keep their own switch).
bool HudBoxShown(HudElementId id)
{
    return id == HudElementId::Arrows ? arrowBackground : id == HudElementId::Resources ? resourceBackground : HudBoxOn(id);
}
// Background "Frame only": a border instead of the box (never both).
bool HudFrameOnly(HudElementId id)
{
    return (int)id > 0 && (int)id < kHudLookCount && hudLooks[(int)id].border && !HudBoxShown(id);
}
bool HudSeeThrough(HudElementId id)
{
    return HudBoxOn(id) && (int)id > 0 && (int)id < kHudLookCount && hudLooks[(int)id].see;
}

// The swatch of each module's own text colour ("Default"; modules that colour their text by meaning show
// one of those colours). Fixed, so the indices into its palette stay the same.
COLORREF HudOwnTextSwatch(HudElementId id)
{
    switch (id)
    {
    case HudElementId::Armor:     return RGB(110,230,110);
    case HudElementId::Resources: return RGB(215,215,215);
    case HudElementId::Ping:      return RGB(100,220,130);
    case HudElementId::HiveStats: return RGB(230,230,230);
    default:                      return RGB(255,255,255);
    }
}
COLORREF HudOwnBoxSwatch(HudElementId id)
{
    return id == HudElementId::Keystrokes ? RGB(35,35,35)
           : id == HudElementId::Arrows || id == HudElementId::Resources || id == HudElementId::Armor ? RGB(14,14,18)
                                                                                                   : RGB(20,20,20);
}

// The palette of `part` of module `id`: the module's own colour first, then the shared ones (without a
// copy of the first).
std::vector<COLORREF> HudPalette(HudElementId id, HudPart part)
{
    const COLORREF own = part == HudText ? HudOwnTextSwatch(id) : part == HudBox ? HudOwnBoxSwatch(id)
                         : part == HudPressed ? RGB(70,150,220) : GlassAccent();
    std::vector<COLORREF> colors{own};
    const COLORREF* shared = part == HudBox ? kHudBoxes : kHudInks;
    for (int i = 0, n = part == HudBox ? kHudBoxCount : kHudInkCount; i < n; i++)
        if (part != HudText || shared[i] != own) colors.push_back(shared[i]);
    return colors;
}

// The colour to draw `part` of HUD module `id` with; `ownColor` is what the module uses by default.
COLORREF HudColor(HudElementId id, HudPart part, COLORREF ownColor)
{
    if ((int)id <= 0 || (int)id >= kHudLookCount) return ownColor;
    const HudLook& look = hudLooks[(int)id];
    const int i = look.index[part];
    if (i <= 0) return ownColor;
    const std::vector<COLORREF> palette = HudPalette(id, part);
    COLORREF c = i < (int)palette.size() ? palette[i] : HsvToRgb(look.custom[part]);
    if (c == RGB(255,0,255)) c = RGB(254,0,255);   // the overlay's colour key would hide it
    return c;
}

GlassHsv RgbToHsv(COLORREF c)
{
    const int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    const int hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
    int h = 0;
    if (d > 0)
        h = hi == r ? 60 * (g - b) / d : hi == g ? 120 + 60 * (b - r) / d : 240 + 60 * (r - g) / d;
    return {(h + 360) % 360, hi == 0 ? 0 : d * 100 / hi, hi * 100 / 255};
}

// Block Outline colour: kOutlinePalette, then the rainbow button (index 12 = hue / saturation / brightness of
// your own). Saved in BLOCK_OUTLINE.
constexpr int kOutlineCount = 12;
int blockOutlineIndex = 7;   // blue
GlassHsv blockOutlineHsv = {205, 69, 100};
COLORREF BlockOutlineColor()
{
    return blockOutlineIndex >= 0 && blockOutlineIndex < kOutlineCount ? kOutlinePalette[blockOutlineIndex] : HsvToRgb(blockOutlineHsv);
}

// Health Warning glow colour: kHealthPalette, then the rainbow button (your own hue / saturation / brightness).
// Saved at the end of HEALTHWARNING.
const COLORREF kHealthPalette[] = {RGB(224,16,16), RGB(255,110,20), RGB(255,210,40), RGB(60,200,80), RGB(40,200,220),
                                   RGB(50,110,255), RGB(150,70,240), RGB(240,80,180), RGB(255,255,255), RGB(0,0,0)};
constexpr int kHealthPaletteCount = (int)(sizeof(kHealthPalette) / sizeof(kHealthPalette[0]));
int healthWarningIndex = 0;   // red
GlassHsv healthWarningHsv = {0, 93, 88};
COLORREF HealthWarningColor()
{
    return healthWarningIndex >= 0 && healthWarningIndex < kHealthPaletteCount ? kHealthPalette[healthWarningIndex]
                                                                                : HsvToRgb(healthWarningHsv);
}

// Hitbox colours by kind (player, item, entity, mob): 0 the kind's own colour, then kHudInks, then the rainbow
// button (index = palette size). Saved in HITBOX, sent to the DLL as Shared.hitboxRgb.
const COLORREF kHitboxOwn[4] = {RGB(255, 70, 70), RGB(255, 210, 60), RGB(235, 235, 235), RGB(90, 220, 120)};
int hitboxColorIndex[4] = {};
GlassHsv hitboxHsv[4] = {{0, 73, 100}, {44, 76, 100}, {0, 0, 92}, {135, 59, 86}};
std::vector<COLORREF> HitboxPalette(int kind)
{
    std::vector<COLORREF> colors{kHitboxOwn[kind & 3]};
    colors.insert(colors.end(), kHudInks, kHudInks + kHudInkCount);
    return colors;
}
COLORREF HitboxColor(int kind)
{
    const int i = hitboxColorIndex[kind & 3];
    if (i <= 0) return kHitboxOwn[kind & 3];
    COLORREF c = i <= kHudInkCount ? kHudInks[i - 1] : HsvToRgb(hitboxHsv[kind & 3]);
    if (c == RGB(255,0,255)) c = RGB(254,0,255);   // the overlay's colour key would hide it
    return c;
}

std::vector<DetailFeature> pinnedFeatures;

bool partyAddingPlayer = false;


// ------------------------- Settings --------------------------

std::wstring clientName =
    L"OriginX Client";

float guiScale = 1.0f;

bool menuAnimation = true;

// Menu colour theme (index into kThemes), saved in the config file.
int themeIndex = 0;

// True only while the G menu itself is being drawn (not the in-game HUD).
bool menuDrawing = false;

bool MenuLightMode();
COLORREF MenuTextColor(COLORREF c);
COLORREF MenuBoxColor(COLORREF c);


// ------------------------- CPS -------------------------------

std::vector<
    std::chrono::steady_clock::time_point
> leftClicks;

std::vector<
    std::chrono::steady_clock::time_point
> rightClicks;


// ------------------------- Paths -----------------------------

std::wstring GetExecutableDirectory()
{
    wchar_t path[MAX_PATH]{};

    GetModuleFileNameW(
        nullptr,
        path,
        MAX_PATH
    );

    return std::filesystem::path(path)
        .parent_path()
        .wstring();
}


std::wstring ConfigPath()
{
    return GetExecutableDirectory()
        + L"\\myclient_config.txt";
}


// ------------------------- Helpers ---------------------------

bool IsInside(
    int mx,
    int my,
    int x,
    int y,
    int w,
    int h)
{
    return mx >= x &&
           mx <= x + w &&
           my >= y &&
           my <= y + h;
}


std::wstring KeyName(int vk)
{
    if (vk == 0)
        return L"Not Set";

    if (vk == VK_SPACE)
        return L"SPACE";

    if (vk == VK_RETURN)
        return L"ENTER";

    if (vk == VK_TAB)
        return L"TAB";

    if (vk == VK_SHIFT)
        return L"SHIFT";

    if (vk == VK_CONTROL)
        return L"CTRL";

    if (vk == VK_MENU)
        return L"ALT";

    if (vk == VK_ESCAPE)
        return L"ESC";

    if (vk == VK_BACK)
        return L"BACKSPACE";

    if (vk == VK_LEFT)
        return L"LEFT";

    if (vk == VK_RIGHT)
        return L"RIGHT";

    if (vk == VK_UP)
        return L"UP";

    if (vk == VK_DOWN)
        return L"DOWN";

    if (vk == VK_DELETE)
        return L"DELETE";

    if (vk == VK_INSERT)
        return L"INSERT";

    if (vk == VK_HOME)
        return L"HOME";

    if (vk == VK_END)
        return L"END";

    if (vk == VK_PRIOR)
        return L"PAGEUP";

    if (vk == VK_NEXT)
        return L"PAGEDOWN";

    if (vk >= VK_F1 &&
        vk <= VK_F24)
    {
        return L"F" +
            std::to_wstring(
                vk - VK_F1 + 1
            );
    }

    UINT scan =
        MapVirtualKeyW(
            static_cast<UINT>(vk),
            MAPVK_VK_TO_VSC
        );

    if (scan)
    {
        LONG lp =
            static_cast<LONG>(
                scan << 16
            );

        wchar_t name[128]{};

        if (GetKeyNameTextW(
                lp,
                name,
                128) > 0)
        {
            return name;
        }
    }

    if (vk >= 32 &&
        vk <= 126)
    {
        return std::wstring(
            1,
            static_cast<wchar_t>(vk)
        );
    }

    return L"VK " +
        std::to_wstring(vk);
}


bool IsReservedShortcut(int vk)
{
    return vk == shortcuts.menuKey ||
           vk == VK_ESCAPE;
}


int* GetShortcutPtr(
    DetailFeature f)
{
    switch (f)
    {
    case DetailFeature::Keystrokes:
        return &shortcuts.keystrokes;

    case DetailFeature::CPS:
        return &shortcuts.cps;

    case DetailFeature::Armor:
        return &shortcuts.armor;

    case DetailFeature::Arrows:
        return &shortcuts.arrows;

    case DetailFeature::FPS:
        return &shortcuts.fps;

    case DetailFeature::PartyAutoAccept:
        return &shortcuts.party;

    case DetailFeature::BlockOutline:
        return &shortcuts.blockOutline;

    case DetailFeature::Zoom:
        return &shortcuts.zoom;

    case DetailFeature::Resources:
        return &shortcuts.resources;

    case DetailFeature::Fullbright:
        return &shortcuts.fullbright;

    case DetailFeature::Nametag:
        return &shortcuts.nametag;

    case DetailFeature::Ping:
        return &shortcuts.ping;

    case DetailFeature::Textures:
        return &shortcuts.textures;

    case DetailFeature::AutoRq:
        return &shortcuts.autoRq;

    case DetailFeature::FeatureList:
        return &shortcuts.featureList;

    case DetailFeature::ShulkerPreview:
        return &shortcuts.shulker;

    case DetailFeature::HiveUtils:
        return &shortcuts.hive;

    case DetailFeature::TabList:
        return &shortcuts.tabList;

    case DetailFeature::HiveStats:
        return &shortcuts.hiveStats;

    case DetailFeature::ChunkBorder:
        return &shortcuts.chunkBorder;

    case DetailFeature::Hitbox:
        return &shortcuts.hitbox;

    case DetailFeature::Nick:
        return &shortcuts.nick;

    case DetailFeature::Replay:
        return &shortcuts.replay;

    case DetailFeature::MenuSounds:
        return &shortcuts.menuSounds;

    case DetailFeature::Lite:
        return &shortcuts.lite;

    case DetailFeature::FastInventory:
        return &shortcuts.fastInventory;

    case DetailFeature::LeftHand:
        return &shortcuts.leftHand;
    case DetailFeature::HitEffect:
        return &shortcuts.hitEffect;
    case DetailFeature::FrameGen:
        return &shortcuts.frameGen;
    case DetailFeature::MenuOpen:
        return &shortcuts.menuKey;
    case DetailFeature::DayTimer:
        return &shortcuts.dayTimer;
    case DetailFeature::DebugMenu:
        return &shortcuts.debugMenu;
    case DetailFeature::BlockDetails:
        return &shortcuts.blockDetails;
    case DetailFeature::HealthWarning:
        return &shortcuts.healthWarning;

    default:
        return nullptr;
    }
}


bool IsValidPlayerTag(const std::wstring& value)
{
    std::string tag;
    for (wchar_t c : value) {
        if (static_cast<uint32_t>(c) > 127) return false;
        tag += static_cast<char>(c);
    }
    return MyClientParty::ValidTag(tag);
}


// ------------------------- Persistence ----------------------
// Simple wide text format.
// Party names are stored after PARTY_BEGIN until PARTY_END.

void SaveConfig()
{
    std::wofstream file(
        std::filesystem::path(ConfigPath()),
        std::ios::trunc
    );

    if (!file.is_open())
        return;

    file << L"VERSION 4\n";

    file << L"FEATURES "
         << keystrokesEnabled << L' '
         << cpsEnabled << L' '
         << armorHudEnabled << L' '
         << arrowCounterEnabled << L' '
         << fpsEnabled << L' '
         << partyAutoAcceptEnabled << L' '
         << blockOutlineEnabled << L' '
         << zoomEnabled
         << L"\n";

    file << L"SHORTCUTS "
         << shortcuts.keystrokes << L' '
         << shortcuts.cps << L' '
         << shortcuts.armor << L' '
         << shortcuts.arrows << L' '
         << shortcuts.fps << L' '
         << shortcuts.party << L' '
         << shortcuts.blockOutline << L' '
         << shortcuts.zoom
         << L"\n";

    file << L"SHORTCUTS2 "
         << shortcuts.resources << L' '
         << shortcuts.fullbright << L' '
         << shortcuts.nametag << L' '
         << shortcuts.ping << L' '
         << shortcuts.textures << L' '
         << shortcuts.autoRq << L' '
         << shortcuts.featureList << L' '
         << shortcuts.shulker << L' '
         << shortcuts.hive << L' '
         << shortcuts.tabList << L' '
         << shortcuts.hiveStats << L' '
         << shortcuts.chunkBorder << L' '
         << shortcuts.hitbox << L' '
         << shortcuts.nick << L' '
         << shortcuts.replay << L' '
         << shortcuts.menuSounds << L' '
         << shortcuts.lite << L' '
         << shortcuts.fastInventory << L' '
         << shortcuts.leftHand << L' '
         << shortcuts.hitEffect << L' '
         << shortcuts.frameGen << L' '
         << shortcuts.menuKey << L' '
         << shortcuts.dayTimer << L' '
         << shortcuts.debugMenu << L' '
         << shortcuts.blockDetails << L' '
         << shortcuts.healthWarning
         << L"\n";

    file << L"REPLAY "
         << replayEnabled
         << L"\n";

    file << L"MENUSOUNDS " << menuSoundsEnabled << L' ' << toggleSoundMode << L"\n";

    file << L"FASTINV " << fastInventoryEnabled << L"\n";

    file << L"ARMORHUD " << armorShowDurability << L' ' << armorHorizontal << L"\n";
    file << L"COUNTERBG " << resourceBackground << L' ' << arrowBackground << L' '
         << resourceTranslucent << L' ' << arrowTranslucent << L"\n";

    file << L"LEFTHAND " << leftHandEnabled << L"\n";
    file << L"HITEFFECT " << hitEffectEnabled << L' ' << hitEffectMagic << L' ' << hitEffectCrit << L"\n";
    file << L"FRAMEGEN " << frameGenEnabled << L' ' << frameGenMultiplier << L' ' << frameGenMethod << L' ' << frameGenPacing << L"\n";

    file << L"LITE " << liteEnabled << L' ' << liteClouds << L' ' << liteSky << L' ' << liteFog << L' ' << liteWeather
         << L' ' << liteEntityDistance << L' ' << litePlayerLimit << L' ' << liteEntityLimit
         << L' ' << liteRenderEnabled << L' ' << liteRenderDistance << L' ' << liteNoCrit << L' ' << liteNoPotion << L"\n";
    file << L"LITE2 " << liteNoParticles << L' ' << liteNoShadows << L' ' << liteNoNameTags << L' ' << liteCullMobs
         << L' ' << liteCullItems << L' ' << liteCullMisc << L' ' << litePlayerLimit << L' ' << litePlayerDistance
         << L' ' << liteHighPriority << L' ' << liteNoThrottle << L' ' << liteGpuPriority << L"\n";

    file << L"NICK "
         << nickEnabled << L' '
         << nickText
         << L"\n";

    file << L"HITBOX "
         << hitboxEnabled << L' '
         << hitboxFilter;
    for (int k = 0; k < 4; k++)
        file << L' ' << hitboxColorIndex[k] << L' ' << hitboxHsv[k].h << L' ' << hitboxHsv[k].s << L' ' << hitboxHsv[k].v;
    file << L"\n";

    file << L"CHUNKBORDER "
         << chunkBorderEnabled << L' '
         << chunkBorderRadius
         << L"\n";

    file << L"BLOCK_OUTLINE "
         << blockOutlineSettings.lineWidth << L' '
         << GetRValue(BlockOutlineColor()) << L' '
         << GetGValue(BlockOutlineColor()) << L' '
         << GetBValue(BlockOutlineColor()) << L' '
         << blockOutlineSettings.showOnlyWhenTargeted << L' '
         << blockOutlineIndex << L' '
         << blockOutlineHsv.h << L' ' << blockOutlineHsv.s << L' ' << blockOutlineHsv.v
         << L"\n";

    file << L"FULLBRIGHT "
         << fullbrightEnabled
         << L"\n";

    file << L"NAMETAG "
         << nametagEnabled
         << L"\n";

    file << L"TEXTURES "
         << texturesEnabled
         << L"\n";

    file << L"AUTORQ "
         << autoRqEnabled << L' '
         << autoRqMode
         << L"\n";

    file << L"ZOOM "
          << shortcuts.zoom << L' '
          << zoomEnabled
          << L"\n";

    file << L"HUD "
         << keystrokesHud.x << L' '
         << keystrokesHud.y << L' '
         << keystrokesHud.scale << L' '

         << cpsHud.x << L' '
         << cpsHud.y << L' '
         << cpsHud.scale << L' '

         << armorHud.x << L' '
         << armorHud.y << L' '
         << armorHud.scale << L' '

         << arrowHud.x << L' '
         << arrowHud.y << L' '
         << arrowHud.scale << L' '

         << fpsHud.x << L' '
         << fpsHud.y << L' '
         << fpsHud.scale

         << L"\n";

    file << L"RESOURCES "
         << resourceCounterEnabled << L' '
         << resourceHud.x << L' '
         << resourceHud.y << L' '
         << resourceHud.scale
         << L"\n";

    file << L"PING "
         << pingEnabled << L' '
         << pingHud.x << L' '
         << pingHud.y << L' '
         << pingHud.scale
         << L"\n";

    file << L"BLOCKDETAILS " << blockDetailsEnabled << L' ' << blockDetailsHud.x << L' ' << blockDetailsHud.y << L' '
         << blockDetailsHud.scale << L' ' << blockDetailsSource << L' ' << blockDetailsTool << L"\n";
    file << L"DEBUGMENU " << debugMenuEnabled << L' ' << debugTextSize << L"\n";
    file << L"HEALTHWARNING " << healthWarningEnabled << L' ' << healthWarningAt << L' ' << healthWarningStrength << L' '
         << healthWarningPulse << L' ' << healthWarningIndex << L' '
         << healthWarningHsv.h << L' ' << healthWarningHsv.s << L' ' << healthWarningHsv.v << L"\n";
    file << L"DAYTIMER " << dayTimerEnabled << L' ' << dayTimerHud.x << L' ' << dayTimerHud.y << L' '
         << dayTimerHud.scale << L' ' << dayTimerBoth << L"\n";

    file << L"FEATURELIST "
         << featureListEnabled << L' '
         << featureListHud.x << L' '
         << featureListHud.y << L' '
         << featureListHud.scale
         << L"\n";

    file << L"HIVE " << hiveEnabled << L"\n";

    file << L"SHULKER "
         << shulkerEnabled << L' '
         << shulkerHud.x << L' '
         << shulkerHud.y << L' '
         << shulkerHud.scale
         << L"\n";

    file << L"ZOOMLEVEL " << zoomFactorX100.load() << L"\n";

    file << L"TABLIST "
         << tabListEnabled << L' '
         << tabListHud.x << L' '
         << tabListHud.y << L' '
         << tabListHud.scale
         << L"\n";

    file << L"HIVESTATS "
         << hiveStatsEnabled << L' '
         << hiveStatsHud.x << L' '
         << hiveStatsHud.y << L' '
         << hiveStatsHud.scale << L' '
         << hiveStatsGame << L' '
         << hiveStatsAuto
         << L"\n";

    file << L"CLIENT "
         << clientName
         << L"\n";

    file << L"GUI_SCALE "
         << guiScale
         << L"\n";

    file << L"MENU_ANIMATION "
         << menuAnimation
         << L"\n";

    file << L"GLASS_DARK " << glassDark << L"\n";
    file << L"GLASS_LOOK " << glassAccentIndex << L' ' << glassLightIndex << L' ' << glassDarkIndex << L' '
         << glassShadow << L' ' << glassOpacity;
    for (const GlassHsv& c : glassCustom)
        file << L' ' << c.h << L' ' << c.s << L' ' << c.v;
    file << L"\n";
    for (int id = 1; id < kHudLookCount; id++)
    {
        const HudLook& look = hudLooks[id];
        file << L"HUD_COLORS " << id << L' ' << look.border;
        for (int i : look.index) file << L' ' << i;
        for (const GlassHsv& c : look.custom) file << L' ' << c.h << L' ' << c.s << L' ' << c.v;
        file << L' ' << look.box << L' ' << look.see << L"\n";
    }

    file << L"PINNED";
    for (DetailFeature f : pinnedFeatures)
        file << L' ' << (int)f;
    file << L"\n";


    file << L"PARTY_BEGIN\n";

    for (const auto& p :
         partyPlayers)
    {
        file << p << L"\n";
    }

    file << L"PARTY_END\n";
}


void LoadConfig()
{
    std::wifstream file(
        std::filesystem::path{ConfigPath()}
    );

    if (!file.is_open())
        return;

    std::wstring key;

    partyPlayers.clear();

    bool readingParty = false;

    std::wstring line;

    while (std::getline(
        file,
        line))
    {
        if (line == L"PARTY_BEGIN")
        {
            readingParty = true;
            continue;
        }

        if (line == L"PARTY_END")
        {
            readingParty = false;
            continue;
        }

        if (readingParty)
        {
            if (IsValidPlayerTag(line))
            {
                partyPlayers.push_back(
                    line
                );
            }

            continue;
        }

        std::wistringstream ss(line);

        ss >> key;

        if (key == L"FEATURES")
        {
            ss >> keystrokesEnabled
               >> cpsEnabled
               >> armorHudEnabled
               >> arrowCounterEnabled
               >> fpsEnabled
               >> partyAutoAcceptEnabled;

            // Compatible with old config files.
            if (!(ss >> blockOutlineEnabled))
            {
                blockOutlineEnabled = false;
            }

            if (!(ss >> zoomEnabled))
            {
                zoomEnabled = false;
            }
        }
        else if (key == L"SHORTCUTS")
        {
            ss >> shortcuts.keystrokes
               >> shortcuts.cps
               >> shortcuts.armor
               >> shortcuts.arrows
               >> shortcuts.fps
               >> shortcuts.party;

            // Compatible with old config files.
            if (!(ss >> shortcuts.blockOutline))
            {
                shortcuts.blockOutline = 0;
            }

            if (!(ss >> shortcuts.zoom))
            {
                shortcuts.zoom = 'C';
            }
        }
        else if (key == L"SHORTCUTS2")
        {
            // Optional line: configs written by older versions simply lack it.
            ShortcutSettings read = shortcuts;
            if (ss >> read.resources >> read.fullbright >> read.nametag >> read.ping >>
                      read.textures >> read.autoRq >> read.featureList)
            {
                ss >> read.shulker;   // optional: absent in configs from before Shulker Preview
                ss >> read.hive;      // optional: absent in configs from before Hive Utils
                int tabKey = shortcuts.tabList;
                if (ss >> tabKey)     // optional: absent in configs from before Tab List
                    shortcuts.tabList = tabKey;
                int statsKey = shortcuts.hiveStats;
                if (ss >> statsKey)   // optional: absent in configs from before Hive Statistic
                    shortcuts.hiveStats = statsKey;
                int chunkKey = shortcuts.chunkBorder;
                if (ss >> chunkKey)   // optional: absent in configs from before Chunk Border
                    shortcuts.chunkBorder = chunkKey;
                int hitboxKey = shortcuts.hitbox;
                if (ss >> hitboxKey)  // optional: absent in configs from before Hitbox
                    shortcuts.hitbox = hitboxKey;
                int nickKey = shortcuts.nick;
                if (ss >> nickKey)    // optional: absent in configs from before Nick
                    shortcuts.nick = nickKey;
                int replayKey = shortcuts.replay;
                if (ss >> replayKey)  // optional: absent in configs from before Replay Mod
                    shortcuts.replay = replayKey;
                int soundKey = shortcuts.menuSounds;
                if (ss >> soundKey)   // optional: absent in configs from before Menu Sounds
                    shortcuts.menuSounds = soundKey;
                int liteKey = shortcuts.lite;
                if (ss >> liteKey)    // optional: absent in configs from before Lite
                    shortcuts.lite = liteKey;
                int fastKey = shortcuts.fastInventory;
                if (ss >> fastKey)    // optional: absent in configs from before Fast Inventory
                    shortcuts.fastInventory = fastKey;
                int handKey = shortcuts.leftHand;
                if (ss >> handKey)    // optional: absent in configs from before Left Hand
                    shortcuts.leftHand = handKey;
                int hitKey = shortcuts.hitEffect;
                if (ss >> hitKey)     // optional: absent in configs from before Hit Effect
                    shortcuts.hitEffect = hitKey;
                int frameGenKey = shortcuts.frameGen;
                if (ss >> frameGenKey)   // optional: absent in configs from before Frame Generation
                    shortcuts.frameGen = frameGenKey;
                int menuKey = shortcuts.menuKey;
                if (ss >> menuKey && menuKey > 0 && menuKey < 256 && menuKey != VK_ESCAPE)   // optional: before Menu Open
                    shortcuts.menuKey = menuKey;
                int dayTimerKey = shortcuts.dayTimer;
                if (ss >> dayTimerKey)   // optional: absent in configs from before Day Timer
                    shortcuts.dayTimer = dayTimerKey;
                int debugKey = shortcuts.debugMenu;
                if (ss >> debugKey && debugKey >= 0 && debugKey < 256 && debugKey != VK_ESCAPE)   // optional: before Debug Menu
                    shortcuts.debugMenu = debugKey;
                int blockDetailsKey = shortcuts.blockDetails;
                if (ss >> blockDetailsKey)   // optional: absent in configs from before Block Details
                    shortcuts.blockDetails = blockDetailsKey;
                int healthWarningKey = shortcuts.healthWarning;
                if (ss >> healthWarningKey)   // optional: absent in configs from before Health Warning
                    shortcuts.healthWarning = healthWarningKey;
                shortcuts.shulker = read.shulker;
                shortcuts.hive = read.hive;
                shortcuts.resources = read.resources;
                shortcuts.fullbright = read.fullbright;
                shortcuts.nametag = read.nametag;
                shortcuts.ping = read.ping;
                shortcuts.textures = read.textures;
                shortcuts.autoRq = read.autoRq;
                shortcuts.featureList = read.featureList;
            }
        }
        else if (key == L"FULLBRIGHT")
        {
            // Optional line: configs written by older versions simply lack it.
            bool enabled = false;
            if (ss >> enabled)
            {
                fullbrightEnabled = enabled;
            }
        }
        else if (key == L"AUTORQ")
        {
            bool enabled = false;
            int mode = 0;
            if (ss >> enabled >> mode)
            {
                autoRqEnabled = enabled;
                autoRqMode = std::clamp(mode, 0, (int)MyClientAutoRq::kModeCount - 1);
            }
        }
        else if (key == L"TEXTURES")
        {
            bool enabled = false;
            if (ss >> enabled)
            {
                texturesEnabled = enabled;
            }
        }
        else if (key == L"NAMETAG")
        {
            bool enabled = false;
            if (ss >> enabled)
            {
                nametagEnabled = enabled;
            }
        }
        else if (key == L"ZOOM")
        {
            int zoomKey = 'C';
            bool enabled = false;

            ss >> zoomKey >> enabled;

            if (zoomKey == 'G' || zoomKey == VK_ESCAPE)
            {
                zoomKey = 'C';
            }

            shortcuts.zoom = zoomKey;
            zoomEnabled = enabled;
        }
        else if (key == L"BLOCK_OUTLINE")
        {
            int lineWidth = 2;

            int r = 80;
            int g = 180;
            int b = 255;

            bool targetedOnly = true;

            ss >> lineWidth
               >> r
               >> g
               >> b
               >> targetedOnly;

            blockOutlineSettings.lineWidth =
                std::clamp(
                    lineWidth,
                    1,
                    8
                );

            r = std::clamp(
                r,
                0,
                255
            );

            g = std::clamp(
                g,
                0,
                255
            );

            b = std::clamp(
                b,
                0,
                255
            );

            blockOutlineSettings.color =
                RGB(
                    r,
                    g,
                    b
                );

            blockOutlineSettings.showOnlyWhenTargeted =
                targetedOnly;

            // The palette entry, or the rainbow button's own colour (configs from before it: found by the colour).
            int index = -1;
            GlassHsv hsv{};
            if (ss >> index >> hsv.h >> hsv.s >> hsv.v)
            {
                blockOutlineIndex = std::clamp(index, 0, kOutlineCount);
                blockOutlineHsv = {std::clamp(hsv.h, 0, 359), std::clamp(hsv.s, 0, 100), std::clamp(hsv.v, 0, 100)};
            }
            else
            {
                blockOutlineIndex = kOutlineCount;
                blockOutlineHsv = RgbToHsv(blockOutlineSettings.color);
                for (int i = 0; i < kOutlineCount; i++)
                    if (kOutlinePalette[i] == blockOutlineSettings.color)
                        blockOutlineIndex = i;
            }
            blockOutlineSettings.color = BlockOutlineColor();
        }
        else if (key == L"HUD")
        {
            ss >> keystrokesHud.x
               >> keystrokesHud.y
               >> keystrokesHud.scale

               >> cpsHud.x
               >> cpsHud.y
               >> cpsHud.scale

               >> armorHud.x
               >> armorHud.y
               >> armorHud.scale

               >> arrowHud.x
               >> arrowHud.y
               >> arrowHud.scale

               >> fpsHud.x
               >> fpsHud.y
               >> fpsHud.scale;
        }
        else if (key == L"RESOURCES")
        {
            // Optional line: configs written by older versions simply lack it.
            bool enabled = resourceCounterEnabled;
            HudElement element = resourceHud;
            if (ss >> enabled >> element.x >> element.y >> element.scale)
            {
                resourceCounterEnabled = enabled;
                resourceHud = element;
            }
        }
        else if (key == L"PING")
        {
            // Optional line: configs written by older versions simply lack it.
            bool enabled = pingEnabled;
            HudElement element = pingHud;
            if (ss >> enabled >> element.x >> element.y >> element.scale)
            {
                pingEnabled = enabled;
                pingHud = element;
            }
        }
        else if (key == L"HEALTHWARNING")
        {
            bool enabled = healthWarningEnabled, pulse = healthWarningPulse;
            int at = healthWarningAt, strength = healthWarningStrength;
            if (ss >> enabled) healthWarningEnabled = enabled;
            if (ss >> at) healthWarningAt = std::clamp(at, 1, 20);
            if (ss >> strength) healthWarningStrength = std::clamp(strength, 20, 100);
            if (ss >> pulse) healthWarningPulse = pulse;
            int index = 0;
            GlassHsv hsv{};
            if (ss >> index >> hsv.h >> hsv.s >> hsv.v)   // optional: absent before the colour setting
            {
                healthWarningIndex = std::clamp(index, 0, kHealthPaletteCount);
                healthWarningHsv = {std::clamp(hsv.h, 0, 359), std::clamp(hsv.s, 0, 100), std::clamp(hsv.v, 0, 100)};
            }
        }
        else if (key == L"BLOCKDETAILS")
        {
            bool enabled = blockDetailsEnabled, source = blockDetailsSource, tool = blockDetailsTool;
            HudElement element = blockDetailsHud;
            if (ss >> enabled >> element.x >> element.y >> element.scale)
            {
                blockDetailsEnabled = enabled;
                blockDetailsHud = element;
                if (ss >> source >> tool) { blockDetailsSource = source; blockDetailsTool = tool; }
            }
        }
        else if (key == L"DEBUGMENU")
        {
            bool on = true;
            if (ss >> on) debugMenuEnabled = on;
            int size = 100;
            if (ss >> size) debugTextSize = std::clamp(size, 50, 200);   // optional: absent before Text size
        }
        else if (key == L"DAYTIMER")
        {
            bool enabled = dayTimerEnabled, both = dayTimerBoth;
            HudElement element = dayTimerHud;
            if (ss >> enabled >> element.x >> element.y >> element.scale)
            {
                dayTimerEnabled = enabled;
                dayTimerHud = element;
                if (ss >> both) dayTimerBoth = both;
            }
        }
        else if (key == L"FEATURELIST")
        {
            bool enabled = featureListEnabled;
            HudElement element = featureListHud;
            if (ss >> enabled >> element.x >> element.y >> element.scale)
            {
                featureListEnabled = enabled;
                featureListHud = element;
            }
        }
        else if (key == L"HIVE")
        {
            bool enabled = hiveEnabled;
            if (ss >> enabled) hiveEnabled = enabled;
        }
        else if (key == L"SHULKER")
        {
            bool enabled = shulkerEnabled;
            HudElement element = shulkerHud;
            if (ss >> enabled >> element.x >> element.y >> element.scale)
            {
                shulkerEnabled = enabled;
                shulkerHud = element;
            }
        }
        else if (key == L"ZOOMLEVEL")
        {
            int level = 300;
            if (ss >> level && level >= 120 && level <= 2000)
                zoomFactorX100 = level;
        }
        else if (key == L"TOGGLESOUNDMODE")
        {
            // Older configs (before Menu Sounds became a module): 2 meant "no sound".
            int mode = 0;
            if (ss >> mode)
            {
                menuSoundsEnabled = mode != 2;
                toggleSoundMode = mode == 1 ? 1 : 0;
            }
        }
        else if (key == L"LEFTHAND")
        {
            bool on = false;
            if (ss >> on) leftHandEnabled = on;
        }
        else if (key == L"HITEFFECT")
        {
            bool on = false, magic = true;
            if (ss >> on >> magic) { hitEffectEnabled = on; hitEffectMagic = magic; }
            bool crit = true;
            if (ss >> crit) hitEffectCrit = crit;   // optional: absent before the split switches
        }
        else if (key == L"FRAMEGEN")
        {
            bool on = false, pacing = true;
            int multiplier = 0, method = 0;
            if (ss >> on >> multiplier >> method >> pacing)
            {
                // Older files kept 0 (x2) / 1 (x3) here.
                frameGenMultiplier = multiplier <= 1 ? 2 + std::max(multiplier, 0)
                                                     : std::min(multiplier, (int)MyClientFeatures::FrameGenMaxMultiplier);
                frameGenEnabled = on; frameGenPacing = pacing;
                frameGenMethod = method >= 0 && method <= 2 ? method : 0;
            }
        }
        else if (key == L"COUNTERBG")
        {
            bool resources = false, arrows = false;
            if (ss >> resources >> arrows)
            {
                resourceBackground = resources;
                arrowBackground = arrows;
                bool resourcesSee = false, arrowsSee = false;
                if (ss >> resourcesSee >> arrowsSee)   // absent in older configs
                {
                    resourceTranslucent = resourcesSee;
                    arrowTranslucent = arrowsSee;
                }
            }
        }
        else if (key == L"ARMORHUD")
        {
            bool durability = true, horizontal = true;
            if (ss >> durability >> horizontal)
            {
                armorShowDurability = durability;
                armorHorizontal = horizontal;
            }
        }
        else if (key == L"FASTINV")
        {
            bool on = false;
            if (ss >> on) fastInventoryEnabled = on;
        }
        else if (key == L"MENUSOUNDS")
        {
            bool on = menuSoundsEnabled;
            int mode = toggleSoundMode;
            if (ss >> on >> mode)
            {
                menuSoundsEnabled = on;
                toggleSoundMode = (mode >= 0 && mode <= 2) ? mode : 0;
            }
        }
        else if (key == L"LITE")
        {
            bool on = false, c = true, k = true, f = true, w = true;
            if (ss >> on >> c >> k >> f >> w)
            {
                liteEnabled = on; liteClouds = c; liteSky = k; liteFog = f; liteWeather = w;
                int distance = 64; bool players = false, limit = false;   // optional: absent in older configs
                if (ss >> distance >> players)
                {
                    liteEntityLimit = distance > 0;   // the first test build stored "0" for off
                    if (ss >> limit) liteEntityLimit = limit;
                    bool render = false; int chunks = 12;   // optional: absent in configs from before Render Distance
                    if (ss >> render >> chunks)
                    {
                        liteRenderEnabled = render;
                        liteRenderDistance = std::clamp(chunks / 2 * 2, 4, 48);
                        bool noCrit = false, noPotion = false;   // optional: absent in older configs
                        if (ss >> noCrit >> noPotion) { liteNoCrit = noCrit; liteNoPotion = noPotion; }
                    }
                    liteEntityDistance = std::clamp(distance / 8 * 8, 8, 128);
                    // Older configs: "Hide far players too" used the entity distance (LITE2 overrides this).
                    litePlayerLimit = players && liteEntityLimit;
                    litePlayerDistance = liteEntityDistance;
                }
            }
        }
        else if (key == L"LITE2")
        {
            bool particles = false, shadows = false, tags = false, mobs = true, items = true, misc = true, players = false;
            int playerDistance = 64;
            bool priority = false, throttle = false, gpu = false;
            if (ss >> particles >> shadows >> tags >> mobs >> items >> misc >> players >> playerDistance >> priority >> throttle >> gpu)
            {
                liteNoParticles = particles; liteNoShadows = shadows; liteNoNameTags = tags;
                liteCullMobs = mobs; liteCullItems = items; liteCullMisc = misc;
                litePlayerLimit = players; litePlayerDistance = std::clamp(playerDistance / 8 * 8, 8, 128);
                liteHighPriority = priority; liteNoThrottle = throttle; liteGpuPriority = gpu;
            }
        }
        else if (key == L"REPLAY")
        {
            // Recording never starts by itself after a restart: the switch always begins OFF.
            replayEnabled = false;
        }
        else if (key == L"NICK")
        {
            bool enabled = nickEnabled;
            if (ss >> enabled)
            {
                nickEnabled = enabled;
                std::wstring name;
                std::getline(ss, name);
                if (!name.empty() && name[0] == L' ')
                    name.erase(0, 1);
                if (!name.empty())
                    nickText = name.substr(0, 24);
            }
        }
        else if (key == L"HITBOX")
        {
            bool enabled = hitboxEnabled;
            if (ss >> enabled)
            {
                hitboxEnabled = enabled;
                int filter = hitboxFilter;
                if (ss >> filter)
                    hitboxFilter = filter & 0xF;
                for (int k = 0; k < 4; k++)   // optional: absent in configs from before the hitbox colours
                {
                    int index = 0;
                    GlassHsv hsv{};
                    if (!(ss >> index >> hsv.h >> hsv.s >> hsv.v))
                        break;
                    hitboxColorIndex[k] = std::clamp(index, 0, kHudInkCount + 1);
                    hitboxHsv[k] = {std::clamp(hsv.h, 0, 359), std::clamp(hsv.s, 0, 100), std::clamp(hsv.v, 0, 100)};
                }
            }
        }
        else if (key == L"CHUNKBORDER")
        {
            bool enabled = chunkBorderEnabled;
            int radius = chunkBorderRadius;
            if (ss >> enabled >> radius)
            {
                chunkBorderEnabled = enabled;
                chunkBorderRadius = std::clamp(radius, 1, 3);
            }
        }
        else if (key == L"TABLIST")
        {
            bool enabled = tabListEnabled;
            HudElement element = tabListHud;
            if (ss >> enabled >> element.x >> element.y >> element.scale)
            {
                tabListEnabled = enabled;
                tabListHud = element;
            }
        }
        else if (key == L"HIVESTATS")
        {
            bool enabled = hiveStatsEnabled;
            HudElement element = hiveStatsHud;
            int game = hiveStatsGame;
            if (ss >> enabled >> element.x >> element.y >> element.scale >> game)
            {
                bool autoGame = true;
                if (ss >> autoGame)
                    hiveStatsAuto = autoGame;
                hiveStatsEnabled = enabled;
                hiveStatsHud = element;
                hiveStatsGame = std::clamp(game, 0, HiveStats::kGameCount - 1);
            }
        }
        else if (key == L"CLIENT")
        {
            std::getline(
                ss,
                clientName
            );

            if (!clientName.empty() &&
                clientName.front() == L' ')
            {
                clientName.erase(
                    clientName.begin()
                );
            }

            // Configs written before the rename still say "MyClient": use the new name.
            if (clientName == L"MyClient" || clientName.empty())
                clientName = L"OriginX Client";
        }
        else if (key == L"GUI_SCALE")
        {
            ss >> guiScale;

            guiScale =
                std::clamp(
                    guiScale,
                    0.75f,
                    1.5f
                );
        }
        else if (key == L"MENU_ANIMATION")
        {
            ss >> menuAnimation;
        }
        else if (key == L"GLASS_DARK")
        {
            ss >> glassDark;
        }
        else if (key == L"HUD_COLORS")
        {
            int id = 0;
            HudLook read;
            ss >> id >> read.border;
            for (int& i : read.index) ss >> i;
            for (GlassHsv& c : read.custom) ss >> c.h >> c.s >> c.v;
            if (ss && id > 0 && id < kHudLookCount)
            {
                for (int i = 0; i < kHudPartCount; i++)
                {
                    read.index[i] = std::clamp(read.index[i], 0, (int)HudPalette((HudElementId)id, (HudPart)i).size());
                    read.custom[i] = {std::clamp(read.custom[i].h, 0, 359), std::clamp(read.custom[i].s, 0, 100),
                                      std::clamp(read.custom[i].v, 0, 100)};
                }
                int box = -1;
                bool see = false;
                if (ss >> box >> see)   // older lines: no background switches
                {
                    read.box = std::clamp(box, -1, 1);
                    read.see = see;
                }
                hudLooks[id] = read;
            }
        }
        else if (key == L"GLASS_LOOK")
        {
            ss >> glassAccentIndex >> glassLightIndex >> glassDarkIndex >> glassShadow >> glassOpacity;
            for (GlassHsv& c : glassCustom)
            {
                GlassHsv read = c;
                if (!(ss >> read.h >> read.s >> read.v)) break;   // older configs: no colours of your own
                c = {std::clamp(read.h, 0, 359), std::clamp(read.s, 0, 100), std::clamp(read.v, 0, 100)};
            }
            glassAccentIndex = std::clamp(glassAccentIndex, 0, kGlassAccentCount);   // == count: your own colour
            glassLightIndex = std::clamp(glassLightIndex, 0, kGlassLightCount);
            glassDarkIndex = std::clamp(glassDarkIndex, 0, kGlassDarkCount);
            glassShadow = std::clamp(glassShadow, 0, 100);
            glassOpacity = std::clamp(glassOpacity, 20, 100);
        }
        else if (key == L"PINNED")
        {
            pinnedFeatures.clear();
            int value = 0;
            while (ss >> value)
                if (value > (int)DetailFeature::None && value <= (int)DetailFeature::HealthWarning &&
                    std::find(pinnedFeatures.begin(), pinnedFeatures.end(), (DetailFeature)value) == pinnedFeatures.end())
                    pinnedFeatures.push_back((DetailFeature)value);
        }
        else if (key == L"FAVORITE")
        {
            // Favourites were removed; older configs still carry these lines.
        }
        else if (key == L"THEME")
        {
            themeIndex = 0;   // old configs stored a theme number; the colour scheme is fixed now
        }
    }
}


void ResetHudLayout()
{
    keystrokesHud = {
        25,
        130,
        1.0f
    };

    cpsHud = {
        25,
        235,
        1.0f
    };

    armorHud = {
        25,
        315,
        1.0f
    };

    arrowHud = {
        25,
        455,
        1.0f
    };

    fpsHud = {
        25,
        505,
        1.0f
    };

    resourceHud = {
        25,
        555,
        1.0f
    };

    pingHud = {
        190,
        505,
        1.0f
    };

    dayTimerHud = {
        190,
        455,
        1.0f
    };

    blockDetailsHud = {
        810,
        12,
        1.0f
    };

    featureListHud = {
        450,
        25,
        1.0f
    };

    shulkerHud = {
        700,
        120,
        1.0f
    };

    tabListHud = {
        1000,
        120,
        1.0f
    };

    hiveStatsHud = {
        20,
        200,
        1.0f
    };

    SaveConfig();
}


// ------------------------- Input -----------------------------

bool IsKeyDown(int key)
{
    return (
        GetAsyncKeyState(key)
        & 0x8000
    ) != 0;
}

bool IsZoomRequested()
{
    return zoomEnabled && !menuOpen && !hudEditMode &&
           GetForegroundWindow() == minecraftWindow &&
           shortcuts.zoom != 0 &&
           IsKeyDown(shortcuts.zoom);
}


void ShowMouseCursor()
{
    while (ShowCursor(TRUE) < 0)
    {
    }

    ClipCursor(nullptr);
}


void HideMouseCursor()
{
    while (ShowCursor(FALSE) >= 0)
    {
    }

    ClipCursor(nullptr);
}


// ------------------------- Minecraft window -----------------

// The launcher pins the HUD to the exact game process it started/verified.
DWORD requestedMinecraftPid = 0;

BOOL CALLBACK FindRequestedMinecraft(HWND window, LPARAM parameter)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    wchar_t title[256]{};
    GetWindowTextW(window, title, 256);
    if (pid == requestedMinecraftPid && IsWindowVisible(window) &&
        std::wstring(title) == L"Minecraft")
    {
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }
    return TRUE;
}

HWND FindMinecraft()
{
    if (requestedMinecraftPid)
    {
        HWND found = nullptr;
        EnumWindows(FindRequestedMinecraft, reinterpret_cast<LPARAM>(&found));
        return found;
    }
    HWND hwnd =
        FindWindowW(
            nullptr,
            L"Minecraft"
        );

    if (hwnd &&
        IsWindowVisible(hwnd))
    {
        return hwnd;
    }

    return nullptr;
}


// ------------------------- CPS ------------------------------

void CleanClicks(
    std::vector<
        std::chrono::steady_clock::time_point
    >& clicks)
{
    auto limit =
        std::chrono::steady_clock::now()
        - std::chrono::seconds(1);

    clicks.erase(
        std::remove_if(
            clicks.begin(),
            clicks.end(),
            [limit](const auto& t)
            {
                return t < limit;
            }
        ),
        clicks.end()
    );
}


void AddLeftClick()
{
    leftClicks.push_back(
        std::chrono::steady_clock::now()
    );
}


void AddRightClick()
{
    rightClicks.push_back(
        std::chrono::steady_clock::now()
    );
}


int GetLeftCPS()
{
    CleanClicks(leftClicks);

    return static_cast<int>(
        leftClicks.size()
    );
}


int GetRightCPS()
{
    CleanClicks(rightClicks);

    return static_cast<int>(
        rightClicks.size()
    );
}


// Fonts used for drawing text, created once per (size, face) and kept for the life of the HUD
// (creating and deleting a font for every string cost more than drawing the text itself).
HFONT UiFont(int size, const wchar_t* face = L"Segoe UI")
{
    static std::map<std::pair<int, std::wstring>, HFONT> fonts;
    HFONT& font = fonts[{size, face}];
    if (!font)
        font = CreateFontW(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    return font;
}

// ------------------------- Drawing ---------------------------

uint32_t* DibPixels(HDC hdc, int& width, int& height);
// Glass pages: remembers where text of colour `color` goes (device pixels), so its anti-aliased edge can
// be made see-through like the glass under it (OverlayAlpha::SoftenInk). The DC's font is the text's.
void NoteInk(HDC hdc, int x, int y, const std::wstring& text, COLORREF color)
{
    int width = 0, height = 0;
    if (!DibPixels(hdc, width, height) || text.empty()) return;
    SIZE size{};
    if (!GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size)) return;
    POINT p[2] = {{x - 1, y - 1}, {x + size.cx + 2, y + size.cy + 1}};
    LPtoDP(hdc, p, 2);
    textInks.push_back({(int)p[0].x, (int)p[0].y, (int)p[1].x, (int)p[1].y,
                        ((uint32_t)GetRValue(color) << 16) | ((uint32_t)GetGValue(color) << 8) | GetBValue(color)});
}

// Glass pages: is the pixel at (x, y) (logical) the blue of a switched-on row / accent button? White text
// there stays white instead of turning into the light glass's dark text.
bool OnGlassAccent(HDC hdc, int x, int y)
{
    int width = 0, height = 0;
    const uint32_t* px = DibPixels(hdc, width, height);
    if (!px) return false;
    POINT p{x, y};
    LPtoDP(hdc, &p, 1);
    if (p.x < 0 || p.y < 0 || p.x >= width || p.y >= height) return false;
    const uint32_t accent = ((uint32_t)GetRValue(GlassAccent()) << 16) | ((uint32_t)GetGValue(GlassAccent()) << 8) | GetBValue(GlassAccent());
    return (px[(size_t)p.y * width + p.x] & 0xFFFFFFu) == accent;
}

void DrawTextSimple(
    HDC hdc,
    int x,
    int y,
    const std::wstring& text,
    COLORREF color,
    int size = 18)
{
    if (!(glassContent && menuDrawing && OnGlassAccent(hdc, x - 3, y + size / 2)))
        color = MenuTextColor(color);

    if (size < 1)
        size = 1;

    HFONT font =
        UiFont(size);

    HFONT oldFont =
        (HFONT)SelectObject(
            hdc,
            font
        );

    SetTextColor(
        hdc,
        color
    );

    SetBkMode(
        hdc,
        TRANSPARENT
    );

    if (inkTracking)
        NoteInk(hdc, x, y, text, color);

    TextOutW(
        hdc,
        x,
        y,
        text.c_str(),
        static_cast<int>(
            text.size()
        )
    );

    SelectObject(
        hdc,
        oldFont
    );

}


void DrawBox(
    HDC hdc,
    int x,
    int y,
    int w,
    int h,
    COLORREF color)
{
    color = MenuBoxColor(color);

    HBRUSH brush =
        CreateSolidBrush(color);

    RECT r{
        x,
        y,
        x + w,
        y + h
    };

    FillRect(
        hdc,
        &r,
        brush
    );

    DeleteObject(brush);
}


// Fold arrow: a thin chevron drawn with lines (same look on every font), pointing up when the
// section is open and down when it is folded. (cx, cy) is its centre. Both arms are 45-degree strokes
// drawn outward from the tip, so they rasterise as exact mirror images (no lopsided arm).
void DrawLineSmooth(HDC hdc, float x0, float y0, float x1, float y1, float thick, COLORREF color);   // defined below

// Fold arrow (up / down): two smooth 2 px strokes at 45 degrees, mirror-symmetric about cx.
void DrawChevron(HDC hdc, int cx, int cy, bool up, COLORREF color, int size = 4)
{
    const float fx = cx + 0.5f, fy = cy + 0.5f, arm = size + 1.0f;
    const float dir = up ? 1.0f : -1.0f;               // arms go down from an up-tip, up from a down-tip
    const float tipY = fy - dir * arm / 2;
    DrawLineSmooth(hdc, fx, tipY, fx - arm, tipY + dir * arm, 2.0f, color);
    DrawLineSmooth(hdc, fx, tipY, fx + arm, tipY + dir * arm, 2.0f, color);
}


// Right-pointing arrow ("from -> to"), same smooth 2 px strokes as the chevron. (x, cy) = left end, middle.
void DrawRightArrow(HDC hdc, int x, int cy, COLORREF color, int length = 16)
{
    const float y = cy + 1.0f, tip = (float)(x + length);
    DrawLineSmooth(hdc, (float)x, y, tip, y, 2.0f, color);
    DrawLineSmooth(hdc, tip, y, tip - 5.5f, y - 5.5f, 2.0f, color);
    DrawLineSmooth(hdc, tip, y, tip - 5.5f, y + 5.5f, 2.0f, color);
}

// Offset from DrawTextSimple's y to the middle of the capitals / digits at `size`, from the real font
// metrics, so a drawn symbol lines up with the text on any font.
int TextMiddle(HDC hdc, int size)
{
    HFONT font = UiFont(size);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    int cap = tm.tmAscent * 7 / 10;   // fallback when the outline metrics are unavailable
    OUTLINETEXTMETRICW otm{};
    otm.otmSize = sizeof(otm);
    if (GetOutlineTextMetricsW(hdc, sizeof(otm), &otm) && otm.otmsCapEmHeight > 0)
        cap = (int)otm.otmsCapEmHeight;
    SelectObject(hdc, oldFont);
    return tm.tmAscent - cap / 2;
}

// Width of `text` in the font DrawTextSimple uses at `size`.
int TextWidth(HDC hdc, const std::wstring& text, int size)
{
    HFONT font = UiFont(size);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE extent{};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &extent);
    SelectObject(hdc, oldFont);
    return extent.cx;
}


void DrawOutline(
    HDC hdc,
    int x,
    int y,
    int w,
    int h,
    COLORREF color)
{
    HPEN pen =
        CreatePen(
            PS_SOLID,
            2,
            color
        );

    HPEN oldPen =
        (HPEN)SelectObject(
            hdc,
            pen
        );

    HBRUSH oldBrush =
        (HBRUSH)SelectObject(
            hdc,
            GetStockObject(
                HOLLOW_BRUSH
            )
        );

    Rectangle(
        hdc,
        x,
        y,
        x + w,
        y + h
    );

    SelectObject(
        hdc,
        oldBrush
    );

    SelectObject(
        hdc,
        oldPen
    );

    DeleteObject(pen);
}


// ---- Menu themes: each theme is just a background + an accent colour;
// every other menu colour is derived from those two.
struct MenuTheme
{
    const wchar_t* name;
    COLORREF bg;
    COLORREF accent;
    bool light;   // light background (dark text) instead of the usual dark theme
};

// One fixed colour scheme (navy background, sky-blue accent). The theme switcher was removed;
// only per-feature colours (block outline, resources) are still configurable.
const MenuTheme kThemes[] =
{
    { L"Blue", RGB(8,14,26), RGB(34,150,240), false }
};

const int kThemeCount = 1;

COLORREF MixColor(
    COLORREF a,
    COLORREF b,
    float t)
{
    auto ch =
        [&](int (*get)(COLORREF))
        {
            return (int)(
                get(a) +
                (get(b) - get(a)) * t
            );
        };

    return RGB(
        ch([](COLORREF c) { return (int)GetRValue(c); }),
        ch([](COLORREF c) { return (int)GetGValue(c); }),
        ch([](COLORREF c) { return (int)GetBValue(c); })
    );
}

struct ThemeColors
{
    COLORREF bg;
    COLORREF card;
    COLORREF tile;
    COLORREF button;
    COLORREF accent;
    COLORREF accentHi;
    COLORREF text;
    COLORREF muted;
    COLORREF off;
    COLORREF onAccent;   // text colour used on top of the accent colour
};

ThemeColors CurrentTheme()
{
    if (glassContent)
    {
        const GlassColors c = GlassTheme();
        ThemeColors t{};
        t.bg = c.panel; t.card = c.fill; t.tile = c.fill; t.button = c.fill;
        t.accent = GlassAccent(); t.accentHi = GlassAccent();
        t.text = c.text; t.muted = c.muted; t.off = c.track; t.onAccent = RGB(255,255,255);
        return t;
    }
    const MenuTheme& m =
        kThemes[
            std::clamp(
                themeIndex,
                0,
                kThemeCount - 1
            )
        ];

    ThemeColors t{};

    if (m.light)
    {
        t.bg = m.bg;
        t.card = MixColor(m.bg, m.accent, 0.08f);
        t.tile = MixColor(m.bg, m.accent, 0.20f);
        t.button = MixColor(m.bg, m.accent, 0.13f);
        t.accent = m.accent;
        t.accentHi = m.accent;
        t.text = RGB(28,30,40);
        t.muted = MixColor(m.bg, RGB(0,0,0), 0.50f);
        t.off = MixColor(m.bg, RGB(110,110,125), 0.28f);
        t.onAccent = RGB(255,255,255);
        return t;
    }

    t.bg = m.bg;
    t.card = MixColor(m.bg, m.accent, 0.13f);
    t.tile = MixColor(m.bg, m.accent, 0.32f);
    t.button = MixColor(m.bg, m.accent, 0.24f);
    t.accent = m.accent;
    t.accentHi = MixColor(m.accent, RGB(255,255,255), 0.45f);
    t.text = MixColor(RGB(255,255,255), m.accent, 0.08f);
    t.muted = MixColor(m.bg, RGB(255,255,255), 0.55f);
    t.off = MixColor(m.bg, RGB(150,150,160), 0.30f);

    // Bright accents (white, yellow) need dark text on top of them.
    const int luminance =
        (299 * GetRValue(m.accent) +
         587 * GetGValue(m.accent) +
         114 * GetBValue(m.accent)) / 1000;

    t.onAccent =
        luminance > 150
            ? RGB(18,16,24)
            : RGB(255,255,255);

    return t;
}


bool MenuLightMode()
{
    return menuDrawing && !glassContent &&
           kThemes[
               std::clamp(
                   themeIndex,
                   0,
                   kThemeCount - 1
               )
           ].light;
}

// Menu text colours are written for dark themes (white / light grey).
// On a light theme they are flipped to dark so they stay readable.
COLORREF MenuTextColor(COLORREF c)
{
    if (menuDrawing && glassContent)
    {
        // The pages' colours were written for a dark menu: on light glass, light greys turn into the glass
        // text colours and other colours darken a little. Dark glass keeps them.
        if (glassDark) return c;
        const GlassColors g = GlassTheme();
        const int r = GetRValue(c), gr = GetGValue(c), b = GetBValue(c);
        if (abs(r - gr) < 24 && abs(gr - b) < 24 && abs(r - b) < 24)
            return r >= 170 ? g.text : (r >= 90 ? g.muted : c);
        return RGB(r * 70 / 100, gr * 70 / 100, b * 70 / 100);
    }
    if (!MenuLightMode())
        return c;

    const int r = GetRValue(c);
    const int g = GetGValue(c);
    const int b = GetBValue(c);

    const bool grey =
        abs(r - g) < 24 &&
        abs(g - b) < 24 &&
        abs(r - b) < 24;

    if (grey)
    {
        const int v = std::max(28, 255 - r);
        return RGB(v, v, std::min(255, v + 6));
    }

    return RGB(r * 55 / 100, g * 55 / 100, b * 55 / 100);
}

COLORREF MenuBoxColor(COLORREF c)
{
    return MenuLightMode()
               ? MixColor(c, RGB(255,255,255), 0.80f)
               : c;
}


// Gaming-purple menu theme. The menu is drawn on a magenta colour-key buffer,
// so rounded corners simply show the game behind the overlay.
// The overlay's back buffer (32-bit, top-down), registered by the paint code before it draws.
// DrawRoundBox blends its corners straight into these pixels; on any other DC it falls back to GDI.
HDC smoothDc = nullptr;
uint32_t* smoothBits = nullptr;
int smoothW = 0, smoothH = 0;
uint32_t* DibPixels(HDC hdc, int& width, int& height)
{
    if (hdc != smoothDc || !smoothBits)
        return nullptr;
    width = smoothW;
    height = smoothH;
    return smoothBits;
}

// Blends `color` into one back-buffer pixel with coverage `a` (0..255). Over the transparent key colour
// there is nothing to blend with: the pixel is filled when at least half covered.
void BlendPixel(uint32_t* px, int width, int height, int x, int y, COLORREF color, uint32_t a)
{
    if (a == 0 || x < 0 || y < 0 || x >= width || y >= height) return;
    uint32_t& d = px[(size_t)y * width + x];
    const uint32_t fr = GetRValue(color), fg = GetGValue(color), fb = GetBValue(color);
    if ((d & 0x00FFFFFFu) == 0x00FF00FFu)
    {
        if (a >= 128) d = (fr << 16) | (fg << 8) | fb;
        return;
    }
    const uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    d = (((fr * a + dr * (255 - a)) / 255) << 16) | (((fg * a + dg * (255 - a)) / 255) << 8) | ((fb * a + db * (255 - a)) / 255);
}

// Smooth filled triangle (logical coordinates, may be fractional). Falls back to a GDI polygon.
void FillTriangleSmooth(HDC hdc, float x0, float y0, float x1, float y1, float x2, float y2, COLORREF color)
{
    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    if (!px)
    {
        HBRUSH b = CreateSolidBrush(color);
        HGDIOBJ ob = SelectObject(hdc, b), op = SelectObject(hdc, GetStockObject(NULL_PEN));
        POINT tri[3] = {{(LONG)std::lround(x0), (LONG)std::lround(y0)}, {(LONG)std::lround(x1), (LONG)std::lround(y1)},
                        {(LONG)std::lround(x2), (LONG)std::lround(y2)}};
        Polygon(hdc, tri, 3);
        SelectObject(hdc, ob); SelectObject(hdc, op); DeleteObject(b);
        return;
    }
    POINT o{0, 0};
    LPtoDP(hdc, &o, 1);
    const float ox = (float)o.x, oy = (float)o.y;
    x0 += ox; x1 += ox; x2 += ox; y0 += oy; y1 += oy; y2 += oy;
    const auto edge = [](float ax, float ay, float bx, float by, float x, float y) { return (bx - ax) * (y - ay) - (by - ay) * (x - ax); };
    const float area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    const int left = (int)std::floor(std::min({x0, x1, x2})), right = (int)std::ceil(std::max({x0, x1, x2}));
    const int top = (int)std::floor(std::min({y0, y1, y2})), bottom = (int)std::ceil(std::max({y0, y1, y2}));
    constexpr int kSub = 4;
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++)
        {
            int inside = 0;
            for (int sy = 0; sy < kSub; sy++)
                for (int sx = 0; sx < kSub; sx++)
                {
                    const float qx = x + (sx + 0.5f) / kSub, qy = y + (sy + 0.5f) / kSub;
                    const float w0 = edge(x1, y1, x2, y2, qx, qy), w1 = edge(x2, y2, x0, y0, qx, qy), w2 = edge(x0, y0, x1, y1, qx, qy);
                    if (area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0)) inside++;
                }
            BlendPixel(px, width, height, x, y, color, inside * 255 / (kSub * kSub));
        }
}

// Smooth filled polygon (logical coordinates, any shape; even-odd rule). Needs the back buffer.
void FillPolygonSmooth(HDC hdc, const float* xs, const float* ys, int n, COLORREF color)
{
    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    if (!px || n < 3)
        return;
    POINT o{0, 0};
    LPtoDP(hdc, &o, 1);
    std::vector<float> X(xs, xs + n), Y(ys, ys + n);
    for (int i = 0; i < n; i++) { X[i] += o.x; Y[i] += o.y; }
    const int left = (int)std::floor(*std::min_element(X.begin(), X.end())), right = (int)std::ceil(*std::max_element(X.begin(), X.end()));
    const int top = (int)std::floor(*std::min_element(Y.begin(), Y.end())), bottom = (int)std::ceil(*std::max_element(Y.begin(), Y.end()));
    constexpr int kSub = 4;
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++)
        {
            int inside = 0;
            for (int sy = 0; sy < kSub; sy++)
                for (int sx = 0; sx < kSub; sx++)
                {
                    const float qx = x + (sx + 0.5f) / kSub, qy = y + (sy + 0.5f) / kSub;
                    bool in = false;
                    for (int i = 0, j = n - 1; i < n; j = i++)
                        if ((Y[i] > qy) != (Y[j] > qy) && qx < (X[j] - X[i]) * (qy - Y[i]) / (Y[j] - Y[i]) + X[i])
                            in = !in;
                    inside += in;
                }
            BlendPixel(px, width, height, x, y, color, inside * 255 / (kSub * kSub));
        }
}

// Smooth line with round ends, `thick` pixels wide (logical coordinates). Falls back to a GDI pen.
void DrawLineSmooth(HDC hdc, float x0, float y0, float x1, float y1, float thick, COLORREF color)
{
    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    if (!px)
    {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, (int)std::lround(thick)), color);
        HGDIOBJ op = SelectObject(hdc, pen);
        MoveToEx(hdc, (int)std::lround(x0), (int)std::lround(y0), nullptr);
        LineTo(hdc, (int)std::lround(x1), (int)std::lround(y1));
        SelectObject(hdc, op); DeleteObject(pen);
        return;
    }
    POINT o{0, 0};
    LPtoDP(hdc, &o, 1);
    x0 += o.x; x1 += o.x; y0 += o.y; y1 += o.y;
    const float r = thick / 2, dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    const int left = (int)std::floor(std::min(x0, x1) - r), right = (int)std::ceil(std::max(x0, x1) + r);
    const int top = (int)std::floor(std::min(y0, y1) - r), bottom = (int)std::ceil(std::max(y0, y1) + r);
    constexpr int kSub = 4;
    for (int y = top; y < bottom; y++)
        for (int x = left; x < right; x++)
        {
            int inside = 0;
            for (int sy = 0; sy < kSub; sy++)
                for (int sx = 0; sx < kSub; sx++)
                {
                    const float qx = x + (sx + 0.5f) / kSub, qy = y + (sy + 0.5f) / kSub;
                    float t = len2 > 0 ? ((qx - x0) * dx + (qy - y0) * dy) / len2 : 0;
                    t = std::clamp(t, 0.0f, 1.0f);
                    const float ex = qx - (x0 + t * dx), ey = qy - (y0 + t * dy);
                    if (ex * ex + ey * ey <= r * r) inside++;
                }
            BlendPixel(px, width, height, x, y, color, inside * 255 / (kSub * kSub));
        }
}

// Rounded box with smooth (anti-aliased) corners. The straight parts are plain GDI fills; each corner
// pixel is blended by how much of it lies inside the arc. Over the transparent key colour (magenta)
// there is nothing to blend with, so those pixels are either filled (half covered or more) or left.
void GlassBox(HDC hdc, const RECT& r, int radius, COLORREF color, uint8_t alpha);   // with the glass pages

void DrawRoundBox(
    HDC hdc,
    int x,
    int y,
    int w,
    int h,
    int radius,
    COLORREF fill)
{
    if (glassContent && menuDrawing)
    {
        // Glass pages: cards and buttons (the glass fill) and the blue ones become glass boxes like the Mods rows.
        const GlassColors c = GlassTheme();
        if (fill == RGB(24,92,168)) fill = GlassAccent();   // a switched-on row of the module list
        int width = 0, height = 0;
        if ((fill == c.fill || fill == GlassAccent()) && DibPixels(hdc, width, height))
        {
            POINT a{x, y}, b{x + w + 1, y + h + 1};
            LPtoDP(hdc, &a, 1);
            LPtoDP(hdc, &b, 1);
            POINT org{};
            SetViewportOrgEx(hdc, 0, 0, &org);   // GlassBox works in device pixels
            GlassBox(hdc, RECT{a.x, a.y, b.x, b.y}, radius, fill, fill == GlassAccent() ? kGlassAccentAlpha : c.fillA);
            SetViewportOrgEx(hdc, org.x, org.y, nullptr);
            return;
        }
    }
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    HBRUSH brush = CreateSolidBrush(fill);

    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    if (!px || radius < 2)
    {
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
        RoundRect(hdc, x, y, x + w + 1, y + h + 1, radius * 2, radius * 2);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(brush);
        return;
    }

    // Same footprint as the GDI RoundRect above (it fills one pixel past w and h).
    w += 1;
    h += 1;

    const RECT parts[3] = {
        {x + radius, y, x + w - radius, y + h},
        {x, y + radius, x + radius, y + h - radius},
        {x + w - radius, y + radius, x + w, y + h - radius},
    };
    for (const RECT& r : parts)
        if (r.right > r.left && r.bottom > r.top)
            FillRect(hdc, &r, brush);
    DeleteObject(brush);
    GdiFlush();

    // Corner squares, in device pixels (the menu draws with a viewport offset).
    POINT origin{x, y};
    LPtoDP(hdc, &origin, 1);
    const uint32_t fr = GetRValue(fill), fg = GetGValue(fill), fb = GetBValue(fill);
    const uint32_t key = 0x00FF00FFu;   // RGB(255,0,255) as stored in the DIB (0x00RRGGBB)

    // Coverage (0..255) of the top-left corner square, 4x4 samples per pixel; cached per radius and
    // mirrored for the other corners.
    static std::map<int, std::vector<uint8_t>> coverageByRadius;
    std::vector<uint8_t>& coverage = coverageByRadius[radius];
    if (coverage.empty())
    {
        constexpr int kSub = 4;
        coverage.resize((size_t)radius * radius);
        for (int j = 0; j < radius; j++)
            for (int i = 0; i < radius; i++)
            {
                int inside = 0;
                for (int sy = 0; sy < kSub; sy++)
                    for (int sx = 0; sx < kSub; sx++)
                    {
                        const float dx = radius - (i + (sx + 0.5f) / kSub), dy = radius - (j + (sy + 0.5f) / kSub);
                        if (dx * dx + dy * dy <= (float)radius * radius) inside++;
                    }
                coverage[(size_t)j * radius + i] = (uint8_t)(inside * 255 / (kSub * kSub));
            }
    }

    for (int corner = 0; corner < 4; corner++)
    {
        const bool right = corner & 1, bottom = corner & 2;
        const int left = origin.x + (right ? w - radius : 0);
        const int top = origin.y + (bottom ? h - radius : 0);
        for (int j = 0; j < radius; j++)
        {
            const int py = top + j;
            if (py < 0 || py >= height) continue;
            const int cj = bottom ? radius - 1 - j : j;
            for (int i = 0; i < radius; i++)
            {
                const int qx = left + i;
                if (qx < 0 || qx >= width) continue;
                const uint32_t a = coverage[(size_t)cj * radius + (right ? radius - 1 - i : i)];
                if (a == 0) continue;
                uint32_t& d = px[(size_t)py * width + qx];
                if ((d & 0x00FFFFFFu) == key)
                {
                    if (a >= 128) d = (fr << 16) | (fg << 8) | fb;
                    continue;
                }
                const uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
                d = (((fr * a + dr * (255 - a)) / 255) << 16) | (((fg * a + dg * (255 - a)) / 255) << 8) |
                    ((fb * a + db * (255 - a)) / 255);
            }
        }
    }
}


void DrawTextCentered(
    HDC hdc,
    int x,
    int y,
    int w,
    int h,
    const std::wstring& text,
    COLORREF color,
    int size,
    const wchar_t* face = L"Segoe UI")
{
    if (glassContent && menuDrawing && !OnGlassAccent(hdc, x + 3, y + h / 2))
        color = MenuTextColor(color);

    HFONT font =
        UiFont(size, face);

    HFONT oldFont =
        (HFONT)SelectObject(hdc, font);

    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);

    RECT r{x, y, x + w, y + h};
    if (inkTracking)
    {
        SIZE size{};
        if (GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size))
            NoteInk(hdc, x + (w - size.cx) / 2, y + (h - size.cy) / 2, text, color);
    }

    DrawTextW(
        hdc,
        text.c_str(),
        static_cast<int>(text.size()),
        &r,
        DT_CENTER | DT_VCENTER |
            DT_SINGLELINE | DT_NOPREFIX
    );

    SelectObject(hdc, oldFont);
}


void DrawButton(
    HDC hdc,
    int x,
    int y,
    int w,
    int h,
    const std::wstring& text,
    bool selected)
{
    const ThemeColors t = CurrentTheme();

    DrawRoundBox(
        hdc,
        x,
        y,
        w,
        h,
        10,
        selected
            ? t.accent
            : t.button
    );

    // Centred in the box; 17 px in tall buttons, smaller in short ones, shrunk further if the text is too wide.
    int size = h >= 40 ? 17 : h >= 32 ? 15 : 14;
    while (size > 10 && TextWidth(hdc, text, size) > w - 16)
        size--;
    DrawTextCentered(hdc, x, y, w, h, text, selected ? t.onAccent : t.text, size);
}


// Trash buttons (delete): the first click arms the button (it turns red), a second click within
// kTrashArmMs deletes. One armed item at a time, identified by a string such as "preset:PvP".
constexpr ULONGLONG kTrashArmMs = 4000;
std::wstring trashArmedId;
ULONGLONG trashArmedAt = 0;
bool TrashArmed(const std::wstring& id) { return trashArmedId == id && GetTickCount64() - trashArmedAt < kTrashArmMs; }
// True when this click confirms the delete; otherwise the button is armed and false is returned.
bool TrashConfirm(const std::wstring& id)
{
    if (TrashArmed(id)) { trashArmedId.clear(); return true; }
    trashArmedId = id;
    trashArmedAt = GetTickCount64();
    return false;
}
void DrawReplayIconButton(HDC hdc, const RECT& r, int kind, COLORREF fill, COLORREF ink);   // defined with the Replays panel
void DrawTrashButton(HDC hdc, const RECT& r, bool armed)
{
    const ThemeColors t = CurrentTheme();
    DrawReplayIconButton(hdc, r, 0, armed ? RGB(190,60,60) : t.button, armed ? RGB(255,255,255) : t.text);
}


// ------------------------- HUD drawing ----------------------

// A rounded box drawn hard-edged in `rgb` at device rect `r` (a pixel takes the colour when at least half
// of it is inside), for OverlayAlpha to find by colour and smooth (see OverlayAlpha::Glass).
void HardRoundBox(HDC hdc, uint32_t* px, int width, int height, const OverlayAlpha::Glass& glass, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    const int radius = glass.radius;
    const RECT middle{glass.left + radius, glass.top, glass.right - radius, glass.bottom};
    const RECT sides{glass.left, glass.top + radius, glass.right, glass.bottom - radius};
    FillRect(hdc, &middle, brush);
    FillRect(hdc, &sides, brush);
    DeleteObject(brush);
    GdiFlush();
    for (int corner = 0; corner < 4 && radius > 0; corner++)
    {
        const int left = (corner & 1) ? glass.right - radius : glass.left, top = (corner & 2) ? glass.bottom - radius : glass.top;
        for (int y = std::max(0, top); y < top + radius && y < height; y++)
            for (int x = std::max(0, left); x < left + radius && x < width; x++)
                if (OverlayAlpha::GlassCoverage(glass, x, y) >= 128)
                    px[(size_t)y * width + x] = glass.color;
    }
}

// HUD boxes and frames have smooth round corners: drawn hard-edged, then made solid / see-through and their
// corners blended by OverlayAlpha (Convert, SmoothGlass) like the glass pages. kHudClear at alpha 0 is
// "nothing": the inside of a frame (so its inner edge is smooth too) and the ground under text without a box
// (its anti-aliased edge then fades into the game instead of the colour key's magenta).
constexpr uint8_t kHudBoxAlpha = 150;
constexpr COLORREF kHudClear = RGB(3,1,2);
int HudRadius(float s) { return std::max(3, (int)std::lround(9 * s)); }

void HudSmoothBox(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color, uint8_t alpha)
{
    if (w <= 0 || h <= 0) return;
    if (overlayAlphaError)   // the colour key only: solid boxes, nothing for the clear ones
    {
        if (alpha > 0) DrawRoundBox(hdc, x, y, w - 1, h - 1, radius, color);
        return;
    }
    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    if (!px)
    {
        if (alpha > 0) DrawRoundBox(hdc, x, y, w - 1, h - 1, radius, color);
        return;
    }
    POINT o{x, y};
    LPtoDP(hdc, &o, 1);
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    const uint32_t rgb = ((uint32_t)GetRValue(color) << 16) | ((uint32_t)GetGValue(color) << 8) | GetBValue(color);
    const OverlayAlpha::Glass glass{o.x, o.y, o.x + w, o.y + h, radius, rgb, alpha};
    POINT org{};
    SetViewportOrgEx(hdc, 0, 0, &org);   // device pixels
    HardRoundBox(hdc, px, width, height, glass, color);
    SetViewportOrgEx(hdc, org.x, org.y, nullptr);
    translucentRects.push_back({glass.left, glass.top, glass.right, glass.bottom, rgb, alpha});
    glassShapes.push_back(glass);
}

// The frame of a HUD module whose Background is "Frame only" (default: the menu's accent colour): a 2 px
// rounded ring, nothing inside.
void DrawHudBorder(HDC hdc, HudElementId id, int x, int y, int w, int h, int radius)
{
    if (!HudFrameOnly(id))
        return;
    const int t = 2;
    HudSmoothBox(hdc, x, y, w, h, radius, HudColor(id, HudBorder, GlassAccent()), 255);
    HudSmoothBox(hdc, x + t, y + t, w - 2 * t, h - 2 * t, std::max(0, radius - t), kHudClear, 0);
}

// A box behind a HUD module (or one key) in `color`, see-through when its Translucent switch is on.
void FillHudBox(HDC hdc, HudElementId id, int x, int y, int w, int h, int radius, COLORREF color)
{
    HudSmoothBox(hdc, x, y, w, h, radius, color, HudSeeThrough(id) ? kHudBoxAlpha : 255);
}

// The module's Background: its box, its frame, or (None) only the clear ground under its text.
void DrawHudBox(HDC hdc, HudElementId id, int x, int y, int w, int h, int radius, COLORREF ownColor)
{
    if (HudBoxOn(id)) FillHudBox(hdc, id, x, y, w, h, radius, HudColor(id, HudBox, ownColor));
    else if (HudFrameOnly(id)) DrawHudBorder(hdc, id, x, y, w, h, radius);
    else HudSmoothBox(hdc, x, y, w, h, 0, kHudClear, 0);
}

// HUD text: with a 1 px shadow when there is no box behind it (straight over the game); on a translucent
// box its anti-aliased edge is made see-through too (OverlayAlpha::SoftenInk).
void DrawHudText(HDC hdc, HudElementId id, int x, int y, const std::wstring& text, COLORREF color, int size)
{
    if (!HudBoxOn(id)) DrawTextSimple(hdc, x + 1, y + 1, text, RGB(25,25,25), size);
    const bool was = inkTracking;
    inkTracking = was || HudSeeThrough(id);
    DrawTextSimple(hdc, x, y, text, color, size);
    inkTracking = was;
}
void DrawHudTextCentered(HDC hdc, HudElementId id, int x, int y, int w, int h, const std::wstring& text, COLORREF color, int size)
{
    if (!HudBoxOn(id)) DrawTextCentered(hdc, x + 1, y + 1, w, h, text, RGB(25,25,25), size);
    const bool was = inkTracking;
    inkTracking = was || HudSeeThrough(id);
    DrawTextCentered(hdc, x, y, w, h, text, color, size);
    inkTracking = was;
}

void DrawKeystrokes(HDC hdc)
{
    if (!keystrokesEnabled)
        return;

    float s =
        keystrokesHud.scale;

    int x =
        keystrokesHud.x;

    int y =
        keystrokesHud.y;

    int size =
        std::max(
            10,
            (int)(40 * s)
        );

    int gap =
        std::max(
            2,
            (int)(5 * s)
        );

    struct K
    {
        int px;
        int py;
        wchar_t c;
        int vk;
    };

    K keys[] =
    {
        {1,0,L'W','W'},
        {0,1,L'A','A'},
        {1,1,L'S','S'},
        {2,1,L'D','D'}
    };

    for (auto& k : keys)
    {
        int bx =
            x + k.px *
            (size + gap);

        int by =
            y + k.py *
            (size + gap);

        // a pressed key always shows, also without the box
        if (IsKeyDown(k.vk))
            FillHudBox(hdc, HudElementId::Keystrokes, bx, by, size, size, HudRadius(s),
                       HudColor(HudElementId::Keystrokes, HudPressed, RGB(70,150,220)));
        else
            DrawHudBox(hdc, HudElementId::Keystrokes, bx, by, size, size, HudRadius(s), RGB(35,35,35));

        DrawHudText(
            hdc,
            HudElementId::Keystrokes,
            bx + (int)(12 * s),
            by + (int)(8 * s),
            std::wstring(
                1,
                k.c
            ),
            HudColor(HudElementId::Keystrokes, HudText, RGB(255,255,255)),
            std::max(
                10,
                (int)(20 * s)
            )
        );
    }

    if (hudEditMode &&
        selectedHud ==
            HudElementId::Keystrokes)
    {
        DrawOutline(
            hdc,
            x,
            y,
            size * 3 +
                gap * 2,
            size * 2 +
                gap,
            RGB(80,180,255)
        );
    }
}


void DrawCPS(HDC hdc)
{
    if (!cpsEnabled)
        return;

    float s =
        cpsHud.scale;

    int x =
        cpsHud.x;

    int y =
        cpsHud.y;

    int w =
        std::max(
            60,
            (int)(165 * s)
        );

    int h =
        std::max(
            30,
            (int)(65 * s)
        );

    DrawHudBox(hdc, HudElementId::CPS, x, y, w, h, HudRadius(s), RGB(20,20,20));

    DrawHudText(hdc, HudElementId::CPS,
        x + (int)(12 * s),
        y + (int)(7 * s),
        L"LMB: " +
            std::to_wstring(
                GetLeftCPS()
            ),
        HudColor(HudElementId::CPS, HudText, RGB(255,255,255)),
        std::max(
            10,
            (int)(17 * s)
        )
    );

    DrawHudText(hdc, HudElementId::CPS,
        x + (int)(12 * s),
        y + (int)(34 * s),
        L"RMB: " +
            std::to_wstring(
                GetRightCPS()
            ),
        HudColor(HudElementId::CPS, HudText, RGB(255,255,255)),
        std::max(
            10,
            (int)(17 * s)
        )
    );

    if (hudEditMode &&
        selectedHud ==
            HudElementId::CPS)
    {
        DrawOutline(
            hdc,
            x,
            y,
            w,
            h,
            RGB(80,180,255)
        );
    }
}


std::wstring ArmorText(unsigned slot)
{
    const auto armor = MinecraftDataProvider::GetArmor(slot);
    switch (armor.state) {
    case MyClientFeatures::Empty: return L"EMPTY";
    case MyClientFeatures::NoDurability: return L"N/A";
    case MyClientFeatures::Durable: return std::to_wstring(armor.remaining) + L"/" + std::to_wstring(armor.maximum);
    default: return L"---";
    }
}

// Game icons for the HUD (Armor HUD, Resource Counter): one cache, looked up in the player's active
// resource packs (same list the Textures page shows) before the game's own textures.
ItemIcons::Cache& HudIcons() { static ItemIcons::Cache icons; return icons; }

// The client's logo mark at `size` x `size`, decoded from the full-size PNG resource (RCDATA 3 in
// myclient.rc) with a high-quality filter: the .ico only has fixed sizes, and stretching those looks rough.
bool DrawLogo(HDC hdc, int x, int y, int size)
{
    static std::map<int, ItemIcons::Icon> bySize;
    ItemIcons::Icon& icon = bySize[size];
    if (!icon.tried)
    {
        icon.tried = true;
        HMODULE module = GetModuleHandleW(nullptr);
        if (HRSRC res = FindResourceW(module, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(10)))   // 10 = RT_RCDATA
            if (HGLOBAL mem = LoadResource(module, res))
                if (const void* data = LockResource(mem))
                    ItemIcons::DecodeWicMemory(data, SizeofResource(module, res), size, size, icon.image);
    }
    if (!ItemIcons::MakeBitmap(icon))
        return false;
    ItemIcons::Draw(hdc, icon, x, y, size);
    return true;
}
// Rebuilt only when the active list changes (it is asked for several times per HUD frame).
const std::vector<std::filesystem::path>& ActiveTexturePacks()
{
    if (!texScanned) RefreshTextures();
    static std::vector<std::string> builtFor;
    static size_t builtForPacks = 0;
    static std::vector<std::filesystem::path> packs;
    if (builtFor != texState.active || builtForPacks != texState.packs.size())
    {
        builtFor = texState.active;
        builtForPacks = texState.packs.size();
        packs.clear();
        for (const auto& id : texState.active)
            for (const auto& pack : texState.packs)
                if (pack.uuid == id) { packs.push_back(pack.folder); break; }
    }
    return packs;
}

// Armor HUD size before scaling, for drawing and for the HUD editor's hit test.
constexpr int kArmorIcon = 32, kArmorGap = 6;
void ArmorHudSize(int& w, int& h)
{
    if (armorHorizontal)
    {
        w = 4 * kArmorIcon + 3 * kArmorGap;
        h = kArmorIcon + (armorShowDurability ? 16 : 0);
    }
    else
    {
        w = kArmorIcon + (armorShowDurability ? 8 + 64 : 0);
        h = 4 * kArmorIcon + 3 * kArmorGap;
    }
}

int ArmorHudWidth() { int w = 0, h = 0; ArmorHudSize(w, h); return w; }
int ArmorHudHeight() { int w = 0, h = 0; ArmorHudSize(w, h); return h; }

// Durability colour: green above half, yellow above a quarter, red below.
COLORREF ArmorColor(const MyClientFeatures::Armor& armor)
{
    if (armor.state != MyClientFeatures::Durable || armor.maximum == 0) return RGB(230,230,230);
    const unsigned percent = armor.remaining * 100 / armor.maximum;
    return percent > 50 ? RGB(110,230,110) : percent > 25 ? RGB(240,210,80) : RGB(240,90,80);
}

// Text with a 1 px dark shadow, readable straight over the game (the Armor HUD has no box).
void DrawShadowText(HDC hdc, int x, int y, const std::wstring& text, COLORREF color, int size)
{
    DrawTextSimple(hdc, x + 1, y + 1, text, RGB(25,25,25), size);
    DrawTextSimple(hdc, x, y, text, color, size);
}

// Armor icons straight over the game (no box), in a row or a column. Each icon is the item's own texture
// (active texture packs first, then the game's), an empty slot shows the game's empty-slot outline.
// Durability: the remaining points under each icon in a row, "remaining/maximum" beside it in a column.
// Red pixel-art "!!" over the top-left corner of an armor icon (dark outline so it reads on any background).
void DrawLowDurabilityMark(HDC hdc, int ix, int iy, int icon)
{
    const int p = std::max(1, icon / 16);           // one texture pixel
    const int left = ix - p, top = iy - 2 * p;        // sticks out of the icon a little, like the game's badges
    const auto fill = [&](int px, int py, int pw, int ph, COLORREF color)
    {
        const RECT r{left + px * p, top + py * p, left + (px + pw) * p, top + (py + ph) * p};
        HBRUSH b = CreateSolidBrush(color);
        FillRect(hdc, &r, b);
        DeleteObject(b);
    };
    for (int mark = 0; mark < 2; mark++)
    {
        const int mx = mark * 3;                     // two marks, 3 texture pixels apart
        fill(mx, 0, 4, 7, RGB(60,8,8));              // outline of the bar
        fill(mx, 6, 4, 4, RGB(60,8,8));              // outline of the dot
        fill(mx + 1, 1, 2, 4, RGB(240,60,55));       // bar
        fill(mx + 1, 1, 1, 4, RGB(255,120,110));     // light edge
        fill(mx + 1, 7, 2, 2, RGB(240,60,55));       // dot
    }
}

void DrawCounterRowBackground(HDC hdc, HudElementId id, int x, int y, int contentW, int h, float s, bool filled, bool translucent);

void DrawArmor(HDC hdc)
{
    if (!armorHudEnabled)
        return;

    const float s = armorHud.scale;
    const int x = armorHud.x;
    const int y = armorHud.y;
    const int icon = std::max(8, (int)(kArmorIcon * s));
    const int gap = std::max(2, (int)(kArmorGap * s));
    const int textSize = std::max(9, (int)(13 * s));

    auto& icons = HudIcons();
    const std::wstring gameDir = MinecraftDataProvider::GameDirectory();
    const auto& activePacks = ActiveTexturePacks();

    {
        int w = 0, h = 0;
        ArmorHudSize(w, h);
        DrawCounterRowBackground(hdc, HudElementId::Armor, x, y, (int)(w * s), (int)(h * s), s, HudBoxOn(HudElementId::Armor),
                                 HudSeeThrough(HudElementId::Armor));
    }

    const char* emptySlots[4] = {"empty_armor_slot_helmet", "empty_armor_slot_chestplate", "empty_armor_slot_leggings", "empty_armor_slot_boots"};
    const wchar_t* labels[4] = {L"HEAD", L"CHEST", L"LEGS", L"FEET"};
    for (unsigned slot = 0; slot < 4; slot++)
    {
        const int ix = armorHorizontal ? x + (int)slot * (icon + gap) : x;
        const int iy = armorHorizontal ? y : y + (int)slot * (icon + gap);
        const auto armor = MinecraftDataProvider::GetArmor(slot);
        std::string item = MinecraftDataProvider::GetArmorItem(slot);
        if (item.empty()) item = emptySlots[slot];
        if (const ItemIcons::Icon* found = icons.Get(gameDir, activePacks, item))
            ItemIcons::Draw(hdc, *found, ix, iy, icon);
        else
            DrawShadowText(hdc, ix, iy + (icon - textSize) / 2, labels[slot], HudColor(HudElementId::Armor, HudText, RGB(230,230,230)),
                           std::max(8, textSize - 2));

        if (!armorShowDurability || armor.state != MyClientFeatures::Durable)
            continue;
        if (armor.maximum > 0 && armor.remaining * 5 < armor.maximum)   // under 20% left
            DrawLowDurabilityMark(hdc, ix, iy, icon);
        if (armorHorizontal)
        {
            const std::wstring text = std::to_wstring(armor.remaining);
            const int tx = ix + (icon - TextWidth(hdc, text, textSize)) / 2;
            DrawShadowText(hdc, tx, iy + icon, text, HudColor(HudElementId::Armor, HudText, ArmorColor(armor)), textSize);
        }
        else
        {
            DrawShadowText(hdc, ix + icon + (int)(8 * s), iy + (icon - textSize) / 2 - 1,
                           std::to_wstring(armor.remaining) + L"/" + std::to_wstring(armor.maximum),
                           HudColor(HudElementId::Armor, HudText, ArmorColor(armor)), textSize);
        }
    }

    if (hudEditMode && selectedHud == HudElementId::Armor)
    {
        int w = 0, h = 0;
        ArmorHudSize(w, h);
        DrawOutline(hdc, x, y, (int)(w * s), (int)(h * s), RGB(80,180,255));
    }
}

// Resource Counter size before scaling, for drawing and for the HUD editor's hit test.
constexpr int kResourceIcon = 24, kResourceGap = 4;
int ResourceHudWidth() { return kResourceIcon + 8 + 96; }
int ResourceHudHeight() { return 4 * kResourceIcon + 3 * kResourceGap; }

// Optional dark box behind one counter row (icon + text), so it reads on bright ground.
// Optional dark box behind one counter row (icon + text), so it reads on bright ground. `translucent`:
// the box lets the game show through (about 60% dark); its icon and text stay solid.
constexpr COLORREF kCounterBoxColor = RGB(14,14,18);
constexpr uint8_t kCounterBoxAlpha = 150;
// `filled` false: the frame (Frame only) or only the clear ground under the text (None).
void DrawCounterRowBackground(HDC hdc, HudElementId id, int x, int y, int contentW, int h, float s, bool filled, bool translucent)
{
    const int padX = std::max(2, (int)(5 * s)), padY = std::max(1, (int)(2 * s));
    const int bx = x - padX, by = y - padY, bw = contentW + 2 * padX + 1, bh = h + 2 * padY + 1;
    const int radius = HudRadius(s);
    if (filled)
        HudSmoothBox(hdc, bx, by, bw, bh, radius, HudColor(id, HudBox, kCounterBoxColor), translucent ? kCounterBoxAlpha : 255);
    else if (HudFrameOnly(id))
        DrawHudBorder(hdc, id, bx, by, bw, bh, radius);
    else
        HudSmoothBox(hdc, bx, by, bw, bh, 0, kHudClear, 0);
}

// True while any translucent box is switched on: the overlay then uses per-pixel alpha
// (UpdateLayeredWindow) instead of the colour key. Off = the lighter colour-key path, as before.
bool GlassModsShown();   // the Mods page (glass panel); defined with it
bool GlassPageShown();   // any glass page (Mods, Closet)

// Health Warning: "3.5 hearts" for health points (1 = half a heart).
std::wstring HeartsText(int points)
{
    wchar_t text[32];
    if (points % 2) swprintf(text, 32, L"%d.5 hearts", points / 2);
    else swprintf(text, 32, points == 2 ? L"%d heart" : L"%d hearts", points / 2);
    return text;
}

ULONGLONG healthWarningPreviewUntil = 0;   // the Preview button shows the glow until then

// How strong the edge glow is this frame (0 = none): stronger the lower your health, beating if Pulse is on.
uint8_t HealthWarningAlpha()
{
    const ULONGLONG now = GetTickCount64();
    float low = 0;   // 0..1: just at the threshold .. half a heart
    if (now < healthWarningPreviewUntil)
        low = 0.5f;
    else
    {
        int points = 0;
        if (!healthWarningEnabled || hudEditMode || !MinecraftDataProvider::IsInWorld() ||
            !MinecraftDataProvider::GetPlayerHealth(points) || points <= 0 || points > healthWarningAt)
            return 0;
        low = healthWarningAt > 1 ? (float)(healthWarningAt - points) / (healthWarningAt - 1) : 1.0f;
    }
    float level = healthWarningStrength / 100.0f * (0.6f + 0.4f * low);
    if (healthWarningPulse)
    {
        const float period = 1100.0f - 550.0f * low;   // ms per beat
        const float phase = std::fmod((float)now, period) / period;
        level *= 0.65f + 0.35f * (0.5f + 0.5f * std::cos(phase * 6.2831853f));
    }
    const int alpha = (int)(level * 230.0f) / 6 * 6;   // in steps: fewer whole-picture updates while it beats
    return (uint8_t)std::clamp(alpha, 0, 255);
}

bool OverlayAlphaWanted()
{
    if (overlayAlphaError)   // refused once: stay on the colour key (solid boxes) until the HUD restarts
        return false;
    if (menuOpen && !hudEditMode && selectedCategory == MenuCategory::Home)
        return true;   // the home screen's ring and dim
    if (menuOpen && GlassPageShown())
        return true;   // the glass pages
    // The HUD's boxes and frames: their round corners are smoothed and the ground under text without a
    // box is clear (HudSmoothBox), which needs per-pixel alpha.
    if (!hudEditMode && !MinecraftDataProvider::IsInWorld())
        return false;   // no HUD outside a world
    return keystrokesEnabled || cpsEnabled || fpsEnabled || pingEnabled || armorHudEnabled || arrowCounterEnabled ||
           resourceCounterEnabled || featureListEnabled || tabListEnabled || (hiveStatsEnabled && hiveEnabled) || dayTimerEnabled || blockDetailsEnabled ||
           (debugMenuEnabled && debugScreenShown) || healthWarningEnabled || GetTickCount64() < healthWarningPreviewUntil;
}

// Status line for the Resource / Arrow Counter pages: which display method is really in use.
void DrawTranslucentStatus(HDC hdc, int x, int y, bool wanted)
{
    if (!wanted) return;
    if (overlayAlphaError)
        DrawTextSimple(hdc, x, y, L"Translucent: Windows refused it (error " + std::to_wstring(overlayAlphaError) +
                                      L") - a solid box is used.", RGB(230,150,110), 12);
    else
        DrawTextSimple(hdc, x, y, overlayAlphaShown ? L"Translucent: active." : L"Translucent: waiting for the next frame...",
                       overlayAlphaShown ? RGB(100,220,130) : RGB(170,170,170), 12);
}

// One row per resource straight over the game (no box): the item's own icon (active texture packs first,
// then the game's), its name and how many you carry. Without an icon only the text is shown.
void DrawResources(HDC hdc)
{
    if (!resourceCounterEnabled)
        return;

    MyClientResources::Counts counts{};
    const bool known = MinecraftDataProvider::GetResources(counts);

    struct Row { const wchar_t* name; const char* item; uint32_t value; COLORREF color; };
    const Row rows[] =
    {
        { L"Iron",    "iron_ingot", counts.iron,    RGB(215,215,215) },
        { L"Gold",    "gold_ingot", counts.gold,    RGB(255,205,70)  },
        { L"Diamond", "diamond",    counts.diamond, RGB(90,235,235)  },
        { L"Emerald", "emerald",    counts.emerald, RGB(85,225,110)  }
    };

    const float s = resourceHud.scale;
    const int x = resourceHud.x;
    const int y = resourceHud.y;
    const int icon = std::max(8, (int)(kResourceIcon * s));
    const int gap = std::max(1, (int)(kResourceGap * s));
    const int textSize = std::max(9, (int)(15 * s));
    auto& icons = HudIcons();
    const std::wstring gameDir = MinecraftDataProvider::GameDirectory();
    const auto& activePacks = ActiveTexturePacks();

    for (int i = 0; i < 4; i++)
    {
        const int rowY = y + i * (icon + gap);
        const ItemIcons::Icon* found = icons.Get(gameDir, activePacks, rows[i].item);
        int textX = found ? x + icon + (int)(8 * s) : x;
        const std::wstring label = std::wstring(rows[i].name) + L"  " +
            (known ? std::to_wstring(rows[i].value) : std::wstring(L"---"));
        DrawCounterRowBackground(hdc, HudElementId::Resources, x, rowY, textX - x + TextWidth(hdc, label, textSize), icon, s,
                                     resourceBackground, resourceTranslucent);
        if (found)
            ItemIcons::Draw(hdc, *found, x, rowY, icon);
        DrawShadowText(hdc, textX, rowY + (icon - textSize) / 2 - 1, label, HudColor(HudElementId::Resources, HudText, rows[i].color),
                       textSize);
    }

    if (hudEditMode && selectedHud == HudElementId::Resources)
        DrawOutline(hdc, x, y, (int)(ResourceHudWidth() * s), (int)(ResourceHudHeight() * s), RGB(80,180,255));
}


std::vector<std::wstring> EnabledFeatureNames();   // defined with the feature list below

void DrawFeatureList(HDC hdc)
{
    if (!featureListEnabled)
        return;

    const auto names = EnabledFeatureNames();

    const float s = featureListHud.scale;
    const int x = featureListHud.x;
    const int y = featureListHud.y;
    const int rowH = std::max(18, (int)(26 * s));
    const int w = std::max(110, (int)(170 * s));
    const int count = std::max(1, (int)names.size());
    const int h = rowH * count;
    DrawHudBox(hdc, HudElementId::FeatureList, x, y, w, h, HudRadius(s), RGB(20,20,20));

    if (names.empty())
    {
        DrawHudText(hdc, HudElementId::FeatureList, x + (int)(10 * s), y + (int)(4 * s), L"(no features on)", HudColor(HudElementId::FeatureList, HudText, RGB(170,170,170)), std::max(10, (int)(15 * s)));
    }

    for (size_t i = 0; i < names.size(); i++)
    {
        const int ry = y + (int)i * rowH;
        DrawHudText(hdc, HudElementId::FeatureList, x + (int)(10 * s), ry + (int)(4 * s), names[i], HudColor(HudElementId::FeatureList, HudText, CurrentTheme().text), std::max(10, (int)(15 * s)));
    }

    if (hudEditMode && selectedHud == HudElementId::FeatureList)
        DrawOutline(hdc, x, y, w, h, RGB(80,180,255));
}


// Shulker Preview: only the hover popup below (no panel on the HUD; it stays hidden until a shulker box is hovered).

// The box's dye colour (0xRRGGBB); an undyed box is the shulker's own purple.
uint32_t ShulkerColor(const std::string& box)
{
    static const std::pair<const char*, uint32_t> dyes[] = {
        {"light_blue", 0x3AB3DA}, {"light_gray", 0x9D9D97}, {"silver", 0x9D9D97}, {"white", 0xF9FFFE},
        {"orange", 0xF9801D}, {"magenta", 0xC74EBD}, {"yellow", 0xFED83D}, {"lime", 0x80C71F},
        {"pink", 0xF38BAA}, {"gray", 0x474F52}, {"cyan", 0x169C9C}, {"purple", 0x8932B8},
        {"blue", 0x3C44AA}, {"brown", 0x835432}, {"green", 0x5E7C16}, {"red", 0xB02E26}, {"black", 0x1D1D21},
    };
    for (const auto& [name, rgb] : dyes)
        if (box.rfind(name, 0) == 0)
            return rgb;
    return 0x976997;
}

// `rgb` mixed into a vanilla GUI grey: amount 0 = grey, 1 = the dye colour.
COLORREF ShulkerMix(uint32_t rgb, int grey, float amount)
{
    const auto mix = [&](int shift) { return (int)(grey + (((rgb >> shift) & 0xFF) - grey) * amount + 0.5f); };
    return RGB(mix(16), mix(8), mix(0));
}

std::string WideToUtf8(const std::wstring& text);
// Items / blocks the icon lookup found no picture for, and the packs it looked in (icons_scan.txt next to the HUD),
// rewritten when the list grows. `who` names the feature.
void WriteIconScan(const SlotIcons::Cache& cache, const wchar_t* who, size_t& written)
{
    const auto& misses = cache.Misses();
    if (misses.size() == written) return;
    written = misses.size();
    static std::map<std::wstring, std::string> parts;
    std::string text = "Looked in:\r\n";
    for (const auto& root : cache.Roots()) text += "  " + WideToUtf8(root.wstring()) + "\r\n";
    text += "No picture (" + std::to_string(misses.size()) + "):\r\n";
    for (const auto& line : misses) text += "  " + line + "\r\n";
    parts[who] = text;
    if (FILE* f = _wfopen((GetExecutableDirectory() + L"\\icons_scan.txt").c_str(), L"wb"))
    {
        for (const auto& [name, body] : parts)
        {
            const std::string head = "== " + WideToUtf8(name) + " ==\r\n";
            fwrite(head.data(), 1, head.size(), f);
            fwrite(body.data(), 1, body.size(), f);
        }
        fclose(f);
    }
}

// Shulker hover popup: while the mouse rests on a shulker box in an inventory / container screen,
// the box is drawn next to the cursor the way the game shows it when opened: its 9x3 slots with the
// item icons (the game's own textures, blocks as 3D cubes) and stack counts. Part of Shulker Preview.
void DrawShulkerHover(HDC hdc, int width, int height)
{
    if (!shulkerEnabled || menuOpen || hudEditMode || !overlayWindow)
        return;

    MyClientShulker::HoverBox box{};
    if (!MinecraftDataProvider::GetShulkerHover(box))
        return;

    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(overlayWindow, &cursor))
        return;

    // "light_blue_shulker_box" -> "Light Blue Shulker Box"
    std::wstring title;
    bool wordStart = true;
    for (const char* c = box.name; *c; ++c)
    {
        wchar_t ch = (*c == '_') ? L' ' : (wchar_t)(unsigned char)*c;
        if (wordStart && ch >= L'a' && ch <= L'z') ch = (wchar_t)(ch - L'a' + L'A');
        wordStart = ch == L' ';
        title += ch;
    }
    if (title == L"Undyed Shulker Box") title = L"Shulker Box";

    // One GUI pixel of the game's container screen; the layout below is in those pixels (chest/shulker screen).
    const int p = std::max(1, (int)std::lround(2 * shulkerHud.scale));
    const int w = 176 * p;
    const int h = 79 * p;

    // Below-right of the cursor; flipped to the other side when it would leave the window.
    int x = cursor.x + 18;
    int y = cursor.y + 18;
    if (x + w > width) x = cursor.x - 18 - w;
    if (y + h > height) y = cursor.y - 18 - h;
    x = std::max(0, std::min(x, width - w));
    y = std::max(0, std::min(y, height - h));

    const auto fill = [&](int gx, int gy, int gw, int gh, COLORREF color)
    {
        const RECT r{x + gx * p, y + gy * p, x + (gx + gw) * p, y + (gy + gh) * p};
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(hdc, &r, brush);
        DeleteObject(brush);
    };
    const auto text = [&](int px, int py, const std::wstring& str, COLORREF color, int size)
    {
        HFONT old = (HFONT)SelectObject(hdc, UiFont(size));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, color);
        TextOutW(hdc, px, py, str.c_str(), (int)str.size());
        SelectObject(hdc, old);
    };
    const auto textWidth = [&](const std::wstring& str, int size)
    {
        HFONT old = (HFONT)SelectObject(hdc, UiFont(size));
        SIZE extent{};
        GetTextExtentPoint32W(hdc, str.c_str(), (int)str.size(), &extent);
        SelectObject(hdc, old);
        return (int)extent.cx;
    };

    // Panel: the game's bevelled window (black rim with cut corners, light top-left, dark bottom-right),
    // tinted with the box's colour.
    const uint32_t dye = ShulkerColor(box.name);
    const COLORREF face = ShulkerMix(dye, 0xC6, 0.30f);
    const COLORREF light = ShulkerMix(dye, 0xFF, 0.15f);
    const COLORREF dark = ShulkerMix(dye, 0x55, 0.35f);
    fill(2, 0, 172, 1, RGB(0,0,0));            // rim
    fill(2, 78, 172, 1, RGB(0,0,0));
    fill(0, 2, 1, 75, RGB(0,0,0));
    fill(175, 2, 1, 75, RGB(0,0,0));
    fill(1, 1, 1, 1, RGB(0,0,0));
    fill(174, 1, 1, 1, RGB(0,0,0));
    fill(1, 77, 1, 1, RGB(0,0,0));
    fill(174, 77, 1, 1, RGB(0,0,0));
    fill(2, 1, 172, 77, face);
    fill(1, 2, 174, 75, face);
    fill(2, 1, 171, 2, light);                 // light top / left edge
    fill(1, 2, 2, 74, light);
    fill(3, 76, 171, 2, dark);                 // dark bottom / right edge
    fill(173, 3, 2, 74, dark);
    fill(173, 1, 1, 2, face);                  // the corners where the two edges meet stay plain
    fill(2, 76, 1, 1, face);

    text(x + 8 * p, y + 5 * p, title, RGB(64,64,64), 9 * p);

    auto& icons = HudIcons();
    static SlotIcons::Cache slotIcons;
    const std::wstring gameDir = MinecraftDataProvider::GameDirectory();
    const auto& activePacks = ActiveTexturePacks();
    const int countSize = std::max(8, 8 * p);

    for (uint32_t i = 0; i < MyClientShulker::Slots; i++)
    {
        const int gx = 7 + (int)(i % 9) * 18;
        const int gy = 17 + (int)(i / 9) * 18;
        fill(gx, gy, 18, 18, RGB(139,139,139));     // slot: dark top-left, white bottom-right
        fill(gx, gy, 17, 1, RGB(55,55,55));
        fill(gx, gy, 1, 17, RGB(55,55,55));
        fill(gx + 1, gy + 17, 17, 1, RGB(255,255,255));
        fill(gx + 17, gy + 1, 1, 17, RGB(255,255,255));
        fill(gx + 17, gy, 1, 1, RGB(139,139,139));
        fill(gx, gy + 17, 1, 1, RGB(139,139,139));

        const auto& slot = box.slots[i];
        if (!slot.count)
            continue;
        char name[sizeof(slot.name) + 1] = {};
        std::memcpy(name, slot.name, sizeof(slot.name));
        const int ix = x + (gx + 1) * p, iy = y + (gy + 1) * p, size = 16 * p;
        const ItemIcons::Icon* found = slotIcons.Get(gameDir, activePacks, name, size);
        static size_t scanned = 0;
        WriteIconScan(slotIcons, L"Shulker Preview", scanned);
        if (found)
            ItemIcons::Draw(hdc, *found, ix, iy, size);
        else if (const ItemIcons::Icon* armor = icons.Get(gameDir, activePacks, name))
            ItemIcons::Draw(hdc, *armor, ix, iy, size);
        else
        {
            // No picture in the game's files: the first letters of the id.
            std::wstring shortName;
            for (const char* c = name; *c && shortName.size() < 3; ++c)
                if (*c != '_') shortName += (wchar_t)std::toupper((unsigned char)*c);
            text(ix + p, iy + 4 * p, shortName, RGB(40,40,40), 7 * p);
        }

        if (slot.count > 1)
        {
            // The game's stack count: white with a dark shadow, in the slot's bottom-right corner.
            const std::wstring count = std::to_wstring(slot.count);
            const int tx = ix + size + p - textWidth(count, countSize);
            const int ty = iy + size + 2 * p - countSize;
            text(tx + p, ty + p, count, RGB(63,63,63), countSize);
            text(tx, ty, count, RGB(255,255,255), countSize);
        }
    }
}


// Tab List: the names of the players in this world / server, one per line.
struct TabListLine { std::wstring text; bool header; };

std::vector<TabListLine> TabListLines()
{
    std::vector<TabListLine> lines;

    MyClientTabList::Shared data{};
    if (!MinecraftDataProvider::GetTabList(data))
    {
        lines.push_back({ L"Tab List: no data", true });
        return lines;
    }

    lines.push_back({ L"Players  " + std::to_wstring(data.total), true });

    if (data.count == 0)
    {
        lines.push_back({ L"(nobody)", false });
        return lines;
    }

    std::vector<std::wstring> names;
    for (uint32_t i = 0; i < data.count; i++)
    {
        const char* raw = data.names[i];
        const int need = MultiByteToWideChar(CP_UTF8, 0, raw, -1, nullptr, 0);
        std::wstring w(need > 0 ? need - 1 : 0, L'\0');
        if (need > 1)
            MultiByteToWideChar(CP_UTF8, 0, raw, -1, &w[0], need);
        names.push_back(w);
    }

    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b)
    {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    const size_t shown = std::min<size_t>(names.size(), 20);
    for (size_t i = 0; i < shown; i++)
        lines.push_back({ names[i], false });

    if (data.total > shown)
        lines.push_back({ L"+" + std::to_wstring(data.total - shown) + L" more", false });

    return lines;
}

void DrawTabList(HDC hdc)
{
    if (!tabListEnabled)
        return;

    const auto lines = TabListLines();

    const float s = tabListHud.scale;
    const int x = tabListHud.x;
    const int y = tabListHud.y;
    const int rowH = std::max(16, (int)(22 * s));
    const int w = std::max(120, (int)(190 * s));
    const int h = rowH * (int)lines.size();
    DrawHudBox(hdc, HudElementId::TabList, x, y, w, h, HudRadius(s), RGB(20,20,20));

    for (size_t i = 0; i < lines.size(); i++)
    {
        const int ry = y + (int)i * rowH;
        DrawHudText(hdc, HudElementId::TabList, x + (int)(10 * s), ry + (int)(3 * s),
            lines[i].text,
            HudColor(HudElementId::TabList, HudText, lines[i].header ? CurrentTheme().accentHi : CurrentTheme().text),
            std::max(10, (int)(14 * s))
        );
    }

    if (hudEditMode && selectedHud == HudElementId::TabList)
        DrawOutline(hdc, x, y, w, h, RGB(80,180,255));
}


// Hive Statistic: a table of the players in this match (Player Name / KDR / Win% / Lvl).
HiveStats::Fetcher hiveFetcher;

struct HiveStatsRow
{
    std::wstring name;
    bool known = false;       // stats arrived
    bool missing = false;     // no stats in this game (never played it)
    double kdr = 0, win = 0, level = 0;
    uint32_t prestige = 0;    // shown as a coin left of the level
};

std::vector<HiveStatsRow> HiveStatsRows(bool& haveData, int& total)
{
    std::vector<HiveStatsRow> rows;
    total = 0;

    MyClientTabList::Shared data{};
    haveData = MinecraftDataProvider::GetTabList(data);

    if (!haveData)
        return rows;

    total = (int)data.total;

    hiveDetectedTitle = data.title;
    hiveDetectedGame = HiveStats::DetectGame(hiveDetectedTitle);
    const int gameIndex = (hiveStatsAuto && hiveDetectedGame >= 0) ? hiveDetectedGame : hiveStatsGame;
    const HiveStats::Game& game = HiveStats::kGames[std::clamp(gameIndex, 0, HiveStats::kGameCount - 1)];

    for (uint32_t i = 0; i < data.count; i++)
    {
        const std::string utf8 = data.names[i];
        HiveStatsRow row;

        const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        row.name.assign(need > 0 ? need - 1 : 0, L'\0');
        if (need > 1)
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &row.name[0], need);

        const HiveStats::Entry e = hiveFetcher.Get(game.id, utf8);

        if (e.state == HiveStats::State::Done)
        {
            row.known = true;
            row.kdr = HiveStats::Kdr(e.stats);
            row.win = HiveStats::WinPercent(e.stats);
            row.level = HiveStats::Level(game, e.stats.xp);
            row.prestige = e.stats.prestige;
        }
        else if (e.state == HiveStats::State::Missing)
            row.missing = true;

        rows.push_back(row);
    }

    // Highest level first; players still loading or without stats go to the bottom.
    std::stable_sort(rows.begin(), rows.end(), [](const HiveStatsRow& a, const HiveStatsRow& b)
    {
        if (a.known != b.known)
            return a.known;
        if (a.known && (a.prestige != b.prestige || a.level != b.level))
            return HiveStats::Higher(a.prestige, a.level, b.prestige, b.level);
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    if (rows.size() > 16)
        rows.resize(16);

    return rows;
}

std::wstring FormatDecimal(double v, int decimals)
{
    wchar_t buffer[32];
    swprintf(buffer, 32, decimals == 2 ? L"%.2f" : L"%.0f", v);
    return buffer;
}

// Hive's prestige coin, drawn as pixel art like the game's own (I white, II gold, III blue, IV green, V purple).
constexpr int kCoinW = 11, kCoinH = 10;

void DrawPrestigeCoin(HDC hdc, int left, int top, float cell, uint32_t prestige)
{
    static const char* shape[kCoinH] = {
        " ######### ",
        "###########",
        "###########",
        "###########",
        "###########",
        "###########",
        "###########",
        "###########",
        "###########",
        " ######### ",
    };
    // numerals in the dark shade, 6 rows tall (rows 2..7), centred
    static const char* glyphs[5][6] = {
        {"#", "#", "#", "#", "#", "#"},
        {"#.#", "#.#", "#.#", "#.#", "#.#", "#.#"},
        {"#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#"},
        {"#.#...#", "#.#...#", "#.#...#", "#..#.#.", "#..#.#.", "#...#.."},
        {"#...#", "#...#", "#...#", ".#.#.", ".#.#.", "..#.."},
    };
    struct Look { COLORREF light, fill, shadow, glyph; };
    static const Look looks[5] = {
        {RGB(248,250,252), RGB(218,224,230), RGB(165,174,188), RGB(118,130,150)},
        {RGB(255,236,120), RGB(255,204,48),  RGB(226,150,28),  RGB(205,118,18)},
        {RGB(196,232,255), RGB(132,196,250), RGB(78,148,228),  RGB(48,108,208)},
        {RGB(196,242,146), RGB(122,214,60),  RGB(70,168,40),   RGB(52,138,34)},
        {RGB(234,146,246), RGB(204,62,230),  RGB(150,32,180),  RGB(108,18,138)},
    };
    const int tier = (int)std::clamp<uint32_t>(prestige, 1, 5) - 1;
    const Look& look = looks[tier];
    const auto inside = [&](int cx, int cy) { return cx >= 0 && cy >= 0 && cx < kCoinW && cy < kCoinH && shape[cy][cx] == '#'; };
    const int glyphW = (int)std::strlen(glyphs[tier][0]);
    const int glyphX = (kCoinW - glyphW) / 2;

    for (int cy = 0; cy < kCoinH; cy++)
    {
        // one FillRect per run of the same colour
        int runStart = -1;
        COLORREF runColor = 0;
        for (int cx = 0; cx <= kCoinW; cx++)
        {
            bool on = cx < kCoinW && inside(cx, cy);
            COLORREF c = look.fill;
            if (on)
            {
                const bool top = !inside(cx, cy - 1), leftEdge = !inside(cx - 1, cy);
                const bool bottom = !inside(cx, cy + 1), rightEdge = !inside(cx + 1, cy);
                if (bottom || rightEdge) c = look.shadow;
                else if (top || leftEdge) c = look.light;
                if ((cx == 1 && cy == 1) || (cx == 9 && cy == 8)) c = RGB(255,255,255);   // the shine pixels
                const int gx = cx - glyphX, gy = cy - 2;
                if (gx >= 0 && gx < glyphW && gy >= 0 && gy < 6 && glyphs[tier][gy][gx] == '#') c = look.glyph;
            }
            if (runStart >= 0 && (!on || c != runColor))
            {
                // cells may be fractional (small coins): round each edge so the cells still tile
                const auto at = [cell](int base, int i) { return base + (int)std::lround(i * cell); };
                RECT r{at(left, runStart), at(top, cy), at(left, cx), at(top, cy + 1)};
                HBRUSH b = CreateSolidBrush(runColor);
                FillRect(hdc, &r, b);
                DeleteObject(b);
                runStart = -1;
            }
            if (on && runStart < 0) { runStart = cx; runColor = c; }
        }
    }
}

void DrawHiveStats(HDC hdc)
{
    if (!hiveStatsEnabled || !hiveEnabled)
        return;

    bool haveData = false;
    int total = 0;
    const auto rows = HiveStatsRows(haveData, total);

    const float s = hiveStatsHud.scale;
    const int x = hiveStatsHud.x;
    const int y = hiveStatsHud.y;
    const int rowH = std::max(16, (int)(22 * s));
    const int nameW = (int)(170 * s), kdrW = (int)(58 * s), winW = (int)(74 * s), lvlW = (int)(70 * s);
    const int w = nameW + kdrW + winW + lvlW;
    const int fs = std::max(10, (int)(14 * s));
    const ThemeColors t = CurrentTheme();
    const auto ink = [](COLORREF c) { return HudColor(HudElementId::HiveStats, HudText, c); };

    int lineCount = haveData ? 1 + std::max<int>(1, (int)rows.size()) : 2;
    DrawHudBox(hdc, HudElementId::HiveStats, x, y, w, rowH * lineCount, HudRadius(s), RGB(20,20,20));

    // header
    DrawHudText(hdc, HudElementId::HiveStats, x + (int)(8 * s), y + (int)(3 * s), L"Player Name", ink(t.accentHi), fs);
    DrawHudTextCentered(hdc, HudElementId::HiveStats, x + nameW, y, kdrW, rowH, L"KDR", ink(t.accentHi), fs);
    DrawHudTextCentered(hdc, HudElementId::HiveStats, x + nameW + kdrW, y, winW, rowH, L"Win%", ink(t.accentHi), fs);
    DrawHudTextCentered(hdc, HudElementId::HiveStats, x + nameW + kdrW + winW, y, lvlW, rowH, L"Lvl", ink(t.accentHi), fs);

    if (!haveData)
    {
        DrawHudText(hdc, HudElementId::HiveStats, x + (int)(8 * s), y + rowH + (int)(3 * s),
                       L"no player list yet (needs the updated DLL)", ink(t.muted), fs);
    }
    else
    {
        for (size_t i = 0; i < rows.size(); i++)
        {
            const HiveStatsRow& r = rows[i];
            const int ry = y + (int)(i + 1) * rowH;

            DrawHudText(hdc, HudElementId::HiveStats, x + (int)(8 * s), ry + (int)(3 * s), r.name, ink(t.text), fs);

            if (r.known)
            {
                const COLORREF kdrColor = r.kdr >= 3 ? RGB(255,100,90) : (r.kdr >= 1.5 ? RGB(255,205,70) : RGB(230,230,230));
                const COLORREF winColor = r.win >= 50 ? RGB(100,220,130) : (r.win >= 25 ? RGB(200,235,200) : RGB(170,170,170));
                const COLORREF lvlColor = r.level >= 50 ? RGB(190,120,255) : (r.level >= 20 ? RGB(255,205,70) : RGB(230,230,230));

                DrawHudTextCentered(hdc, HudElementId::HiveStats, x + nameW, ry, kdrW, rowH, FormatDecimal(r.kdr, 2), ink(kdrColor), fs);
                DrawHudTextCentered(hdc, HudElementId::HiveStats, x + nameW + kdrW, ry, winW, rowH, FormatDecimal(r.win, 2) + L"%", ink(winColor), fs);
                const std::wstring lvl = FormatDecimal(std::floor(r.level), 0);
                // the level stays centred like everyone else's; the coin sits small just left of it
                DrawHudTextCentered(hdc, HudElementId::HiveStats, x + nameW + kdrW + winW, ry, lvlW, rowH, lvl, ink(lvlColor), fs);
                if (r.prestige > 0)
                {
                    // whole-pixel cells only: fractional ones make the numeral bars uneven
                    const int cell = std::max(1, (int)std::lround(s));
                    const int coinW = kCoinW * cell, coinH = kCoinH * cell;
                    // one column for every coin: just left of where a 3-digit level starts
                    const int widest = x + nameW + kdrW + winW + (lvlW - TextWidth(hdc, L"100", fs)) / 2;
                    DrawPrestigeCoin(hdc, widest - (int)(4 * s) - coinW, ry + (rowH - coinH) / 2, (float)cell, r.prestige);
                }
            }
            else
            {
                DrawHudTextCentered(hdc, HudElementId::HiveStats, x + nameW, ry, kdrW + winW + lvlW, rowH,
                                 r.missing ? L"no stats" : L"...", ink(t.muted), fs);
            }
        }


        if (rows.empty())
        {
            DrawHudText(hdc, HudElementId::HiveStats, x + (int)(8 * s), y + rowH + (int)(3 * s), L"(nobody)", ink(t.muted), fs);
        }
    }

    if (hudEditMode && selectedHud == HudElementId::HiveStats)
        DrawOutline(hdc, x, y, w, rowH * lineCount, RGB(80,180,255));
}


// Day Timer: "m:ss" (minutes are not padded: 20:00 at most).
std::wstring FormatMinSec(int seconds)
{
    seconds = std::max(0, seconds);
    wchar_t buffer[16];
    swprintf(buffer, 16, L"%d:%02d", seconds / 60, seconds % 60);
    return buffer;
}

// The Day Timer lines: the next change first (night in the day, morning at night), then the other one
// when "Show both" is on. Empty when the world has no running day cycle (nothing is drawn then).
std::vector<std::wstring> DayTimerLines()
{
    int32_t time = 0;
    bool moving = false;
    if (!MinecraftDataProvider::GetWorldClock(time, moving) || !moving)
    {
        if (!hudEditMode) return {};
        return dayTimerBoth ? std::vector<std::wstring>{L"Night in --:--", L"Morning in --:--"}
                            : std::vector<std::wstring>{L"Night in --:--"};
    }
    const std::wstring night = L"Night in " + FormatMinSec(MyClientFeatures::SecondsUntilNight(time));
    const std::wstring morning = L"Morning in " + FormatMinSec(MyClientFeatures::SecondsUntilMorning(time));
    const bool isNight = MyClientFeatures::IsNight(time);
    if (!dayTimerBoth) return {isNight ? morning : night};
    return isNight ? std::vector<std::wstring>{morning, night} : std::vector<std::wstring>{night, morning};
}

int DayTimerHudHeight(size_t lines) { return 10 + 24 * (int)std::max<size_t>(1, lines); }

void DrawDayTimer(HDC hdc)
{
    if (!dayTimerEnabled)
        return;
    const std::vector<std::wstring> lines = DayTimerLines();
    if (lines.empty())
        return;

    const float s = dayTimerHud.scale;
    const int x = dayTimerHud.x;
    const int y = dayTimerHud.y;
    const int w = std::max(110, (int)(170 * s));
    const int h = std::max(24, (int)(DayTimerHudHeight(lines.size()) * s));

    DrawHudBox(hdc, HudElementId::DayTimer, x, y, w, h, HudRadius(s), RGB(20,20,20));
    for (size_t i = 0; i < lines.size(); i++)
        DrawHudText(hdc, HudElementId::DayTimer, x + (int)(12 * s), y + (int)((6 + 24 * i) * s), lines[i],
                    HudColor(HudElementId::DayTimer, HudText, i == 0 ? RGB(255,255,255) : RGB(190,190,190)),
                    std::max(10, (int)(18 * s)));

    if (hudEditMode && selectedHud == HudElementId::DayTimer)
        DrawOutline(hdc, x, y, w, h, RGB(80,180,255));
}


// ------------------------- Block Details ------------------
// The box at the top about the block under the crosshair: its picture and name, where it comes from, and the tool.
std::wstring Utf8ToWide(const std::string& text);
constexpr int kBlockDetailsAreaW = 300;   // the box is centred in this width (HudElement x / y = its top-left)

struct BlockDetailsInfo
{
    std::string id, held;
    std::wstring name, source;
    BlockDetails::ToolInfo tool;
    int verdict = 0;   // BlockDetails::Verdict
};

// The add-on names (namespace -> pack name) and the game's .lang, read once on a worker thread; read again when a
// block's namespace is unknown and the last read is a minute old (an add-on installed while the HUD runs).
struct BlockDetailsNames
{
    std::mutex mutex;
    std::map<std::string, std::string> addons, lang;
    std::atomic<bool> busy{false};
    ULONGLONG scannedAt = 0;
};
BlockDetailsNames& BlockNames()
{
    static BlockDetailsNames names;
    return names;
}
void ScanBlockNames(bool unknownNamespace)
{
    BlockDetailsNames& names = BlockNames();
    const ULONGLONG now = GetTickCount64();
    if (names.busy.load() || (names.scannedAt && !(unknownNamespace && now - names.scannedAt > 60000))) return;
    names.scannedAt = now;
    names.busy.store(true);
    const std::wstring gameDir = MinecraftDataProvider::GameDirectory();
    std::thread([gameDir]()
    {
        const auto roots = SkinLibrary::MojangRoots();
        auto addons = BlockDetails::ScanAddons(roots);
        // The game's own words for "@tile.x" names: its language's .lang in every vanilla pack, newest first.
        std::map<std::string, std::string> lang;
        if (!gameDir.empty())
        {
            const std::string language = BlockDetails::GameLanguage(roots);
            std::vector<std::filesystem::path> packs;
            std::error_code ec;
            for (std::filesystem::directory_iterator it(std::filesystem::path(gameDir) / L"data" / L"resource_packs", ec), end;
                 !ec && it != end; it.increment(ec))
                if (it->path().filename().wstring().rfind(L"vanilla", 0) == 0) packs.push_back(it->path());
            std::sort(packs.rbegin(), packs.rend());
            for (const auto& pack : packs)
                for (const auto& [key, value] : SkinLibrary::ParseLang(BlockDetails::ReadText(pack / "texts" / (language + ".lang"))))
                    lang.emplace(key, value);
        }
        BlockDetailsNames& n = BlockNames();
        {
            std::lock_guard guard(n.mutex);
            n.addons = std::move(addons);
            n.lang = std::move(lang);
        }
        n.busy.store(false);
    }).detach();
}

bool CurrentBlockDetails(BlockDetailsInfo& info)
{
    MyClientFeatures::Debug debug{};
    MyClientFeatures::Look look{};
    if (!MinecraftDataProvider::GetDebug(debug) || debug.lookFace == MyClientFeatures::NoLook || !debug.lookBlock[0]) return false;
    debug.lookBlock[sizeof(debug.lookBlock) - 1] = 0;
    info.id = debug.lookBlock;
    if (info.id == "minecraft:air" || info.id.empty()) return false;
    const bool lookKnown = MinecraftDataProvider::GetLook(look);
    BlockDetailsNames& names = BlockNames();
    const std::string ns = BlockDetails::Namespace(info.id);
    bool unknown = false;
    {
        std::lock_guard guard(names.mutex);
        unknown = ns != "minecraft" && !names.addons.count(ns);
        info.name = Utf8ToWide(BlockDetails::DisplayName(lookKnown ? look.name : "", info.id, names.lang));
        info.source = Utf8ToWide(BlockDetails::SourceName(info.id, names.addons));
    }
    ScanBlockNames(unknown);
    if (lookKnown)
    {
        info.held = look.held;
        info.tool = BlockDetails::ToolFor(look.tags);
        info.verdict = BlockDetails::Verdict(info.tool, info.held, look.requiresTool != 0, look.canHarvest != 0);
    }
    return true;
}

int BlockDetailsHeight() { return blockDetailsSource ? 52 : 40; }

void DrawBlockDetails(HDC hdc)
{
    if (!blockDetailsEnabled)
        return;
    if (!hudEditMode && !MinecraftDataProvider::IsInWorld())
        return;
    BlockDetailsInfo info;
    if (!CurrentBlockDetails(info))
    {
        if (!hudEditMode) return;
        info.id = "minecraft:diorite"; info.name = L"Diorite"; info.source = L"Minecraft";
        info.tool = BlockDetails::ToolFor("stone"); info.verdict = 1;
    }

    const float s = blockDetailsHud.scale;
    const auto px = [s](int v) { return std::max(1, (int)std::lround(v * s)); };
    const int nameSize = std::max(10, px(18)), sourceSize = std::max(9, px(15));
    const int icon = px(32), toolIcon = px(24), pad = px(8), gap = px(8);
    const std::string toolItem = blockDetailsTool ? BlockDetails::ToolItem(info.tool) : std::string{};
    const int textW = std::max(TextWidth(hdc, info.name, nameSize), blockDetailsSource ? TextWidth(hdc, info.source, sourceSize) : 0);
    const int w = pad + icon + gap + textW + (toolItem.empty() ? 0 : gap + toolIcon + px(6)) + pad;
    const int h = std::max(24, px(BlockDetailsHeight()));
    const int areaW = std::max(1, (int)(kBlockDetailsAreaW * s));
    const int x = blockDetailsHud.x + (areaW - w) / 2;
    const int y = blockDetailsHud.y;

    DrawHudBox(hdc, HudElementId::BlockDetails, x, y, w, h, HudRadius(s), RGB(20,20,24));

    static SlotIcons::Cache icons;
    const std::wstring gameDir = MinecraftDataProvider::GameDirectory();
    const auto& packs = ActiveTexturePacks();
    const int iconY = y + (h - icon) / 2;
    const ItemIcons::Icon* found = icons.Get(gameDir, packs, BlockDetails::PathOf(info.id), icon);
    static size_t scanned = 0;
    WriteIconScan(icons, L"Block Details", scanned);
    if (found)
        ItemIcons::Draw(hdc, *found, x + pad, iconY, icon);
    else
        DrawHudBox(hdc, HudElementId::BlockDetails, x + pad, iconY, icon, icon, px(4), RGB(70,70,76));

    const int textX = x + pad + icon + gap;
    if (blockDetailsSource)
    {
        DrawHudText(hdc, HudElementId::BlockDetails, textX, y + px(5), info.name, HudColor(HudElementId::BlockDetails, HudText, RGB(255,255,255)), nameSize);
        DrawHudText(hdc, HudElementId::BlockDetails, textX, y + px(28), info.source, HudColor(HudElementId::BlockDetails, HudText, RGB(95,120,255)), sourceSize);
    }
    else
        DrawHudText(hdc, HudElementId::BlockDetails, textX, y + (h - nameSize) / 2 - px(1), info.name,
                    HudColor(HudElementId::BlockDetails, HudText, RGB(255,255,255)), nameSize);

    if (!toolItem.empty())
    {
        const int tx = x + w - pad - px(6) - toolIcon, ty = y + (h - toolIcon) / 2 - px(3);
        if (const ItemIcons::Icon* found = icons.Get(gameDir, packs, toolItem, toolIcon))
            ItemIcons::Draw(hdc, *found, tx, ty, toolIcon);
        else
            DrawHudText(hdc, HudElementId::BlockDetails, tx, ty, Utf8ToWide(BlockDetails::ToolWord(info.tool.tool)).substr(0, 2), RGB(200,200,200), px(14));
        // The mark for what you hold: green tick = it works, red cross = the block will not drop.
        if (info.verdict != 0)
        {
            const float mx = (float)(tx + toolIcon - px(3)), my = (float)(ty + toolIcon + px(1)), r = (float)px(5), t = std::max(2.f, 2.5f * s);
            for (int pass = 0; pass < 2; pass++)
            {
                const float o = pass == 0 ? std::max(1.f, s) : 0.f;   // a dark copy first, so it shows on any picture
                const COLORREF c = pass == 0 ? RGB(25,25,25) : info.verdict > 0 ? RGB(85,225,105) : RGB(240,70,70);
                if (info.verdict > 0)
                {
                    DrawLineSmooth(hdc, mx - r + o, my + o, mx - r * 0.3f + o, my + r * 0.7f + o, t, c);
                    DrawLineSmooth(hdc, mx - r * 0.3f + o, my + r * 0.7f + o, mx + r + o, my - r * 0.8f + o, t, c);
                }
                else
                {
                    DrawLineSmooth(hdc, mx - r * 0.8f + o, my - r * 0.8f + o, mx + r * 0.8f + o, my + r * 0.8f + o, t, c);
                    DrawLineSmooth(hdc, mx - r * 0.8f + o, my + r * 0.8f + o, mx + r * 0.8f + o, my - r * 0.8f + o, t, c);
                }
            }
        }
    }

    if (hudEditMode && selectedHud == HudElementId::BlockDetails)
        DrawOutline(hdc, blockDetailsHud.x, y, areaW, h, RGB(80,180,255));
}


void DrawPing(HDC hdc)
{
    if (!pingEnabled)
        return;

    int ms = 0;
    const bool known = PingMonitor::Get(ms);

    const COLORREF color = HudColor(HudElementId::Ping, HudText,
        !known ? RGB(170,170,170)
               : (ms < 60 ? RGB(100,220,130)
                          : (ms < 120 ? RGB(255,205,70)
                                      : RGB(255,100,90))));

    const float s = pingHud.scale;
    const int x = pingHud.x;
    const int y = pingHud.y;
    const int w = std::max(90, (int)(120 * s));
    const int h = std::max(24, (int)(34 * s));

    DrawHudBox(hdc, HudElementId::Ping, x, y, w, h, HudRadius(s), RGB(20,20,20));

    DrawHudText(hdc, HudElementId::Ping,
        x + (int)(12 * s),
        y + (int)(7 * s),
        known ? L"Ping: " + std::to_wstring(ms) + L" ms"
              : std::wstring(L"Ping: ---"),
        color,
        std::max(10, (int)(18 * s))
    );

    if (hudEditMode && selectedHud == HudElementId::Ping)
        DrawOutline(hdc, x, y, w, h, RGB(80,180,255));
}


// Arrow Counter size before scaling, for drawing and for the HUD editor's hit test.
int ArrowHudWidth() { return kResourceIcon + 8 + 96; }
int ArrowHudHeight() { return kResourceIcon; }

// Same look as the Resource Counter: the arrow's own icon (texture packs first), then "Arrow <count>", no box.
void DrawArrows(HDC hdc)
{
    if (!arrowCounterEnabled)
        return;

    uint32_t count = 0;
    const std::wstring label = MinecraftDataProvider::GetArrowCount(count)
        ? L"Arrow  " + std::to_wstring(count)
        : L"Arrow  ---";

    const float s = arrowHud.scale;
    const int x = arrowHud.x;
    const int y = arrowHud.y;
    const int icon = std::max(8, (int)(kResourceIcon * s));
    const int textSize = std::max(9, (int)(15 * s));
    const ItemIcons::Icon* found = HudIcons().Get(MinecraftDataProvider::GameDirectory(), ActiveTexturePacks(), "arrow");
    const int textX = found ? x + icon + (int)(8 * s) : x;
    DrawCounterRowBackground(hdc, HudElementId::Arrows, x, y, textX - x + TextWidth(hdc, label, textSize), icon, s,
                                 arrowBackground, arrowTranslucent);
    if (found)
        ItemIcons::Draw(hdc, *found, x, y, icon);
    DrawShadowText(hdc, textX, y + (icon - textSize) / 2 - 1, label, HudColor(HudElementId::Arrows, HudText, RGB(255,255,255)), textSize);

    if (hudEditMode && selectedHud == HudElementId::Arrows)
        DrawOutline(hdc, x, y, (int)(ArrowHudWidth() * s), (int)(ArrowHudHeight() * s), RGB(80,180,255));
}


void DrawFPS(HDC hdc)
{
    float gameFps = 0;
    const bool knownFps = MinecraftDataProvider::GetFps(gameFps);
    if (!fpsEnabled)
        return;
    // With Frame Generation running: the frames shown, then the game's own in brackets.
    float shownFps = 0; uint32_t frameGen = 0;
    const bool generating = knownFps && MinecraftDataProvider::GetFrameGen(shownFps, frameGen) &&
                            frameGen == MyClientFeatures::FrameGenRunning && shownFps > 0;
    const std::wstring fpsLabel = !knownFps ? L"FPS: ---"
        : generating ? L"FPS: " + std::to_wstring(std::lround(shownFps)) + L" (" + std::to_wstring(std::lround(gameFps)) + L")"
                     : L"FPS: " + std::to_wstring(static_cast<int>(std::lround(gameFps)));

    float s =
        fpsHud.scale;

    int x =
        fpsHud.x;

    int y =
        fpsHud.y;

    int w =
        std::max(
            70,
            (int)((generating ? 180 : 120) * s)
        );

    int h =
        std::max(
            25,
            (int)(38 * s)
        );

    DrawHudBox(hdc, HudElementId::FPS, x, y, w, h, HudRadius(s), RGB(20,20,20));

    DrawHudText(hdc, HudElementId::FPS,
        x + (int)(10 * s),
        y + (int)(8 * s),
        fpsLabel,
        HudColor(HudElementId::FPS, HudText, RGB(255,255,255)),
        std::max(
            10,
            (int)(16 * s)
        )
    );

    if (hudEditMode &&
        selectedHud ==
            HudElementId::FPS)
    {
        DrawOutline(
            hdc,
            x,
            y,
            w,
            h,
            RGB(80,180,255)
        );
    }
}


// The block outline colour is now changed inside the game (DLL hook on the game's own outline
// rendering). Nothing is drawn over the game any more: the old "Block: x, y, z" label and the
// projected lines were never connected to real game data.
void DrawBlockOutline(HDC)
{
}


// Debug Menu: what the screen shows about the computer. CPU and GPU names never change; the display,
// window and memory are looked at once a second.
DebugScreen::Info DebugSystemInfo()
{
    static DebugScreen::Info cached;
    static ULONGLONG at = 0;
    static bool named = false;
    if (!named)
    {
        named = true;
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            wchar_t name[128] = {};
            DWORD size = sizeof(name) - sizeof(wchar_t), type = 0;
            if (RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type, (BYTE*)name, &size) == ERROR_SUCCESS && type == REG_SZ)
            {
                std::wstring cpu = name;
                while (!cpu.empty() && cpu.back() == L' ') cpu.pop_back();
                while (!cpu.empty() && cpu.front() == L' ') cpu.erase(0, 1);
                cached.cpu = cpu;
            }
            RegCloseKey(key);
        }
        SYSTEM_INFO system{};
        GetNativeSystemInfo(&system);
        cached.cores = (int)system.dwNumberOfProcessors;
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); i++, device.cb = sizeof(device))
            if (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { cached.gpu = device.DeviceString; break; }
    }
    const ULONGLONG now = GetTickCount64();
    if (at && now - at < 1000) return cached;
    at = now;
    cached.windowW = cached.windowH = cached.displayW = cached.displayH = cached.displayHz = 0;
    cached.gameMemory = 0;
    if (minecraftWindow && IsWindow(minecraftWindow))
    {
        RECT client{};
        if (GetClientRect(minecraftWindow, &client)) { cached.windowW = client.right; cached.windowH = client.bottom; }
        MONITORINFOEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(minecraftWindow, MONITOR_DEFAULTTONEAREST), &monitor))
        {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode))
            {
                cached.displayW = (int)mode.dmPelsWidth; cached.displayH = (int)mode.dmPelsHeight; cached.displayHz = (int)mode.dmDisplayFrequency;
            }
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(minecraftWindow, &pid);
        if (HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr)
        {
            PROCESS_MEMORY_COUNTERS memory{};
            if (GetProcessMemoryInfo(process, &memory, sizeof(memory))) cached.gameMemory = memory.WorkingSetSize;
            CloseHandle(process);
        }
    }
    MEMORYSTATUSEX ram{};
    ram.dwLength = sizeof(ram);
    if (GlobalMemoryStatusEx(&ram)) { cached.ramTotal = ram.ullTotalPhys; cached.ramFree = ram.ullAvailPhys; }
    return cached;
}

// Debug Menu: the Java-style F3 screen, text on grey see-through strips at the top left (the game and the
// world) and the top right (the computer). Only in a world, while its key has switched it on.
void DrawDebugScreen(HDC hdc, int width, int)
{
    if (!debugMenuEnabled || !debugScreenShown || hudEditMode || !MinecraftDataProvider::IsInWorld())
        return;
    DebugScreen::Info info = DebugSystemInfo();
    info.title = clientName + L" (Minecraft Bedrock)";
    float fps = 0, shown = 0;
    uint32_t frameGen = 0;
    info.fpsKnown = MinecraftDataProvider::GetFps(fps);
    info.fps = fps;
    info.frameGen = info.fpsKnown && MinecraftDataProvider::GetFrameGen(shown, frameGen) &&
                    frameGen == MyClientFeatures::FrameGenRunning && shown > 0;
    info.shownFps = shown;
    // The camera is only sent while nothing covers the world (chat, inventory): keep the last one a while.
    static MinecraftDataProvider::Camera lastCam{};
    static ULONGLONG lastCamAt = 0;
    MinecraftDataProvider::Camera cam{};
    if (MinecraftDataProvider::GetCamera(cam)) { lastCam = cam; lastCamAt = GetTickCount64(); }
    info.camKnown = lastCamAt && GetTickCount64() - lastCamAt < 10000;
    info.eyeX = lastCam.x; info.eyeY = lastCam.y; info.eyeZ = lastCam.z; info.yaw = lastCam.yaw; info.pitch = lastCam.pitch;
    info.serverKnown = MinecraftDataProvider::GetPingTarget(info.host, info.port);
    info.pingKnown = info.serverKnown && PingMonitor::Get(info.pingMs);
    info.clockKnown = MinecraftDataProvider::GetWorldClock(info.time, info.clockMoving);
    info.debugKnown = MinecraftDataProvider::GetDebug(info.debug);
    if (MinecraftDataProvider::Gpu gpu{}; MinecraftDataProvider::GetGpu(gpu))
    {
        info.gameGpu = gpu.name; info.directX = gpu.directX; info.featureLevel = gpu.featureLevel;
        info.videoMb = gpu.videoMb; info.vendor = gpu.vendor; info.driver = gpu.driver;
    }

    const int size = std::max(10, (int)std::lround(24 * guiScale * debugTextSize / 100.0));
    const int lineH = size + std::max(3, size / 4), pad = std::max(2, size / 6);
    const COLORREF strip = RGB(80,80,80), ink = RGB(224,224,224);
    const bool was = inkTracking;
    inkTracking = true;   // the strips are see-through: so are the text's soft edges
    const auto column = [&](const std::vector<std::wstring>& lines, bool right)
    {
        int y = pad;
        for (const auto& line : lines)
        {
            if (!line.empty())
            {
                const int tw = TextWidth(hdc, line, size);
                const int x = right ? width - pad - tw - 2 * pad : pad;
                HudSmoothBox(hdc, x, y, tw + 2 * pad, lineH, 0, strip, 144);
                DrawTextSimple(hdc, x + pad, y + (lineH - size) / 2, line, ink, size);
            }
            y += lineH;
        }
    };
    column(DebugScreen::LeftLines(info), false);
    column(DebugScreen::RightLines(info), true);
    inkTracking = was;
}


void DrawHud(HDC hdc)
{
    // Show the HUD only inside a world/server. The position editor still shows it.
    if (!hudEditMode && !MinecraftDataProvider::IsInWorld()) return;
    DrawBlockOutline(hdc);
    DrawKeystrokes(hdc);
    DrawCPS(hdc);
    DrawArmor(hdc);
    DrawArrows(hdc);
    DrawFPS(hdc);
    DrawResources(hdc);
    DrawPing(hdc);
    DrawDayTimer(hdc);
    DrawBlockDetails(hdc);
    DrawFeatureList(hdc);
    DrawTabList(hdc);
    DrawHiveStats(hdc);
}


// ------------------------- HUD hit testing ------------------

HudElement* GetHudElement(
    HudElementId id)
{
    switch (id)
    {
    case HudElementId::Keystrokes:
        return &keystrokesHud;

    case HudElementId::CPS:
        return &cpsHud;

    case HudElementId::Armor:
        return &armorHud;

    case HudElementId::Arrows:
        return &arrowHud;

    case HudElementId::FPS:
        return &fpsHud;

    case HudElementId::Resources:
        return &resourceHud;

    case HudElementId::Ping:
        return &pingHud;

    case HudElementId::DayTimer:
        return &dayTimerHud;

    case HudElementId::BlockDetails:
        return &blockDetailsHud;

    case HudElementId::FeatureList:
        return &featureListHud;

    case HudElementId::ShulkerPreview:
        return &shulkerHud;

    case HudElementId::TabList:
        return &tabListHud;

    case HudElementId::HiveStats:
        return &hiveStatsHud;

    default:
        return nullptr;
    }
}


HudElementId GetHudAt(
    int mx,
    int my)
{
    auto hit =
        [&](HudElementId id,
            HudElement& e,
            int w,
            int h)
        {
            int ww =
                std::max(
                    1,
                    (int)(w * e.scale)
                );

            int hh =
                std::max(
                    1,
                    (int)(h * e.scale)
                );

            return IsInside(
                mx,
                my,
                e.x,
                e.y,
                ww,
                hh
            )
                ? id
                : HudElementId::None;
        };

    if (keystrokesEnabled)
    {
        int size =
            std::max(
                10,
                (int)(
                    40 *
                    keystrokesHud.scale
                )
            );

        int gap =
            std::max(
                2,
                (int)(
                    5 *
                    keystrokesHud.scale
                )
            );

        auto r =
            hit(
                HudElementId::Keystrokes,
                keystrokesHud,
                size * 3 +
                    gap * 2,
                size * 2 +
                    gap
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (cpsEnabled)
    {
        auto r =
            hit(
                HudElementId::CPS,
                cpsHud,
                165,
                65
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (armorHudEnabled)
    {
        auto r =
            hit(
                HudElementId::Armor,
                armorHud,
                ArmorHudWidth(),
                ArmorHudHeight()
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (arrowCounterEnabled)
    {
        auto r =
            hit(
                HudElementId::Arrows,
                arrowHud,
                ArrowHudWidth(),
                ArrowHudHeight()
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (fpsEnabled)
    {
        auto r =
            hit(
                HudElementId::FPS,
                fpsHud,
                120,
                38
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (featureListEnabled)
    {
        auto r =
            hit(
                HudElementId::FeatureList,
                featureListHud,
                170,
                (int)std::max<size_t>(1, EnabledFeatureNames().size()) * 26
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (hiveStatsEnabled && hiveEnabled)
    {
        auto r =
            hit(
                HudElementId::HiveStats,
                hiveStatsHud,
                372,   // 170 + 58 + 74 + 70
                22 * 12
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (tabListEnabled)
    {
        auto r =
            hit(
                HudElementId::TabList,
                tabListHud,
                190,
                (int)std::max<size_t>(1, TabListLines().size()) * 22
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (pingEnabled)
    {
        auto r =
            hit(
                HudElementId::Ping,
                pingHud,
                120,
                34
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    if (blockDetailsEnabled)
    {
        auto r = hit(HudElementId::BlockDetails, blockDetailsHud, kBlockDetailsAreaW, BlockDetailsHeight());
        if (r != HudElementId::None)
            return r;
    }

    if (dayTimerEnabled)
    {
        auto r = hit(HudElementId::DayTimer, dayTimerHud, 170, DayTimerHudHeight(dayTimerBoth ? 2 : 1));
        if (r != HudElementId::None)
            return r;
    }

    if (resourceCounterEnabled)
    {
        auto r =
            hit(
                HudElementId::Resources,
                resourceHud,
                ResourceHudWidth(),
                ResourceHudHeight()
            );

        if (r !=
            HudElementId::None)
        {
            return r;
        }
    }

    return HudElementId::None;
}


// ------------------------- Feature UI -----------------------

std::wstring FeatureTitle(
    DetailFeature f)
{
    switch (f)
    {
    case DetailFeature::Keystrokes:
        return L"Keystrokes";

    case DetailFeature::CPS:
        return L"CPS Counter";

    case DetailFeature::Armor:
        return L"Armor HUD";

    case DetailFeature::Arrows:
        return L"Arrow Counter";

    case DetailFeature::FPS:
        return L"FPS";

    case DetailFeature::PartyAutoAccept:
        return L"Party Auto Accept";

    case DetailFeature::BlockOutline:
        return L"Block Outline";

    case DetailFeature::Zoom:
        return L"Zoom";

    case DetailFeature::Resources:
        return L"Resource Counter";

    case DetailFeature::Fullbright:
        return L"Full Bright";

    case DetailFeature::Nametag:
        return L"Third Person Nametag";

    case DetailFeature::Ping:
        return L"Ping";

    case DetailFeature::Textures:
        return L"Textures";

    case DetailFeature::AutoRq:
        return L"Auto RQ";

    case DetailFeature::FeatureList:
        return L"Feature List";

    case DetailFeature::ShulkerPreview:
        return L"Shulker Preview";

    case DetailFeature::HiveUtils:
        return L"Hive Utils";

    case DetailFeature::TabList:
        return L"Tab List";

    case DetailFeature::ChunkBorder:
        return L"Chunk Border";

    case DetailFeature::Hitbox:
        return L"Hitbox";

    case DetailFeature::Nick:
        return L"Nick";

    case DetailFeature::Replay:
        return L"Replay Mod";

    case DetailFeature::MenuSounds:
        return L"Menu Sounds";

    case DetailFeature::Lite:
        return L"Optimization";

    case DetailFeature::FastInventory:
        return L"Fast Inventory";

    case DetailFeature::LeftHand:
        return L"Left Hand";

    case DetailFeature::HitEffect:
        return L"Hit Effect";

    case DetailFeature::FrameGen:
        return L"Frame Generation";

    case DetailFeature::MenuOpen:
        return L"Menu Open";

    case DetailFeature::DayTimer:
        return L"Day Timer";

    case DetailFeature::DebugMenu:
        return L"Debug Menu";

    case DetailFeature::BlockDetails:
        return L"Block Details";

    case DetailFeature::HealthWarning:
        return L"Health Warning";

    case DetailFeature::HiveStats:
        return L"Hive Statistic";

    default:
        return L"";
    }
}


bool FeatureEnabled(
    DetailFeature f)
{
    switch (f)
    {
    case DetailFeature::Keystrokes:
        return keystrokesEnabled;

    case DetailFeature::CPS:
        return cpsEnabled;

    case DetailFeature::Armor:
        return armorHudEnabled;

    case DetailFeature::Arrows:
        return arrowCounterEnabled;

    case DetailFeature::FPS:
        return fpsEnabled;

    case DetailFeature::PartyAutoAccept:
        return partyAutoAcceptEnabled;

    case DetailFeature::BlockOutline:
        return blockOutlineEnabled;

    case DetailFeature::Zoom:
        return zoomEnabled;

    case DetailFeature::Resources:
        return resourceCounterEnabled;

    case DetailFeature::Fullbright:
        return fullbrightEnabled;

    case DetailFeature::Nametag:
        return nametagEnabled;

    case DetailFeature::Ping:
        return pingEnabled;

    case DetailFeature::Textures:
        return texturesEnabled;

    case DetailFeature::AutoRq:
        return autoRqEnabled;

    case DetailFeature::FeatureList:
        return featureListEnabled;

    case DetailFeature::ShulkerPreview:
        return shulkerEnabled;

    case DetailFeature::TabList:
        return tabListEnabled;

    case DetailFeature::ChunkBorder:
        return chunkBorderEnabled;

    case DetailFeature::Hitbox:
        return hitboxEnabled;

    case DetailFeature::Nick:
        return nickEnabled;

    case DetailFeature::Replay:
        return replayEnabled;

    case DetailFeature::MenuSounds:
        return menuSoundsEnabled;

    case DetailFeature::Lite:
        return liteEnabled;

    case DetailFeature::FastInventory:
        return fastInventoryEnabled;

    case DetailFeature::LeftHand:
        return leftHandEnabled;
    case DetailFeature::HitEffect:
        return hitEffectEnabled;
    case DetailFeature::FrameGen:
        return frameGenEnabled;
    case DetailFeature::DayTimer:
        return dayTimerEnabled;
    case DetailFeature::DebugMenu:
        return debugMenuEnabled;
    case DetailFeature::BlockDetails:
        return blockDetailsEnabled;
    case DetailFeature::HealthWarning:
        return healthWarningEnabled;

    case DetailFeature::HiveStats:
        return hiveStatsEnabled;

    case DetailFeature::HiveUtils:
        return hiveEnabled && (partyAutoAcceptEnabled || autoRqEnabled);

    default:
        return false;
    }
}


// ---- Toggle notifications (bottom-right): "Module Enabled / Disabled", the name, a draining bar ----
struct Toast
{
    std::wstring name;
    bool on;
    double at;   // ToastClockMs() when shown or last switched
};

std::vector<Toast> toasts;
constexpr double kToastMs = 2000;

// Sub-millisecond clock for the toast bars: GetTickCount64 only moves in ~16 ms steps, which made the
// bar advance unevenly.
double ToastClockMs()
{
    static LARGE_INTEGER frequency{};
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

// The toggle toast plays one of the bundled sounds (ToggleSound.h, WAVs held in memory) asynchronously, so
// nothing blocks the HUD loop. Default: the arpeggio pair; the Menu Sounds module picks the custom clip, switching it off mutes.
void PlayToggleSound(bool on)
{
    if (!menuSoundsEnabled)
        return;

    if (toggleSoundMode == 2)
        return;   // "None": the module is on but stays silent

    const unsigned char* wav = toggleSoundMode == 1 ? kToggleWavCustom : (on ? kToggleWavOn : kToggleWavOff);
    PlaySoundW((LPCWSTR)wav, nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void PushToast(const std::wstring& name, bool on)
{
    PlayToggleSound(on);

    // Every switch gets its own toast on top of the ones shown, the same module included.
    toasts.push_back({name, on, ToastClockMs()});

    if (toasts.size() > 4)
        toasts.erase(toasts.begin());
}

void DrawToasts(HDC hdc, int width, int height)
{
    const double now = ToastClockMs();

    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [now](const Toast& t) { return now - t.at >= kToastMs; }),
                 toasts.end());

    const int w = 320, h = 64, gap = 8, margin = 24;

    for (size_t i = 0; i < toasts.size(); i++)
    {
        // Oldest at the bottom, each new toast above the ones already shown (they keep their place).
        const Toast& t = toasts[i];
        const int x = width - w - margin;
        const int y = height - margin - h - (int)i * (h + gap);

        const double left = std::clamp(1.0 - (now - t.at) / kToastMs, 0.0, 1.0);
        const COLORREF accent = t.on ? RGB(64,164,255) : RGB(38,84,150);

        DrawRoundBox(hdc, x, y, w, h, 10, RGB(26,28,34));

        DrawTextSimple(hdc, x + 12, y + 8,
                       t.on ? L"[√] Module Enabled" : L"[x] Module Disabled",
                       accent, 15);

        wchar_t secs[16];
        swprintf(secs, 16, L"%.1f", left * kToastMs / 1000.0);
        DrawTextSimple(hdc, x + w - 44, y + 8, secs, RGB(230,230,230), 15);

        DrawTextSimple(hdc, x + 12, y + 32, t.name, RGB(255,255,255), 15);

        // time bar along the bottom edge, filling from left to right until the toast closes
        // The end of the bar is drawn with a partly covered last pixel column, so it glides instead of
        // jumping a whole pixel at a time.
        DrawBox(hdc, x + 6, y + h - 8, w - 12, 4, RGB(0,0,0));
        const double filled = (w - 12) * (1.0 - left);
        const int whole = std::clamp((int)filled, 0, w - 12);
        if (whole > 0)
            DrawBox(hdc, x + 6, y + h - 8, whole, 4, accent);
        if (smoothBits && whole < w - 12)
        {
            const uint32_t cover = (uint32_t)std::lround((filled - whole) * 255.0);
            for (int row = 0; row < 4 && cover > 0; row++)
                BlendPixel(smoothBits, smoothW, smoothH, x + 6 + whole, y + h - 8 + row, accent, cover);
        }
    }

    (void)height;
}

// ------------------------- Chunk Border ---------------------------------------------------------
// Lines on the chunk boundaries around the player, projected from the game's own camera (eye position,
// yaw/pitch and FOV published by the DLL every frame). The overlay has no depth buffer, so the lines
// are drawn over walls too.
std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

struct CamV { double x, y, z; };

// Projects world points with the game's camera (eye, yaw/pitch, vertical FOV) onto the overlay.
struct CamProjector
{
    MinecraftDataProvider::Camera cam{};
    int width = 0, height = 0;
    double rx = 0, ry = 0, rz = 0, ux = 0, uy = 0, uz = 0, fx = 0, fy = 0, fz = 0, focal = 1;

    CamProjector(const MinecraftDataProvider::Camera& c, int w, int h) : cam(c), width(w), height(h)
    {
        const double rad = 3.14159265358979323846 / 180.0;
        const double yaw = cam.yaw * rad, pitch = cam.pitch * rad;
        // Bedrock: yaw 0 looks along +Z, yaw 90 along -X; positive pitch looks down.
        fx = -std::sin(yaw) * std::cos(pitch); fy = -std::sin(pitch); fz = std::cos(yaw) * std::cos(pitch);
        rx = -std::cos(yaw); ry = 0.0; rz = -std::sin(yaw);
        ux = ry * fz - rz * fy; uy = rz * fx - rx * fz; uz = rx * fy - ry * fx;
        focal = (height / 2.0) / std::tan(cam.fov * rad / 2.0);
    }

    CamV ToCam(const CamV& p) const
    {
        const double dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z;
        return CamV{dx * rx + dy * ry + dz * rz, dx * ux + dy * uy + dz * uz, dx * fx + dy * fy + dz * fz};
    }

    void Line(HDC hdc, CamV a, CamV b, HPEN pen) const
    {
        const double kNear = 0.05;
        CamV ca = ToCam(a), cb = ToCam(b);
        if (ca.z < kNear && cb.z < kNear) return;
        if (ca.z < kNear) { const double t = (kNear - ca.z) / (cb.z - ca.z); ca = CamV{ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, kNear}; }
        if (cb.z < kNear) { const double t = (kNear - cb.z) / (ca.z - cb.z); cb = CamV{cb.x + (ca.x - cb.x) * t, cb.y + (ca.y - cb.y) * t, kNear}; }
        const double x0 = width / 2.0 + ca.x / ca.z * focal, y0 = height / 2.0 - ca.y / ca.z * focal;
        const double x1 = width / 2.0 + cb.x / cb.z * focal, y1 = height / 2.0 - cb.y / cb.z * focal;
        const double lim = 20000.0;   // GDI misbehaves with huge coordinates
        if (std::fabs(x0) > lim || std::fabs(x1) > lim || std::fabs(y0) > lim || std::fabs(y1) > lim) return;
        SelectObject(hdc, pen);
        MoveToEx(hdc, (int)std::lround(x0), (int)std::lround(y0), nullptr);
        LineTo(hdc, (int)std::lround(x1), (int)std::lround(y1));
    }
};

void DrawChunkBorders(HDC hdc, int width, int height)
{
    if (!chunkBorderEnabled || menuOpen || hudEditMode || width < 50 || height < 50)
        return;

    MinecraftDataProvider::Camera cam{};
    if (!MinecraftDataProvider::GetCamera(cam))
        return;

    const CamProjector proj(cam, width, height);
    using V = CamV;
    auto line = [&](V a, V b, HPEN pen) { proj.Line(hdc, a, b, pen); };

    const int R = std::clamp(chunkBorderRadius, 1, 3);
    const int cx = (int)std::floor(cam.x / 16.0), cz = (int)std::floor(cam.z / 16.0);
    const int x0 = (cx - R) * 16, x1 = (cx + R + 1) * 16;
    const int z0 = (cz - R) * 16, z1 = (cz + R + 1) * 16;
    const double yMin = std::floor(cam.y / 16.0) * 16.0 - 32.0, yMax = yMin + 80.0;

    HPEN gold = CreatePen(PS_SOLID, 2, RGB(255, 205, 60));
    HPEN blue = CreatePen(PS_SOLID, 1, RGB(70, 140, 255));
    const int pcx0 = cx * 16, pcz0 = cz * 16;

    // Horizontal lines every 16 blocks along each boundary.
    for (double y = yMin; y <= yMax; y += 16.0)
    {
        for (int x = x0; x <= x1; x += 16)
            line({(double)x, y, (double)z0}, {(double)x, y, (double)z1},
                 (x == pcx0 || x == pcx0 + 16) ? gold : blue);
        for (int z = z0; z <= z1; z += 16)
            line({(double)x0, y, (double)z}, {(double)x1, y, (double)z},
                 (z == pcz0 || z == pcz0 + 16) ? gold : blue);
    }
    // Vertical lines on every chunk corner.
    for (int x = x0; x <= x1; x += 16)
        for (int z = z0; z <= z1; z += 16)
        {
            const bool mine = (x == pcx0 || x == pcx0 + 16) && (z == pcz0 || z == pcz0 + 16);
            line({(double)x, yMin, (double)z}, {(double)x, yMax, (double)z}, mine ? gold : blue);
        }

    SelectObject(hdc, GetStockObject(BLACK_PEN));
    DeleteObject(gold);
    DeleteObject(blue);
}

// Hitboxes of other players. The DLL cuts every edge into 8 segments and marks the ones a block-ray from
// our eye reaches; only those are drawn, so a hitbox is never shown through a wall.
// ---- In-game drawing (HudFrameChannel.h, client/Compositor.cpp) ----
MyClientHudFrame::Channel hudFrame;
bool inGameDrawing = false;   // the picture currently goes to the game, the window is blank

// True when the game DLL is ready to draw the HUD into the game's frame and the HUD should hand it over.
// Only while playing: the menu, the HUD editor and the home screen (clickable Replays button) stay in the window.
bool InGameDrawingUsable(int width, int height)
{
    if (menuOpen || hudEditMode || !MinecraftDataProvider::IsInWorld()) return false;
    if (width <= 0 || height <= 0 || (uint32_t)width > MyClientHudFrame::MaxWidth || (uint32_t)height > MyClientHudFrame::MaxHeight)
        return false;
    const DWORD pid = MinecraftDataProvider::GamePid();
    static DWORD openedFor = 0;
    static ULONGLONG lastTry = 0;
    const ULONGLONG now = GetTickCount64();
    if (!hudFrame.Data() || openedFor != pid)
    {
        if (!pid || now - lastTry < 2000) return false;
        lastTry = now;
        if (!hudFrame.Open(pid, false)) return false;
        openedFor = pid;
    }
    const auto* h = hudFrame.Data();
    if (h->dllState == MyClientHudFrame::Unsupported) return false;
    if (!MyClientHudFrame::Fresh(h->dllAt, now, 500)) return false;
    return true;
}

// What Hitbox last saw while playing (the menu pauses it), shown on its detail page.
int hitboxSeenBoxes = -1;   // -1 = not played with Hitbox on yet
bool hitboxSeenCamera = false, hitboxSeenList = false;

void DrawHitboxes(HDC hdc, int width, int height)
{
    if (!hitboxEnabled || menuOpen || hudEditMode || width < 50 || height < 50)
        return;

    MinecraftDataProvider::Camera cam{};
    MyClientTabList::Shared data{};
    hitboxSeenCamera = MinecraftDataProvider::GetCamera(cam);
    hitboxSeenList = MinecraftDataProvider::GetTabList(data);
    hitboxSeenBoxes = hitboxSeenList ? (int)data.boxCount : 0;
    if (inGameDrawing || !hitboxSeenCamera || !hitboxSeenList || data.boxCount == 0)
        return;   // in game drawing: the DLL draws the boxes in the game's own frame

    const CamProjector proj(cam, width, height);
    HPEN pens[4];
    for (int k = 0; k < 4; k++)
        pens[k] = CreatePen(PS_SOLID, 2, HitboxColor(k));

    for (uint32_t i = 0; i < data.boxCount && i < MyClientTabList::MaxBoxes; i++)
    {
        const auto& box = data.boxes[i];
        if (!((hitboxFilter >> (box.kind & 3)) & 1))
            continue;
        HPEN pen = pens[box.kind & 3];
        for (int e = 0; e < 12; e++)
        {
            float a[3], b[3];
            MyClientTabList::Corner(box, MyClientTabList::kEdges[e][0], a);
            MyClientTabList::Corner(box, MyClientTabList::kEdges[e][1], b);
            const int n = MyClientTabList::BoxSegments;
            for (int s = 0; s < n; s++)
            {
                if (!((box.visible[e] >> s) & 1))
                    continue;
                const double t0 = (double)s / n, t1 = (double)(s + 1) / n;
                proj.Line(hdc,
                          CamV{a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0, a[2] + (b[2] - a[2]) * t0},
                          CamV{a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1, a[2] + (b[2] - a[2]) * t1},
                          pen);
            }
        }
    }

    SelectObject(hdc, GetStockObject(BLACK_PEN));
    for (HPEN p : pens)
        DeleteObject(p);
}

void ToggleFeature(
    DetailFeature f)
{
    switch (f)
    {
    case DetailFeature::Keystrokes:
        keystrokesEnabled =
            !keystrokesEnabled;
        break;

    case DetailFeature::CPS:
        cpsEnabled =
            !cpsEnabled;
        break;

    case DetailFeature::Armor:
        armorHudEnabled =
            !armorHudEnabled;
        break;

    case DetailFeature::Arrows:
        arrowCounterEnabled =
            !arrowCounterEnabled;
        break;

    case DetailFeature::FPS:
        fpsEnabled =
            !fpsEnabled;
        break;

    case DetailFeature::PartyAutoAccept:
        partyAutoAcceptEnabled =
            !partyAutoAcceptEnabled;
        break;

    case DetailFeature::BlockOutline:
        blockOutlineEnabled =
            !blockOutlineEnabled;

        if (!blockOutlineEnabled)
        {
            ClearBlockOutlineData();
        }

        break;

    case DetailFeature::Zoom:
        zoomEnabled = !zoomEnabled;

        if (!zoomEnabled)
        {
            zoomActive = false;
        }

        break;

    case DetailFeature::Resources:
        resourceCounterEnabled =
            !resourceCounterEnabled;
        break;

    case DetailFeature::Fullbright:
        fullbrightEnabled =
            !fullbrightEnabled;
        break;

    case DetailFeature::Nametag:
        nametagEnabled =
            !nametagEnabled;
        break;

    case DetailFeature::Ping:
        pingEnabled =
            !pingEnabled;
        break;

    case DetailFeature::Textures:
        texturesEnabled =
            !texturesEnabled;
        break;

    case DetailFeature::AutoRq:
        autoRqEnabled =
            !autoRqEnabled;
        break;

    case DetailFeature::FeatureList:
        featureListEnabled =
            !featureListEnabled;
        break;

    case DetailFeature::ShulkerPreview:
        shulkerEnabled =
            !shulkerEnabled;
        break;

    case DetailFeature::TabList:
        tabListEnabled =
            !tabListEnabled;
        break;

    case DetailFeature::ChunkBorder:
        chunkBorderEnabled =
            !chunkBorderEnabled;
        break;

    case DetailFeature::Hitbox:
        hitboxEnabled =
            !hitboxEnabled;
        break;

    case DetailFeature::Nick:
        nickEnabled =
            !nickEnabled;
        break;

    case DetailFeature::Replay:
        replayEnabled =
            !replayEnabled;
        break;

    case DetailFeature::MenuSounds:
        menuSoundsEnabled =
            !menuSoundsEnabled;
        break;

    case DetailFeature::Lite:
        liteEnabled =
            !liteEnabled;
        ApplyLite();
        RequestRenderDistance();   // ON with the Render Distance row already on: apply it now (OFF: nothing)
        break;

    case DetailFeature::FastInventory:
        fastInventoryEnabled =
            !fastInventoryEnabled;
        ApplyLite();   // same generated pack
        break;

    case DetailFeature::HitEffect:
        hitEffectEnabled = !hitEffectEnabled;
        if (hitEffectMagic) ApplyLite();   // the pack carries the enchanted-hit particle
        break;

    case DetailFeature::FrameGen:
        frameGenEnabled = !frameGenEnabled;
        break;

    case DetailFeature::DayTimer:
        dayTimerEnabled = !dayTimerEnabled;
        break;

    case DetailFeature::HealthWarning:
        healthWarningEnabled = !healthWarningEnabled;
        break;

    case DetailFeature::DebugMenu:
        debugMenuEnabled = !debugMenuEnabled;
        if (!debugMenuEnabled) debugScreenShown = false;
        break;

    case DetailFeature::BlockDetails:
        blockDetailsEnabled = !blockDetailsEnabled;
        break;

    case DetailFeature::LeftHand:
        leftHandEnabled =
            !leftHandEnabled;
        ApplyLite();   // same generated pack
        break;

    case DetailFeature::HiveStats:
        hiveStatsEnabled =
            !hiveStatsEnabled;
        break;

    case DetailFeature::HiveUtils:
        // ON -> pause the whole group. OFF -> resume it; with nothing switched on inside yet,
        // open the details so the sub-features can be chosen.
        if (hiveEnabled && (partyAutoAcceptEnabled || autoRqEnabled))
            hiveEnabled = false;
        else
        {
            hiveEnabled = true;
            if (!partyAutoAcceptEnabled && !autoRqEnabled)
                detailFeature = DetailFeature::HiveUtils;
        }
        break;

    default:
        break;
    }

    if (applyingPreset)
        return;   // the preset saves once and shows one toast for all of its modules

    SaveConfig();
    PushToast(FeatureTitle(f), FeatureEnabled(f));
}


// Feature keys must not fire while the player types in chat or has a game screen (inventory, pause)
// open. In those screens the game shows the mouse cursor; during play it hides it. If the cursor
// has never been seen hidden on this PC, the detection is not usable and keys work as before.
bool GameOwnsKeyboard()
{
    static bool everHidden = false;

    CURSORINFO info{};
    info.cbSize = sizeof(info);

    if (!GetCursorInfo(&info))
        return true;

    const bool hidden =
        (info.flags & CURSOR_SHOWING) == 0 ||
        info.hCursor == nullptr;

    if (hidden)
        everHidden = true;

    return !everHidden || hidden;
}


void CheckShortcut(
    int vk,
    bool& previousState,
    DetailFeature f)
{
    if (vk == 0 ||
        vk == shortcuts.menuKey ||
        vk == VK_ESCAPE)
    {
        previousState = false;
        return;
    }

    bool down =
        IsKeyDown(vk);

    if (down &&
        !previousState &&
        gameActive &&
        !menuOpen &&
        !waitingForShortcut &&
        GameOwnsKeyboard())
    {
        ToggleFeature(f);
    }

    previousState = down;
}


// ------------------------- Navigation / menu ----------------

bool FeatureGridMode();   // defined with the card grid below
void FeatureFilterCounts(int filter, int& total, int& enabled);   // defined with the card list below

void DrawNavigation(HDC hdc)
{
    const ThemeColors t = CurrentTheme();

    // Sidebar panel: a little darker than the page, square on the inner edge.
    const COLORREF side = MixColor(t.bg, RGB(0,0,0), 0.30f);
    DrawRoundBox(hdc, 10, 10, 176, 590, 18, side);
    DrawBox(hdc, 150, 10, 36, 590, side);
    DrawBox(hdc, 185, 10, 1, 590, t.card);

    // The logo mark (resource 2, transparent background, see myclient.rc) stands in for the client name.
    static HICON logo = nullptr;
    static bool logoTried = false;

    if (!logoTried)
    {
        logoTried = true;
        logo = (HICON)LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(2),
            IMAGE_ICON,
            44, 44,
            LR_DEFAULTCOLOR
        );
    }

    if (!DrawLogo(hdc, 22, 22, 44))
    {
        if (logo)
            DrawIconEx(hdc, 22, 22, logo, 44, 44, 0, nullptr, DI_NORMAL);
        else
            DrawTextSimple(hdc, 24, 30, clientName, RGB(255,255,255), 18);
    }

    DrawTextSimple(hdc, 76, 26, L"Bedrock", t.muted, 13);
    DrawTextSimple(hdc, 76, 46, L"v0.5.0", t.muted, 13);

    DrawTextSimple(hdc, 26, 92, L"MODULES", t.muted, 11);

    const struct { const wchar_t* label; const wchar_t* glyph; } items[1] =
    {
        { L"Modules",   L"\u25A6" }
    };

    for (int i = 0; i < 1; i++)
    {
        const RECT r = SideItemRect(i);
        const int w = r.right - r.left;
        const int h = r.bottom - r.top;

        const bool selected =
            selectedCategory == MenuCategory::Feature &&
            featureFilter == i;

        if (selected)
            DrawRoundBox(hdc, r.left, r.top, w, h, 10, t.tile);

        DrawTextCentered(
            hdc, r.left + 6, r.top, 28, h,
            items[i].glyph,
            selected ? t.accentHi : t.muted,
            16,
            L"Segoe UI Symbol"
        );

        DrawTextSimple(
            hdc, r.left + 40, r.top + 9,
            items[i].label,
            selected ? RGB(255,255,255) : t.text,
            16
        );

        int total = 0;
        int enabled = 0;
        FeatureFilterCounts(i, total, enabled);

        const std::wstring count =
            std::to_wstring(total);

        DrawTextSimple(
            hdc, r.right - 14 - (int)count.size() * 9, r.top + 11,
            count, t.muted, 14
        );
    }

    // Presets: right under Modules
    {
        const RECT r = SideItemRect(1);
        const int w = r.right - r.left, h = r.bottom - r.top;
        const bool selected = selectedCategory == MenuCategory::Presets;
        if (selected)
            DrawRoundBox(hdc, r.left, r.top, w, h, 10, t.tile);
        DrawTextCentered(hdc, r.left + 6, r.top, 28, h, L"\u2630", selected ? t.accentHi : t.muted, 16, L"Segoe UI Symbol");
        DrawTextSimple(hdc, r.left + 40, r.top + 9, L"Presets", selected ? RGB(255,255,255) : t.text, 16);
        const std::wstring count = std::to_wstring(presets.size());
        DrawTextSimple(hdc, r.right - 14 - (int)count.size() * 9, r.top + 11, count, t.muted, 14);
    }

    // Bottom buttons
    DrawRoundBox(
        hdc, kEditHudRect.left, kEditHudRect.top,
        kEditHudRect.right - kEditHudRect.left,
        kEditHudRect.bottom - kEditHudRect.top,
        10, t.card
    );

    DrawTextCentered(
        hdc, kEditHudRect.left + 6, kEditHudRect.top, 28,
        kEditHudRect.bottom - kEditHudRect.top,
        L"\u270E", t.accentHi, 16, L"Segoe UI Symbol"
    );

    DrawTextSimple(hdc, kEditHudRect.left + 40, kEditHudRect.top + 9, L"Edit HUD", t.text, 15);

    const bool settingsOn = selectedCategory == MenuCategory::Settings;

    DrawRoundBox(
        hdc, kClientSettingsRect.left, kClientSettingsRect.top,
        kClientSettingsRect.right - kClientSettingsRect.left,
        kClientSettingsRect.bottom - kClientSettingsRect.top,
        10, settingsOn ? t.tile : t.card
    );

    DrawTextCentered(
        hdc, kClientSettingsRect.left + 6, kClientSettingsRect.top, 28,
        kClientSettingsRect.bottom - kClientSettingsRect.top,
        L"\u2699", t.accentHi, 17, L"Segoe UI Symbol"
    );

    DrawTextSimple(hdc, kClientSettingsRect.left + 40, kClientSettingsRect.top + 9, L"Client Settings", t.text, 15);
}


// Module cards: 3 columns x N rows while no details panel is open
// (Flarial-style grid); a single compact column while details are open.
const DetailFeature kFeatureList[] =
{
    DetailFeature::Keystrokes,
    DetailFeature::CPS,
    DetailFeature::Armor,
    DetailFeature::Arrows,
    DetailFeature::FPS,
    DetailFeature::BlockOutline,
    DetailFeature::Zoom,
    DetailFeature::Resources,
    DetailFeature::Fullbright,
    DetailFeature::Nametag,
    DetailFeature::Ping,
    DetailFeature::Textures,
    DetailFeature::HiveUtils,
    DetailFeature::FeatureList,
    DetailFeature::ShulkerPreview,
    DetailFeature::TabList,
    DetailFeature::ChunkBorder,
    DetailFeature::Hitbox,
    DetailFeature::Nick,
    DetailFeature::Replay,
    DetailFeature::MenuSounds,
    DetailFeature::Lite,
    DetailFeature::FastInventory,
    DetailFeature::LeftHand,
    DetailFeature::HitEffect,
    DetailFeature::FrameGen,
    DetailFeature::MenuOpen,
    DetailFeature::DayTimer,
    DetailFeature::DebugMenu,
    DetailFeature::BlockDetails,
    DetailFeature::HealthWarning
};

const int kFeatureCount = 31;

// Modules that are only settings (no on / off): Menu Open. Their Mods row has no switch and opens the page.
bool FeatureHasSwitch(DetailFeature f) { return f != DetailFeature::MenuOpen; }

// Names of the switched-on features (the list itself excluded), longest name first.
std::vector<std::wstring> EnabledFeatureNames()
{
    std::vector<std::wstring> names;

    for (int i = 0; i < kFeatureCount; i++)
    {
        const DetailFeature f = kFeatureList[i];

        if (f != DetailFeature::FeatureList && FeatureEnabled(f))
            names.push_back(FeatureTitle(f));
    }

    std::stable_sort(names.begin(), names.end(),
        [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });

    return names;
}

bool ContainsNoCase(
    const std::wstring& text,
    const std::wstring& part)
{
    if (part.empty())
        return true;

    auto lower =
        [](std::wstring v)
        {
            for (auto& c : v)
                c = (wchar_t)towlower(c);

            return v;
        };

    return lower(text).find(lower(part)) !=
           std::wstring::npos;
}




// Indices into kFeatureList that match the search text.
// HUD modules draw something on screen; the rest are utilities.
bool FeatureIsHud(DetailFeature f)
{
    switch (f)
    {
    case DetailFeature::Keystrokes:
    case DetailFeature::CPS:
    case DetailFeature::Armor:
    case DetailFeature::Arrows:
    case DetailFeature::FPS:
    case DetailFeature::Resources:
    case DetailFeature::Ping:
    case DetailFeature::FeatureList:
    case DetailFeature::ShulkerPreview:
    case DetailFeature::TabList:
    case DetailFeature::DayTimer:
    case DetailFeature::BlockDetails:
        return true;

    default:
        return false;
    }
}

bool FeatureInFilter(DetailFeature f, int filter)
{
    switch (filter)
    {
    case 1:  return FeatureIsHud(f);
    case 2:  return !FeatureIsHud(f);
    default: return true;
    }
}

// How many modules the sidebar filter holds (and how many of them are on).
void FeatureFilterCounts(int filter, int& total, int& enabled)
{
    total = 0;
    enabled = 0;

    for (int i = 0; i < kFeatureCount; i++)
    {
        const DetailFeature f = kFeatureList[i];

        if (!FeatureInFilter(f, filter))
            continue;

        total++;

        if (FeatureEnabled(f))
            enabled++;
    }
}

std::vector<int> VisibleFeatures()
{
    std::vector<int> visible;

    for (int i = 0; i < kFeatureCount; i++)
    {
        if (FeatureInFilter(kFeatureList[i], featureFilter) &&
            ContainsNoCase(
                FeatureTitle(kFeatureList[i]),
                searchQuery))
        {
            visible.push_back(i);
        }
    }

    // Alphabetical (case-insensitive).
    std::sort(
        visible.begin(),
        visible.end(),
        [](int a, int b)
        {
            return _wcsicmp(
                       FeatureTitle(kFeatureList[a]).c_str(),
                       FeatureTitle(kFeatureList[b]).c_str()
                   ) < 0;
        }
    );

    return visible;
}

// The Modules page is always the list + settings view: make sure one feature is selected.
void EnsureDetailSelected()
{
    if (detailFeature == DetailFeature::None)
        detailFeature = DetailFeature::Armor;
}

bool FeatureGridMode()
{
    return detailFeature ==
           DetailFeature::None;
}


// The card grid scrolls by whole rows (mouse wheel): the cards keep their size, however many
// features there are. 4 rows fit inside the menu panel.
int featureScrollRow = 0;
// Card geometry (content coordinates)
const int kCardCols = 3;
const int kCardW = 224;
const int kCardH = 196;
const int kCardGap = 14;
const int kFeatureRowsVisible = 2;
const int kGridWidth = kCardCols * kCardW + (kCardCols - 1) * kCardGap;
const int kGridLeft = 205;   // right of the sidebar
const int kGridTop = 112;    // below the title / search row

int FeatureRowCount(int cardCount)
{
    return (cardCount + kCardCols - 1) / kCardCols;
}

// Clamp to the rows that exist for `cardCount` cards.
void ClampFeatureScroll(int cardCount)
{
    featureScrollRow =
        std::clamp(
            featureScrollRow,
            0,
            std::max(0, FeatureRowCount(cardCount) - kFeatureRowsVisible)
        );
}

// Is the card at list position `i` inside the visible rows? (Hidden cards are neither drawn nor clickable.)
bool FeatureCardOnScreen(int i)
{
    if (!FeatureGridMode())
        return true;

    const int row = i / kCardCols - featureScrollRow;

    return row >= 0 && row < kFeatureRowsVisible;
}

void FeatureCardRect(
    int i,
    int& x,
    int& y,
    int& w,
    int& h)
{
    if (FeatureGridMode())
    {
        const int cols = kCardCols;
        const int cardW = kCardW;
        const int cardH = kCardH;
        const int gap = kCardGap;

        x = kGridLeft + (i % cols) * (cardW + gap);
        y = kGridTop + (i / cols - featureScrollRow) * (cardH + gap);
        w = cardW;
        h = cardH;
    }
    else
    {
        x = 190;
        y = 160 + i * 36;
        w = 320;
        h = 34;
    }
}


// Star button of a card (favourite on/off).


// Gear button of a grid card (opens the details/settings panel).
void FeatureGearRect(
    int i,
    int& x,
    int& y,
    int& w,
    int& h)
{
    int cx, cy, cw, ch;
    FeatureCardRect(i, cx, cy, cw, ch);

    x = cx + 12;
    y = cy + ch - 44;
    w = 36;
    h = 32;
}


// Key badge of a grid card (top right): click, then press a key.
void FeatureKeyRect(
    int i,
    int& x,
    int& y,
    int& w,
    int& h)
{
    int cx, cy, cw, ch;
    FeatureCardRect(i, cx, cy, cw, ch);

    w = 66;
    h = 26;
    x = cx + cw - 10 - w;
    y = cy + 10;
}


// ON / OFF switch of a grid card (bottom right).
void FeatureSwitchRect(
    int i,
    int& x,
    int& y,
    int& w,
    int& h)
{
    int cx, cy, cw, ch;
    FeatureCardRect(i, cx, cy, cw, ch);

    w = 48;
    h = 24;
    x = cx + cw - 14 - w;
    y = cy + ch - 40;
}


// Symbol shown on top of a card (Segoe UI Symbol characters, present on every Windows 10/11).
const wchar_t* FeatureGlyph(DetailFeature f)
{
    switch (f)
    {
    case DetailFeature::Keystrokes:      return L"\u2328";
    case DetailFeature::CPS:             return L"\u26A1";
    case DetailFeature::Armor:           return L"\u25C6";
    case DetailFeature::Arrows:          return L"\u27A4";
    case DetailFeature::FPS:             return L"\u25D4";
    case DetailFeature::PartyAutoAccept: return L"\u2714";
    case DetailFeature::BlockOutline:    return L"\u25A2";
    case DetailFeature::Zoom:            return L"\u2295";
    case DetailFeature::Resources:       return L"\u25A6";
    case DetailFeature::Fullbright:      return L"\u2600";
    case DetailFeature::Nametag:         return L"\u263A";
    case DetailFeature::Ping:            return L"\u21C5";
    case DetailFeature::Textures:        return L"\u25A8";
    case DetailFeature::AutoRq:          return L"\u27F3";
    case DetailFeature::FeatureList:     return L"\u2630";
    case DetailFeature::ShulkerPreview:  return L"\u25A3";
    case DetailFeature::HiveUtils:       return L"\u2B22";
    case DetailFeature::TabList:         return L"\u2261";
    case DetailFeature::ChunkBorder:     return L"\u25A6";
    case DetailFeature::Hitbox:          return L"\u25A1";
    case DetailFeature::Nick:            return L"\u270E";
    case DetailFeature::Replay:          return L"\u25CF";
    case DetailFeature::MenuSounds:      return L"\u266A";
    case DetailFeature::Lite:            return L"\u2601";
    case DetailFeature::FastInventory:   return L"\u25A3";
    case DetailFeature::LeftHand:        return L"\u270B";
    case DetailFeature::HitEffect:       return L"\u2727";
    case DetailFeature::FrameGen:        return L"\u25B6";
    case DetailFeature::MenuOpen:        return L"\u2630";
    case DetailFeature::DayTimer:        return L"\u263C";
    case DetailFeature::DebugMenu:       return L"\u2261";
    case DetailFeature::BlockDetails:    return L"\u25A7";
    case DetailFeature::HealthWarning:   return L"\u2665";
    default:                             return L"\u25CF";
    }
}


// Two short lines under the card title ('|' splits them).
const wchar_t* FeatureDescription(DetailFeature f)
{
    switch (f)
    {
    case DetailFeature::Keystrokes:      return L"Shows WASD, jump and mouse|inputs on screen";
    case DetailFeature::CPS:             return L"Displays your left and right|clicks per second";
    case DetailFeature::Armor:           return L"Your armor as icons, with|durability (optional)";
    case DetailFeature::Arrows:          return L"Arrows left in your|inventory";
    case DetailFeature::FPS:             return L"Current frames per second|";
    case DetailFeature::PartyAutoAccept: return L"Joins Hive party invites|from players you list";
    case DetailFeature::BlockOutline:    return L"Changes the colour of the|block outline";
    case DetailFeature::Zoom:            return L"Hold a key to zoom in|on what you look at";
    case DetailFeature::Resources:       return L"Counts resources in your|inventory";
    case DetailFeature::Fullbright:      return L"Keeps dark areas bright|without torches";
    case DetailFeature::Nametag:         return L"Shows your own name tag|in third person";
    case DetailFeature::Ping:            return L"Shows your delay to the|server you are on";
    case DetailFeature::Textures:        return L"Switch texture packs and|install many at once";
    case DetailFeature::AutoRq:          return L"Queues the next Hive game|when you or your team are out";
    case DetailFeature::FeatureList:     return L"Lists the features you|have switched on";
    case DetailFeature::ShulkerPreview:  return L"Hover a shulker box to|see what is inside";
    case DetailFeature::HiveUtils:       return L"Hive helpers: auto accept|and auto queue";
    case DetailFeature::TabList:         return L"Lists the players in your|world or server";
    case DetailFeature::ChunkBorder:     return L"Shows the chunk borders|around you";
    case DetailFeature::Hitbox:          return L"Shows the hitboxes of players|you can actually see";
    case DetailFeature::Nick:            return L"Changes your own name tag|in third person (only you)";
    case DetailFeature::Replay:          return L"Records the game's packets|to a file (early test)";
    case DetailFeature::MenuSounds:      return L"Plays a sound when you|switch a module on or off";
    case DetailFeature::Lite:            return L"Removes fog, clouds, sky and|weather for more FPS";
    case DetailFeature::FastInventory:   return L"Opens the inventory and chests|with a 0.1 s animation";
    case DetailFeature::LeftHand:        return L"Shows your arm and item|on the left (first person)";
    case DetailFeature::HitEffect:       return L"Java-style crit and|enchant hit particles";
    case DetailFeature::FrameGen:        return L"Adds made-up frames between|the game's frames (x2 / x3)";
    case DetailFeature::MenuOpen:        return L"The key that opens and|closes this menu";
    case DetailFeature::DayTimer:        return L"Time left until night|and until morning";
    case DetailFeature::DebugMenu:       return L"Java-style F3 screen:|position, FPS, memory";
    case DetailFeature::BlockDetails:    return L"The block you look at:|name, add-on and tool";
    case DetailFeature::HealthWarning:   return L"Screen edges turn red|when your health is low";
    default:                             return L"|";
    }
}

// Text of the key badge: "None" or the key name (shortened).
std::wstring FeatureKeyLabel(DetailFeature f)
{
    if (waitingForShortcut && shortcutFeature == f)
        return L"Press key";

    const int* vk = GetShortcutPtr(f);

    if (!vk || *vk == 0)
        return L"None";

    std::wstring name = KeyName(*vk);

    if (name.size() > 9)
        name = name.substr(0, 9);

    return name;
}


void DrawFeatureCard(
    HDC hdc,
    int pos,
    int idx)
{
    const ThemeColors t = CurrentTheme();

    const DetailFeature f =
        kFeatureList[idx];

    const bool on =
        FeatureEnabled(f);

    const std::wstring title =
        FeatureTitle(f);

    int x, y, w, h;
    FeatureCardRect(pos, x, y, w, h);

    if (on && FeatureGridMode())
    {
        // switched-on cards get a thin accent border
        DrawRoundBox(hdc, x, y, w, h, 12, t.accent);
        DrawRoundBox(hdc, x + 1, y + 1, w - 2, h - 2, 11, t.card);
    }
    else
    {
        DrawRoundBox(
            hdc, x, y, w, h, 12,
            t.card
        );
    }

    const COLORREF pillColor =
        on ? t.accent
           : t.off;

    const std::wstring state =
        on ? L"Enabled"
           : L"Disabled";

    if (FeatureGridMode())
    {
        // Key badge: top-right corner
        int kx, ky, kw, kh;
        FeatureKeyRect(pos, kx, ky, kw, kh);

        const bool binding = waitingForShortcut && shortcutFeature == f;

        DrawRoundBox(hdc, kx, ky, kw, kh, 8, binding ? t.accent : t.button);

        DrawTextCentered(
            hdc, kx, ky, kw, kh,
            FeatureKeyLabel(f),
            binding ? t.onAccent : t.muted,
            12
        );

        // Feature symbol
        const int tileSize = 56;
        const int tileX = x + (w - tileSize) / 2;
        const int tileY = y + 20;

        DrawRoundBox(
            hdc, tileX, tileY, tileSize, tileSize, 14,
            on ? t.tile : t.button
        );

        DrawTextCentered(
            hdc, tileX, tileY, tileSize, tileSize,
            FeatureGlyph(f),
            on ? t.accentHi : t.muted,
            28,
            L"Segoe UI Symbol"
        );

        DrawTextCentered(
            hdc, x + 8, y + 82, w - 16, 26,
            title,
            t.text,
            title.size() > 16 ? 15 : 18
        );

        // Description (two lines)
        const std::wstring desc = FeatureDescription(f);
        const size_t bar = desc.find(L'|');
        const std::wstring line1 = desc.substr(0, bar);
        const std::wstring line2 = bar == std::wstring::npos ? std::wstring() : desc.substr(bar + 1);

        DrawTextCentered(hdc, x + 8, y + 108, w - 16, 18, line1, t.muted, 12);

        if (!line2.empty())
            DrawTextCentered(hdc, x + 8, y + 126, w - 16, 18, line2, t.muted, 12);

        // Divider
        DrawRoundBox(hdc, x + 1, y + h - 52, w - 2, 1, 0, t.button);

        // Footer: gear on the left, ON/OFF switch on the right
        int gx, gy, gw, gh;
        FeatureGearRect(pos, gx, gy, gw, gh);

        DrawRoundBox(
            hdc, gx, gy, gw, gh, 8,
            t.button
        );

        DrawTextCentered(
            hdc, gx, gy, gw, gh,
            L"\u2699",
            t.accentHi,
            20,
            L"Segoe UI Symbol"
        );

        int sx, sy, sw, sh;
        FeatureSwitchRect(pos, sx, sy, sw, sh);

        // Centred between the gear button and the switch.
        DrawTextCentered(
            hdc, gx + gw, sy, sx - (gx + gw), sh,
            on ? L"ON" : L"OFF",
            on ? t.accentHi : t.muted,
            13
        );

        DrawRoundBox(hdc, sx, sy, sw, sh, 12, on ? t.accent : t.off);

        const int knob = sh - 6;

        DrawRoundBox(
            hdc,
            on ? sx + sw - knob - 3 : sx + 3,
            sy + 3, knob, knob, knob / 2,
            RGB(255,255,255)
        );
    }
    else
    {
        DrawTextSimple(
            hdc,
            x + 40,
            y + 8,
            title,
            t.text,
            16
        );

        DrawRoundBox(
            hdc, x + w - 96, y + 5, 86, 26, 8,
            pillColor
        );

        DrawTextCentered(
            hdc, x + w - 96, y + 5, 86, 26,
            state,
            on ? t.onAccent : t.text,
            13
        );
    }
}


void DrawFeatureMenu(HDC hdc)
{
    const ThemeColors t = CurrentTheme();

    const int x = kGridLeft;

    static const wchar_t* const titles[3] =
        { L"Modules", L"Modules", L"Modules" };

    DrawTextSimple(
        hdc,
        x,
        22,
        titles[std::clamp(featureFilter, 0, 2)],
        RGB(255,255,255),
        28
    );

    int total = 0;
    int enabled = 0;
    FeatureFilterCounts(featureFilter, total, enabled);

    DrawTextSimple(
        hdc,
        x,
        66,
        std::to_wstring(enabled) + L" of " + std::to_wstring(total) + L" modules enabled",
        t.muted,
        14
    );

    // Search box (top right, aligned with the grid)
    {
        const int sx = kSearchRect.left;
        const int sy = kSearchRect.top;
        const int sw = kSearchRect.right - kSearchRect.left;
        const int sh = kSearchRect.bottom - kSearchRect.top;

        DrawRoundBox(hdc, sx, sy, sw, sh, 12, searchActive ? t.accent : t.button);
        DrawRoundBox(hdc, sx + 1, sy + 1, sw - 2, sh - 2, 11, t.bg);

        if (searchQuery.empty() && !searchActive)
            DrawTextSimple(hdc, sx + 16, sy + 10, L"Search modules", t.muted, 15);
        else
            DrawTextSimple(hdc, sx + 16, sy + 10,
                           searchQuery + (searchActive ? L"|" : L""), t.text, 15);
    }

    const std::vector<int> visible =
        VisibleFeatures();

    if (visible.empty())
    {
        DrawTextSimple(
            hdc,
            kGridLeft,
            kGridTop + 10,
            L"No matching modules",
            t.muted,
            16
        );
    }

    ClampFeatureScroll((int)visible.size());

    for (int pos = 0;
         pos < (int)visible.size();
         pos++)
    {
        if (!FeatureCardOnScreen(pos))
            continue;

        DrawFeatureCard(
            hdc,
            pos,
            visible[pos]
        );
    }

    // Scrollbar (only when some rows are hidden)
    const int rows = FeatureRowCount((int)visible.size());

    if (rows > kFeatureRowsVisible)
    {
        const int trackTop = kGridTop;
        const int trackHeight = kFeatureRowsVisible * (kCardH + kCardGap) - kCardGap;
        const int thumbHeight = std::max(30, trackHeight * kFeatureRowsVisible / rows);
        const int maxScroll = rows - kFeatureRowsVisible;
        const int thumbTop =
            trackTop + (trackHeight - thumbHeight) * featureScrollRow / maxScroll;

        DrawRoundBox(hdc, kGridLeft + kGridWidth + 10, trackTop, 8, trackHeight, 4, t.card);
        DrawRoundBox(hdc, kGridLeft + kGridWidth + 10, thumbTop, 8, thumbHeight, 4, t.accent);
    }

    // Hint row
    DrawRoundBox(hdc, x, 538, kGridWidth, 1, 0, t.card);

    const struct { const wchar_t* chip; int chipW; const wchar_t* label; int labelW; } hints[3] =
    {
        { L"Click",       56,  L"Toggle",   52 },
        { L"Right-click", 100, L"Settings", 62 },
        { L"Wheel",       58,  L"Scroll",   48 }
    };

    int hx = x;

    for (const auto& hint : hints)
    {
        DrawRoundBox(hdc, hx, 552, hint.chipW, 26, 8, t.button);

        DrawTextCentered(
            hdc, hx, 552, hint.chipW, 26,
            hint.chip, t.text, 12
        );

        DrawTextSimple(hdc, hx + hint.chipW + 10, 556, hint.label, t.muted, 13);

        hx += hint.chipW + 10 + hint.labelW + 30;
    }
}


// Party page (layout space, x = kPaneX): the name box and its Add nametag button.
RECT PartyInputRect() { return RECT{535, 70 + 108, 535 + 206, 70 + 108 + 34}; }   // 535 = kPaneX (declared later)
RECT PartyAddRect() { return RECT{535 + 212, 70 + 108, 535 + 310, 70 + 108 + 34}; }

void DrawPartyDetails(
    HDC hdc,
    int x,
    int y)
{
    if (detailFeature != DetailFeature::HiveUtils)
        DrawTextSimple(
            hdc,
            x,
            y + 50,
            L"Party Auto Accept",
            RGB(255,255,255),
            20
        );

    DrawTextSimple(
        hdc,
        x,
        y + 82,
        L"Registered GameTags (max 64, ASCII)",
        RGB(220,220,220),
        15
    );

    // Name box + "Add nametag" first, the saved players below them.
    const int addY = y + 108;
    {
        const ThemeColors t = CurrentTheme();
        const RECT box = PartyInputRect(), add = PartyAddRect();
        DrawRoundBox(hdc, box.left, box.top, box.right - box.left, box.bottom - box.top, 8,
                     partyAddingPlayer ? MixColor(t.button, t.accent, 0.35f) : t.button);
        const bool empty = partyInput.empty();
        DrawTextSimple(hdc, box.left + 10, box.top + 8,
                       (empty && !partyAddingPlayer ? std::wstring(L"GameTag") : partyInput) +
                           (partyAddingPlayer && (GetTickCount64() / 500) % 2 ? L"|" : L""),
                       empty && !partyAddingPlayer ? t.muted : t.text, 14);
        DrawRoundBox(hdc, add.left, add.top, add.right - add.left, add.bottom - add.top, 8, t.accent);
        DrawTextCentered(hdc, add.left, add.top, add.right - add.left, add.bottom - add.top, L"Add nametag", t.onAccent, 13);
    }

    int listY =
        addY + 34 + 12;

    const int maxVisible = 6;

    int count = 0;

    for (
        size_t i = 0;
        i < partyPlayers.size() &&
        count < maxVisible;
        i++, count++)
    {
        int yy =
            listY +
            count * 34;

        DrawBox(
            hdc,
            x,
            yy,
            230,
            28,
            RGB(38,38,38)
        );

        DrawTextSimple(
            hdc,
            x + 8,
            yy + 5,
            partyPlayers[i],
            RGB(235,235,235),
            14
        );

        DrawTrashButton(hdc, RECT{x + 238, yy, x + 238 + 28, yy + 28}, TrashArmed(L"party:" + partyPlayers[i]));
    }

    if (partyPlayers.size() >
        maxVisible)
    {
        DrawTextSimple(
            hdc,
            x,
            listY +
                maxVisible * 34,
            L"More players saved...",
            RGB(130,130,130),
            12
        );
    }


    DrawTextSimple(
        hdc,
        x,
        y + 400,
        MinecraftDataProvider::GetPartyStatusText(),
        RGB(140,140,140),
        13
    );

    DrawTextSimple(
        hdc,
        x,
        y + 423,
        L"Registered tags only. Live Hive test pending.",
        RGB(140,140,140),
        13
    );
}


void DrawAutoRqDetails(HDC hdc, int x, int y);
void DrawHiveStatsDetails(HDC hdc, int x, int y);
void DrawTexturesDetails(HDC hdc, int x, int y);   // defined below, with the other Textures helpers

// Highest hook rates seen while actually playing (menu closed). With this menu open the game stops
// drawing things like the block selection, so the live numbers read 0 here; these show what happened
// just before the menu was opened.
MyClientFeatures::Stats playingRates{};
bool playingRatesKnown = false;
void TrackPlayingRates(bool playing)
{
    static bool wasPlaying = false;
    MyClientFeatures::Stats r{};
    if (!playing || !MinecraftDataProvider::GetHookRates(r)) { wasPlaying = false; return; }
    if (!wasPlaying) playingRates = {};   // a new stretch of play starts from zero
    wasPlaying = true;
    playingRatesKnown = true;
    auto peak = [](uint32_t& a, uint32_t b) { if (b > a) a = b; };
    peak(playingRates.lightBuild, r.lightBuild); peak(playingRates.lightSplit, r.lightSplit);
    peak(playingRates.lightCreate, r.lightCreate); peak(playingRates.nameTag, r.nameTag);
    peak(playingRates.nameTagLocal, r.nameTagLocal); peak(playingRates.outline, r.outline);
    peak(playingRates.tessColor, r.tessColor);
}

// "Game calls per second" line for the detail screens: shows whether the game runs a hooked function.
std::wstring HookRateLine(int which)
{
    MyClientFeatures::Stats r{};
    if (playingRatesKnown)
        r = playingRates;
    else if (!MinecraftDataProvider::GetHookRates(r))
        return L"Game calls per second: ---";

    auto n = [](uint32_t v) { return std::to_wstring(v); };

    switch (which)
    {
    case 0:
        return L"Game calls per second (while playing): light map " + n(r.lightBuild) + L" / " + n(r.lightSplit) +
               L", light data " + n(r.lightCreate);
    case 1:
        return L"Game calls per second (while playing): name tags " + n(r.nameTag) + L", about you " + n(r.nameTagLocal);
    default:
        return L"Game calls per second (while playing): outline " + n(r.outline) + L", colour " + n(r.tessColor);
    }
}


// ---- Hive Utils details: a list of its features on the left, the selected feature's page on the right ----
// Layout space of the details pages (origin x = 535, y = 70).
constexpr int kPaneX = 535;       // left edge of the selected feature's page
constexpr int kPaneY = 70;

RECT HivePillRect(const RECT& row) { return RECT{row.right - 8 - 50, row.top + (row.bottom - row.top - 22) / 2,
                                                   row.right - 8, row.top + (row.bottom - row.top - 22) / 2 + 22}; }
RECT HiveKeyRect() { return RECT{kPaneX + 240, 112, kPaneX + 240 + 140, 144}; }
RECT HeaderSwitchRect() { return RECT{kPaneX + 448, 117, kPaneX + 448 + 52, 117 + 26}; }

bool HiveSectionEnabled(int section)
{
    return section == 0 ? partyAutoAcceptEnabled : (section == 1 ? autoRqEnabled : hiveStatsEnabled);
}

void HiveToggleSection(int section)
{
    bool& flag = section == 0 ? partyAutoAcceptEnabled : (section == 1 ? autoRqEnabled : hiveStatsEnabled);
    flag = !flag;
    if (flag) hiveEnabled = true;   // switching something on also resumes the group
    SaveConfig();
    {
        const wchar_t* names[3] = {L"Party Auto Accept", L"Auto RQ", L"Hive Statistic"};
        PushToast(names[std::clamp(section, 0, 2)], flag);
    }
}


// ON / OFF pill (same look as the feature list); (x, y) is its top-left corner.
void DrawGlassSwitch(HDC hdc, const RECT& r, bool on, const GlassColors& c, bool onAccentRow = true);   // with the glass pages

void DrawSwitchAt(HDC hdc, int x, int y, int w, int h, bool on)
{
    if (glassContent && menuDrawing)
    {
        DrawGlassSwitch(hdc, RECT{x, y + 1, x + w, y + h - 1}, on, GlassTheme());
        return;
    }
    const ThemeColors t = CurrentTheme();
    DrawRoundBox(hdc, x, y, w, h, 8, on ? t.accent : t.off);
    DrawTextCentered(hdc, x, y, w, h, on ? L"ON" : L"OFF", RGB(255,255,255), 12);
}

// A settings row: label on the left, switch on the right.
// ---- Replay list (inside the menu, so the folder never has to be opened) ----
struct ReplayFile { std::wstring name, path; unsigned long long size; unsigned seconds; };
std::vector<ReplayFile> replayFiles;
ULONGLONG replayFilesAt = 0;

std::wstring ReplayDir()
{
    MinecraftDataProvider::ReplayStatus st{};
    if (MinecraftDataProvider::GetReplayStatus(st) && st.path[0])
    {
        std::wstring dir = Utf8ToWide(st.path);
        const size_t cut = dir.find_last_of(L"\\/");
        if (cut != std::wstring::npos)
        {
            dir.resize(cut);
            return dir;
        }
    }

    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return std::wstring(base) + L"\\MyClient\\Replays";

    return L"";
}

void RefreshReplayFiles(bool force)
{
    const ULONGLONG now = GetTickCount64();
    if (!force && replayFilesAt && now - replayFilesAt < 2000)
        return;

    replayFilesAt = now;
    replayFiles.clear();

    const std::wstring dir = ReplayDir();
    if (dir.empty())
        return;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.mcrec").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        ReplayFile f;
        f.name = fd.cFileName;
        f.path = dir + L"\\" + f.name;
        f.size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        f.seconds = 0;

        // The recorder writes the length into the header when it closes the file.
        FILE* file = _wfopen(f.path.c_str(), L"rb");
        if (file)
        {
            char header[40];
            if (std::fread(header, 1, sizeof header, file) == sizeof header &&
                std::memcmp(header, "MCREC001", 8) == 0)
                std::memcpy(&f.seconds, header + 12, 4);
            std::fclose(file);
        }

        replayFiles.push_back(f);
    }
    while (FindNextFileW(h, &fd));

    FindClose(h);

    // Newest first (the names start with the date).
    std::sort(replayFiles.begin(), replayFiles.end(),
              [](const ReplayFile& a, const ReplayFile& b) { return a.name > b.name; });
}

std::wstring replayNotice;
ULONGLONG replayNoticeAt = 0;

void SetReplayNotice(const std::wstring& text)
{
    replayNotice = text;
    replayNoticeAt = GetTickCount64();
}

// A file name from a title: characters Windows does not allow become '_', trailing dots/spaces go.
std::wstring SafeFileName(std::wstring name)
{
    for (auto& c : name)
        if (c < 32 || wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
    return name;
}

// Copies a recording to the PC's Downloads folder (Downloads\MyClient-Replays), named `title`.mcrec
// (the original file name when `title` is empty). Returns the new path or "".
std::wstring DownloadReplay(const ReplayFile& f, const std::wstring& title = L"")
{
    wchar_t profile[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return L"";

    std::wstring dir = std::wstring(profile) + L"\\Downloads";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\MyClient-Replays";
    CreateDirectoryW(dir.c_str(), nullptr);

    const std::wstring safe = SafeFileName(title);
    const std::wstring stem = safe.empty() ? f.name.substr(0, f.name.size() - 6) : safe;
    std::wstring target = dir + L"\\" + stem + L".mcrec";
    for (int k = 2; k < 100 && GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES; k++)
        target = dir + L"\\" + stem + L" (" + std::to_wstring(k) + L").mcrec";

    return CopyFileW(f.path.c_str(), target.c_str(), TRUE) ? target : L"";
}

void DrawReplayIconButton(HDC hdc, const RECT& r, int kind, COLORREF fill, COLORREF ink)
{
    // kind: 0 trash, 1 download, 2 play, 3 star (filled), 4 star (outline), 5 folder, 6 pencil, 7 magnifier
    DrawRoundBox(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, 7, fill);

    const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 2, ink);
    HBRUSH brush = CreateSolidBrush(ink);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    // Centre of the button as a float (an even-sized button has its middle between two pixels).
    const float fx = (r.left + r.right) / 2.0f, fy = (r.top + r.bottom) / 2.0f;
    if (kind == 2)
    {
        // Equal top / bottom slopes; shifted right a little so it looks centred (optical centre of a triangle).
        const float h = std::min(r.right - r.left, r.bottom - r.top) * 0.42f;
        FillTriangleSmooth(hdc, fx - h * 0.40f, fy - h / 2, fx - h * 0.40f, fy + h / 2, fx + h * 0.60f, fy, ink);
    }
    else if (kind == 3 || kind == 4)
    {
        // Five-pointed star (inner radius 0.382 of the outer = the regular star), slightly below centre
        // so it looks centred. Filled when on, a smooth outline when off.
        float xs[10], ys[10];
        const float outer = 7.5f, inner = outer * 0.382f;
        for (int k = 0; k < 10; k++)
        {
            const float a = -3.14159265f / 2 + k * 3.14159265f / 5;
            const float rad = k % 2 ? inner : outer;
            xs[k] = fx + rad * std::cos(a);
            ys[k] = fy + 0.8f + rad * std::sin(a);
        }
        // The filled star gets the same outline too, so both have exactly the same outer size.
        if (kind == 3)
            FillPolygonSmooth(hdc, xs, ys, 10, ink);
        for (int k = 0; k < 10; k++)
            DrawLineSmooth(hdc, xs[k], ys[k], xs[(k + 1) % 10], ys[(k + 1) % 10], 1.4f, ink);
    }
    else if (kind == 5)
    {
        POINT folder[6] = {{cx - 8, cy - 6}, {cx - 3, cy - 6}, {cx - 1, cy - 4}, {cx + 8, cy - 4}, {cx + 8, cy + 6}, {cx - 8, cy + 6}};
        SelectObject(hdc, brush);
        Polygon(hdc, folder, 6);
    }
    else if (kind == 6)
    {
        // Pencil, 45 degrees, tip at the bottom left: thick body, a gap before the eraser, a pointed tip.
        const float ux = 0.7071f, uy = -0.7071f;              // along the pencil (towards the eraser)
        const float tx = fx - 6.5f, ty = fy + 6.5f;            // tip
        const auto at = [&](float d, float& x, float& y) { x = tx + ux * d; y = ty + uy * d; };
        float ax, ay, bx, by;
        at(4.5f, ax, ay); at(13.0f, bx, by);
        DrawLineSmooth(hdc, ax, ay, bx, by, 4.2f, ink);       // body
        at(14.6f, ax, ay); at(16.6f, bx, by);
        DrawLineSmooth(hdc, ax, ay, bx, by, 4.2f, ink);       // eraser
        const float nx = 0.7071f, ny = 0.7071f;                // across the pencil
        float wx, wy;
        at(3.6f, wx, wy);
        FillTriangleSmooth(hdc, tx, ty, wx + nx * 2.1f, wy + ny * 2.1f, wx - nx * 2.1f, wy - ny * 2.1f, ink);   // tip
    }
    else if (kind == 7)
    {
        Ellipse(hdc, cx - 7, cy - 7, cx + 3, cy + 3);
        MoveToEx(hdc, cx + 2, cy + 2, nullptr); LineTo(hdc, cx + 7, cy + 7);
    }
    else if (kind == 1)
    {
        // Down arrow into a tray, mirror-symmetric about the centre line.
        DrawLineSmooth(hdc, fx, fy - 7, fx, fy + 3, 2.0f, ink);
        DrawLineSmooth(hdc, fx - 5, fy - 2, fx, fy + 3, 2.0f, ink);
        DrawLineSmooth(hdc, fx + 5, fy - 2, fx, fy + 3, 2.0f, ink);
        DrawLineSmooth(hdc, fx - 7, fy + 7, fx + 7, fy + 7, 2.0f, ink);
    }
    else
    {
        MoveToEx(hdc, cx - 7, cy - 4, nullptr); LineTo(hdc, cx + 8, cy - 4);      // lid
        MoveToEx(hdc, cx - 2, cy - 7, nullptr); LineTo(hdc, cx + 3, cy - 7);      // handle
        MoveToEx(hdc, cx - 5, cy - 2, nullptr); LineTo(hdc, cx - 4, cy + 7);      // body
        LineTo(hdc, cx + 5, cy + 7); LineTo(hdc, cx + 6, cy - 2);
    }

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// ---- Replays panel (opened from the home screen's top-left button or the Replay Mod page) ----
// Flarial-style list: Favorites / Today / Yesterday / This week / Older, each recording with
// play, favourite, delete, show in folder, download and rename. Titles and favourites: replays.txt.
void OpenMenu();
void CloseMenu(bool backToGame = true);
void ToggleFeature(DetailFeature f);
bool replaysOpen = false;
bool replaysFromHome = false;            // Close goes back to the game instead of the Replay Mod page
ReplayLibrary::Sort replaySort = ReplayLibrary::Sort::Date;
bool replayCollapsed[ReplayLibrary::kGroupCount] = {};
int replayScroll = 0;                    // first visible list line
bool replaySearching = false;
std::wstring replaySearchText;
std::string replayRenaming;              // file being renamed ("" = none)
std::wstring replayRenameText;
std::string replayDeleteFile;            // file waiting for the second click on the trash
ULONGLONG replayDeleteFileAt = 0;
ReplayLibrary::MetaMap replayMeta;
bool replayMetaLoaded = false;
std::vector<ReplayLibrary::Entry> replayEntries;
std::vector<ReplayLibrary::Row> replayRows;

const RECT kReplaysHeader{28, 26, 912, 86};
const RECT kReplaysSearchButton{862, 38, 898, 74};
const RECT kReplaysSearchBox{560, 38, 852, 74};
const RECT kReplaysClose{28, 540, 148, 578};
const RECT kReplaysSort{560, 540, 730, 578};
const RECT kReplaysAuto{742, 540, 912, 578};
// While a replay plays back: its controls in the header.
const RECT kReplaysPause{220, 42, 300, 72};
const RECT kReplaysSpeed{308, 42, 368, 72};
const RECT kReplaysStop{376, 42, 436, 72};
constexpr int kReplaysListTop = 100, kReplaysListBottom = 528;
constexpr int kReplaysHeaderH = 34, kReplaysRowH = 64, kReplaysGap = 8;

std::wstring ReplayMetaPath() { const std::wstring d = ReplayDir(); return d.empty() ? d : d + L"\\replays.txt"; }

void LoadReplayMeta()
{
    replayMetaLoaded = true;
    std::ifstream in(std::filesystem::path(ReplayMetaPath()), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    replayMeta = ReplayLibrary::ParseMeta(text);
}

void SaveReplayMeta()
{
    const std::wstring path = ReplayMetaPath();
    if (path.empty()) return;
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    out << ReplayLibrary::SerializeMeta(replayMeta);
}

long ReplayToday()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return ReplayLibrary::DayNumber(st.wYear, st.wMonth, st.wDay);
}

// Rebuilds the entries and the visible lines (called before drawing and before handling a click).
void BuildReplayRows()
{
    if (!replayMetaLoaded) LoadReplayMeta();
    RefreshReplayFiles(false);
    replayEntries.clear();
    for (const ReplayFile& f : replayFiles)
    {
        ReplayLibrary::Entry e;
        e.file = WideToUtf8(f.name);
        e.size = f.size;
        e.seconds = f.seconds;
        ReplayLibrary::ParseName(e);
        if (const auto it = replayMeta.find(e.file); it != replayMeta.end()) e.meta = it->second;
        replayEntries.push_back(e);
    }
    replayRows = ReplayLibrary::Layout(replayEntries, replaySort, WideToUtf8(replaySearchText), ReplayToday(), replayCollapsed);
    replayScroll = std::clamp(replayScroll, 0, std::max(0, (int)replayRows.size() - 1));
}

int ReplayLineHeight(const ReplayLibrary::Row& row) { return row.header ? kReplaysHeaderH : kReplaysRowH + kReplaysGap; }

// Top of each visible line (-1 = not shown), in list order.
std::vector<int> ReplayLineTops()
{
    std::vector<int> tops(replayRows.size(), -1);
    int y = kReplaysListTop;
    for (int i = replayScroll; i < (int)replayRows.size(); i++)
    {
        const int h = ReplayLineHeight(replayRows[i]);
        if (y + (replayRows[i].header ? h : kReplaysRowH) > kReplaysListBottom) break;
        tops[i] = y;
        y += h;
    }
    return tops;
}

// Row buttons, left to right: favourite, delete, show in folder, download, rename.
constexpr int kReplayIconCount = 5;
RECT ReplayIconRect(int top, int k) { const int left = 896 - kReplayIconCount * 30 - (kReplayIconCount - 1) * 6 + k * 36; return RECT{left, top + 17, left + 30, top + 47}; }
RECT ReplayPlayButtonRect(int top) { return RECT{44, top + 14, 80, top + 50}; }

ReplayFile ReplayFileOf(const ReplayLibrary::Entry& e)
{
    ReplayFile f{};
    f.name = Utf8ToWide(e.file);
    f.path = ReplayDir() + L"\\" + f.name;
    f.size = e.size;
    f.seconds = e.seconds;
    return f;
}

void OpenReplays(bool fromHome)
{
    if (!menuOpen) OpenMenu();
    replaysOpen = true;
    replaysFromHome = fromHome;
    replayScroll = 0;
    replaySearching = false;
    replayRenaming.clear();
    replayMetaLoaded = false;
    RefreshReplayFiles(true);
}

void CloseReplays()
{
    replaysOpen = false;
    replaySearching = false;
    replayRenaming.clear();
    if (replaysFromHome) CloseMenu();
}

void DrawReplayControls(HDC hdc);   // defined below
RECT HomeReplaysButtonRect();

void DrawReplaysPanel(HDC hdc)
{
    const ThemeColors t = CurrentTheme();
    BuildReplayRows();

    // Header: logo, title, search
    DrawRoundBox(hdc, kReplaysHeader.left, kReplaysHeader.top, kReplaysHeader.right - kReplaysHeader.left,
                 kReplaysHeader.bottom - kReplaysHeader.top, 14, t.card);
    // The client's logo mark, as at the top left of the menu.
    DrawLogo(hdc, 42, 38, 36);
    DrawTextSimple(hdc, 90, 40, L"Replays", RGB(255,255,255), 26);
    if (replaySearching || !replaySearchText.empty())
    {
        const RECT b = kReplaysSearchBox;
        DrawRoundBox(hdc, b.left, b.top, b.right - b.left, b.bottom - b.top, 9, t.button);
        const std::wstring shown = replaySearchText.empty() && !replaySearching ? L"Search..." : replaySearchText;
        DrawTextSimple(hdc, b.left + 12, b.top + 9, shown + (replaySearching && (GetTickCount64() / 500) % 2 ? L"|" : L""),
                       replaySearchText.empty() ? t.muted : t.text, 14);
    }
    DrawReplayIconButton(hdc, kReplaysSearchButton, 7, t.accent, t.onAccent);
    DrawReplayControls(hdc);

    // List
    const std::vector<int> tops = ReplayLineTops();
    if (replayRows.empty())
        DrawTextSimple(hdc, 44, kReplaysListTop + 10,
                       replaySearchText.empty() ? L"No recordings yet. Turn on Auto recording below, then play."
                                                : L"No recording matches the search.", t.muted, 14);
    if (!replayDeleteFile.empty() && GetTickCount64() - replayDeleteFileAt > 4000)
        replayDeleteFile.clear();
    for (int i = 0; i < (int)replayRows.size(); i++)
    {
        const int top = tops[i];
        if (top < 0) continue;
        const ReplayLibrary::Row& row = replayRows[i];
        if (row.header)
        {
            const bool closed = replayCollapsed[(int)row.group];
            // Same-size triangles centred on (38, top + 17): pointing right when folded, down when open.
            const float tx = 38.0f, ty = top + 17.0f;
            if (closed)
                FillTriangleSmooth(hdc, tx - 3.5f, ty - 6, tx - 3.5f, ty + 6, tx + 5.5f, ty, t.text);
            else
                FillTriangleSmooth(hdc, tx - 6, ty - 3.5f, tx + 6, ty - 3.5f, tx, ty + 5.5f, t.text);
            DrawTextSimple(hdc, 54, top + 7, Utf8ToWide(ReplayLibrary::GroupName(row.group)), t.text, 16);
            continue;
        }
        const ReplayLibrary::Entry& e = replayEntries[row.entry];
        DrawRoundBox(hdc, 28, top, 884, kReplaysRowH, 12, t.card);
        DrawReplayIconButton(hdc, ReplayPlayButtonRect(top), 2, t.accent, t.onAccent);
        if (replayRenaming == e.file)
        {
            DrawRoundBox(hdc, 92, top + 8, 460, 28, 7, t.button);
            DrawTextSimple(hdc, 100, top + 13, replayRenameText + ((GetTickCount64() / 500) % 2 ? L"|" : L""), t.text, 15);
        }
        else
            DrawTextSimple(hdc, 96, top + 12, Utf8ToWide(ReplayLibrary::Title(e)), t.text, 16);
        DrawTextSimple(hdc, 96, top + 39, L"(" + Utf8ToWide(ReplayLibrary::Details(e)) + L")", t.muted, 12);

        const bool armed = replayDeleteFile == e.file;
        DrawReplayIconButton(hdc, ReplayIconRect(top, 0), e.meta.favorite ? 3 : 4, t.button,
                             e.meta.favorite ? RGB(245,200,70) : t.text);
        DrawReplayIconButton(hdc, ReplayIconRect(top, 1), 0, armed ? RGB(190,60,60) : t.button, t.text);
        DrawReplayIconButton(hdc, ReplayIconRect(top, 2), 5, t.button, t.text);
        DrawReplayIconButton(hdc, ReplayIconRect(top, 3), 1, t.button, t.text);
        DrawReplayIconButton(hdc, ReplayIconRect(top, 4), 6, t.button, t.text);
    }
    if (replayScroll > 0 || (!tops.empty() && tops.back() < 0))
        DrawTextSimple(hdc, 760, kReplaysListBottom - 2, L"Scroll: mouse wheel", t.muted, 11);

    // Footer
    DrawButton(hdc, kReplaysClose.left, kReplaysClose.top, kReplaysClose.right - kReplaysClose.left,
               kReplaysClose.bottom - kReplaysClose.top, L"Close", false);
    DrawButton(hdc, kReplaysSort.left, kReplaysSort.top, kReplaysSort.right - kReplaysSort.left,
               kReplaysSort.bottom - kReplaysSort.top, L"Sort: " + Utf8ToWide(ReplayLibrary::SortName(replaySort)), false);
    DrawButton(hdc, kReplaysAuto.left, kReplaysAuto.top, kReplaysAuto.right - kReplaysAuto.left,
               kReplaysAuto.bottom - kReplaysAuto.top, replayEnabled ? L"Auto recording: on" : L"Auto recording: off", replayEnabled);
    std::wstring notice;
    if (!replayNotice.empty() && GetTickCount64() - replayNoticeAt < 8000) notice = replayNotice;
    else if (!replayDeleteFile.empty()) notice = L"Click the trash again to delete.";
    else if (!replayRenaming.empty()) notice = L"Type a name, Enter to save, Esc to cancel.";
    if (!notice.empty())
        DrawTextSimple(hdc, 160, 552, notice, notice[0] == L'C' && !replayDeleteFile.empty() ? RGB(230,150,150) : RGB(170,200,255), 12);
}

std::wstring ReplayClock(uint64_t ms)
{
    const uint64_t sec = ms / 1000;
    wchar_t text[32];
    swprintf(text, 32, L"%llu:%02llu", (unsigned long long)(sec / 60), (unsigned long long)(sec % 60));
    return text;
}

// Plays a recording in the game: a local server on 127.0.0.1 that the game joins (ReplayPlayer.h).
void StartReplayPlayback(const ReplayLibrary::Entry& e, const ReplayFile& f)
{
    if (MinecraftDataProvider::IsInWorld())
    {
        SetReplayNotice(L"Leave the world first (home screen), then press play.");
        return;
    }
    std::wstring error;
    if (!ReplayPlayer::Start(f.path, error))
    {
        SetReplayNotice(L"Cannot play: " + error);
        return;
    }
    SetReplayNotice(L"Joining the replay: " + Utf8ToWide(ReplayLibrary::Title(e)));
    CloseReplays();
    if (menuOpen) CloseMenu();
}

const float kReplaySpeeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f};

std::wstring ReplaySpeedText(float speed)
{
    wchar_t text[16];
    swprintf(text, 16, speed < 1.f ? L"x%.2g" : L"x%.0f", speed);
    return text;
}

void DrawReplayControls(HDC hdc)
{
    if (!ReplayPlayer::Active()) return;
    const ReplayPlayer::Status st = ReplayPlayer::Get();
    DrawButton(hdc, kReplaysPause.left, kReplaysPause.top, kReplaysPause.right - kReplaysPause.left,
               kReplaysPause.bottom - kReplaysPause.top, st.paused ? L"Resume" : L"Pause", st.paused);
    DrawButton(hdc, kReplaysSpeed.left, kReplaysSpeed.top, kReplaysSpeed.right - kReplaysSpeed.left,
               kReplaysSpeed.bottom - kReplaysSpeed.top, ReplaySpeedText(st.speed), false);
    DrawButton(hdc, kReplaysStop.left, kReplaysStop.top, kReplaysStop.right - kReplaysStop.left,
               kReplaysStop.bottom - kReplaysStop.top, L"Stop", false);
    std::wstring state = st.phase == ReplayPlayer::Phase::Playing ? ReplayClock(st.positionMs) + L" / " + ReplayClock(st.lengthMs)
                       : st.phase == ReplayPlayer::Phase::Finished ? L"Finished"
                       : L"Joining...";
    DrawTextSimple(hdc, 446, 50, state, RGB(220,220,220), 13);
}

// The controls' clicks; true when one was hit.
bool HandleReplayControlsClick(int mx, int my)
{
    if (!ReplayPlayer::Active()) return false;
    const POINT p{mx, my};
    const ReplayPlayer::Status st = ReplayPlayer::Get();
    if (PtInRect(&kReplaysPause, p)) { ReplayPlayer::SetPaused(!st.paused); return true; }
    if (PtInRect(&kReplaysSpeed, p))
    {
        size_t next = 0;
        for (size_t i = 0; i < std::size(kReplaySpeeds); ++i)
            if (kReplaySpeeds[i] > st.speed + 0.01f) { next = i; break; }
        ReplayPlayer::SetSpeed(kReplaySpeeds[next]);
        return true;
    }
    if (PtInRect(&kReplaysStop, p)) { ReplayPlayer::Stop(); SetReplayNotice(L"Playback stopped."); return true; }
    return false;
}

// While playing, top left of the game: position, speed, and where the controls are.
void DrawReplayPlaybackBar(HDC hdc)
{
    if (menuOpen || !ReplayPlayer::Active()) return;
    const ReplayPlayer::Status st = ReplayPlayer::Get();
    std::wstring text = L"REPLAY  ";
    if (st.phase == ReplayPlayer::Phase::Playing)
        text += ReplayClock(st.positionMs) + L" / " + ReplayClock(st.lengthMs) + L"  " + ReplaySpeedText(st.speed) + (st.paused ? L"  (paused)" : L"");
    else if (st.phase == ReplayPlayer::Phase::Finished)
        text += L"finished - leave the world to stop";
    else
        text += L"joining...";
    if (st.packsRequested) text += L"  (some packs missing)";
    const RECT r = HomeReplaysButtonRect();
    DrawRoundBox(hdc, r.left, r.top, 430, 44, 7, RGB(28,24,28));
    DrawTextSimple(hdc, r.left + 10, r.top + 5, text, RGB(255,120,120), 13);
    DrawTextSimple(hdc, r.left + 10, r.top + 24, L"Menu > Replay Mod > Open Replays: pause, speed, stop", RGB(170,170,170), 11);
}

void FinishReplayRename(bool save)
{
    if (save && !replayRenaming.empty())
    {
        auto& m = replayMeta[replayRenaming];
        m.title = ReplayLibrary::CleanTitle(WideToUtf8(replayRenameText));
        SaveReplayMeta();
    }
    replayRenaming.clear();
}

// Click inside the Replays panel (menu coordinates). Always handled while the panel is open.
void HandleReplaysClick(int mx, int my)
{
    const auto in = [&](const RECT& r) { return mx >= r.left && mx < r.right && my >= r.top && my < r.bottom; };
    if (!replayRenaming.empty()) FinishReplayRename(true);   // a click elsewhere keeps the typed name
    BuildReplayRows();

    if (in(kReplaysClose)) { CloseReplays(); return; }
    if (HandleReplayControlsClick(mx, my)) return;
    if (in(kReplaysSort)) { replaySort = (ReplayLibrary::Sort)(((int)replaySort + 1) % ReplayLibrary::kSortCount); replayScroll = 0; return; }
    if (in(kReplaysAuto)) { ToggleFeature(DetailFeature::Replay); SaveConfig(); return; }
    if (in(kReplaysSearchButton)) { replaySearching = !replaySearching || !replaySearchText.empty() ? !replaySearching : false; if (!replaySearching) replaySearchText.clear(); replayScroll = 0; return; }
    if (in(kReplaysSearchBox) && (replaySearching || !replaySearchText.empty())) { replaySearching = true; return; }
    replaySearching = false;

    const std::vector<int> tops = ReplayLineTops();
    for (int i = 0; i < (int)replayRows.size(); i++)
    {
        const int top = tops[i];
        if (top < 0) continue;
        const ReplayLibrary::Row& row = replayRows[i];
        if (row.header)
        {
            if (my >= top && my < top + kReplaysHeaderH && mx >= 28 && mx < 912)
                replayCollapsed[(int)row.group] = !replayCollapsed[(int)row.group];
            if (my >= top && my < top + kReplaysHeaderH) return;
            continue;
        }
        if (my < top || my >= top + kReplaysRowH) continue;
        const ReplayLibrary::Entry e = replayEntries[row.entry];
        const ReplayFile f = ReplayFileOf(e);
        if (in(ReplayPlayButtonRect(top)))
            StartReplayPlayback(e, f);
        else if (in(ReplayIconRect(top, 0)))
        {
            replayMeta[e.file].favorite = !e.meta.favorite;
            SaveReplayMeta();
        }
        else if (in(ReplayIconRect(top, 1)))
        {
            if (replayDeleteFile == e.file && GetTickCount64() - replayDeleteFileAt <= 4000)
            {
                replayDeleteFile.clear();
                if (DeleteFileW(f.path.c_str()))
                {
                    replayMeta.erase(e.file);
                    SaveReplayMeta();
                    SetReplayNotice(L"Deleted: " + Utf8ToWide(ReplayLibrary::Title(e)));
                }
                else
                    SetReplayNotice(L"Could not delete the file (is it still being written?).");
                RefreshReplayFiles(true);
            }
            else
            {
                replayDeleteFile = e.file;
                replayDeleteFileAt = GetTickCount64();
            }
        }
        else if (in(ReplayIconRect(top, 2)))
        {
            const std::wstring args = L"/select,\"" + f.path + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        else if (in(ReplayIconRect(top, 3)))
        {
            // A renamed recording is saved under its new name; otherwise under the original file name.
            const std::wstring saved = DownloadReplay(f, e.meta.title.empty() ? L"" : Utf8ToWide(e.meta.title));
            SetReplayNotice(saved.empty() ? L"Could not copy the file to Downloads." : L"Saved to: " + saved);
        }
        else if (in(ReplayIconRect(top, 4)))
        {
            replayRenaming = e.file;
            replayRenameText = Utf8ToWide(ReplayLibrary::Title(e));
        }
        return;
    }
}

// Home screen (the game's title screen, no world loaded): a small "Replays" button at the top left.
// Not on the Play / Settings / Marketplace screens opened from it. Older DLLs do not report the title screen:
// then any screen outside a world counts. No world ticks there, so the frame channel goes stale; the DLL's
// per-frame heartbeat says the game runs.
bool OnHomeScreen()
{
    if (!MinecraftDataProvider::IsGameRunning() || MinecraftDataProvider::IsInWorld()) return false;
    return MinecraftDataProvider::IsOnTitleScreen() != 0;
}

// The button in overlay coordinates (the overlay covers the whole game window, title bar included).
RECT HomeReplaysButtonRect()
{
    POINT client{0, 0};
    RECT window{};
    if (minecraftWindow && GetWindowRect(minecraftWindow, &window) && ClientToScreen(minecraftWindow, &client))
    {
        client.x -= window.left;
        client.y -= window.top;
    }
    else
        client = {0, 0};
    return RECT{client.x + 10, client.y + 10, client.x + 10 + 96, client.y + 10 + 28};
}

void DrawHomeReplaysButton(HDC hdc)
{
    if (menuOpen || !OnHomeScreen())
        return;
    const RECT r = HomeReplaysButtonRect();
    DrawRoundBox(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, 7, RGB(28,24,28));
    DrawReplayIconButton(hdc, RECT{r.left + 4, r.top + 4, r.left + 24, r.top + 24}, 5, RGB(60,48,56), RGB(200,190,200));
    DrawTextSimple(hdc, r.left + 30, r.top + 6, L"Replays", RGB(235,235,235), 13);
}

RECT MenuSoundRect(int index)
{
    return RECT{kPaneX, 200 + index * 31, kPaneX + 400, 200 + index * 31 + 27};
}
// Resource / Arrow Counter pages: the "Black background" switch, below the page's text lines.
RECT CounterBackgroundRect() { return RECT{kPaneX, 236, kPaneX + 400, 236 + 27}; }
RECT CounterTranslucentRect() { return RECT{kPaneX, 267, kPaneX + 400, 267 + 27}; }

// Sounds in list order (values of toggleSoundMode).
constexpr int kMenuSoundCount = 3;
constexpr int kMenuSoundOrder[kMenuSoundCount] = {0, 1, 2};

const wchar_t* MenuSoundName(int mode)
{
    switch (mode)
    {
    case 1:  return L"Custom clip";
    case 2:  return L"None (silent)";
    default: return L"OriginX (default)";
    }
}

// ------------------------- Optimization page ------------------
// One scrolling list of rows: the presets, section headers, switches and sliders (each slider sits right
// under the switch it belongs to). The wheel scrolls it a row at a time; only whole rows are drawn.
enum class LiteRowKind { Header, Presets, Switch, Slider, Status };
enum LiteKey
{
    LiteClouds, LiteSun, LiteFog, LiteWeather, LiteShadows,
    LiteParticles, LiteCrit, LitePotion,
    LiteEntities, LiteEntityDistance, LiteMobs, LiteItems, LiteMisc, LitePlayers, LitePlayerDistance, LiteNameTags,
    LiteRender, LiteRenderDistance,
    LitePriority, LiteThrottle, LiteGpu,
    LiteNone
};
struct LiteRow { LiteRowKind kind; int key; const wchar_t* label; };

const std::vector<LiteRow>& LiteRows()
{
    static const std::vector<LiteRow> rows = {
        {LiteRowKind::Header, LiteNone, L"Presets"},
        {LiteRowKind::Presets, LiteNone, L""},
        {LiteRowKind::Header, LiteNone, L"World"},
        {LiteRowKind::Switch, LiteClouds, L"No clouds"},
        {LiteRowKind::Switch, LiteSun, L"No sun & moon"},
        {LiteRowKind::Switch, LiteFog, L"No fog"},
        {LiteRowKind::Switch, LiteWeather, L"No rain & snow"},
        {LiteRowKind::Switch, LiteShadows, L"No shadows"},
        {LiteRowKind::Header, LiteNone, L"Particles"},
        {LiteRowKind::Switch, LiteParticles, L"No particles"},
        {LiteRowKind::Switch, LiteCrit, L"No Critical Hit Effect"},
        {LiteRowKind::Switch, LitePotion, L"No Potion Effect"},
        {LiteRowKind::Header, LiteNone, L"Entities"},
        {LiteRowKind::Switch, LiteEntities, L"No far entities"},
        {LiteRowKind::Slider, LiteEntityDistance, L"Entity distance"},
        {LiteRowKind::Switch, LiteMobs, L"Cull mobs"},
        {LiteRowKind::Switch, LiteItems, L"Cull dropped items"},
        {LiteRowKind::Switch, LiteMisc, L"Cull projectiles & misc"},
        {LiteRowKind::Switch, LitePlayers, L"Cull distant players"},
        {LiteRowKind::Slider, LitePlayerDistance, L"Player distance"},
        {LiteRowKind::Switch, LiteNameTags, L"No name tags"},
        {LiteRowKind::Header, LiteNone, L"Render distance"},
        {LiteRowKind::Switch, LiteRender, L"Render Distance"},
        {LiteRowKind::Slider, LiteRenderDistance, L"Chunks"},
        {LiteRowKind::Header, LiteNone, L"System"},
        {LiteRowKind::Switch, LitePriority, L"High process priority"},
        {LiteRowKind::Switch, LiteThrottle, L"Disable CPU power throttling"},
        {LiteRowKind::Switch, LiteGpu, L"High GPU scheduling priority"},
        {LiteRowKind::Status, LiteNone, L""},
    };
    return rows;
}

bool* LiteSwitchPtr(int key)
{
    switch (key)
    {
    case LiteClouds: return &liteClouds;
    case LiteSun: return &liteSky;
    case LiteFog: return &liteFog;
    case LiteWeather: return &liteWeather;
    case LiteShadows: return &liteNoShadows;
    case LiteParticles: return &liteNoParticles;
    case LiteCrit: return &liteNoCrit;
    case LitePotion: return &liteNoPotion;
    case LiteEntities: return &liteEntityLimit;
    case LiteMobs: return &liteCullMobs;
    case LiteItems: return &liteCullItems;
    case LiteMisc: return &liteCullMisc;
    case LitePlayers: return &litePlayerLimit;
    case LiteNameTags: return &liteNoNameTags;
    case LiteRender: return &liteRenderEnabled;
    case LitePriority: return &liteHighPriority;
    case LiteThrottle: return &liteNoThrottle;
    case LiteGpu: return &liteGpuPriority;
    default: return nullptr;
    }
}

// Switches that change the generated client pack (the rest work through the DLL or Windows at once).
bool LiteKeyUsesPack(int key)
{
    return key == LiteClouds || key == LiteSun || key == LiteFog || key == LiteWeather || key == LiteShadows ||
           key == LiteParticles || key == LiteCrit || key == LitePotion;
}

// A row that only matters while another switch is on: drawn dimmed while that one is off.
bool LiteRowActive(int key)
{
    switch (key)
    {
    case LiteEntityDistance: case LiteMobs: case LiteItems: case LiteMisc: return liteEntityLimit;
    case LitePlayerDistance: return litePlayerLimit;
    case LiteRenderDistance: return liteRenderEnabled;
    default: return true;
    }
}

// Sliders: the value, its range and its unit.
struct LiteSliderInfo { int* value; int min, max, step; const wchar_t* unit; };
LiteSliderInfo LiteSlider(int key)
{
    switch (key)
    {
    case LiteEntityDistance: return {&liteEntityDistance, 8, 128, 8, L" blocks"};
    case LitePlayerDistance: return {&litePlayerDistance, 8, 128, 8, L" blocks"};
    default:                 return {&liteRenderDistance, 4, 48, 2, L" chunks"};
    }
}

int LiteRowHeight(LiteRowKind kind)
{
    switch (kind)
    {
    case LiteRowKind::Header: return 26;
    case LiteRowKind::Presets: return 40;
    case LiteRowKind::Slider: return 46;
    case LiteRowKind::Status: return 46;
    default: return 31;
    }
}

constexpr int kLiteTop = 196, kLiteBottom = 584;   // the list's area on the page

// The rows shown at the current scroll position, with their boxes (row index, rect).
struct LiteShown { int index; RECT rect; };
std::vector<LiteShown> LiteLayout()
{
    std::vector<LiteShown> shown;
    const std::vector<LiteRow>& rows = LiteRows();
    int y = kLiteTop;
    for (int i = std::max(0, liteScrollRow); i < (int)rows.size(); i++)
    {
        const int h = LiteRowHeight(rows[i].kind);
        if (y + h - 4 > kLiteBottom) break;
        shown.push_back({i, RECT{kPaneX, y, kPaneX + 400, y + h - 4}});
        y += h;
    }
    return shown;
}

// The last first-row that still shows the end of the list.
int LiteMaxScroll()
{
    const std::vector<LiteRow>& rows = LiteRows();
    int used = 0, first = (int)rows.size();
    while (first > 0 && used + LiteRowHeight(rows[first - 1].kind) - 4 <= kLiteBottom - kLiteTop)
        used += LiteRowHeight(rows[--first].kind);
    return first;
}

void ClampLiteScroll() { liteScrollRow = std::clamp(liteScrollRow, 0, LiteMaxScroll()); }

// The slider's track inside its row box.
RECT LiteTrackRect(const RECT& row) { return RECT{row.left + 28, row.top + 24, row.right - 14, row.top + 40}; }

int LiteSliderAt(int key, const RECT& track, int mx)
{
    const LiteSliderInfo s = LiteSlider(key);
    const int intervals = (s.max - s.min) / s.step;
    const int width = std::max(1, (int)(track.right - track.left));
    const int half = (std::clamp(mx, (int)track.left, (int)track.right) - track.left) * intervals * 2 / width;
    return s.min + std::clamp((half + 1) / 2, 0, intervals) * s.step;   // nearest step
}

// Preset buttons: Off / Low / Balanced / Max, side by side in the presets row.
constexpr int kLitePresetCount = 4;
const wchar_t* const kLitePresetNames[kLitePresetCount] = {L"Off", L"Low", L"Balanced", L"Max"};
RECT LitePresetRect(const RECT& row, int i)
{
    const int gap = 8, w = (row.right - row.left - gap * (kLitePresetCount - 1)) / kLitePresetCount;
    const int left = row.left + i * (w + gap);
    return RECT{left, row.top, left + w, row.bottom};
}

// Before the first change in a session: keep a copy of the game's options.txt next to it.
void BackupGameOptions()
{
    static bool done = false;
    if (done) return;
    if (!texState.found) RefreshTextures();
    if (!texState.found) return;
    std::error_code ec;
    const auto options = texState.root / L"minecraftpe" / L"options.txt";
    const auto backup = texState.root / L"minecraftpe" / L"options.txt.myclient-backup";
    if (std::filesystem::is_regular_file(options, ec))
        std::filesystem::copy_file(options, backup, std::filesystem::copy_options::overwrite_existing, ec);
    done = !ec;
}

// Asks the DLL to apply the current distance once (only while the module and the row are ON).
void RequestRenderDistance()
{
    if (!liteEnabled || !liteRenderEnabled) return;
    BackupGameOptions();
    renderRequestCounter = (renderRequestCounter + 1) & 0xFFu;
}

// Every Optimization setting at once, for the presets.
struct LiteValues
{
    bool clouds, sun, fog, weather, shadows, particles, crit, potion;
    bool entities; int entityDistance; bool mobs, items, misc, players; int playerDistance; bool nameTags;
    bool render; int renderDistance;
    bool priority, throttle, gpu;
};

LiteValues CurrentLiteValues()
{
    return {liteClouds, liteSky, liteFog, liteWeather, liteNoShadows, liteNoParticles, liteNoCrit, liteNoPotion,
            liteEntityLimit, liteEntityDistance, liteCullMobs, liteCullItems, liteCullMisc, litePlayerLimit, litePlayerDistance,
            liteNoNameTags, liteRenderEnabled, liteRenderDistance, liteHighPriority, liteNoThrottle, liteGpuPriority};
}

// Off: nothing. Low: the cheap wins that barely change the look. Balanced: also shadows, potion swirls and
// mobs farther than 64 blocks. Max: everything that costs frames (name tags stay: they matter in a fight).
LiteValues LitePresetValues(int preset)
{
    switch (preset)
    {
    case 1:  return {true, false, false, true, false, false, false, false,
                     true, 96, false, true, true, false, 64, false, false, liteRenderDistance, true, true, false};
    case 2:  return {true, false, false, true, true, false, false, true,
                     true, 64, true, true, true, false, 64, false, false, liteRenderDistance, true, true, true};
    case 3:  return {true, true, true, true, true, true, true, true,
                     true, 32, true, true, true, true, 64, false, true, 8, true, true, true};
    default: return {false, false, false, false, false, false, false, false,
                     false, liteEntityDistance, liteCullMobs, liteCullItems, liteCullMisc, false, litePlayerDistance,
                     false, false, liteRenderDistance, false, false, false};
    }
}

bool SameLiteValues(const LiteValues& a, const LiteValues& b)
{
    // Distances only count while their switch is on (an Off preset keeps whatever the sliders say).
    return a.clouds == b.clouds && a.sun == b.sun && a.fog == b.fog && a.weather == b.weather && a.shadows == b.shadows &&
           a.particles == b.particles && a.crit == b.crit && a.potion == b.potion && a.entities == b.entities &&
           (!a.entities || (a.entityDistance == b.entityDistance && a.mobs == b.mobs && a.items == b.items && a.misc == b.misc)) &&
           a.players == b.players && (!a.players || a.playerDistance == b.playerDistance) && a.nameTags == b.nameTags &&
           a.render == b.render && (!a.render || a.renderDistance == b.renderDistance) &&
           a.priority == b.priority && a.throttle == b.throttle && a.gpu == b.gpu;
}

// The preset the settings match now, or -1 (Custom).
int CurrentLitePreset()
{
    const LiteValues now = CurrentLiteValues();
    for (int i = 0; i < kLitePresetCount; i++)
        if (SameLiteValues(now, LitePresetValues(i))) return i;
    return -1;
}

void ApplyLitePreset(int preset)
{
    const LiteValues v = LitePresetValues(preset);
    const bool renderChanged = v.render != liteRenderEnabled || v.renderDistance != liteRenderDistance;
    liteClouds = v.clouds; liteSky = v.sun; liteFog = v.fog; liteWeather = v.weather; liteNoShadows = v.shadows;
    liteNoParticles = v.particles; liteNoCrit = v.crit; liteNoPotion = v.potion;
    liteEntityLimit = v.entities; liteEntityDistance = v.entityDistance;
    liteCullMobs = v.mobs; liteCullItems = v.items; liteCullMisc = v.misc;
    litePlayerLimit = v.players; litePlayerDistance = v.playerDistance; liteNoNameTags = v.nameTags;
    liteRenderEnabled = v.render; liteRenderDistance = v.renderDistance;
    liteHighPriority = v.priority; liteNoThrottle = v.throttle; liteGpuPriority = v.gpu;
    if (liteEnabled) ApplyLite();
    if (renderChanged) RequestRenderDistance();
    SaveConfig();
}


// ---- Optimization > System: Windows settings on the game's process ----
// Done by the HUD itself (no DLL needed) while the module and the row are ON. A row switched off (or the module,
// or the HUD closing) puts back Windows' default for that setting; only what this HUD changed is put back.
struct ProcessTweaks { DWORD pid = 0; bool priority = false, throttle = false, gpu = false; };
ProcessTweaks processTweaks;
std::wstring processTweakStatus;

struct PowerThrottlingState { ULONG Version, ControlMask, StateMask; };   // PROCESS_POWER_THROTTLING_STATE
constexpr int kProcessPowerThrottling = 4;                                // PROCESS_INFORMATION_CLASS
constexpr ULONG kThrottleExecutionSpeed = 1;                              // PROCESS_POWER_THROTTLING_EXECUTION_SPEED
constexpr int kGpuPriorityNormal = 2, kGpuPriorityHigh = 4;               // D3DKMT_SCHEDULINGPRIORITYCLASS

// `restoreAll`: put everything back (the HUD is closing).
void ApplyProcessTweaks(bool restoreAll = false)
{
    DWORD pid = 0;
    if (minecraftWindow) GetWindowThreadProcessId(minecraftWindow, &pid);
    if (pid != processTweaks.pid)
    {
        processTweaks = ProcessTweaks{};   // another (or no) game process: nothing of ours is set there yet
        processTweaks.pid = pid;
    }
    const bool wantPriority = !restoreAll && liteEnabled && liteHighPriority;
    const bool wantThrottle = !restoreAll && liteEnabled && liteNoThrottle;
    const bool wantGpu = !restoreAll && liteEnabled && liteGpuPriority;
    if (!pid)
    {
        processTweakStatus = (wantPriority || wantThrottle || wantGpu) ? L"System: waiting for Minecraft" : L"";
        return;
    }
    if (wantPriority == processTweaks.priority && wantThrottle == processTweaks.throttle && wantGpu == processTweaks.gpu)
        return;

    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
    {
        processTweakStatus = L"System: Windows refused access to Minecraft (error " + std::to_wstring(GetLastError()) + L")";
        return;
    }
    std::wstring failed;
    if (wantPriority != processTweaks.priority)
    {
        if (SetPriorityClass(process, wantPriority ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS)) processTweaks.priority = wantPriority;
        else failed += L" priority";
    }
    if (wantThrottle != processTweaks.throttle)
    {
        typedef BOOL (WINAPI* SetInfoFn)(HANDLE, int, LPVOID, DWORD);
        const auto setInfo = (SetInfoFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation");
        // ON: execution speed under our control and not throttled. OFF: Windows decides again.
        PowerThrottlingState state{1, wantThrottle ? kThrottleExecutionSpeed : 0u, 0u};
        if (setInfo && setInfo(process, kProcessPowerThrottling, &state, sizeof(state))) processTweaks.throttle = wantThrottle;
        else { failed += L" throttling"; processTweaks.throttle = wantThrottle; }   // not on this Windows: do not retry
    }
    if (wantGpu != processTweaks.gpu)
    {
        typedef LONG (APIENTRY* SetGpuFn)(HANDLE, int);
        HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
        const auto setGpu = gdi ? (SetGpuFn)(void*)GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass") : nullptr;
        if (setGpu && setGpu(process, wantGpu ? kGpuPriorityHigh : kGpuPriorityNormal) >= 0) processTweaks.gpu = wantGpu;
        else { failed += L" GPU"; processTweaks.gpu = wantGpu; }
    }
    CloseHandle(process);
    if (!failed.empty()) processTweakStatus = L"System: Windows refused:" + failed;
    else processTweakStatus = (processTweaks.priority || processTweaks.throttle || processTweaks.gpu) ? L"System: applied to Minecraft" : L"";
}

RECT MenuSoundOptionRect(int i)
{
    return RECT{kPaneX, 200 + 31 + i * 29, kPaneX + 400, 200 + 31 + i * 29 + 27};
}

RECT ReplayOpenRect()
{
    return RECT{kPaneX, 290, kPaneX + 220, 290 + 32};
}
RECT ReplaysButtonRect()
{
    return RECT{kPaneX, 350, kPaneX + 220, 350 + 36};
}

RECT NickFieldRect()
{
    return RECT{kPaneX, 200, kPaneX + 400, 200 + 34};
}

RECT HitboxFilterRect(int index)
{
    const int top = 200 + index * 31;
    return RECT{kPaneX, top, kPaneX + 400, top + 27};
}

RECT ChunkBorderRadiusRect(int index)
{
    const int top = 150 + index * 31;
    return RECT{kPaneX, top, kPaneX + 400, top + 27};
}

void DrawSwitchRow(HDC hdc, const RECT& r, const std::wstring& label, bool on, int fontSize)
{
    const ThemeColors t = CurrentTheme();
    // On a glass page the row is blue while on, like a Mods row.
    DrawRoundBox(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, glassContent ? 12 : 8,
                 glassContent && on ? GlassAccent() : t.card);
    DrawTextSimple(hdc, r.left + 14, r.top + (r.bottom - r.top - fontSize - 4) / 2, label,
                   on ? RGB(255,255,255) : t.text, fontSize);
    const int h = 24, w = 52;
    DrawSwitchAt(hdc, r.right - w - 12, r.top + (r.bottom - r.top - h) / 2, w, h, on);
}

// The Optimization page's list (DrawFeatureDetails draws the title lines above it).
void DrawLitePage(HDC hdc)
{
    const ThemeColors th = CurrentTheme();
    ClampLiteScroll();
    const std::vector<LiteRow>& rows = LiteRows();
    const int preset = CurrentLitePreset();
    for (const LiteShown& shown : LiteLayout())
    {
        const LiteRow& row = rows[shown.index];
        const RECT r = shown.rect;
        const bool active = LiteRowActive(row.key);
        switch (row.kind)
        {
        case LiteRowKind::Header:
        {
            std::wstring text = row.label;
            if (shown.index == 0) text += std::wstring(L"  -  ") + (preset < 0 ? L"Custom" : kLitePresetNames[preset]);
            DrawTextSimple(hdc, r.left + 2, r.top + 4, text, RGB(255,255,255), 14);
            break;
        }
        case LiteRowKind::Presets:
            for (int i = 0; i < kLitePresetCount; i++)
            {
                const RECT b = LitePresetRect(r, i);
                DrawButton(hdc, b.left, b.top, b.right - b.left, b.bottom - b.top, kLitePresetNames[i], i == preset);
            }
            break;
        case LiteRowKind::Switch:
        {
            const bool on = *LiteSwitchPtr(row.key);
            // Rows that depend on another switch are indented under it.
            const bool child = row.key == LiteMobs || row.key == LiteItems || row.key == LiteMisc;
            const RECT box = child ? RECT{r.left + 20, r.top, r.right, r.bottom} : r;
            if (child) DrawRoundBox(hdc, r.left + 8, r.top - 2, 3, r.bottom - r.top + 4, 1, th.muted);
            DrawSwitchRow(hdc, box, row.label, on, 13);
            if (!active)
                DrawTextSimple(hdc, box.right - 64 - TextWidth(hdc, L"(off)", 11) - 8, box.top + 7, L"(off)", th.muted, 11);
            break;
        }
        case LiteRowKind::Slider:
        {
            const LiteSliderInfo s = LiteSlider(row.key);
            const RECT track = LiteTrackRect(r);
            DrawRoundBox(hdc, r.left + 8, r.top - 2, 3, r.bottom - r.top + 2, 1, th.muted);
            DrawTextSimple(hdc, r.left + 20, r.top + 2, row.label, active ? th.text : th.muted, 13);
            const std::wstring value = std::to_wstring(*s.value) + s.unit;
            DrawTextSimple(hdc, r.right - 14 - TextWidth(hdc, value, 13), r.top + 2, value, active ? th.text : th.muted, 13);
            const int trackW = track.right - track.left;
            const int knobX = track.left + trackW * ((*s.value - s.min) / s.step) / ((s.max - s.min) / s.step);
            DrawRoundBox(hdc, track.left, track.top + 5, trackW, 6, 3, th.button);
            if (knobX > track.left)
                DrawRoundBox(hdc, track.left, track.top + 5, knobX - track.left, 6, 3, active ? th.accent : th.muted);
            DrawRoundBox(hdc, knobX - 7, track.top, 14, 16, 7,
                         liteSliderDrag == row.key ? th.accentHi : active ? RGB(235,235,235) : th.muted);
            break;
        }
        case LiteRowKind::Status:
            DrawTextSimple(hdc, r.left + 2, r.top + 2, liteStatus.empty() ? L"Pack rows take effect after the game reloads packs." : liteStatus,
                           RGB(170,170,170), 12);
            DrawTextSimple(hdc, r.left + 2, r.top + 20,
                           processTweakStatus.empty() ? L"Entity and name tag rows work at once (need the updated DLL)." : processTweakStatus,
                           RGB(170,170,170), 12);
            break;
        }
    }

    // Scroll bar: where the shown rows are in the whole list.
    const int total = (int)rows.size(), maxScroll = LiteMaxScroll();
    if (maxScroll > 0)
    {
        const int barTop = kLiteTop, barH = kLiteBottom - kLiteTop, x = kPaneX + 408;
        const int shownCount = (int)LiteLayout().size();
        const int thumbH = std::max(30, barH * shownCount / std::max(1, total));
        const int thumbY = barTop + (barH - thumbH) * liteScrollRow / maxScroll;
        DrawRoundBox(hdc, x, barTop, 4, barH, 2, th.button);
        DrawRoundBox(hdc, x, thumbY, 4, thumbH, 2, th.muted);
    }
}

// A click on the Optimization page at page point (mx, my). True when it hit something.
bool HandleLiteClick(HWND hwnd, int mx, int my)
{
    const std::vector<LiteRow>& rows = LiteRows();
    for (const LiteShown& shown : LiteLayout())
    {
        const LiteRow& row = rows[shown.index];
        const RECT r = shown.rect;
        if (row.kind == LiteRowKind::Presets)
        {
            for (int i = 0; i < kLitePresetCount; i++)
            {
                const RECT b = LitePresetRect(r, i);
                if (!IsInside(mx, my, b.left, b.top, b.right - b.left, b.bottom - b.top)) continue;
                ApplyLitePreset(i);
                InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            continue;
        }
        if (row.kind == LiteRowKind::Slider)
        {
            const RECT track = LiteTrackRect(r);
            if (!IsInside(mx, my, track.left - 8, track.top - 8, track.right - track.left + 16, track.bottom - track.top + 14)) continue;
            *LiteSlider(row.key).value = LiteSliderAt(row.key, track, mx);
            liteSliderDrag = row.key;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (row.kind != LiteRowKind::Switch || !IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top)) continue;
        bool* option = LiteSwitchPtr(row.key);
        if (!option) return false;
        *option = !*option;
        if (LiteKeyUsesPack(row.key) && liteEnabled) ApplyLite();
        if (row.key == LiteRender) RequestRenderDistance();   // ON: apply the chosen distance now
        SaveConfig();
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}

// The dragged slider follows the mouse (page x); `done` = the button was let go.
void DragLiteSlider(int mx, bool done)
{
    if (liteSliderDrag < 0) return;
    const std::vector<LiteRow>& rows = LiteRows();
    for (const LiteShown& shown : LiteLayout())
        if (rows[shown.index].kind == LiteRowKind::Slider && rows[shown.index].key == liteSliderDrag)
            *LiteSlider(liteSliderDrag).value = LiteSliderAt(liteSliderDrag, LiteTrackRect(shown.rect), mx);
    if (!done) return;
    if (liteSliderDrag == LiteRenderDistance) RequestRenderDistance();   // the distance is applied when the knob is let go
    liteSliderDrag = -1;
    ReleaseCapture();
    SaveConfig();
}

// Top-right of every settings page: the keybind button.
void DrawPageHeaderControls(HDC hdc)
{
    const ThemeColors t = CurrentTheme();
    const DetailFeature section = ActiveDetail();
    const int* key = GetShortcutPtr(section);
    const RECT k = HiveKeyRect();
    const bool waiting = waitingForShortcut && shortcutFeature == section;

    DrawButton(hdc, k.left, k.top, k.right - k.left, k.bottom - k.top,
               waiting ? std::wstring(L"Press a key...") : L"Keybind: " + KeyName(key ? *key : 0), waiting);
}

void DrawHivePanel(HDC hdc)
{
    const ThemeColors t = CurrentTheme();
    const wchar_t* names[3] = {L"Party Auto Accept", L"Auto RQ", L"Hive Statistic"};

    // Selected feature: title and its own key.
    DrawTextSimple(hdc, kPaneX, kPaneY + 50, names[hiveSection], RGB(255,255,255), 20);

    if (!hiveEnabled)
        DrawTextSimple(hdc, 535, kPaneY + 400,
            L"Hive Utils is paused: switch the group ON in the list to run these.",
            t.muted, 12);
}


// ---- Details page: feature list on the left (every feature, ON/OFF pill each), selected page on the right.
// Layout space (same as the pages): the list sits left of the page origin x = 535.
constexpr int kListX = 309;
constexpr int kListW = 210;
constexpr int kListTop = 102;
constexpr int kListPitch = 31;
constexpr int kListRowH = 27;
constexpr int kListVisible = 15;
const RECT kListSearchRect{309, 66, 519, 94};
constexpr int kChevronW = 28;   // right part of a row: opens that feature's settings

struct DetailRow
{
    DetailFeature feature;   // kFeatureList entry (HiveUtils for the group and for its children)
    int sub;                 // -1 = normal row / group header, 0 = Party Auto Accept, 1 = Auto RQ
};

std::vector<DetailRow> BuildDetailRows()
{
    std::vector<int> order;

    for (int i = 0; i < kFeatureCount; i++)
        order.push_back(i);

    std::sort(order.begin(), order.end(), [](int a, int b)
    {
        return _wcsicmp(FeatureTitle(kFeatureList[a]).c_str(),
                        FeatureTitle(kFeatureList[b]).c_str()) < 0;
    });

    const bool searching = !searchQuery.empty();
    std::vector<DetailRow> rows;

    for (int i : order)
    {
        if (kFeatureList[i] == DetailFeature::HiveUtils)
        {
            const bool m0 = ContainsNoCase(L"Party Auto Accept", searchQuery);
            const bool m1 = ContainsNoCase(L"Auto RQ", searchQuery);
            const bool m2 = ContainsNoCase(L"Hive Statistic", searchQuery);
            const bool mh = ContainsNoCase(FeatureTitle(kFeatureList[i]), searchQuery);

            if (searching && !(m0 || m1 || m2 || mh))
                continue;

            rows.push_back({kFeatureList[i], -1});

            if (searching ? true : hiveExpanded)
            {
                if (!searching || mh || m0) rows.push_back({DetailFeature::HiveUtils, 0});
                if (!searching || mh || m1) rows.push_back({DetailFeature::HiveUtils, 1});
                if (!searching || mh || m2) rows.push_back({DetailFeature::HiveUtils, 2});
            }

            continue;
        }

        if (searching && !ContainsNoCase(FeatureTitle(kFeatureList[i]), searchQuery))
            continue;

        rows.push_back({kFeatureList[i], -1});
    }

    return rows;
}

RECT DetailRowRect(int visibleIndex)
{
    const int top = kListTop + visibleIndex * kListPitch;
    return RECT{kListX, top, kListX + kListW, top + kListRowH};
}

RECT DetailPillRect(const RECT& row)
{
    return HivePillRect(row);
}

bool DetailRowOn(const DetailRow& r)
{
    if (r.sub >= 0)
        return HiveSectionEnabled(r.sub);

    if (r.feature == DetailFeature::HiveUtils)
        return hiveEnabled;

    return FeatureEnabled(r.feature);
}

bool DetailRowSelected(const DetailRow& r)
{
    if (r.feature != detailFeature)
        return false;

    if (r.feature != DetailFeature::HiveUtils)
        return true;

    // Hive: the header row is lit when the group is collapsed; otherwise the open child is lit.
    if (!hiveExpanded)
        return r.sub < 0;

    return r.sub == hiveSection;
}

void ClampDetailListScroll(int total)
{
    detailListScroll = std::clamp(detailListScroll, 0, std::max(0, total - kListVisible));
}

// Favourite star at the right end of a (non-child) list row.

void DrawDetailList(HDC hdc)
{
    const ThemeColors t = CurrentTheme();
    const std::vector<DetailRow> rows = BuildDetailRows();
    const COLORREF lit = RGB(24, 92, 168);   // a switched-on row

    ClampDetailListScroll((int)rows.size());


    // search
    {
        const RECT r = kListSearchRect;
        DrawRoundBox(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, 8, searchActive ? t.accent : t.button);
        DrawRoundBox(hdc, r.left + 1, r.top + 1, r.right - r.left - 2, r.bottom - r.top - 2, 7, t.bg);

        if (searchQuery.empty() && !searchActive)
            DrawTextSimple(hdc, r.left + 10, r.top + 5, L"Search modules...", t.muted, 13);
        else
            DrawTextSimple(hdc, r.left + 10, r.top + 5, searchQuery + (searchActive ? L"|" : L""), t.text, 13);
    }

    for (int v = 0; v < kListVisible; v++)
    {
        const int index = detailListScroll + v;

        if (index >= (int)rows.size())
            break;

        const DetailRow& r = rows[index];
        RECT row = DetailRowRect(v);

        const bool child = r.sub >= 0;
        const bool group = r.feature == DetailFeature::HiveUtils && !child;

        if (child)
            row.left += 16;

        const bool selected = DetailRowSelected(r);
        const bool on = !group && DetailRowOn(r);

        // The row colour is the switch: lit = ON, dark = OFF.
        DrawRoundBox(hdc, row.left, row.top, row.right - row.left, row.bottom - row.top, 8,
                     on ? lit : t.card);

        std::wstring name;

        if (child)
            name = r.sub == 0 ? L"Party Auto Accept" : (r.sub == 1 ? L"Auto RQ" : L"Hive Statistic");
        else
            name = FeatureTitle(r.feature);

        DrawTextSimple(hdc, row.left + 10, row.top + 5, name,
                       on || selected ? RGB(255,255,255) : t.text, child ? 12 : 13);

        if (selected)
            DrawRoundBox(hdc, row.left, row.top + 6, 3, row.bottom - row.top - 12, 1, t.accentHi);

        if (group)
            DrawChevron(hdc, row.right - kChevronW / 2, (row.top + row.bottom) / 2, hiveExpanded, t.text);
    }

    if ((int)rows.size() > kListVisible)
        DrawTextSimple(hdc, kListX + 6, kListTop + kListVisible * kListPitch + 2,
                       L"Wheel: scroll list", t.muted, 10);
}

// Returns true when the click was handled. Coordinates are in layout space (already shifted).
bool HandleDetailListClick(int mx, int my, bool openSettings = false)
{
    {
        const RECT r = kListSearchRect;
        const bool inside = IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top);
        searchActive = inside;

        if (inside)
        {
            waitingForShortcut = false;
            partyAddingPlayer = false;
            return true;
        }
    }

    if (mx < kListX || mx > kListX + kListW)
        return false;

    const std::vector<DetailRow> rows = BuildDetailRows();

    for (int v = 0; v < kListVisible; v++)
    {
        const int index = detailListScroll + v;

        if (index >= (int)rows.size())
            break;

        const DetailRow& r = rows[index];
        RECT row = DetailRowRect(v);

        if (r.sub >= 0)
            row.left += 16;

        if (!IsInside(mx, my, row.left, row.top, row.right - row.left, row.bottom - row.top))
            continue;

        const bool group = r.feature == DetailFeature::HiveUtils && r.sub < 0;

        waitingForShortcut = false;
        partyAddingPlayer = false;

        if (group)
        {
            hiveExpanded = !hiveExpanded;   // the group header only folds / unfolds
            return true;
        }

        if (openSettings)
        {
            // open this feature's settings
            if (r.feature == DetailFeature::HiveUtils)
            {
                hiveSection = r.sub;
                detailFeature = DetailFeature::HiveUtils;
            }
            else
            {
                if (r.feature == DetailFeature::Textures && detailFeature != DetailFeature::Textures)
                    texScanned = false;

                detailFeature = r.feature;
            }

            return true;
        }

        // The rest of the row switches the feature; the page on the right stays where it is.
        if (r.sub >= 0)
            HiveToggleSection(r.sub);
        else
        {
            const DetailFeature keep = detailFeature;
            ToggleFeature(r.feature);
            detailFeature = keep;
        }

        return true;
    }

    return false;
}

void DrawFeatureDetails(HDC hdc)
{
    if (detailFeature ==
        DetailFeature::None)
    {
        return;
    }

    // Hive Utils shows its selected feature's page in a right-hand pane.
    int x = kPaneX;
    int y = 70;

    DrawPageHeaderControls(hdc);

    if (detailFeature == DetailFeature::HiveUtils)
    {
        DrawHivePanel(hdc);
    }
    else
    {
        DrawTextSimple(
            hdc,
            x,
            y + 50,
            FeatureTitle(
                detailFeature
            ),
            RGB(255,255,255),
            20
        );
    }


    if (detailFeature ==
        DetailFeature::Keystrokes)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"W / A / S / D",
            RGB(190,190,190),
            15
        );

        DrawTextSimple(
            hdc,
            x,
            y + 108,
            L"Move and resize in HUD editor.",
            RGB(150,150,150),
            13
        );
    }
    else if (
        detailFeature ==
        DetailFeature::CPS)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"LMB and RMB are counted separately.",
            RGB(190,190,190),
            14
        );
    }
    else if (
        detailFeature ==
        DetailFeature::Armor)
    {
        DrawTextSimple(hdc, x, y + 82, L"Your armor as the game's own icons (texture packs included).", RGB(220,220,220), 14);
        DrawSwitchRow(hdc, MenuSoundRect(0), L"Show durability", armorShowDurability, 13);
        DrawSwitchRow(hdc, MenuSoundRect(1), L"Horizontal (off = vertical)", armorHorizontal, 13);
        DrawTextSimple(hdc, x, MenuSoundRect(1).bottom + 16,
                       armorHorizontal ? L"Icons in a row; durability under each icon."
                                       : L"Icons in a column; remaining/maximum beside each icon.",
                       RGB(170,170,170), 13);
        DrawTextSimple(hdc, x, MenuSoundRect(1).bottom + 38, L"Move and resize it in the HUD editor.", RGB(170,170,170), 13);
    }
    else if (
        detailFeature ==
        DetailFeature::Arrows)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"Arrow count display",
            RGB(190,190,190),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 108,
            L"Live inventory and offhand arrow total.",
            RGB(150,150,150),
            13
        );

        DrawSwitchRow(hdc, CounterBackgroundRect(), L"Black background", arrowBackground, 13);
        DrawSwitchRow(hdc, CounterTranslucentRect(), L"Translucent (see the game through it)", arrowTranslucent, 13);
        DrawTranslucentStatus(hdc, x, CounterTranslucentRect().bottom + 8, arrowBackground && arrowTranslucent && arrowCounterEnabled);
    }
    else if (
        detailFeature ==
        DetailFeature::FPS)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"FPS display",
            RGB(190,190,190),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 108,
            L"Minecraft render FPS (1-second average).",
            RGB(150,150,150),
            13
        );
    }
    else if (
        ActiveDetail() ==
        DetailFeature::PartyAutoAccept)
    {
        DrawPartyDetails(
            hdc,
            x,
            y
        );
    }
    else if (
        detailFeature ==
        DetailFeature::BlockOutline)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"Changes the colour of the game's own block outline.",
            RGB(220,220,220),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 108,
            L"Look at a block to see it. Pick a colour:",
            RGB(150,150,150),
            13
        );

        for (int i = 0; i < 12; i++)
        {
            const RECT r = OutlineSwatchRect(i);

            const bool selected =
                blockOutlineSettings.color ==
                kOutlinePalette[i];

            // selection frame, then the colour itself
            DrawRoundBox(
                hdc, r.left - 3, r.top - 3,
                (r.right - r.left) + 6, (r.bottom - r.top) + 6, 9,
                selected ? RGB(255,255,255) : RGB(70,70,70)
            );

            DrawRoundBox(
                hdc, r.left, r.top,
                r.right - r.left, r.bottom - r.top, 7,
                kOutlinePalette[i]
            );
        }

        DrawTextSimple(
            hdc,
            x,
            y + 232,
            L"Color: RGB(" +
                std::to_wstring(GetRValue(blockOutlineSettings.color)) + L", " +
                std::to_wstring(GetGValue(blockOutlineSettings.color)) + L", " +
                std::to_wstring(GetBValue(blockOutlineSettings.color)) + L")",
            RGB(190,190,190),
            14
        );

        const bool available =
            MinecraftDataProvider::IsOutlineAvailable();
        const bool running =
            MinecraftDataProvider::IsOutlineRunning();
        const bool colored =
            MinecraftDataProvider::IsOutlineColored();

        const wchar_t* state =
            !blockOutlineEnabled
                ? L"State: OFF"
                : (!available
                    ? L"State: UNAVAILABLE (needs the updated DLL)"
                    : (colored
                        ? L"State: APPLIED"
                        : (running
                            ? L"State: RUNNING (colour not confirmed - check the outline)"
                            : L"State: WAITING (look at a block)")));

        DrawTextSimple(
            hdc,
            x,
            y + 260,
            state,
            (blockOutlineEnabled && colored)
                ? RGB(100,220,130)
                : (blockOutlineEnabled && !available
                    ? RGB(230,120,100)
                    : (blockOutlineEnabled && running
                        ? RGB(230,200,100)
                        : RGB(190,190,190))),
            14
        );

        {
            bool leaseFresh = false;
            unsigned int echoRgb = 0;
            const bool haveEcho = MinecraftDataProvider::GetOutlineEcho(leaseFresh, echoRgb);

            DrawTextSimple(
                hdc,
                x,
                y + 278,
                haveEcho
                    ? L"DLL holds: RGB(" + std::to_wstring((echoRgb >> 16) & 0xFF) + L", " +
                          std::to_wstring((echoRgb >> 8) & 0xFF) + L", " + std::to_wstring(echoRgb & 0xFF) +
                          L"), request " + (leaseFresh ? L"active" : L"NOT active")
                    : std::wstring(L"DLL holds: ---"),
                RGB(130,130,130),
                12
            );
        }

        DrawTextSimple(
            hdc,
            x,
            y + 294,
            HookRateLine(2),
            RGB(130,130,130),
            12
        );
    }
    else if (
        detailFeature ==
        DetailFeature::Textures)
    {
        DrawTexturesDetails(hdc, x, y);
    }
    else if (
        ActiveDetail() ==
        DetailFeature::AutoRq)
    {
        DrawAutoRqDetails(hdc, x, y);
    }
    else if (
        ActiveDetail() ==
        DetailFeature::HiveStats)
    {
        DrawHiveStatsDetails(hdc, x, y);
    }
    else if (
        detailFeature ==
        DetailFeature::ShulkerPreview)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Hover a shulker box in your inventory or a chest to see what is inside.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            L"Nothing is shown until you hover one. Needs the updated MyClientData DLL.",
            RGB(170,170,170), 13
        );
    }
    else if (
        detailFeature ==
        DetailFeature::TabList)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Lists the players in the world or server you are in.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            L"Nothing is shown until you hover one. Needs the updated MyClientData DLL.",
            RGB(170,170,170), 13
        );
    }
    else if (
        detailFeature ==
        DetailFeature::Lite)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Turn off visual extras and tune Windows for more FPS.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            liteEnabled ? L"Optimization: ON" : L"Optimization: OFF  (switch it ON in the list on the left)",
            liteEnabled ? RGB(100,220,130) : RGB(230,160,100), 13
        );

        DrawLitePage(hdc);
    }
    else if (
        detailFeature ==
        DetailFeature::LeftHand)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Shows your arm and held item on the left in first person.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            leftHandEnabled ? L"Left Hand: ON" : L"Left Hand: OFF  (switch it ON in the list on the left)",
            leftHandEnabled ? RGB(100,220,130) : RGB(230,160,100), 13
        );

        DrawTextSimple(hdc, x, y + 140, L"Only your own view changes; other players still see you as usual.", CurrentTheme().text, 13);
        DrawTextSimple(hdc, x, y + 162, L"Item models are drawn by the game and keep their right-hand shape.", CurrentTheme().text, 13);
        DrawTextSimple(hdc, x, y + 196, liteStatus.empty() ? L"Takes effect after the game reloads packs." : liteStatus, RGB(170,170,170), 13);
        DrawTextSimple(hdc, x, y + 218, L"A server's own player animations can override it.", RGB(170,170,170), 13);
    }
    else if (detailFeature == DetailFeature::HitEffect)
    {
        DrawTextSimple(hdc, x, y + 82, L"Java-style hit particles. Pick the effects below.", RGB(220,220,220), 14);
        DrawTextSimple(hdc, x, y + 106,
            hitEffectEnabled ? L"Hit Effect: ON" : L"Hit Effect: OFF  (switch it ON in the list on the left)",
            hitEffectEnabled ? RGB(100,220,130) : RGB(230,160,100), 13);
        DrawSwitchRow(hdc, MenuSoundRect(0), L"Crit effect", hitEffectCrit, 13);
        DrawSwitchRow(hdc, MenuSoundRect(1), L"Enchant effect", hitEffectMagic, 13);
        const int below = MenuSoundRect(1).bottom;
        if (!hitEffectCrit && !hitEffectMagic)
            DrawTextSimple(hdc, x + 420, MenuSoundRect(0).top + 6, L"Both OFF: nothing shows", RGB(230,160,100), 13);
        DrawTextSimple(hdc, x, below + 16, L"Only you see these particles (needs the updated DLL).", CurrentTheme().text, 13);
        DrawTextSimple(hdc, x, below + 38, L"Blue sparks come from the client pack: they show after the game reloads packs.", RGB(170,170,170), 13);
        DrawTextSimple(hdc, x, below + 60, L"\"No Critical Hit Effect\" in Optimization hides the crit sparks too.", RGB(170,170,170), 13);
    }
    else if (detailFeature == DetailFeature::FrameGen)
    {
        DrawTextSimple(hdc, x, y + 82, L"Shows made-up frames between the game's frames for smoother motion.", RGB(220,220,220), 14);
        float shown = 0; uint32_t state = 0;
        const bool known = MinecraftDataProvider::GetFrameGen(shown, state);
        float game = 0;
        const bool knownGame = MinecraftDataProvider::GetFps(game);
        std::wstring status;
        COLORREF statusColor = RGB(230,160,100);
        if (!frameGenEnabled) status = L"Frame Generation: OFF  (switch it ON in the list on the left)";
        else if (!known) status = L"Frame Generation: ON - waiting for the game (needs the updated DLL)";
        else if (state == MyClientFeatures::FrameGenRunning)
        {
            status = L"Running: " + std::to_wstring(std::lround(shown)) + L" FPS shown" +
                     (knownGame ? L" (game " + std::to_wstring(std::lround(game)) + L")" : std::wstring());
            statusColor = RGB(100,220,130);
        }
        else if (state == MyClientFeatures::FrameGenPaused) status = L"Paused (menu, loading or a hitch)";
        else if (state == MyClientFeatures::FrameGenUnavailable) status = L"Unavailable on this PC (see the DLL log)";
        else status = L"Frame Generation: ON - waiting for the game";
        DrawTextSimple(hdc, x, y + 106, status, statusColor, 13);
        DrawSwitchRow(hdc, MenuSoundRect(0), L"x3 (OFF: x2)", frameGenMultiplier >= 3, 13);
        DrawSwitchRow(hdc, MenuSoundRect(1), L"Simple blend (no motion)", frameGenMethod == 1, 13);
        DrawSwitchRow(hdc, MenuSoundRect(2), L"Test: repeat frame only", frameGenMethod == 2, 13);
        DrawSwitchRow(hdc, MenuSoundRect(3), L"Pacing", frameGenPacing, 13);
        const int below = MenuSoundRect(3).bottom;
        DrawTextSimple(hdc, x, below + 16, L"Raises the FPS you see, not the game's own FPS; adds a little input delay.", CurrentTheme().text, 13);
        DrawTextSimple(hdc, x, below + 38, L"Little effect when the game's FPS is already above your monitor's refresh rate.", RGB(170,170,170), 13);
        DrawTextSimple(hdc, x, below + 60, L"Paused in menus and while loading. Needs the updated DLL (D3D12 only).", RGB(170,170,170), 13);
        DrawTextSimple(hdc, x, below + 82, L"Highest FPS number: Pacing OFF (+ Test: repeat frame only = lightest).", RGB(170,170,170), 13);
        DrawTextSimple(hdc, x, below + 104, L"V-Sync is switched off while this is ON (the game's setting is not changed).", RGB(170,170,170), 13);
    }
    else if (
        detailFeature ==
        DetailFeature::FastInventory)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Opens the inventory, chests and other screens faster.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            fastInventoryEnabled ? L"Fast Inventory: ON" : L"Fast Inventory: OFF  (switch it ON in the list on the left)",
            fastInventoryEnabled ? RGB(100,220,130) : RGB(230,160,100), 13
        );

        {
            const std::wstring from = L"Screen animation: 0.4\u20130.6 s";
            const int arrowX = x + TextWidth(hdc, from, 13) + 10;
            DrawTextSimple(hdc, x, y + 140, from, CurrentTheme().text, 13);
            DrawRightArrow(hdc, arrowX, y + 140 + TextMiddle(hdc, 13) - 1, CurrentTheme().text);   // 2 px shaft centred
            DrawTextSimple(hdc, arrowX + 26, y + 140, L"0.1 s", RGB(100,220,130), 13);
        }
        DrawTextSimple(hdc, x, y + 170, liteStatus.empty() ? L"Takes effect after the game reloads packs." : liteStatus, RGB(170,170,170), 13);
        DrawTextSimple(hdc, x, y + 192, L"A server's own UI pack can override it.", RGB(170,170,170), 13);
    }
    else if (
        detailFeature ==
        DetailFeature::MenuSounds)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Plays a sound when you switch a module on or off.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            L"Pick the sound. Switch this module OFF in the list to mute.",
            RGB(190,190,190), 13
        );

        const ThemeColors th = CurrentTheme();
        const RECT head = MenuSoundRect(0);
        DrawRoundBox(hdc, head.left, head.top, head.right - head.left, head.bottom - head.top, 8,
                     menuSoundOpen ? th.accent : th.button);
        DrawTextSimple(hdc, head.left + 12, head.top + 5, MenuSoundName(toggleSoundMode), th.text, 13);

        // Arrow on the right (points down, up while the list is open).
        {
            const float ax = head.right - 22.0f, ay = (head.top + head.bottom) / 2.0f;
            if (menuSoundOpen)
                FillTriangleSmooth(hdc, ax - 6, ay + 3.5f, ax + 6, ay + 3.5f, ax, ay - 3.5f, th.text);
            else
                FillTriangleSmooth(hdc, ax - 6, ay - 3.5f, ax + 6, ay - 3.5f, ax, ay + 3.5f, th.text);
        }

        if (menuSoundOpen)
        {
            for (int i = 0; i < kMenuSoundCount; i++)
            {
                const RECT r = MenuSoundOptionRect(i);
                const int mode = kMenuSoundOrder[i];
                DrawRoundBox(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, 8,
                             mode == toggleSoundMode ? RGB(24,92,168) : th.tile);
                DrawTextSimple(hdc, r.left + 12, r.top + 5, MenuSoundName(mode), th.text, 13);
            }
        }
    }
    else if (
        detailFeature ==
        DetailFeature::Replay)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Records every packet the game receives to a .mcrec file.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            L"Early test: recording only. Playback is not built yet.",
            RGB(190,190,190), 13
        );

        MinecraftDataProvider::ReplayStatus st{};
        const bool have = MinecraftDataProvider::GetReplayStatus(st);
        std::wstring line;
        COLORREF col = RGB(190,190,190);
        if (!replayEnabled)
            line = L"State: OFF (switch it ON in the list - also works mid-game)";
        else if (have)
        {
            wchar_t buf[128];
            swprintf(buf, 128, L"State: RECORDING  %02u:%02u   %.1f MB   %u packets",
                     st.seconds / 60, st.seconds % 60, st.bytes / 1048576.0, st.packets);
            line = buf;
            col = RGB(100,220,130);
        }
        else
            line = L"State: WAITING (needs the updated DLL, or the game is not connected)";

        DrawTextSimple(hdc, x, y + 138, line, col, 14);

        if (have && st.path[0])
            DrawTextSimple(hdc, x, y + 166, L"File: " + Utf8ToWide(st.path), RGB(150,150,150), 11);

        const RECT o = ReplayOpenRect();
        DrawButton(hdc, o.left, o.top, o.right - o.left, o.bottom - o.top, L"Open replay folder", false);

        DrawTextSimple(
            hdc, x, o.bottom + 8,
            L"Start recording any time: it also keeps the game from when you joined.",
            RGB(170,170,170), 12
        );

        const RECT rp = ReplaysButtonRect();
        DrawButton(hdc, rp.left, rp.top, rp.right - rp.left, rp.bottom - rp.top, L"Open Replays", true);
        DrawTextSimple(hdc, x, rp.bottom + 8, L"Play, rename, favourite, download or delete recordings.", RGB(170,170,170), 12);
        DrawTextSimple(hdc, x, rp.bottom + 26, L"Also on the Minecraft home screen: the Replays button at the top left.", RGB(170,170,170), 12);
    }
    else if (
        detailFeature ==
        DetailFeature::Nick)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Changes the name above your own character (third person).",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            L"Only you see it. Other players still see your real name.",
            RGB(190,190,190), 13
        );

        const RECT f = NickFieldRect();
        const ThemeColors th = CurrentTheme();
        DrawRoundBox(hdc, f.left, f.top, f.right - f.left, f.bottom - f.top, 8, nickEditing ? th.accent : th.button);
        DrawTextSimple(hdc, f.left + 12, f.top + 7,
                       nickText + (nickEditing ? L"|" : L""), th.text, 14);

        DrawTextSimple(
            hdc, x, f.bottom + 10,
            nickEditing ? L"Type a name, Enter to save." : L"Click the box to change the name (default: Nick).",
            RGB(170,170,170), 12
        );

        DrawTextSimple(
            hdc, x, f.bottom + 32,
            L"Shows your tag in third person. Needs the updated DLL.",
            RGB(170,170,170), 12
        );
    }
    else if (
        detailFeature ==
        DetailFeature::Hitbox)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Pick which hitboxes to show. Parts behind blocks are hidden.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            L"Colours: players red, items yellow, entities white, mobs green.",
            RGB(190,190,190), 13
        );

        static const wchar_t* const names[4] = {L"Players", L"Items", L"Entities", L"Mobs"};
        for (int i = 0; i < 4; i++)
            DrawSwitchRow(hdc, HitboxFilterRect(i), names[i], ((hitboxFilter >> i) & 1) != 0, 13);

        DrawTextSimple(
            hdc, x, HitboxFilterRect(4).top + 4,
            L"Glass also hides them (on purpose). Needs the updated DLL.",
            RGB(170,170,170), 12
        );
        // Last state seen in game, so a "nothing shows" report says which part is missing.
        const std::wstring seen = hitboxSeenBoxes < 0 ? std::wstring(L"Last in game: not played with Hitbox ON yet")
            : L"Last in game: " + std::to_wstring(hitboxSeenBoxes) + L" boxes from the DLL, camera " +
              (hitboxSeenCamera ? L"OK" : L"MISSING") + L", player data " + (hitboxSeenList ? L"OK" : L"MISSING");
        DrawTextSimple(hdc, x, HitboxFilterRect(4).top + 26, seen, CurrentTheme().text, 12);
    }
    else if (
        detailFeature ==
        DetailFeature::ChunkBorder)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Draws the chunk borders around you (gold = your chunk).",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            L"Lines are drawn over walls too. Needs the updated DLL.",
            RGB(170,170,170), 13
        );

        for (int i = 0; i < 3; i++)
        {
            wchar_t label[32];
            swprintf(label, 32, L"%d chunk%ls around you", i + 1, i ? L"s" : L"");
            DrawSwitchRow(hdc, ChunkBorderRadiusRect(i), label, chunkBorderRadius == i + 1, 13);
        }
    }
    else if (
        detailFeature ==
        DetailFeature::FeatureList)
    {
        DrawTextSimple(
            hdc, x, y + 82,
            L"Shows the names of the features you have switched on.",
            RGB(220,220,220), 14
        );

        DrawTextSimple(
            hdc, x, y + 106,
            L"Move it in the HUD editor like the other elements.",
            RGB(170,170,170), 13
        );
    }


    else if (
        detailFeature ==
        DetailFeature::Ping)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"Shows your ping (delay) to the server you are on.",
            RGB(220,220,220),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 108,
            L"Measured by OriginX Client with one small UDP ping per second.",
            RGB(150,150,150),
            13
        );

        std::string pingHost;
        int pingPort = 0;
        const bool haveTarget =
            MinecraftDataProvider::GetPingTarget(pingHost, pingPort);

        int pingMs = 0;
        const bool known = PingMonitor::Get(pingMs);

        DrawTextSimple(
            hdc,
            x,
            y + 140,
            haveTarget
                ? L"Server: " + std::wstring(pingHost.begin(), pingHost.end()) + L":" + std::to_wstring(pingPort)
                : std::wstring(L"Server: ---"),
            RGB(190,190,190),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 168,
            known
                ? L"Ping: " + std::to_wstring(pingMs) + L" ms"
                : std::wstring(L"Ping: ---"),
            RGB(190,190,190),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 198,
            !pingEnabled
                ? L"State: OFF"
                : (!haveTarget
                    ? L"State: WAITING (join a server)"
                    : (known
                        ? L"State: MEASURING"
                        : L"State: NO REPLY (the server may ignore pings)")),
            (pingEnabled && known)
                ? RGB(100,220,130)
                : (pingEnabled && haveTarget
                    ? RGB(230,200,100)
                    : RGB(190,190,190)),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 228,
            L"Green: under 60 ms    Yellow: under 120 ms    Red: 120 ms or more",
            RGB(130,130,130),
            12
        );

        DrawTextSimple(
            hdc,
            x,
            y + 248,
            L"Not available on servers that do not use the standard UDP transport.",
            RGB(130,130,130),
            12
        );
    }


    else if (
        detailFeature ==
        DetailFeature::Nametag)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"Shows your own name tag above your head.",
            RGB(220,220,220),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 108,
            L"Meant for third-person view (front / back camera).",
            RGB(150,150,150),
            13
        );

        const bool available =
            MinecraftDataProvider::IsNametagAvailable();

        const bool applied =
            MinecraftDataProvider::IsNametagApplied();

        DrawTextSimple(
            hdc,
            x,
            y + 138,
            !nametagEnabled
                ? L"State: OFF"
                : (!available
                    ? L"State: UNAVAILABLE (needs the updated DLL)"
                    : (applied
                        ? L"State: APPLIED"
                        : L"State: WAITING (switch to third person)")),
            (nametagEnabled && applied)
                ? RGB(100,220,130)
                : (nametagEnabled && !available
                    ? RGB(230,120,100)
                    : RGB(190,190,190)),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 172,
            L"Turning it OFF or closing OriginX Client",
            RGB(130,130,130),
            12
        );

        DrawTextSimple(
            hdc,
            x,
            y + 190,
            L"returns the game to normal.",
            RGB(130,130,130),
            12
        );

        DrawTextSimple(
            hdc,
            x,
            y + 226,
            HookRateLine(1),
            RGB(130,130,130),
            12
        );
    }


    else if (
        detailFeature ==
        DetailFeature::Fullbright)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"Keeps the game at full brightness.",
            RGB(220,220,220),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 108,
            L"Dark caves and nights are lit like night vision.",
            RGB(150,150,150),
            13
        );

        const bool available =
            MinecraftDataProvider::IsFullbrightAvailable();

        const bool applied =
            MinecraftDataProvider::IsFullbrightApplied();

        DrawTextSimple(
            hdc,
            x,
            y + 138,
            !fullbrightEnabled
                ? L"State: OFF"
                : (!available
                    ? L"State: UNAVAILABLE (game hook not found)"
                    : (applied
                        ? L"State: APPLIED"
                        : L"State: WAITING FOR GAME")),
            (fullbrightEnabled && applied)
                ? RGB(100,220,130)
                : (fullbrightEnabled && !available
                    ? RGB(230,120,100)
                    : RGB(190,190,190)),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 172,
            L"Turning it OFF or closing OriginX Client",
            RGB(130,130,130),
            12
        );

        DrawTextSimple(
            hdc,
            x,
            y + 192,
            L"returns the game to normal within a second.",
            RGB(130,130,130),
            12
        );

        DrawTextSimple(
            hdc,
            x,
            y + 226,
            HookRateLine(0),
            RGB(130,130,130),
            12
        );
    }


    else if (
        detailFeature ==
        DetailFeature::Resources)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"Iron / Gold / Diamond / Emerald totals",
            RGB(190,190,190),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 108,
            L"Counts the whole inventory, hotbar and offhand.",
            RGB(150,150,150),
            13
        );

        DrawSwitchRow(hdc, CounterBackgroundRect(), L"Black background", resourceBackground, 13);
        DrawSwitchRow(hdc, CounterTranslucentRect(), L"Translucent (see the game through it)", resourceTranslucent, 13);
        DrawTranslucentStatus(hdc, x, CounterTranslucentRect().bottom + 8, resourceBackground && resourceTranslucent && resourceCounterEnabled);

        DrawTextSimple(
            hdc,
            x,
            y + 130,
            L"Move and resize in the HUD editor.",
            RGB(150,150,150),
            13
        );
    }
    else if (
        detailFeature ==
        DetailFeature::Zoom)
    {
        DrawTextSimple(
            hdc,
            x,
            y + 82,
            L"Hold the Zoom key, scroll the wheel to zoom in/out.",
            RGB(220,220,220),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 110,
            L"Key: " +
                KeyName(shortcuts.zoom),
            RGB(190,190,190),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 138,
            MinecraftDataProvider::IsZoomApplied()
                ? L"State: APPLIED"
                : (zoomActive ? L"State: WAITING FOR GAME" : L"State: READY"),
            zoomActive
                ? RGB(100,220,130)
                : RGB(190,190,190),
            14
        );

        DrawTextSimple(
            hdc,
            x,
            y + 172,
            L"Release the key or leave the game",
            RGB(130,130,130),
            12
        );

        DrawTextSimple(
            hdc,
            x,
            y + 192,
            L"window to restore normal FOV.",
            RGB(130,130,130),
            12
        );

        {
            wchar_t zl[48];
            swprintf(zl, 48, L"Zoom level: %.1fx (wheel while held)",
                     zoomFactorX100.load() / 100.0);
            DrawTextSimple(hdc, x, y + 222, zl, RGB(190,190,190), 14);
        }
    }
}


// HUD editor: no menu panel (it used to cover the HUD at the top-left).
// Only a small toolbar at the bottom-centre of the game window with Reset / Done.
bool EditorToolbarRect(
    int& x,
    int& y,
    int& w,
    int& h)
{
    RECT r;

    if (!overlayWindow ||
        !GetClientRect(overlayWindow, &r))
    {
        return false;
    }

    w = 620;
    h = 64;
    x = std::max(0, (int)(r.right - w) / 2);
    y = std::max(0, (int)r.bottom - h - 28);

    return true;
}


void EditorButtonRects(
    int tx,
    int ty,
    int tw,
    RECT& reset,
    RECT& menu,
    RECT& done)
{
    done  = RECT{tx + tw - 16 - 96, ty + 15,
                 tx + tw - 16,      ty + 15 + 34};

    menu  = RECT{done.left - 12 - 88, ty + 15,
                 done.left - 12,      ty + 15 + 34};

    reset = RECT{menu.left - 12 - 88, ty + 15,
                 menu.left - 12,      ty + 15 + 34};
}


void DrawHudEditor(HDC hdc)
{
    int x, y, w, h;

    if (!EditorToolbarRect(x, y, w, h))
        return;

    const ThemeColors t = CurrentTheme();

    DrawRoundBox(hdc, x, y, w, h, 14, t.accent);
    DrawRoundBox(hdc, x + 2, y + 2, w - 4, h - 4, 12, t.bg);

    DrawTextSimple(
        hdc,
        x + 20,
        y + 9,
        L"HUD Editor",
        t.text,
        18
    );

    const bool notice =
        GetTickCount64() < editorNoticeUntil;

    DrawTextSimple(
        hdc,
        x + 20,
        y + 36,
        notice
            ? L"HUD positions were reset to the defaults."
            : L"Drag: move   Wheel: size   Esc: done",
        notice ? t.accentHi : t.muted,
        13
    );

    RECT reset, menu, done;
    EditorButtonRects(x, y, w, reset, menu, done);

    DrawRoundBox(
        hdc, menu.left, menu.top,
        menu.right - menu.left, menu.bottom - menu.top,
        8, t.button
    );

    DrawTextCentered(
        hdc, menu.left, menu.top,
        menu.right - menu.left, menu.bottom - menu.top,
        L"Menu", t.text, 15
    );

    DrawRoundBox(
        hdc, reset.left, reset.top,
        reset.right - reset.left, reset.bottom - reset.top,
        8, t.button
    );

    DrawTextCentered(
        hdc, reset.left, reset.top,
        reset.right - reset.left, reset.bottom - reset.top,
        L"Reset", t.text, 15
    );

    DrawRoundBox(
        hdc, done.left, done.top,
        done.right - done.left, done.bottom - done.top,
        8, t.accent
    );

    DrawTextCentered(
        hdc, done.left, done.top,
        done.right - done.left, done.bottom - done.top,
        L"Done", t.onAccent, 15
    );
}


// ---- Presets ----------------------------------------------------------------------------------
std::wstring PresetsPath() { return GetExecutableDirectory() + L"\\myclient_presets.txt"; }

void LoadPresets()
{
    presetsLoaded = true;
    presets.clear();
    std::ifstream in(std::filesystem::path(PresetsPath()), std::ios::binary);
    std::string line;
    while (std::getline(in, line) && presets.size() < Presets::MaxPresets)
    {
        Presets::Preset preset;
        if (Presets::Parse(line, preset)) presets.push_back(std::move(preset));
    }
}

void SavePresets()
{
    std::ofstream out(std::filesystem::path(PresetsPath()), std::ios::binary | std::ios::trunc);
    for (const auto& preset : presets) out << Presets::Serialize(preset) << "\n";
}

// What a preset remembers: every module's ON/OFF, and Hive Utils' own switches one by one.
Presets::Preset CapturePreset(const std::string& name)
{
    Presets::Preset preset;
    preset.name = Presets::CleanName(name);
    for (int i = 0; i < kFeatureCount; i++)
    {
        const DetailFeature f = kFeatureList[i];
        if (f == DetailFeature::HiveUtils || !FeatureHasSwitch(f)) continue;
        preset.states.emplace_back("f" + std::to_string((int)f), FeatureEnabled(f));
    }
    preset.states.emplace_back("hive", hiveEnabled);
    preset.states.emplace_back("party", partyAutoAcceptEnabled);
    preset.states.emplace_back("autorq", autoRqEnabled);
    preset.states.emplace_back("hivestats", hiveStatsEnabled);
    return preset;
}

// Switches every module the preset knows to its saved state through the normal toggle (so pack-based
// modules rewrite their pack). Modules added after the preset was saved are left as they are.
void ApplyPreset(const Presets::Preset& preset)
{
    applyingPreset = true;
    for (const auto& [key, on] : preset.states)
    {
        if (key == "hive") { hiveEnabled = on; continue; }
        if (key == "party") { partyAutoAcceptEnabled = on; continue; }
        if (key == "autorq") { autoRqEnabled = on; continue; }
        if (key == "hivestats") { hiveStatsEnabled = on; continue; }
        if (key.size() < 2 || key[0] != 'f') continue;
        const int value = std::atoi(key.c_str() + 1);
        for (int i = 0; i < kFeatureCount; i++)
        {
            const DetailFeature f = kFeatureList[i];
            if ((int)f != value || f == DetailFeature::HiveUtils) continue;
            if (FeatureEnabled(f) != on) ToggleFeature(f);
            break;
        }
    }
    applyingPreset = false;
    SaveConfig();
    PushToast(L"Preset: " + Utf8ToWide(preset.name), true);
}

// Page layout (menu coordinates).
const RECT kPresetNameRect{190, 150, 560, 186};
const RECT kPresetSaveRect{572, 150, 712, 186};
constexpr int kPresetRowTop = 232, kPresetRowH = 40, kPresetRowGap = 6;
RECT PresetRowRect(int i) { const int top = kPresetRowTop + i * (kPresetRowH + kPresetRowGap); return RECT{190, top, 900, top + kPresetRowH}; }
RECT PresetButtonRect(int i, int button)   // 0 = Apply, 1 = Update, 2 = Delete (square trash button)
{
    const RECT r = PresetRowRect(i);
    const int h = (r.bottom - r.top) - 12;
    const RECT trash{r.right - 8 - h, r.top + 6, r.right - 8, r.bottom - 6};
    if (button == 2) return trash;
    const int right = trash.left - 8 - (1 - button) * 92;
    return RECT{right - 84, r.top + 6, right, r.bottom - 6};
}

void DrawPresets(HDC hdc)
{
    if (!presetsLoaded) LoadPresets();
    const ThemeColors t = CurrentTheme();
    const int x = 190, y = 70;
    DrawTextSimple(hdc, x, y, L"Presets", RGB(255,255,255), 26);
    DrawTextSimple(hdc, x, y + 44, L"Save which modules are ON under a name, then switch back to it with one click.", t.muted, 13);

    const RECT n = kPresetNameRect;
    DrawRoundBox(hdc, n.left, n.top, n.right - n.left, n.bottom - n.top, 8, presetEditing ? t.accent : t.button);
    const std::wstring shown = presetNameText.empty() && !presetEditing ? std::wstring(L"Preset name (e.g. PvP)") : presetNameText + (presetEditing ? L"|" : L"");
    DrawTextSimple(hdc, n.left + 12, n.top + 9, shown, presetNameText.empty() && !presetEditing ? t.muted : t.text, 14);
    const RECT b = kPresetSaveRect;
    DrawRoundBox(hdc, b.left, b.top, b.right - b.left, b.bottom - b.top, 8, t.accent);
    DrawTextCentered(hdc, b.left, b.top, b.right - b.left, b.bottom - b.top, L"Save Preset", t.onAccent, 14);
    DrawTextSimple(hdc, x, n.bottom + 12,
                   presetStatus.empty() ? L"Up to " + std::to_wstring(Presets::MaxPresets) + L" presets. Same name = overwrite." : presetStatus,
                   t.muted, 12);

    if (presets.empty())
        DrawTextSimple(hdc, x, kPresetRowTop + 8, L"No presets yet.", t.muted, 14);
    for (int i = 0; i < (int)presets.size(); i++)
    {
        const RECT r = PresetRowRect(i);
        DrawRoundBox(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, 8, t.card);
        DrawTextSimple(hdc, r.left + 14, r.top + 10, Utf8ToWide(presets[i].name), t.text, 15);
        DrawTextSimple(hdc, r.left + 260, r.top + 12, std::to_wstring(Presets::CountOn(presets[i])) + L" modules ON", t.muted, 12);
        const wchar_t* labels[2] = {L"Apply", L"Update"};
        for (int k = 0; k < 2; k++)
        {
            const RECT c = PresetButtonRect(i, k);
            DrawRoundBox(hdc, c.left, c.top, c.right - c.left, c.bottom - c.top, 8, k == 0 ? t.accent : t.button);
            DrawTextCentered(hdc, c.left, c.top, c.right - c.left, c.bottom - c.top, labels[k],
                             k == 0 ? t.onAccent : t.text, 13);
        }
        DrawTrashButton(hdc, PresetButtonRect(i, 2), TrashArmed(L"preset:" + Utf8ToWide(presets[i].name)));
    }
}

void SavePresetFromBox()
{
    if (!presetsLoaded) LoadPresets();
    const std::string name = Presets::CleanName(WideToUtf8(presetNameText));
    if (name.empty()) { presetStatus = L"Type a name first."; return; }
    for (auto& preset : presets)
        if (preset.name == name)
        {
            preset = CapturePreset(name);
            SavePresets();
            presetStatus = L"Updated \"" + Utf8ToWide(name) + L"\".";
            return;
        }
    if (presets.size() >= Presets::MaxPresets) { presetStatus = L"The list is full: delete or update a preset."; return; }
    presets.push_back(CapturePreset(name));
    SavePresets();
    presetNameText.clear();
    presetStatus = L"Saved \"" + Utf8ToWide(name) + L"\".";
}

// Clicks on the Presets page; true when handled.
bool HandlePresetsClick(HWND hwnd, int mx, int my)
{
    const auto inside = [&](const RECT& r) { return IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top); };
    if (inside(kPresetNameRect)) { presetEditing = true; SetFocus(hwnd); return true; }
    if (inside(kPresetSaveRect)) { presetEditing = false; SavePresetFromBox(); return true; }
    for (int i = 0; i < (int)presets.size(); i++)
    {
        if (inside(PresetButtonRect(i, 0)))
        {
            ApplyPreset(presets[i]);
            presetStatus = L"Applied \"" + Utf8ToWide(presets[i].name) + L"\".";
            return true;
        }
        if (inside(PresetButtonRect(i, 1)))
        {
            presets[i] = CapturePreset(presets[i].name);
            SavePresets();
            presetStatus = L"Updated \"" + Utf8ToWide(presets[i].name) + L"\" with the modules ON now.";
            return true;
        }
        if (inside(PresetButtonRect(i, 2)))
        {
            if (!TrashConfirm(L"preset:" + Utf8ToWide(presets[i].name)))
            {
                presetStatus = L"Click the trash again to delete \"" + Utf8ToWide(presets[i].name) + L"\".";
                return true;
            }
            presetStatus = L"Deleted \"" + Utf8ToWide(presets[i].name) + L"\".";
            presets.erase(presets.begin() + i);
            SavePresets();
            return true;
        }
    }
    return false;
}

void DrawSettings(HDC hdc)
{
    int x = 190;
    int y = 70;

    DrawTextSimple(
        hdc,
        x,
        y,
        L"Settings",
        RGB(255,255,255),
        26
    );

    DrawTextSimple(
        hdc,
        x,
        y + 48,
        L"Client Name: " +
            clientName,
        RGB(220,220,220),
        16
    );

    DrawTextSimple(
        hdc,
        x,
        y + 82,
        L"GUI Scale: " +
            std::to_wstring(
                guiScale
            ).substr(0,4),
        RGB(190,190,190),
        14
    );

    DrawTextSimple(
        hdc,
        x,
        y + 112,
        L"Menu Animation: " +
            std::wstring(
                menuAnimation
                    ? L"ON"
                    : L"OFF"
            ),
        RGB(190,190,190),
        14
    );

    DrawButton(
        hdc,
        x,
        y + 145,
        260,
        42,
        L"GUI Scale +",
        false
    );

    DrawButton(
        hdc,
        x,
        y + 195,
        260,
        42,
        L"GUI Scale -",
        false
    );

    DrawButton(
        hdc,
        x,
        y + 245,
        260,
        42,
        L"Toggle Animation",
        menuAnimation
    );

    DrawButton(
        hdc,
        x,
        y + 295,
        260,
        42,
        L"Reset HUD Layout",
        false
    );

    DrawTextSimple(
        hdc,
        x,
        y + 405,
        L"External Windows Client",
        RGB(130,130,130),
        13
    );
}


// ------------------------- Auto RQ page ----------------------------------------------------------
std::wstring AsciiToWide(const char* text)
{
    std::wstring out;
    while (text && *text) out.push_back((wchar_t)(unsigned char)*text++);
    return out;
}

// Two columns of five mode buttons (layout space like the Textures page: details origin x = 535).
RECT AutoRqModeRect(int index)
{
    const int top = 200 + index * 31;
    return RECT{kPaneX, top, kPaneX + 400, top + 27};
}

void DrawAutoRqDetails(HDC hdc, int x, int y)
{
    const ThemeColors t = CurrentTheme();

    DrawTextSimple(
        hdc, x, y + 82,
        L"Queues the next Hive game for you (team modes wait for your team).",
        t.muted, 13
    );

    DrawTextSimple(
        hdc, x, y + 106,
        autoRqEnabled
            ? L"Auto RQ: ON"
            : L"Auto RQ: OFF  (switch it ON in the list on the left)",
        autoRqEnabled ? RGB(100,220,130) : RGB(230,160,100),
        13
    );

    for (int i = 0; i < (int)MyClientAutoRq::kModeCount; i++)
    {
        const RECT r = AutoRqModeRect(i);
        DrawSwitchRow(hdc, r, AsciiToWide(MyClientAutoRq::kModes[i].name), i == autoRqMode, 13);
    }

    const int below = AutoRqModeRect((int)MyClientAutoRq::kModeCount - 1).bottom;
    DrawTextSimple(
        hdc, x, below + 8,
        L"Sends:  /" + AsciiToWide(MyClientAutoRq::kModes[autoRqMode].command) +
            (MyClientAutoRq::kModes[autoRqMode].team ? L"   when your team is out or the game ends" : L"   when you die or the game ends"),
        t.muted, 13
    );
    if (MyClientAutoRq::kModes[autoRqMode].team)
        DrawTextSimple(hdc, x, below + 28, L"In a party only the leader can queue on Hive; members do nothing.", t.muted, 12);
}

RECT HiveStatsAutoRect()
{
    return RECT{kPaneX, 190, kPaneX + 400, 190 + 27};
}

RECT HiveStatsGameRect(int index)
{
    const int top = 190 + (index + 1) * 31;   // row 0 of the list is the "Auto" switch
    return RECT{kPaneX, top, kPaneX + 400, top + 27};
}

void DrawHiveStatsDetails(HDC hdc, int x, int y)
{
    const ThemeColors t = CurrentTheme();

    DrawTextSimple(hdc, x, y + 82, L"Shows KDR, win rate and level of the players in your match.", t.muted, 13);
    DrawTextSimple(hdc, x, y + 104, L"Data comes from The Hive's public API.", t.muted, 12);

    {
        std::wstring label = L"Auto-detect game";
        if (hiveStatsAuto)
            label += hiveDetectedGame >= 0
                ? L"  (" + AsciiToWide(HiveStats::kGames[hiveDetectedGame].name) + L")"
                : L"  (not detected - using the pick below)";
        DrawSwitchRow(hdc, HiveStatsAutoRect(), label, hiveStatsAuto, 13);
    }

    for (int i = 0; i < HiveStats::kGameCount; i++)
    {
        const bool shown = hiveStatsAuto && hiveDetectedGame >= 0 ? i == hiveDetectedGame : i == hiveStatsGame;
        DrawSwitchRow(hdc, HiveStatsGameRect(i), AsciiToWide(HiveStats::kGames[i].name), shown, 13);
    }

    DrawTextSimple(hdc, x, 190 + (HiveStats::kGameCount + 1) * 31 + 4,
                   L"Needs the updated DLL for the player list. Move the table in the HUD editor.", t.muted, 11);
}

// ------------------------- Textures page ---------------------------------------------------------
std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return L"";

    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);

    if (length <= 0)
        return std::wstring(text.begin(), text.end());

    std::wstring wide(length, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &wide[0], length);

    return wide;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();

    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);

    if (length <= 0)
        return std::string();

    std::string utf8(length, '\0');

    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], length, nullptr, nullptr);

    return utf8;
}

// The generated Lite pack is not one the user picks: keep it out of the shown / edited stack.
void StripLitePack(TexturePacks::State& state)
{
    auto& ac = state.active;
    ac.erase(std::remove(ac.begin(), ac.end(), std::string(LitePack::kPackUuid)), ac.end());
    auto& st = state.stack;
    st.erase(std::remove_if(st.begin(), st.end(), [](const TexturePacks::Entry& e) { return e.uuid == LitePack::kPackUuid; }), st.end());
}

void RefreshTextures()
{
    texState =
        TexturePacks::Scan(
            TexturePacks::CandidateRootsFromEnvironment()
        );

    {
        // The generated Lite pack is not a pack the user picks: hide it from the list.
        auto& pk = texState.packs;
        pk.erase(std::remove_if(pk.begin(), pk.end(),
                     [](const TexturePacks::Pack& q) { return q.uuid == LitePack::kPackUuid; }),
                 pk.end());
        StripLitePack(texState);
    }

    texScanned = true;
    texFileTime = TexturePacks::WriteTime(texState.globalFile);
    // Which data folders were looked at and which one holds the list MyClient writes, for when a switch does not
    // reach the game (textures_scan.txt next to the HUD).
    if (FILE* f = _wfopen((GetExecutableDirectory() + L"\\textures_scan.txt").c_str(), L"wb"))
    {
        std::vector<std::wstring> lines;
        std::error_code ec;
        for (const auto& root : TexturePacks::CandidateRootsFromEnvironment())
        {
            const bool dir = std::filesystem::is_directory(root, ec);
            const bool options = std::filesystem::is_regular_file(root / L"minecraftpe" / L"options.txt", ec);
            const bool list = std::filesystem::is_regular_file(root / L"minecraftpe" / L"global_resource_packs.json", ec);
            const bool packs = std::filesystem::is_directory(root / L"resource_packs", ec);
            lines.push_back((root == texState.root ? L"* " : L"  ") + root.wstring() + (dir ? L"" : L"  (missing)") +
                            (options ? L"  options.txt" : L"") + (list ? L"  global_resource_packs.json" : L"") +
                            (packs ? L"  resource_packs" : L""));
        }
        lines.push_back(L"list: " + texState.globalFile.wstring() + L" (" + std::to_wstring(texState.stack.size()) + L" entries)");
        for (const auto& root : TexturePacks::PackRoots(texState))
            lines.push_back(L"OriginX Lite goes to: " + (root / L"resource_packs" / L"OriginX_Lite").wstring());
        lines.push_back(std::to_wstring(texState.packs.size()) + L" packs found");
        for (const std::wstring& line : lines) { const std::string u = WideToUtf8(line) + "\r\n"; fwrite(u.data(), 1, u.size(), f); }
        fclose(f);
    }
    texScroll = 0;
    texEditing = false;          // a rescan shows the game's real list again
    texPendingStack.clear();
    texSubpackFor.clear();
    texStatus = Utf8ToWide(texState.note);
}

// Every OriginX_Lite folder the pack is written to (TexturePacks::PackRoots), the active list's folder first.
std::vector<std::filesystem::path> LitePackFolders()
{
    std::vector<std::filesystem::path> folders;
    for (const auto& root : TexturePacks::PackRoots(texState))
        folders.push_back(root / L"resource_packs" / L"OriginX_Lite");
    return folders;
}

TexturePacks::Pack LitePackRef()
{
    TexturePacks::Pack ref;
    ref.name = "OriginX Lite";
    ref.uuid = LitePack::kPackUuid;
    ref.folder = texState.root / L"resource_packs" / L"OriginX_Lite";
    // The list entry must name the version the folder's manifest has now (it follows the content).
    TexturePacks::Pack onDisk;
    if (TexturePacks::ParseManifest(TexturePacks::ReadFile(ref.folder / L"manifest.json"), onDisk) &&
        onDisk.uuid == ref.uuid)
        std::copy(onDisk.version, onDisk.version + 3, ref.version);
    return ref;
}

// Particle identifiers the two particle switches silence, read from the game's own vanilla pack
// (data\resource_packs\vanilla\particles) so they match this game version; the usual names when the
// game folder is not known yet. Read once per game folder.
std::string vanillaCritJson;   // the game's own critical-hit particle file (Hit Effect copies it)
void GameParticleIds(std::vector<std::string>& crit, std::vector<std::string>& potion, std::vector<std::string>& all)
{
    static std::wstring scannedFor;
    static std::vector<std::string> foundCrit, foundPotion, foundAll;
    const std::wstring game = MinecraftDataProvider::GameDirectory();
    if (!game.empty() && game != scannedFor)
    {
        scannedFor = game;
        std::vector<std::string> files;
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::path(game) / L"data" / L"resource_packs" / L"vanilla" / L"particles";
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->path().extension() != L".json") continue;
            std::ifstream in(it->path(), std::ios::binary);
            files.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        foundCrit = LitePack::MatchParticles(files, LitePack::CritKeys());
        foundPotion = LitePack::MatchParticles(files, LitePack::PotionKeys());
        foundAll = LitePack::MatchParticles(files, LitePack::AllKeys());
        vanillaCritJson.clear();
        for (const auto& text : files)
            if (LitePack::ParticleIdentifier(text).find("critical_hit") != std::string::npos) { vanillaCritJson = text; break; }
    }
    crit = foundCrit.empty() ? LitePack::DefaultCritParticles() : foundCrit;
    potion = foundPotion.empty() ? LitePack::DefaultPotionParticles() : foundPotion;
    all = foundAll;   // No particles needs the game's own list (nothing is silenced before the game folder is known)
}

// Every module whose effect comes from the generated client pack (LitePack.h). Add new pack-based modules
// here only: ApplyLite writes the pack from it and the DLL is told to reload packs from it.
bool ClientPackWanted()
{
    // Left Hand is not here any more: the DLL mirrors the whole first-person pass (arm and item), and the pack's
    // mirrored arm animations would put the arm back on the right.
    return liteEnabled || fastInventoryEnabled || (hitEffectEnabled && hitEffectMagic);
}
// The DLL reloads packs only while the HUD reports pack switching as wanted: switching the last pack-based
// module OFF made it "not wanted" in the same moment, so that reload never ran and the game kept the
// pack's effects. The request stays on for a few seconds after every client-pack rewrite.
ULONGLONG clientPackReloadUntil = 0;

std::string VanillaCritParticle()
{
    std::vector<std::string> crit, potion, all;
    GameParticleIds(crit, potion, all);   // scans the game folder once
    return vanillaCritJson;
}

// Writes / removes the generated Lite pack and puts it above the user's chosen pack.
void ApplyLite()
{
    if (!texState.found)
        RefreshTextures();

    if (!texState.found)
    {
        liteStatus = L"Minecraft's data folder was not found.";
        return;
    }

    const std::vector<std::filesystem::path> folders = LitePackFolders();

    // The pack carries Optimization and Fast Inventory; each part only while its module is ON.
    const bool packWanted = ClientPackWanted();
    if (packWanted)
    {
        LitePack::Options o;
        o.clouds = liteEnabled && liteClouds; o.sky = liteEnabled && liteSky;
        o.fog = liteEnabled && liteFog; o.weather = liteEnabled && liteWeather;
        o.fastUi = fastInventoryEnabled;
        o.leftHand = false;   // see ClientPackWanted
        o.noCrit = liteEnabled && liteNoCrit;
        o.noPotion = liteEnabled && liteNoPotion;
        o.noParticles = liteEnabled && liteNoParticles;
        o.noShadows = liteEnabled && liteNoShadows;
        if (o.noCrit || o.noPotion || o.noParticles)
            GameParticleIds(o.critParticles, o.potionParticles, o.allParticles);
        if (hitEffectEnabled && hitEffectMagic)
            o.enchantedHit = LitePack::EnchantedHitParticle(VanillaCritParticle());
        bool written = false;
        for (const auto& folder : folders)
            written = LitePack::Write(folder, o) || written;
        if (!written)
        {
            liteStatus = L"Could not write the Lite pack.";
            return;
        }
    }
    else
    {
        // Leaving the pack's files behind kept e.g. Left Hand's animations around after it was switched off.
        std::error_code ec;
        for (const auto& folder : folders)
            if (std::filesystem::exists(folder, ec))
                LitePack::Write(folder, LitePack::NothingOn());
    }
    const TexturePacks::Pack ref = LitePackRef();   // after writing: the version follows the content

    // The player's own packs stay exactly as they are (all of them, same order); Lite goes on top.
    std::string error;
    const std::vector<TexturePacks::Entry> stack = texState.stack;
    if (!TexturePacks::WriteStack(texState, stack, error, packWanted ? &ref : nullptr))
    {
        liteStatus = L"Could not update the pack list: " + Utf8ToWide(error);
        return;
    }
    StripLitePack(texState);
    texFileTime = TexturePacks::WriteTime(texState.globalFile);

    texturesSeq++;
    clientPackReloadUntil = GetTickCount64() + 5000;
    liteStatus = packWanted ? L"Applied  (reload requested)" : L"Off  (reload requested)";
}

// Makes `stack` the active pack list (first = highest priority), with the Lite pack on top while it is needed.
bool WriteTextureStack(const std::vector<TexturePacks::Entry>& stack, std::string& error)
{
    TexturePacks::Pack liteRef;
    const bool packWanted = ClientPackWanted();
    if (packWanted) liteRef = LitePackRef();
    if (!TexturePacks::WriteStack(texState, stack, error, packWanted ? &liteRef : nullptr))
        return false;
    StripLitePack(texState);
    texFileTime = TexturePacks::WriteTime(texState.globalFile);
    texturesSeq++;   // asks the DLL to make the game re-read the list
    return true;
}

// Pack list inside the Textures settings page (layout space: details origin x = 535, y = 70).
const int kTexRowsVisible = 9;
const int kTexListTop = 246;
const int kTexRowPitch = 34;
const int kTexRowHeight = 30;
const RECT kTexRefreshRect{925, 148, 1035, 176};
const RECT kPackInstallRect{925, 180, 1035, 208};
const RECT kPackInboxRect{925, 212, 1035, 240};

// Delete button at the right end of pack row r (0 = top visible row). The first click arms it (turns red),
// a second click within kTexDeleteArmMs sends the pack folder to the Recycle Bin.
constexpr ULONGLONG kTexDeleteArmMs = 4000;
std::wstring texDeleteArmed;          // folder of the pack waiting for the second click
ULONGLONG texDeleteArmedAt = 0;
bool TexDeleteArmed(const TexturePacks::Pack& pack)
{
    return texDeleteArmed == pack.folder.wstring() && GetTickCount64() - texDeleteArmedAt < kTexDeleteArmMs;
}
// Clicking packs edits a copy of the active stack (texPendingStack); Done writes it, Cancel drops it.
// While nothing differs from the game's list, texEditing is false and the list shows the real stack.
const std::vector<TexturePacks::Entry>& TexShownStack() { return texEditing ? texPendingStack : texState.stack; }
int TexStackPos(const std::vector<TexturePacks::Entry>& stack, const std::string& uuid)
{
    for (size_t i = 0; i < stack.size(); i++)
        if (stack[i].uuid == uuid) return (int)i;
    return -1;
}
bool SameStack(const std::vector<TexturePacks::Entry>& a, const std::vector<TexturePacks::Entry>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].uuid != b[i].uuid || a[i].json != b[i].json) return false;
    return true;
}
// Pack shown in list row `index` (1..): packs in the stack first, in priority order, then the rest A-Z.
const TexturePacks::Pack& TexRowPack(int index)
{
    std::vector<int> order;
    for (const auto& e : TexShownStack())
        for (int i = 0; i < (int)texState.packs.size(); i++)
            if (texState.packs[i].uuid == e.uuid && std::find(order.begin(), order.end(), i) == order.end()) { order.push_back(i); break; }
    for (int i = 0; i < (int)texState.packs.size(); i++)
        if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
    return texState.packs[order[index - 1]];
}
// Rows that are in the stack show [up][down] [#priority] before the delete button.
RECT TexMoveRect(int r, bool up)
{
    const int top = kTexListTop + r * kTexRowPitch, right = 535 + 496 - 5 - 26 - 4 - 34 - 4;
    return up ? RECT{right - 50, top + 4, right - 26, top + kTexRowHeight - 4}
              : RECT{right - 24, top + 4, right, top + kTexRowHeight - 4};
}
// Packs with subpacks: a gear button left of the stack controls opens the subpack list.
RECT TexGearRect(int r)
{
    const int top = kTexListTop + r * kTexRowPitch, right = 535 + 496 - 5 - 26 - 4 - 34 - 4 - 50 - 6;
    return RECT{right - 26, top + 3, right, top + kTexRowHeight - 3};
}
constexpr int kTexSubpackItemH = 26, kTexSubpackMax = 8;
// The open list: under the gear of row r (the row index on screen), right-aligned with it.
RECT TexSubpackItemRect(int r, int i)
{
    const RECT g = TexGearRect(r);
    const int top = g.bottom + 4 + i * kTexSubpackItemH;
    return RECT{g.right - 240, top, g.right, top + kTexSubpackItemH - 2};
}
// Subpack the shown stack selects for `pack` ("" = none chosen: the game uses the pack's default).
std::string TexSubpackChosen(const TexturePacks::Pack& pack)
{
    const auto& stack = TexShownStack();
    const int pos = TexStackPos(stack, pack.uuid);
    return pos >= 0 ? TexturePacks::SubpackOf(stack[pos]) : std::string();
}
std::wstring TexSubpackLabel(const TexturePacks::Pack& pack, const std::string& folder)
{
    for (const auto& sp : pack.subpacks)
        if (sp.folder == folder) return Utf8ToWide(sp.name.empty() ? sp.folder : sp.name);
    return folder.empty() ? L"Default" : Utf8ToWide(folder);
}

RECT TexNumberRect(int r)
{
    const int top = kTexListTop + r * kTexRowPitch, right = 535 + 496 - 5 - 26 - 4;
    return RECT{right - 34, top + 4, right, top + kTexRowHeight - 4};
}
// Done / Cancel under the last shown row while there are unapplied changes.
int TexFooterTop()
{
    const int total = (int)texState.packs.size() + 1;
    const int shown = std::min(kTexRowsVisible, total - texScroll);
    return kTexListTop + shown * kTexRowPitch + 2;
}
RECT TexDoneRect() { const int y = TexFooterTop(); return RECT{535 + 496 - 90, y, 535 + 496, y + 28}; }
RECT TexCancelRect() { const int y = TexFooterTop(); return RECT{535 + 496 - 188, y, 535 + 496 - 98, y + 28}; }

void ApplyPendingTextures()
{
    if (!texturesEnabled)
    {
        texStatus = L"Turn switching ON first (Textures in the list on the left).";
        return;
    }
    std::string error;
    const std::vector<TexturePacks::Entry> stack = texPendingStack;
    if (!WriteTextureStack(stack, error))
    {
        texStatus = L"Could not switch: " + Utf8ToWide(error);
        return;
    }
    texEditing = false;
    texPendingStack.clear();
    texStatus = stack.empty() ? L"Switched to: Default textures  (reload requested)"
                              : L"Applied " + std::to_wstring(stack.size()) + (stack.size() == 1 ? L" pack" : L" packs") + L"  (reload requested)";
}

void UpdatePendingStatus()
{
    texEditing = !SameStack(texPendingStack, texState.stack);
    texStatus = texEditing ? L"Not applied yet - click Done to use " + std::to_wstring(texPendingStack.size()) +
                                 (texPendingStack.size() == 1 ? L" pack." : L" packs.")
                           : std::wstring();
}

RECT TexDeleteRect(int r, bool)
{
    const int top = kTexListTop + r * kTexRowPitch, right = 535 + 496 - 5;
    return RECT{right - 26, top + 3, right, top + kTexRowHeight - 3};
}

std::filesystem::path PackInboxFolder()
{
    return TexturePacks::EnvPath("LOCALAPPDATA", L"LOCALAPPDATA") / L"MyClient" / L"packs-inbox";
}

// Unpacks a zip-based archive (.mcpack / .mcaddon / .zip) with the tar.exe that ships with Windows 10+.
bool ExtractWithTar(const std::filesystem::path& archive, const std::filesystem::path& destination)
{
    std::wstring command =
        L"tar.exe -xf \"" + archive.wstring() + L"\" -C \"" + destination.wstring() + L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return false;

    const DWORD waited = WaitForSingleObject(process.hProcess, 120000);
    DWORD code = 1;

    if (waited == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &code);
    else
        TerminateProcess(process.hProcess, 1);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    return waited == WAIT_OBJECT_0 && code == 0;
}

void InstallInboxPacks()
{
    RefreshTextures();

    if (!texState.found)
    {
        texStatus = L"Minecraft data folder not found - cannot install packs.";
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(PackInboxFolder(), ec);

    const PackInstall::Summary r =
        PackInstall::InstallInbox(PackInboxFolder(), texState.root, ExtractWithTar);

    RefreshTextures();

    if (r.files == 0 && r.failed == 0)
    {
        texStatus = L"Inbox is empty. Put .mcpack / .mcaddon / .zip files in it first.";
        return;
    }

    texStatus =
        L"Installed " + std::to_wstring(r.Installed()) +
        L" (resource " + std::to_wstring(r.resource) +
        L", add-on " + std::to_wstring(r.behavior) +
        L", skin " + std::to_wstring(r.skin) + L")" +
        (r.failed ? L"  failed " + std::to_wstring(r.failed) : std::wstring()) +
        (r.skipped ? L"  skipped " + std::to_wstring(r.skipped) : std::wstring()) +
        L" - restart Minecraft for add-ons";
}

// Removes an installed pack: takes it off the active list first, then moves its folder to the Recycle Bin
// (so a mistake can be undone from there).
void DeleteTexturePack(const TexturePacks::Pack& pack)
{
    const std::wstring name = Utf8ToWide(pack.name);
    if (pack.folder.empty() || !std::filesystem::is_directory(pack.folder))
    {
        RefreshTextures();
        texStatus = L"Could not delete " + name + L": folder not found.";
        return;
    }
    if (TexturePacks::IsActive(texState, pack))
    {
        // Only this pack leaves the active list; the rest of the stack stays.
        std::vector<TexturePacks::Entry> stack = texState.stack;
        stack.erase(std::remove_if(stack.begin(), stack.end(), [&](const TexturePacks::Entry& e) { return e.uuid == pack.uuid; }), stack.end());
        std::string error;
        if (!WriteTextureStack(stack, error))
        {
            texStatus = L"Could not delete " + name + L": " + Utf8ToWide(error);
            return;
        }
    }

    std::wstring from = pack.folder.wstring();
    from.push_back(L'\0');   // SHFileOperation takes a double-null-terminated list
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int result = SHFileOperationW(&op);
    RefreshTextures();   // resets texStatus and any unapplied edit, so the result is set after it
    texStatus = result == 0 && !op.fAnyOperationsAborted
        ? L"Moved to the Recycle Bin: " + name
        : L"Could not delete " + name + L" (is Minecraft using it? error " + std::to_wstring(result) + L")";
}

// Packs switched in Minecraft's own settings rewrite global_resource_packs.json: show that list here too
// (checked once a second while the page is open; an unapplied edit on the page is left alone).
void FollowGameTextureList()
{
    const ULONGLONG now = GetTickCount64();
    if (!texScanned || texEditing || !texState.found || now - texFileCheckedAt < 1000)
        return;
    texFileCheckedAt = now;
    if (TexturePacks::WriteTime(texState.globalFile) == texFileTime)
        return;
    const std::wstring status = texStatus;
    RefreshTextures();
    if (texStatus.empty())
        texStatus = status.empty() ? L"Updated from Minecraft's settings." : status;
}

void DrawTexturesDetails(HDC hdc, int x, int y)
{
    if (!texScanned)
        RefreshTextures();
    FollowGameTextureList();

    const ThemeColors t = CurrentTheme();

    DrawTextSimple(
        hdc, x, y + 82,
        L"Click packs to stack them (#1 = top), then click Done.",
        t.muted, 13
    );

    DrawRoundBox(
        hdc, kTexRefreshRect.left, kTexRefreshRect.top,
        kTexRefreshRect.right - kTexRefreshRect.left,
        kTexRefreshRect.bottom - kTexRefreshRect.top, 8, t.button
    );

    DrawTextCentered(
        hdc, kTexRefreshRect.left, kTexRefreshRect.top,
        kTexRefreshRect.right - kTexRefreshRect.left,
        kTexRefreshRect.bottom - kTexRefreshRect.top,
        L"Refresh", t.text, 14
    );

    for (const RECT* r : {&kPackInstallRect, &kPackInboxRect})
    {
        DrawRoundBox(
            hdc, r->left, r->top,
            r->right - r->left, r->bottom - r->top, 8, t.button
        );

        DrawTextCentered(
            hdc, r->left, r->top,
            r->right - r->left, r->bottom - r->top,
            r == &kPackInstallRect ? L"Install all" : L"Open inbox",
            t.text, 14
        );
    }

    DrawTextSimple(
        hdc, x, y + 106,
        texturesEnabled
            ? L"Switching: ON"
            : L"Switching: OFF  (switch Textures ON in the list on the left)",
        texturesEnabled ? RGB(100,220,130) : RGB(230,160,100),
        13
    );

    const bool available =
        MinecraftDataProvider::IsTexturesAvailable();

    unsigned int reloads = 0;
    const bool haveReloads =
        MinecraftDataProvider::GetTextureReloads(reloads);

    DrawTextSimple(
        hdc, x, y + 126,
        std::wstring(L"Reload function: ") +
            (available
                ? L"AVAILABLE"
                : L"UNAVAILABLE (needs the updated DLL)") +
            (haveReloads
                ? L"    Reloads done: " + std::to_wstring(reloads)
                : std::wstring()),
        available ? RGB(100,220,130) : RGB(230,120,100),
        13
    );

    DrawTextSimple(
        hdc, x, y + 148,
        texStatus.empty()
            ? L"A server that forces its own pack can still override this."
            : texStatus,
        t.muted, 12
    );

    // row 0 = default textures, then the installed packs
    const int total = (int)texState.packs.size() + 1;

    texScroll = std::clamp(texScroll, 0, std::max(0, total - kTexRowsVisible));

    for (int r = 0; r < kTexRowsVisible; r++)
    {
        const int index = texScroll + r;

        if (index >= total)
            break;

        const int rowY = kTexListTop + r * kTexRowPitch;

        const bool isDefault = index == 0;
        const TexturePacks::Pack* pack =
            isDefault ? nullptr : &TexRowPack(index);

        // [pack icon]  name ...  [up][down] [#n] [x]; packs in the (edited) stack are highlighted, #1 = top
        const auto& shownStack = TexShownStack();
        const int pos = isDefault ? -1 : TexStackPos(shownStack, pack->uuid);
        const bool active = isDefault ? shownStack.empty() : pos >= 0;
        DrawRoundBox(hdc, x, rowY, 496, kTexRowHeight, 8, active ? t.accent : t.card);

        const int icon = kTexRowHeight - 6;
        const std::filesystem::path iconFile = isDefault
            ? std::filesystem::path(MinecraftDataProvider::GameDirectory()) / L"data" / L"resource_packs" / L"vanilla" / L"pack_icon.png"
            : pack->folder / L"pack_icon.png";
        static ItemIcons::FileCache packIcons;
        if (const ItemIcons::Icon* found = (isDefault && MinecraftDataProvider::GameDirectory().empty()) ? nullptr : packIcons.Get(iconFile, icon))
            ItemIcons::Draw(hdc, *found, x + 6, rowY + 3, icon);
        else
            DrawRoundBox(hdc, x + 6, rowY + 3, icon, icon, 5, t.button);

        DrawTextSimple(
            hdc, x + 40, rowY + 6,
            isDefault ? std::wstring(L"Default textures") : Utf8ToWide(pack->name),
            active ? t.onAccent : t.text, 15
        );

        if (pos >= 0)
        {
            const RECT n = TexNumberRect(r);
            DrawTextCentered(hdc, n.left, n.top, n.right - n.left, n.bottom - n.top, L"#" + std::to_wstring(pos + 1), t.onAccent, 13);
            if (shownStack.size() > 1)
                for (const bool up : {true, false})
                {
                    const RECT m = TexMoveRect(r, up);
                    const bool can = up ? pos > 0 : pos + 1 < (int)shownStack.size();
                    DrawRoundBox(hdc, m.left, m.top, m.right - m.left, m.bottom - m.top, 6, t.accentHi);
                    DrawChevron(hdc, (m.left + m.right) / 2, (m.top + m.bottom) / 2, up,
                                can ? t.onAccent : MixColor(t.accentHi, t.onAccent, 0.35f));
                }
        }

        if (pack)
        {
            const bool armed = TexDeleteArmed(*pack);
            DrawTrashButton(hdc, TexDeleteRect(r, armed), armed);
        }

        if (pack && !pack->subpacks.empty())
        {
            const RECT g = TexGearRect(r);
            const bool open = texSubpackFor == pack->folder.wstring();
            DrawRoundBox(hdc, g.left, g.top, g.right - g.left, g.bottom - g.top, 6, open ? t.accentHi : t.button);
            DrawTextCentered(hdc, g.left, g.top, g.right - g.left, g.bottom - g.top, L"\u2699", t.text, 15, L"Segoe UI Symbol");
            // The chosen subpack, right-aligned before the gear (only when it fits after the pack name).
            const std::wstring sub = TexSubpackLabel(*pack, TexSubpackChosen(*pack));
            const int subW = TextWidth(hdc, sub, 12);
            const int nameEnd = x + 40 + TextWidth(hdc, Utf8ToWide(pack->name), 15);
            if (g.left - 8 - subW > nameEnd + 10)
                DrawTextSimple(hdc, g.left - 8 - subW, rowY + 9, sub, active ? t.onAccent : t.muted, 12);
        }
    }

    // Open subpack list, drawn last so it lies over the rows below it.
    for (int r = 0; r < kTexRowsVisible && !texSubpackFor.empty(); r++)
    {
        const int index = texScroll + r;
        if (index < 1 || index >= total) continue;
        const TexturePacks::Pack& pack = TexRowPack(index);
        if (pack.folder.wstring() != texSubpackFor) continue;
        const std::string chosen = TexSubpackChosen(pack);
        const int n = std::min((int)pack.subpacks.size(), kTexSubpackMax);
        const RECT first = TexSubpackItemRect(r, 0), last = TexSubpackItemRect(r, n - 1);
        DrawRoundBox(hdc, first.left - 4, first.top - 4, first.right - first.left + 8, last.bottom - first.top + 8, 8, t.tile);
        for (int i = 0; i < n; i++)
        {
            const RECT it = TexSubpackItemRect(r, i);
            const bool on = pack.subpacks[i].folder == chosen;
            DrawRoundBox(hdc, it.left, it.top, it.right - it.left, it.bottom - it.top, 6, on ? t.accent : t.card);
            DrawTextSimple(hdc, it.left + 10, it.top + 4, TexSubpackLabel(pack, pack.subpacks[i].folder),
                           on ? t.onAccent : t.text, 13);
        }
    }

    if (total > kTexRowsVisible)
        DrawTextSimple(hdc, x, kTexListTop + kTexRowsVisible * kTexRowPitch, L"Scroll: mouse wheel", t.muted, 11);

    if (texEditing)
    {
        const RECT d = TexDoneRect(), c = TexCancelRect();
        DrawRoundBox(hdc, c.left, c.top, c.right - c.left, c.bottom - c.top, 8, t.button);
        DrawTextCentered(hdc, c.left, c.top, c.right - c.left, c.bottom - c.top, L"Cancel", t.text, 14);
        DrawRoundBox(hdc, d.left, d.top, d.right - d.left, d.bottom - d.top, 8, t.accent);
        DrawTextCentered(hdc, d.left, d.top, d.right - d.left, d.bottom - d.top, L"Done", t.onAccent, 14);
    }
}


// The G menu panel is 920x590 and starts at (10,10) in its own coordinates.
// Shift it so it sits in the middle of the game window. (The HUD layout editor
// keeps raw coordinates because it edits positions on the game screen.)
bool GlassContentShown();
void GlassContentOrigin(int& ox, int& oy);   // with the glass pages

void MenuOffset(
    int& ox,
    int& oy)
{
    ox = 0;
    oy = 0;

    if (GlassContentShown())
    {
        GlassContentOrigin(ox, oy);
        return;
    }

    if (!overlayWindow || hudEditMode)
        return;

    RECT r;

    if (!GetClientRect(
            overlayWindow,
            &r))
    {
        return;
    }

    ox = std::max(
        0,
        (int)(r.right - 920) / 2 - 10
    );

    oy = std::max(
        0,
        (int)(r.bottom - 590) / 2 - 10
    );
}


// ------------------------- Home (radial) --------------------
// The first screen of the menu: the player's skin head in the middle and four translucent ring
// segments around it (MODS up, HUD left, CLOSET right, SETTINGS down). Drawn straight on the game
// (no panel); the ring and a light dim let the game show through (per-pixel alpha, OverlayAlpha).
enum class HomeItem { None = -1, Mods = 0, Closet = 1, Settings = 2, Hud = 3 };   // clockwise from the top
HomeItem homeHover = HomeItem::None;

constexpr COLORREF kHomeDim = RGB(6,6,9);             // whole screen, light
constexpr uint8_t kHomeDimAlpha = 60;
constexpr COLORREF kHomeFill = RGB(14,14,20);         // ring segments: dark glass
constexpr uint8_t kHomeFillAlpha = 150;
constexpr COLORREF kHomeHoverFill = RGB(196,198,214); // the segment under the mouse: light glass
constexpr uint8_t kHomeHoverAlpha = 70;
constexpr COLORREF kHomeChipFill = RGB(16,24,46);   // module chips: dark blue glass
constexpr uint8_t kHomeChipAlpha = 170;

struct HomeLayout { float cx, cy, inner, outer, s; };
HomeLayout GetHomeLayout()
{
    RECT r{0, 0, 1280, 720};
    if (overlayWindow) GetClientRect(overlayWindow, &r);
    const float w = (float)std::max<LONG>(r.right, 320), h = (float)std::max<LONG>(r.bottom, 240);
    const float s = std::clamp(std::min(w / 1280.0f, h / 720.0f), 0.55f, 1.6f);
    return {w / 2, h / 2, 74 * s, 196 * s, s};
}

// Which segment (if any) is at window point (mx, my).
HomeItem HomeItemAt(int mx, int my)
{
    const HomeLayout l = GetHomeLayout();
    const float dx = mx - l.cx, dy = my - l.cy, d = std::sqrt(dx * dx + dy * dy);
    if (d < l.inner || d > l.outer + 10 * l.s) return HomeItem::None;
    // Angle clockwise from straight up, 0..360; each segment spans 90 degrees around its direction.
    float a = std::atan2(dx, -dy) * 180.0f / 3.14159265f;
    if (a < 0) a += 360;
    return (HomeItem)(((int)std::floor((a + 45) / 90)) % 4);
}

// One ring segment as a polygon: 90 degrees around `centerDeg` (clockwise from up), with a constant
// pixel gap to its neighbours (the angular cut shrinks with the radius).
void HomeSegment(const HomeLayout& l, int index, float inner, float outer, std::vector<float>& xs, std::vector<float>& ys)
{
    xs.clear(); ys.clear();
    const float pi = 3.14159265f, gap = 5 * l.s;
    const float c = index * 90 * pi / 180;
    const auto point = [&](float r, float a) { xs.push_back(l.cx + r * std::sin(a)); ys.push_back(l.cy - r * std::cos(a)); };
    const float outCut = std::asin(std::min(1.0f, gap / outer)), inCut = std::asin(std::min(1.0f, gap / inner));
    const int steps = 40;
    for (int i = 0; i <= steps; i++)
        point(outer, c - pi / 4 + outCut + (pi / 2 - 2 * outCut) * i / steps);
    for (int i = steps; i >= 0; i--)
        point(inner, c - pi / 4 + inCut + (pi / 2 - 2 * inCut) * i / steps);
}

void HomeStroke(HDC hdc, const std::vector<float>& xs, const std::vector<float>& ys, bool closed, float thick, COLORREF color)
{
    const size_t n = xs.size();
    for (size_t i = 0; i + 1 < n; i++) DrawLineSmooth(hdc, xs[i], ys[i], xs[i + 1], ys[i + 1], thick, color);
    if (closed && n > 2) DrawLineSmooth(hdc, xs[n - 1], ys[n - 1], xs[0], ys[0], thick, color);
}

// Line icons in the ring (white strokes): house, person, gear, pencil. (cx, cy) centre, u = size.
void DrawHomeIcon(HDC hdc, HomeItem item, float cx, float cy, float u, COLORREF color)
{
    const float t = std::max(1.6f, u * 0.075f);
    std::vector<float> xs, ys;
    const auto P = [&](float x, float y) { xs.push_back(cx + x * u); ys.push_back(cy + y * u); };
    const auto flush = [&](bool closed) { HomeStroke(hdc, xs, ys, closed, t, color); xs.clear(); ys.clear(); };
    const float pi = 3.14159265f;
    switch (item)
    {
    case HomeItem::Mods:   // house
        P(-0.56f, 0.02f); P(0, -0.50f); P(0.56f, 0.02f); flush(false);
        P(-0.40f, -0.12f); P(-0.40f, 0.48f); P(0.40f, 0.48f); P(0.40f, -0.12f); flush(false);
        P(-0.12f, 0.48f); P(-0.12f, 0.16f); P(0.12f, 0.16f); P(0.12f, 0.48f); flush(false);
        break;
    case HomeItem::Closet: // person
        for (int i = 0; i < 28; i++) P(0.19f * std::cos(2 * pi * i / 28), -0.27f + 0.19f * std::sin(2 * pi * i / 28));
        flush(true);
        for (int i = 0; i <= 20; i++) P(0.40f * std::cos(pi + pi * i / 20), 0.50f + 0.40f * std::sin(pi + pi * i / 20));
        flush(true);
        break;
    case HomeItem::Settings: // gear: 8 teeth, hole in the middle
        for (int i = 0; i < 8; i++)
        {
            const float a = 2 * pi * i / 8, half = pi / 8;
            P(0.36f * std::cos(a - half * 0.95f), 0.36f * std::sin(a - half * 0.95f));
            P(0.50f * std::cos(a - half * 0.45f), 0.50f * std::sin(a - half * 0.45f));
            P(0.50f * std::cos(a + half * 0.45f), 0.50f * std::sin(a + half * 0.45f));
            P(0.36f * std::cos(a + half * 0.95f), 0.36f * std::sin(a + half * 0.95f));
        }
        flush(true);
        for (int i = 0; i < 24; i++) P(0.15f * std::cos(2 * pi * i / 24), 0.15f * std::sin(2 * pi * i / 24));
        flush(true);
        break;
    case HomeItem::Hud:    // pencil, tip at the lower left
    {
        const float dx = 0.7071f, dy = -0.7071f, nx = 0.7071f, ny = 0.7071f, w = 0.15f;
        const auto A = [&](float along, float side) { P(along * dx + side * nx, along * dy + side * ny); };
        A(-0.62f, 0); A(-0.30f, w); A(0.58f, w); A(0.58f, -w); A(-0.30f, -w); flush(true);
        A(0.36f, w); A(0.36f, -w); flush(false);
        A(-0.30f, w); A(-0.30f, -w); flush(false);
        break;
    }
    default: break;
    }
}

// Steve's face, for when no skin was seen yet (8x8, 0x00RRGGBB).
const uint32_t kDefaultFace[64] = {
    0x2F200D,0x2B1E0D,0x2F1F0F,0x281C0B,0x241808,0x261A0A,0x2B1E0D,0x2A1D0D,
    0x2B1E0D,0x2B1E0D,0x2B1E0D,0x332411,0x422A12,0x3F2A15,0x2C1E0E,0x281C0B,
    0x2B1E0D,0xB6896C,0xBD8E72,0xC69680,0xBD8B72,0xBD8E74,0xAC765A,0x342512,
    0xAA7D66,0xB4846D,0xAA7D66,0xAD806D,0x9C725C,0xBB8972,0x9C694C,0x9C694C,
    0xB4846D,0xFFFFFF,0x523D89,0xB57B67,0xBB8972,0x523D89,0xFFFFFF,0xAA7D66,
    0x9C6346,0xB37B62,0xB78272,0x6A4030,0x6A4030,0xBE886C,0xA26A47,0x805334,
    0x905E43,0x965F40,0x40281A,0x40281A,0x40281A,0x40281A,0x8F5E3E,0x815339,
    0x6F452C,0x6D432A,0x7A4E33,0x83553B,0x83553B,0x7A4E33,0x704B32,0x5E3E2A,
};

// The head picture to show: the player's skin (face + hat) or Steve. Remembers the last skin seen in
// myclient_skin.bin next to the HUD, so it also shows before the game sends one.
std::wstring SkinCachePath() { return GetExecutableDirectory() + L"\\myclient_skin.bin"; }
const std::vector<uint8_t>& CurrentSkin(uint32_t& w, uint32_t& h)
{
    static bool loaded = false;
    static uint32_t saved = 0;
    if (!loaded)
    {
        loaded = true;
        if (FILE* f = _wfopen(SkinCachePath().c_str(), L"rb"))
        {
            uint32_t head[3] = {};
            if (fread(head, sizeof(head), 1, f) == 1 && head[0] == MyClientSkin::Magic &&
                head[1] >= 64 && head[1] <= MyClientSkin::MaxSide && head[2] >= 32 && head[2] <= MyClientSkin::MaxSide)
            {
                std::vector<uint8_t> rgba((size_t)head[1] * head[2] * 4);
                if (fread(rgba.data(), rgba.size(), 1, f) == 1)
                    MinecraftDataProvider::SetSkin(std::move(rgba), head[1], head[2]);
            }
            fclose(f);
        }
        MinecraftDataProvider::GetSkin(w, h, saved);
    }
    uint32_t changes = 0;
    const std::vector<uint8_t>& rgba = MinecraftDataProvider::GetSkin(w, h, changes);
    if (changes != saved && rgba.empty())
    {
        saved = changes;
        _wremove(SkinCachePath().c_str());   // a persona / own-model skin: the default head from now on
    }
    else if (changes != saved)
    {
        saved = changes;
        if (FILE* f = _wfopen(SkinCachePath().c_str(), L"wb"))
        {
            const uint32_t head[3] = {MyClientSkin::Magic, w, h};
            fwrite(head, sizeof(head), 1, f);
            fwrite(rgba.data(), rgba.size(), 1, f);
            fclose(f);
        }
    }
    return rgba;
}

// Pixel-art picture (side*side 0x00RRGGBB) as solid cells at (x, y), `cell` pixels each. Colours equal
// to a translucent fill are nudged by one so they stay solid.
void DrawPixelArt(HDC hdc, const std::vector<uint32_t>& colors, int side, int x, int y, int cell)
{
    for (int j = 0; j < side; j++)
        for (int i = 0; i < side; i++)
        {
            uint32_t c = colors[(size_t)j * side + i];
            COLORREF rgb = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            if (rgb == kHomeDim || rgb == kHomeFill || rgb == kHomeHoverFill || rgb == RGB(255,0,255))
                rgb = RGB(GetRValue(rgb), GetGValue(rgb) ^ 1, GetBValue(rgb));
            HBRUSH b = CreateSolidBrush(rgb);
            RECT r{x + i * cell, y + j * cell, x + (i + 1) * cell, y + (j + 1) * cell};
            FillRect(hdc, &r, b);
            DeleteObject(b);
        }
}

// The head as 0x00RRGGBB colours, side*side.
std::vector<uint32_t> HeadColors(int& side)
{
    uint32_t w = 0, h = 0;
    const std::vector<uint8_t>& skin = CurrentSkin(w, h);
    std::vector<uint8_t> head;
    if (!skin.empty() && MyClientSkin::Head(skin.data(), w, h, head))
    {
        side = (int)(w / 8);
        std::vector<uint32_t> out((size_t)side * side);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = ((uint32_t)head[i * 4] << 16) | ((uint32_t)head[i * 4 + 1] << 8) | head[i * 4 + 2];
        return out;
    }
    side = 8;
    return std::vector<uint32_t>(kDefaultFace, kDefaultFace + 64);
}

// ---- The head in the middle: a 3D cube with the skin's head (and hat layer) that turns toward the mouse ----

// The skin to wrap the cube in: the player's, or Steve's face on a plain head (no skin seen yet).
const std::vector<uint8_t>& HeadSkin(uint32_t& w, uint32_t& h)
{
    const std::vector<uint8_t>& skin = CurrentSkin(w, h);
    if (!skin.empty() && w >= 64 && w % 64 == 0 && (h == w || h == w / 2)) return skin;
    static std::vector<uint8_t> steve;
    if (steve.empty())
    {
        steve.assign(64 * 32 * 4, 0);
        const auto put = [&](int x, int y, uint32_t c) {
            uint8_t* p = &steve[((size_t)y * 64 + x) * 4];
            p[0] = (c >> 16) & 0xFF; p[1] = (c >> 8) & 0xFF; p[2] = c & 0xFF; p[3] = 255;
        };
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 32; x++)
            {
                const bool front = x >= 8 && x < 16 && y >= 8;
                // Top, back and the top two rows of the sides: hair; the rest of the sides and the chin: skin.
                const uint32_t c = front ? kDefaultFace[(y - 8) * 8 + (x - 8)] : (y < 10 || x >= 24) ? 0x2B1E0D : 0xAA7D66;
                put(x, y, c);
            }
    }
    w = 64; h = 32;
    return steve;
}

// Texel of the head (layer 0) or hat (layer 1) where a ray hits the face `axis` (0 x, 1 y, 2 z) on side `sign`,
// at (x, y, z) in -1..1 box units. False for a see-through texel.
bool HeadTexel(const std::vector<uint8_t>& skin, uint32_t w, uint32_t h, int layer, int axis, int sign,
               float x, float y, float z, uint32_t& rgb)
{
    // Face origins in the 64x64 layout (8 units per face), and the face coordinates 0..8 inside it.
    float u = 0, v = 0; int ou = 0, ov = 8;
    const auto cell = [](float t) { return std::clamp((t + 1) * 4, 0.0f, 7.999f); };
    if (axis == 2 && sign > 0)      { ou = 8;  u = cell(x);  v = cell(-y); }   // front
    else if (axis == 2)             { ou = 24; u = cell(-x); v = cell(-y); }   // back
    else if (axis == 0 && sign < 0) { ou = 0;  u = cell(z);  v = cell(-y); }   // the player's right (our left)
    else if (axis == 0)             { ou = 16; u = cell(-z); v = cell(-y); }   // the player's left
    else if (sign > 0)              { ou = 8;  ov = 0; u = cell(x); v = cell(z); }   // top
    else                            { ou = 16; ov = 0; u = cell(x); v = cell(z); }   // bottom
    if (layer) ou += 32;
    const uint32_t k = w / 64;
    const uint32_t tx = (uint32_t)((ou + u) * k), ty = (uint32_t)((ov + v) * k);
    if (tx >= w || ty >= h) return false;
    const uint8_t* p = &skin[((size_t)ty * w + tx) * 4];
    if (p[3] < 128) return false;
    rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return true;
}

// Ray along -z (screen into the page) against the box |x|,|y|,|z| <= half, in the head's own space.
// Returns the entry (near) and exit (far) hits as t and face axis/sign; false on a miss.
struct BoxHit { float t; int axis, sign; };
bool RayBox(const float o[3], const float d[3], float half, BoxHit& nearHit, BoxHit& farHit)
{
    float tNear = -1e9f, tFar = 1e9f;
    int nearAxis = 0, farAxis = 0, nearSign = 1, farSign = 1;
    for (int a = 0; a < 3; a++)
    {
        if (std::fabs(d[a]) < 1e-6f)
        {
            if (o[a] < -half || o[a] > half) return false;
            continue;
        }
        float t1 = (-half - o[a]) / d[a], t2 = (half - o[a]) / d[a];
        int s1 = -1, s2 = 1;
        if (t1 > t2) { std::swap(t1, t2); std::swap(s1, s2); }
        if (t1 > tNear) { tNear = t1; nearAxis = a; nearSign = s1; }
        if (t2 < tFar) { tFar = t2; farAxis = a; farSign = s2; }
    }
    if (tNear > tFar) return false;
    nearHit = {tNear, nearAxis, nearSign};
    farHit = {tFar, farAxis, farSign};
    return true;
}

// Draws the head cube centred at (cx, cy), `size` pixels across its front face, turned by yaw (+ = to our
// right) and pitch (+ = looking down), in degrees. Software ray casting, 2x2 samples per pixel inside the cube's
// projected box (crisp edges, no blending with the translucent dim behind it).
void DrawHead3D(HDC hdc, float cx, float cy, float size, float yawDeg, float pitchDeg)
{
    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    if (!px || size < 4) return;
    uint32_t w = 0, h = 0;
    const std::vector<uint8_t>& skin = HeadSkin(w, h);
    if (skin.empty()) return;

    const float yaw = yawDeg * 3.14159265f / 180, pitch = pitchDeg * 3.14159265f / 180;
    const float cyw = std::cos(yaw), syw = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    // World = Ry(yaw) * Rx(pitch) * local; local = Rx(-pitch) * Ry(-yaw) * world.
    const auto toLocal = [&](const float in[3], float out[3]) {
        const float x1 = in[0] * cyw - in[2] * syw, z1 = in[0] * syw + in[2] * cyw, y1 = in[1];
        out[0] = x1;
        out[1] = y1 * cp + z1 * sp;
        out[2] = -y1 * sp + z1 * cp;
    };
    const auto toWorldNormal = [&](int axis, int sign, float out[3]) {
        float n[3] = {0, 0, 0}; n[axis] = (float)sign;
        const float y1 = n[1] * cp - n[2] * sp, z1 = n[1] * sp + n[2] * cp, x1 = n[0];
        out[0] = x1 * cyw + z1 * syw; out[1] = y1; out[2] = -x1 * syw + z1 * cyw;
    };
    // Light from the upper left, in front: faces turned away are darker.
    const float L[3] = {-0.35f, 0.55f, 0.76f};
    const auto shade = [&](int axis, int sign, bool inside) {
        float n[3]; toWorldNormal(axis, sign, n);
        if (inside) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        return 0.58f + 0.42f * std::max(0.0f, n[0] * L[0] + n[1] * L[1] + n[2] * L[2]);
    };

    POINT o{0, 0};
    LPtoDP(hdc, &o, 1);
    const float unit = size / 2;      // pixels per box unit (the head box is -1..1)
    const float hat = 1.125f;         // the hat layer is half a skin pixel bigger on every side
    // Screen box of the turned hat cube: its 8 corners projected.
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int c = 0; c < 8; c++)
    {
        const float lx = (c & 1 ? hat : -hat), ly = (c & 2 ? hat : -hat), lz = (c & 4 ? hat : -hat);
        const float y1 = ly * cp - lz * sp, z1 = ly * sp + lz * cp;
        const float wx = lx * cyw + z1 * syw, wy = y1;
        minX = std::min(minX, wx); maxX = std::max(maxX, wx); minY = std::min(minY, wy); maxY = std::max(maxY, wy);
    }
    const int left = std::max(0, (int)std::floor(cx + o.x + minX * unit) - 1), right = std::min(width, (int)std::ceil(cx + o.x + maxX * unit) + 1);
    const int top = std::max(0, (int)std::floor(cy + o.y - maxY * unit) - 1), bottom = std::min(height, (int)std::ceil(cy + o.y - minY * unit) + 1);
    float dir[3];
    { const float dw[3] = {0, 0, -1}; toLocal(dw, dir); }
    constexpr int kSub = 2;
    for (int py = top; py < bottom; py++)
        for (int pxl = left; pxl < right; pxl++)
        {
            uint32_t r = 0, g = 0, b = 0, hits = 0;
            for (int sy = 0; sy < kSub; sy++)
                for (int sx = 0; sx < kSub; sx++)
                {
                    const float wx = (pxl - o.x + (sx + 0.5f) / kSub - cx) / unit;
                    const float wy = -(py - o.y + (sy + 0.5f) / kSub - cy) / unit;
                    const float ow[3] = {wx, wy, 4};
                    float ol[3]; toLocal(ow, ol);
                    BoxHit nearHat, farHat, nearHead, farHead;
                    if (!RayBox(ol, dir, hat, nearHat, farHat)) continue;
                    uint32_t rgb = 0; float light = 1; bool found = false;
                    const auto at = [&](const BoxHit& hit, float half, float p[3]) {
                        for (int a = 0; a < 3; a++) p[a] = (ol[a] + dir[a] * hit.t) / half;
                    };
                    float p[3];
                    at(nearHat, hat, p);
                    if (HeadTexel(skin, w, h, 1, nearHat.axis, nearHat.sign, p[0], p[1], p[2], rgb))
                    { found = true; light = shade(nearHat.axis, nearHat.sign, false); }
                    else if (RayBox(ol, dir, 1.0f, nearHead, farHead))
                    {
                        at(nearHead, 1.0f, p);
                        if (!HeadTexel(skin, w, h, 0, nearHead.axis, nearHead.sign, p[0], p[1], p[2], rgb))
                            rgb = 0x202020;   // a see-through base texel (odd skins): dark, never a hole
                        found = true;
                        light = shade(nearHead.axis, nearHead.sign, false);
                    }
                    else
                    {
                        // Past the head: the inside of the hat's far side (e.g. hair seen past the face's edge).
                        at(farHat, hat, p);
                        if (HeadTexel(skin, w, h, 1, farHat.axis, farHat.sign, p[0], p[1], p[2], rgb))
                        { found = true; light = shade(farHat.axis, farHat.sign, true) * 0.8f; }
                    }
                    if (!found) continue;
                    r += (uint32_t)(((rgb >> 16) & 0xFF) * light);
                    g += (uint32_t)(((rgb >> 8) & 0xFF) * light);
                    b += (uint32_t)((rgb & 0xFF) * light);
                    hits++;
                }
            if (hits * 2 < kSub * kSub) continue;   // less than half covered: leave the background
            COLORREF c = RGB(r / hits, g / hits, b / hits);
            // Never one of the translucent key colours (those pixels would turn see-through).
            if (c == kHomeDim || c == kHomeFill || c == kHomeHoverFill || c == RGB(255,0,255) || c == kHomeChipFill)
                c = RGB(GetRValue(c), GetGValue(c) ^ 1, GetBValue(c));
            px[(size_t)py * width + pxl] = ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | GetBValue(c);
        }
}

// The head's turn follows the mouse, eased so it glides; a slow idle sway keeps it alive when the mouse rests.
void HeadAngles(const HomeLayout& l, float& yaw, float& pitch)
{
    static float curYaw = 0, curPitch = 0;
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    const float dt = last ? std::min(0.1f, (now - last) / 1000.0f) : 0.0f;
    last = now;

    float targetYaw = 0, targetPitch = 0;
    POINT cursor;
    if (overlayWindow && GetCursorPos(&cursor) && ScreenToClient(overlayWindow, &cursor))
    {
        const float dx = (cursor.x - l.cx) / std::max(1.0f, l.cx), dy = (cursor.y - l.cy) / std::max(1.0f, l.cy);
        targetYaw = std::clamp(dx * 55.0f, -42.0f, 42.0f);
        targetPitch = std::clamp(dy * 38.0f, -26.0f, 26.0f);
    }
    const float t = now / 1000.0f;
    targetYaw += 4.0f * std::sin(t * 0.9f);
    targetPitch += 2.0f * std::sin(t * 0.7f + 1.0f);
    const float k = 1.0f - std::exp(-dt * 9.0f);
    curYaw += (targetYaw - curYaw) * k;
    curPitch += (targetPitch - curPitch) * k;
    yaw = curYaw; pitch = curPitch;
}


// ---- Module chips: what is switched on, in four groups around the ring (like Blueberry's G menu) ----
// Upper left: HUD elements. Lower left: game helpers. Upper right: visuals. Lower right: performance / client.
enum class ChipGroup { Hud = 0, Game = 1, Visual = 2, System = 3 };
ChipGroup FeatureChipGroup(DetailFeature f)
{
    if (FeatureIsHud(f)) return ChipGroup::Hud;
    switch (f)
    {
    case DetailFeature::HiveUtils: case DetailFeature::Replay: case DetailFeature::FastInventory:
    case DetailFeature::AutoRq: case DetailFeature::PartyAutoAccept:
        return ChipGroup::Game;
    case DetailFeature::Lite: case DetailFeature::FrameGen: case DetailFeature::MenuSounds:
        return ChipGroup::System;
    default:
        return ChipGroup::Visual;
    }
}

struct HomeChip { DetailFeature feature; ChipGroup group; RECT rect; int more; };   // more > 0: "+N more"

std::vector<HomeChip> HomeChips(HDC hdc)
{
    const HomeLayout l = GetHomeLayout();
    std::vector<DetailFeature> groups[4];
    for (int i = 0; i < kFeatureCount; i++)
        if (FeatureEnabled(kFeatureList[i]))
            groups[(int)FeatureChipGroup(kFeatureList[i])].push_back(kFeatureList[i]);

    const int chipH = (int)std::lround(34 * l.s), pitch = (int)std::lround(42 * l.s);
    const int textSize = std::max(11, (int)std::lround(15 * l.s));
    const int padL = (int)std::lround(44 * l.s), padR = (int)std::lround(18 * l.s);
    const int gapToRing = (int)std::lround(110 * l.s);
    // Upper groups start level with the top of the ring; lower groups just under the middle.
    const int upperTop = (int)std::lround(l.cy - l.outer), lowerTop = (int)std::lround(l.cy + 40 * l.s);
    const int upperRows = std::max(1, (lowerTop - upperTop - pitch / 2) / pitch);
    const int lowerRows = std::max(1, (int)((l.cy + l.outer + 20 * l.s - lowerTop) / pitch));

    std::vector<HomeChip> chips;
    for (int g = 0; g < 4; g++)
    {
        const bool leftSide = g < 2, upper = g == 0 || g == 2;
        const int rows = upper ? upperRows : lowerRows;
        const int top = upper ? upperTop : lowerTop;
        const int count = (int)groups[g].size();
        const int shown = count > rows ? rows - 1 : count;
        for (int i = 0; i <= shown && i < rows; i++)
        {
            const bool moreChip = i == shown;
            if (moreChip && count <= rows) break;
            const std::wstring text = moreChip ? L"+" + std::to_wstring(count - shown) + L" more" : FeatureTitle(groups[g][i]);
            const int w = TextWidth(hdc, text, textSize) + (moreChip ? padR * 2 : padL + padR);
            const int y = top + i * pitch;
            const int x = leftSide ? (int)std::lround(l.cx - l.outer) - gapToRing - w : (int)std::lround(l.cx + l.outer) + gapToRing;
            chips.push_back({moreChip ? DetailFeature::None : groups[g][i], (ChipGroup)g, RECT{x, y, x + w, y + chipH},
                             moreChip ? count - shown : 0});
        }
    }
    return chips;
}

int homeChipHover = -1;   // index into HomeChips, -1 = none

// Chip at window point (mx, my), or -1.
int HomeChipAt(int mx, int my)
{
    if (!overlayWindow) return -1;
    HDC dc = GetDC(overlayWindow);
    if (!dc) return -1;
    const std::vector<HomeChip> chips = HomeChips(dc);
    ReleaseDC(overlayWindow, dc);
    for (size_t i = 0; i < chips.size(); i++)
    {
        const RECT& r = chips[i].rect;
        if (mx >= r.left && mx < r.right && my >= r.top && my < r.bottom) return (int)i;
    }
    return -1;
}

// Small line icons for the chip groups: monitor (HUD), gamepad (game), eye (visual), gear (system).
void DrawChipIcon(HDC hdc, ChipGroup group, float cx, float cy, float u, COLORREF color)
{
    const float t = std::max(1.4f, u * 0.09f);
    std::vector<float> xs, ys;
    const auto P = [&](float x, float y) { xs.push_back(cx + x * u); ys.push_back(cy + y * u); };
    const auto flush = [&](bool closed) { HomeStroke(hdc, xs, ys, closed, t, color); xs.clear(); ys.clear(); };
    const float pi = 3.14159265f;
    switch (group)
    {
    case ChipGroup::Hud:   // monitor on a stand
        P(-0.5f, -0.38f); P(0.5f, -0.38f); P(0.5f, 0.22f); P(-0.5f, 0.22f); flush(true);
        P(0, 0.22f); P(0, 0.42f); flush(false);
        P(-0.22f, 0.44f); P(0.22f, 0.44f); flush(false);
        break;
    case ChipGroup::Game:  // gamepad: a rounded body, a d-pad and two buttons
        for (int i = 0; i <= 16; i++) { const float a = pi / 2 + pi * i / 16; P(-0.24f + 0.26f * std::cos(a), 0.26f * std::sin(a)); }
        for (int i = 0; i <= 16; i++) { const float a = -pi / 2 + pi * i / 16; P(0.24f + 0.26f * std::cos(a), 0.26f * std::sin(a)); }
        flush(true);
        P(-0.36f, 0); P(-0.14f, 0); flush(false);
        P(-0.25f, -0.11f); P(-0.25f, 0.11f); flush(false);
        P(0.18f, -0.05f); P(0.21f, -0.05f); flush(false);
        P(0.30f, 0.06f); P(0.33f, 0.06f); flush(false);
        break;
    case ChipGroup::Visual: // eye
        for (int i = 0; i <= 20; i++) { const float a = pi * i / 20; P(-0.52f * std::cos(a), -0.30f * std::sin(a)); }
        for (int i = 0; i <= 20; i++) { const float a = pi * i / 20; P(0.52f * std::cos(a), 0.30f * std::sin(a)); }
        flush(true);
        for (int i = 0; i < 16; i++) P(0.13f * std::cos(2 * pi * i / 16), 0.13f * std::sin(2 * pi * i / 16));
        flush(true);
        break;
    default:
        DrawHomeIcon(hdc, HomeItem::Settings, cx, cy, u * 1.05f, color);
        break;
    }
}

void DrawHomeChips(HDC hdc)
{
    const HomeLayout l = GetHomeLayout();
    const std::vector<HomeChip> chips = HomeChips(hdc);
    const int textSize = std::max(11, (int)std::lround(15 * l.s));
    for (size_t i = 0; i < chips.size(); i++)
    {
        const HomeChip& c = chips[i];
        const RECT& r = c.rect;
        const int w = r.right - r.left, h = r.bottom - r.top;
        const bool hover = (int)i == homeChipHover;
        DrawRoundBox(hdc, r.left, r.top, w, h, h / 2, hover ? kHomeHoverFill : kHomeChipFill);
        if (c.more)
        {
            DrawTextCentered(hdc, r.left, r.top, w, h, L"+" + std::to_wstring(c.more) + L" more", RGB(220,222,232), textSize);
            continue;
        }
        DrawChipIcon(hdc, c.group, r.left + 24 * l.s, r.top + h / 2.0f, 18 * l.s, RGB(255,255,255));
        DrawTextSimple(hdc, r.left + (int)std::lround(44 * l.s), r.top + h / 2 - TextMiddle(hdc, textSize), FeatureTitle(c.feature),
                       RGB(255,255,255), textSize);
    }
}

// ---- Mods page: a glass panel over the blurred game (light or dark glass) ----
// Left: icon buttons (Mods, Settings, Closet, Presets, light/dark, Edit HUD). Top: back, the tabs (Quick Access =
// the pinned modules, Visual, HUD, Input, Misc) and the search box. Below: the modules in two columns, each row
// with its name, a gear (its settings page) and a switch; a switched-on row is blue. Right-click a row: pin / unpin.
// Drawn in window coordinates like the home screen and scaled with it. The glass boxes are translucent
// (OverlayAlpha) and a separate window under the overlay blurs the game behind the panel.
void OpenHudEditor();   // defined with OpenMenu below

bool GlassModsShown()
{
    return !hudEditMode && !replaysOpen && selectedCategory == MenuCategory::Feature && detailFeature == DetailFeature::None;
}

bool GlassClosetShown()
{
    return !hudEditMode && !replaysOpen && selectedCategory == MenuCategory::Closet;
}

// Settings, Presets and a module's settings page: their own layout (old menu coordinates, the content was
// x 190..920, y 62..590) drawn inside the glass frame with glass colours.
bool GlassContentShown()
{
    return menuOpen && !hudEditMode && !replaysOpen &&
           (selectedCategory == MenuCategory::Settings || selectedCategory == MenuCategory::Presets ||
            (selectedCategory == MenuCategory::Feature && detailFeature != DetailFeature::None));
}

bool GlassPageShown() { return GlassModsShown() || GlassClosetShown() || GlassContentShown(); }

ModsTab ModsTabOf(DetailFeature f)
{
    if (FeatureIsHud(f)) return ModsTab::Hud;
    switch (f)
    {
    case DetailFeature::FastInventory: case DetailFeature::MenuOpen:
        return ModsTab::Input;
    case DetailFeature::HiveUtils: case DetailFeature::Replay: case DetailFeature::Nick: case DetailFeature::Lite:
    case DetailFeature::FrameGen: case DetailFeature::MenuSounds: case DetailFeature::DebugMenu:
        return ModsTab::Misc;
    default:
        return ModsTab::Visual;
    }
}

bool FeaturePinned(DetailFeature f)
{
    return std::find(pinnedFeatures.begin(), pinnedFeatures.end(), f) != pinnedFeatures.end();
}

void TogglePin(DetailFeature f)
{
    const auto it = std::find(pinnedFeatures.begin(), pinnedFeatures.end(), f);
    if (it != pinnedFeatures.end()) pinnedFeatures.erase(it);
    else pinnedFeatures.push_back(f);
    SaveConfig();
}

// The rows of the current tab (or every module matching the search): pinned first, then by name.
std::vector<DetailFeature> GlassFeatures()
{
    std::vector<DetailFeature> list;
    const bool searching = !searchQuery.empty();
    for (int i = 0; i < kFeatureCount; i++)
    {
        const DetailFeature f = kFeatureList[i];
        if (f == DetailFeature::MenuOpen) continue;   // its key is on the settings page (the gear)
        if (f == DetailFeature::FrameGen) continue;   // in Optimization's page
        const bool wanted = searching ? ContainsNoCase(FeatureTitle(f), searchQuery)
                                      : (modsTab == ModsTab::Quick ? FeaturePinned(f) : ModsTabOf(f) == modsTab);
        if (wanted) list.push_back(f);
    }
    std::sort(list.begin(), list.end(), [](DetailFeature a, DetailFeature b)
    {
        const bool pa = FeaturePinned(a), pb = FeaturePinned(b);
        if (pa != pb) return pa;
        return _wcsicmp(FeatureTitle(a).c_str(), FeatureTitle(b).c_str()) < 0;
    });
    return list;
}

// Layout in panel units (1100 x 600), scaled by s and placed in the middle of the window.
constexpr float kGlassW = 1100, kGlassH = 600;
constexpr int kGlassRows = 6;   // visible rows of two modules
struct GlassLayout
{
    float s; int x, y, w, h;
    int L(float u) const { return (int)std::lround(u * s); }
    RECT R(float ux, float uy, float uw, float uh) const
    {
        return RECT{x + L(ux), y + L(uy), x + L(ux + uw), y + L(uy + uh)};
    }
};

GlassLayout GetGlassLayout()
{
    const HomeLayout l = GetHomeLayout();
    const float s = std::min({l.s, l.cx * 2 * 0.96f / kGlassW, l.cy * 2 * 0.94f / kGlassH});
    GlassLayout g{s, 0, 0, (int)std::lround(kGlassW * s), (int)std::lround(kGlassH * s)};
    g.x = (int)std::lround(l.cx - g.w / 2.0f);
    g.y = (int)std::lround(l.cy - g.h / 2.0f);
    return g;
}

// Where the old layout's (0, 0) goes so its content sits right of the back button, centred in the panel.
void GlassContentOrigin(int& ox, int& oy)
{
    const GlassLayout g = GetGlassLayout();
    ox = g.x + g.L(186) + std::max(0, (g.L(874) - 730) / 2) - 190;
    oy = g.y + g.L(62) - 62;
}

enum class GlassRail { Mods = 0, Settings = 1, Closet = 2, Presets = 3, Theme = 4, EditHud = 5 };
constexpr int kGlassRailCount = 6;
RECT GlassRailRect(const GlassLayout& g, int i)
{
    const float y = i < 4 ? 62 + i * 82.0f : (i == 4 ? kGlassH - 96 - 82 : kGlassH - 96);
    return g.R(30, y, 66, 66);
}
RECT GlassCloseRect(const GlassLayout& g) { return g.R(kGlassW - 56, 16, 36, 36); }
RECT GlassBackRect(const GlassLayout& g) { return g.R(126, 62, 48, 40); }
RECT GlassSearchRect(const GlassLayout& g) { return g.R(kGlassW - 40 - 210, 62, 210, 40); }

const wchar_t* const kModsTabNames[kModsTabCount] = { L"QUICK ACCESS", L"VISUAL", L"HUD", L"INPUT", L"MISC" };
RECT GlassTabRect(const GlassLayout& g, int i)
{
    static const float widths[kModsTabCount] = {160, 108, 86, 100, 92};
    float x = 186;
    for (int k = 0; k < i; k++) x += widths[k] + 10;
    return g.R(x, 62, widths[i], 40);
}

// Row `pos` of the list (two columns), or an empty rect when it is scrolled out of view.
RECT GlassRowRect(const GlassLayout& g, int pos)
{
    const int row = pos / 2 - featureScrollRow;
    if (row < 0 || row >= kGlassRows) return RECT{0, 0, 0, 0};
    return g.R(126 + (pos % 2) * 474.0f, 118 + row * 68.0f, 460, 56);
}
RECT GlassGearRect(const GlassLayout& g, const RECT& row)
{
    const int size = g.L(34), cy = (row.top + row.bottom) / 2;
    return RECT{row.right - g.L(116), cy - size / 2, row.right - g.L(116) + size, cy + size / 2};
}
RECT GlassSwitchRect(const GlassLayout& g, const RECT& row)
{
    const int h = g.L(28), cy = (row.top + row.bottom) / 2;
    return RECT{row.right - g.L(68), cy - h / 2, row.right - g.L(18), cy - h / 2 + h};
}

void ClampGlassScroll()
{
    const int rows = ((int)GlassFeatures().size() + 1) / 2;
    featureScrollRow = std::clamp(featureScrollRow, 0, std::max(0, rows - kGlassRows));
}

bool ContainsPoint(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

// A glass box: drawn hard-edged in its colour (see OverlayAlpha::Glass), made translucent and smoothed later.
void GlassBox(HDC hdc, const RECT& r, int radius, COLORREF color, uint8_t alpha)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    alpha = (uint8_t)(alpha * std::clamp(glassOpacity, 20, 100) / 100);   // Settings: UI opacity
    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    if (!px)
    {
        DrawRoundBox(hdc, r.left, r.top, w - 1, h - 1, radius, color);
        return;
    }
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    const uint32_t rgb = ((uint32_t)GetRValue(color) << 16) | ((uint32_t)GetGValue(color) << 8) | GetBValue(color);
    const OverlayAlpha::Glass glass{r.left, r.top, r.right, r.bottom, radius, rgb, alpha};
    HardRoundBox(hdc, px, width, height, glass, color);
    translucentRects.push_back({r.left, r.top, r.right, r.bottom, rgb, alpha});
    glassShapes.push_back(glass);
}

// Line icons of the page (centre cx, cy; size u).
enum class GlassIcon { Back, Close, Save, Pin, Puzzle, Moon, Sun };
void DrawGlassIcon(HDC hdc, GlassIcon icon, float cx, float cy, float u, COLORREF color)
{
    const float t = std::max(1.6f, u * 0.08f), pi = 3.14159265f;
    std::vector<float> xs, ys;
    const auto P = [&](float x, float y) { xs.push_back(cx + x * u); ys.push_back(cy + y * u); };
    const auto flush = [&](bool closed) { HomeStroke(hdc, xs, ys, closed, t, color); xs.clear(); ys.clear(); };
    const auto fill = [&]() { if (xs.size() > 2) FillPolygonSmooth(hdc, xs.data(), ys.data(), (int)xs.size(), color); xs.clear(); ys.clear(); };
    switch (icon)
    {
    case GlassIcon::Back:
        P(0.45f, 0); P(-0.45f, 0); flush(false);
        P(-0.05f, -0.38f); P(-0.45f, 0); P(-0.05f, 0.38f); flush(false);
        break;
    case GlassIcon::Close:
        P(-0.36f, -0.36f); P(0.36f, 0.36f); flush(false);
        P(0.36f, -0.36f); P(-0.36f, 0.36f); flush(false);
        break;
    case GlassIcon::Save:   // floppy disk
        P(-0.42f, -0.42f); P(0.26f, -0.42f); P(0.42f, -0.26f); P(0.42f, 0.42f); P(-0.42f, 0.42f); flush(true);
        P(-0.24f, -0.42f); P(-0.24f, -0.16f); P(0.16f, -0.16f); P(0.16f, -0.42f); flush(false);
        P(-0.24f, 0.42f); P(-0.24f, 0.12f); P(0.24f, 0.12f); P(0.24f, 0.42f); flush(false);
        break;
    case GlassIcon::Pin:    // push pin, tilted, point at the lower left
    {
        // rotated 45 degrees: the pin's axis (0, 1) points to the lower left
        const float c = 0.7071f;
        const auto Q = [&](float x, float y) { P(x * c - y * c, x * c + y * c); };
        Q(-0.30f, -0.52f); Q(0.30f, -0.52f); Q(0.30f, -0.40f); Q(0.19f, -0.40f); Q(0.19f, -0.06f);
        Q(0.38f, 0.08f); Q(-0.38f, 0.08f); Q(-0.19f, -0.06f); Q(-0.19f, -0.40f); Q(-0.30f, -0.40f);
        fill();
        Q(0, 0.08f); Q(0, 0.52f); flush(false);
        break;
    }
    case GlassIcon::Puzzle:
        P(-0.42f, -0.28f); P(-0.12f, -0.28f);
        for (int i = 0; i <= 10; i++) { const float a = pi + pi * i / 10; P(0.12f * std::cos(a), -0.28f + 0.12f * std::sin(a)); }
        P(0.42f, -0.28f); P(0.42f, -0.12f);
        for (int i = 0; i <= 10; i++) { const float a = -pi / 2 + pi * i / 10; P(0.42f + 0.12f * std::cos(a), 0.12f * std::sin(a)); }
        P(0.42f, 0.44f); P(-0.42f, 0.44f); flush(true);
        break;
    case GlassIcon::Moon:   // crescent: an arc of one circle closed by an arc of a second one
    {
        const float r1 = 0.42f, r2 = 0.3245f;
        for (int i = 0; i <= 28; i++) { const float a = (40 + 280.0f * i / 28) * pi / 180; P(r1 * std::cos(a), r1 * std::sin(a)); }
        for (int i = 0; i <= 16; i++) { const float a = (236.3f - 112.6f * i / 16) * pi / 180; P(0.5f + r2 * std::cos(a), r2 * std::sin(a)); }
        fill();
        break;
    }
    case GlassIcon::Sun:
        for (int i = 0; i < 20; i++) P(0.2f * std::cos(2 * pi * i / 20), 0.2f * std::sin(2 * pi * i / 20));
        fill();
        for (int i = 0; i < 8; i++)
        {
            const float a = 2 * pi * i / 8;
            P(0.32f * std::cos(a), 0.32f * std::sin(a)); P(0.46f * std::cos(a), 0.46f * std::sin(a)); flush(false);
        }
        break;
    }
}

// Gear, eye, monitor and gamepad come from the home screen's icon sets.
void DrawTabIcon(HDC hdc, int tab, float cx, float cy, float u, COLORREF color)
{
    switch ((ModsTab)tab)
    {
    case ModsTab::Quick:  DrawGlassIcon(hdc, GlassIcon::Pin, cx, cy, u, color); break;
    case ModsTab::Visual: DrawChipIcon(hdc, ChipGroup::Visual, cx, cy, u, color); break;
    case ModsTab::Hud:    DrawChipIcon(hdc, ChipGroup::Hud, cx, cy, u, color); break;
    case ModsTab::Input:  DrawChipIcon(hdc, ChipGroup::Game, cx, cy, u, color); break;
    default:              DrawGlassIcon(hdc, GlassIcon::Puzzle, cx, cy, u, color); break;
    }
}

// `onAccentRow`: the switch sits on a switched-on (accent) row, so it is lit with a lighter accent.
void DrawGlassSwitch(HDC hdc, const RECT& r, bool on, const GlassColors& c, bool onAccentRow)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    DrawRoundBox(hdc, r.left, r.top, w - 1, h - 1, h / 2, on ? (onAccentRow ? GlassAccentLight() : GlassAccent()) : c.track);
    const int pad = std::max(2, h / 8), knob = h - 2 * pad;
    DrawRoundBox(hdc, on ? r.right - pad - knob : r.left + pad, r.top + pad, knob - 1, knob - 1, knob / 2, RGB(255,255,255));
}

// The parts every glass page shares: the panel, the close button and the left column of icon buttons
// (`current` is lit), plus the back button at the top left.
void DrawGlassFrame(HDC hdc, const GlassLayout& g, const GlassColors& c, GlassRail current)
{
    const RECT panel{g.x, g.y, g.x + g.w, g.y + g.h};

    // Drop shadow (Settings: its strength): rings of dark glass around and a little below the panel, fainter
    // outwards. Each ring has its own colour so OverlayAlpha tells them apart; the panel covers their middle.
    if (glassShadow > 0)
    {
        constexpr int kRings = 6;
        for (int k = 0; k < kRings; k++)
        {
            const int grow = g.L(3.0f * (kRings - k)), drop = g.L(6);
            const int alpha = glassShadow * 150 / 100 * (k + 1) / kRings;
            GlassBox(hdc, RECT{panel.left - grow, panel.top - grow + drop, panel.right + grow, panel.bottom + grow + drop},
                     g.L(28) + grow, RGB(0,0,1 + k), (uint8_t)std::clamp(alpha, 1, 255));
        }
    }

    // Panel: a light rim, then the glass.
    const int rim = std::max(1, g.L(1.5f));
    GlassBox(hdc, panel, g.L(28), c.border, c.borderA);
    GlassBox(hdc, RECT{panel.left + rim, panel.top + rim, panel.right - rim, panel.bottom - rim}, g.L(28) - rim, c.panel, c.panelA);

    {
        const RECT r = GlassCloseRect(g);
        DrawGlassIcon(hdc, GlassIcon::Close, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, (float)g.L(26), c.icon);
    }

    // Left column of icon buttons.
    for (int i = 0; i < kGlassRailCount; i++)
    {
        const RECT r = GlassRailRect(g, i);
        const bool active = i == (int)current;
        GlassBox(hdc, r, g.L(16), active ? GlassAccent() : c.fill, active ? kGlassAccentAlpha : c.fillA);
        const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f, u = (float)g.L(30);
        const COLORREF ic = active ? RGB(255,255,255) : c.icon;
        switch ((GlassRail)i)
        {
        case GlassRail::Mods:     DrawHomeIcon(hdc, HomeItem::Mods, cx, cy, u, ic); break;
        case GlassRail::Settings: DrawHomeIcon(hdc, HomeItem::Settings, cx, cy, u * 1.1f, ic); break;
        case GlassRail::Closet:   DrawHomeIcon(hdc, HomeItem::Closet, cx, cy - u * 0.1f, u, ic); break;
        case GlassRail::Presets:  DrawGlassIcon(hdc, GlassIcon::Save, cx, cy, u * 0.95f, ic); break;
        case GlassRail::Theme:    DrawGlassIcon(hdc, glassDark ? GlassIcon::Sun : GlassIcon::Moon, cx, cy, u, ic); break;
        case GlassRail::EditHud:  DrawHomeIcon(hdc, HomeItem::Hud, cx, cy, u, ic); break;
        }
    }

    {
        const RECT r = GlassBackRect(g);
        GlassBox(hdc, r, g.L(12), c.fill, c.fillA);
        DrawGlassIcon(hdc, GlassIcon::Back, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, (float)g.L(24), c.icon);
    }
}

void DrawGlassMods(HDC hdc)
{
    const GlassLayout g = GetGlassLayout();
    const GlassColors c = GlassTheme();
    const int rim = std::max(1, g.L(1.5f));
    DrawGlassFrame(hdc, g, c, GlassRail::Mods);

    // Top row: tabs, search (back is part of the frame).
    const int tabText = std::max(10, g.L(14));
    for (int i = 0; i < kModsTabCount; i++)
    {
        const RECT r = GlassTabRect(g, i);
        const bool active = (int)modsTab == i && searchQuery.empty();
        GlassBox(hdc, r, g.L(12), active ? GlassAccent() : c.fill, active ? kGlassAccentAlpha : c.fillA);
        const COLORREF fg = active ? RGB(255,255,255) : c.text;
        const int mid = (r.top + r.bottom) / 2;
        DrawTabIcon(hdc, i, r.left + g.L(24), (float)mid, (float)g.L(18), fg);
        DrawTextSimple(hdc, r.left + g.L(40), mid - TextMiddle(hdc, tabText), kModsTabNames[i], fg, tabText);
    }
    {
        const RECT r = GlassSearchRect(g);
        if (searchActive)
        {
            GlassBox(hdc, r, g.L(12), GlassAccent(), kGlassAccentAlpha);
            GlassBox(hdc, RECT{r.left + rim + 1, r.top + rim + 1, r.right - rim - 1, r.bottom - rim - 1}, g.L(12) - rim - 1, c.fill, c.fillA);
        }
        else
            GlassBox(hdc, r, g.L(12), c.fill, c.fillA);
        const int size = std::max(10, g.L(15)), y = (r.top + r.bottom) / 2 - TextMiddle(hdc, size);
        if (searchQuery.empty() && !searchActive)
            DrawTextSimple(hdc, r.left + g.L(16), y, L"search...", c.muted, size);
        else
            DrawTextSimple(hdc, r.left + g.L(16), y, searchQuery + (searchActive ? L"|" : L""), c.text, size);
    }

    // The modules.
    ClampGlassScroll();
    const std::vector<DetailFeature> list = GlassFeatures();
    const int nameSize = std::max(11, g.L(19));
    if (list.empty())
    {
        const std::wstring text = !searchQuery.empty() ? L"No matching modules."
                                  : modsTab == ModsTab::Quick ? L"Nothing pinned yet. Right-click a module to pin it here."
                                                              : L"No modules here yet.";
        DrawTextSimple(hdc, g.x + g.L(130), g.y + g.L(130), text, c.muted, std::max(10, g.L(17)));
    }
    for (int pos = 0; pos < (int)list.size(); pos++)
    {
        const RECT row = GlassRowRect(g, pos);
        if (row.right <= row.left) continue;
        const DetailFeature f = list[pos];
        const bool on = FeatureEnabled(f) && FeatureHasSwitch(f);
        GlassBox(hdc, row, g.L(16), on ? GlassAccent() : c.fill, on ? kGlassAccentAlpha : c.fillA);
        const int mid = (row.top + row.bottom) / 2;
        int textX = row.left + g.L(20);
        if (FeaturePinned(f))
        {
            DrawGlassIcon(hdc, GlassIcon::Pin, (float)row.left + g.L(32), (float)mid, (float)g.L(22), on ? RGB(255,255,255) : GlassAccent());
            textX = row.left + g.L(58);
        }
        DrawTextSimple(hdc, textX, mid - TextMiddle(hdc, nameSize), FeatureTitle(f), on ? RGB(255,255,255) : c.text, nameSize);
        const RECT gear = GlassGearRect(g, row);
        DrawHomeIcon(hdc, HomeItem::Settings, (gear.left + gear.right) / 2.0f, (gear.top + gear.bottom) / 2.0f, (float)g.L(26),
                     on ? RGB(255,255,255) : c.muted);
        if (FeatureHasSwitch(f))
            DrawGlassSwitch(hdc, GlassSwitchRect(g, row), on, c);
    }

    // Scrollbar, when some rows are out of view.
    const int rows = ((int)list.size() + 1) / 2;
    if (rows > kGlassRows)
    {
        const RECT track = g.R(kGlassW - 30, 118, 6, kGlassRows * 68.0f - 12);
        const int th = track.bottom - track.top, thumb = std::max(g.L(30), th * kGlassRows / rows);
        const int top = track.top + (th - thumb) * featureScrollRow / (rows - kGlassRows);
        DrawRoundBox(hdc, track.left, track.top, track.right - track.left - 1, th - 1, g.L(3), c.track);
        DrawRoundBox(hdc, track.left, top, track.right - track.left - 1, thumb - 1, g.L(3), GlassAccent());
    }
}

// Close, back and the left icon column of any glass page. True = handled.
bool HandleGlassFrameClick(const GlassLayout& g, int mx, int my)
{
    if (ContainsPoint(GlassCloseRect(g), mx, my))
    {
        CloseMenu();
        return true;
    }
    if (ContainsPoint(GlassBackRect(g), mx, my))
    {
        if (selectedCategory == MenuCategory::Feature && detailFeature != DetailFeature::None)
            detailFeature = DetailFeature::None;   // a module's page: back to the Mods list
        else
        {
            selectedCategory = MenuCategory::Home;
            homeHover = HomeItem::None;
        }
        return true;
    }
    for (int i = 0; i < kGlassRailCount; i++)
    {
        if (!ContainsPoint(GlassRailRect(g, i), mx, my)) continue;
        waitingForShortcut = false;
        partyAddingPlayer = false;
        switch ((GlassRail)i)
        {
        case GlassRail::Mods:     selectedCategory = MenuCategory::Feature; detailFeature = DetailFeature::None; break;
        case GlassRail::Settings: selectedCategory = MenuCategory::Settings; break;
        case GlassRail::Closet:   selectedCategory = MenuCategory::Closet; break;
        case GlassRail::Presets:  selectedCategory = MenuCategory::Presets; presetStatus.clear(); break;
        case GlassRail::Theme:    glassDark = !glassDark; SaveConfig(); break;
        case GlassRail::EditHud:  OpenHudEditor(); break;
        }
        return true;
    }
    return false;
}

// Mouse on the Mods page (window coordinates). `right`: right button (pin / unpin a row).
void HandleGlassClick(int mx, int my, bool right)
{
    const GlassLayout g = GetGlassLayout();
    const RECT search = GlassSearchRect(g);
    if (!right)
        searchActive = ContainsPoint(search, mx, my);
    if (searchActive)
        return;

    const std::vector<DetailFeature> list = GlassFeatures();
    for (int pos = 0; pos < (int)list.size(); pos++)
    {
        const RECT row = GlassRowRect(g, pos);
        if (!ContainsPoint(row, mx, my)) continue;
        waitingForShortcut = false;
        partyAddingPlayer = false;
        if (right)
            TogglePin(list[pos]);
        else if (ContainsPoint(GlassGearRect(g, row), mx, my) || !FeatureHasSwitch(list[pos]))
            detailFeature = list[pos];
        else
            ToggleFeature(list[pos]);
        return;
    }
    if (right)
        return;

    if (HandleGlassFrameClick(g, mx, my))
        return;
    for (int i = 0; i < kModsTabCount; i++)
        if (ContainsPoint(GlassTabRect(g, i), mx, my))
        {
            modsTab = (ModsTab)i;
            searchQuery.clear();
            featureScrollRow = 0;
            return;
        }
}

// ---- Settings, Presets and the module pages as rows ----
// One rounded row per setting, across the panel: its name (and a grey note under it) on the left, its control
// on the right (a switch, a slider with its value, colour swatches, a key button, choices, a button, a text box
// or a radio mark). Headers split long pages; lists (texture packs, party players, presets) use custom rows.
// Drawn in window coordinates like the Mods page; the rows scroll with the wheel.
enum class GlassRowKind { Switch, Slider, Colors, Key, Choice, Button, Info, Header, Radio, Text, Custom };
struct GlassRowCtx;
struct GlassSetting
{
    GlassRowKind kind = GlassRowKind::Info;
    std::wstring label, note;
    std::function<bool()> on;                 // Switch, Radio (chosen), Text (being typed in)
    std::function<void()> press;              // Switch (toggle), Button, Radio (choose), Text (start typing)
    std::function<float()> value;             // Slider: 0..1
    std::function<void(float)> setValue;      // Slider (while dragging)
    std::function<void()> released;           // Slider: the knob was let go (after SaveConfig)
    std::function<std::wstring()> text;       // Slider: its value; Info: the right-hand text; Button: caption; Text: contents
    std::function<int()> tone;                // Info text: 0 grey, 1 good, 2 waiting, 3 bad
    std::vector<COLORREF> colors;             // Colors
    bool rainbow = false;                     // Colors: a rainbow button first (your own colour = index colors.size())
    std::function<COLORREF(float)> gradient;  // Slider: the track drawn as this colour along it (0..1)
    std::vector<std::wstring> options;        // Choice; Text: the button beside the box (optional)
    std::function<int()> chosen;              // Colors / Choice
    std::function<void(int)> choose;          // Colors / Choice; Text: its button
    DetailFeature keyOf = DetailFeature::None;   // Key: whose shortcut
    bool dim = false;                         // only matters while another switch is on
    bool indent = false;                      // belongs to the row above
    std::function<bool()> lit;                // the row is drawn in the accent colour (in use)
    std::function<bool()> expanded;           // Switch with an arrow button: its own settings are shown under it
    std::function<void()> toggleExpand;       // ...and the button opens / closes them
    std::function<void(HDC, const GlassRowCtx&)> draw;                 // Custom: the right-hand part
    std::function<bool(HWND, const GlassRowCtx&, int, int, bool)> click; // Custom: true = handled
};
struct GlassLayout;
struct GlassRowCtx { const GlassLayout& g; const RECT& row; const GlassColors& c; bool lit; };

int glassRowScroll = 0;        // first visible row
bool glassToFrameGen = false;  // the next Optimization page shown starts at its Frame Generation part
int hiveOpen = 0;              // Hive Utils: whose settings are open under its switch (bits: 1 party, 2 auto RQ, 4 stats)
int glassSliderDrag = -1;      // row whose slider is being dragged (-1 = none)
constexpr float kGlassRowTop = 118, kGlassRowBottom = kGlassH - 22, kGlassRowPitch = 66, kGlassRowGap = 8;

float GlassRowPitch(const GlassSetting& s) { return s.kind == GlassRowKind::Header ? 38.0f : kGlassRowPitch; }

GlassSetting SwitchSetting(const std::wstring& label, const std::wstring& note, bool& flag)
{
    GlassSetting s;
    s.kind = GlassRowKind::Switch;
    s.label = label;
    s.note = note;
    s.on = [&flag]() { return flag; };
    s.press = [&flag]() { flag = !flag; SaveConfig(); };
    return s;
}

GlassSetting InfoSetting(const std::wstring& label, const std::wstring& note, std::function<std::wstring()> text = nullptr,
                         std::function<int()> tone = nullptr)
{
    GlassSetting s;
    s.label = label;
    s.note = note;
    s.text = std::move(text);
    s.tone = std::move(tone);
    return s;
}

GlassSetting HeaderSetting(const std::wstring& label)
{
    GlassSetting s;
    s.kind = GlassRowKind::Header;
    s.label = label;
    return s;
}

GlassSetting ButtonSetting(const std::wstring& label, const std::wstring& note, const std::wstring& caption, std::function<void()> press)
{
    GlassSetting s;
    s.kind = GlassRowKind::Button;
    s.label = label;
    s.note = note;
    s.text = [caption]() { return caption; };
    s.press = std::move(press);
    return s;
}

GlassSetting RadioSetting(const std::wstring& label, const std::wstring& note, std::function<bool()> on, std::function<void()> press)
{
    GlassSetting s;
    s.kind = GlassRowKind::Radio;
    s.label = label;
    s.note = note;
    s.on = std::move(on);
    s.press = std::move(press);
    return s;
}

GlassSetting PercentSlider(const std::wstring& label, const std::wstring& note, int& percent, int low, int high)
{
    GlassSetting s;
    s.kind = GlassRowKind::Slider;
    s.label = label;
    s.note = note;
    s.value = [&percent, low, high]() { return (percent - low) / (float)(high - low); };
    s.setValue = [&percent, low, high](float t) { percent = low + (int)std::lround(t * (high - low)); };
    s.text = [&percent]() { return std::to_wstring(percent) + L"%"; };
    return s;
}

GlassSetting PaletteSetting(const std::wstring& label, const COLORREF* colors, int count, int& index)
{
    GlassSetting s;
    s.kind = GlassRowKind::Colors;
    s.label = label;
    s.colors.assign(colors, colors + count);
    s.chosen = [&index]() { return index; };
    s.choose = [&index](int i) { index = i; SaveConfig(); };
    return s;
}

// A palette with the rainbow button; under it, while open, the three sliders of your own colour `hsv`.
// `picker` names the palette for glassPickerOpen; the brightness stays within minV..maxV.
void PaletteWithPicker(std::vector<GlassSetting>& rows, const std::wstring& label, const std::wstring& note,
                       std::vector<COLORREF> colors, int& index, GlassHsv& hsv, int picker, int minV = 0, int maxV = 100)
{
    const int count = (int)colors.size();
    GlassSetting s;
    s.kind = GlassRowKind::Colors;
    s.label = label;
    s.note = note;
    s.colors = std::move(colors);
    s.chosen = [&index]() { return index; };
    s.rainbow = true;
    s.choose = [&index, count, picker](int i)
    {
        if (i == count)   // the rainbow button: your own colour, and its sliders open / close
            glassPickerOpen = glassPickerOpen == picker && index == count ? -1 : picker;
        else if (glassPickerOpen == picker)
            glassPickerOpen = -1;
        index = i;
        SaveConfig();
    };
    if (index == count)
    {
        const COLORREF c = HsvToRgb(hsv);
        wchar_t hex[16];
        swprintf(hex, 16, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        s.note = std::wstring(L"Your own colour ") + hex;
    }
    rows.push_back(s);
    if (glassPickerOpen != picker || index != count)
        return;

    hsv.v = std::clamp(hsv.v, minV, maxV);
    GlassHsv* own = &hsv;
    const auto slider = [&](const wchar_t* name, int part, int low, int high)
    {
        GlassSetting r;
        r.kind = GlassRowKind::Slider;
        r.label = name;
        r.indent = true;
        int* value = part == 0 ? &own->h : part == 1 ? &own->s : &own->v;
        r.value = [value, low, high]() { return (*value - low) / (float)(high - low); };
        r.setValue = [value, low, high](float t) { *value = low + (int)std::lround(t * (high - low)); };
        r.text = [value, part]() { return std::to_wstring(*value) + (part == 0 ? L"\u00B0" : L"%"); };
        r.gradient = [own, part, low, high](float t)
        {
            GlassHsv c = *own;
            const int v = low + (int)std::lround(t * (high - low));
            if (part == 0) { c.h = v; c.s = std::max(c.s, 60); c.v = std::max(c.v, 70); }   // the hue track stays colourful
            else if (part == 1) c.s = v;
            else c.v = v;
            return HsvToRgb(c);
        };
        rows.push_back(r);
    };
    slider(L"Hue", 0, 0, 359);
    slider(L"Saturation", 1, 0, 100);
    slider(L"Brightness", 2, minV, maxV);
}

// The Settings page's palettes: `which` 0 accent, 1 light glass, 2 dark glass. Light glass stays light and
// dark glass dark, so the text on them stays readable.
void PaletteWithPicker(std::vector<GlassSetting>& rows, const std::wstring& label, const COLORREF* colors, int count,
                       int& index, int which)
{
    PaletteWithPicker(rows, label, L"", std::vector<COLORREF>(colors, colors + count), index, glassCustom[which], which,
                      which == 1 ? 70 : 0, which == 2 ? 40 : 100);
}

// A HUD module's colour rows: text, box (when it has one), the Border switch and its colour, and for
// Keystrokes the pressed key. `textNote` says what the module's own text colour is.
void HudColorRows(std::vector<GlassSetting>& rows, HudElementId id, const wchar_t* textNote, bool hasBox)
{
    if ((int)id <= 0 || (int)id >= kHudLookCount) return;
    HudLook& look = hudLooks[(int)id];
    const int picker = 100 + (int)id * kHudPartCount;   // one per module and part
    rows.push_back(HeaderSetting(L"Colors"));
    const auto note = [&look](HudPart part, const wchar_t* text) { return look.index[part] == 0 ? std::wstring(text) : std::wstring(); };
    PaletteWithPicker(rows, L"Text color", note(HudText, textNote), HudPalette(id, HudText), look.index[HudText],
                      look.custom[HudText], picker + HudText);
    if (id == HudElementId::Keystrokes)
        PaletteWithPicker(rows, L"Pressed key color", note(HudPressed, L"Default: blue"), HudPalette(id, HudPressed),
                          look.index[HudPressed], look.custom[HudPressed], picker + HudPressed);
    if (hasBox)
        PaletteWithPicker(rows, L"Background color", note(HudBox, L"Default: dark gray"), HudPalette(id, HudBox),
                          look.index[HudBox], look.custom[HudBox], picker + HudBox);
    if (HudFrameOnly(id))
        PaletteWithPicker(rows, L"Frame color", note(HudBorder, L"Default: the menu's accent color"), HudPalette(id, HudBorder),
                          look.index[HudBorder], look.custom[HudBorder], picker + HudBorder);
    GlassSetting reset = ButtonSetting(L"Reset colors", L"Back to the module's own colors", L"Reset", [&look]()
    {
        const int box = look.box;
        const bool see = look.see, frame = look.border;
        look = HudLook{};
        look.box = box;   // the Background choice is not a colour
        look.see = see;
        look.border = frame;
        glassPickerOpen = -1;
        SaveConfig();
    });
    rows.push_back(reset);
}

GlassSetting ChoiceSetting(const std::wstring& label, const std::wstring& note, std::vector<std::wstring> options,
                           std::function<int()> chosen, std::function<void(int)> choose)
{
    GlassSetting s;
    s.kind = GlassRowKind::Choice;
    s.label = label;
    s.note = note;
    s.options = std::move(options);
    s.chosen = std::move(chosen);
    s.choose = std::move(choose);
    return s;
}

// The "Enabled" switch of a module (or of one part of Hive Utils).
GlassSetting EnabledSetting(DetailFeature f, const std::wstring& label = L"Enabled")
{
    GlassSetting s;
    s.kind = GlassRowKind::Switch;
    s.label = label;
    s.note = FeatureDescription(f);
    std::replace(s.note.begin(), s.note.end(), L'|', L' ');
    s.on = [f]() { return FeatureEnabled(f); };
    s.press = [f]() { ToggleFeature(f); };
    return s;
}

GlassSetting KeySetting(DetailFeature f)
{
    GlassSetting s;
    s.kind = GlassRowKind::Key;
    s.label = f == DetailFeature::Zoom ? L"Zoom key" : f == DetailFeature::MenuOpen ? L"Menu key"
              : f == DetailFeature::DebugMenu ? L"Debug key" : L"Keybind";
    s.note = f == DetailFeature::Zoom ? L"Hold it to zoom. Right-click to reset"
             : f == DetailFeature::MenuOpen ? L"Opens and closes this menu. Right-click to reset to G"
             : f == DetailFeature::DebugMenu ? L"Shows / hides the debug screen in a world. Right-click to reset to F3"
                                            : L"Turns the module on / off. Right-click to reset";
    s.keyOf = f;
    return s;
}

// A trash button that needs a second click (TrashConfirm); `id` names what it deletes.
void DrawGlassTrash(HDC hdc, const RECT& r, bool armed, const GlassColors& c)
{
    DrawReplayIconButton(hdc, r, 0, armed ? RGB(190,60,60) : c.panel, armed ? RGB(255,255,255) : c.text);
}

// A small boxed button inside a custom row.
void DrawGlassSmallButton(HDC hdc, const GlassLayout& g, const RECT& r, const std::wstring& text, bool accent, const GlassColors& c)
{
    GlassBox(hdc, r, g.L(10), accent ? GlassAccent() : c.panel, accent ? kGlassAccentAlpha : c.panelA);
    DrawTextCentered(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, text, accent ? RGB(255,255,255) : c.text,
                     std::max(10, g.L(15)));
}

// "Background" of a HUD module: Black (a solid dark box), Translucent (the game shows through it), Frame only
// (just the border) or None. `box` / `see` are the module's switches; the border is its HudLook's (the Border
// switch under Colors also adds it to a box).
void BackgroundSetting(std::vector<GlassSetting>& rows, HudElementId id, std::function<bool()> box, std::function<bool()> see,
                       std::function<void(bool, bool)> set, bool enabled)
{
    if ((int)id <= 0 || (int)id >= kHudLookCount) return;
    HudLook& look = hudLooks[(int)id];
    GlassSetting s = ChoiceSetting(L"Background", L"Frame only: just a line around it", {L"Black", L"Translucent", L"Frame only", L"None"},
        [box, see, &look]() { return box() ? (see() ? 1 : 0) : (look.border ? 2 : 3); },
        [set, &look](int i)
        {
            set(i <= 1, i == 1);
            look.border = i == 2;
            SaveConfig();
        });
    if (id == HudElementId::Keystrokes)
        s.note = L"A pressed key always shows";
    if (box() && see() && enabled)
        s.note = overlayAlphaError ? L"Windows refused it (error " + std::to_wstring(overlayAlphaError) + L"): a solid box is used"
                 : overlayAlphaShown ? L"Translucent: active" : L"Translucent: waiting for the next frame...";
    rows.push_back(s);
}

// The Arrow / Resource Counters keep their own switches (COUNTERBG).
void CounterSettings(std::vector<GlassSetting>& rows, HudElementId id, bool& box, bool& see, bool enabled)
{
    BackgroundSetting(rows, id, [&box]() { return box; }, [&see]() { return see; },
                      [&box, &see](bool b, bool t) { box = b; see = t; }, enabled);
}

// The other HUD modules with a box: kept in their HudLook.
void HudBoxSettings(std::vector<GlassSetting>& rows, HudElementId id, bool enabled)
{
    if ((int)id <= 0 || (int)id >= kHudLookCount) return;
    HudLook& look = hudLooks[(int)id];
    BackgroundSetting(rows, id, [id]() { return HudBoxOn(id); }, [&look]() { return look.see; },
                      [&look](bool b, bool t) { look.box = b ? 1 : 0; look.see = t; }, enabled);
}

bool AddPartyPlayerFromInput();   // defined with the overlay control below

void OpenReplayFolder()
{
    std::wstring dir;
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    MinecraftDataProvider::ReplayStatus st{};
    if (MinecraftDataProvider::GetReplayStatus(st) && st.path[0])
    {
        dir = Utf8ToWide(st.path);
        const size_t cut = dir.find_last_of(L"\\/");
        if (cut != std::wstring::npos)
            dir.resize(cut);
    }
    else if (n > 0 && n < MAX_PATH)
        dir = std::wstring(base) + L"\\MyClient\\Replays";
    if (!dir.empty())
    {
        CreateDirectoryW((std::wstring(base) + L"\\MyClient").c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ---- The rows of each page ----
void TextureRows(std::vector<GlassSetting>& rows);
void PartyRows(std::vector<GlassSetting>& rows);
void PresetRows(std::vector<GlassSetting>& rows);
void LiteSettings(std::vector<GlassSetting>& rows);
void FrameGenRows(std::vector<GlassSetting>& rows);

// The settings of one part of Hive Utils (shown under its switch).
void HivePartRows(std::vector<GlassSetting>& rows, DetailFeature part)
{
    switch (part)
    {
    case DetailFeature::PartyAutoAccept:
        PartyRows(rows);
        break;
    case DetailFeature::AutoRq:
        rows.push_back(HeaderSetting(L"Mode"));
        for (int i = 0; i < (int)MyClientAutoRq::kModeCount; i++)
            rows.push_back(RadioSetting(AsciiToWide(MyClientAutoRq::kModes[i].name),
                                        L"/" + AsciiToWide(MyClientAutoRq::kModes[i].command) +
                                            (MyClientAutoRq::kModes[i].team ? L" when your team is out or the game ends"
                                                                            : L" when you die or the game ends"),
                                        [i]() { return autoRqMode == i; }, [i]() { autoRqMode = i; SaveConfig(); }));
        if (MyClientAutoRq::kModes[autoRqMode].team)
            rows.push_back(InfoSetting(L"Team modes", L"In a party only the leader can queue on Hive; members do nothing"));
        break;
    case DetailFeature::HiveStats:
    {
        GlassSetting a = SwitchSetting(L"Auto-detect game", L"", hiveStatsAuto);
        a.note = !hiveStatsAuto ? L"Uses the game picked below"
                 : hiveDetectedGame >= 0 ? L"Detected: " + AsciiToWide(HiveStats::kGames[hiveDetectedGame].name)
                                         : L"Not detected - using the pick below";
        a.press = []() { hiveStatsAuto = !hiveStatsAuto; hiveFetcher.Clear(); SaveConfig(); };
        rows.push_back(a);
        rows.push_back(HeaderSetting(L"Game"));
        for (int i = 0; i < HiveStats::kGameCount; i++)
            rows.push_back(RadioSetting(AsciiToWide(HiveStats::kGames[i].name), L"",
                                        [i]() { return hiveStatsAuto && hiveDetectedGame >= 0 ? i == hiveDetectedGame : i == hiveStatsGame; },
                                        [i]() { hiveStatsGame = i; hiveStatsAuto = false; hiveFetcher.Clear(); SaveConfig(); }));
        rows.push_back(InfoSetting(L"Data", L"The Hive's public API. Needs the updated DLL for the player list"));
        HudBoxSettings(rows, HudElementId::HiveStats, hiveStatsEnabled && hiveEnabled);
        HudColorRows(rows, HudElementId::HiveStats, L"Default: the stats colored by how high they are", HudBoxOn(HudElementId::HiveStats));
        break;
    }
    default:
        break;
    }
}

std::vector<GlassSetting> GlassSettings()
{
    std::vector<GlassSetting> rows;
    if (selectedCategory == MenuCategory::Settings)
    {
        rows.push_back(KeySetting(DetailFeature::MenuOpen));   // the menu key lives here, not in Mods
        rows.push_back(SwitchSetting(L"Dark mode", L"Dark glass instead of light (also the moon / sun button)", glassDark));
        PaletteWithPicker(rows, L"Accent color", kGlassAccents, kGlassAccentCount, glassAccentIndex, 0);
        PaletteWithPicker(rows, L"Light mode color", kGlassLightTints, kGlassLightCount, glassLightIndex, 1);
        PaletteWithPicker(rows, L"Dark mode color", kGlassDarkTints, kGlassDarkCount, glassDarkIndex, 2);
        rows.push_back(PercentSlider(L"Drop shadow strength", L"The shadow around the menu panel", glassShadow, 0, 100));
        rows.push_back(PercentSlider(L"UI opacity", L"How solid the glass is", glassOpacity, 20, 100));
        rows.push_back(ButtonSetting(L"Edit HUD", L"Move and resize the in-game HUD", L"Open", []() { OpenHudEditor(); }));
        rows.push_back(ButtonSetting(L"Reset HUD layout", L"Every HUD element back to its first place", L"Reset", []() { ResetHudLayout(); }));
        return rows;
    }
    if (selectedCategory == MenuCategory::Presets)
    {
        PresetRows(rows);
        return rows;
    }

    const DetailFeature f = detailFeature;
    if (FeatureHasSwitch(f))
        rows.push_back(EnabledSetting(f, f == DetailFeature::HiveUtils ? L"Hive Utils" : L"Enabled"));
    if (GetShortcutPtr(f)) rows.push_back(KeySetting(f));

    // Hive Utils: each part has its own switch (they run together) and an arrow button that opens its
    // settings under it.
    if (f == DetailFeature::HiveUtils)
    {
        static const DetailFeature parts[3] = {DetailFeature::PartyAutoAccept, DetailFeature::AutoRq, DetailFeature::HiveStats};
        static const wchar_t* const names[3] = {L"Party Auto Accept", L"Auto RQ", L"Hive Statistic"};
        for (int i = 0; i < 3; i++)
        {
            GlassSetting s = EnabledSetting(parts[i], names[i]);
            if (!hiveEnabled) s.note = L"Paused: switch Hive Utils on above to run it";
            s.expanded = [i]() { return ((hiveOpen >> i) & 1) != 0; };
            s.toggleExpand = [i]() { hiveOpen ^= 1 << i; };
            rows.push_back(s);
            if (!((hiveOpen >> i) & 1)) continue;
            const size_t first = rows.size();
            if (GetShortcutPtr(parts[i])) rows.push_back(KeySetting(parts[i]));
            HivePartRows(rows, parts[i]);
            for (size_t k = first; k < rows.size(); k++) rows[k].indent = true;
        }
    }

    const auto stateTone = [](bool enabled, bool good, bool bad) { return !enabled ? 0 : good ? 1 : bad ? 3 : 2; };
    switch (f)
    {
    case DetailFeature::Zoom:
    {
        GlassSetting s;
        s.kind = GlassRowKind::Slider;
        s.label = L"Zoom level";
        s.note = L"Also the mouse wheel while the key is held";
        // 1.2x .. 20x, even steps on a log scale (the wheel multiplies by 1.15)
        s.value = []() { return (float)(std::log(zoomFactorX100.load() / 120.0) / std::log(20.0 / 1.2)); };
        s.setValue = [](float t)
        {
            zoomFactorX100 = (int)std::lround(120.0 * std::pow(20.0 / 1.2, (double)t));
            zoomFactorDirty = true;
        };
        s.text = []()
        {
            wchar_t text[16];
            swprintf(text, 16, L"%.1fx", zoomFactorX100.load() / 100.0);
            return std::wstring(text);
        };
        rows.push_back(s);
        rows.push_back(InfoSetting(L"State", L"Releasing the key or leaving the game window restores the normal view", []()
        {
            return std::wstring(MinecraftDataProvider::IsZoomApplied() ? L"Applied" : (zoomActive ? L"Waiting for the game" : L"Ready"));
        }, []() { return MinecraftDataProvider::IsZoomApplied() ? 1 : 0; }));
        break;
    }
    case DetailFeature::HealthWarning:
    {
        GlassSetting s;
        s.kind = GlassRowKind::Slider;
        s.label = L"Warn at";
        s.note = L"The edges glow at or below this many hearts (half a heart = 0.5)";
        s.value = []() { return (healthWarningAt - 1) / 19.0f; };
        s.setValue = [](float t) { healthWarningAt = std::clamp(1 + (int)std::lround(t * 19), 1, 20); };
        s.text = []() { return HeartsText(healthWarningAt); };
        rows.push_back(s);
        PaletteWithPicker(rows, L"Color", healthWarningIndex == 0 ? L"Default: red" : L"",
                          std::vector<COLORREF>(kHealthPalette, kHealthPalette + kHealthPaletteCount), healthWarningIndex,
                          healthWarningHsv, 320);
        rows.push_back(PercentSlider(L"Strength", L"How strong the glow is", healthWarningStrength, 20, 100));
        rows.push_back(SwitchSetting(L"Pulse", L"Beats like a heart, faster the lower you are", healthWarningPulse));
        rows.push_back(ButtonSetting(L"Preview", L"Shows the warning for 3 seconds", L"Show",
                                     []() { healthWarningPreviewUntil = GetTickCount64() + 3000; }));
        rows.push_back(InfoSetting(L"Your health", L"Read from the game in a world or server", []()
        {
            int points = 0;
            if (!MinecraftDataProvider::GetPlayerHealth(points)) return std::wstring(L"Not in a world (or the DLL is older)");
            return HeartsText(points);
        }, []()
        {
            int points = 0;
            if (!MinecraftDataProvider::GetPlayerHealth(points)) return 0;
            return points <= healthWarningAt ? 3 : 1;
        }));
        break;
    }
    case DetailFeature::Fullbright:
        rows.push_back(InfoSetting(L"State", L"Turning it off or closing the client returns the game to normal within a second", []()
        {
            if (!fullbrightEnabled) return std::wstring(L"Off");
            if (!MinecraftDataProvider::IsFullbrightAvailable()) return std::wstring(L"Unavailable (game hook not found)");
            return std::wstring(MinecraftDataProvider::IsFullbrightApplied() ? L"Applied" : L"Waiting for the game");
        }, [stateTone]() { return stateTone(fullbrightEnabled, MinecraftDataProvider::IsFullbrightApplied(), !MinecraftDataProvider::IsFullbrightAvailable()); }));
        rows.push_back(InfoSetting(L"Hook", HookRateLine(0)));
        break;
    case DetailFeature::Nametag:
        rows.push_back(InfoSetting(L"State", L"For third-person view (front / back camera)", []()
        {
            if (!nametagEnabled) return std::wstring(L"Off");
            if (!MinecraftDataProvider::IsNametagAvailable()) return std::wstring(L"Unavailable (needs the updated DLL)");
            return std::wstring(MinecraftDataProvider::IsNametagApplied() ? L"Applied" : L"Waiting (switch to third person)");
        }, [stateTone]() { return stateTone(nametagEnabled, MinecraftDataProvider::IsNametagApplied(), !MinecraftDataProvider::IsNametagAvailable()); }));
        rows.push_back(InfoSetting(L"Hook", HookRateLine(1)));
        break;
    case DetailFeature::BlockOutline:
    {
        PaletteWithPicker(rows, L"Outline color", L"Look at a block to see it",
                          std::vector<COLORREF>(kOutlinePalette, kOutlinePalette + kOutlineCount), blockOutlineIndex, blockOutlineHsv, 300);
        rows.push_back(InfoSetting(L"State", HookRateLine(2), []()
        {
            if (!blockOutlineEnabled) return std::wstring(L"Off");
            if (!MinecraftDataProvider::IsOutlineAvailable()) return std::wstring(L"Unavailable (needs the updated DLL)");
            if (MinecraftDataProvider::IsOutlineColored()) return std::wstring(L"Applied");
            return std::wstring(MinecraftDataProvider::IsOutlineRunning() ? L"Running (colour not confirmed)" : L"Waiting (look at a block)");
        }, [stateTone]() { return stateTone(blockOutlineEnabled, MinecraftDataProvider::IsOutlineColored(), !MinecraftDataProvider::IsOutlineAvailable()); }));
        break;
    }
    case DetailFeature::Armor:
        rows.push_back(SwitchSetting(L"Show durability", L"Remaining / maximum beside or under each icon", armorShowDurability));
        rows.push_back(SwitchSetting(L"Horizontal", L"Icons in a row (off: in a column)", armorHorizontal));
        break;
    case DetailFeature::Arrows:
        CounterSettings(rows, HudElementId::Arrows, arrowBackground, arrowTranslucent, arrowCounterEnabled);
        break;
    case DetailFeature::Resources:
        CounterSettings(rows, HudElementId::Resources, resourceBackground, resourceTranslucent, resourceCounterEnabled);
        break;
    case DetailFeature::Keystrokes:
        rows.push_back(InfoSetting(L"Keys", L"W / A / S / D"));
        break;
    case DetailFeature::CPS:
        rows.push_back(InfoSetting(L"Counting", L"Left and right clicks are counted separately"));
        break;
    case DetailFeature::FPS:
        rows.push_back(InfoSetting(L"Value", L"The game's render FPS (1-second average)"));
        break;
    case DetailFeature::Ping:
        rows.push_back(InfoSetting(L"Server", L"One small UDP ping per second", []()
        {
            std::string host;
            int port = 0;
            return MinecraftDataProvider::GetPingTarget(host, port) ? Utf8ToWide(host) + L":" + std::to_wstring(port) : std::wstring(L"---");
        }));
        rows.push_back(InfoSetting(L"Ping", L"Green under 60 ms, yellow under 120 ms, red 120 ms or more", []()
        {
            int ms = 0;
            if (!pingEnabled) return std::wstring(L"Off");
            std::string host;
            int port = 0;
            if (!MinecraftDataProvider::GetPingTarget(host, port)) return std::wstring(L"Waiting (join a server)");
            return PingMonitor::Get(ms) ? std::to_wstring(ms) + L" ms" : std::wstring(L"No reply (the server may ignore pings)");
        }, []() { int ms = 0; return !pingEnabled ? 0 : PingMonitor::Get(ms) ? 1 : 2; }));
        break;
    case DetailFeature::FeatureList:
        rows.push_back(InfoSetting(L"Shows", L"The names of the modules you have switched on"));
        break;
    case DetailFeature::DebugMenu:
        rows.push_back(InfoSetting(L"Shows", L"FPS, server, position, facing, light, biome, time; memory, CPU, display, GPU and DirectX version, targeted block"));
        rows.push_back(PercentSlider(L"Text size", L"How big the debug screen's text is", debugTextSize, 50, 200));
        rows.push_back(InfoSetting(L"Now", L"Press the debug key while playing in a world or server", []()
        {
            if (!debugMenuEnabled) return std::wstring(L"Off");
            if (!shortcuts.debugMenu) return std::wstring(L"No key set");
            return debugScreenShown ? L"Shown (" + KeyName(shortcuts.debugMenu) + L" hides it)"
                                    : L"Hidden (" + KeyName(shortcuts.debugMenu) + L" shows it)";
        }, []() { return !debugMenuEnabled || !shortcuts.debugMenu ? 0 : 1; }));
        break;
    case DetailFeature::DayTimer:
        rows.push_back(InfoSetting(L"Now", L"Night starts at 13000, morning at 0 (20 minutes a day). Hidden when time stops", []()
        {
            int32_t time = 0;
            bool moving = false;
            if (!dayTimerEnabled) return std::wstring(L"Off");
            if (!MinecraftDataProvider::GetWorldClock(time, moving)) return std::wstring(L"Waiting (join a world; needs the updated DLL)");
            const std::wstring tick = L" (time " + std::to_wstring(MyClientFeatures::TimeOfDay(time)) + L")";
            if (!moving) return L"Time is not moving (daylight cycle off)" + tick;
            return (MyClientFeatures::IsNight(time) ? L"Night: morning in " + FormatMinSec(MyClientFeatures::SecondsUntilMorning(time))
                                                    : L"Day: night in " + FormatMinSec(MyClientFeatures::SecondsUntilNight(time))) + tick;
        }, []()
        {
            int32_t time = 0;
            bool moving = false;
            return !dayTimerEnabled ? 0 : MinecraftDataProvider::GetWorldClock(time, moving) && moving ? 1 : 2;
        }));
        rows.push_back(SwitchSetting(L"Show both", L"Time until night and until morning (off: only the next one)", dayTimerBoth));
        break;
    case DetailFeature::BlockDetails:
        rows.push_back(InfoSetting(L"Now", L"The block your crosshair is on, in a world or server", []()
        {
            if (!blockDetailsEnabled) return std::wstring(L"Off");
            BlockDetailsInfo info;
            if (!CurrentBlockDetails(info)) return std::wstring(L"Not looking at a block (needs the updated DLL)");
            return Utf8ToWide(info.id);
        }, []() { BlockDetailsInfo info; return !blockDetailsEnabled ? 0 : CurrentBlockDetails(info) ? 1 : 2; }));
        rows.push_back(SwitchSetting(L"Show source", L"Minecraft, or the name of the add-on that adds the block", blockDetailsSource));
        rows.push_back(SwitchSetting(L"Show tool", L"The tool that mines it: a green tick when what you hold works, a red cross when it will not drop", blockDetailsTool));
        break;
    case DetailFeature::ShulkerPreview:
        rows.push_back(InfoSetting(L"How to use", L"Hover a shulker box in your inventory or a chest to see what is inside"));
        break;
    case DetailFeature::TabList:
        rows.push_back(InfoSetting(L"Shows", L"The players in the world or server you are in"));
        break;
    case DetailFeature::Textures:
        TextureRows(rows);
        break;
    case DetailFeature::ChunkBorder:
        rows.push_back(ChoiceSetting(L"Distance", L"Chunks around you (gold = your chunk)", {L"1", L"2", L"3"},
                                     []() { return chunkBorderRadius - 1; }, [](int i) { chunkBorderRadius = i + 1; SaveConfig(); }));
        rows.push_back(InfoSetting(L"Note", L"Lines are drawn over walls too. Needs the updated DLL"));
        break;
    case DetailFeature::Hitbox:
    {
        static const wchar_t* const names[4] = {L"Players", L"Items", L"Entities", L"Mobs"};
        static const wchar_t* const colours[4] = {L"Red boxes", L"Yellow boxes", L"White boxes", L"Green boxes"};
        static const wchar_t* const defaults[4] = {L"Default: red", L"Default: yellow", L"Default: white", L"Default: green"};
        for (int i = 0; i < 4; i++)
        {
            GlassSetting s;
            s.kind = GlassRowKind::Switch;
            s.label = names[i];
            s.note = hitboxColorIndex[i] == 0 ? colours[i] : L"Boxes in the color below";
            s.on = [i]() { return ((hitboxFilter >> i) & 1) != 0; };
            s.press = [i]() { hitboxFilter ^= (1 << i); SaveConfig(); };
            rows.push_back(s);
        }
        rows.push_back(HeaderSetting(L"Colors"));
        for (int i = 0; i < 4; i++)
            PaletteWithPicker(rows, std::wstring(names[i]) + L" color", hitboxColorIndex[i] == 0 ? defaults[i] : L"",
                              HitboxPalette(i), hitboxColorIndex[i], hitboxHsv[i], 310 + i);
        rows.push_back(ButtonSetting(L"Reset colors", L"Back to red / yellow / white / green", L"Reset", []()
        {
            for (int& index : hitboxColorIndex) index = 0;
            glassPickerOpen = -1;
            SaveConfig();
        }));
        rows.push_back(InfoSetting(L"Last in game", L"Parts behind blocks (and glass) are hidden. Needs the updated DLL", []()
        {
            return hitboxSeenBoxes < 0 ? std::wstring(L"Not played with Hitbox on yet")
                : std::to_wstring(hitboxSeenBoxes) + L" boxes, camera " + (hitboxSeenCamera ? L"OK" : L"missing") +
                  L", players " + (hitboxSeenList ? L"OK" : L"missing");
        }));
        break;
    }
    case DetailFeature::Nick:
    {
        GlassSetting s;
        s.kind = GlassRowKind::Text;
        s.label = L"Name";
        s.note = nickEditing ? L"Type a name, Enter to save" : L"Only you see it, above your own character (third person)";
        s.text = []() { return nickText; };
        s.on = []() { return nickEditing; };
        s.press = []() { nickEditing = true; };
        rows.push_back(s);
        break;
    }
    case DetailFeature::Replay:
        rows.push_back(InfoSetting(L"Recording", L"Start any time: it also keeps the game from when you joined", []()
        {
            MinecraftDataProvider::ReplayStatus st{};
            if (!replayEnabled) return std::wstring(L"Off");
            if (!MinecraftDataProvider::GetReplayStatus(st)) return std::wstring(L"Waiting (needs the updated DLL)");
            wchar_t buf[96];
            swprintf(buf, 96, L"%02u:%02u   %.1f MB   %u packets", st.seconds / 60, st.seconds % 60, st.bytes / 1048576.0, st.packets);
            return std::wstring(buf);
        }, []() { MinecraftDataProvider::ReplayStatus st{}; return !replayEnabled ? 0 : MinecraftDataProvider::GetReplayStatus(st) ? 1 : 2; }));
        rows.push_back(ButtonSetting(L"Replays", L"Play, rename, favourite, download or delete recordings", L"Open Replays", []() { OpenReplays(false); }));
        rows.push_back(ButtonSetting(L"Replay folder", L"The .mcrec files", L"Open folder", []() { OpenReplayFolder(); }));
        break;
    case DetailFeature::MenuSounds:
        rows.push_back(HeaderSetting(L"Sound"));
        for (int i = 0; i < kMenuSoundCount; i++)
        {
            const int mode = kMenuSoundOrder[i];
            rows.push_back(RadioSetting(MenuSoundName(mode), L"", [mode]() { return toggleSoundMode == mode; }, [mode]()
            {
                toggleSoundMode = mode;
                SaveConfig();
                if (menuSoundsEnabled) PlayToggleSound(true);   // preview the sound that was just picked
            }));
        }
        break;
    case DetailFeature::Lite:
        LiteSettings(rows);
        FrameGenRows(rows);
        break;
    case DetailFeature::FastInventory:
        rows.push_back(InfoSetting(L"Screen animation", L"Takes effect after the game reloads packs. A server's UI pack can override it",
                                   []() { return std::wstring(L"0.4–0.6 s  →  0.1 s"); }));
        if (!liteStatus.empty()) rows.push_back(InfoSetting(L"Pack", liteStatus));
        break;
    case DetailFeature::LeftHand:
        rows.push_back(InfoSetting(L"View", L"Only your own first-person view changes; others still see you as usual"));
        rows.push_back(InfoSetting(L"Pack", liteStatus.empty() ? L"Takes effect after the game reloads packs" : liteStatus));
        break;
    case DetailFeature::HitEffect:
    {
        rows.push_back(SwitchSetting(L"Crit effect", L"\"No Critical Hit Effect\" in Optimization hides these too", hitEffectCrit));
        GlassSetting magic = SwitchSetting(L"Enchant effect", L"Blue sparks: show after the game reloads packs", hitEffectMagic);
        magic.press = []() { hitEffectMagic = !hitEffectMagic; if (hitEffectEnabled) ApplyLite(); SaveConfig(); };
        rows.push_back(magic);
        if (!hitEffectCrit && !hitEffectMagic)
            rows.push_back(InfoSetting(L"Nothing shows", L"Both effects are off", nullptr, []() { return 2; }));
        break;
    }
    default:
        HivePartRows(rows, f);   // a part of Hive Utils opened on its own
        break;
    }
    switch (f)
    {
    case DetailFeature::Keystrokes:
        HudBoxSettings(rows, HudElementId::Keystrokes, keystrokesEnabled);
        HudColorRows(rows, HudElementId::Keystrokes, L"Default: white", HudBoxOn(HudElementId::Keystrokes));
        break;
    case DetailFeature::CPS:
        HudBoxSettings(rows, HudElementId::CPS, cpsEnabled);
        HudColorRows(rows, HudElementId::CPS, L"Default: white", HudBoxOn(HudElementId::CPS));
        break;
    case DetailFeature::FPS:
        HudBoxSettings(rows, HudElementId::FPS, fpsEnabled);
        HudColorRows(rows, HudElementId::FPS, L"Default: white", HudBoxOn(HudElementId::FPS));
        break;
    case DetailFeature::DayTimer:
        HudBoxSettings(rows, HudElementId::DayTimer, dayTimerEnabled);
        HudColorRows(rows, HudElementId::DayTimer, L"Default: white (the second line grey)", HudBoxOn(HudElementId::DayTimer));
        break;
    case DetailFeature::BlockDetails:
        HudBoxSettings(rows, HudElementId::BlockDetails, blockDetailsEnabled);
        HudColorRows(rows, HudElementId::BlockDetails, L"Default: white name, blue source", HudBoxOn(HudElementId::BlockDetails));
        break;
    case DetailFeature::Ping:
        HudBoxSettings(rows, HudElementId::Ping, pingEnabled);
        HudColorRows(rows, HudElementId::Ping, L"Default: green / yellow / red by the ping", HudBoxOn(HudElementId::Ping));
        break;
    case DetailFeature::Armor:
        HudBoxSettings(rows, HudElementId::Armor, armorHudEnabled);
        HudColorRows(rows, HudElementId::Armor, L"Default: green / yellow / red by durability", HudBoxOn(HudElementId::Armor));
        break;
    case DetailFeature::Arrows:
        HudColorRows(rows, HudElementId::Arrows, L"Default: white", arrowBackground);
        break;
    case DetailFeature::Resources:
        HudColorRows(rows, HudElementId::Resources, L"Default: each resource's own color", resourceBackground);
        break;
    case DetailFeature::FeatureList:
        HudBoxSettings(rows, HudElementId::FeatureList, featureListEnabled);
        HudColorRows(rows, HudElementId::FeatureList, L"Default: the menu's text color", HudBoxOn(HudElementId::FeatureList));
        break;
    case DetailFeature::TabList:
        HudBoxSettings(rows, HudElementId::TabList, tabListEnabled);
        HudColorRows(rows, HudElementId::TabList, L"Default: headers in the accent color", HudBoxOn(HudElementId::TabList));
        break;
    default:
        break;
    }
    if (FeatureIsHud(f))
        rows.push_back(ButtonSetting(L"Position and size", L"Move and resize it in the HUD editor", L"Edit HUD", []() { OpenHudEditor(); }));
    return rows;
}

// Optimization: the same rows as before (LiteRows), as glass rows.
// Frame Generation, at the end of the Optimization page (2026-10-28: "no need to keep it apart"). It keeps its own
// switch, key and settings, and runs only while Optimization is on too (FrameGenActive).
bool FrameGenActive() { return liteEnabled && frameGenEnabled; }

void FrameGenRows(std::vector<GlassSetting>& rows)
{
    rows.push_back(HeaderSetting(L"Frame Generation"));
    rows.push_back(EnabledSetting(DetailFeature::FrameGen, L"Frame Generation"));
    rows.back().dim = !liteEnabled;
    GlassSetting key = KeySetting(DetailFeature::FrameGen);
    key.label = L"Frame Generation key";
    key.indent = true;
    rows.push_back(key);
    const size_t first = rows.size();
    rows.push_back(InfoSetting(L"State", L"Raises the FPS you see, not the game's own; adds a little input delay", []()
    {
        float shown = 0, game = 0;
        uint32_t state = 0;
        if (!frameGenEnabled) return std::wstring(L"Off");
        if (!liteEnabled) return std::wstring(L"Off (switch Optimization on)");
        if (!MinecraftDataProvider::GetFrameGen(shown, state)) return std::wstring(L"Waiting (needs the updated DLL)");
        if (state == MyClientFeatures::FrameGenRunning)
            return std::to_wstring(std::lround(shown)) + L" FPS shown" +
                   (MinecraftDataProvider::GetFps(game) ? L" (game " + std::to_wstring(std::lround(game)) + L")" : std::wstring());
        if (state == MyClientFeatures::FrameGenPaused) return std::wstring(L"Paused (menu, loading or a hitch)");
        if (state == MyClientFeatures::FrameGenUnavailable) return std::wstring(L"Unavailable on this PC");
        return std::wstring(L"Waiting for the game");
    }, []()
    {
        float shown = 0;
        uint32_t state = 0;
        if (!FrameGenActive()) return 0;
        if (!MinecraftDataProvider::GetFrameGen(shown, state)) return 2;
        return state == MyClientFeatures::FrameGenRunning ? 1 : state == MyClientFeatures::FrameGenUnavailable ? 3 : 2;
    }));
    {
        GlassSetting multiplier;
        multiplier.kind = GlassRowKind::Slider;
        multiplier.label = L"Multiplier";
        multiplier.note = L"Shown FPS = the game's FPS x this, as far as the GPU keeps up";
        const int high = (int)MyClientFeatures::FrameGenMaxMultiplier;
        multiplier.value = [high]() { return (frameGenMultiplier - 2) / (float)(high - 2); };
        multiplier.setValue = [high](float t) { frameGenMultiplier = std::clamp(2 + (int)std::lround(t * (high - 2)), 2, high); };
        multiplier.text = []() { return L"x" + std::to_wstring(frameGenMultiplier); };
        rows.push_back(multiplier);
    }
    rows.push_back(ChoiceSetting(L"Method", L"Repeat only: the lightest, for testing", {L"Motion", L"Blend", L"Repeat"},
                                 []() { return frameGenMethod; }, [](int i) { frameGenMethod = i; SaveConfig(); }));
    rows.push_back(SwitchSetting(L"Pacing", L"Spread the extra frames evenly (off: highest FPS number)", frameGenPacing));
    rows.push_back(InfoSetting(L"Note", L"Paused in menus and while loading. V-Sync is off while this is on. D3D12 only"));
    for (size_t i = first; i < rows.size(); i++)
    {
        rows[i].indent = true;
        rows[i].dim = !FrameGenActive();
    }
}

void LiteSettings(std::vector<GlassSetting>& rows)
{
    for (const LiteRow& row : LiteRows())
    {
        const int key = row.key;
        switch (row.kind)
        {
        case LiteRowKind::Header:
            rows.push_back(HeaderSetting(row.label));
            break;
        case LiteRowKind::Presets:
        {
            std::vector<std::wstring> names(kLitePresetNames, kLitePresetNames + kLitePresetCount);
            rows.push_back(ChoiceSetting(L"Preset", CurrentLitePreset() < 0 ? L"Custom" : L"", names,
                                         []() { return CurrentLitePreset(); }, [](int i) { ApplyLitePreset(i); }));
            break;
        }
        case LiteRowKind::Switch:
        {
            GlassSetting s;
            s.kind = GlassRowKind::Switch;
            s.label = row.label;
            s.dim = !LiteRowActive(key);
            s.indent = key == LiteMobs || key == LiteItems || key == LiteMisc;
            s.on = [key]() { bool* p = LiteSwitchPtr(key); return p && *p; };
            s.press = [key]()
            {
                bool* p = LiteSwitchPtr(key);
                if (!p) return;
                *p = !*p;
                if (LiteKeyUsesPack(key) && liteEnabled) ApplyLite();
                if (key == LiteRender) RequestRenderDistance();   // on: apply the chosen distance now
                SaveConfig();
            };
            rows.push_back(s);
            break;
        }
        case LiteRowKind::Slider:
        {
            GlassSetting s;
            s.kind = GlassRowKind::Slider;
            s.label = row.label;
            s.dim = !LiteRowActive(key);
            s.indent = true;
            s.value = [key]() { const LiteSliderInfo i = LiteSlider(key); return (*i.value - i.min) / (float)(i.max - i.min); };
            s.setValue = [key](float t)
            {
                const LiteSliderInfo i = LiteSlider(key);
                const int steps = (i.max - i.min) / i.step;
                *i.value = i.min + (int)std::lround(t * steps) * i.step;
            };
            s.text = [key]() { const LiteSliderInfo i = LiteSlider(key); return std::to_wstring(*i.value) + i.unit; };
            if (key == LiteRenderDistance) s.released = []() { RequestRenderDistance(); };   // applied when the knob is let go
            rows.push_back(s);
            break;
        }
        case LiteRowKind::Status:
            rows.push_back(InfoSetting(L"Packs", liteStatus.empty() ? L"Pack rows take effect after the game reloads packs." : liteStatus));
            rows.push_back(InfoSetting(L"Entities and system", processTweakStatus.empty()
                                           ? L"Entity and name tag rows work at once (need the updated DLL)." : processTweakStatus));
            break;
        }
    }
}

// Textures: the buttons and the state, then one row per pack (Default textures first): click to add it on
// top of the stack or take it out; #n, move up / down, a gear for its subpacks (their rows open under it) and
// a trash button. Changes wait for Done.
void TextureRows(std::vector<GlassSetting>& rows)
{
    if (!texScanned)
        RefreshTextures();
    FollowGameTextureList();

    {
        GlassSetting s = InfoSetting(L"Packs", L"", nullptr, nullptr);
        s.kind = GlassRowKind::Choice;
        s.note = L"Put .mcpack / .mcaddon / .zip files in the inbox, then Install all";
        s.options = {L"Refresh", L"Open inbox", L"Install all"};
        s.chosen = []() { return -1; };
        s.choose = [](int i)
        {
            if (i == 0) RefreshTextures();
            else if (i == 1)
            {
                std::error_code ec;
                std::filesystem::create_directories(PackInboxFolder(), ec);
                ShellExecuteW(nullptr, L"open", PackInboxFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                texStatus = L"Put your .mcpack / .mcaddon / .zip files in the folder, then press Install all.";
            }
            else InstallInboxPacks();
        };
        rows.push_back(s);
    }
    rows.push_back(InfoSetting(L"Reload", texStatus.empty() ? L"A server that forces its own pack can still override this" : texStatus, []()
    {
        unsigned int reloads = 0;
        if (!MinecraftDataProvider::IsTexturesAvailable()) return std::wstring(L"Unavailable (needs the updated DLL)");
        return MinecraftDataProvider::GetTextureReloads(reloads) ? std::to_wstring(reloads) + L" done" : std::wstring(L"Available");
    }, []() { return MinecraftDataProvider::IsTexturesAvailable() ? 0 : 3; }));
    if (texEditing)
        rows.push_back(ChoiceSetting(L"Not applied yet", L"Click Done to use the new order", {L"Cancel", L"Done"},
                                     []() { return 1; }, [](int i)
                                     {
                                         if (i == 1) ApplyPendingTextures();
                                         else { texEditing = false; texPendingStack.clear(); texStatus = L"Changes cancelled."; }
                                     }));
    rows.push_back(HeaderSetting(L"Click packs to stack them (#1 = top)"));

    const int total = (int)texState.packs.size() + 1;
    for (int index = 0; index < total; index++)
    {
        GlassSetting s;
        s.kind = GlassRowKind::Custom;
        const bool isDefault = index == 0;
        const TexturePacks::Pack* pack = isDefault ? nullptr : &TexRowPack(index);
        s.label = isDefault ? std::wstring(L"Default textures") : Utf8ToWide(pack->name);
        if (pack && !pack->subpacks.empty())
            s.note = L"Subpack: " + TexSubpackLabel(*pack, TexSubpackChosen(*pack));
        const std::string uuid = pack ? pack->uuid : std::string();
        s.lit = [isDefault, uuid]()
        {
            const auto& stack = TexShownStack();
            return isDefault ? stack.empty() : TexStackPos(stack, uuid) >= 0;
        };
        // Right-hand buttons, from the right: trash, gear, #n, down, up.
        const auto part = [](const GlassRowCtx& x, int i)
        {
            const int size = x.g.L(32), gap = x.g.L(8), cy = (x.row.top + x.row.bottom) / 2;
            const int right = x.row.right - x.g.L(16) - i * (size + gap);
            return RECT{right - size, cy - size / 2, right, cy - size / 2 + size};
        };
        s.draw = [index, isDefault, part](HDC hdc, const GlassRowCtx& x)
        {
            const TexturePacks::Pack* p = isDefault ? nullptr : &TexRowPack(index);
            const COLORREF ink = x.lit ? RGB(255,255,255) : x.c.text;
            if (p)
            {
                const bool armed = TexDeleteArmed(*p);
                DrawGlassTrash(hdc, part(x, 0), armed, x.c);
                if (!p->subpacks.empty())
                {
                    const RECT r = part(x, 1);
                    const bool open = texSubpackFor == p->folder.wstring();
                    GlassBox(hdc, r, x.g.L(10), open ? GlassAccentLight() : x.c.panel, open ? kGlassAccentAlpha : x.c.panelA);
                    DrawHomeIcon(hdc, HomeItem::Settings, (r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f, (float)x.g.L(22), x.c.text);
                }
                const auto& stack = TexShownStack();
                const int pos = TexStackPos(stack, p->uuid);
                if (pos >= 0)
                {
                    const RECT n = part(x, 2);
                    DrawTextCentered(hdc, n.left, n.top, n.right - n.left, n.bottom - n.top, L"#" + std::to_wstring(pos + 1), ink,
                                     std::max(10, x.g.L(16)));
                    if (stack.size() > 1)
                        for (const bool up : {true, false})
                        {
                            const RECT m = part(x, up ? 4 : 3);
                            const bool can = up ? pos > 0 : pos + 1 < (int)stack.size();
                            GlassBox(hdc, m, x.g.L(10), x.c.panel, x.c.panelA);
                            DrawChevron(hdc, (m.left + m.right) / 2, (m.top + m.bottom) / 2, up, can ? x.c.text : x.c.muted, std::max(3, x.g.L(5)));
                        }
                }
            }
        };
        s.click = [index, isDefault, part](HWND, const GlassRowCtx& x, int mx, int my, bool right)
        {
            if (right) return false;
            if (!texEditing)
                texPendingStack = texState.stack;
            if (!isDefault)
            {
                const TexturePacks::Pack p = TexRowPack(index);
                if (ContainsPoint(part(x, 0), mx, my))
                {
                    if (TexDeleteArmed(p)) { texDeleteArmed.clear(); DeleteTexturePack(p); }
                    else
                    {
                        texDeleteArmed = p.folder.wstring();
                        texDeleteArmedAt = GetTickCount64();
                        texStatus = L"Click the trash again to remove " + Utf8ToWide(p.name) + L".";
                    }
                    return true;
                }
                if (!p.subpacks.empty() && ContainsPoint(part(x, 1), mx, my))
                {
                    texSubpackFor = texSubpackFor == p.folder.wstring() ? std::wstring() : p.folder.wstring();
                    return true;
                }
                const int pos = TexStackPos(texPendingStack, p.uuid);
                if (pos >= 0 && texPendingStack.size() > 1)
                    for (const bool up : {true, false})
                    {
                        if (!ContainsPoint(part(x, up ? 4 : 3), mx, my)) continue;
                        const int to = up ? pos - 1 : pos + 1;
                        if (to >= 0 && to < (int)texPendingStack.size())
                        {
                            std::swap(texPendingStack[pos], texPendingStack[to]);
                            UpdatePendingStatus();
                        }
                        return true;
                    }
            }
            // The row itself: put the pack on top of the stack / take it out ("Default textures" empties it).
            texDeleteArmed.clear();
            if (isDefault)
                texPendingStack.clear();
            else
            {
                const TexturePacks::Pack p = TexRowPack(index);
                const int pos = TexStackPos(texPendingStack, p.uuid);
                if (pos >= 0) texPendingStack.erase(texPendingStack.begin() + pos);
                else texPendingStack.insert(texPendingStack.begin(), TexturePacks::EntryFor(p));
            }
            UpdatePendingStatus();
            return true;
        };
        rows.push_back(s);

        // The open subpack list: one row per subpack under its pack (picking one waits for Done like any change).
        if (pack && !pack->subpacks.empty() && texSubpackFor == pack->folder.wstring())
            for (const auto& sp : pack->subpacks)
            {
                const std::string folder = sp.folder;
                GlassSetting r = RadioSetting(TexSubpackLabel(*pack, folder), L"", [index, folder]()
                {
                    return TexSubpackChosen(TexRowPack(index)) == folder;
                }, [index, folder]()
                {
                    const TexturePacks::Pack p = TexRowPack(index);
                    if (!texEditing)
                        texPendingStack = texState.stack;
                    int pos = TexStackPos(texPendingStack, p.uuid);
                    if (pos < 0)
                    {
                        // Picking a subpack of a pack not in use adds the pack on top.
                        texPendingStack.insert(texPendingStack.begin(), TexturePacks::EntryFor(p));
                        pos = 0;
                    }
                    texPendingStack[pos] = TexturePacks::WithSubpack(texPendingStack[pos], folder);
                    UpdatePendingStatus();
                    if (texEditing)
                        texStatus = L"Subpack: " + TexSubpackLabel(p, folder) + L" - click Done to use it.";
                });
                r.indent = true;
                rows.push_back(r);
            }
    }
}

// Party Auto Accept: the GameTag box with Add, then the saved players (trash to remove).
void PartyRows(std::vector<GlassSetting>& rows)
{
    {
        GlassSetting s;
        s.kind = GlassRowKind::Text;
        s.label = L"Add a GameTag";
        s.note = L"Max 64, ASCII. Enter or Add to save it";
        s.text = []() { return partyInput; };
        s.on = []() { return partyAddingPlayer; };
        s.press = []() { partyAddingPlayer = true; };
        s.options = {L"Add"};
        s.choose = [](int)
        {
            if (partyInput.empty()) partyAddingPlayer = true;   // nothing typed yet: start typing in the box
            else AddPartyPlayerFromInput();
        };
        rows.push_back(s);
    }
    rows.push_back(InfoSetting(L"Status", MinecraftDataProvider::GetPartyStatusText()));
    rows.push_back(HeaderSetting(partyPlayers.empty() ? L"No players saved yet" : L"Saved players"));
    for (const std::wstring& name : partyPlayers)
    {
        GlassSetting s;
        s.kind = GlassRowKind::Custom;
        s.label = name;
        const auto trash = [](const GlassRowCtx& x)
        {
            const int size = x.g.L(32), cy = (x.row.top + x.row.bottom) / 2, right = x.row.right - x.g.L(16);
            return RECT{right - size, cy - size / 2, right, cy - size / 2 + size};
        };
        s.draw = [name, trash](HDC hdc, const GlassRowCtx& x) { DrawGlassTrash(hdc, trash(x), TrashArmed(L"party:" + name), x.c); };
        s.click = [name, trash](HWND, const GlassRowCtx& x, int mx, int my, bool right)
        {
            if (right || !ContainsPoint(trash(x), mx, my)) return false;
            if (TrashConfirm(L"party:" + name))
            {
                const auto it = std::find(partyPlayers.begin(), partyPlayers.end(), name);
                if (it != partyPlayers.end()) partyPlayers.erase(it);
                SaveConfig();
            }
            return true;
        };
        rows.push_back(s);
    }
}

// Presets: the name box with Save, then one row per preset (Apply, Update, trash).
void PresetRows(std::vector<GlassSetting>& rows)
{
    if (!presetsLoaded) LoadPresets();
    {
        GlassSetting s;
        s.kind = GlassRowKind::Text;
        s.label = L"New preset";
        s.note = presetStatus.empty() ? L"Saves which modules are on. Same name = overwrite" : presetStatus;
        s.text = []() { return presetNameText; };
        s.on = []() { return presetEditing; };
        s.press = []() { presetEditing = true; };
        s.options = {L"Save"};
        s.choose = [](int) { presetEditing = false; SavePresetFromBox(); };
        rows.push_back(s);
    }
    rows.push_back(HeaderSetting(presets.empty() ? L"No presets yet"
                                                 : L"Presets (up to " + std::to_wstring(Presets::MaxPresets) + L")"));
    for (const auto& preset : presets)
    {
        GlassSetting s;
        s.kind = GlassRowKind::Custom;
        const std::string name = preset.name;
        s.label = Utf8ToWide(name);
        s.note = std::to_wstring(Presets::CountOn(preset)) + L" modules on";
        // From the right: trash, Update, Apply.
        const auto part = [](const GlassRowCtx& x, int i)
        {
            const int cy = (x.row.top + x.row.bottom) / 2, h = x.g.L(32), right = x.row.right - x.g.L(16);
            if (i == 0) return RECT{right - h, cy - h / 2, right, cy - h / 2 + h};
            const int r = right - h - x.g.L(10) - (i - 1) * x.g.L(110);
            return RECT{r - x.g.L(100), cy - h / 2, r, cy - h / 2 + h};
        };
        s.draw = [name, part](HDC hdc, const GlassRowCtx& x)
        {
            DrawGlassTrash(hdc, part(x, 0), TrashArmed(L"preset:" + Utf8ToWide(name)), x.c);
            DrawGlassSmallButton(hdc, x.g, part(x, 1), L"Update", false, x.c);
            DrawGlassSmallButton(hdc, x.g, part(x, 2), L"Apply", true, x.c);
        };
        s.click = [name, part](HWND, const GlassRowCtx& x, int mx, int my, bool right)
        {
            if (right) return false;
            for (size_t i = 0; i < presets.size(); i++)
            {
                if (presets[i].name != name) continue;
                if (ContainsPoint(part(x, 2), mx, my))
                {
                    ApplyPreset(presets[i]);
                    presetStatus = L"Applied \"" + Utf8ToWide(name) + L"\".";
                }
                else if (ContainsPoint(part(x, 1), mx, my))
                {
                    presets[i] = CapturePreset(name);
                    SavePresets();
                    presetStatus = L"Updated \"" + Utf8ToWide(name) + L"\" with the modules on now.";
                }
                else if (ContainsPoint(part(x, 0), mx, my))
                {
                    if (!TrashConfirm(L"preset:" + Utf8ToWide(name)))
                        presetStatus = L"Click the trash again to delete \"" + Utf8ToWide(name) + L"\".";
                    else
                    {
                        presetStatus = L"Deleted \"" + Utf8ToWide(name) + L"\".";
                        presets.erase(presets.begin() + i);
                        SavePresets();
                    }
                }
                else
                    return false;
                return true;
            }
            return false;
        };
        rows.push_back(s);
    }
}

// ---- Layout ----
// The rows' rects from the scroll position (empty for rows out of view).
std::vector<RECT> GlassRowRects(const GlassLayout& g, const std::vector<GlassSetting>& rows)
{
    std::vector<RECT> rects(rows.size(), RECT{0, 0, 0, 0});
    float y = kGlassRowTop;
    for (int i = std::max(0, glassRowScroll); i < (int)rows.size(); i++)
    {
        const float h = GlassRowPitch(rows[i]) - kGlassRowGap;
        if (y + h > kGlassRowBottom) break;
        const float indent = rows[i].indent ? 28.0f : 0.0f;
        rects[i] = g.R(126 + indent, y, kGlassW - 40 - 126 - 14 - indent, h);
        y += GlassRowPitch(rows[i]);
    }
    return rects;
}

// The last first row that still shows the end of the page.
int GlassRowMaxScroll(const std::vector<GlassSetting>& rows)
{
    float used = 0;
    int first = (int)rows.size();
    while (first > 0 && used + GlassRowPitch(rows[first - 1]) - kGlassRowGap <= kGlassRowBottom - kGlassRowTop)
        used += GlassRowPitch(rows[--first]);
    return first;
}

void ClampGlassRowScroll(const std::vector<GlassSetting>& rows)
{
    glassRowScroll = std::clamp(glassRowScroll, 0, GlassRowMaxScroll(rows));
}

// Part `i` of a row's control: the switch / key / button box (i = 0), a slider's track, swatch or choice i,
// a text box (i = 0) and its button (i = 1), a radio mark.
RECT GlassControlRect(const GlassLayout& g, const RECT& row, const GlassSetting& s, int i)
{
    const int cy = (row.top + row.bottom) / 2, right = row.right - g.L(20);
    switch (s.kind)
    {
    case GlassRowKind::Switch:
        if (i == 1)   // the arrow button left of the switch (opens the row's settings)
        {
            const RECT sw = GlassSwitchRect(g, row);
            return RECT{sw.left - g.L(14) - g.L(34), cy - g.L(17), sw.left - g.L(14), cy + g.L(17)};
        }
        return GlassSwitchRect(g, row);
    case GlassRowKind::Slider:
        return RECT{right - g.L(210), cy - g.L(12), right, cy + g.L(12)};
    case GlassRowKind::Colors:
    {
        // slot i (the rainbow button is slot 0 when there is one)
        const int size = g.L(30), gap = g.L(8), n = (int)s.colors.size() + (s.rainbow ? 1 : 0);
        const int left = right - n * size - (n - 1) * gap + i * (size + gap);
        return RECT{left, cy - size / 2, left + size, cy - size / 2 + size};
    }
    case GlassRowKind::Choice:
    {
        const int w = g.L(s.options.size() > 3 ? 92 : 108), gap = g.L(8), n = (int)s.options.size();
        const int left = right - n * w - (n - 1) * gap + i * (w + gap);
        return RECT{left, cy - g.L(17), left + w, cy + g.L(17)};
    }
    case GlassRowKind::Key: case GlassRowKind::Button:
        return RECT{right - g.L(150), cy - g.L(17), right, cy + g.L(17)};
    case GlassRowKind::Text:
    {
        const int button = s.options.empty() ? 0 : g.L(90) + g.L(8);
        if (i == 1) return RECT{right - g.L(90), cy - g.L(17), right, cy + g.L(17)};
        return RECT{right - button - g.L(300), cy - g.L(17), right - button, cy + g.L(17)};
    }
    case GlassRowKind::Radio:
    {
        const int size = g.L(24);
        return RECT{right - size, cy - size / 2, right, cy - size / 2 + size};
    }
    default:
        return RECT{0, 0, 0, 0};
    }
}

std::wstring GlassPageTitle()
{
    if (selectedCategory == MenuCategory::Settings) return L"Settings";
    if (selectedCategory == MenuCategory::Presets) return L"Presets";
    return FeatureTitle(detailFeature);
}

COLORREF GlassTone(int tone, const GlassColors& c)
{
    switch (tone)
    {
    case 1:  return glassDark ? RGB(100,220,130) : RGB(28,140,66);
    case 2:  return glassDark ? RGB(230,190,100) : RGB(170,110,10);
    case 3:  return glassDark ? RGB(235,120,100) : RGB(190,50,40);
    default: return c.muted;
    }
}

void DrawGlassRows(HDC hdc)
{
    const GlassLayout g = GetGlassLayout();
    const GlassColors c = GlassTheme();
    DrawGlassFrame(hdc, g, c, selectedCategory == MenuCategory::Settings ? GlassRail::Settings
                              : selectedCategory == MenuCategory::Presets ? GlassRail::Presets : GlassRail::Mods);

    {
        const RECT back = GlassBackRect(g);
        const int size = std::max(12, g.L(26));
        DrawTextSimple(hdc, back.right + g.L(16), (back.top + back.bottom) / 2 - TextMiddle(hdc, size), GlassPageTitle(), c.text, size);
    }

    // Frame Generation has no page of its own any more (it is at the end of Optimization's): whatever opened it
    // (the home chips, 2026-10-28) shows Optimization's page at its Frame Generation part.
    bool toFrameGen = glassToFrameGen;
    glassToFrameGen = false;
    if (selectedCategory == MenuCategory::Feature && detailFeature == DetailFeature::FrameGen)
    {
        detailFeature = DetailFeature::Lite;
        toFrameGen = true;
    }

    // Another page than last time: start at its top.
    static int shownPage = -3;
    const int page = selectedCategory == MenuCategory::Settings ? -1 : selectedCategory == MenuCategory::Presets ? -2 : (int)detailFeature;
    if (page != shownPage)
    {
        shownPage = page;
        glassRowScroll = 0;
    }

    const std::vector<GlassSetting> rows = GlassSettings();
    if (toFrameGen)
        for (int i = 0; i < (int)rows.size(); i++)
            if (rows[i].kind == GlassRowKind::Header && rows[i].label == L"Frame Generation")
            {
                glassRowScroll = i;
                break;
            }
    ClampGlassRowScroll(rows);
    const std::vector<RECT> rects = GlassRowRects(g, rows);
    const int labelSize = std::max(11, g.L(18)), noteSize = std::max(9, g.L(13)), smallSize = std::max(10, g.L(15));
    for (int i = 0; i < (int)rows.size(); i++)
    {
        const RECT row = rects[i];
        if (row.right <= row.left) continue;
        const GlassSetting& s = rows[i];
        const int mid = (row.top + row.bottom) / 2;
        if (s.kind == GlassRowKind::Header)
        {
            DrawTextSimple(hdc, row.left + g.L(6), row.bottom - g.L(4) - TextMiddle(hdc, smallSize) * 2, s.label, c.muted, smallSize);
            continue;
        }
        const bool lit = s.lit && s.lit();
        GlassBox(hdc, row, g.L(16), lit ? GlassAccent() : c.fill, lit ? kGlassAccentAlpha : c.fillA);
        const COLORREF ink = lit ? RGB(255,255,255) : (s.dim ? c.muted : c.text);
        const COLORREF soft = lit ? RGB(230,236,250) : c.muted;
        int textX = row.left + g.L(20);
        if (s.kind == GlassRowKind::Custom && s.label.empty()) textX = row.left;
        if (s.note.empty())
            DrawTextSimple(hdc, textX, mid - TextMiddle(hdc, labelSize), s.label, ink, labelSize);
        else
        {
            DrawTextSimple(hdc, textX, row.top + g.L(8), s.label, ink, labelSize);
            DrawTextSimple(hdc, textX, row.top + g.L(32), s.note, soft, noteSize);
        }

        switch (s.kind)
        {
        case GlassRowKind::Switch:
            DrawGlassSwitch(hdc, GlassControlRect(g, row, s, 0), s.on && s.on(), c, false);
            if (s.toggleExpand)
            {
                const RECT r = GlassControlRect(g, row, s, 1);
                const bool open = s.expanded && s.expanded();
                GlassBox(hdc, r, g.L(10), open ? GlassAccent() : c.panel, open ? kGlassAccentAlpha : c.panelA);
                DrawChevron(hdc, (r.left + r.right) / 2, (r.top + r.bottom) / 2, open, open ? RGB(255,255,255) : c.text, std::max(3, g.L(5)));
            }
            break;
        case GlassRowKind::Slider:
        {
            const RECT r = GlassControlRect(g, row, s, 0);
            const float t = std::clamp(s.value ? s.value() : 0.0f, 0.0f, 1.0f);
            const int th = std::max(4, g.L(6)), ty = mid - th / 2, knob = g.L(20);
            const int kx = r.left + knob / 2 + (int)std::lround(t * (r.right - r.left - knob));
            if (s.gradient)
            {
                // A colour track: thicker, filled with the colours it picks (2 px strips).
                const int gh = std::max(8, g.L(12)), gy = mid - gh / 2, w = r.right - r.left;
                for (int x = 0; x < w; x += 2)
                {
                    HBRUSH brush = CreateSolidBrush(s.gradient(x / (float)std::max(1, w - 1)));
                    const RECT strip{r.left + x, gy, std::min((int)r.right, (int)(r.left + x + 2)), gy + gh};
                    FillRect(hdc, &strip, brush);
                    DeleteObject(brush);
                }
            }
            else
            {
                DrawRoundBox(hdc, r.left, ty, r.right - r.left - 1, th - 1, th / 2, c.track);
                DrawRoundBox(hdc, r.left, ty, std::max(th, (int)(kx - r.left)) - 1, th - 1, th / 2, s.dim ? c.muted : GlassAccent());
            }
            if (s.gradient)
                DrawRoundBox(hdc, kx - knob / 2 - 2, mid - knob / 2 - 2, knob + 3, knob + 3, knob / 2 + 2, RGB(60,64,78));
            DrawRoundBox(hdc, kx - knob / 2, mid - knob / 2, knob - 1, knob - 1, knob / 2, RGB(255,255,255));
            if (s.text)
            {
                const std::wstring text = s.text();
                DrawTextSimple(hdc, r.left - g.L(16) - TextWidth(hdc, text, smallSize), mid - TextMiddle(hdc, smallSize), text, ink, smallSize);
            }
            break;
        }
        case GlassRowKind::Colors:
        {
            const int chosen = s.chosen ? s.chosen() : -1;
            const int off = s.rainbow ? 1 : 0, count = (int)s.colors.size();
            for (int slot = 0; slot < count + off; slot++)
            {
                const int k = slot == 0 && s.rainbow ? count : slot - off;   // palette index (count = your own colour)
                const RECT r = GlassControlRect(g, row, s, slot);
                const int w = r.right - r.left, ring = std::max(2, g.L(3));
                if (k == chosen)
                    DrawRoundBox(hdc, r.left - ring, r.top - ring, w + 2 * ring - 1, w + 2 * ring - 1, g.L(9) + ring, c.text);
                if (k == count)
                {
                    // Rainbow button: four colour dots on a dark tile.
                    static const COLORREF dots[4] = { RGB(235,70,70), RGB(250,200,40), RGB(60,190,90), RGB(60,130,240) };
                    DrawRoundBox(hdc, r.left, r.top, w - 1, w - 1, g.L(9), RGB(36,38,48));
                    const int d = (w - g.L(8)) / 2, pad = g.L(3);
                    for (int q = 0; q < 4; q++)
                        DrawRoundBox(hdc, r.left + pad + (q % 2) * (d + g.L(2)), r.top + pad + (q / 2) * (d + g.L(2)), d - 1, d - 1, d / 2, dots[q]);
                    continue;
                }
                // A swatch can be a glass colour of this page (the panel's tint): nudge it so it stays solid.
                const COLORREF col = s.colors[k];
                DrawRoundBox(hdc, r.left, r.top, w - 1, w - 1, g.L(9), RGB(GetRValue(col), GetGValue(col), GetBValue(col) ^ 1));
            }
            break;
        }
        case GlassRowKind::Choice:
        {
            const int chosen = s.chosen ? s.chosen() : -1;
            for (int k = 0; k < (int)s.options.size(); k++)
                DrawGlassSmallButton(hdc, g, GlassControlRect(g, row, s, k), s.options[k], k == chosen, c);
            break;
        }
        case GlassRowKind::Key: case GlassRowKind::Button:
        {
            const RECT r = GlassControlRect(g, row, s, 0);
            std::wstring caption;
            bool waiting = false;
            if (s.kind == GlassRowKind::Key)
            {
                const int* key = GetShortcutPtr(s.keyOf);
                waiting = waitingForShortcut && shortcutFeature == s.keyOf;
                caption = waiting ? L"Press a key..." : KeyName(key ? *key : 0);
            }
            else
                caption = s.text ? s.text() : L"";
            DrawGlassSmallButton(hdc, g, r, caption, waiting, c);
            break;
        }
        case GlassRowKind::Text:
        {
            const RECT r = GlassControlRect(g, row, s, 0);
            const bool typing = s.on && s.on();
            const int ring = std::max(1, g.L(2));
            if (typing)
                GlassBox(hdc, r, g.L(10), GlassAccent(), kGlassAccentAlpha);
            GlassBox(hdc, typing ? RECT{r.left + ring, r.top + ring, r.right - ring, r.bottom - ring} : r, g.L(10) - (typing ? ring : 0),
                     c.panel, c.panelA);
            const std::wstring text = s.text ? s.text() : L"";
            const bool caret = typing && (GetTickCount64() / 500) % 2;
            DrawTextSimple(hdc, r.left + g.L(12), (r.top + r.bottom) / 2 - TextMiddle(hdc, smallSize),
                           text.empty() && !typing ? std::wstring(L"Click to type") : text + (caret ? L"|" : L""),
                           text.empty() && !typing ? c.muted : c.text, smallSize);
            if (!s.options.empty())
                DrawGlassSmallButton(hdc, g, GlassControlRect(g, row, s, 1), s.options[0], true, c);
            break;
        }
        case GlassRowKind::Radio:
        {
            const RECT r = GlassControlRect(g, row, s, 0);
            const int size = r.right - r.left, ring = std::max(2, g.L(2.5f));
            const bool on = s.on && s.on();
            DrawRoundBox(hdc, r.left, r.top, size - 1, size - 1, size / 2, on ? GlassAccent() : c.track);
            if (on)
            {
                const int dot = size / 2 - 1;
                DrawRoundBox(hdc, r.left + (size - dot) / 2, r.top + (size - dot) / 2, dot - 1, dot - 1, dot / 2, RGB(255,255,255));
            }
            else
                DrawRoundBox(hdc, r.left + ring, r.top + ring, size - 2 * ring - 1, size - 2 * ring - 1, size / 2 - ring, c.text == RGB(34,38,52) ? RGB(250,251,253) : RGB(40,44,58));
            break;
        }
        case GlassRowKind::Info:
            if (s.text)
            {
                const std::wstring text = s.text();
                DrawTextSimple(hdc, row.right - g.L(20) - TextWidth(hdc, text, smallSize), mid - TextMiddle(hdc, smallSize), text,
                               GlassTone(s.tone ? s.tone() : 0, c), smallSize);
            }
            break;
        case GlassRowKind::Custom:
            if (s.draw) s.draw(hdc, GlassRowCtx{g, row, c, lit});
            break;
        default:
            break;
        }
    }

    // Scrollbar, when some rows are out of view.
    const int maxScroll = GlassRowMaxScroll(rows);
    if (maxScroll > 0)
    {
        int shown = 0;
        for (const RECT& r : rects) shown += r.right > r.left;
        const RECT track = g.R(kGlassW - 30, kGlassRowTop, 6, kGlassRowBottom - kGlassRowTop - kGlassRowGap);
        const int th = track.bottom - track.top, thumb = std::max(g.L(30), th * shown / std::max(1, (int)rows.size()));
        const int top = track.top + (th - thumb) * glassRowScroll / maxScroll;
        DrawRoundBox(hdc, track.left, track.top, track.right - track.left - 1, th - 1, g.L(3), c.track);
        DrawRoundBox(hdc, track.left, top, track.right - track.left - 1, thumb - 1, g.L(3), GlassAccent());
    }
}

// A slider follows the mouse while its row is dragged; `done` (button released) saves it.
void DragGlassSlider(int mx, bool done)
{
    const std::vector<GlassSetting> rows = GlassSettings();
    if (glassSliderDrag < 0 || glassSliderDrag >= (int)rows.size() || rows[glassSliderDrag].kind != GlassRowKind::Slider)
    {
        glassSliderDrag = -1;
        ReleaseCapture();
        return;
    }
    const GlassLayout g = GetGlassLayout();
    const GlassSetting& s = rows[glassSliderDrag];
    const RECT row = GlassRowRects(g, rows)[glassSliderDrag];
    const RECT r = GlassControlRect(g, row, s, 0);
    const int knob = g.L(20), span = std::max(1, (int)(r.right - r.left - knob));
    if (s.setValue && r.right > r.left)
        s.setValue(std::clamp((mx - r.left - knob / 2) / (float)span, 0.0f, 1.0f));
    if (done)
    {
        glassSliderDrag = -1;
        ReleaseCapture();
        SaveConfig();
        if (s.released) s.released();
    }
}

// Mouse on a row page (window coordinates). `right`: right button (resets a key).
void HandleGlassRowsClick(HWND hwnd, int mx, int my, bool right)
{
    const GlassLayout g = GetGlassLayout();
    if (!right)
        partyAddingPlayer = false;   // a click anywhere ends typing; the GameTag box turns it back on
    if (!right && HandleGlassFrameClick(g, mx, my))
    {
        glassRowScroll = 0;
        return;
    }
    const std::vector<GlassSetting> rows = GlassSettings();
    const std::vector<RECT> rects = GlassRowRects(g, rows);
    for (int i = 0; i < (int)rows.size(); i++)
    {
        const RECT row = rects[i];
        if (!ContainsPoint(row, mx, my)) continue;
        const GlassSetting& s = rows[i];
        if (s.kind != GlassRowKind::Key)
            waitingForShortcut = false;
        switch (s.kind)
        {
        case GlassRowKind::Switch: case GlassRowKind::Radio:
            if (right) break;
            if (s.toggleExpand && ContainsPoint(GlassControlRect(g, row, s, 1), mx, my))
                s.toggleExpand();
            else if (s.press)
                s.press();   // anywhere else on the row
            break;
        case GlassRowKind::Button:
            if (!right && s.press && ContainsPoint(GlassControlRect(g, row, s, 0), mx, my)) s.press();
            break;
        case GlassRowKind::Slider:
        {
            RECT r = GlassControlRect(g, row, s, 0);
            InflateRect(&r, g.L(10), 0);
            if (!right && ContainsPoint(r, mx, my))
            {
                glassSliderDrag = i;
                SetCapture(hwnd);
                DragGlassSlider(mx, false);
            }
            break;
        }
        case GlassRowKind::Colors: case GlassRowKind::Choice:
        {
            const int off = s.kind == GlassRowKind::Colors && s.rainbow ? 1 : 0;
            const int n = s.kind == GlassRowKind::Colors ? (int)s.colors.size() + off : (int)s.options.size();
            for (int slot = 0; slot < n && !right; slot++)
            {
                RECT r = GlassControlRect(g, row, s, slot);
                InflateRect(&r, g.L(4), g.L(4));
                const int k = off && slot == 0 ? (int)s.colors.size() : slot - off;
                if (ContainsPoint(r, mx, my) && s.choose) { s.choose(k); break; }
            }
            break;
        }
        case GlassRowKind::Text:
            if (right) break;
            if (!s.options.empty() && ContainsPoint(GlassControlRect(g, row, s, 1), mx, my))
            {
                if (s.choose) s.choose(0);
            }
            else if (s.press)
                s.press();
            SetFocus(hwnd);
            break;
        case GlassRowKind::Key:
        {
            if (!ContainsPoint(GlassControlRect(g, row, s, 0), mx, my)) break;
            if (right)
            {
                if (int* key = GetShortcutPtr(s.keyOf)) { *key = s.keyOf == DetailFeature::MenuOpen ? 'G' : s.keyOf == DetailFeature::DebugMenu ? VK_F3 : 0; SaveConfig(); }
                waitingForShortcut = false;
            }
            else
            {
                const bool same = waitingForShortcut && shortcutFeature == s.keyOf;
                waitingForShortcut = !same;
                shortcutFeature = s.keyOf;
                SetFocus(hwnd);
            }
            break;
        }
        case GlassRowKind::Custom:
            if (s.click) s.click(hwnd, GlassRowCtx{g, row, GlassTheme(), s.lit && s.lit()}, mx, my, right);
            break;
        default:
            break;
        }
        return;
    }
    if (!right)
        waitingForShortcut = false;
}

// The blur under the glass: a window just below the overlay, the size of the panel, that Windows fills with
// the blurred game (SetWindowCompositionAttribute, blur behind). None when Windows' transparency effects are off
// or the call is missing: the glass is then plain translucent.
HWND blurWindow = nullptr;

LRESULT CALLBACK BlurProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CreateBlurWindow(HINSTANCE instance)
{
    DWORD transparency = 1, size = sizeof(transparency);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size) == ERROR_SUCCESS &&
        transparency == 0)
        return;

    struct AccentPolicy { int state; int flags; DWORD gradient; int animation; };
    struct CompositionData { int attribute; void* data; SIZE_T size; };
    using SetAttribute = BOOL (WINAPI*)(HWND, CompositionData*);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const SetAttribute set = user32 ? (SetAttribute)(void*)GetProcAddress(user32, "SetWindowCompositionAttribute") : nullptr;
    if (!set)
        return;

    WNDCLASSW wc{};
    wc.lpfnWndProc = BlurProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"MyClientBlur";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);   // black = nothing painted: only the blur shows
    RegisterClassW(&wc);
    blurWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
                                 L"MyClientBlur", L"MyClientBlur", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, instance, nullptr);
    if (!blurWindow)
        return;
    AccentPolicy accent{3, 0, 0, 0};   // ACCENT_ENABLE_BLURBEHIND
    CompositionData data{19, &accent, sizeof(accent)};   // WCA_ACCENT_POLICY
    if (!set(blurWindow, &data))
    {
        DestroyWindow(blurWindow);   // without the blur it would be a black box
        blurWindow = nullptr;
    }
}

// Shows the blur under the Mods page's panel, or hides it.
void UpdateGlassBlur(bool show)
{
    static bool shown = false;
    static RECT last{};
    if (!blurWindow || !overlayWindow)
        return;
    if (!show)
    {
        if (shown) ShowWindow(blurWindow, SW_HIDE);
        shown = false;
        return;
    }
    const GlassLayout g = GetGlassLayout();
    POINT o{0, 0};
    ClientToScreen(overlayWindow, &o);
    const RECT r{o.x + g.x, o.y + g.y, o.x + g.x + g.w, o.y + g.y + g.h};
    if (shown && EqualRect(&r, &last))
        return;
    const int radius = g.L(28);
    SetWindowRgn(blurWindow, CreateRoundRectRgn(0, 0, g.w + 1, g.h + 1, radius * 2, radius * 2), FALSE);   // Windows owns the region
    SetWindowPos(blurWindow, overlayWindow, r.left, r.top, g.w, g.h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    last = r;
    shown = true;
}

void DrawHome(HDC hdc)
{
    const HomeLayout l = GetHomeLayout();
    RECT full{0, 0, (LONG)(l.cx * 2), (LONG)(l.cy * 2)};
    {
        HBRUSH dim = CreateSolidBrush(kHomeDim);
        FillRect(hdc, &full, dim);
        DeleteObject(dim);
    }
    const auto rgb = [](COLORREF c) { return (uint32_t)((GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c)); };
    translucentRects.push_back({full.left, full.top, full.right, full.bottom, rgb(kHomeDim), kHomeDimAlpha});
    translucentRects.push_back({full.left, full.top, full.right, full.bottom, rgb(kHomeFill), kHomeFillAlpha});
    translucentRects.push_back({full.left, full.top, full.right, full.bottom, rgb(kHomeHoverFill), kHomeHoverAlpha});
    translucentRects.push_back({full.left, full.top, full.right, full.bottom, rgb(kHomeChipFill), kHomeChipAlpha});

    // The ring (segments, icons, labels) costs a few hundred ms to draw with its smooth edges, which stalled the
    // HUD's loop: it is drawn once per window size and hovered segment (none + 4) and copied from then on.
    static std::vector<uint32_t> ringCaches[5];
    static int cacheW = 0, cacheH = 0;
    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    POINT org{0, 0};
    LPtoDP(hdc, &org, 1);
    const int reach = (int)std::ceil(l.outer + 14 * l.s);
    const int boxL = std::max(0, (int)(l.cx + org.x) - reach), boxR = std::min(width, (int)(l.cx + org.x) + reach);
    const int boxT = std::max(0, (int)(l.cy + org.y) - reach), boxB = std::min(height, (int)(l.cy + org.y) + reach);
    if (cacheW != width || cacheH != height)
    {
        for (auto& c : ringCaches) c.clear();
        cacheW = width; cacheH = height;
    }
    std::vector<uint32_t>& ringCache = ringCaches[std::clamp((int)homeHover + 1, 0, 4)];
    const bool cached = px && !ringCache.empty() && ringCache.size() == (size_t)std::max(0, boxR - boxL) * std::max(0, boxB - boxT);
    if (cached)
    {
        const int bw = boxR - boxL;
        for (int y = boxT; y < boxB; y++)
            std::memcpy(px + (size_t)y * width + boxL, ringCache.data() + (size_t)(y - boxT) * bw, (size_t)bw * 4);
    }
    static const wchar_t* const labels[4] = {L"MODS", L"CLOSET", L"SETTINGS", L"HUD"};
    std::vector<float> xs, ys;
    for (int i = 0; i < 4 && !cached; i++)
    {
        const bool hover = (int)homeHover == i;
        const float outer = l.outer + (hover ? 8 * l.s : 0);
        HomeSegment(l, i, l.inner, outer, xs, ys);
        FillPolygonSmooth(hdc, xs.data(), ys.data(), (int)xs.size(), hover ? kHomeHoverFill : kHomeFill);
        HomeStroke(hdc, xs, ys, true, std::max(1.5f, 2.0f * l.s), RGB(255,255,255));

        // Icon and label in the middle of the segment, the label under the icon.
        const float pi = 3.14159265f, a = i * pi / 2, mid = (l.inner + outer) / 2;
        const float ix = l.cx + mid * std::sin(a), iy = l.cy - mid * std::cos(a);
        const float icon = 38 * l.s;
        DrawHomeIcon(hdc, (HomeItem)i, ix, iy - 11 * l.s, icon, RGB(255,255,255));
        const int textSize = std::max(11, (int)std::lround(15 * l.s));
        DrawTextCentered(hdc, (int)(ix - 70 * l.s), (int)(iy + 16 * l.s), (int)(140 * l.s), textSize + 6,
                         labels[i], RGB(255,255,255), textSize);
    }
    if (!cached && px && boxR > boxL && boxB > boxT)
    {
        const int bw = boxR - boxL;
        ringCache.resize((size_t)bw * (boxB - boxT));
        for (int y = boxT; y < boxB; y++)
            std::memcpy(ringCache.data() + (size_t)(y - boxT) * bw, px + (size_t)y * width + boxL, (size_t)bw * 4);
    }

    // The head: a 3D cube that turns toward the mouse and sways a little, bobbing gently.
    float yaw = 0, pitch = 0;
    HeadAngles(l, yaw, pitch);
    const float bob = 3.0f * l.s * std::sin(GetTickCount64() / 1000.0f * 1.6f);
    DrawHead3D(hdc, l.cx, l.cy + bob, l.inner * 1.1f, yaw, pitch);

    DrawHomeChips(hdc);

    const int hint = std::max(10, (int)std::lround(13 * l.s));
    DrawTextCentered(hdc, (int)(l.cx - 200 * l.s), (int)(l.cy + l.outer + 26 * l.s), (int)(400 * l.s), hint + 6,
                     L"G / ESC to close", RGB(220,220,226), hint);
}

void OpenHomeItem(HomeItem item);   // defined with OpenMenu / OpenHudEditor below


// ------------------------- Closet ----------------------------
// A glass page like Mods (same frame, Closet lit in the left column). Left card: the skin worn now.
// Right: the skins saved on this PC (SkinLibrary: Minecraft's custom_skins and
// skin packs); a click wears one through the game DLL, which also sends it to the server so everyone
// sees it. A pick made outside a world is worn when the next world loads.
std::vector<SkinLibrary::Entry> closetSkins;
bool closetScanned = false;
int closetScroll = 0;                   // in rows
std::wstring closetStatus;              // the line under the buttons (empty = the hint)
std::wstring closetWearing;             // name of the last skin asked for
ULONGLONG closetAskedAt = 0;            // when it was asked for (0 = no answer awaited)
MinecraftDataProvider::SkinRequestState closetShown = MinecraftDataProvider::SkinRequestState::None;

// Panel units (GlassLayout, 1100 x 600): the grid of skins right of the "now wearing" card.
constexpr int kClosetCols = 6, kClosetRows = 3;
constexpr float kClosetTileW = 104, kClosetTileH = 132, kClosetGap = 12, kClosetGridX = 376, kClosetGridY = 140;
RECT ClosetTileRect(const GlassLayout& g, int slot)   // slot = visible position 0 .. cols*rows-1
{
    return g.R(kClosetGridX + (slot % kClosetCols) * (kClosetTileW + kClosetGap),
               kClosetGridY + (slot / kClosetCols) * (kClosetTileH + kClosetGap), kClosetTileW, kClosetTileH);
}
RECT ClosetFolderRect(const GlassLayout& g) { return g.R(kGlassW - 40 - 150, 62, 150, 40); }
RECT ClosetRefreshRect(const GlassLayout& g) { return g.R(kGlassW - 40 - 150 - 10 - 120, 62, 120, 40); }

int ClosetMaxScroll()
{
    const int rows = ((int)closetSkins.size() + kClosetCols - 1) / kClosetCols;
    return std::max(0, rows - kClosetRows);
}

void ScanClosetSkins()
{
    std::vector<std::wstring> log;
    closetSkins = SkinLibrary::Scan(MinecraftDataProvider::GameDirectory(), log);
    closetScanned = true;
    closetScroll = std::min(closetScroll, ClosetMaxScroll());
    // Which folders were looked at, for when a skin the user expects is missing.
    if (FILE* f = _wfopen((GetExecutableDirectory() + L"\\closet_scan.txt").c_str(), L"wb"))
    {
        log.push_back(std::to_wstring(closetSkins.size()) + L" skins found");
        for (const std::wstring& line : log) { const std::string u = WideToUtf8(line) + "\r\n"; fwrite(u.data(), 1, u.size(), f); }
        fclose(f);
    }
}

// A 16x32 front doll (SkinLibrary::FrontDoll) at (x, y), `cell` pixels per skin pixel. Runs of one colour
// in a row are one rectangle, so a thumbnail costs a few dozen GDI calls. Colours the overlay treats
// specially (the colour key, the glass colours) are nudged by one so they stay solid.
void DrawSkinDoll(HDC hdc, const std::vector<uint32_t>& doll, int x, int y, int cell)
{
    if (doll.size() < 16 * 32) return;
    const GlassColors c = GlassTheme();
    for (int j = 0; j < 32; j++)
        for (int i = 0; i < 16;)
        {
            const uint32_t v = doll[(size_t)j * 16 + i];
            int run = 1;
            while (i + run < 16 && doll[(size_t)j * 16 + i + run] == v) run++;
            if (v >> 24)
            {
                COLORREF rgb = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
                if (rgb == RGB(255,0,255) || rgb == c.border || rgb == c.panel || rgb == c.fill || rgb == GlassAccent())
                    rgb = RGB(GetRValue(rgb), GetGValue(rgb) ^ 1, GetBValue(rgb));
                HBRUSH b = CreateSolidBrush(rgb);
                RECT r{x + i * cell, y + j * cell, x + (i + run) * cell, y + (j + 1) * cell};
                FillRect(hdc, &r, b);
                DeleteObject(b);
            }
            i += run;
        }
}

// The left card's player turns when dragged (right-click: back to the start). Degrees; yaw + = the front
// turned to our right, pitch + = seen from above.
constexpr float kClosetYaw = -28, kClosetPitch = 10;
float closetYaw = kClosetYaw, closetPitch = kClosetPitch;
bool closetSpinning = false;
POINT closetSpinFrom{};
float closetSpinYaw = 0, closetSpinPitch = 0;
RECT ClosetCardRect(const GlassLayout& g) { return g.R(126, 140, 226, 420); }
RECT ClosetModelRect(const GlassLayout& g) { return g.R(126 + 10, 140 + 14, 226 - 20, 420 - 14 - 92); }

// The whole player (SkinModel's boxes, both layers) inside `area`, turned by yaw / pitch, lit like the home
// head. Software ray casting with 2x2 samples per pixel; a ray only tests the boxes whose screen box it is in.
void DrawSkin3D(HDC hdc, const RECT& area, const std::vector<uint8_t>& skin, uint32_t w, uint32_t h, bool slim,
                float yawDeg, float pitchDeg)
{
    int width = 0, height = 0;
    uint32_t* px = DibPixels(hdc, width, height);
    if (!px || skin.empty() || w < 64 || w % 64 || (h != w && h != w / 2)) return;
    const std::vector<SkinModel::Box> boxes = SkinModel::Boxes(slim, h == w);

    const float yaw = yawDeg * 3.14159265f / 180, pitch = pitchDeg * 3.14159265f / 180;
    const float cyw = std::cos(yaw), syw = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    constexpr float kMidY = 16;   // the model turns about its middle
    // World = Rx(pitch) * Ry(yaw) * (local - middle).
    const auto toWorld = [&](float x, float y, float z, float out[3]) {
        const float x1 = x * cyw + z * syw, z1 = -x * syw + z * cyw;
        out[0] = x1; out[1] = y * cp - z1 * sp; out[2] = y * sp + z1 * cp;
    };
    const auto toLocal = [&](const float in[3], float out[3]) {
        const float y1 = in[1] * cp + in[2] * sp, z1 = -in[1] * sp + in[2] * cp;
        out[0] = in[0] * cyw - z1 * syw; out[1] = y1; out[2] = in[0] * syw + z1 * cyw;
    };
    const float L[3] = {-0.35f, 0.55f, 0.76f};
    const auto shade = [&](int axis, int sign, bool inside) {
        float n[3] = {0, 0, 0}, wn[3];
        n[axis] = (float)(inside ? -sign : sign);
        toWorld(n[0], n[1], n[2], wn);
        return 0.58f + 0.42f * std::max(0.0f, wn[0] * L[0] + wn[1] * L[1] + wn[2] * L[2]);
    };

    POINT o{0, 0};
    LPtoDP(hdc, &o, 1);
    const float areaW = (float)(area.right - area.left), areaH = (float)(area.bottom - area.top);
    const float unit = std::min(areaH / 36, areaW / 20);   // pixels per skin pixel, the same at every turn
    if (unit < 1) return;
    const float cx = area.left + o.x + areaW / 2, cy = area.top + o.y + areaH / 2;

    // Each box's screen box (its 8 corners turned), and all of them together.
    struct Span { float x0, x1, y0, y1; };
    std::vector<Span> spans(boxes.size());
    Span all{1e9f, -1e9f, 1e9f, -1e9f};
    for (size_t i = 0; i < boxes.size(); i++)
    {
        const SkinModel::Box& b = boxes[i];
        Span s{1e9f, -1e9f, 1e9f, -1e9f};
        for (int c = 0; c < 8; c++)
        {
            float wp[3];
            toWorld(c & 1 ? b.x1 : b.x0, (c & 2 ? b.y1 : b.y0) - kMidY, c & 4 ? b.z1 : b.z0, wp);
            const float sx = cx + wp[0] * unit, sy = cy - wp[1] * unit;
            s.x0 = std::min(s.x0, sx); s.x1 = std::max(s.x1, sx); s.y0 = std::min(s.y0, sy); s.y1 = std::max(s.y1, sy);
        }
        spans[i] = s;
        all.x0 = std::min(all.x0, s.x0); all.x1 = std::max(all.x1, s.x1); all.y0 = std::min(all.y0, s.y0); all.y1 = std::max(all.y1, s.y1);
    }
    const int left = std::max({0, (int)std::floor(all.x0) - 1, (int)(area.left + o.x)});
    const int right = std::min({width, (int)std::ceil(all.x1) + 1, (int)(area.right + o.x)});
    const int top = std::max({0, (int)std::floor(all.y0) - 1, (int)(area.top + o.y)});
    const int bottom = std::min({height, (int)std::ceil(all.y1) + 1, (int)(area.bottom + o.y)});

    float dir[3];
    { const float dw[3] = {0, 0, -1}; toLocal(dw, dir); }
    const GlassColors colors = GlassTheme();
    constexpr int kSub = 2;
    for (int py = top; py < bottom; py++)
        for (int pxl = left; pxl < right; pxl++)
        {
            uint32_t r = 0, gr = 0, bl = 0, hits = 0;
            for (int sy = 0; sy < kSub; sy++)
                for (int sx = 0; sx < kSub; sx++)
                {
                    const float fx = pxl + (sx + 0.5f) / kSub, fy = py + (sy + 0.5f) / kSub;
                    const float ow[3] = {(fx - cx) / unit, -(fy - cy) / unit, 100};
                    float ol[3];
                    toLocal(ow, ol);
                    ol[1] += kMidY;
                    float bestT = 1e9f, light = 1;
                    uint32_t best = 0;
                    for (size_t i = 0; i < boxes.size(); i++)
                    {
                        const Span& s = spans[i];
                        if (fx < s.x0 || fx > s.x1 || fy < s.y0 || fy > s.y1) continue;
                        const SkinModel::Box& b = boxes[i];
                        const float lo[3] = {b.x0, b.y0, b.z0}, hi[3] = {b.x1, b.y1, b.z1};
                        float tNear = -1e9f, tFar = 1e9f;
                        int nearAxis = 0, farAxis = 0, nearSign = 1, farSign = 1;
                        bool miss = false;
                        for (int a = 0; a < 3 && !miss; a++)
                        {
                            if (std::fabs(dir[a]) < 1e-6f) { miss = ol[a] < lo[a] || ol[a] > hi[a]; continue; }
                            float t1 = (lo[a] - ol[a]) / dir[a], t2 = (hi[a] - ol[a]) / dir[a];
                            int s1 = -1, s2 = 1;
                            if (t1 > t2) { std::swap(t1, t2); std::swap(s1, s2); }
                            if (t1 > tNear) { tNear = t1; nearAxis = a; nearSign = s1; }
                            if (t2 < tFar) { tFar = t2; farAxis = a; farSign = s2; }
                        }
                        if (miss || tNear > tFar || tNear >= bestT) continue;
                        uint32_t rgb = 0;
                        const auto texel = [&](float t, int axis, int sign) {
                            return SkinModel::Texel(skin, w, h, b, axis, sign, ol[0] + dir[0] * t, ol[1] + dir[1] * t, ol[2] + dir[2] * t, rgb);
                        };
                        if (texel(tNear, nearAxis, nearSign))
                        { bestT = tNear; best = rgb; light = shade(nearAxis, nearSign, false); }
                        else if (b.outer && tFar < bestT && texel(tFar, farAxis, farSign))
                        { bestT = tFar; best = rgb; light = shade(farAxis, farSign, true) * 0.8f; }   // the layer's inside
                    }
                    if (bestT >= 1e9f) continue;
                    r += (uint32_t)(((best >> 16) & 0xFF) * light);
                    gr += (uint32_t)(((best >> 8) & 0xFF) * light);
                    bl += (uint32_t)((best & 0xFF) * light);
                    hits++;
                }
            if (hits * 2 < kSub * kSub) continue;   // less than half covered: leave the card
            COLORREF c = RGB(r / hits, gr / hits, bl / hits);
            // Never a colour the overlay treats specially (it would turn see-through).
            if (c == RGB(255,0,255) || c == colors.border || c == colors.panel || c == colors.fill || c == GlassAccent() || c == kHudClear)
                c = RGB(GetRValue(c), GetGValue(c) ^ 1, GetBValue(c));
            px[(size_t)py * width + pxl] = ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | GetBValue(c);
        }
}

// Left button down on the card: start turning the player. False when the card has no skin to turn.
bool ClosetStartSpin(HWND hwnd, int mx, int my)
{
    uint32_t w = 0, h = 0;
    if (CurrentSkin(w, h).empty() || !ContainsPoint(ClosetCardRect(GetGlassLayout()), mx, my)) return false;
    closetSpinning = true;
    closetSpinFrom = {mx, my};
    closetSpinYaw = closetYaw;
    closetSpinPitch = closetPitch;
    SetCapture(hwnd);
    return true;
}

// The mouse moved while turning; `done` (button released) ends it.
void ClosetSpin(int mx, int my, bool done)
{
    const float s = std::max(0.25f, GetGlassLayout().s);
    closetYaw = std::fmod(closetSpinYaw + (mx - closetSpinFrom.x) * 0.7f / s, 360.0f);
    closetPitch = std::clamp(closetSpinPitch + (my - closetSpinFrom.y) * 0.4f / s, -40.0f, 40.0f);
    if (done)
    {
        closetSpinning = false;
        ReleaseCapture();
    }
}

// Right-click on the card: face the start angle again.
bool ClosetResetSpin(int mx, int my)
{
    if (!ContainsPoint(ClosetCardRect(GetGlassLayout()), mx, my)) return false;
    closetYaw = kClosetYaw;
    closetPitch = kClosetPitch;
    return true;
}

// Follows the game's answer to the last pick; true when the shown status changed (redraw).
bool PollClosetAnswer()
{
    using State = MinecraftDataProvider::SkinRequestState;
    if (!closetAskedAt) return false;
    State state = MinecraftDataProvider::SkinRequestAnswer();
    if (state == closetShown && state != State::Waiting) return false;
    const ULONGLONG waited = GetTickCount64() - closetAskedAt;
    std::wstring text;
    switch (state)
    {
    case State::Sent: text = L"Wearing \"" + closetWearing + L"\" - sent to the server, everyone sees it."; closetAskedAt = 0; break;
    case State::Failed: text = L"Could not wear \"" + closetWearing + L"\" (see the MyClientData log)."; closetAskedAt = 0; break;
    case State::NoPlayer: text = L"Join a world first."; closetAskedAt = 0; break;
    case State::Waiting:
        if (waited < 1500) return false;
        text = L"\"" + closetWearing + L"\" will be worn when you join a world.";
        break;
    default: closetAskedAt = 0; return false;
    }
    closetShown = state;
    if (text == closetStatus) return false;
    closetStatus = text;
    return true;
}

void DrawCloset(HDC hdc)
{
    if (!closetScanned) ScanClosetSkins();
    const GlassLayout g = GetGlassLayout();
    const GlassColors c = GlassTheme();
    DrawGlassFrame(hdc, g, c, GlassRail::Closet);

    // Top row: the title, Refresh and Open folder.
    {
        const RECT back = GlassBackRect(g);
        const int size = std::max(12, g.L(22));
        DrawTextSimple(hdc, back.right + g.L(16), (back.top + back.bottom) / 2 - TextMiddle(hdc, size), L"CLOSET", c.text, size);
    }
    const int buttonText = std::max(10, g.L(14));
    for (int k = 0; k < 2; k++)
    {
        const RECT r = k ? ClosetFolderRect(g) : ClosetRefreshRect(g);
        GlassBox(hdc, r, g.L(12), c.fill, c.fillA);
        DrawTextCentered(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, k ? L"Open folder" : L"Refresh", c.text, buttonText);
    }
    const int small = std::max(10, g.L(15));
    DrawTextSimple(hdc, g.x + g.L(126), g.y + g.L(112), closetStatus.empty()
                   ? L"Click a skin to wear it. Other players see it too." : closetStatus, closetStatus.empty() ? c.muted : c.text, small);

    // Left card: the skin worn now.
    uint32_t w = 0, h = 0;
    const std::vector<uint8_t>& skin = CurrentSkin(w, h);
    const bool persona = MinecraftDataProvider::SkinHasNoPicture();
    {
        const RECT card = ClosetCardRect(g);
        GlassBox(hdc, card, g.L(16), c.fill, c.fillA);
        const int cell = std::max(2, g.L(10));
        const int dx = (card.left + card.right) / 2 - 8 * cell, dy = card.top + g.L(24);
        if (!skin.empty())
        {
            // Cached: LooksSlim reads the arm pixels, and the card is redrawn on every drag step.
            static std::vector<uint8_t> slimOf;
            static bool slim = false;
            if (slimOf != skin) { slimOf = skin; slim = MyClientSkin::LooksSlim(skin.data(), w, h); }
            DrawSkin3D(hdc, ClosetModelRect(g), skin, w, h, slim, closetYaw, closetPitch);
            DrawTextCentered(hdc, card.left, card.bottom - g.L(82), card.right - card.left, g.L(18),
                             L"Drag to turn", c.muted, std::max(9, g.L(12)));
        }
        else
        {
            int side = 8;
            const std::vector<uint32_t> face = HeadColors(side);
            DrawPixelArt(hdc, face, side, dx + 4 * cell, dy, cell * 8 / side);
        }
        DrawTextCentered(hdc, card.left, card.bottom - g.L(56), card.right - card.left, g.L(20),
                         skin.empty() ? (persona ? L"Now wearing" : L"Not seen yet") : L"Now wearing", c.text, std::max(10, g.L(16)));
        DrawTextCentered(hdc, card.left, card.bottom - g.L(34), card.right - card.left, g.L(18),
                         skin.empty() ? (persona ? L"a character creator skin" : L"join a world once") : std::to_wstring(w) + L" x " + std::to_wstring(h),
                         c.muted, std::max(9, g.L(13)));
    }

    if (closetSkins.empty())
    {
        const int x = g.x + g.L(kClosetGridX + 4), y = g.y + g.L(kClosetGridY + 10), line = g.L(24);
        DrawTextSimple(hdc, x, y, L"No skins found on this PC.", c.text, std::max(11, g.L(18)));
        const wchar_t* const lines[] = {
            L"Import one in Minecraft: Dressing Room -> Classic skins -> Import,",
            L"or press Open folder, put skin PNGs there and press Refresh.",
            L"Marketplace and Character Creator skins cannot be listed",
            L"(the game keeps them locked).",
        };
        for (int i = 0; i < 4; i++) DrawTextSimple(hdc, x, y + g.L(36) + i * line, lines[i], c.muted, small);
        return;
    }

    // The skins: worn one in blue, like a switched-on module.
    const int cell = std::max(2, (int)std::lround(3.2f * g.s)), nameSize = std::max(9, g.L(13));
    for (int slot = 0; slot < kClosetCols * kClosetRows; slot++)
    {
        const int index = closetScroll * kClosetCols + slot;
        if (index >= (int)closetSkins.size()) break;
        const SkinLibrary::Entry& e = closetSkins[index];
        const RECT r = ClosetTileRect(g, slot);
        const bool worn = !skin.empty() && e.w == w && e.h == h && e.rgba == skin;
        GlassBox(hdc, r, g.L(16), worn ? GlassAccent() : c.fill, worn ? kGlassAccentAlpha : c.fillA);
        DrawSkinDoll(hdc, e.doll, (r.left + r.right) / 2 - 8 * cell, r.top + g.L(8), cell);
        const int tileW = r.right - r.left;
        std::wstring name = e.name;
        while (name.size() > 1 && TextWidth(hdc, name, nameSize) > tileW - g.L(10)) name.pop_back();
        if (name.size() < e.name.size() && name.size() > 1) { name.pop_back(); name += L"\u2026"; }
        DrawTextCentered(hdc, r.left, r.bottom - g.L(22), tileW, g.L(16), name, worn ? RGB(255,255,255) : c.text, nameSize);
    }

    // Scrollbar, when some rows are out of view (like the Mods page's).
    const int rows = ((int)closetSkins.size() + kClosetCols - 1) / kClosetCols;
    if (rows > kClosetRows)
    {
        const RECT track = g.R(kGlassW - 30, kClosetGridY, 6, kClosetRows * (kClosetTileH + kClosetGap) - kClosetGap);
        const int th = track.bottom - track.top, thumb = std::max(g.L(30), th * kClosetRows / rows);
        const int top = track.top + (th - thumb) * closetScroll / (rows - kClosetRows);
        DrawRoundBox(hdc, track.left, track.top, track.right - track.left - 1, th - 1, g.L(3), c.track);
        DrawRoundBox(hdc, track.left, top, track.right - track.left - 1, thumb - 1, g.L(3), GlassAccent());
    }
}

// Mouse on the Closet page (window coordinates).
void HandleClosetClick(int mx, int my)
{
    const GlassLayout g = GetGlassLayout();
    if (HandleGlassFrameClick(g, mx, my)) return;
    if (ContainsPoint(ClosetRefreshRect(g), mx, my)) { ScanClosetSkins(); closetStatus.clear(); return; }
    if (ContainsPoint(ClosetFolderRect(g), mx, my))
    {
        const std::filesystem::path folder = SkinLibrary::CustomSkinsFolder();
        std::error_code ec;
        if (folder.empty()) { closetStatus = L"Minecraft's com.mojang folder was not found."; return; }
        std::filesystem::create_directories(folder, ec);
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    for (int slot = 0; slot < kClosetCols * kClosetRows; slot++)
    {
        const int index = closetScroll * kClosetCols + slot;
        if (index >= (int)closetSkins.size()) break;
        if (!ContainsPoint(ClosetTileRect(g, slot), mx, my)) continue;
        const SkinLibrary::Entry& e = closetSkins[index];
        using State = MinecraftDataProvider::SkinRequestState;
        const State state = MinecraftDataProvider::RequestSkin(e.rgba, e.w, e.h, e.slim, WideToUtf8(e.name));
        closetWearing = e.name;
        closetAskedAt = 0;
        closetShown = State::None;
        switch (state)
        {
        case State::Waiting:
            closetAskedAt = GetTickCount64();
            closetShown = State::Waiting;
            closetStatus = L"Putting on \"" + e.name + L"\"...";
            break;
        case State::NoGame: closetStatus = L"Start Minecraft first."; break;
        case State::OldDll: closetStatus = L"Update MyClientData (the game DLL) to switch skins."; break;
        default: closetStatus = L"Could not reach the game - try again."; break;
        }
        return;
    }
}


void DrawMenu(HDC hdc)
{
    // Text on a glass page is noted for OverlayAlpha::SoftenInk (every return below ends the scope).
    struct InkScope { explicit InkScope(bool on) { inkTracking = on; } ~InkScope() { inkTracking = false; } } inkScope(GlassPageShown());
    if (hudEditMode)
    {
        menuDrawing = false;   // the real in-game HUD is shown and edited here

        DrawHudEditor(hdc);

        return;
    }

    if (selectedCategory == MenuCategory::Home && !replaysOpen)
    {
        menuDrawing = false;   // drawn straight on the game, in window coordinates
        DrawHome(hdc);
        return;
    }

    if (GlassModsShown())
    {
        menuDrawing = false;   // the glass panel, in window coordinates
        DrawGlassMods(hdc);
        return;
    }

    if (GlassClosetShown())
    {
        menuDrawing = false;   // a glass page, in window coordinates
        DrawCloset(hdc);
        return;
    }

    if (GlassContentShown())
    {
        menuDrawing = false;   // Settings, Presets, a module's page: rows, in window coordinates
        DrawGlassRows(hdc);
        return;
    }

    // Presets, a module's page not converted yet: the glass frame, then the page's own layout inside it.
    const bool glassPage = GlassContentShown();
    if (glassPage)
        DrawGlassFrame(hdc, GetGlassLayout(), GlassTheme(),
                       selectedCategory == MenuCategory::Settings ? GlassRail::Settings
                       : selectedCategory == MenuCategory::Presets ? GlassRail::Presets : GlassRail::Mods);

    int offsetX = 0;
    int offsetY = 0;

    MenuOffset(offsetX, offsetY);

    menuDrawing = true;

    SetViewportOrgEx(
        hdc,
        offsetX,
        offsetY,
        nullptr
    );

    if (!glassPage)
        DrawRoundBox(
            hdc,
            10,
            10,
            920,
            590,
            18,
            CurrentTheme().bg
        );

    if (replaysOpen)
    {
        DrawReplaysPanel(hdc);
        menuDrawing = false;
        SetViewportOrgEx(hdc, 0, 0, nullptr);
        return;
    }

    if (!glassPage)
        DrawNavigation(hdc);
    glassContent = glassPage;

    // Everything below the top bar was laid out with the old left menu in mind: shift it left.
    SetViewportOrgEx(
        hdc,
        offsetX - kContentShift,
        offsetY,
        nullptr
    );

    if (selectedCategory ==
        MenuCategory::Feature)
    {
        EnsureDetailSelected();

        if (detailFeature ==
            DetailFeature::None)
        {
            DrawFeatureMenu(hdc);
        }
        else
        {
            // Settings page: only the settings, shifted to the left, plus a Back button.
            POINT origin{};
            GetViewportOrgEx(hdc, &origin);

            SetViewportOrgEx(
                hdc,
                origin.x - kDetailShift,
                origin.y,
                nullptr
            );

            DrawDetailList(hdc);

            DrawFeatureDetails(hdc);

            SetViewportOrgEx(
                hdc,
                origin.x,
                origin.y,
                nullptr
            );
        }
    }
    else if (
        selectedCategory ==
        MenuCategory::Settings)
    {
        DrawSettings(hdc);
    }
    else if (selectedCategory == MenuCategory::Presets)
    {
        DrawPresets(hdc);
    }

    menuDrawing = false;
    glassContent = false;

    SetViewportOrgEx(
        hdc,
        0,
        0,
        nullptr
    );
}


// ------------------------- Overlay control ------------------

// SetForegroundWindow alone is often refused for a background process; attaching
// to the foreground window's input thread for a moment makes it succeed.
void ForceForeground(HWND hwnd)
{
    HWND foreground =
        GetForegroundWindow();

    DWORD foregroundThread =
        foreground
            ? GetWindowThreadProcessId(
                  foreground,
                  nullptr
              )
            : 0;

    DWORD thisThread =
        GetCurrentThreadId();

    const bool attach =
        foregroundThread != 0 &&
        foregroundThread != thisThread;

    if (attach)
    {
        AttachThreadInput(
            thisThread,
            foregroundThread,
            TRUE
        );
    }

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);

    if (attach)
    {
        AttachThreadInput(
            thisThread,
            foregroundThread,
            FALSE
        );
    }
}


void ActivateOverlay()
{
    if (!overlayWindow)
        return;

    LONG_PTR style =
        GetWindowLongPtrW(
            overlayWindow,
            GWL_EXSTYLE
        );

    style &= ~WS_EX_NOACTIVATE;

    SetWindowLongPtrW(
        overlayWindow,
        GWL_EXSTYLE,
        style
    );

    SetWindowPos(
        overlayWindow,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE |
        SWP_NOSIZE |
        SWP_SHOWWINDOW
    );

    // Cover the whole game window so the mouse stays with the menu.
    if (catcherWindow && minecraftWindow)
    {
        RECT gr;

        if (GetWindowRect(
                minecraftWindow,
                &gr))
        {
            SetWindowPos(
                catcherWindow,
                overlayWindow,
                gr.left,
                gr.top,
                gr.right - gr.left,
                gr.bottom - gr.top,
                SWP_NOACTIVATE |
                SWP_SHOWWINDOW
            );
        }
    }

    ForceForeground(
        overlayWindow
    );

    SetFocus(
        overlayWindow
    );

    ShowMouseCursor();
}


// backToGame = false: another app is in front; leave the focus and the cursor with it (giving the focus back
// to Minecraft made the game take the mouse for its camera while that app covered it).
void DeactivateOverlay(bool backToGame)
{
    if (!overlayWindow)
        return;

    if (catcherWindow)
    {
        ShowWindow(
            catcherWindow,
            SW_HIDE
        );
    }

    LONG_PTR style =
        GetWindowLongPtrW(
            overlayWindow,
            GWL_EXSTYLE
        );

    style |= WS_EX_NOACTIVATE;

    SetWindowLongPtrW(
        overlayWindow,
        GWL_EXSTYLE,
        style
    );

    ReleaseCapture();

    if (!backToGame)
        return;

    if (minecraftWindow)
    {
        SetForegroundWindow(
            minecraftWindow
        );
    }

    HideMouseCursor();
}


void OpenMenu()
{
    menuOpen = true;
    hudEditMode = false;

    selectedCategory =
        MenuCategory::Home;
    homeHover = HomeItem::None;
    homeChipHover = -1;

    detailFeature =
        DetailFeature::None;

    selectedHud =
        HudElementId::None;

    draggingHud =
        HudElementId::None;

    waitingForShortcut = false;
    partyAddingPlayer = false;

    MinecraftDataProvider::SetHudMenuOpen(true);   // before the focus moves (see TrackGameScreens in the DLL)
    ActivateOverlay();
}


void OpenHudEditor()
{
    menuOpen = true;
    hudEditMode = true;

    selectedCategory =
        MenuCategory::HUD;

    detailFeature =
        DetailFeature::None;

    selectedHud =
        HudElementId::None;

    draggingHud =
        HudElementId::None;

    waitingForShortcut = false;
    partyAddingPlayer = false;

    MinecraftDataProvider::SetHudMenuOpen(true);   // before the focus moves (see TrackGameScreens in the DLL)
    ActivateOverlay();
}


void OpenHomeItem(HomeItem item)
{
    homeHover = HomeItem::None;
    switch (item)
    {
    case HomeItem::Mods:
        selectedCategory = MenuCategory::Feature;
        featureFilter = 0;
        featureScrollRow = 0;
        detailFeature = DetailFeature::None;
        modsTab = pinnedFeatures.empty() ? ModsTab::Visual : ModsTab::Quick;
        break;
    case HomeItem::Hud: OpenHudEditor(); break;
    case HomeItem::Closet: selectedCategory = MenuCategory::Closet; break;
    case HomeItem::Settings: selectedCategory = MenuCategory::Settings; break;
    default: break;
    }
}


void CloseMenu(bool backToGame)
{
    menuOpen = false;
    UpdateGlassBlur(false);
    replaysOpen = false;
    replaySearching = false;
    replayRenaming.clear();
    searchActive = false;
    presetEditing = false;
    if (nickEditing)
    {
        nickEditing = false;
        if (nickText.empty())
            nickText = L"Nick";
        SaveConfig();
    }
    hudEditMode = false;

    detailFeature =
        DetailFeature::None;

    selectedHud =
        HudElementId::None;

    draggingHud =
        HudElementId::None;

    waitingForShortcut = false;
    partyAddingPlayer = false;

    SaveConfig();

    DeactivateOverlay(backToGame);
    MinecraftDataProvider::SetHudMenuOpen(false);   // the DLL closes the pause screen opening the menu caused
}


// ------------------------- Window Procedure -----------------

// HUD editor toolbar click. Returns true when the click was on the toolbar (and so is consumed).
//   Done  = finish editing and go back to the game (same result as pressing G)
//   Menu  = back to the normal menu
//   Reset = HUD layout back to the defaults
bool HandleEditorClick(int mx, int my)
{
    if (!menuOpen || !hudEditMode)
        return false;

    int tx, ty, tw, th;

    if (!EditorToolbarRect(tx, ty, tw, th))
        return false;

    if (mx < tx || mx >= tx + tw || my < ty || my >= ty + th)
        return false;

    RECT reset, menu, done;
    EditorButtonRects(tx, ty, tw, reset, menu, done);

    const POINT p{mx, my};

    if (PtInRect(&done, p))
        CloseMenu();
    else if (PtInRect(&menu, p))
        OpenMenu();
    else if (PtInRect(&reset, p))
    {
        ResetHudLayout();
        editorNoticeUntil = GetTickCount64() + 1800;
    }

    if (overlayWindow)
        InvalidateRect(overlayWindow, nullptr, FALSE);

    return true;
}


// Adds the typed GameTag to the party list (Enter or the Add nametag button). False (and a beep)
// when the text is not a valid tag.
bool AddPartyPlayerFromInput()
{
    if (!IsValidPlayerTag(partyInput))
    {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    bool duplicate = false;
    for (const auto& p : partyPlayers)
        if (_wcsicmp(p.c_str(), partyInput.c_str()) == 0)
            duplicate = true;
    if (!duplicate && partyPlayers.size() < MyClientParty::MaxPlayers)
        partyPlayers.push_back(partyInput);
    partyInput.clear();
    partyAddingPlayer = false;
    SaveConfig();
    return true;
}

// Switching the overlay between the colour key and per-pixel alpha takes WS_EX_LAYERED off and on again.
// For that moment the window is an ordinary one and Windows showed its old contents (the magenta key colour
// and black) over the whole game: a pink-and-black flash when G opened the menu. An empty window region hides
// it only while the style changes: a window region also clips painting, so drawing the new picture while it
// was still empty left the window black with the closed menu on it. Instead a colour-keyed window stays fully
// transparent (alpha 0) until its first picture is painted (ShowKeyedPicture); a per-pixel alpha window shows
// nothing before its first UpdateLayeredWindow anyway.
bool overlayKeyHidden = false;

void Relayer(HWND hwnd, bool alpha)
{
    if (!hwnd) return;
    HRGN none = CreateRectRgn(0, 0, 0, 0);
    const bool hidden = none && SetWindowRgn(hwnd, none, FALSE);   // Windows owns the region now
    if (none && !hidden) DeleteObject(none);
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    overlayKeyHidden = !alpha;
    if (!alpha)
        SetLayeredWindowAttributes(hwnd, RGB(255,0,255), 0, LWA_COLORKEY | LWA_ALPHA);
    if (hidden) SetWindowRgn(hwnd, nullptr, FALSE);
}

void ShowKeyedPicture(HWND hwnd)
{
    if (!hwnd || !overlayKeyHidden) return;
    overlayKeyHidden = false;
    SetLayeredWindowAttributes(hwnd, RGB(255,0,255), 255, LWA_COLORKEY);
}

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;


    case WM_PAINT:
    {
        PAINTSTRUCT ps;

        HDC dc =
            BeginPaint(
                hwnd,
                &ps
            );

        RECT r;

        GetClientRect(
            hwnd,
            &r
        );

        int w = r.right;
        int h = r.bottom;

        // The back buffer is kept between paints (no per-frame allocation) and the window is only
        // blitted when the finished picture really differs from what is already on screen. A colour-keyed
        // layered window flickers when it is redrawn with identical content 60 times a second.
        static HDC back = nullptr;
        static HBITMAP dib = nullptr;
        static HBITMAP oldBitmap = nullptr;
        static uint32_t* bits = nullptr;
        static int bufferW = 0;
        static int bufferH = 0;
        static std::vector<uint32_t> shown;
        static bool shownValid = false;

        if (w > 0 && h > 0 && (!back || w != bufferW || h != bufferH))
        {
            if (back && oldBitmap)
                SelectObject(back, oldBitmap);

            if (dib)
                DeleteObject(dib);

            if (!back)
                back = CreateCompatibleDC(dc);

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h;   // top-down
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            void* pixels = nullptr;
            dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            bits = static_cast<uint32_t*>(pixels);
            bufferW = w;
            bufferH = h;
            shownValid = false;
            shown.clear();

            if (dib)
                oldBitmap = (HBITMAP)SelectObject(back, dib);
        }

        if (back && dib && bits)
        {
            HBRUSH bg =
                CreateSolidBrush(
                    RGB(255,0,255)
                );

            FillRect(
                back,
                &r,
                bg
            );

            DeleteObject(bg);

            smoothDc = back; smoothBits = bits; smoothW = bufferW; smoothH = bufferH;
            translucentRects.clear();
            glassShapes.clear();
            textInks.clear();

            DrawChunkBorders(back, w, h);
            DrawHitboxes(back, w, h);
            DrawHud(back);
            DrawDebugScreen(back, w, h);
            DrawToasts(back, w, h);
            DrawShulkerHover(back, w, h);
            DrawHomeReplaysButton(back);
            DrawReplayPlaybackBar(back);

            if (menuOpen)
            {
                DrawMenu(back);
            }
            UpdateGlassBlur(menuOpen && GlassPageShown());

            GdiFlush();

            const size_t count = (size_t)bufferW * (size_t)bufferH;
            // Only the part of the picture that changed goes to the screen. Sending the whole window every
            // frame (8 MB at 1080p) made the desktop compositor copy it each time and slowed the game down.
            RECT dirty{0, 0, w, h};
            bool same = false;
            if (shownValid && shown.size() == count)
            {
                int top = -1, bottom = -1;
                for (int y = 0; y < bufferH; y++)
                    if (std::memcmp(&shown[(size_t)y * bufferW], bits + (size_t)y * bufferW, (size_t)bufferW * sizeof(uint32_t)) != 0)
                    {
                        if (top < 0) top = y;
                        bottom = y;
                    }
                same = top < 0;
                if (!same)
                {
                    int left = bufferW, right = -1;
                    for (int y = top; y <= bottom; y++)
                    {
                        const uint32_t* a = &shown[(size_t)y * bufferW];
                        const uint32_t* b = bits + (size_t)y * bufferW;
                        int x = 0;
                        while (x < left && a[x] == b[x]) x++;
                        if (x < left) left = x;
                        int r = bufferW - 1;
                        while (r > right && a[r] == b[r]) r--;
                        if (r > right) right = r;
                    }
                    dirty = RECT{left, top, right + 1, bottom + 1};
                }
            }
            // Health Warning: the glow is added after Convert (not in `shown`), so a change of it alone sends it all.
            const uint8_t healthGlow = overlayAlphaError ? 0 : HealthWarningAlpha();
            static uint8_t shownHealthGlow = 0;
            static COLORREF shownHealthColor = 0;
            if (healthGlow != shownHealthGlow || (healthGlow && HealthWarningColor() != shownHealthColor))
            {
                shownHealthGlow = healthGlow;
                shownHealthColor = HealthWarningColor();
                same = false;
                dirty = RECT{0, 0, w, h};
            }
            const auto addHealthGlow = [&]()
            {
                if (healthGlow)
                {
                    const COLORREF c = HealthWarningColor();
                    OverlayAlpha::Vignette(bits, bufferW, bufferH, (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c), healthGlow,
                                           std::max(8, std::min(bufferW, bufferH) * 22 / 100));
                }
            };

            // In-game drawing: while playing (menu closed) the finished picture goes to the game DLL, which
            // draws it inside the game's own frame; this window then shows nothing at all. Any visible pixel
            // in a window on top of the game makes Windows compose the game and caps it at the refresh rate.
            static bool alphaActive = false;
            const bool composite = InGameDrawingUsable(w, h);
            if (composite)
            {
                MyClientHudFrame::Header* frame = hudFrame.Data();
                if (!inGameDrawing || !same || !shownValid)
                {
                    shown.assign(bits, bits + count);
                    shownValid = true;
                    OverlayAlpha::Convert(bits, bufferW, bufferH, translucentRects);
                    OverlayAlpha::SoftenInk(bits, bufferW, bufferH, translucentRects, textInks);
                    OverlayAlpha::SmoothGlass(bits, bufferW, bufferH, glassShapes);
                    addHealthGlow();
                    const RECT rows = inGameDrawing ? dirty : RECT{0, 0, w, h};
                    MyClientHudFrame::Write(frame, bits, (uint32_t)w, (uint32_t)h, (uint32_t)rows.top, (uint32_t)rows.bottom, GetTickCount64());
                }
                else
                    frame->hudAt = GetTickCount64();   // unchanged picture: keep it alive
                if (!inGameDrawing)
                {
                    // Blank the window once (colour key everywhere) and leave it alone while in game drawing.
                    const bool relayered = alphaActive;
                    if (alphaActive)
                    {
                        alphaActive = false;
                        Relayer(hwnd, false);
                        overlayAlphaShown = false;
                    }
                    // After a relayer the paint DC still belongs to the old surface: draw on a fresh one.
                    HDC out = relayered ? GetDC(hwnd) : dc;
                    HBRUSH key = CreateSolidBrush(RGB(255,0,255));
                    FillRect(out ? out : dc, &r, key);
                    DeleteObject(key);
                    if (relayered && out) ReleaseDC(hwnd, out);
                    ShowKeyedPicture(hwnd);
                    inGameDrawing = true;
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            if (inGameDrawing)
            {
                // Back to the window (menu opened, editor, left the world, DLL gone): stop the game drawing
                // the old picture and repaint everything here.
                if (hudFrame.Data()) hudFrame.Data()->hudAt = 0;
                inGameDrawing = false;
                shownValid = false;
                same = false;
                dirty = RECT{0, 0, w, h};
            }

            // Display method: the colour key (light, as always) or, while a translucent box is on,
            // per-pixel alpha. Switching resets the layered style, which the two methods do not share.
            const bool alpha = OverlayAlphaWanted();
            bool relayered = false;
            if (alpha != alphaActive)
            {
                alphaActive = alpha;
                Relayer(hwnd, alpha);
                relayered = true;
                overlayAlphaShown = false;
                shownValid = false;
                same = false;
                dirty = RECT{0, 0, w, h};   // the re-layered window holds nothing yet: send the whole picture
            }

            if (!same || !shownValid)
            {
                shown.assign(bits, bits + count);
                shownValid = true;

                if (alpha)
                {
                    OverlayAlpha::Convert(bits, bufferW, bufferH, translucentRects);
                    OverlayAlpha::SoftenInk(bits, bufferW, bufferH, translucentRects, textInks);
                    OverlayAlpha::SmoothGlass(bits, bufferW, bufferH, glassShapes);
                    addHealthGlow();
                    RECT wr{};
                    GetWindowRect(hwnd, &wr);
                    POINT dst{wr.left, wr.top}, src{0, 0};
                    SIZE size{w, h};
                    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                    HDC screen = GetDC(nullptr);
                    UPDATELAYEREDWINDOWINFO info{sizeof(info), screen, &dst, &size, back, &src, 0, &blend, ULW_ALPHA, &dirty};
                    BOOL ok = UpdateLayeredWindowIndirect(hwnd, &info);
                    if (!ok && overlayAlphaShown)   // a partial update can be refused: send the whole picture
                        ok = UpdateLayeredWindow(hwnd, screen, &dst, &size, back, &src, 0, &blend, ULW_ALPHA);
                    const DWORD error = ok ? 0 : GetLastError();
                    ReleaseDC(nullptr, screen);
                    overlayAlphaShown = ok != FALSE;
                    if (!ok)
                    {
                        // Back to the colour key at once (an unconfirmed layered window shows nothing).
                        overlayAlphaError = error ? error : 1;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
                else
                {
                    HDC out = relayered ? GetDC(hwnd) : dc;   // see the in-game drawing branch
                    BitBlt(out ? out : dc, dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top,
                           back, dirty.left, dirty.top, SRCCOPY);
                    if (relayered && out) ReleaseDC(hwnd, out);
                }
            }
            ShowKeyedPicture(hwnd);
        }


        EndPaint(
            hwnd,
            &ps
        );

        return 0;
    }


    case WM_NCHITTEST:
        if (!menuOpen)
            return HTTRANSPARENT;

        return HTCLIENT;


    case WM_KEYDOWN:
        if (menuOpen &&
            hudEditMode)
        {
            const int vk = (int)wParam;

            if (vk == VK_ESCAPE ||
                vk == VK_RETURN)
            {
                CloseMenu();

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );
            }

            return 0;
        }

        if (menuOpen && replaysOpen)
        {
            const int vk = (int)wParam;
            if (!replayRenaming.empty())
            {
                if (vk == VK_RETURN) FinishReplayRename(true);
                else if (vk == VK_ESCAPE) FinishReplayRename(false);
            }
            else if (replaySearching && (vk == VK_RETURN || vk == VK_ESCAPE))
                replaySearching = false;
            else if (vk == VK_ESCAPE)
                CloseReplays();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen && presetEditing)
        {
            const int vk = (int)wParam;
            if (vk == VK_RETURN) { presetEditing = false; SavePresetFromBox(); InvalidateRect(hwnd, nullptr, FALSE); }
            else if (vk == VK_ESCAPE) { presetEditing = false; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        }

        if (menuOpen &&
            nickEditing)
        {
            const int vk = (int)wParam;

            if (vk == VK_ESCAPE ||
                vk == VK_RETURN)
            {
                nickEditing = false;
                if (nickText.empty())
                    nickText = L"Nick";
                SaveConfig();
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            return 0;
        }

        if (menuOpen &&
            searchActive)
        {
            const int vk = (int)wParam;

            if (vk == VK_ESCAPE ||
                vk == VK_RETURN)
            {
                searchActive = false;

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );
            }

            return 0;
        }

        if (menuOpen &&
            waitingForShortcut)
        {
            int vk =
                (int)wParam;

            if (vk == VK_ESCAPE)
            {
                waitingForShortcut = false;

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            // Menu Open's key: any key but Esc (Delete / Backspace put G back); it then stops being a module's key.
            const bool settingMenuKey = shortcutFeature == DetailFeature::MenuOpen;

            if (vk != 0 &&
                !settingMenuKey &&
                IsReservedShortcut(vk))
            {
                MessageBeep(
                    MB_ICONWARNING
                );

                return 0;
            }

            int* target =
                GetShortcutPtr(
                    shortcutFeature != DetailFeature::None
                        ? shortcutFeature
                        : detailFeature
                );

            if (target &&
                (vk == VK_DELETE || vk == VK_BACK))
            {
                vk = settingMenuKey ? 'G' : 0;   // clear: "None" (the menu key: back to G)
            }

            if (target)
            {
                if (settingMenuKey)
                    for (int i = 0; i < kFeatureCount; i++)
                        if (kFeatureList[i] != DetailFeature::MenuOpen)
                            if (int* other = GetShortcutPtr(kFeatureList[i]); other && *other == vk)
                                *other = 0;

                *target = vk;
                previousGState = true;   // the key still held must not toggle the menu once it is the menu key

                SaveConfig();

                waitingForShortcut = false;

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );
            }

            return 0;
        }

        // ESC: a module's settings -> the module list -> the home screen -> closed.
        if (menuOpen && !hudEditMode && !replaysOpen && (int)wParam == VK_ESCAPE)
        {
            if (selectedCategory == MenuCategory::Home)
                CloseMenu();
            else if (selectedCategory == MenuCategory::Feature && detailFeature != DetailFeature::None)
                detailFeature = DetailFeature::None;
            else
            {
                selectedCategory = MenuCategory::Home;
                homeHover = HomeItem::None;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        break;


    case WM_CHAR:
        if (menuOpen && replaysOpen && (replaySearching || !replayRenaming.empty()))
        {
            std::wstring& text = replayRenaming.empty() ? replaySearchText : replayRenameText;
            const wchar_t c = (wchar_t)wParam;
            if (c == L'\b') { if (!text.empty()) text.pop_back(); }
            else if (c >= 32 && c != 127 && text.size() < 40) text.push_back(c);
            if (replayRenaming.empty()) replayScroll = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen && presetEditing)
        {
            const wchar_t c = (wchar_t)wParam;
            if (c == L'\b') { if (!presetNameText.empty()) presetNameText.pop_back(); }
            else if (c >= 32 && c != 127 && presetNameText.size() < 24) presetNameText.push_back(c);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen &&
            nickEditing)
        {
            const wchar_t c = (wchar_t)wParam;

            if (c == L'\b')
            {
                if (!nickText.empty())
                    nickText.pop_back();
            }
            else if (c >= 32 && c != 127 && nickText.size() < 16)
            {
                nickText.push_back(c);
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen &&
            searchActive)
        {
            const wchar_t c =
                (wchar_t)wParam;

            if (c == L'\b')
            {
                if (!searchQuery.empty())
                    searchQuery.pop_back();
            }
            else if (c >= 32 &&
                     c != 127 &&
                     searchQuery.size() < 24)
            {
                searchQuery.push_back(c);
            }

            featureScrollRow = 0;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        if (menuOpen &&
            partyAddingPlayer)
        {
            wchar_t c =
                (wchar_t)wParam;

            if (c == L'\r')
            {
                AddPartyPlayerFromInput();

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (c == L'\b')
            {
                if (!partyInput.empty())
                {
                    partyInput.pop_back();
                }

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (c >= 32 &&
                c != 127 &&
                partyInput.size() < 32)
            {
                partyInput.push_back(c);

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }
        }

        break;


    case WM_LBUTTONDOWN:
    {
        if (!menuOpen)
            return 0;

        presetEditing = false;   // a click anywhere ends typing; the name box turns it back on
        if (nickEditing)
        {
            // A click anywhere ends the edit; the Nick box re-enables it when it is the target.
            nickEditing = false;
            if (nickText.empty())
                nickText = L"Nick";
            SaveConfig();
        }

        int mx =
            LOWORD(lParam);

        int my =
            HIWORD(lParam);

        if (menuSoundOpen && mx < 400)
            menuSoundOpen = false;   // clicking the module list closes the sound list


        if (hudEditMode)
        {
            if (HandleEditorClick(mx, my))
                return 0;

            HudElementId hit =
                GetHudAt(
                    mx,
                    my
                );

            if (hit !=
                HudElementId::None)
            {
                selectedHud = hit;
                draggingHud = hit;

                HudElement* e =
                    GetHudElement(hit);

                if (e)
                {
                    dragOffset.x =
                        mx - e->x;

                    dragOffset.y =
                        my - e->y;
                }

                SetCapture(hwnd);
            }
            else
            {
                selectedHud =
                    HudElementId::None;

                draggingHud =
                    HudElementId::None;
            }

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        if (selectedCategory == MenuCategory::Home && !replaysOpen)
        {
            // A module chip opens that module's settings page.
            if (overlayWindow)
            {
                const int chip = HomeChipAt(mx, my);
                HDC dc = chip >= 0 ? GetDC(overlayWindow) : nullptr;
                const std::vector<HomeChip> chips = dc ? HomeChips(dc) : std::vector<HomeChip>{};
                if (dc) ReleaseDC(overlayWindow, dc);
                if (chip >= 0 && chip < (int)chips.size())
                {
                    homeHover = HomeItem::None;
                    homeChipHover = -1;
                    selectedCategory = MenuCategory::Feature;
                    featureFilter = 0;
                    detailFeature = chips[chip].more ? DetailFeature::None : chips[chip].feature;   // "+N more": the module grid
                    if (detailFeature == DetailFeature::FrameGen)   // its settings are in Optimization's page
                    {
                        detailFeature = DetailFeature::Lite;
                        glassToFrameGen = true;
                    }
                    waitingForShortcut = false;
                    partyAddingPlayer = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            const HomeItem item = HomeItemAt(mx, my);
            if (item != HomeItem::None)
                OpenHomeItem(item);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (GlassModsShown())
        {
            HandleGlassClick(mx, my, false);
            if (searchActive)
                SetFocus(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (GlassClosetShown())
        {
            if (!ClosetStartSpin(hwnd, mx, my))
                HandleClosetClick(mx, my);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (GlassContentShown())
        {
            HandleGlassRowsClick(hwnd, mx, my, false);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Settings / Presets / a module's page: the glass frame first; the old sidebar's buttons are gone.
        const bool glassPage = GlassContentShown();
        if (glassPage && HandleGlassFrameClick(GetGlassLayout(), mx, my))
        {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }


        {
            int ox, oy;

            MenuOffset(ox, oy);

            mx -= ox;
            my -= oy;
        }

        if (replaysOpen)
        {
            HandleReplaysClick(mx, my);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // The logo (top of the sidebar) goes back to the home screen.
        if (!glassPage && IsInside(mx, my, 14, 14, 160, 60))
        {
            selectedCategory = MenuCategory::Home;
            detailFeature = DetailFeature::None;
            homeHover = HomeItemAt(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }


        {
            const bool inSearch =
                selectedCategory ==
                    MenuCategory::Feature &&
                FeatureGridMode() &&
                IsInside(mx, my, kSearchRect.left, kSearchRect.top,
                         kSearchRect.right - kSearchRect.left,
                         kSearchRect.bottom - kSearchRect.top);

            searchActive = inSearch;
            if (nickEditing)
            {
                nickEditing = false;
                if (nickText.empty())
                    nickText = L"Nick";
                SaveConfig();
            }

            if (inSearch)
            {
                waitingForShortcut = false;
                partyAddingPlayer = false;

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }
        }


        // Sidebar: module filters
        for (int i = 0; i < (glassPage ? 0 : 1); i++)
        {
            const RECT r = SideItemRect(i);

            if (!IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                continue;

            selectedCategory =
                MenuCategory::Feature;

            detailFeature =
                DetailFeature::None;

            featureFilter = i;
            featureScrollRow = 0;

            waitingForShortcut = false;
            partyAddingPlayer = false;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }


        {
            const RECT r = SideItemRect(1);
            if (!glassPage && IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
            {
                selectedCategory = MenuCategory::Presets;
                detailFeature = DetailFeature::None;
                waitingForShortcut = false;
                partyAddingPlayer = false;
                presetStatus.clear();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        if (selectedCategory == MenuCategory::Presets && HandlePresetsClick(hwnd, mx, my))
        {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (!glassPage && IsInside(mx, my, kEditHudRect.left, kEditHudRect.top,
                     kEditHudRect.right - kEditHudRect.left,
                     kEditHudRect.bottom - kEditHudRect.top))
        {
            OpenHudEditor();

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }


        if (!glassPage && IsInside(mx, my, kClientSettingsRect.left, kClientSettingsRect.top,
                     kClientSettingsRect.right - kClientSettingsRect.left,
                     kClientSettingsRect.bottom - kClientSettingsRect.top))
        {
            selectedCategory =
                MenuCategory::Settings;

            detailFeature =
                DetailFeature::None;

            waitingForShortcut = false;
            partyAddingPlayer = false;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }


        if (selectedCategory ==
            MenuCategory::Feature)
        {
            EnsureDetailSelected();

            if (!FeatureGridMode())
            {
                // Settings page: the page was drawn shifted left; bring the mouse back to its layout.
                mx += kDetailShift;

                if (HandleDetailListClick(mx, my))
                {
                    if (searchActive)
                        SetFocus(hwnd);

                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            const std::vector<int> visible =
                VisibleFeatures();

            for (int pos = 0;
                 FeatureGridMode() &&
                 pos < (int)visible.size();
                 pos++)
            {
                const int i = visible[pos];

                if (!FeatureCardOnScreen(pos))
                    continue;

                int cx, cy, cw, ch;
                FeatureCardRect(pos, cx, cy, cw, ch);

                if (!IsInside(mx, my, cx, cy, cw, ch))
                    continue;

                int kx, ky, kw, kh;
                FeatureKeyRect(pos, kx, ky, kw, kh);

                if (IsInside(mx, my, kx, ky, kw, kh))
                {
                    // click the badge, then press the key (Delete = None, Esc = cancel)
                    const bool same =
                        waitingForShortcut &&
                        shortcutFeature == kFeatureList[i];

                    waitingForShortcut = !same;
                    shortcutFeature = kFeatureList[i];

                    InvalidateRect(
                        hwnd,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }

                waitingForShortcut = false;

                int gx, gy, gw, gh;
                FeatureGearRect(pos, gx, gy, gw, gh);

                if (FeatureGridMode() &&
                    IsInside(mx, my, gx, gy, gw, gh))
                {
                    detailFeature =
                        kFeatureList[i];

                    waitingForShortcut = false;
                    partyAddingPlayer = false;
                }
                else
                {
                    ToggleFeature(
                        kFeatureList[i]
                    );
                }

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }


            {
                const RECT key = HiveKeyRect();

                if (IsInside(mx, my, key.left, key.top, key.right - key.left, key.bottom - key.top))
                {
                    waitingForShortcut = true;
                    shortcutFeature = ActiveDetail();
                    SetFocus(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (ActiveDetail() ==
                DetailFeature::PartyAutoAccept)
            {
                int listY =
                    70 + 108 + 34 + 12;   // below the name box

                for (
                    size_t i = 0;
                    i < partyPlayers.size() &&
                    i < 6;
                    i++)
                {
                    int yy =
                        listY +
                        (int)i * 34;

                    if (IsInside(
                            mx,
                            my,
                            kPaneX + 238,
                            yy,
                            28,
                            28))
                    {
                        if (!TrashConfirm(L"party:" + partyPlayers[i]))
                        {
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }

                        partyPlayers.erase(
                            partyPlayers.begin() +
                            i
                        );

                        SaveConfig();

                        InvalidateRect(
                            hwnd,
                            nullptr,
                            FALSE
                        );

                        return 0;
                    }
                }


                const RECT box = PartyInputRect(), add = PartyAddRect();

                if (IsInside(mx, my, add.left, add.top, add.right - add.left, add.bottom - add.top))
                {
                    if (partyInput.empty())
                    {
                        partyAddingPlayer = true;   // nothing typed yet: start typing in the box
                        SetFocus(hwnd);
                    }
                    else
                        AddPartyPlayerFromInput();

                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                if (IsInside(mx, my, box.left, box.top, box.right - box.left, box.bottom - box.top))
                {
                    partyAddingPlayer = true;

                    SetFocus(hwnd);

                    InvalidateRect(
                        hwnd,
                        nullptr,
                        FALSE
                    );

                    return 0;
                }
            }
            else if (
                detailFeature !=
                DetailFeature::None)
            {
                if (detailFeature == DetailFeature::Resources || detailFeature == DetailFeature::Arrows)
                {
                    const bool resources = detailFeature == DetailFeature::Resources;
                    const RECT r = CounterBackgroundRect(), t = CounterTranslucentRect();
                    if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top) ||
                        IsInside(mx, my, t.left, t.top, t.right - t.left, t.bottom - t.top))
                    {
                        const bool see = IsInside(mx, my, t.left, t.top, t.right - t.left, t.bottom - t.top);
                        bool& option = see ? (resources ? resourceTranslucent : arrowTranslucent)
                                           : (resources ? resourceBackground : arrowBackground);
                        option = !option;
                        if (see && option)   // a translucent box needs the box itself
                            (resources ? resourceBackground : arrowBackground) = true;
                        SaveConfig();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }

                if (detailFeature == DetailFeature::FrameGen)
                {
                    for (int i = 0; i < 4; i++)
                    {
                        const RECT r = MenuSoundRect(i);
                        if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                        {
                            if (i == 0) frameGenMultiplier = frameGenMultiplier >= 3 ? 2 : 3;
                            else if (i == 3) frameGenPacing = !frameGenPacing;
                            else frameGenMethod = frameGenMethod == i ? 0 : i;   // 1 blend / 2 copy; both off = motion
                            SaveConfig();
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                }

                if (detailFeature == DetailFeature::HitEffect)
                {
                    for (int i = 0; i < 2; i++)
                    {
                        const RECT r = MenuSoundRect(i);
                        if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                        {
                            bool& option = i == 0 ? hitEffectCrit : hitEffectMagic;
                            option = !option;
                            if (i == 1 && hitEffectEnabled) ApplyLite();   // adds / removes the enchanted-hit particle
                            SaveConfig();
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                }

                if (detailFeature == DetailFeature::Armor)
                {
                    for (int i = 0; i < 2; i++)
                    {
                        const RECT r = MenuSoundRect(i);
                        if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                        {
                            bool& option = i == 0 ? armorShowDurability : armorHorizontal;
                            option = !option;
                            SaveConfig();
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                }

                if (detailFeature == DetailFeature::Lite && HandleLiteClick(hwnd, mx, my))
                    return 0;

                if (detailFeature == DetailFeature::MenuSounds)
                {
                    const RECT head = MenuSoundRect(0);
                    if (IsInside(mx, my, head.left, head.top, head.right - head.left, head.bottom - head.top))
                    {
                        menuSoundOpen = !menuSoundOpen;
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }

                    if (menuSoundOpen)
                    {
                        menuSoundOpen = false;   // any other click closes the list
                        for (int i = 0; i < kMenuSoundCount; i++)
                        {
                            const RECT r = MenuSoundOptionRect(i);
                            if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                            {
                                toggleSoundMode = kMenuSoundOrder[i];
                                SaveConfig();
                                if (menuSoundsEnabled)
                                    PlayToggleSound(true);   // preview the sound that was just picked
                                break;
                            }
                        }
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }

                if (detailFeature == DetailFeature::Replay)
                {
                    const RECT rp = ReplaysButtonRect();
                    if (IsInside(mx, my, rp.left, rp.top, rp.right - rp.left, rp.bottom - rp.top))
                    {
                        OpenReplays(false);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }

                    const RECT o = ReplayOpenRect();
                    if (IsInside(mx, my, o.left, o.top, o.right - o.left, o.bottom - o.top))
                    {
                        std::wstring dir;
                        wchar_t base[MAX_PATH]{};
                        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
                        MinecraftDataProvider::ReplayStatus st{};
                        if (MinecraftDataProvider::GetReplayStatus(st) && st.path[0])
                        {
                            dir = Utf8ToWide(st.path);
                            const size_t cut = dir.find_last_of(L"\\/");
                            if (cut != std::wstring::npos)
                                dir.resize(cut);
                        }
                        else if (n > 0 && n < MAX_PATH)
                            dir = std::wstring(base) + L"\\MyClient\\Replays";
                        if (!dir.empty())
                        {
                            CreateDirectoryW((std::wstring(base) + L"\\MyClient").c_str(), nullptr);
                            CreateDirectoryW(dir.c_str(), nullptr);
                            ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        }
                        return 0;
                    }
                }

                if (detailFeature == DetailFeature::Nick)
                {
                    const RECT f = NickFieldRect();
                    if (IsInside(mx, my, f.left, f.top, f.right - f.left, f.bottom - f.top))
                    {
                        nickEditing = true;
                        SetFocus(hwnd);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }

                if (detailFeature == DetailFeature::Hitbox)
                {
                    for (int i = 0; i < 4; i++)
                    {
                        const RECT r = HitboxFilterRect(i);
                        if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                        {
                            hitboxFilter ^= (1 << i);
                            SaveConfig();
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                }

                if (detailFeature == DetailFeature::ChunkBorder)
                {
                    for (int i = 0; i < 3; i++)
                    {
                        const RECT r = ChunkBorderRadiusRect(i);
                        if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                        {
                            chunkBorderRadius = i + 1;
                            SaveConfig();
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                }

                if (ActiveDetail() ==
                    DetailFeature::HiveStats)
                {
                    {
                        const RECT r = HiveStatsAutoRect();
                        if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                        {
                            hiveStatsAuto = !hiveStatsAuto;
                            hiveFetcher.Clear();
                            SaveConfig();
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }

                    for (int i = 0; i < HiveStats::kGameCount; i++)
                    {
                        const RECT r = HiveStatsGameRect(i);

                        if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                        {
                            hiveStatsGame = i;
                            hiveStatsAuto = false;   // picking a game by hand turns auto off
                            hiveFetcher.Clear();   // stats differ per game
                            SaveConfig();
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                }

                if (ActiveDetail() ==
                    DetailFeature::AutoRq)
                {
                    for (int i = 0; i < (int)MyClientAutoRq::kModeCount; i++)
                    {
                        const RECT r = AutoRqModeRect(i);

                        if (IsInside(mx, my, r.left, r.top, r.right - r.left, r.bottom - r.top))
                        {
                            autoRqMode = i;
                            SaveConfig();
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                }

                if (detailFeature ==
                    DetailFeature::Textures)
                {
                    const POINT p{mx, my};

                    if (PtInRect(&kPackInboxRect, p))
                    {
                        std::error_code ec;
                        std::filesystem::create_directories(PackInboxFolder(), ec);
                        ShellExecuteW(nullptr, L"open", PackInboxFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        texStatus = L"Put your .mcpack / .mcaddon / .zip files in the folder, then press Install all.";
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }

                    if (PtInRect(&kPackInstallRect, p))
                    {
                        InstallInboxPacks();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }

                    if (PtInRect(&kTexRefreshRect, p))
                    {
                        RefreshTextures();

                        InvalidateRect(hwnd, nullptr, FALSE);

                        return 0;
                    }

                    // Open subpack list: a click on an entry picks it (applied with Done, like any change);
                    // a click anywhere else just closes the list.
                    if (!texSubpackFor.empty())
                    {
                        const int listTotal = (int)texState.packs.size() + 1;
                        for (int r = 0; r < kTexRowsVisible; r++)
                        {
                            const int index = texScroll + r;
                            if (index < 1 || index >= listTotal) continue;
                            const TexturePacks::Pack pack = TexRowPack(index);
                            if (pack.folder.wstring() != texSubpackFor) continue;
                            for (int i = 0; i < std::min((int)pack.subpacks.size(), kTexSubpackMax); i++)
                            {
                                const RECT it = TexSubpackItemRect(r, i);
                                if (!PtInRect(&it, p)) continue;
                                if (!texEditing)
                                    texPendingStack = texState.stack;
                                int pos = TexStackPos(texPendingStack, pack.uuid);
                                if (pos < 0)
                                {
                                    // Picking a subpack of a pack not in use adds the pack on top.
                                    texPendingStack.insert(texPendingStack.begin(), TexturePacks::EntryFor(pack));
                                    pos = 0;
                                }
                                texPendingStack[pos] = TexturePacks::WithSubpack(texPendingStack[pos], pack.subpacks[i].folder);
                                UpdatePendingStatus();
                                if (texEditing)
                                    texStatus = L"Subpack: " + TexSubpackLabel(pack, pack.subpacks[i].folder) + L" - click Done to use it.";
                            }
                        }
                        texSubpackFor.clear();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }

                    if (texEditing)
                    {
                        const RECT done = TexDoneRect(), cancel = TexCancelRect();
                        if (PtInRect(&done, p) || PtInRect(&cancel, p))
                        {
                            if (PtInRect(&done, p))
                                ApplyPendingTextures();
                            else
                            {
                                texEditing = false;
                                texPendingStack.clear();
                                texStatus = L"Changes cancelled.";
                            }
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }

                    const int total = (int)texState.packs.size() + 1;

                    for (int r = 0; r < kTexRowsVisible; r++)
                    {
                        const int index = texScroll + r;

                        if (index >= total)
                            break;

                        if (!IsInside(mx, my, 535, kTexListTop + r * kTexRowPitch, 496, kTexRowHeight))
                            continue;

                        if (!texEditing)
                            texPendingStack = texState.stack;

                        if (index > 0)
                        {
                            const TexturePacks::Pack pack = TexRowPack(index);
                            const RECT gear = TexGearRect(r);
                            if (!pack.subpacks.empty() && PtInRect(&gear, p))
                            {
                                texSubpackFor = pack.folder.wstring();
                                InvalidateRect(hwnd, nullptr, FALSE);
                                return 0;
                            }
                            const bool armed = TexDeleteArmed(pack);
                            const RECT del = TexDeleteRect(r, armed);
                            if (PtInRect(&del, p))
                            {
                                if (armed)
                                {
                                    texDeleteArmed.clear();
                                    DeleteTexturePack(pack);
                                }
                                else
                                {
                                    texDeleteArmed = pack.folder.wstring();
                                    texDeleteArmedAt = GetTickCount64();
                                    texStatus = L"Click the trash again to remove " + Utf8ToWide(pack.name) + L".";
                                }
                                InvalidateRect(hwnd, nullptr, FALSE);
                                return 0;
                            }

                            // Priority arrows on packs in the stack
                            const int pos = TexStackPos(texPendingStack, pack.uuid);
                            if (pos >= 0 && texPendingStack.size() > 1)
                                for (const bool up : {true, false})
                                {
                                    const RECT m = TexMoveRect(r, up);
                                    if (!PtInRect(&m, p))
                                        continue;
                                    const int to = up ? pos - 1 : pos + 1;
                                    if (to >= 0 && to < (int)texPendingStack.size())
                                    {
                                        std::swap(texPendingStack[pos], texPendingStack[to]);
                                        UpdatePendingStatus();
                                    }
                                    InvalidateRect(hwnd, nullptr, FALSE);
                                    return 0;
                                }
                        }

                        // Click a row: add the pack on top of the stack / take it out.
                        // "Default textures" empties the stack.
                        texDeleteArmed.clear();
                        if (index == 0)
                            texPendingStack.clear();
                        else
                        {
                            const TexturePacks::Pack pack = TexRowPack(index);
                            const int pos = TexStackPos(texPendingStack, pack.uuid);
                            if (pos >= 0)
                                texPendingStack.erase(texPendingStack.begin() + pos);
                            else
                                texPendingStack.insert(texPendingStack.begin(), TexturePacks::EntryFor(pack));
                        }
                        UpdatePendingStatus();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }

                if (detailFeature ==
                    DetailFeature::BlockOutline)
                {
                    for (int i = 0; i < 12; i++)
                    {
                        const RECT r = OutlineSwatchRect(i);

                        if (IsInside(
                                mx,
                                my,
                                r.left,
                                r.top,
                                r.right - r.left,
                                r.bottom - r.top))
                        {
                            blockOutlineSettings.color =
                                kOutlinePalette[i];

                            SaveConfig();

                            InvalidateRect(
                                hwnd,
                                nullptr,
                                FALSE
                            );

                            return 0;
                        }
                    }
                }
            }
        }


        if (selectedCategory ==
            MenuCategory::Settings)
        {
            int x = 190;
            int y = 70;

            if (IsInside(
                    mx,
                    my,
                    x,
                    y + 145,
                    260,
                    42))
            {
                guiScale =
                    std::min(
                        1.5f,
                        guiScale + 0.05f
                    );

                SaveConfig();

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (IsInside(
                    mx,
                    my,
                    x,
                    y + 195,
                    260,
                    42))
            {
                guiScale =
                    std::max(
                        0.75f,
                        guiScale - 0.05f
                    );

                SaveConfig();

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (IsInside(
                    mx,
                    my,
                    x,
                    y + 245,
                    260,
                    42))
            {
                menuAnimation =
                    !menuAnimation;

                SaveConfig();

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (IsInside(
                    mx,
                    my,
                    x,
                    y + 295,
                    260,
                    42))
            {
                ResetHudLayout();

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }
        }

        return 0;
    }


    case WM_RBUTTONDOWN:
    {
        if (menuOpen && GlassModsShown())
        {
            HandleGlassClick((short)LOWORD(lParam), (short)HIWORD(lParam), true);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen && GlassClosetShown())
        {
            if (ClosetResetSpin((short)LOWORD(lParam), (short)HIWORD(lParam)))
                InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen && GlassContentShown())
        {
            HandleGlassRowsClick(hwnd, (short)LOWORD(lParam), (short)HIWORD(lParam), true);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen &&
            !hudEditMode &&
            selectedCategory == MenuCategory::Feature)
        {
            EnsureDetailSelected();

            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);

            int ox, oy;
            MenuOffset(ox, oy);

            mx = mx - ox + kContentShift + kDetailShift;
            my -= oy;

            // Right-click a feature in the list: open its settings.
            if (HandleDetailListClick(mx, my, true))
                InvalidateRect(hwnd, nullptr, FALSE);
        }

        return 0;
    }


    case WM_MOUSEMOVE:
    {
        if (menuOpen && !hudEditMode && !replaysOpen && selectedCategory == MenuCategory::Home)
        {
            const HomeItem item = HomeItemAt((short)LOWORD(lParam), (short)HIWORD(lParam));
            const int chip = HomeChipAt((short)LOWORD(lParam), (short)HIWORD(lParam));
            if (item != homeHover || chip != homeChipHover)
            {
                homeHover = item;
                homeChipHover = chip;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        if (liteSliderDrag >= 0)
        {
            int ox, oy;
            MenuOffset(ox, oy);
            DragLiteSlider((short)LOWORD(lParam) - ox + kContentShift + kDetailShift, !(wParam & MK_LBUTTON));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (closetSpinning)
        {
            ClosetSpin((short)LOWORD(lParam), (short)HIWORD(lParam), !(wParam & MK_LBUTTON));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (glassSliderDrag >= 0)
        {
            DragGlassSlider((short)LOWORD(lParam), !(wParam & MK_LBUTTON));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (hudEditMode &&
            draggingHud !=
                HudElementId::None)
        {
            if (!(wParam & MK_LBUTTON))
            {
                draggingHud =
                    HudElementId::None;

                ReleaseCapture();

                return 0;
            }

            int mx =
                LOWORD(lParam);

            int my =
                HIWORD(lParam);

            HudElement* e =
                GetHudElement(
                    draggingHud
                );

            if (e)
            {
                e->x =
                    std::max(
                        0,
                        mx -
                            static_cast<int>(dragOffset.x)
                    );

                e->y =
                    std::max(
                        0,
                        my -
                            static_cast<int>(dragOffset.y)
                    );
            }

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        return 0;
    }


    case WM_LBUTTONUP:
        if (closetSpinning)
        {
            ClosetSpin((short)LOWORD(lParam), (short)HIWORD(lParam), true);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (glassSliderDrag >= 0)
        {
            DragGlassSlider((short)LOWORD(lParam), true);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (liteSliderDrag >= 0)
        {
            int ox, oy;
            MenuOffset(ox, oy);
            DragLiteSlider((short)LOWORD(lParam) - ox + kContentShift + kDetailShift, true);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (hudEditMode)
        {
            draggingHud =
                HudElementId::None;

            ReleaseCapture();

            SaveConfig();

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        return 0;


    case WM_MOUSEWHEEL:
    {
        if (menuOpen && !hudEditMode && replaysOpen)
        {
            replayScroll = std::max(0, replayScroll - (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen && GlassContentShown())
        {
            glassRowScroll -= GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;
            ClampGlassRowScroll(GlassSettings());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen && !hudEditMode && selectedCategory == MenuCategory::Closet)
        {
            closetScroll = std::clamp(closetScroll - (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1), 0, ClosetMaxScroll());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (menuOpen &&
            !hudEditMode &&
            selectedCategory ==
                MenuCategory::Feature &&
            detailFeature ==
                DetailFeature::None)
        {
            featureScrollRow -=
                GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;

            ClampGlassScroll();

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        if (menuOpen &&
            !hudEditMode &&
            selectedCategory ==
                MenuCategory::Feature &&
            detailFeature !=
                DetailFeature::None)
        {
            // Cursor over the feature list: scroll the list. Elsewhere: the Textures pack list.
            POINT cp{};
            GetCursorPos(&cp);
            ScreenToClient(hwnd, &cp);

            int ox, oy;
            MenuOffset(ox, oy);

            const int lx = cp.x - ox + kContentShift + kDetailShift;
            const int dir = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;

            if (lx < kPaneX - 8)
            {
                detailListScroll += dir;
                ClampDetailListScroll((int)BuildDetailRows().size());
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (detailFeature == DetailFeature::Lite)
            {
                liteScrollRow += dir;
                ClampLiteScroll();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        if (menuOpen &&
            !hudEditMode &&
            selectedCategory ==
                MenuCategory::Feature &&
            detailFeature ==
                DetailFeature::Textures)
        {
            texScroll -=
                GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        if (!hudEditMode ||
            selectedHud ==
                HudElementId::None)
        {
            return 0;
        }

        int delta =
            GET_WHEEL_DELTA_WPARAM(
                wParam
            );

        HudElement* e =
            GetHudElement(
                selectedHud
            );

        if (e)
        {
            e->scale +=
                delta > 0
                    ? 0.05f
                    : -0.05f;

            e->scale =
                std::clamp(
                    e->scale,
                    0.5f,
                    2.5f
                );

            SaveConfig();
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    case WM_DESTROY:
        ApplyProcessTweaks(true);   // the game's process gets Windows' defaults back
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam
    );
}


// ------------------------- Create overlay -------------------

LRESULT CALLBACK CatcherProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam
    );
}

void CreateOverlay()
{
    HINSTANCE instance =
        GetModuleHandleW(nullptr);

    WNDCLASSW wc{};

    wc.lpfnWndProc =
        WindowProc;

    wc.hInstance =
        instance;

    wc.lpszClassName =
        L"MyClientOverlay";

    wc.hIcon =
        LoadIconW(
            instance,
            MAKEINTRESOURCEW(1)
        );

    wc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW
        );

    RegisterClassW(
        &wc
    );


    overlayWindow =
        CreateWindowExW(
            WS_EX_LAYERED |
            WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE,

            L"MyClientOverlay",
            L"MyClient",
            WS_POPUP,

            0,
            0,
            100,
            100,

            nullptr,
            nullptr,
            instance,
            nullptr
        );


    SetLayeredWindowAttributes(
        overlayWindow,
        RGB(255,0,255),
        255,
        LWA_COLORKEY
    );


    ShowWindow(
        overlayWindow,
        SW_SHOWNOACTIVATE
    );

    CreateBlurWindow(instance);


    WNDCLASSW cc{};

    cc.lpfnWndProc =
        CatcherProc;

    cc.hInstance =
        instance;

    cc.lpszClassName =
        L"MyClientInputCatcher";

    cc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW
        );

    cc.hbrBackground =
        (HBRUSH)GetStockObject(
            BLACK_BRUSH
        );

    RegisterClassW(
        &cc
    );

    catcherWindow =
        CreateWindowExW(
            WS_EX_LAYERED |
            WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE |
            WS_EX_TOPMOST,

            L"MyClientInputCatcher",
            L"MyClientInput",
            WS_POPUP,

            0,
            0,
            100,
            100,

            nullptr,
            nullptr,
            instance,
            nullptr
        );

    if (catcherWindow)
    {
        // Alpha 1/255: invisible to the eye, but it still receives the mouse.
        SetLayeredWindowAttributes(
            catcherWindow,
            0,
            1,
            LWA_ALPHA
        );
    }
}


// ------------------------- Main ------------------------------

int WINAPI WinMain(
    HINSTANCE,
    HINSTANCE,
    LPSTR,
    int)
{
    // 1 ms timer resolution: the loop's Sleep(16) otherwise lasts 15.6 or 31.2 ms at random, so
    // animations (toast bars) step unevenly.
    timeBeginPeriod(1);
    HANDLE sessionMutex = nullptr;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    std::wstring argument = argc == 2 ? argv[1] : L"";
    LocalFree(argv);
    if (argc != 1 && argc != 2) return 1;
    if (argc == 2)
    {
        uint32_t pid = 0;
        if (!MyClientLauncher::ParsePidOption(argument, pid)) return 1;
        requestedMinecraftPid = pid;
        const auto mutexName = L"Local\\MyClient.Hud." + std::to_wstring(requestedMinecraftPid);
        sessionMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
        const DWORD error = GetLastError();
        if (!sessionMutex) return 1;
        if (error == ERROR_ALREADY_EXISTS) { CloseHandle(sessionMutex); return 0; }
    }
    struct SessionGuard {
        HANDLE handle;
        ~SessionGuard() { if (handle) CloseHandle(handle); }
    } sessionGuard{sessionMutex};
    LoadConfig();
    // The pack folders follow the game's data layout (TexturePacks::PackRoots): bring an ON pack up to date there.
    if (ClientPackWanted()) ApplyLite();

    CreateOverlay();

    MSG msg{};

    RECT previousMinecraftRect{};

    bool hasPreviousRect =
        false;


    bool prevKS = false;
    bool prevCPS = false;
    bool prevArmor = false;
    bool prevArrows = false;
    bool prevFPS = false;
    bool prevBlockOutline = false;
    bool prevExtra[26] = {};


    bool previousLeft = false;
    bool previousRight = false;
    bool providerInitialized = false;


    while (true)
    {
        while (PeekMessageW(
            &msg,
            nullptr,
            0,
            0,
            PM_REMOVE))
        {
            if (msg.message ==
                WM_QUIT)
            {
                HideMouseCursor();
                ReplayPlayer::Stop();
                MinecraftDataProvider::Shutdown();
                return 0;
            }

            TranslateMessage(
                &msg
            );

            DispatchMessageW(
                &msg
            );
        }


        minecraftWindow =
            FindMinecraft();

        if (minecraftWindow && !providerInitialized)
        {
            providerInitialized =
                MinecraftDataProvider::Initialize(
                    minecraftWindow
                );
        }


        if (!minecraftWindow)
        {
            if (menuOpen)
            {
                CloseMenu();
            }

            if (catcherWindow)
            {
                DestroyWindow(
                    catcherWindow
                );
            }

            if (overlayWindow)
            {
                DestroyWindow(
                    overlayWindow
                );
            }

            ReplayPlayer::Stop();
            MinecraftDataProvider::Shutdown();
            return 0;
        }

        MinecraftDataProvider::Tick(
            minecraftWindow
        );

        if (PollClosetAnswer() && menuOpen && overlayWindow)
            InvalidateRect(overlayWindow, nullptr, FALSE);


        RECT mr;

        GetWindowRect(
            minecraftWindow,
            &mr
        );


        // The overlay is topmost, so hide it whenever Minecraft is not the active
        // window (another app in front, or Minecraft minimized). The overlay itself
        // counts as active while the G menu is open.
        const HWND foregroundWindow = GetForegroundWindow();
        // Any window of the game or of this HUD counts (the IME's windows, the game's own popups...): such a
        // window taking the front for a moment closed the menu, and the game then opened its pause screen.
        DWORD foregroundPid = 0, minecraftPid = 0;
        if (foregroundWindow) GetWindowThreadProcessId(foregroundWindow, &foregroundPid);
        GetWindowThreadProcessId(minecraftWindow, &minecraftPid);
        const bool minecraftActive =
            !IsIconic(minecraftWindow) &&
            (foregroundWindow == minecraftWindow ||
             foregroundWindow == overlayWindow ||
             (catcherWindow &&
              foregroundWindow == catcherWindow) ||
             (foregroundPid != 0 && (foregroundPid == minecraftPid || foregroundPid == GetCurrentProcessId())));
        gameActive = minecraftActive;
        {
            const bool overlayShown = IsWindowVisible(overlayWindow) != FALSE;
            // Hide only after Minecraft has been inactive for a moment: a notification or another
            // window briefly taking focus must not make the overlay vanish and pop back (flicker).
            static ULONGLONG inactiveSince = 0;

            if (minecraftActive)
                inactiveSince = 0;
            else if (inactiveSince == 0)
                inactiveSince = GetTickCount64();

            if (!minecraftActive && overlayShown &&
                GetTickCount64() - inactiveSince >= 400)
            {
                // Another app is in front: close the menu too. Its invisible input window covers the
                // game area and would otherwise keep taking the clicks meant for that app.
                if (menuOpen)
                    CloseMenu(false);
                ShowWindow(overlayWindow, SW_HIDE);
                if (catcherWindow)
                    ShowWindow(catcherWindow, SW_HIDE);
            }
            else if (minecraftActive && !overlayShown)
            {
                ShowWindow(overlayWindow, SW_SHOWNOACTIVATE);
                hasPreviousRect = false; // Force repositioning over the game.
            }
        }


        bool changed =
            !hasPreviousRect ||
            mr.left !=
                previousMinecraftRect.left ||
            mr.top !=
                previousMinecraftRect.top ||
            mr.right !=
                previousMinecraftRect.right ||
            mr.bottom !=
                previousMinecraftRect.bottom;


        if (changed)
        {
            int width =
                mr.right -
                mr.left;

            int height =
                mr.bottom -
                mr.top;


            SetWindowPos(
                overlayWindow,
                HWND_TOPMOST,
                mr.left,
                mr.top,
                width,
                height,
                SWP_NOACTIVATE |
                SWP_NOSENDCHANGING
            );


            if (catcherWindow && menuOpen)
            {
                SetWindowPos(
                    catcherWindow,
                    overlayWindow,
                    mr.left,
                    mr.top,
                    width,
                    height,
                    SWP_NOACTIVATE
                );
            }

            previousMinecraftRect =
                mr;

            hasPreviousRect =
                true;


            InvalidateRect(
                overlayWindow,
                nullptr,
                FALSE
            );
        }


        // G is a global key state: ignore it while typing in another app.
        // ...and typing a "g" into a text box must not close the menu.
        const bool typingText =
            menuOpen &&
            (searchActive ||
             nickEditing ||
             presetEditing ||
             partyAddingPlayer ||
             (replaysOpen && (replaySearching || !replayRenaming.empty())));

        // In a world, G only opens the menu from play: not while the game's chat (or any other game
        // screen, e.g. a sign or the pause screen) is open, where it is a typed letter.
        const bool gameScreenOpen =
            !menuOpen &&
            MinecraftDataProvider::IsGameMenuOpen();

        bool g =
            minecraftActive &&
            !typingText &&
            !gameScreenOpen &&
            !waitingForShortcut &&   // the key being set (it may become the menu key) must not close the menu
            IsKeyDown(shortcuts.menuKey);


        if (g &&
            !previousGState)
        {
            if (!menuOpen)
            {
                OpenMenu();
            }
            else
            {
                CloseMenu();
            }

            InvalidateRect(
                overlayWindow,
                nullptr,
                FALSE
            );
        }


        // A G still held when the game screen closes (e.g. the last letter typed in chat) must not open the menu.
        previousGState =
            g ||
            (gameScreenOpen && minecraftActive && IsKeyDown(shortcuts.menuKey)) ||
            (waitingForShortcut && IsKeyDown(shortcuts.menuKey));


        // Fallback for the HUD editor toolbar: read the mouse button state directly, in case the
        // click message goes to another window (the shared handler ignores everything except the toolbar).
        {
            static bool previousLeft = false;

            const bool left =
                (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

            if (left && !previousLeft &&
                menuOpen && hudEditMode && overlayWindow)
            {
                POINT cursor;

                if (GetCursorPos(&cursor) &&
                    ScreenToClient(overlayWindow, &cursor))
                {
                    HandleEditorClick(cursor.x, cursor.y);
                }
            }

            // Home screen: the top-left "Replays" button opens the Replays panel when the button is let go.
            // Taking the focus while the game still held the press left its title screen drawn shrunk
            // (with black edges) until a world was started.
            {
                static bool replaysPressed = false;
                const auto overButton = [&] {
                    POINT cursor;
                    const RECT b = HomeReplaysButtonRect();
                    return GetCursorPos(&cursor) && ScreenToClient(overlayWindow, &cursor) && PtInRect(&b, cursor);
                };
                const bool usable = !menuOpen && overlayWindow && minecraftActive && OnHomeScreen();
                if (left && !previousLeft)
                    replaysPressed = usable && overButton();
                else if (!left && previousLeft)
                {
                    if (replaysPressed && usable && overButton())
                        OpenReplays(true);
                    replaysPressed = false;
                }
            }
            // Opened from the home screen: closes by itself once a world starts (or the game is gone).
            if (menuOpen && replaysOpen && replaysFromHome &&
                (MinecraftDataProvider::IsInWorld() || !MinecraftDataProvider::IsGameRunning()))
            {
                CloseReplays();
                if (overlayWindow) InvalidateRect(overlayWindow, nullptr, FALSE);
            }

            previousLeft = left;
        }


        CheckShortcut(
            shortcuts.keystrokes,
            prevKS,
            DetailFeature::Keystrokes
        );


        CheckShortcut(
            shortcuts.cps,
            prevCPS,
            DetailFeature::CPS
        );


        CheckShortcut(
            shortcuts.armor,
            prevArmor,
            DetailFeature::Armor
        );


        CheckShortcut(
            shortcuts.arrows,
            prevArrows,
            DetailFeature::Arrows
        );


        CheckShortcut(
            shortcuts.fps,
            prevFPS,
            DetailFeature::FPS
        );



        CheckShortcut(
            shortcuts.blockOutline,
            prevBlockOutline,
            DetailFeature::BlockOutline
        );

        CheckShortcut(shortcuts.resources, prevExtra[0], DetailFeature::Resources);
        CheckShortcut(shortcuts.fullbright, prevExtra[1], DetailFeature::Fullbright);
        CheckShortcut(shortcuts.nametag, prevExtra[2], DetailFeature::Nametag);
        CheckShortcut(shortcuts.ping, prevExtra[3], DetailFeature::Ping);
        CheckShortcut(shortcuts.textures, prevExtra[4], DetailFeature::Textures);
        CheckShortcut(shortcuts.featureList, prevExtra[6], DetailFeature::FeatureList);
        CheckShortcut(shortcuts.shulker, prevExtra[7], DetailFeature::ShulkerPreview);
        CheckShortcut(shortcuts.tabList, prevExtra[11], DetailFeature::TabList);
        CheckShortcut(shortcuts.hiveStats, prevExtra[12], DetailFeature::HiveStats);
        CheckShortcut(shortcuts.chunkBorder, prevExtra[13], DetailFeature::ChunkBorder);
        CheckShortcut(shortcuts.hitbox, prevExtra[14], DetailFeature::Hitbox);
        CheckShortcut(shortcuts.nick, prevExtra[15], DetailFeature::Nick);
        CheckShortcut(shortcuts.replay, prevExtra[16], DetailFeature::Replay);
        CheckShortcut(shortcuts.menuSounds, prevExtra[17], DetailFeature::MenuSounds);
        CheckShortcut(shortcuts.lite, prevExtra[18], DetailFeature::Lite);
        CheckShortcut(shortcuts.fastInventory, prevExtra[19], DetailFeature::FastInventory);
        CheckShortcut(shortcuts.leftHand, prevExtra[20], DetailFeature::LeftHand);
        CheckShortcut(shortcuts.hitEffect, prevExtra[21], DetailFeature::HitEffect);
        CheckShortcut(shortcuts.frameGen, prevExtra[22], DetailFeature::FrameGen);
        CheckShortcut(shortcuts.dayTimer, prevExtra[23], DetailFeature::DayTimer);
        CheckShortcut(shortcuts.blockDetails, prevExtra[24], DetailFeature::BlockDetails);
        CheckShortcut(shortcuts.healthWarning, prevExtra[25], DetailFeature::HealthWarning);
        {
            // Debug Menu: its key shows / hides the debug screen (it never switches the module itself).
            static bool previousDebugKey = false;
            const int vk = shortcuts.debugMenu;
            const bool down = debugMenuEnabled && vk != 0 && vk != shortcuts.menuKey && IsKeyDown(vk);
            if (down && !previousDebugKey && gameActive && !menuOpen && !waitingForShortcut && GameOwnsKeyboard() &&
                MinecraftDataProvider::IsInWorld())
            {
                debugScreenShown = !debugScreenShown;
                if (overlayWindow) InvalidateRect(overlayWindow, nullptr, FALSE);
            }
            previousDebugKey = down;
        }
        CheckShortcut(shortcuts.party, prevExtra[9], DetailFeature::PartyAutoAccept);
        CheckShortcut(shortcuts.autoRq, prevExtra[10], DetailFeature::AutoRq);
        CheckShortcut(shortcuts.hive, prevExtra[8], DetailFeature::HiveUtils);

        // Zoom is a hold action rather than a toggle.
        // Heartbeat expires in the DLL if the HUD stops or loses focus.
        zoomActive =
            IsZoomRequested();

        // The wheel hook (zoom factor) is a low-level hook: every mouse event on the PC passes through this
        // process while it is installed, so other apps lag whenever this loop is busy. It is installed only
        // while the Zoom key is held and removed as soon as it is released.
        if (zoomActive && !wheelHook)
            wheelHook = SetWindowsHookExW(WH_MOUSE_LL, ZoomWheelHook, GetModuleHandleW(nullptr), 0);
        else if (!zoomActive && wheelHook)
        {
            UnhookWindowsHookEx(wheelHook);
            wheelHook = nullptr;
        }

        MinecraftDataProvider::SetZoomRequested(
            zoomActive, static_cast<uint32_t>(shortcuts.zoom),
            static_cast<uint32_t>(zoomFactorX100.load())
        );
        if (!zoomActive && zoomFactorDirty.exchange(false))
            SaveConfig();
        // Full Bright is a level, not a hold: refreshed every tick so the
        // DLL's 250ms lease expires if this HUD stops or crashes.
        MinecraftDataProvider::SetFullbrightRequested(
            fullbrightEnabled
        );
        {
            const std::string nickUtf8 = WideToUtf8(nickText);
            MinecraftDataProvider::SetNickRequested(nickEnabled && !nickUtf8.empty(), nickUtf8.c_str());
        }
        // Recording can always start mid-game; never while a replay plays back (it would record itself).
        const bool replayPlaying = ReplayPlayer::Active();
        MinecraftDataProvider::SetReplayBuffer(!replayPlaying);
        MinecraftDataProvider::SetHitEffect(hitEffectEnabled, hitEffectCrit, hitEffectMagic);
        MinecraftDataProvider::SetFrameGen(liteEnabled && frameGenEnabled, frameGenMultiplier, frameGenMethod, frameGenPacing);
        MinecraftDataProvider::SetLeftHand(leftHandEnabled);
        MinecraftDataProvider::SetShulkerHover(shulkerEnabled);
        MinecraftDataProvider::SetHudMenuOpen(menuOpen);
        TrackPlayingRates(!menuOpen && !hudEditMode && minecraftActive && MinecraftDataProvider::IsInWorld());
        MinecraftDataProvider::SetReplayRequested(replayEnabled && !replayPlaying);
        MinecraftDataProvider::SetRenderDistance(
            liteEnabled && liteRenderEnabled ? (uint32_t)liteRenderDistance : 0u, renderRequestCounter);
        MinecraftDataProvider::SetEntityDistance(liteEnabled && liteEntityLimit ? (uint32_t)liteEntityDistance : 0u, false);
        MinecraftDataProvider::SetOptimize(liteCullMobs, liteCullItems, liteCullMisc, liteEnabled && liteNoNameTags,
                                           liteEnabled && litePlayerLimit ? (uint32_t)litePlayerDistance : 0u);
        {
            static ULONGLONG lastTweak = 0;   // Windows settings on the game's process: about once a second
            if (GetTickCount64() - lastTweak >= 1000) { lastTweak = GetTickCount64(); ApplyProcessTweaks(); }
        }
        {
            uint32_t hitboxRgb[4];
            for (int k = 0; k < 4; k++)
            {
                const COLORREF c = HitboxColor(k);
                hitboxRgb[k] = hitboxColorIndex[k] <= 0 ? 0u
                    : MyClientFeatures::HitboxCustom | (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c);
            }
            MinecraftDataProvider::SetHitboxRequested(hitboxEnabled && !menuOpen && !hudEditMode, (uint32_t)hitboxFilter, hitboxRgb);
        }
        MinecraftDataProvider::SetNametagRequested(
            nametagEnabled
        );
        MinecraftDataProvider::SetTextures(
            texturesEnabled || ClientPackWanted() || GetTickCount64() < clientPackReloadUntil,
            texturesSeq
        );
        MinecraftDataProvider::SetAutoRq(
            (hiveEnabled && autoRqEnabled) ? (unsigned int)autoRqMode + 1u : 0u
        );
        {
            std::string pingHost;
            int pingPort = 0;
            const bool havePingTarget =
                pingEnabled &&
                MinecraftDataProvider::GetPingTarget(pingHost, pingPort);

            PingMonitor::SetTarget(
                havePingTarget ? pingHost : std::string(),
                pingPort,
                havePingTarget
            );
        }
        blockOutlineSettings.color = BlockOutlineColor();
        MinecraftDataProvider::SetOutlineRequested(
            blockOutlineEnabled,
            (unsigned)GetRValue(blockOutlineSettings.color) << 16 |
            (unsigned)GetGValue(blockOutlineSettings.color) << 8 |
            (unsigned)GetBValue(blockOutlineSettings.color)
        );
        MinecraftDataProvider::SetPartyRequested(hiveEnabled && partyAutoAcceptEnabled, partyPlayers);


        bool left =
            IsKeyDown(
                VK_LBUTTON
            );

        bool right =
            IsKeyDown(
                VK_RBUTTON
            );


        // Mouse buttons are polled globally: only count clicks made in Minecraft.
        if (!menuOpen && minecraftActive)
        {
            if (left &&
                !previousLeft)
            {
                AddLeftClick();
            }

            if (right &&
                !previousRight)
            {
                AddRightClick();
            }
        }


        previousLeft =
            left;

        previousRight =
            right;


        // Repaint pacing. Keys and mouse are still polled every 16 ms above, and clicks, drags and typing
        // repaint at once from the window messages; this timer only keeps things moving between them.
        // In game the HUD redraws ~60 times a second (keystrokes, hitboxes follow the camera); the menu,
        // the home screen and a background game need far fewer, which keeps the HUD light on slow PCs.
        {
            static ULONGLONG lastRepaint = 0;
            static int lastState = -1;
            const ULONGLONG now = GetTickCount64();
            const bool inWorld = MinecraftDataProvider::IsInWorld();
            // A toast bar is moving: every frame, wherever the HUD is, so it glides (the menu's 50 ms made it jerky).
            const ULONGLONG interval =
                !toasts.empty() ? 0 :
                !minecraftActive ? 250 :
                // The home screen's head turns and sways: ~30 frames a second while it is shown.
                (menuOpen && !hudEditMode && !replaysOpen && selectedCategory == MenuCategory::Home) ? 33 :
                (menuOpen && !hudEditMode) ? 50 :
                (!menuOpen && !inWorld) ? 100 :
                0;
            // Any change of what is on screen (menu opened / closed, editor, world joined or left, game back
            // in front) repaints at once, so the old picture never lingers for a slow interval.
            const int state = (minecraftActive ? 1 : 0) | (menuOpen ? 2 : 0) | (hudEditMode ? 4 : 0) | (inWorld ? 8 : 0);
            if (interval == 0 || state != lastState || now - lastRepaint >= interval)
            {
                lastState = state;
                lastRepaint = now;
                InvalidateRect(overlayWindow, nullptr, FALSE);
            }
        }


        Sleep(16);
    }


    return 0;
}

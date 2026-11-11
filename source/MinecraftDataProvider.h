#pragma once
#include "JavaChatChannel.h"

#include "BridgeApi.h"
#include "FeatureData.h"
#include "ResourceChannel.h"
#include "ShulkerChannel.h"
#include "ItemCountChannel.h"
#include "TabListChannel.h"
#include "PartyData.h"
#include <string>
#include <vector>

namespace MinecraftDataProvider {
struct BlockOutlineData {
    bool valid = false;
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 1, maxY = 1, maxZ = 1;
};

struct BlockOutlineScreenData {
    bool valid = false;
    POINT points[8]{};
};

struct TargetBlockData {
    bool valid = false;
    int32_t x = 0, y = 0, z = 0;
};

enum class Status { Disconnected, WaitingForData, Live, Stale, InvalidData };

// These functions are called only by the overlay's UI thread.
bool Initialize(HWND minecraftWindow);
void Tick(HWND minecraftWindow);
void Shutdown();
bool GetArrowCount(uint32_t& count);
bool IsInWorld();
// True while the game DLL is rendering frames (also on the title screen, where no world ticks and the
// frame channel goes stale).
bool IsGameRunning();
// The game's title screen is on top (newer DLLs); -1 = the DLL does not say (older DLLs), 0 / 1 otherwise.
int IsOnTitleScreen();
Status GetStatus();
const wchar_t* GetStatusText();
const TargetBlockData& GetTargetBlockData();
const BlockOutlineData& GetBlockOutlineData();
const BlockOutlineScreenData& GetBlockOutlineScreenData();
void UpdateBlockOutlineData(const BlockOutlineData& data);
void UpdateBlockOutlineScreenData(const BlockOutlineScreenData& data);
void ClearBlockOutlineData();

bool GetFps(float& fps);
bool GetResources(MyClientResources::Counts& counts);
// The local player's skin picture (RGBA, rows top to bottom; empty = none seen yet). `changes` goes up
// whenever it changed. SetSkin puts back the one saved by the HUD (shown before the game sends one).
const std::vector<uint8_t>& GetSkin(uint32_t& width, uint32_t& height, uint32_t& changes);
void SetSkin(std::vector<uint8_t> rgba, uint32_t width, uint32_t height);
bool SkinHasNoPicture();   // the skin worn is a persona / own-model one (no classic picture to show)
// The Closet: asks the game to wear this skin (shown to everyone: it is sent to the server like a
// Dressing Room change). SkinRequestAnswer follows the last request until the game answers.
enum class SkinRequestState { None, NoGame, OldDll, Waiting, Sent, NoPlayer, Failed, Saved, Unsupported };
SkinRequestState RequestSkin(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, bool slim, const std::string& name);
SkinRequestState SkinRequestAnswer();
SkinRequestState RequestCape(uint32_t id, const std::vector<uint8_t>& rgba);
uint32_t CapeChoice();
bool JavaChatSnapshot(bool enabled, MyClientJavaChat::Snapshot& out);
bool JavaChatReady();
// Command Hotkeys: a line for the game's chat ('/...' = a command), sent by the DLL on its next tick.
// Busy = the last line has not been sent yet; ChatLineAnswer follows the last line written.
enum class ChatRequestState { None, NoGame, OldDll, Busy, Empty, Waiting, Sent, NoPlayer, Failed };
ChatRequestState SendChatLine(const std::string& utf8);
ChatRequestState ChatLineAnswer();
// Replay Mod: asks the DLL to take the game to the local replay server on 127.0.0.1:<port> (leaving a world first).
// OldDll / Busy / Failed: the HUD opens the minecraft:// link instead. Joining = the game was asked to connect.
enum class ReplayJoinState { None, NoGame, OldDll, Busy, Waiting, Joining, Failed };
ReplayJoinState RequestReplayJoin(uint16_t port);
ReplayJoinState ReplayJoinAnswer();
bool GetShulkers(MyClientShulker::Shared& out);
// The shulker box under the mouse in an inventory / container screen (fresh within ~300 ms).
bool GetShulkerHover(MyClientShulker::HoverBox& out);
// Item Counter (ItemCountChannel.h): tell the DLL the feature is on (every tick), read the counts while Alt is held.
void SetItemCounterWanted(bool on);
bool GetItemCount(MyClientItemCount::Shared& out);
bool ItemCounterReady();   // the DLL has the Item Counter channel
bool GetTabList(MyClientTabList::Shared& out);
// Local camera for Chunk Border (fresh within ~250 ms): eye position, yaw/pitch degrees, vertical FOV degrees.
struct Camera { float x, y, z, yaw, pitch, fov; };
bool GetCamera(Camera& out);
MyClientFeatures::Armor GetArmor(unsigned slot);
// Item id in that armor slot ("diamond_helmet"; empty when the slot is empty or there is no fresh data).
std::string GetArmorItem(unsigned slot);
// Day Timer: the world's time of day in ticks (fresh within ~1.5 s, i.e. in a world that is ticking);
// moving = false when it has not changed for a few seconds (daylight cycle off, as on most minigame servers).
bool GetWorldClock(int32_t& time, bool& moving);
// Health Warning: your health in points (1 = half a heart), fresh with the clock; false = not in a world or an older DLL.
bool GetPlayerHealth(int& points);
// Debug Menu: the looked-at block, biome and light from the DLL (fresh with the clock). Strings are NUL-terminated.
bool GetDebug(MyClientFeatures::Debug& out);
// Block Details: the name / tags / tool of the block in GetDebug's lookBlock (false = none or an older DLL).
bool GetLook(MyClientFeatures::Look& out);
// Debug Menu: the GPU the game draws with (name UTF-8); false until the DLL has seen the game's first frame.
struct Gpu { std::string name; uint32_t directX, featureLevel, videoMb, vendor; uint64_t driver; };
bool GetGpu(Gpu& out);
// Folder of the running Minecraft.Windows.exe (its data\resource_packs hold the game's own textures).
std::wstring GameDirectory();
// Where the HUD reads the game's textures and .lang files: the game folder, or the DLL's copy when it is protected.
std::wstring GameTextureDirectory();
bool IsZoomApplied();
void SetZoomRequested(bool requested, uint32_t key = 'C', uint32_t factorX100 = 300);
bool IsZoomRequested();
bool IsFullbrightApplied();
bool IsFullbrightAvailable();
void SetFullbrightRequested(bool requested);
bool IsNametagApplied();
bool GetPingTarget(std::string& host, int& port);
bool GetHookRates(MyClientFeatures::Stats& rates);
bool GetOutlineEcho(bool& leaseFresh, unsigned int& rgb);
void SetTextures(bool on, unsigned int sequence);
void SetAutoRq(unsigned int mode);  // 0 = off, 1..10 = AutoRq.h mode
bool IsTexturesAvailable();
bool IsLiveTexturesAvailable();
bool GetTextureReloads(unsigned int& count);
bool IsNametagAvailable();
void SetNametagRequested(bool requested);
// Replay recorder (stage 1: raw packet log). The HUD asks for recording; the DLL reports what it writes.
struct ReplayStatus { uint64_t bytes; uint32_t packets, seconds; char path[264]; };
void SetReplayRequested(bool requested);
// Keep each session from joining (deleted unless recorded), so recording can be switched on mid-game.
void SetReplayBuffer(bool keep);
DWORD GamePid();   // the connected game process (0 = none)
void SetHitEffect(bool on, bool crit, bool magic);   // reserved bits 30 (crit sparks) / 31 (magic sparks)
void SetLeftHand(bool on);   // bit 24 of the autoRq field: the first-person held item drawn on the left
// Frame Generation: bits 27..31 of the autoRq field (multiplier 2/3, MyClientFeatures::FrameGenMethod, pacing).
void SetFrameGen(bool on, int multiplier, int method, bool pacing);
// Frames shown per second (made-up ones included) and MyClientFeatures::FrameGenState; false when unknown.
bool GetFrameGen(float& presentedFps, uint32_t& state);
// While Frame Generation runs: frames shown per game frame now, and the most the GPU fits (any multiplier).
bool GetFrameGenReach(uint32_t& shownPerFrame, uint32_t& fitsPerFrame);
void SetShulkerHover(bool on);   // bit 26 of the autoRq field: Shulker Preview's popup replaces the game's tooltip
void SetHudMenuOpen(bool open);   // bit 25 of the autoRq field: the HUD menu is open
// In a world with a game screen open (chat, inventory, pause, signs...); false when unknown (old DLL, no data).
bool IsGameMenuOpen();
// Optimization > Render Distance: `chunks` (0 = leave the game alone); each new `counter` applies it once.
void SetRenderDistance(uint32_t chunks, uint32_t counter);
// Optimization: do not draw entities farther than `blocks` (0 = off, 8..128 in steps of 8); `players` hides far players too.
void SetEntityDistance(uint32_t blocks, bool players);
// Optimization: which kinds the entity distance hides, other actors' name tags hidden, and a separate distance for
// players (`playerBlocks`, 0 = players are always drawn).
// uncapFps: the game's FPS is not held to the refresh rate (Optimization on).
// `extras`: MyClientFeatures::EncodeOptimizeExtras (render-hook rows), ORed in as they are.
void SetOptimize(bool cullMobs, bool cullItems, bool cullMisc, bool hideNameTags, uint32_t playerBlocks, bool uncapFps, uint32_t extras = 0);
bool GetReplayStatus(ReplayStatus& out);   // false = the DLL reports nothing (not recording or old DLL)
void SetNickRequested(bool requested, const char* utf8Name);   // name shown above your own character
// filter: 1 players, 2 items, 4 entities, 8 mobs; colors (by kind, may be null): HitboxCustom | 0xRRGGBB, 0 = the kind's own
void SetHitboxRequested(bool requested, uint32_t filter = 1, const uint32_t* colors = nullptr);
bool IsOutlineAvailable();
bool IsOutlineRunning();
bool IsOutlineColored();
void SetOutlineRequested(bool requested, unsigned int rgb);
void SetPartyRequested(bool enabled, const std::vector<std::wstring>& tags);
const wchar_t* GetPartyStatusText();
}

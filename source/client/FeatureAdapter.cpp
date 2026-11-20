#include "WholeName.h"
#include "OriginMarks.h"
#include "CrashDiagnostics.h"
#include "TextureReloadPolicy.h"
#include "SelfCheck.h"
#include "FeatureAdapter.h"
#include "JavaChatAdapter.h"
#include "JavaChatPack.h"
#include "ResourceReloadCompletion.h"
#include "OwnHook.h"
#include "GameState.h"
#include "GameAccess.h"
#include "Log.h"
#include "FeatureChannel.h"
#include "ReplayFormat.h"
#include "ChatChannel.h"
#include "Profile.h"
#include "Compositor.h"
#include "mc/resources/IResourcePackRepository.h"
#include "mc/resources/InvalidPack.h"
#include "mc/platform/UUID.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePackListener.h"
#include "mc/resources/ResourcePackStack.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/ResourcePack.h"
#include "mc/resources/ResourcePackRepository.h"
#include "mc/resources/RepositorySources.h"
#include "mc/resources/CompositePackSource.h"
#include "mc/resources/DirectoryPackSource.h"
#include "mc/resources/PackSourceFactory.h"
#include "mc/resources/ResourceLoadManager.h"
#include "mc/deps/core/file/Path.h"
#include "mc/deps/core/threading/IAsyncResult.h"
#include "mc/client/game/IMinecraftGame.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/gui/screens/models/IContentManager.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/network/BatchedNetworkPeer.h"
#include "GameSymbols.h"
#include "AutoRq.h"
#include "ComboCounter.h"
#include "ComboPackets.h"
#include "mc/network/NetworkPeer.h"
#include "PartyAdapter.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/Versions.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/deps/core/string/StringHash.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/minecraft_renderer/renderer/TextureGroupBase.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/phys/AABB.h"
#include "mc/comprehensive/ParticleType.h"
#include "LitePack.h"
#include <random>
#include "mc/client/renderer/game/GameRenderer.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/options/option_types/IntOption.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/client/renderer/chunks/RenderChunkInstanced.h"
#include "mc/client/renderer/chunks/RenderChunkShared.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/gui/DirtyFlag.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/common/Brightness.h"
#include "LeftHand.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/MeshContext.h"
#include "mc/deps/minecraft_renderer/objects/NameTagRenderObjectCollection.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/client/renderer/ptexture/BaseLightData.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/renderer/ShaderColor.h"
#include "mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/input/MouseDevice.h"
#include <atomic>
#include <filesystem>
#include <map>
#include <variant>
#include <fstream>
#include <iterator>
#include <vector>
#include <new>
#include <cmath>
#include <cstring>
#include <string>
#include <memory>
#include <mutex>
#include "../ReplayCameraChannel.h"
#include "../ReplayVideoChannel.h"
#include "VideoExport.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"
#include "mc/deps/vanilla_components/AABBShapeComponent.h"
#include "mc/entity/components/ActorRotationComponent.h"

// Game classes the HUD cost hooks only pass through (by reference).
class MinecraftUIFrameUpdateContext;
struct FrameRenderObject;
class MinecraftUIRenderContext;
class IClientInstance;
class UIControl;
class UIScene;
class TextureAtlas;

namespace {
std::atomic<bool> active{false};
std::mutex featureMutex;
MyClientFeatures::Channel channel;
MyClientFeatures::FrameRate frames;
bool nickHooked = false;
bool renderHooked = false, fovHooked = false, brightHooked = false, nametagHooked = false;
bool outlineHooked = false, tessColorHooked = false;
// Texture packs: MinecraftGame::_loadLastGlobalResourcePacksFromFile(bool) re-reads
// global_resource_packs.json and reloads the textures. The HUD rewrites that file, then bumps a counter.
using TextureReloadFn = void (*)(void* game, bool waitForUiReload);
TextureReloadFn textureReload = nullptr;
std::atomic<bool> textureReloadPending{false};
// The in-world apply is how a pack switch is applied in a world (2026-11-10, user: always, no switch). Its hooks sit on
// the world's render path, so they are installed when a switch is first applied in a world (EnsureLiveTextureHooks),
// not at start; liveTextureKnown = this game build is identified and the functions exist (reported to the HUD as
// FlagLiveTextures). Where it is not known, a switch made in a world is saved and applied at the title screen.
bool liveTextureReady = false, liveTextureKnown = false, liveDrawingComposited = false;
std::atomic<bool> liveTextureWanted{false};
std::atomic<bool> liveTextureLocalAllowed{false};
bool javaChatChecked = false, javaChatAvailable = false;
std::atomic<bool> liveTextureBusy{false};
std::atomic<LevelRenderer*> liveRendererHeld{nullptr};
// The render callbacks must never wait behind sampling/replay work holding featureMutex. Copy the
// zoom command once per frame; the timestamp preserves the same HUD lease on every subsequent read.
std::atomic<uint64_t> zoomCommandAt{0};
std::atomic<uint32_t> zoomCommandFields{0};
// What has to follow a reload while you are in a world (the game re-read the list, but blocks and animations
// kept their old look until the world was joined again). Counters come from hooks on the game's own reload steps.
std::atomic<uint32_t> statPacksChanged{0};
std::atomic<uint32_t> statPacksChangedInner{0};   // MinecraftGame::_onActiveResourcePacksChanged only     // MinecraftGame::onActiveResourcePacksChanged: the game reloads the packs
std::atomic<bool> atlasReloaded{false};        // MinecraftGame::_onTerrainAtlasReloaded: the block textures are new
std::atomic<uint32_t> terrainAtlasUpdates{0}, legacyAtlasSignals{0};
bool packsChangedHooked = false, atlasHooked = false, commonTerrainHooked = false;
std::atomic<uint32_t> hitboxFilterValue{0};       // bits 17..20 of command.reserved: 1 players, 2 items, 4 entities, 8 mobs
std::atomic<bool> hitboxRequestedValue{false};     // Hitbox: bit 16 of command.reserved (the low 16 bits are the zoom factor)
std::atomic<uint32_t> frameGenWide{0};             // Shared.frameGenMultiplier (x2..x10), refreshed once per frame
std::atomic<uint32_t> hitboxRgbValue[4] = {MyClientFeatures::HitboxDefaultRgb[0], MyClientFeatures::HitboxDefaultRgb[1],
                                           MyClientFeatures::HitboxDefaultRgb[2], MyClientFeatures::HitboxDefaultRgb[3]};   // Shared.hitboxRgb
// The actor of each hitbox of the last game tick and its position then (written at the tick, read every frame).
std::mutex hitboxActorsMutex;
std::vector<Actor*> hitboxActors;
std::vector<Vec3> hitboxActorPositions;
std::atomic<uint32_t> autoRqModeValue{0};          // 0 = off; refreshed once per frame from the HUD's command lease
// Render Distance: chunks to apply on the game thread (0 = nothing pending); counter state like textures.
std::atomic<uint32_t> renderDistancePending{0};
std::atomic<uint32_t> fpsLimitWanted{0};
std::atomic<bool> renderDistanceRestoreWanted{false};
int renderDistanceOriginal = -1;
int renderDistanceOriginalValue = -1;   // the setting's own value (blocks) before IntOption::set   // the player's own distance before Optimization changed it (-1 = not changed)
std::atomic<int> optimizationAuditMaster{-1};
uint32_t renderDistanceSeen = 0;
bool renderDistancePrimed = false;
std::atomic<uint32_t> statTextureReloads{0};
uint32_t textureSeen = 0;
bool texturePrimed = false;
std::atomic<bool> outlineWanted{false};             // refreshed once per frame from the HUD's command lease
std::atomic<uint32_t> outlineRgbValue{0x50B4FF};    // 0xRRGGBB chosen in the HUD
std::atomic<ULONGLONG> outlineRanAt{0}, outlineColoredAt{0};
thread_local bool insideOutline = false;
// Diagnostics: how often the game ran each hooked function (shown by the HUD).
std::atomic<uint32_t> statLightBuild{0}, statLightSplit{0}, statLightCreate{0}, statNameTag{0}, statNameTagLocal{0},
                      statOutline{0}, statTessColor{0};            // true only while the game runs its own outline code
// Replay recorder: every packet the game receives is appended to a .mcrec file while the HUD asks for it.
std::atomic<bool> replayWanted{false};             // bit 22 of command.reserved, refreshed once per frame
std::mutex replayMutex;
MyClientReplay::Writer replayWriter;
std::wstring replayPathW;
std::string replayPathUtf8;
uint64_t replayStartedAt = 0;
std::atomic<bool> replayOpen{false};
uint64_t replayWantedUntil = 0;                  // the command record is wiped on level exit; the request is sticky for 1.5 s
bool replayHooked = false;
bool replaySendHooked = false;
// Hit Effect: Java-style critical-hit particles on every hit, plus blue "enchanted hit" sparks with an
// enchanted weapon (bits 30 / 31 of command.reserved). Spawned in this client only.
std::atomic<bool> hitEffectCrit{false}, hitEffectMagic{false};   // Hit Effect: crit sparks / enchanted sparks, each on its own
bool hitHooked = false;
// Combo Counter (docs/superpowers/specs/2026-11-17-combo-counter-design.md): read from the packets the Replay hooks
// already see — the attack the client sends (InventoryTransaction / UseItemOnEntity / Attack) and the server's
// ActorEvent Hurt / SetActorMotion. Network-thread events; comboMutex serializes them (never taken with others held
// except featureMutex -> comboMutex in SampleClock).
std::mutex comboMutex;
MyClientCombo::Counter comboCounter;
uint64_t comboLocalId = 0;            // the local player's runtime id, from StartGame
uint32_t comboAttackPackets = 0;      // attacks read from sent packets (with comboMutex)
int comboHurtLogs = 0, comboHitLogs = 0;
std::atomic<uint32_t> comboAttackCalls{0};   // GameMode::attack calls, to notice when no attack packet is ever read
// Left Hand: the item in your first-person hand is mirrored to the left (bit 24 of command.autoRq).
std::atomic<bool> leftHandWanted{false};
// Shulker Preview: the HUD's popup replaces the game's tooltip over shulker boxes (bit 26 of command.autoRq).
std::atomic<bool> shulkerHoverWanted{false};
// The game's screens, for the HUD's G key (bit 25 of command.autoRq = the HUD menu is open) and published in
// stats.reserved (MyClientFeatures::GameScreenKnown / GameMenuOpen / GamePaused / GameWorldKnown / GameInWorld /
// GameTitleKnown / GameOnTitle).
std::atomic<bool> hudMenuWanted{false};
// The HUD menu flag counts as gone only after this long without a fresh command: with the usual 1 s lease a short
// stall of the HUD made the menu look closed, and the held lost focus then brought up the pause screen behind the
// open menu. Closing the menu still arrives at once (the HUD writes the cleared flag immediately).
constexpr uint64_t kHudMenuLeaseMs = 5000;
std::atomic<uint32_t> statMouseHeld{0};   // mouse moves kept from the game while the HUD menu was open (10 s total)
std::atomic<uint32_t> statRawHeld{0};     // ...and mouse moves not fed to the game's MouseDevice (GameInput)
std::atomic<uint32_t> gameScreenBits{0};
bool focusLostHooked = false;
// The game whose lost focus FocusLostHook held back while the HUD menu was open.
std::atomic<void*> heldFocusLost{nullptr};
// Frame Generation: bits 27..31 of command.autoRq as last read under a fresh lease (0 = off); the Compositor reads it.
std::atomic<uint32_t> frameGenField{0};
bool leftHandHooked = false;
std::atomic<uint32_t> statLeftHand{0};
// Left Hand check (10 s totals, logged with the Perf line): item draws while it is on / of those in the
// first-person pass / matrix put back afterwards.
std::atomic<uint32_t> statLeftHandCalls{0}, statLeftHandFirst{0}, statLeftHandRestored{0}, statFirstPersonPasses{0};
std::atomic<uint32_t> statLeftHandOwn{0};   // item draws for your own player (any view)
// Your own player and whether the camera is first person, taken once a frame (UpdateLeftHandView): in first
// person your player is not drawn in the world, so an item drawn for it outside the UI is the one in your hand.
std::atomic<const void*> leftHandOwnPlayer{nullptr};
std::atomic<bool> leftHandFirstPersonView{false};
std::atomic<uint32_t> statFirstPersonMeshes{0};   // meshes drawn inside renderFirstPerson (Left Hand on)
// InGamePlayScreen::_renderItemInHand passes seen / mirrored (Left Hand on)
std::atomic<uint32_t> statItemInHandPasses{0}, statItemInHandMirrored{0};
// Join buffer: from each StartGame the session is written to a ".part" file even while not recording, so
// recording can be switched on mid-game and still has the world data the server sent at the start. Pressing
// record writes a mark and the file is kept (renamed to .mcrec); a session that is never recorded is deleted.
std::atomic<bool> replayBufferAllowed{false};    // bit 29 of command.reserved (the HUD's "Keep from join" option)
bool replayMarked = false;                        // the player pressed record in this session file
bool replaySawStartGame = false;                  // this session file already holds a StartGame
uint64_t replayMarkedAt = 0;
std::wstring replayPartW;                         // the session file while it is being written
constexpr uint64_t kReplayBufferMax = 512ull << 20;   // an unrecorded buffer larger than this is dropped
// Entity render distance (Optimization): refreshed once per frame; the per-actor hooks only read it.
// cullRange2 == 0 means off. cullLocal is compared with the rendered actor, never dereferenced there.
std::atomic<float> cullRange2{0.f}, cullX{0.f}, cullY{0.f}, cullZ{0.f};
std::atomic<const void*> cullLocal{nullptr};
std::atomic<bool> cullPlayers{false};
// Optimization extras (Shared.optimize): which kinds the range above hides, the players' own range (0 = never)
// and "hide other actors' name tags".
std::atomic<uint32_t> cullKinds{0};
std::atomic<float> cullPlayerRange2{0.f};
std::atomic<bool> hideNameTagsWanted{false};
std::atomic<bool> uncapFpsWanted{false};   // legacy Optimization request; native V-Sync OFF is honored independently
std::atomic<uint32_t> statCulled{0};
bool cullHooked = false;
// Optimization rows that need render hooks (Shared.optimize bits 10..20, DecodeOptimizeExtras), refreshed once per
// frame like the entity distance: sub-chunk rows kept above/below the camera (0 = all), block entities farther than
// this from the player (0 = off), the sky itself, actors behind the camera.
std::atomic<uint32_t> verticalRowsWanted{0};
std::atomic<float> blockEntityRange2{0.f};
std::atomic<bool> noSkyWanted{false}, cullBehindWanted{false}, noCloudsWanted{false};
std::atomic<uint32_t> statChunksSkipped{0}, statBlockEntitiesSkipped{0}, statSkySkipped{0}, statBehindSkipped{0};
bool queueChunkHooked = false, blockActorHooked = false, skyHooked = false;
float cameraForward[3] = {};                       // render thread: view direction of the last captured frame
bool cameraForwardKnown = false;
std::atomic<bool> nickWanted{false};               // Nick on (bit 21 of command.reserved)
std::atomic<char> localTagColor{0};                // colour of the local player's REAL name tag, seen by the Nick hook
std::string nickName;                              // written only by the per-frame hook, read by the name hook (same thread)
std::atomic<uint64_t> renderedFrames{0};           // frames counted by the frame hook
std::atomic<uint32_t> statSetupCamera{0}, statCameraCaptures{0}, statNameTagLists{0};
float lastCameraPos[3] = {};                       // render thread: camera position of the last captured frame
std::mutex realNameMutex;
std::string localRealName;                         // the local player's own name tag text (before Nick), for the name tag list
std::atomic<bool> nametagWanted{false};            // refreshed once per frame from the HUD's command lease

// Your own name tag (Nametag, and Nick which renames it): LevelRendererPlayer::extractNameTags skips the camera's
// own player before any check (26.51: "cmp r12,rbx / je next"; 26.60: "cmp [rbp+B8],rbx / je next"), so the
// canShowNameTag hook never sees it (r57-test12: Nametag and Nick did nothing). While Nametag or Nick is on, that one
// short jump is replaced by a 2-byte no-op (66 90); it is put back when both are off. Found in the stored game
// images (Release game-dumps); per exe build, with the bytes around it compared first.
struct OwnTagSkip { uint32_t timestamp, rva; uint8_t before[7]; uint8_t beforeLength; uint8_t jump[2]; };
constexpr OwnTagSkip kOwnTagSkip[] = {
    {0x6AA482FDu, 0x46B3134u, {0x4C,0x39,0xE3}, 3, {0x74,0xDA}},
    {0x6AD79EE4u, 0x5397AB3u, {0x48,0x3B,0x9D,0xB8,0x00,0x00,0x00}, 7, {0x74,0xDB}},
};
bool SetOwnTagPatch(bool on)
{
    static int state = -1;   // -1 unknown/unavailable, 0 original, 1 patched
    static uint8_t* site = nullptr;
    static const OwnTagSkip* row = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        const auto module = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
        for (auto const& candidate : kOwnTagSkip)
            if (candidate.timestamp == nt->FileHeader.TimeDateStamp) row = &candidate;
        if (row) {
            uint8_t* at = module + row->rva;
            if (std::memcmp(at - row->beforeLength, row->before, row->beforeLength) == 0 && std::memcmp(at, row->jump, 2) == 0) {
                site = at; state = 0;
            }
        }
        MyClientLog::Get().info("Name tags: own-tag skip in extractNameTags {}", site ? "found" : row ? "not found (bytes differ)" : "unknown on this build");
    }
    if (state < 0) return false;
    if (int(on) == state) return true;
    const uint8_t noop[2] = {0x66, 0x90};
    DWORD old = 0;
    if (!VirtualProtect(site, 2, PAGE_EXECUTE_READWRITE, &old)) return true;
    std::memcpy(site, on ? noop : row->jump, 2);
    VirtualProtect(site, 2, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 2);
    state = on ? 1 : 0;
    MyClientLog::Get().info("Name tags: your own tag is {} the game's list", on ? "added to" : "left out of");
    return true;
}
// The actors extractNameTags walks: LevelRendererPlayer keeps two arrays side by side, the actor
// pointers (begin/end at +0x1D8/+0x1E0) and a matching array (+0x1F0/+0x1F8); the loop ends when both are at
// their ends. Actors that left the world are removed from them only inside LevelRenderer::preRenderUpdate
// (26.51 0x46AB7C0, reached from preRenderUpdate 0x47F49A0), which the live texture apply holds. On a server
// (Hive) players leave during the hold, the list keeps a freed actor and extractNameTags reads it: the r57-test22
// crash dump (26.51, 0x46B3139 `mov rax,[rbx]`, rbx the last entry with a garbage vtable). The offsets are read
// in the loop setup of each build, checked byte for byte before use.
struct NameTagActorList { uint32_t timestamp, rva; uint8_t load[7]; };
constexpr NameTagActorList kNameTagActorList[] = {
    {0x6AA482FDu, 0x46B30BDu, {0x4C,0x8B,0xA8,0xD8,0x01,0x00,0x00}},   // mov r13,[rax+0x1D8]
    {0x6AD79EE4u, 0x5397A38u, {0x48,0x8B,0xB0,0xD8,0x01,0x00,0x00}},   // mov rsi,[rax+0x1D8]
};
constexpr size_t kNameTagActorsBegin = 0x1D8;   // qwords: [0] +0x1D8 begin, [1] +0x1E0 end, [3] +0x1F0 second, [4] +0x1F8 its end
bool NameTagActorListKnown()
{
    static int known = -1;
    if (known < 0) {
        known = 0;
        const auto module = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
        for (auto const& row : kNameTagActorList)
            if (row.timestamp == nt->FileHeader.TimeDateStamp && std::memcmp(module + row.rva, row.load, sizeof row.load) == 0) known = 1;
        MyClientLog::Get().info("Name tags: actor list during a live texture apply {}", known ? "is emptied for the call" : "unknown on this build");
    }
    return known == 1;
}
std::atomic<uint64_t> nameTagsHeldCalls{0};
// r57-test27: the per-call emptying covered extractNameTags only; another frame-extraction loop over the same list
// (26.51 0x46AF575, a virtual call on each actor) read a freed actor in the same way. The list is now shown empty
// for the whole live apply, from the moment the pre-render update is held until it runs again.
std::atomic<void*> nameTagsPlayerSeen{nullptr};    // the LevelRendererPlayer extractNameTags was last called on
struct ActorListHold { uint64_t* fields = nullptr; uint64_t end = 0, end2 = 0; } actorListHold;
void HoldActorList()
{
    void* player = nameTagsPlayerSeen.load();
    if (actorListHold.fields || !player || !NameTagActorListKnown()) {
        MyClientLog::Get().info("Name tags: actor list for the live apply not emptied (player known={})", player != nullptr);
        return;
    }
    auto* fields = reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(player) + kNameTagActorsBegin);
    actorListHold = {fields, fields[1], fields[4]};
    fields[1] = fields[0];
    fields[4] = fields[3];
    MyClientLog::Get().info("Name tags: actor list shown empty for the whole live apply");
}
void ReleaseActorList()
{
    if (!actorListHold.fields) return;
    actorListHold.fields[1] = actorListHold.end;
    actorListHold.fields[4] = actorListHold.end2;
    actorListHold = {};
    MyClientLog::Get().info("Name tags: actor list put back after the live apply");
}
std::atomic<ULONGLONG> nametagAppliedAt{0};        // last time the hook really answered for the local player

// Full Bright: the light-map builders read BaseLightData::mNightvisionActive/Scale (the game's
// own night-vision state). While the HUD lease is fresh we present night vision to the builder
// for that single call and restore the original values afterwards, so no game state, effect or
// options file is changed. Declarations verified against the pinned SDK headers
// (BaseLightTextureImageBuilder::$buildImage / $buildImageSplit). If a hook cannot be
// installed, only Full Bright is reported UNAVAILABLE; Zoom, FPS and armor keep working.
// Not hooked: TheEndLightTextureImageBuilder overrides buildImage (MCNAPI, no exported symbol),
// so Full Bright does not apply in The End.

// One bit per optional game hook (see FeatureData.h): bit 1 Full Bright, bit 2 Nametag.
uint32_t AvailabilityBits()
{
    return MyClientFeatures::WithFlag(
        MyClientFeatures::WithFlag(0u, MyClientFeatures::FlagFullbright, brightHooked),
        MyClientFeatures::FlagNametag, nametagHooked)
        | (outlineHooked ? MyClientFeatures::FlagOutline : 0u)
        | (textureReload ? MyClientFeatures::FlagTextures : 0u)
        | (liveTextureKnown && liveTextureLocalAllowed.load() ? MyClientFeatures::FlagLiveTextures : 0u)
        | (javaChatChecked ? MyClientFeatures::FlagJavaChatChecked : 0u)
        | (javaChatAvailable ? MyClientFeatures::FlagJavaChatAvailable : 0u)
        | (javaChatChecked ? MyClientFeatures::FlagJavaChatJson : 0u);
}

// Every Reset() wipes telemetry, so re-stamp what must survive it.
void ResetChannel()
{
    channel.Reset(GetCurrentProcessId());
    channel.Data()->telemetry.fullbrightAvailable = AvailabilityBits();
}
using DamageFn = short (*)(ItemStackBase const*);
DamageFn damageValue = nullptr;

void UpdateReplay(bool wanted, MyClientFeatures::Shared& shared, uint64_t now);   // defined below

void PublishFps()
{
    // Connected server (for the HUD's Ping): looked up at most every 2 s, outside the feature lock.
    static ULONGLONG lastServerLookup = 0;   // render thread only
    std::string serverHost;
    uint32_t serverPort = 0, serverTransport = 1;
    bool serverLookedUp = false;
    const auto lookupNow = GetTickCount64();
    if (active.load() && lookupNow - lastServerLookup >= 2000) {
        lastServerLookup = lookupNow;
        serverLookedUp = true;
        const bool found = MyClientGame::CurrentServer(serverHost, serverPort, serverTransport);
        // Ping diagnostics (2026-11-19: Hive from the featured list showed "Waiting (join a server)"): what the game
        // reported, once per change. The HUD pings transport 0 (RakNet) and 4 (NetherNet) with a host and a port.
        {
            static std::string lastLogged = "unset";
            const std::string seen = found ? fmt::format("host='{}' port={} transport={}", serverHost, serverPort, serverTransport)
                                           : std::string("no connection info");
            if (seen != lastLogged) {
                MyClientLog::Get().info("Ping target: {} (connection readable={})", seen, MyClientGame::CanReadConnection());
                lastLogged = seen;
            }
        }
        if (!found) {
            serverHost.clear(); serverPort = 0; serverTransport = 1;
        }
    }
    const auto now = GetTickCount64();
    frames.Frame(now);
    std::unique_lock guard(featureMutex, std::try_to_lock);
    if (!guard.owns_lock() || !active.load()) return;
    if (!channel.Data() || !channel.Lock()) return;
    if (!MyClientFeatures::Valid(*channel.Data(), GetCurrentProcessId())) ResetChannel();
    {
        const auto& command = channel.Data()->command;
        const auto key = command.key > 0 && command.key < 255 ? command.key : 0u;
        zoomCommandFields.store((command.reserved & 0xFFFFu) | (key << 16) |
                                (command.zoom == 1 ? 0x01000000u : 0u), std::memory_order_relaxed);
        zoomCommandAt.store(command.at, std::memory_order_release);
    }
    auto& data = channel.Data()->telemetry;
    data.fullbrightAvailable = AvailabilityBits();
    data.fpsAt = 0; data.fps = 0;
    frames.Read(now, data.fps, data.fpsAt);
    MyClientCompositor::FrameGenStatus(channel.Data()->presentedFps, channel.Data()->frameGen);
    if (MyClientCompositor::Gpu gpu{}; MyClientCompositor::GpuInfo(gpu)) {   // Debug Menu
        auto& shared = *channel.Data();
        std::memcpy(shared.gpuName, gpu.name, sizeof(shared.gpuName));
        shared.gpuName[sizeof(shared.gpuName) - 1] = 0;
        shared.gpuDirectX = gpu.directX; shared.gpuFeatureLevel = gpu.featureLevel; shared.gpuVideoMb = gpu.videoMb;
        shared.gpuVendor = gpu.vendor; shared.gpuDriver = gpu.driver;
    }
    // Name tag: the lease is read here (once per frame) so the per-actor hook stays lock-free.
    const bool wantNametag = MyClientFeatures::WantsNametag(channel.Data()->command, now);
    {
        const bool nick = MyClientFeatures::Fresh(channel.Data()->command.at, now, MyClientFeatures::CommandLeaseMs * 4) &&
                          ((channel.Data()->command.reserved >> 21) & 1u) != 0 && channel.Data()->nick[0] != 0;
        if (nick) { channel.Data()->nick[31] = 0; nickName = channel.Data()->nick; }
        nickWanted.store(nick);
    }
    nametagWanted.store(wantNametag || nickWanted.load());
    {
        const bool replay = MyClientFeatures::Fresh(channel.Data()->command.at, now, MyClientFeatures::CommandLeaseMs * 4) &&
                            ((channel.Data()->command.reserved >> 22) & 1u) != 0;
        if (replay) replayWantedUntil = now + 1500;
        {
            const bool fresh = MyClientFeatures::Fresh(channel.Data()->command.at, now, MyClientFeatures::CommandLeaseMs * 4);
            hitEffectCrit.store(fresh && ((channel.Data()->command.reserved >> 30) & 1u) != 0);
            hitEffectMagic.store(fresh && ((channel.Data()->command.reserved >> 31) & 1u) != 0);
        }
        replayBufferAllowed.store(MyClientFeatures::Fresh(channel.Data()->command.at, now, MyClientFeatures::CommandLeaseMs * 4) &&
                                  ((channel.Data()->command.reserved >> 29) & 1u) != 0);
        const bool keep = now < replayWantedUntil;
        replayWanted.store(keep);
        UpdateReplay(keep, *channel.Data(), now);
    }
    const auto appliedAt = nametagAppliedAt.load();
    const bool appliedNow = wantNametag && appliedAt != 0 && appliedAt <= now && now - appliedAt < 1000;
    data.fullbrightActive = MyClientFeatures::WithFlag(data.fullbrightActive, MyClientFeatures::FlagNametag, appliedNow);
    // Texture packs: one request per change of the HUD's counter (never on the first sight of it).
    liveTextureWanted.store(MyClientFeatures::WantsLiveTextures(channel.Data()->command, now) || liveTextureKnown, std::memory_order_relaxed);
    if (MyClientFeatures::TextureReloadRequested(channel.Data()->command, now, textureSeen, texturePrimed)) {
        textureReloadPending.store(true);
        // When Done was pressed, to line up with the FPS in the log (2026-11-03: the game ran at 10 FPS for minutes).
        MyClientLog::Get().info("Texture packs: the HUD asked for a reload (Done).");
    }
    {
        const bool fresh = MyClientFeatures::Fresh(channel.Data()->command.at, now, MyClientFeatures::CommandLeaseMs * 4);
        const uint32_t field = channel.Data()->command.autoRq;
        autoRqModeValue.store(fresh ? MyClientFeatures::AutoRqModeOf(field) : 0u);
        leftHandWanted.store(fresh && MyClientFeatures::LeftHandOf(field));
        shulkerHoverWanted.store(fresh && MyClientFeatures::ShulkerHoverOf(field));
        {
            // A longer lease of its own (kHudMenuLeaseMs); log when the HUD stops refreshing an open menu.
            const bool menuFlag = MyClientFeatures::HudMenuOf(field);
            const bool menuFresh = MyClientFeatures::Fresh(channel.Data()->command.at, now, kHudMenuLeaseMs);
            static bool staleLogged = false;
            const uint64_t age = now >= channel.Data()->command.at ? now - channel.Data()->command.at : 0;
            if (menuFlag && hudMenuWanted.load() && age > MyClientFeatures::CommandLeaseMs * 4 && !staleLogged) {
                staleLogged = true;
                MyClientLog::Get().warn("HUD menu: no command from the HUD for {} ms (still treated as open).", age);
            }
            if (age <= MyClientFeatures::CommandLeaseMs) staleLogged = false;
            hudMenuWanted.store(menuFresh && menuFlag);
        }
        frameGenField.store(fresh ? (field & 0xF8000000u) : 0u);
        // Render Distance: one request per change of the HUD's counter (never on the first sight of it).
        if (fresh) {
            const uint32_t counter = MyClientFeatures::RenderCounterOf(field);
            const uint32_t chunks = MyClientFeatures::RenderChunksOf(field);
            if (!renderDistancePrimed) { renderDistancePrimed = true; renderDistanceSeen = counter; }
            else if (counter != renderDistanceSeen) {
                renderDistanceSeen = counter;
                if (chunks) renderDistancePending.store(chunks);
            }
            // r57-test28/29 logs: Optimization was already ON when the game started, so the HUD's counter never
            // changed ("requested=4 counter=0") and the distance was never applied. Apply it once in every world
            // the local player enters, and give the player's own distance back when the request goes away (OFF).
            static void* worldPlayer = nullptr;
            static uint32_t lastChunks = 0;
            void* player = nullptr;
            try { if (const auto client = MyClientGame::Client()) player = client->getLocalPlayer(); } catch (...) {}
            if (player && player != worldPlayer && chunks) renderDistancePending.store(chunks);
            worldPlayer = player;
            if (lastChunks && !chunks) renderDistanceRestoreWanted.store(true);
            lastChunks = chunks;
        }
    }
    hitboxFilterValue.store(MyClientFeatures::Fresh(channel.Data()->command.at, now, MyClientFeatures::CommandLeaseMs * 4) ? ((channel.Data()->command.reserved >> 17) & 0xFu) : 0u);
    hitboxRequestedValue.store(MyClientFeatures::Fresh(channel.Data()->command.at, now, MyClientFeatures::CommandLeaseMs * 4) && ((channel.Data()->command.reserved >> 16) & 1u) != 0);
    for (uint32_t k = 0; k < 4; k++)
        hitboxRgbValue[k].store(MyClientFeatures::HitboxRgbOf(channel.Data()->hitboxRgb[k], k), std::memory_order_relaxed);
    {
        // Entity render distance: the local player's position is taken once here for every actor this frame.
        const bool fresh = MyClientFeatures::Fresh(channel.Data()->command.at, now, MyClientFeatures::CommandLeaseMs * 4);
        const uint32_t range = fresh ? MyClientFeatures::EntityDistanceBlocks(channel.Data()->command.reserved) : 0u;
        const uint32_t optimize = fresh ? channel.Data()->optimize : 0u;
        // The current HUD writes its Optimization master into the existing legacy bit; no new command/ABI.
        static int lastOptimizationFlag = -1;
        const int optimizationFlag = (optimize & MyClientFeatures::UncapFpsBit) ? 1 : 0;
        optimizationAuditMaster.store(fresh ? optimizationFlag : -1);
        if (lastOptimizationFlag != optimizationFlag) {
            lastOptimizationFlag = optimizationFlag;
            try { if (const auto client = MyClientGame::Client())
                MyClientLog::Get().info("Render Distance audit: HUD Optimization flag={} fresh={} setting={} chunks; requested={} chunks counter={}",
                    optimizationFlag, fresh, client->getOptions().getViewDistanceChunks(),
                    MyClientFeatures::RenderChunksOf(channel.Data()->command.autoRq),
                    MyClientFeatures::RenderCounterOf(channel.Data()->command.autoRq));
            } catch (...) {}
        }
        // Per-item release audit; these are requests to stop overrides, not proof of a changed world radius.
        const auto releaseExtras = MyClientFeatures::DecodeOptimizeExtras(optimize);
        static uint32_t previous[9]{};
        const uint32_t current[9] = {range, MyClientFeatures::PlayerDistanceBlocks(optimize), releaseExtras.verticalRows,
            releaseExtras.blockEntityBlocks, uint32_t(releaseExtras.noSky), uint32_t(releaseExtras.cullBehind),
            uint32_t((optimize & MyClientFeatures::HideNameTagsBit) != 0),
            uint32_t((optimize & MyClientFeatures::UncapFpsBit) != 0), uint32_t(releaseExtras.noClouds)};
        const char* labels[9] = {"entity distance", "player distance", "vertical rows", "block entity distance",
            "sky", "behind-camera culling", "name tags", "FPS limit", "clouds"};
        for (size_t i = 0; i < 9; ++i) {
            if (previous[i] && !current[i]) MyClientLog::Get().info("Optimization: {} override released; native behavior requested", labels[i]);
            previous[i] = current[i];
        }
        frameGenWide.store(fresh ? channel.Data()->frameGenMultiplier : 0u);
        const uint32_t playerRange = MyClientFeatures::PlayerDistanceBlocks(optimize);
        float range2 = 0.f, playerRange2 = 0.f;
        try {
            const auto client = MyClientGame::Client();
            const auto extras = MyClientFeatures::DecodeOptimizeExtras(optimize);
            auto* local = (range || playerRange || extras.blockEntityBlocks || extras.cullBehind) && client ? client->getLocalPlayer() : nullptr;
            if (local) {
                const auto pos = local->getPosition();
                cullX.store(pos.x); cullY.store(pos.y); cullZ.store(pos.z);
                cullLocal.store(static_cast<const Actor*>(local));
                range2 = static_cast<float>(range) * static_cast<float>(range);
                playerRange2 = static_cast<float>(playerRange) * static_cast<float>(playerRange);
            }
        } catch (...) { range2 = 0.f; playerRange2 = 0.f; }
        cullPlayers.store(fresh && MyClientFeatures::EntityDistancePlayers(channel.Data()->command.reserved));
        cullKinds.store(optimize & (MyClientFeatures::CullMobsBit | MyClientFeatures::CullItemsBit | MyClientFeatures::CullMiscBit));
        cullPlayerRange2.store(playerRange2);
        hideNameTagsWanted.store((optimize & MyClientFeatures::HideNameTagsBit) != 0);
        uncapFpsWanted.store((optimize & MyClientFeatures::UncapFpsBit) != 0);
        {
            const uint32_t code = MyClientFeatures::FpsLimitCodeOf(optimize);
            const uint32_t limit = MyClientFeatures::FpsLimitOf(code, MyClientCompositor::DisplayHz());
            if (limit != fpsLimitWanted.exchange(limit)) MyClientLog::Get().info("FPS limit: {}", limit ? std::to_string(limit) + " FPS" : std::string("none"));
        }
        cullRange2.store(range2);
        {
            const auto extras = MyClientFeatures::DecodeOptimizeExtras(optimize);
            verticalRowsWanted.store(extras.verticalRows, std::memory_order_relaxed);
            const float blocks = static_cast<float>(extras.blockEntityBlocks);
            // Needs the player's position above; without it the distance stays off this frame.
            blockEntityRange2.store(cullLocal.load(std::memory_order_relaxed) ? blocks * blocks : 0.f, std::memory_order_relaxed);
            noSkyWanted.store(extras.noSky, std::memory_order_relaxed);
            noCloudsWanted.store(extras.noClouds, std::memory_order_relaxed);
            cullBehindWanted.store(extras.cullBehind, std::memory_order_relaxed);
        }
    }
    // Block Outline colour: same once-per-frame lease read, plus two status bits for the HUD.
    const bool wantOutline = MyClientFeatures::WantsOutline(channel.Data()->command, now);
    outlineWanted.store(wantOutline);
    outlineRgbValue.store(channel.Data()->command.outlineRgb & 0xFFFFFFu);
    const auto ranAt = outlineRanAt.load();
    const auto coloredAt = outlineColoredAt.load();
    data.fullbrightActive = MyClientFeatures::WithFlag(data.fullbrightActive, MyClientFeatures::FlagOutline,
        wantOutline && ranAt != 0 && ranAt <= now && now - ranAt < 1000);
    data.fullbrightActive = MyClientFeatures::WithFlag(data.fullbrightActive, MyClientFeatures::FlagOutlineColored,
        wantOutline && coloredAt != 0 && coloredAt <= now && now - coloredAt < 1000);
    channel.Data()->stats = MyClientFeatures::Stats{
        now,
        statLightBuild.load(std::memory_order_relaxed), statLightSplit.load(std::memory_order_relaxed),
        statLightCreate.load(std::memory_order_relaxed), statNameTag.load(std::memory_order_relaxed),
        statNameTagLocal.load(std::memory_order_relaxed), statOutline.load(std::memory_order_relaxed),
        statTessColor.load(std::memory_order_relaxed),
        (wantOutline ? 0x01000000u : 0u) | (outlineRgbValue.load(std::memory_order_relaxed) & 0xFFFFFFu),
        statTextureReloads.load(std::memory_order_relaxed), gameScreenBits.load(std::memory_order_relaxed)};
    if (serverLookedUp)
        MyClientFeatures::SetServer(channel.Data()->server, serverHost.c_str(), serverPort, serverTransport, now);
    channel.Unlock();
}

// The game's settings screen (Global Resources) lists the packs from the resource content manager, which read
// global_resource_packs.json once and kept its own copy: after a switch from the HUD it still showed the old
// packs (and saved them back when it was left). Re-reading its sources makes it show the new list.
void RefreshPackScreens(IMinecraftGame& game)
{
    IContentManager& content = game.getResourceContentManager();
    if (!content.isInitialized()) return;
    (void)content.reloadSources(false);   // false: re-read only, never write the file the HUD just wrote
}

// The pack manager tells its listeners (the game, the screens) that the active packs changed: the same call
// the game makes when packs are switched in its own settings. Used only when the reload did not do it.
void NotifyPacksChanged(ResourcePackManager& manager)
{
    std::vector<ResourcePackListener*> listeners;
    for (const auto& listener : *manager.mListeners)
        if (listener.get()) listeners.push_back(listener.get());
    for (ResourcePackListener* listener : listeners)
        if (listener) listener->onActiveResourcePacksChanged(manager);
}

// In a world the game re-reads the list but keeps drawing with the packs it composed at the join (blocks
// included: nothing changed until a rejoin). Put the list into the global stack again and compose the full
// stack, as the game's own settings screen ends up doing; the listeners
// are told afterwards (NotifyPacksChanged).
// ResourcePackManager::setStack / _composeFullStack are not in the SDK's import library: found by name.
// ResourcePackManager::mFullStack exists only before 26.60 (-1 there: not known).
template <class Manager>
int FullStackSize(Manager const& manager)
{
    if constexpr (requires { manager.mFullStack; })
        return manager.mFullStack.get() ? (int)manager.mFullStack.get()->mStack.get().size() : -1;
    else
        return -1;
}

void NoteSubpackIndices(const char* label, ResourcePackStack const* stack, MyClientLog::Logger& log)
{
    if (!stack) { log.info("Texture packs: {} stack is absent", label); return; }
    std::string indices;
    size_t count = 0;
    for (const auto& pack : stack->mStack.get()) {
        if (count++ >= 128) { indices += ",..."; break; }
        if (!indices.empty()) indices += ',';
        indices += std::to_string(static_cast<int>(pack.mSubpackIndex));
    }
    log.info("Texture packs: {} stack subpack indices (priority order)=[{}]", label, indices);
}

// r57-test29 (B): after a live switch on Hive every server-pack picture went (custom blocks, item icons, the
// lobby's statues and sign) while vanilla stayed. Count each stack before and after the switch.
template <class Manager>
void NoteStackSizes(const char* when, Manager const& manager, MyClientLog::Logger& log)
{
    const auto size = [](ResourcePackStack const* stack) { return stack ? static_cast<int>(stack->mStack.get().size()) : -1; };
    int full = -1;
    if constexpr (requires { manager.mFullStack; }) full = size(manager.mFullStack.get());
    log.info("Texture diag: stacks {}: level={} global={} treatment={} baseGame={} full={} useGlobal={}", when,
             size(manager.mLevelStack.get()), size(manager.mGlobalStack.get()), size(manager.mTreatmentStack.get()),
             size(manager.mBaseGameStack.get()), full, static_cast<bool>(manager.mUseGlobalPackStack));
}
template <class Manager>
void NoteFullSubpacks(Manager const& manager, MyClientLog::Logger& log)
{
    if constexpr (requires { manager.mFullStack; })
        NoteSubpackIndices("full", manager.mFullStack.get(), log);
}

std::atomic<bool> skinRefreshWanted{false};
// 2026-11-03 (#109): the arms and dolls went see-through right at the reload, but the skin was handed back only when
// the follow-up ended (block textures in, or the 20 s timeout). It is now also handed back shortly after each reload.
std::atomic<uint64_t> skinRefreshAt{0};
void RefreshSkinSoon() { skinRefreshAt.store(GetTickCount64() + 1500); }

// Chunks keep the block textures they were built with: build them again with the new ones.
// BlockGraphics::reloadBlockTextures(ResourcePackManager&, Experiments const&): the table of where each block's
// pictures sit in the terrain atlas. After a switch in a world the atlas is new but this table was not (r57-test8/9:
// chunks rebuilt with the old positions, blocks drawn from the wrong parts of the new atlas). Not exported and not in
// LeviLamina's headers by address: found in the stored game images (Release game-dumps) as the smaller of the two
// functions that read "blocks.json" (the other, much larger, is initBlocks), called by the game with
// (pack manager, &levelData + offset). Per exe build; the first 24 bytes are compared before it is called.
struct BlockTextureReload { uint32_t timestamp, imageSize, rva; size_t experimentsOffset; uint8_t head[24]; };
constexpr BlockTextureReload kBlockTextureReload[] = {
    {0x6AA482FDu, 0x12C01000u, 0x601CBA0u, 0x158,
     {0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xEC,0x28,0x01,0x00,0x00,0x48,0x8D,0xAC,0x24,0x80}},
    {0x6AD79EE4u, 0u, 0x68037D0u, 0x160,
     {0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xEC,0x48,0x01,0x00,0x00,0x48,0x8D,0xAC,0x24,0x80}},
};
// r57-test26 diagnostics (B: Hive blocks drawn from the wrong part of the atlas after a live switch): who in the
// game calls the block texture table reload and the terrain atlas update, so OriginX's steps can be compared with
// the game's own on a server. Game frames only, as RVAs; nothing else is recorded.
std::string GameBacktrace()
{
    void* frames[24] = {};
    const USHORT count = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::string out;
    int shown = 0;
    for (USHORT i = 0; i < count && shown < 10; ++i) {
        if (!MyClientHook::InGameImage(frames[i])) continue;
        char buf[24];
        std::snprintf(buf, sizeof buf, "%s0x%llX", shown ? "," : "", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) - base));
        out += buf;
        ++shown;
    }
    return out;
}
using BlockTexReloadFn = void (*)(ResourcePackManager&, const void*);
BlockTexReloadFn blockTexReloadOriginal = nullptr;
std::atomic<bool> blockTexReloadOurs{false};
std::atomic<unsigned> blockTexReloadGameCalls{0};
void BlockTexReloadDetour(ResourcePackManager& manager, const void* experiments)
{
    const bool ours = blockTexReloadOurs.load();
    if (!ours && blockTexReloadGameCalls.fetch_add(1) < 12)
        MyClientLog::Get().info("Texture diag: the game reloads the block texture table (manager={} live={} thread={} callers={})",
                               static_cast<const void*>(&manager), liveTextureBusy.load(), GetCurrentThreadId(), GameBacktrace());
    blockTexReloadOriginal(manager, experiments);
    if (!ours) MyClientLog::Get().info("Texture diag: the game's block texture table reload returned");
}
bool InstallBlockTexReloadDiag()
{
    const auto module = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
    for (auto const& row : kBlockTextureReload)
        if (row.timestamp == nt->FileHeader.TimeDateStamp && std::memcmp(module + row.rva, row.head, sizeof row.head) == 0)
            return MyClientHook::Install(module + row.rva, reinterpret_cast<void*>(&BlockTexReloadDetour),
                                         reinterpret_cast<void**>(&blockTexReloadOriginal)) == 0;
    return false;
}
bool ReloadBlockTextures(IMinecraftGame& game, Level& level)
{
    const auto module = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    for (auto const& row : kBlockTextureReload)
    {
        if (row.timestamp != nt->FileHeader.TimeDateStamp) continue;
        const uint8_t* code = module + row.rva;
        // Once the diagnostic hook is in, the first bytes are its jump: it was checked before it was put in.
        if (!blockTexReloadOriginal && std::memcmp(code, row.head, sizeof(row.head)) != 0) {
            MyClientLog::Get().warn("Texture packs: block texture table reload: the function's bytes differ; not called");
            return false;
        }
        auto& data = level.getLevelData();
        const auto* experiments = reinterpret_cast<const char*>(&data) + row.experimentsOffset;
        if (experiments != reinterpret_cast<const char*>(&data.mExperiments)) {
            MyClientLog::Get().warn("Texture packs: block texture table reload: LevelData layout differs ({} vs {}); not called",
                                    row.experimentsOffset, reinterpret_cast<const char*>(&data.mExperiments) - reinterpret_cast<const char*>(&data));
            return false;
        }
        // The game's own caller (26.51 0x868410, a callback on the client) passes the ClientInstance's pack
        // manager ([client+0x218]), not MinecraftGame's. On a server (Hive, r57-test24: every block drawn from
        // the wrong part of the atlas, different on each switch) the two can differ; use the client's like the game.
        ResourcePackManager* manager = &game.getResourcePackManager();
        if (const auto client = MyClientGame::Client()) {
            ResourcePackManager* own = &client->getResourcePackManager();
            MyClientLog::Get().info("Texture packs: block texture table uses the client's pack manager (same as the game's={})", own == manager);
            manager = own;
        }
        MyClientLog::Get().info("Texture diag: OriginX reloads the block texture table (manager={} callers={})", static_cast<const void*>(manager), GameBacktrace());
        using ReloadFn = void (*)(ResourcePackManager&, const void*);
        blockTexReloadOurs.store(true);
        struct Clear { ~Clear() { blockTexReloadOurs.store(false); } } clear;
        reinterpret_cast<ReloadFn>(const_cast<uint8_t*>(code))(*manager, experiments);
        return true;
    }
    MyClientLog::Get().warn("Texture packs: block texture table reload: unknown game build");
    return false;
}

bool RebuildChunks(ClientInstance& client)
{
    if (!client.getLocalPlayer()) return false;
    LevelRenderer* renderer = client.getLevelRenderer();
    if (!renderer) return false;
    return MyClientGame::RebuildAllChunks(*renderer);
}

// The HUD menu is open right now: read straight from the channel, because the game reports the lost focus
// before the next frame has copied the HUD's flag into hudMenuWanted.
bool HudMenuOpenNow()
{
    std::lock_guard guard(featureMutex);
    if (!active.load() || !channel.Data() || !channel.Lock()) return hudMenuWanted.load();
    const auto command = channel.Data()->command;
    channel.Unlock();
    return MyClientFeatures::Fresh(command.at, GetTickCount64(), kHudMenuLeaseMs) &&
           MyClientFeatures::HudMenuOf(command.autoRq);
}

// While the HUD menu is open the game keeps the focus it was not told it lost (FocusLostHook), so it went on turning
// the camera with the mouse behind the menu. The game window is subclassed: while the menu is open, raw mouse input
// that only moves (no button or wheel) and WM_MOUSEMOVE are kept from the game. Buttons and keys still pass, so
// nothing stays held down.
WNDPROC gameWndOriginal = nullptr;
HWND gameWndHooked = nullptr;

LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (hudMenuWanted.load(std::memory_order_relaxed)) {
        if (msg == WM_MOUSEMOVE) { statMouseHeld.fetch_add(1, std::memory_order_relaxed); return 0; }
        if (msg == WM_INPUT) {
            RAWINPUT input{};
            UINT size = sizeof(input);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                input.header.dwType == RIM_TYPEMOUSE && input.data.mouse.usButtonFlags == 0) {
                statMouseHeld.fetch_add(1, std::memory_order_relaxed);
                return DefWindowProcW(hwnd, msg, wParam, lParam);   // lets Windows free the input
            }
        }
    }
    const WNDPROC original = gameWndOriginal;
    return original ? CallWindowProcW(original, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The game reads the mouse through GameInput, which keeps reporting while the game is not in front, and feeds every
// mouse event into MouseDevice::feed. While the HUD menu is open, pure movement (action 0: no button, no wheel) is
// not passed on, so the camera stays still behind the menu; buttons and the wheel still pass, so nothing stays held.
constexpr const char* MouseFeedSymbol = "?feed@MouseDevice@@QEAAXDCFFFF_N@Z";
bool mouseFeedHooked = false;

MYCLIENT_TYPE_INSTANCE_HOOK(MouseFeedHook, ll::memory::HookPriority::Normal, MouseDevice,
                      ll::memory::SymbolView{MouseFeedSymbol}, void,
                      char actionButtonId, schar buttonData, short x, short y, short dx, short dy, bool forceMotionlessPointer)
{
    if (actionButtonId == 0 && hudMenuWanted.load(std::memory_order_relaxed)) {
        statRawHeld.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    origin(actionButtonId, buttonData, x, y, dx, dy, forceMotionlessPointer);
}

// The game's main window: the biggest visible top-level window of this process.
BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM param)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    RECT r{};
    GetClientRect(hwnd, &r);
    auto* best = reinterpret_cast<std::pair<HWND, LONG>*>(param);
    const LONG area = (r.right - r.left) * (r.bottom - r.top);
    if (area > best->second) *best = {hwnd, area};
    return TRUE;
}

// Called every frame from the game thread; looks for the window at most once a second.
void EnsureGameWindowHook()
{
    if (gameWndHooked && IsWindow(gameWndHooked)) return;
    static uint64_t lastTry = 0;
    const uint64_t now = GetTickCount64();
    if (now - lastTry < 1000) return;
    lastTry = now;
    std::pair<HWND, LONG> best{nullptr, 0};
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&best));
    if (!best.first) return;
    const auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(best.first, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameWndProc)));
    if (!previous) {
        MyClientLog::Get().warn("HUD menu: could not hook the game window's input (error {}).", GetLastError());
        gameWndHooked = best.first;   // do not retry every second
        return;
    }
    gameWndOriginal = previous;
    gameWndHooked = best.first;
    wchar_t cls[64]{};
    GetClassNameW(best.first, cls, 64);
    char name[64]{};
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, name, sizeof name, nullptr, nullptr);
    MyClientLog::Get().info("HUD menu: game window input hooked ({}).", name);
}

// Opening the HUD menu takes the focus from the game, and the game answers a lost focus with its pause screen.
// While the HUD menu is open the lost focus is not passed on, so the menu opens straight over play.
// onAppFocusLost is an AppPlatformListener function of MinecraftGame; `this` is only passed through.
MYCLIENT_TYPE_INSTANCE_HOOK(FocusLostHook, ll::memory::HookPriority::Normal, IMinecraftGame,
                      ll::memory::SymbolView{"?onAppFocusLost@MinecraftGame@@UEAAXXZ"}, void)
{
    if (HudMenuOpenNow()) {
        heldFocusLost.store(this);
        return;
    }
    heldFocusLost.store(nullptr);
    origin();
}

// The HUD menu closed: when the focus went to another app instead of back to the game, the game gets the lost
// focus it was not told about (else it keeps the mouse for its camera behind that app).
void ReleaseHeldFocusLost()
{
    void* game = heldFocusLost.exchange(nullptr);
    if (!game) return;
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    if (foregroundPid == GetCurrentProcessId()) return;
    reinterpret_cast<FocusLostHook*>(game)->origin();
    // Which window was in front, to find what made the HUD close its menu.
    wchar_t cls[64]{}, title[64]{};
    const HWND front = GetForegroundWindow();
    if (front) { GetClassNameW(front, cls, 64); GetWindowTextW(front, title, 64); }
    char clsUtf8[128]{}, titleUtf8[128]{};
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, clsUtf8, sizeof clsUtf8, nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, title, -1, titleUtf8, sizeof titleUtf8, nullptr, nullptr);
    MyClientLog::Get().info(
        "HUD menu closed with another app in front: told the game it lost the focus (front window: class '{}', title '{}', pid {}).",
        clsUtf8, titleUtf8, foregroundPid);
}

// Reads the game's screens once per frame (game thread). Opening the HUD menu takes the focus from the game,
// and the game answers a lost focus with its pause screen: a pause screen that came up while the HUD menu was
// opening (or open) is closed again when the menu closes, so G goes back straight to playing. With FocusLostHook
// in place that pause screen does not come up at all; this stays for game builds where the hook is missing.
void TrackGameScreens()
{
    EnsureGameWindowHook();
    static std::string lastName;
    static bool menuWas = false, pauseWas = false, pausedByHud = false;
    static uint64_t pauseSince = 0;
    try {
        const auto client = MyClientGame::Client();
        if (!client) { gameScreenBits.store(0); return; }
        const bool inWorld = client->getLocalPlayer() != nullptr;
        const std::string name = client->getTopScreenName();
        // For the HUD's in-world display: also no loading / saving / disconnect / title screen on top
        // (the player and level can outlive the world for a moment while it is left or joined).
        const bool playing = inWorld && MyClientGame::WorldLevel() &&
                             name.find("progress") == std::string::npos && name.find("loading") == std::string::npos &&
                             name.find("start_screen") == std::string::npos && name.find("disconnect") == std::string::npos;
        // Playing = the HUD screen on top ("hud_screen"); anything else over it is a game menu.
        const bool menu = inWorld && name.find("hud_screen") == std::string::npos;
        const bool pause = menu && name.find("pause") != std::string::npos;
        // The title screen itself (for the HUD's Replays button), not the Play / Settings screens opened from it.
        // 26.x: the home screen is the Ore UI page "ui://./hbui/index.html"; "start_screen" is on top only for a moment
        // (r57-test54: the Replays button showed for <1 s and was gone). The Ore UI page also hosts Play etc., which
        // cannot be told apart by name; the button showing there too is accepted.
        const bool title = !inWorld && (name.find("start_screen") != std::string::npos || name.find("hbui/index.html") != std::string::npos);
        gameScreenBits.store(MyClientFeatures::GameScreenKnown | (menu ? MyClientFeatures::GameMenuOpen : 0u) |
                             (pause ? MyClientFeatures::GamePaused : 0u) | MyClientFeatures::GameWorldKnown |
                             (playing ? MyClientFeatures::GameInWorld : 0u) | MyClientFeatures::GameTitleKnown |
                             (title ? MyClientFeatures::GameOnTitle : 0u));
        if (name != lastName) {
            MyClientLog::Get().info("Game screen: {} (menu={})", name, menu);
            lastName = name;
        }
        const uint64_t now = GetTickCount64();
        const bool hudMenu = hudMenuWanted.load();
        if (pause && !pauseWas) pauseSince = now;
        if (!pause) pausedByHud = false;
        // The HUD's flag arrives a little after its window took the focus: a pause from just before counts too.
        else if (hudMenu && (!pauseWas || (!menuWas && now - pauseSince < 1500))) pausedByHud = true;
        if (!hudMenu && menuWas && pausedByHud && pause) {
            client->getCurrentSceneStack()->schedulePopScreenWithExpectedNames({name});
            pausedByHud = false;
            MyClientLog::Get().info("Closed the pause screen the HUD menu caused ({}).", name);
        }
        // Menu closed: the HUD hands the focus back to the game a moment later, so whether the focus went to
        // another app is decided once the game is in front again or kFocusSettleMs have passed (deciding on the
        // first frame told the game it lost the focus while the HUD's window was still in front: pause screen).
        static uint64_t closedAt = 0;
        constexpr uint64_t kFocusSettleMs = 500;
        if (!hudMenu && menuWas) closedAt = now ? now : 1;
        if (hudMenu) closedAt = 0;
        if (closedAt && heldFocusLost.load()) {
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            if (foregroundPid == GetCurrentProcessId()) {
                heldFocusLost.store(nullptr);   // the game has the focus back: nothing to tell it
                closedAt = 0;
            } else if (now - closedAt >= kFocusSettleMs) {
                ReleaseHeldFocusLost();
                closedAt = 0;
            }
        } else if (closedAt && now - closedAt >= kFocusSettleMs) closedAt = 0;
        // Diagnosis: a pause screen shortly after the menu closed (G should go straight back to playing).
        static uint64_t lastClose = 0;
        if (!hudMenu && menuWas) lastClose = now;
        if (pause && !pauseWas && !hudMenu && lastClose && now - lastClose < 2000)
            MyClientLog::Get().info("Pause screen {} ms after the HUD menu closed ({}).", now - lastClose, name);
        menuWas = hudMenu;
        pauseWas = pause;
    } catch (...) { gameScreenBits.store(0); }
}

std::wstring Utf8Wide(const std::string& text)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), n);
    return wide;
}
std::string WideUtf8(const std::wstring& text)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), n, nullptr, nullptr);
    return utf8;
}

// The HUD writes OriginX Lite into the data folders it finds (Users\<id>\games\com.mojang and Shared). When the game
// reads packs from another folder, the newest written copy is put there too. Returns the source folder, or empty
// when nothing was copied (none found, already the same, or the copy failed).
std::string CopyLitePackTo(std::filesystem::path gamePacks)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    gamePacks = gamePacks.lexically_normal();
    if (!gamePacks.has_filename()) gamePacks = gamePacks.parent_path();   // "...\resource_packs\"
    if (!fs::is_directory(gamePacks, ec)) return {};
    const fs::path target = gamePacks / "OriginX_Lite";
    auto manifestOf = [](const fs::path& folder) {
        std::ifstream in(folder / "manifest.json", std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    // ...\Users\<id>\games\com.mojang\resource_packs: every user's (and Shared's) copy.
    const fs::path users = gamePacks.parent_path().parent_path().parent_path().parent_path();
    std::vector<fs::path> candidates;
    for (fs::directory_iterator it(users, ec), end; !ec && it != end; it.increment(ec))
        candidates.push_back(it->path() / "games" / "com.mojang" / "resource_packs" / "OriginX_Lite");
    fs::path best;
    fs::file_time_type bestTime{};
    for (const auto& folder : candidates) {
        std::error_code e;
        if (folder == target || !fs::is_regular_file(folder / "manifest.json", e)) continue;
        const auto t = fs::last_write_time(folder / "manifest.json", e);
        if (e) continue;
        if (best.empty() || t > bestTime) { best = folder; bestTime = t; }
    }
    if (best.empty()) return {};
    ec.clear();
    if (fs::is_regular_file(target / "manifest.json", ec) && manifestOf(target) == manifestOf(best)) return {};
    fs::remove_all(target, ec);
    ec.clear();
    fs::copy(best, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) return {};
    return WideUtf8(best.wstring());
}

// Whether the game's pack repository knows OriginX Lite, and the folders it reads packs from (the HUD writes the
// pack into the folders it guesses; textures_scan.txt lists them). Once per reload while it is unknown, else once.
void NoteLitePack(ClientInstance& client)
{
    static bool toldFound = false;
    auto& repository = client.getResourcePackRepository();
    // mce::UUID::fromString is not in the SDK's import library: the 32 hex digits are read here, the first 16 as one
    // number and the last 16 as the other. Which half goes in `a` is not documented, so both orders are asked.
    uint64_t high = 0, low = 0;
    int digits = 0;
    for (const char* c = LitePack::kPackUuid; *c; ++c) {
        int v = -1;
        if (*c >= '0' && *c <= '9') v = *c - '0';
        else if (*c >= 'a' && *c <= 'f') v = *c - 'a' + 10;
        else if (*c >= 'A' && *c <= 'F') v = *c - 'A' + 10;
        if (v < 0) continue;
        uint64_t& half = digits < 16 ? high : low;
        half = (half << 4) | static_cast<uint64_t>(v);
        ++digits;
    }
    auto found = repository.getResourcePacksByUUID(mce::UUID(high, low));
    if (found.empty()) found = repository.getResourcePacksByUUID(mce::UUID(low, high));
    if (!found.empty() && toldFound) return;
    toldFound = !found.empty();
    std::string packs, development;
    try { packs = repository.getResourcePacksPath().value; } catch (...) {}
    try { development = repository.getDevelopmentResourcePacksPath().value; } catch (...) {}
    size_t invalid = 0;
    try { invalid = repository.getInvalidPacks().size(); } catch (...) {}
    auto& log = MyClientLog::Get();
    log.info("Texture packs: OriginX Lite {} in the game's packs; the game reads resource packs from '{}' and '{}' ({} invalid packs).",
             found.empty() ? "is NOT" : "is", packs, development, invalid);
    if (found.empty() && !packs.empty()) {
        const std::string copied = CopyLitePackTo(std::filesystem::path(Utf8Wide(packs)));
        if (!copied.empty()) {
            log.info("Texture packs: OriginX Lite copied from '{}' into the game's pack folder; reloading again.", copied);
            textureReloadPending.store(true);
        }
    }
}

// Equivalent to the SDK's addCustomResourcePackPath: use the game's factory-owned source, then attach it to
// the repository. No LeviLamina helper/import is needed. The interface returned by ClientInstance is the
// ResourcePackRepository (its first base). Only our isolated generated-pack directory is rediscovered.
DirectoryPackSource* AttachGeneratedPackSource(RepositorySources& sources, PackSourceFactory& factory)
{
    wchar_t local[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!length || length >= MAX_PATH) return nullptr;
    const auto root = LitePack::GeneratedPackRoot(std::filesystem::path(local));
    std::error_code ec;
    if (root.empty() || !std::filesystem::is_regular_file(root / L"OriginX_Lite" / L"manifest.json", ec)) return nullptr;
    if (!sources.mPackSource) return nullptr;
    const Core::Path path(root);
    DirectoryPackSource* source = factory.getDirectoryPackSource(path, PackType::Resources);
    if (!source) source = &factory.createDirectoryPackSource(path, PackType::Resources, PackOrigin::Dev, false);
    if (!source) return nullptr;
    auto& entries = sources.mPackSource->mPackSources.get();
    const bool added = std::find(entries.begin(), entries.end(), source) == entries.end();
    if (added) entries.push_back(source);   // lifetime belongs to the factory, like the SDK helper
    source->mDiscovered = false;           // this generated directory can change after every HUD request
    MyClientLog::Get().info("Texture packs: generated pack source prepared '{}' (added={})", WideUtf8(root.wstring()), added);
    return source;
}

// User-pack discovery can reconstruct RepositorySources after a caller adds a custom source. Attach at the
// end of that native initialization as well, before the freshly built source list is consumed by load tasks.
constexpr const char* SourcesInitSymbol = "?initializePackSource@RepositorySources@@QEAAXAEAVPackSourceFactory@@@Z";
bool sourcesInitAttempted = false;
MYCLIENT_TYPE_INSTANCE_HOOK(SourcesInitHook, ll::memory::HookPriority::Normal, RepositorySources,
                           MyClientGame::Target(SourcesInitSymbol), void, PackSourceFactory& factory)
{
    origin(factory);
    if (!this) return;
    try { AttachGeneratedPackSource(*this, factory); }
    catch (...) { MyClientLog::Get().warn("Texture packs: generated source attachment after initialization failed"); }
}

DirectoryPackSource* PrepareGeneratedPackSource(IResourcePackRepository& repository)
{
    auto* concrete = static_cast<ResourcePackRepository*>(&repository);
    if (!concrete) return nullptr;
    const auto& sources = concrete->mSources.get();
    if (!sources) return nullptr;
    return AttachGeneratedPackSource(*sources, repository.getPackSourceFactory());
}

bool GeneratedSourceAttached(IResourcePackRepository& repository, DirectoryPackSource* source)
{
    if (!source) return false;
    auto* concrete = static_cast<ResourcePackRepository*>(&repository);
    if (!concrete) return false;
    const auto& sources = concrete->mSources.get();
    if (!sources || !sources->mPackSource) return false;
    const auto& entries = sources->mPackSource->mPackSources.get();
    return std::find(entries.begin(), entries.end(), source) != entries.end();
}

struct PackRefreshWait {
    enum class Phase { Idle, Users, Generated } phase = Phase::Idle;
    IResourcePackRepository* repository = nullptr;
    std::shared_ptr<Bedrock::Threading::IAsyncResult<void>> result;
    DirectoryPackSource* generated = nullptr;   // factory-owned; compare only until completion
    uint64_t started = 0;
};

// A world switch is a transaction. The old full stack is never edited in place. World rendering and
// visibility submission stop first; the game's invalidation listener cancels its own work, then its
// composer and resource loader rebuild the stack/atlases. UI and resource-load updates keep running.
struct LiveTextureReload {
    IMinecraftGame* game = nullptr;
    ResourcePackListener* listener = nullptr;   // native secondary base, validated against both callbacks
    Level* level = nullptr;                     // identity only, never dereferenced after a frame boundary
    LevelRenderer* renderer = nullptr;          // identity checked against the current client each frame
    uint64_t started = 0;
    uint32_t terrainBefore = 0;
    bool applied = false, warned = false;
    uint64_t appliedAt = 0;                     // when the game's reload call returned
} liveTexture;
thread_local IMinecraftGame* liveInvalidationGame = nullptr;
thread_local IMinecraftGame* liveComposeGame = nullptr;
void HoldLevelPacks(ResourcePackManager& manager);
void ReturnLevelPacks(const char* where, bool compose = false);
extern ResourcePackManager* heldLevelManager;
// How long the world may stay hidden (r44 kept it hidden for ever when a completion signal never came):
// the resources get LiveApplyWaitMs after the game's reload call; then the world is shown again whatever arrived,
// and the log says which signal was missing.
constexpr uint64_t LiveApplyWaitMs = 12000, LiveBeforeApplyMs = 40000;

constexpr const char* LiveInvalidSymbol = "?onFullPackStackInvalid@MinecraftGame@@UEAA_NXZ";
constexpr const char* LiveInGameSymbol = "?isInGame@MinecraftGame@@UEBA_NXZ";
constexpr const char* LiveRenderSymbol = "?renderLevel@LevelRenderer@@QEAAXAEAVScreenContext@@AEBUFrameRenderObject@@@Z";
constexpr const char* LivePrepSymbol = "?preRenderUpdate@LevelRenderer@@QEAAXAEAVScreenContext@@AEAULevelRenderPreRenderUpdateParameters@@@Z";
constexpr const char* LiveVisibilitySymbol = "?_handleVisibilityUpdates@RenderChunkCoordinator@@QEAAXXZ";
constexpr const char* LiveSceneRenderSymbol = "?_renderLevel@InGamePlayScreen@@UEAAXAEAVScreenContext@@AEBUFrameRenderObject@@@Z";
constexpr const char* LiveScenePrepSymbol = "?_renderLevelPrep@InGamePlayScreen@@UEAAXAEAVScreenContext@@AEAVLevelRenderer@@AEAVActor@@@Z";
constexpr const char* LoadsCompleteSymbol = "?isComplete@ResourceLoadManager@@QEBA_NXZ";
using LoadsCompleteFn = bool (*)(ResourceLoadManager const*);
LoadsCompleteFn liveLoadsComplete = nullptr;

MYCLIENT_TYPE_INSTANCE_HOOK(LiveInGameHook, ll::memory::HookPriority::Normal, IMinecraftGame,
                          MyClientGame::Target(LiveInGameSymbol), bool)
{
    // During the whole of the game's reload call that the live apply makes (and inside its invalidation
    // callback), on this thread and this game object only. r44 answered only inside the callback, and the block
    // textures never came: the reload path asks "in a world?" in more than one place (the older Flarial pack
    // changer answered for the whole call too). No process-wide flag is changed; other threads, normal play and
    // the load callbacks that follow see the real state.
    if (this && (this == liveInvalidationGame || this == liveComposeGame)) return false;
    return origin();
}
// The other "in a world?" question of the same path: ClientInstance::isPreGame (no local player yet). Answered
// "yes" only on this thread during that same reload call. Optional: without it the apply is still tried.
constexpr const char* LivePreGameSymbol = "?isPreGame@ClientInstance@@UEBA_NXZ";
bool livePreGameHooked = false;
MYCLIENT_INSTANCE_HOOK(LivePreGameHook, ll::memory::HookPriority::Normal,
                      MyClientGame::Target(LivePreGameSymbol), bool)
{
    if (liveComposeGame) return true;
    return origin();
}
MYCLIENT_TYPE_INSTANCE_HOOK(LiveInvalidHook, ll::memory::HookPriority::Normal, ResourcePackListener,
                          MyClientGame::Target(LiveInvalidSymbol), bool)
{
    if (!this) return false;
    if (!liveTextureBusy.load() || !liveTexture.game || liveComposeGame != liveTexture.game ||
        this != liveTexture.listener) return origin();
    struct Scope {
        IMinecraftGame* saved = liveInvalidationGame;
        explicit Scope(IMinecraftGame* game) { liveInvalidationGame = game; }
        ~Scope() { liveInvalidationGame = saved; }
    } scope(liveTexture.game);
    return origin();   // retain device-readiness checks and the game's work cancellation, including rejection
}
MYCLIENT_TYPE_INSTANCE_HOOK(LiveWorldRenderHook, ll::memory::HookPriority::Normal, LevelRenderer,
                          MyClientGame::Target(LiveRenderSymbol), void, ScreenContext& screen, FrameRenderObject const& frame)
{
    if (liveTextureBusy.load() && this == liveRendererHeld.load()) return;
    origin(screen, frame);
}
MYCLIENT_TYPE_INSTANCE_HOOK(LiveWorldPrepHook, ll::memory::HookPriority::Normal, LevelRenderer,
                          MyClientGame::Target(LivePrepSymbol), void, ScreenContext& screen, LevelRenderPreRenderUpdateParameters& parameters)
{
    if (liveTextureBusy.load() && this == liveRendererHeld.load()) return;
    origin(screen, parameters);
}
MYCLIENT_TYPE_INSTANCE_HOOK(LiveVisibilityHook, ll::memory::HookPriority::Normal, RenderChunkCoordinator,
                          MyClientGame::Target(LiveVisibilitySymbol), void)
{
    if (liveTextureBusy.load()) return;
    origin();
}
// Schematic spike (docs/schematic-s0-report.md, step 5): draws two test blocks from this hook, before or after the
// game's own world draw (which one is right is what the spike finds out). Defined with the Left Hand hooks below.
std::atomic<bool> schematicSpikeWanted{false};
void SchematicSpikeFrame(ScreenContext& screen, bool beforeWorld);
// Cape HD spike (docs/superpowers/plans/2026-11-19-cape-hd-s0.md): one HD cape picture on a board in the world.
std::atomic<bool> capeHdSpikeWanted{false};
void CapeHdSpikeFrame(ScreenContext& screen);
MYCLIENT_INSTANCE_HOOK(LiveSceneRenderHook, ll::memory::HookPriority::Normal,
                      MyClientGame::Target(LiveSceneRenderSymbol), void, ScreenContext& screen, FrameRenderObject const& frame)
{
    if (liveTextureBusy.load()) return;
    const bool spike = schematicSpikeWanted.load(std::memory_order_relaxed);
    if (spike) SchematicSpikeFrame(screen, true);
    origin(screen, frame);
    if (spike) SchematicSpikeFrame(screen, false);
    if (capeHdSpikeWanted.load(std::memory_order_relaxed)) CapeHdSpikeFrame(screen);
}
// One install of the scene hook for both users of it (the live texture apply and the schematic spike).
bool EnsureSceneRenderHook()
{
    static int state = -1;   // -1 = not tried, 0 = failed, 1 = installed
    if (state < 0) state = MyClientGame::RvaFallback(LiveSceneRenderSymbol) && LiveSceneRenderHook::hook() == 0 ? 1 : 0;
    return state == 1;
}
MYCLIENT_INSTANCE_HOOK(LiveScenePrepHook, ll::memory::HookPriority::Normal,
                      MyClientGame::Target(LiveScenePrepSymbol), void, ScreenContext& screen, LevelRenderer& renderer, Actor& camera)
{
    if (liveTextureBusy.load()) return;
    origin(screen, renderer, camera);
}
bool LiveLoadsIdle(IMinecraftGame& game)
{
    if (!liveLoadsComplete) return false;
    if (liveTextureBusy.load()) MyClientCrash::Stage("get resource load manager");
    const auto loads = game.getResourceLoadManager();
    const auto* pointer = loads.get().get();
    if (liveTextureBusy.load()) MyClientCrash::Stage("resource loadsComplete");
    const bool complete = pointer && liveLoadsComplete(pointer);
    if (liveTextureBusy.load()) MyClientCrash::Stage("resource loadsComplete returned");
    return complete;
}
void EndLiveTextureReload(const char* reason)
{
    MyClientLog::Get().info("Texture packs: live transaction {} (elapsed={} ms)", reason,
                           liveTexture.started ? GetTickCount64() - liveTexture.started : 0);
    ReleaseActorList();
    ReturnLevelPacks("at the end of the live apply");   // only if still held (e.g. the apply stopped early)
    liveTextureBusy.store(false);
    MyClientCrash::reloadActive.store(false);
    liveRendererHeld.store(nullptr);
    liveTexture = {};
    MyClientCrash::Stage("live reload ended");
}
bool BeginLiveTextureReload(ClientInstance& client, bool& waitForLoads)
{
    waitForLoads = false;
    if (!liveTextureReady || liveTextureBusy.load()) return false;
    auto& game = client.getMinecraftGame_DEPRECATED();
    // The 26.51 SDK casts this base to +B8, but the shipped game's constructor installs it at +C0.
    // Use only verified native layouts, and require the actual vtable's two callbacks to match the
    // fingerprint-checked RVA table before accepting the address. Never call through the SDK cast.
    // Per build in tools/sigs.json "offsets" (0 = unknown on this build: the live apply is not used).
    const size_t listenerOffset = MyClientSigs::OffsetOr("MinecraftGame.ResourcePackListener", 0u);
    auto readable = [](const void* pointer, size_t bytes) {
        if (!pointer || !bytes) return false;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto start = reinterpret_cast<uintptr_t>(pointer);
        const auto base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        return start >= base && start - base < info.RegionSize && bytes <= info.RegionSize - (start - base);
    };
    auto* listener = listenerOffset ? reinterpret_cast<ResourcePackListener*>(reinterpret_cast<char*>(&game) + listenerOffset) : nullptr;
    uintptr_t table = 0;
    if (readable(listener, sizeof(table))) std::memcpy(&table, listener, sizeof(table));
    uintptr_t callbacks[3]{};
    if (table && MyClientHook::InGameImage(reinterpret_cast<void*>(table)) &&
        MyClientHook::InGameImage(reinterpret_cast<void*>(table + sizeof(callbacks) - 1)) &&
        readable(reinterpret_cast<void*>(table), sizeof(callbacks)))
        std::memcpy(callbacks, reinterpret_cast<void*>(table), sizeof(callbacks));
    const auto changed = MyClientGame::RvaFallback("?onActiveResourcePacksChanged@MinecraftGame@@UEAAXAEAVResourcePackManager@@@Z");
    const auto invalid = MyClientGame::RvaFallback(LiveInvalidSymbol);
    if (!listener || !changed || !invalid || callbacks[1] != changed || callbacks[2] != invalid) {
        MyClientLog::Get().warn("Texture packs: native listener validation failed (offset={}); live apply declined", listenerOffset);
        return false;
    }
    auto* renderer = client.getLevelRenderer();
    const auto world = MyClientGame::WorldLevel();
    auto* level = world ? &*world : nullptr;
    if (!renderer || !level || !client.getLocalPlayer() || game.getClientInstanceCount() != 1) return false;
    if (!LiveLoadsIdle(game)) { waitForLoads = true; return false; }
    if (!MyClientCompositor::DrainGameGpu()) return false;
    liveTexture = {&game, listener, level, renderer, GetTickCount64(), terrainAtlasUpdates.load()};
    liveRendererHeld.store(renderer);
    liveTextureBusy.store(true);
    MyClientCrash::reloadActive.store(true);
    MyClientLog::Get().info("Texture packs: live transaction began; GPU drained, world drawing/visibility held");
    try { NoteStackSizes("before the live switch", game.getResourcePackManager(), MyClientLog::Get()); } catch (...) {}
    // test33 log: the level stack was already empty when the composition began; OriginX's own user-pack
    // rediscovery / generated-pack refresh (right after this) empties it. Hold the server's packs from here.
    try { HoldLevelPacks(game.getResourcePackManager()); } catch (...) {}
    HoldActorList();
    return true;
}
bool PollLiveTextureReload(ClientInstance& client)
{
    if (!liveTextureBusy.load()) return false;
    auto& state = liveTexture;
    MyClientCrash::Stage("poll world/client validation");
    const auto world = MyClientGame::WorldLevel();
    if (!client.getLocalPlayer() || !world || &*world != state.level ||
        client.getLevelRenderer() != state.renderer || &client.getMinecraftGame_DEPRECATED() != state.game) {
        EndLiveTextureReload("ended after world/client changed");
        return false;
    }
    const uint64_t now = GetTickCount64();
    if (!state.applied) {
        // the pack discovery before the apply never finished: show the world again, the title screen applies it
        if (now - state.started > LiveBeforeApplyMs) { EndLiveTextureReload("gave up before the apply (pack discovery did not finish); left for the title screen"); return false; }
        return false;
    }
    if (!state.appliedAt) state.appliedAt = now;
    MyClientCrash::Stage("poll pending resource restack");
    const bool pending = static_cast<bool>(state.game->getResourcePackManager().mPendingRestack);
    const bool loadsIdle = LiveLoadsIdle(*state.game);
    const bool terrain = terrainAtlasUpdates.load() != state.terrainBefore;
    MyClientCrash::Stage("poll resource/model wait");
    // Bound disk logging to four snapshots/second; every native probe still updates the crash stage.
    static uint64_t lastTrace = 0;
    if (!lastTrace || now - lastTrace >= 250) {
        lastTrace = now;
        MyClientLog::Get().info("Texture trace: waiting (thread={} elapsed={} pendingRestack={} loadsIdle={} terrain={} packsChanged={} inner={} atlas={})",
                               GetCurrentThreadId(), now - state.appliedAt, pending, loadsIdle, terrain,
                               statPacksChanged.load(), statPacksChangedInner.load(), terrainAtlasUpdates.load());
    }
    if (pending || !loadsIdle || !terrain) {
        if (now - state.appliedAt <= LiveApplyWaitMs) return true;
        // Never hide the world for ever. Without the new block textures nothing is rebuilt (the old meshes
        // still match the old atlas); the saved list is applied at the title screen as before.
        MyClientLog::Get().warn("Texture packs: live apply timed out after {} ms (pendingRestack={} loadsIdle={} terrainAtlasUpdated={} packsChanged={} inner={})",
                               now - state.appliedAt, pending, loadsIdle, terrain, statPacksChanged.load(), statPacksChangedInner.load());
        if (!terrain) {
            EndLiveTextureReload("timed out without new block textures; world shown again, left for the title screen");
            return false;
        }
    }
    // The new atlas is in: build the chunks again with it, and show the world. 2026-11-11 (r57, 4 switches out of 4):
    // the world renderer's onDeviceLost, called here before, made the game reload the world's GPU resources, which
    // never finished while the world was held (loadsIdle went false right after it, 5 s timeout every time): the
    // world stayed hidden 7-9 s and came back half reloaded (wrong-looking blocks, heavy afterwards). Chunks that
    // still have the old atlas's meshes are rebuilt by the game's own "all dirty" (as an options change does).
    bool tableReloaded = false;
    // the current world (checked equal to state.level above)
    MyClientCrash::Stage("reload block texture table");
    MyClientLog::Get().info("Texture trace: block texture table reload enter (thread={})", GetCurrentThreadId());
    try { tableReloaded = ReloadBlockTextures(*state.game, *world); } catch (...) { tableReloaded = false; }
    MyClientCrash::Stage("reload block texture table returned");
    MyClientLog::Get().info("Texture packs: block texture table reloaded={}", tableReloaded);
    bool rebuilt = false;
    MyClientCrash::Stage("chunk rebuild");
    MyClientLog::Get().info("Texture trace: chunk rebuild enter (thread={})", GetCurrentThreadId());
    try { rebuilt = RebuildChunks(client); } catch (...) {}
    MyClientLog::Get().info("Texture trace: chunk rebuild returned={}", rebuilt);
    skinRefreshWanted.store(true);
    MyClientCrash::Stage("refresh pack screens");
    MyClientLog::Get().info("Texture trace: refresh pack screens enter");
    try { RefreshPackScreens(*state.game); } catch (...) {}
    MyClientLog::Get().info("Texture trace: refresh pack screens returned");
    EndLiveTextureReload(rebuilt ? "completed: new block textures in, chunk rebuild requested (visual result unverified)"
                                 : "ended: new block textures in, but the chunk rebuild could not be requested (visual result unverified)");
    return true;
}

// Installs the live apply's hooks the first time the user's switch asks for it (never for anyone else: they sit
// on the world's render path, 10452eb). Game thread, after a frame; OwnHook pauses the other threads while it
// patches. One attempt per game run.
bool EnsureLiveTextureHooks()
{
    static bool attempted = false;
    if (attempted) return liveTextureReady;
    attempted = true;
    auto& log = MyClientLog::Get();
    if (!liveTextureKnown) { log.warn("Texture packs: live apply asked for, but this game build is not identified for it"); return false; }
    const bool invalid = LiveInvalidHook::hook() == 0;
    const bool inGame = LiveInGameHook::hook() == 0;
    const bool draw = MyClientGame::Target(LiveRenderSymbol) && LiveWorldRenderHook::hook() == 0;
    const bool prep = MyClientGame::Target(LivePrepSymbol) && LiveWorldPrepHook::hook() == 0;
    const bool visibility = MyClientGame::Target(LiveVisibilitySymbol) && LiveVisibilityHook::hook() == 0;
    // optional: the scene-level callers of the two above, and the second "in a world?" question
    const bool sceneDraw = EnsureSceneRenderHook();
    const bool scenePrep = MyClientGame::RvaFallback(LiveScenePrepSymbol) && LiveScenePrepHook::hook() == 0;
    livePreGameHooked = MyClientGame::Target(LivePreGameSymbol) && LivePreGameHook::hook() == 0;
    liveLoadsComplete = MyClientGame::ResolveFunction<LoadsCompleteFn>(LoadsCompleteSymbol);
    liveTextureReady = liveDrawingComposited && invalid && inGame && draw && prep && visibility &&
                       liveLoadsComplete && packsChangedHooked && atlasHooked;
    log.info("Texture packs: live apply ready={} composited={} invalid={} inGame={} preGame={} draw={} prep={} visibility={} sceneDraw={} scenePrep={} loads={} packsChanged={} atlas={}",
             liveTextureReady, liveDrawingComposited, invalid, inGame, livePreGameHooked, draw, prep, visibility, sceneDraw, scenePrep,
             liveLoadsComplete != nullptr, packsChangedHooked, atlasHooked);
    return liveTextureReady;
}

// Reuse the already-supported connection and realm queries. Unknown is deferred, never guessed local.
bool LocalWorldTextureReload(ClientInstance& client)
{
    // r57-test23 (user decision): live apply on servers too. The Hive crash (dangling actor in extractNameTags
    // while preRenderUpdate is held) is handled in NameTagsHook; set to false to defer servers to the title again.
    constexpr bool kLiveOnServers = true;
    if (kLiveOnServers) return true;
    try {
        return MyClientTexturePolicy::AllowInWorld(MyClientGame::MapConnectionLocality(), client.isInRealm(),
                                                   client.getMinecraftGame_DEPRECATED().isInServer());
    } catch (...) { return false; }
}

MyClientResources::Completion titleReloadCompletion;
std::atomic<bool> titleResourcesHeld{false}, titleAvatarRefreshWanted{false}, titleReloadRetry{false};
IMinecraftGame* titleCompletionGame = nullptr; // identity only, never dereferenced after a client change
bool NativeJsonChatOnStack(ClientInstance& client)
{
    uint64_t high = 0, low = 0; int digits = 0;
    for (const char* p = JavaChatPack::Uuid; *p; ++p) {
        int value = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : -1;
        if (value < 0) continue;
        auto& half = digits++ < 16 ? high : low; half = (half << 4) | uint64_t(value);
    }
    auto& repository = client.getResourcePackRepository();
    auto found = repository.getResourcePacksByUUID(mce::UUID(high, low));
    if (found.empty()) found = repository.getResourcePacksByUUID(mce::UUID(low, high));
    const auto& manager = client.getMinecraftGame_DEPRECATED().getResourcePackManager();
    const auto contains = [&](const ResourcePackStack* stack) {
        if (!stack) return false;
        for (const auto& entry : stack->mStack.get()) {
            const auto* pack = entry.mPack.get().get(); // not_null<shared_ptr<ResourcePack>>, no new native call
            if (pack && std::find(found.begin(), found.end(), pack) != found.end()) return true;
        }
        return false;
    };
    // mFullStack exists only before 26.60; there the global stack alone is checked.
    return contains(manager.mGlobalStack.get()) && [&](auto const& m) {
        if constexpr (requires { m.mFullStack; }) return contains(m.mFullStack.get());
        else return true;
    }(manager);
}
void AbortTitleResources(const char* why)
{
    if (!titleResourcesHeld.load() && !titleReloadCompletion.pending) return;
    titleResourcesHeld.store(false); titleReloadCompletion.pending = false; titleReloadRetry.store(true);
    MyClientLog::Get().warn("Resources: title reload aborted ({}); released avatar override hold, title retry pending", why);
}
bool initialChatProfileObserved = false;
void ObserveInitialChatProfile(ClientInstance& client)
{
    if (initialChatProfileObserved || titleResourcesHeld.load() || liveTextureBusy.load() || titleReloadCompletion.pending) return;
    if (!liveLoadsComplete) liveLoadsComplete = MyClientGame::ResolveFunction<LoadsCompleteFn>(LoadsCompleteSymbol);
    auto& game = client.getMinecraftGame_DEPRECATED();
    if (!LiveLoadsIdle(game) || static_cast<bool>(game.getResourcePackManager().mPendingRestack)) return;
    const bool applied = NativeJsonChatOnStack(client) && JavaChatPack::ProfileExists(TexturePacks::EnvPath("LOCALAPPDATA", L"LOCALAPPDATA"));
    titleReloadCompletion.appliedChat = applied;
    initialChatProfileObserved = true;
    MyClientJavaChat::JsonProfileApplied(applied);
    MyClientLog::Get().info("JAVA Chat: initial existing stack profile observed (hidden={}); no reload requested", applied);
}
void PollTitleResourceCompletion(ClientInstance& client)
{
    if (!titleReloadCompletion.pending) return;
    if (GetTickCount64() - titleReloadCompletion.began > 30000) {
        titleReloadCompletion.pending = false; titleResourcesHeld.store(false); titleReloadRetry.store(true);
        MyClientLog::Get().warn("Resources: title completion timed out; title retry queued, avatar override hold released");
        return;
    }
    auto& game = client.getMinecraftGame_DEPRECATED();
    if (titleCompletionGame != &game) { AbortTitleResources("game identity changed"); return; }
    if (!liveLoadsComplete) liveLoadsComplete = MyClientGame::ResolveFunction<LoadsCompleteFn>(LoadsCompleteSymbol);
    if (!LiveLoadsIdle(game) || static_cast<bool>(game.getResourcePackManager().mPendingRestack)) return;
    const bool selected = NativeJsonChatOnStack(client);
    const bool diskProfile = JavaChatPack::ProfileExists(TexturePacks::EnvPath("LOCALAPPDATA", L"LOCALAPPDATA"));
    if (titleReloadCompletion.Finish(true, false)) {
        const bool applied = selected && diskProfile; // actual adopted profile, not a mutable ON/OFF request file
        titleReloadCompletion.appliedChat = applied;
        titleAvatarRefreshWanted.store(true); // publish the once-only request before letting model overrides resume
        titleResourcesHeld.store(false);
        initialChatProfileObserved = true;
        MyClientJavaChat::JsonProfileApplied(applied);
        MyClientLog::Get().info("JAVA Chat: applied profile check wanted={} nativeStack={} disk={}", titleReloadCompletion.wantedChat, selected, diskProfile);
        MyClientLog::Get().info("Resources: title reload completion #{}; avatar refresh queued once", titleReloadCompletion.sequence);
    }
}

// Runs after a frame has been rendered, on the game's render/main thread. IMinecraftGame is the first
// base of MinecraftGame, so the interface reference is also the MinecraftGame address.
void RunPendingTextureReload()
{
    static bool reloadAfterWorld = false;
    static uint64_t waitingForIdle = 0;
    static PackRefreshWait refresh;
    if (titleReloadRetry.exchange(false)) reloadAfterWorld = true;
    // Leave newer Done requests queued until the preceding title apply has a stable native stack.
    // Otherwise a second repository refresh can race the completion/UUID observation below.
    if (titleReloadCompletion.pending) return;
    // Update HUD availability even when there is no pending Done request.
    static uint64_t eligibilityAt = 0;
    const auto eligibilityNow = GetTickCount64();
    if (!eligibilityAt || eligibilityNow - eligibilityAt >= 250) {
        eligibilityAt = eligibilityNow;
        const auto client = MyClientGame::Client();
        liveTextureLocalAllowed.store(client && (!client->getLocalPlayer() || LocalWorldTextureReload(*client)));
    }
    if (liveTextureBusy.load()) {
        const auto client = MyClientGame::Client();
        if (!client) { EndLiveTextureReload("ended after client disappeared"); refresh = {}; return; }
        if (!LocalWorldTextureReload(*client)) {
            liveTextureLocalAllowed.store(false);
            EndLiveTextureReload("external/unknown connection during reload; deferred to title");
            refresh = {}; waitingForIdle = 0; reloadAfterWorld = true; return;
        }
        if (PollLiveTextureReload(*client)) return;
        if (!liveTextureBusy.load()) { refresh = {}; reloadAfterWorld = true; return; }
    }
    const auto deferUnlessTitle = [&](ClientInstance& client) {
        const bool hasPlayer = client.getLocalPlayer() != nullptr;
        const bool hasLevel = bool(MyClientGame::WorldLevel());
        const bool inWorld = hasPlayer || hasLevel;
        const auto screen = client.getTopScreenName();
        const bool title = MyClientTexturePolicy::IsTitleForReload(hasPlayer, hasLevel, screen);
        const bool loading = screen.find("progress") != std::string::npos || screen.find("loading") != std::string::npos ||
                             screen.find("disconnect") != std::string::npos;
        if (title) return false; // ordinary title reload does not require live hooks
        const bool chatRequested = JavaChatPack::Requested(TexturePacks::EnvPath("LOCALAPPDATA", L"LOCALAPPDATA"));
        if (inWorld && chatRequested != titleReloadCompletion.appliedChat) {
            AbortTitleResources("profile change deferred until title");
            reloadAfterWorld = true; refresh = {}; waitingForIdle = 0;
            MyClientLog::Get().info("JAVA Chat: profile change saved; waiting for title-screen apply");
            return true;
        }
        // Before discovery AND again before native composition: do not enter the known unsafe server path.
        if (inWorld && !LocalWorldTextureReload(client)) {
            AbortTitleResources("external/unknown world entered during title refresh");
            liveTextureLocalAllowed.store(false);
            waitingForIdle = 0;
            reloadAfterWorld = true;
            refresh = {};
            if (liveTextureBusy.load()) EndLiveTextureReload("connection changed; deferred to title");
            MyClientLog::Get().warn("Texture packs: external/unknown world; changes saved for title-screen apply");
            return true;
        }
        if (inWorld && liveTextureBusy.load()) return false;
        if (inWorld && !loading && liveTextureWanted.load(std::memory_order_relaxed) && EnsureLiveTextureHooks() &&
            refresh.phase == PackRefreshWait::Phase::Idle) {
            bool waitForLoads = false;
            if (BeginLiveTextureReload(client, waitForLoads)) { waitingForIdle = 0; return false; }
            if (waitForLoads && !waitingForIdle) {
                waitingForIdle = GetTickCount64();
                MyClientLog::Get().info("Texture packs: waiting for existing game resource loads/GPU before live apply");
            }
            if (waitForLoads && GetTickCount64() - waitingForIdle < 30000) {
                textureReloadPending.store(true);   // coalesce repeated Done requests, keep reading the latest saved list
                return true;
            }
        }
        AbortTitleResources("not on title screen");
        waitingForIdle = 0;
        reloadAfterWorld = true;
        refresh = {};
        MyClientLog::Get().warn("Texture packs: changes saved for title-screen apply (inWorld={} liveWanted={} liveReady={} screen={} hasPlayer={} hasLevel={}; not an eligible title screen)", inWorld,
                               liveTextureWanted.load(std::memory_order_relaxed), liveTextureReady, screen, hasPlayer, hasLevel);
        return true;
    };
    if (reloadAfterWorld && textureReload) {
        const auto client = MyClientGame::Client();
        if (client && MyClientTexturePolicy::IsTitleForReload(client->getLocalPlayer() != nullptr,
                bool(MyClientGame::WorldLevel()), client->getTopScreenName())) {
            reloadAfterWorld = false;
            textureReloadPending.store(true);
        }
    }
    // Once per game start, on the title screen a few seconds after the mods are enabled: re-read the
    // global pack list, as opening the game's resource pack screen and going back does. Global packs
    // were otherwise often missing after a start until that screen had been visited.
    static MyClientTexturePolicy::StartupTitleReload startup;
    static bool startupDone = false;
    if (!startupDone && textureReload) {
        try {
            const auto client = MyClientGame::Client();
            const bool onTitle = client && MyClientTexturePolicy::IsTitleForReload(client->getLocalPlayer() != nullptr,
                bool(MyClientGame::WorldLevel()), client->getTopScreenName());
            const bool queued = reloadAfterWorld || refresh.phase != PackRefreshWait::Phase::Idle || textureReloadPending.load();
            if (startup.Due(GetTickCount64(), onTitle, queued)) {
                textureReloadPending.store(true);
                MyClientLog::Get().info("Reloading the global resource packs once after start (confirmed title; normal reload).");
            }
        } catch (...) { /* No screen observation: wait, do not mark startup applied. */ }
    }
    // The refresh queue is asynchronous. First await user-pack rediscovery, which can rebuild the sources;
    // only then attach our source and await its refresh. A fixed delay is not evidence either task completed.
    if (refresh.phase == PackRefreshWait::Phase::Idle && textureReloadPending.exchange(false) && textureReload) {
        try {
            const auto client = MyClientGame::Client();
            if (client) {
                if (deferUnlessTitle(*client)) return;
                auto& repository = client->getResourcePackRepository();
                if (!liveTextureBusy.load()) {
                    titleResourcesHeld.store(true);
                    MyClientLog::Get().info("Resources: title reload began; own-avatar overrides held before discovery");
                }
                repository.requestReloadUserPacks();
                refresh.repository = &repository;
                refresh.started = GetTickCount64();
                refresh.result = repository.refreshPacksAsync().mResult;
                refresh.phase = PackRefreshWait::Phase::Users;
                MyClientLog::Get().info("Texture packs: waiting for user-pack rediscovery completion");
            }
        } catch (...) {
            refresh = {};
            if (liveTextureBusy.load() && !liveTexture.applied) EndLiveTextureReload("aborted before resource apply");
            AbortTitleResources("starting repository refresh failed");
            MyClientLog::Get().warn("Texture packs: starting repository refresh failed");
        }
        return;
    }
    if (refresh.phase == PackRefreshWait::Phase::Idle || !textureReload) return;
    try {
        const auto client = MyClientGame::Client();
        if (!client) { refresh = {}; AbortTitleResources("client disappeared during refresh"); return; }
        // A world may have opened while awaiting the task. Do not apply resources to it part way through loading.
        if (deferUnlessTitle(*client)) return;
        auto& repository = client->getResourcePackRepository();
        if (refresh.repository != &repository || !refresh.result) {
            refresh = {};
            if (liveTextureBusy.load()) EndLiveTextureReload("aborted before apply: repository changed");
            AbortTitleResources("repository changed or refresh handle unavailable");
            MyClientLog::Get().warn("Texture packs: repository changed or refresh handle unavailable; reload not applied");
            return;
        }
        using Status = Bedrock::Threading::AsyncStatus;
        const auto status = refresh.result->getStatus();
        if (status == Status::Started && GetTickCount64() - refresh.started <= 30000) return;
        if (status != Status::Completed) {
            MyClientLog::Get().warn("Texture packs: repository refresh did not complete (status={}); reload not applied", static_cast<int>(status));
            AbortTitleResources("discovery did not complete");
            refresh = {};   // release our handle; do not cancel work also owned by the game's refresh queue
            if (liveTextureBusy.load()) EndLiveTextureReload("aborted before apply: discovery did not complete");
            return;
        }
        if (refresh.phase == PackRefreshWait::Phase::Users) {
            MyClientLog::Get().info("Texture packs: user-pack rediscovery completed");
            refresh.generated = PrepareGeneratedPackSource(repository);
            refresh.phase = PackRefreshWait::Phase::Generated;
            if (refresh.generated) {
                refresh.started = GetTickCount64();
                refresh.result = repository.refreshPacksAsync().mResult;
                MyClientLog::Get().info("Texture packs: waiting for generated-pack refresh completion");
                return;
            }
        }
        if (refresh.generated && !GeneratedSourceAttached(repository, refresh.generated)) {
            AbortTitleResources("generated source replaced");
            MyClientLog::Get().warn("Texture packs: generated source was replaced during refresh; reload not applied");
            refresh = {};
            if (liveTextureBusy.load()) EndLiveTextureReload("aborted before apply: generated source replaced");
            return;
        }
        MyClientLog::Get().info("Texture packs: repository refresh completed (generated source retained={})", refresh.generated != nullptr);
        refresh = {};
        IMinecraftGame& game = client->getMinecraftGame_DEPRECATED();
        const bool requestedJsonChat = JavaChatPack::Requested(TexturePacks::EnvPath("LOCALAPPDATA", L"LOCALAPPDATA"));
        auto& log = MyClientLog::Get();
        const bool live = liveTextureBusy.load();
        const uint32_t changes = statPacksChangedInner.load();
        const uint32_t notifications = statPacksChanged.load();
        if (live) liveTexture.applied = true;
        {
            struct ComposeScope {
                IMinecraftGame* saved = liveComposeGame;
                explicit ComposeScope(IMinecraftGame* game) { liveComposeGame = game; }
                ~ComposeScope() { liveComposeGame = saved; }
            } scope(live ? &game : nullptr);
            MyClientCrash::Stage("native resource composition");
            log.info("Texture trace: native composition enter (live={} thread={})", live, GetCurrentThreadId());
            if (live && !heldLevelManager) HoldLevelPacks(game.getResourcePackManager());
            // Put the server's packs back after OriginX's own repository refresh (which emptied the level stack)
            // and before the game composes and reloads, so that one reload sees them.
            ReturnLevelPacks("before the composition");
            textureReload(&game, false);
            ReturnLevelPacks("after the composition");
            MyClientCrash::Stage("native resource composition returned");
            log.info("Texture trace: native composition returned (live={} thread={})", live, GetCurrentThreadId());
        }
        startupDone = true; // native apply submitted; completion below owns readiness and the avatar refresh
        if (!live) {
            titleCompletionGame = &game;
            titleReloadCompletion.Begin(requestedJsonChat, GetTickCount64());
            titleResourcesHeld.store(true);
            MyClientLog::Get().info("Resources: title reload submitted; awaiting resource/model completion (chat={})", requestedJsonChat);
        }
        const auto count = statTextureReloads.fetch_add(1) + 1;
        if (live) {
            auto& manager = game.getResourcePackManager();
            NoteStackSizes("after the live composition", manager, log);
            NoteSubpackIndices("native live level", manager.mLevelStack.get(), log);
            NoteSubpackIndices("native live global", manager.mGlobalStack.get(), log);
            NoteFullSubpacks(manager, log);
            const bool noCallbacks = statPacksChangedInner.load() == changes && statPacksChanged.load() == notifications;
            if (static_cast<bool>(manager.mPendingRestack) && noCallbacks) {
                reloadAfterWorld = true;
                EndLiveTextureReload("native composer declined; deferred to title");
            } else if (noCallbacks) {
                EndLiveTextureReload("native composer reported no resource change");
            } else log.info("Texture packs: native composition accepted; waiting for resource/model completion (request #{})", count);
        } else log.info("Texture packs were reloaded on the title screen (request #{}).", count);
        if (!live) {
            try { NoteLitePack(*client); } catch (...) {}
            try { RefreshPackScreens(game); } catch (...) {}
        }
    } catch (...) {
        refresh = {};
        if (liveTextureBusy.load() && !liveTexture.applied) EndLiveTextureReload("aborted before resource apply");
        AbortTitleResources("resource apply exception");
        MyClientLog::Get().warn("Texture packs: repository/resource application raised an exception");
    }
}

// r57-test31 log (B): the setStack keeper never fired, yet the level stack still went 23 -> 0: the global reload
// empties it some other way. The server's packs are now moved out of the level stack before OriginX's live
// composition (a vector swap: the PackInstances are not copied) and moved back right before the game reacts to
// the new stacks (the inner packs-changed call), with the full stack composed again, so the reload sees them.
// The vector is moved as its three pointers (MSVC std::vector layout), so no PackInstance is copied or destroyed
// by OriginX (their constructors/destructors are not in the import library).
struct RawVector { void* begin; void* end; void* capacity; };
static_assert(sizeof(RawVector) == sizeof(std::vector<PackInstance>));
RawVector heldLevelPacks{};
ResourcePackManager* heldLevelManager = nullptr;
size_t RawCount(RawVector const& v) { return (static_cast<char*>(v.end) - static_cast<char*>(v.begin)) / sizeof(PackInstance); }
void HoldLevelPacks(ResourcePackManager& manager)
{
    auto* level = manager.mLevelStack.get();
    // test32: no "held" line though the level stack had 23 packs; say where this stops.
    if (!level) { MyClientLog::Get().info("Texture diag: hold: no level stack"); return; }
    auto* raw = reinterpret_cast<RawVector*>(&level->mStack.get());
    if (raw->begin == raw->end) return;
    heldLevelPacks = *raw;
    *raw = {};
    heldLevelManager = &manager;
    MyClientLog::Get().info("Texture packs: {} server packs held during the live composition", RawCount(heldLevelPacks));
}
void ReturnLevelPacks(const char* where, bool composeAgain)
{
    if (!heldLevelManager) return;
    ResourcePackManager& manager = *heldLevelManager;
    heldLevelManager = nullptr;
    auto* level = manager.mLevelStack.get();
    if (!level) { MyClientLog::Get().warn("Texture packs: level stack gone; {} server packs could not be put back", RawCount(heldLevelPacks)); return; }
    auto* raw = reinterpret_cast<RawVector*>(&level->mStack.get());
    const size_t had = RawCount(*raw);
    if (had) {   // the game put packs there meanwhile: leave them (ours are leaked rather than mixed in)
        MyClientLog::Get().warn("Texture packs: level stack refilled by the game ({} packs); held {} not put back", had, RawCount(heldLevelPacks));
        heldLevelPacks = {};
        return;
    }
    // An empty vector may still own a buffer: it is swapped into our slot and kept (a small, one-off leak).
    std::swap(*raw, heldLevelPacks);
    heldLevelPacks = {};
    using ComposeFn = void (*)(ResourcePackManager*);
    static const auto compose = MyClientGame::ResolveFunction<ComposeFn>("?_composeFullStack@ResourcePackManager@@QEAAXXZ");
    // test34: recomposing here started a second, nested reload (packs changed inside packs changed) and the game
    // crashed in its resource loading (a freed object read as a pointer). Recompose only when asked.
    if (composeAgain && compose) compose(&manager);
    MyClientLog::Get().info("Texture packs: server packs put back {} (level {} packs, full stack recomposed={} full={})",
                            where, RawCount(*raw), composeAgain && compose != nullptr, FullStackSize(manager));
}

// r57-test30 log (B): on Hive the level stack (the server's 23 packs) went to 0 during OriginX's live
// composition (_loadLastGlobalResourcePacksFromFile), so every server-pack picture went: custom blocks, item
// icons, statues, the first-person arm. During that call only, a request to replace a non-empty level stack is
// declined (the game's new one is dropped and the server's stays); every other stack change passes.
constexpr const char* SetStackSymbol =
    "?setStack@ResourcePackManager@@QEAA_NV?$unique_ptr@VResourcePackStack@@U?$default_delete@VResourcePackStack@@@std@@@std@@W4ResourcePackStackType@@@Z";
std::atomic<unsigned> levelStackKept{0};
MYCLIENT_TYPE_INSTANCE_HOOK(SetStackHook, ll::memory::HookPriority::Normal, ResourcePackManager,
                      MyClientGame::Target(SetStackSymbol), bool, std::unique_ptr<ResourcePackStack> stack, ResourcePackStackType type)
{
    if (liveComposeGame && type == ResourcePackStackType::Level) {
        const auto* current = this->mLevelStack.get();
        const size_t have = current ? current->mStack.get().size() : 0;
        const size_t incoming = stack ? stack->mStack.get().size() : 0;
        MyClientLog::Get().info("Texture diag: live composition asked to replace the level stack ({} -> {} packs){}", have, incoming,
                               have && incoming < have ? "; kept the server's packs" : "");
        if (have && incoming < have) { levelStackKept.fetch_add(1); return true; }
    }
    return origin(std::move(stack), type);
}

// The game's own reload steps, only counted (see RunTextureFollowUp). onActiveResourcePacksChanged is a
// ResourcePackListener function, so `this` is that base of MinecraftGame.
MYCLIENT_TYPE_INSTANCE_HOOK(PacksChangedHook, ll::memory::HookPriority::Normal, ResourcePackListener,
                      MyClientGame::Target("?onActiveResourcePacksChanged@MinecraftGame@@UEAAXAEAVResourcePackManager@@@Z"),
                      void, ResourcePackManager& manager)
{
    const bool tracing = liveTextureBusy.load();
    if (tracing) { MyClientCrash::Stage("onActiveResourcePacksChanged"); MyClientLog::Get().info("Texture trace: packs changed enter (thread={})", GetCurrentThreadId()); }
    statPacksChanged.fetch_add(1);
    origin(manager);
    if (tracing) { MyClientCrash::Stage("onActiveResourcePacksChanged returned"); MyClientLog::Get().info("Texture trace: packs changed returned"); }
}

// The step behind it (the game may also call this one directly). Either counts as "the game reloaded".
inline uintptr_t PacksChangedInnerSymbol()
{
    return MyClientGame::ResolveFirst({"?_onActiveResourcePacksChanged@MinecraftGame@@AEAAXAEAVResourcePackManager@@_N@Z",
                                       "?_onActiveResourcePacksChanged@MinecraftGame@@IEAAXAEAVResourcePackManager@@_N@Z",
                                       "?_onActiveResourcePacksChanged@MinecraftGame@@QEAAXAEAVResourcePackManager@@_N@Z"});
}

MYCLIENT_TYPE_INSTANCE_HOOK(PacksChangedInnerHook, ll::memory::HookPriority::Normal, IMinecraftGame, PacksChangedInnerSymbol(),
                      void, ResourcePackManager& manager, bool fullVanillaPackOnStack)
{
    const bool tracing = liveTextureBusy.load();
    if (tracing) { MyClientCrash::Stage("inner packs changed"); MyClientLog::Get().info("Texture trace: inner packs changed enter (fullVanilla={} thread={})", fullVanillaPackOnStack, GetCurrentThreadId()); }
    statPacksChanged.fetch_add(1);
    statPacksChangedInner.fetch_add(1);
    if (liveComposeGame) ReturnLevelPacks("before the game's reload");
    origin(manager, fullVanillaPackOnStack);
    if (tracing) { MyClientCrash::Stage("inner packs changed returned"); MyClientLog::Get().info("Texture trace: inner packs changed returned"); }
}

inline uintptr_t AtlasReloadedSymbol()
{
    return MyClientGame::ResolveFirst({"?_onTerrainAtlasReloaded@MinecraftGame@@AEAAXXZ",
                                       "?_onTerrainAtlasReloaded@MinecraftGame@@IEAAXXZ",
                                       "?_onTerrainAtlasReloaded@MinecraftGame@@QEAAXXZ"});
}

MYCLIENT_TYPE_INSTANCE_HOOK(AtlasReloadedHook, ll::memory::HookPriority::Normal, IMinecraftGame, AtlasReloadedSymbol(), void)
{
    const bool tracing = liveTextureBusy.load();
    if (tracing) { MyClientCrash::Stage("terrain atlas reloaded"); MyClientLog::Get().info("Texture trace: terrain atlas reloaded enter (thread={})", GetCurrentThreadId()); }
    origin();
    if (tracing) { MyClientCrash::Stage("terrain atlas reloaded returned"); MyClientLog::Get().info("Texture trace: terrain atlas reloaded returned"); }
    legacyAtlasSignals.fetch_add(1);
    atlasReloaded.store(true);
}

// 26.51: the TerrainAtlas load task's completion lambda (0x89FEE0) inlines the body of
// _onTerrainAtlasReloaded instead of calling it. Both paths end in this common function
// (0x7F0130), with terrain=true. Watching only the named wrapper misses normal pack reloads
// and prevents RunTextureFollowUp from invalidating chunks even after the atlas is ready.
inline uintptr_t AtlasUpdatedSymbol()
{
    return MyClientGame::ResolveFirst({
        "?_updateTextureAtlasPBRData@MinecraftGame@@AEAAXAEBVTextureAtlas@@_N@Z",
        "?_updateTextureAtlasPBRData@MinecraftGame@@IEAAXAEBVTextureAtlas@@_N@Z",
        "?_updateTextureAtlasPBRData@MinecraftGame@@QEAAXAEBVTextureAtlas@@_N@Z"});
}
MYCLIENT_TYPE_INSTANCE_HOOK(AtlasUpdatedHook, ll::memory::HookPriority::Normal, IMinecraftGame, AtlasUpdatedSymbol(),
                          void, TextureAtlas const* atlas, bool terrain)
{
    if (!this || !atlas) return;
    const bool tracing = liveTextureBusy.load();
    if (tracing) { MyClientCrash::Stage("update texture atlas PBR"); MyClientLog::Get().info("Texture trace: atlas update enter (terrain={} thread={})", terrain, GetCurrentThreadId()); }
    static std::atomic<unsigned> atlasDiag{0};
    if (terrain && (tracing || atlasDiag.fetch_add(1) < 6))
        MyClientLog::Get().info("Texture diag: terrain atlas update (live={} callers={})", tracing, GameBacktrace());
    origin(atlas, terrain); // a const-reference parameter has the same x64 ABI as this checked pointer
    if (tracing) { MyClientCrash::Stage("update texture atlas PBR returned"); MyClientLog::Get().info("Texture trace: atlas update returned (terrain={})", terrain); }
    if (terrain) {
        terrainAtlasUpdates.fetch_add(1);
        atlasReloaded.store(true);
    }
}

// Name tags: in this game the tags drawn above actors are built by LevelRendererPlayer::extractNameTags
// (the old per-actor hooks are hardly called). Nick replaces your real name in that list.
std::mutex nameTagSampleMutex;
std::string nameTagSample;   // a description of one tag of the last list, for the log
std::string NameTagSample() { std::lock_guard g(nameTagSampleMutex); return nameTagSample; }

MYCLIENT_TYPE_INSTANCE_HOOK(NameTagsHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      MyClientGame::Target("?extractNameTags@LevelRendererPlayer@@UEBA?AUNameTagRenderObjectCollection@@AEAVScreenContext@@@Z"),
                      NameTagRenderObjectCollection*, NameTagRenderObjectCollection* out, ScreenContext& screenContext)
{
    // Returned by value: under the MSVC ABI that is a hidden pointer to the caller's buffer (after `this`),
    // which is also returned. Taking it as such means this code never owns or destroys a collection
    // (NameTagRenderObject's destructor is not exported).
    // While the live texture apply holds the world's pre-render update, the game walks an empty actor list
    // (see NameTagActorList) and so builds an empty collection itself; the list is put back right after.
    // Same thread as the pruning it stands in for, so nothing else changes it meanwhile.
    nameTagsPlayerSeen.store(this);
    if (liveTextureBusy.load() && !actorListHold.fields && NameTagActorListKnown()) {
        auto* fields = reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(this) + kNameTagActorsBegin);
        const uint64_t savedEnd = fields[1], savedEnd2 = fields[4];
        fields[1] = fields[0];
        fields[4] = fields[3];
        NameTagRenderObjectCollection* filled = nullptr;
        struct Restore { uint64_t* f; uint64_t a, b; ~Restore() { f[1] = a; f[4] = b; } } restore{fields, savedEnd, savedEnd2};
        filled = origin(out, screenContext);
        if (nameTagsHeldCalls.fetch_add(1, std::memory_order_relaxed) == 0)
            MyClientLog::Get().info("Name tags: actor list emptied during the live texture apply (game call kept)");
        return filled;
    }
    NameTagRenderObjectCollection* filled = origin(out, screenContext);
    if (!filled) return filled;
    NameTagRenderObjectCollection& result = *filled;
    statNameTagLists.fetch_add(1, std::memory_order_relaxed);
    try {
        auto& tags = *result.mNameTags;
        // Your own name: the local player's name (Actor::getNameTag is inlined here, so the Nick hook that
        // used to record it is hardly ever reached); the name it recorded is only a fallback.
        std::string real;
        if (const auto client = MyClientGame::Client())
            if (auto* local = client->getLocalPlayer()) real = local->mName.get();
        if (real.empty()) { std::lock_guard g(realNameMutex); real = localRealName; }
        const bool nick = nickWanted.load(std::memory_order_relaxed) && active.load(std::memory_order_relaxed) && !nickName.empty();
        bool ownTag = false;
        const bool hideOthers = hideNameTagsWanted.load(std::memory_order_relaxed) && active.load(std::memory_order_relaxed);
        OriginMarks::Read markReader;
        const auto& marks = markReader.Get();
        for (auto& tag : tags) {
            std::string& text = *tag.mNameTag;
            const size_t at = MyClientWholeName::Find(text, real);   // whole name only: not inside "Anna" (report #6)
            // No name tags (Optimization): every tag but your own is drawn at size 0 and fully transparent (the
            // list itself is left alone: removing entries would need NameTagRenderObject's destructor).
            if (hideOthers && at == std::string::npos) {
                tag.mScale = 0.f;
                tag.mTagColor->a = 0.f;
                tag.mTextColor->a = 0.f;
                continue;
            }
            const auto* mark = OriginMarks::MatchMarkName(text, marks);
            if (at != std::string::npos) {
                ownTag = true;
                if (nick) {
                    localTagColor.store(MyClientAutoRq::NameColor(text));
                    text.replace(at, real.size(), nickName);
                }
            }
            char prefix[6];
            if (mark && OriginMarks::Prefix(text, mark->tier, prefix) &&
                std::string_view(text).substr(0, 3) != std::string_view(prefix + 3, 3)) {   // the tag text persists
                // Only the tier's name colour (the last 3 bytes, "\u00a7x"), not the E1F0 logo glyph: drawn as a font
                // glyph the logo was several times taller than the name (r58-test58, 2026-11-19). The author wants
                // the logo only as a separate picture beside the tag (as Flarial does), otherwise none.
                constexpr size_t kGlyph = 3, kColour = sizeof prefix - kGlyph;
                const bool grows = text.capacity() - text.size() < kColour;
                text.insert(0, prefix + kGlyph, kColour);
                if (grows) OriginMarks::statMarkAlloc.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Nametag status: your own tag really is in the list the game draws.
        if (ownTag && nametagWanted.load(std::memory_order_relaxed)) nametagAppliedAt.store(GetTickCount64());
        if (!tags.empty()) {
            const auto& first = tags.front();
            const glm::vec3& p = *first.mPos;
            std::string shown;
            for (char c : *first.mNameTag) shown += (static_cast<unsigned char>(c) < 32) ? '?' : c;
            char buf[160];
            std::snprintf(buf, sizeof buf, "tags=%zu first='%.40s' at (%.2f,%.2f,%.2f)", tags.size(), shown.c_str(), p.x, p.y, p.z);
            std::lock_guard g(nameTagSampleMutex);
            nameTagSample = buf;
        }
    } catch (...) {}
    return filled;
}
bool nameTagsHooked = false;

// Nametag (your own tag in third person): the game leaves the local player out of extractNameTags with one
// conditional jump right before a virtual bool call on the actor. Turning that jump into NOPs makes the game
// build your own tag like anyone else's; Nick then renames it in the hook above. The bytes are the pattern
// other Bedrock clients use for this check; the patch is only made when exactly one match is found, and the
// original bytes are put back whenever Nametag/Nick is off or the view is first person.
constexpr int OwnTagPattern[] = {0x0F, 0x84, -1, -1, -1, -1, 0x49, 0x8B, 0x45, -1, 0x49, 0x8B, 0xCD, 0x48, 0x8B, 0x80,
                                 -1, -1, -1, -1, 0xFF, 0x15, -1, -1, -1, -1, 0x84, 0xC0, 0x0F, 0x85};
constexpr size_t OwnTagJumpSize = 6;   // 0F 84 rel32
uint8_t* ownTagJump = nullptr;         // render thread
uint8_t ownTagOriginal[OwnTagJumpSize] = {};
bool ownTagSearched = false, ownTagPatched = false;

// Matches of the pattern in [begin, end) (readable memory only); stops counting at 2.
int FindOwnTagJump(uint8_t* begin, uint8_t* end, uint8_t*& found)
{
    int matches = 0;
    constexpr size_t n = sizeof(OwnTagPattern) / sizeof(OwnTagPattern[0]);
    for (uint8_t* p = begin; p + n <= end && matches < 2; p++) {
        size_t i = 0;
        while (i < n && (OwnTagPattern[i] < 0 || p[i] == static_cast<uint8_t>(OwnTagPattern[i]))) i++;
        if (i != n) continue;
        // A forward jump within the same function (the loop's "next actor").
        int32_t rel = 0;
        std::memcpy(&rel, p + 2, sizeof rel);
        if (rel <= 0 || rel > 0x4000) continue;
        found = p;
        matches++;
    }
    return matches;
}

// The readable, executable range of the region that holds `address`, clamped to [address, address + limit).
bool CodeRange(const void* address, size_t limit, uint8_t*& begin, uint8_t*& end)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof info) || info.State != MEM_COMMIT) return false;
    const DWORD exec = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(info.Protect & exec)) return false;
    begin = static_cast<uint8_t*>(const_cast<void*>(address));
    uint8_t* regionEnd = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
    end = (regionEnd - begin > static_cast<ptrdiff_t>(limit)) ? begin + limit : regionEnd;
    return end > begin;
}

// 2026-11-03 log: the fixed pattern found nothing in this game version ("0 matches in game code"). The same check
// with other registers / another call form: a forward jz rel32, then within 40 bytes a virtual call
// (mov rax,[rax+disp32] then call) whose bool result is tested (test al,al) and jumped on. Only inside
// extractNameTags. Up to 8 candidates are kept for the log.
int FindOwnTagJumpLoose(uint8_t* begin, uint8_t* end, uint8_t*& found, std::vector<uint8_t*>& candidates)
{
    int matches = 0;
    for (uint8_t* p = begin; p + 46 <= end; p++) {
        if (p[0] != 0x0F || p[1] != 0x84) continue;
        int32_t rel = 0;
        std::memcpy(&rel, p + 2, sizeof rel);
        if (rel <= 0 || rel > 0x4000) continue;
        uint8_t* q = p + 6;
        bool vtableLoad = false, call = false, tested = false;
        for (int i = 0; i < 40 && !tested; i++) {
            const uint8_t* c = q + i;
            if (c[0] == 0x48 && c[1] == 0x8B && c[2] == 0x80) vtableLoad = true;                       // mov rax,[rax+disp32]
            if (vtableLoad && ((c[0] == 0xFF && (c[1] == 0x15 || c[1] == 0xD0 || c[1] == 0x50 || c[1] == 0x90)) ||
                               c[0] == 0xE8))
                call = true;
            if (call && c[0] == 0x84 && c[1] == 0xC0 && (c[2] == 0x0F || c[2] == 0x74 || c[2] == 0x75)) tested = true;
        }
        if (!tested) continue;
        if (candidates.size() < 8) candidates.push_back(p);
        found = p;
        matches++;
    }
    return matches;
}

void LocateOwnTagJump()
{
    if (ownTagSearched) return;
    ownTagSearched = true;
    auto& log = MyClientLog::Get();
    try {
        void* fn = reinterpret_cast<void*>(MyClientGame::Target("?extractNameTags@LevelRendererPlayer@@UEBA?AUNameTagRenderObjectCollection@@AEAVScreenContext@@@Z"));
        uint8_t *begin = nullptr, *end = nullptr, *found = nullptr;
        int matches = 0;
        const char* where = "extractNameTags";
        if (fn && CodeRange(fn, 0x6000, begin, end)) matches = FindOwnTagJump(begin, end, found);
        if (matches == 0) {
            // Not inside the function itself: the whole code section of the game, still exactly one match.
            where = "game code";
            found = nullptr;
            const auto module = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
            const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections && matches < 2; i++, section++) {
                if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
                uint8_t* sBegin = module + section->VirtualAddress;
                uint8_t* sEnd = sBegin + section->Misc.VirtualSize;
                uint8_t* hit = nullptr;
                const int m = FindOwnTagJump(sBegin, sEnd, hit);
                if (m > 0) { matches += m; found = hit; }
            }
        }
        std::vector<uint8_t*> loose;
        if (matches == 0 && fn && CodeRange(fn, 0x6000, begin, end)) {
            uint8_t* hit = nullptr;
            const int m = FindOwnTagJumpLoose(begin, end, hit, loose);
            const auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
            for (uint8_t* c : loose) {
                std::string bytes;
                char b[4];
                for (int i = 0; i < 40; i++) { std::snprintf(b, sizeof b, "%02X ", c[i]); bytes += b; }
                log.info("Nametag: candidate at extractNameTags+0x{:X} (+0x{:X}): {}", static_cast<uint64_t>(c - begin),
                         static_cast<uint64_t>(c - base), bytes);
            }
            // Only logged, not patched: a wrong jump turned into NOPs could make the game read past a check it needs
            // (a crash). The right candidate is picked from the log and its bytes become the new pattern.
            log.info("Nametag: {} loose candidates in extractNameTags (not patched; for the next version's pattern).", m);
            (void)hit;
        }
        if (matches == 1 && found) {
            ownTagJump = found;
            std::memcpy(ownTagOriginal, found, OwnTagJumpSize);
            log.info("Nametag: own-tag check found in {} (+0x{:X}).", where,
                     static_cast<uint64_t>(found - reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr))));
        } else {
            log.warn("Nametag: own-tag check not found ({} matches in {}); your own name tag and Nick cannot be shown.",
                     matches, where);
        }
    } catch (...) {
        log.warn("Nametag: searching the own-tag check failed.");
    }
}

bool WriteCode(uint8_t* at, const uint8_t* bytes, size_t size)
{
    DWORD old = 0;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, size);
    VirtualProtect(at, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, size);
    return true;
}

// Once per frame, before the game draws it (never while extractNameTags runs).
void UpdateOwnTag()
{
    bool want = active.load(std::memory_order_relaxed) && nametagWanted.load(std::memory_order_relaxed);
    if (want) {
        try {
            const auto client = MyClientGame::Client();
            // 0 = first person: your own tag would sit in front of the camera.
            want = client && client->getLocalPlayer() && client->getOptions().getPlayerViewPerspective() != 0;
        } catch (...) { want = false; }
    }
    // The per-build site found in the stored game images first; the old pattern search only where it is unknown.
    if (SetOwnTagPatch(want)) return;
    if (want) LocateOwnTagJump();
    if (!ownTagJump || want == ownTagPatched) return;
    static const uint8_t nops[OwnTagJumpSize] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (WriteCode(ownTagJump, want ? nops : ownTagOriginal, OwnTagJumpSize)) ownTagPatched = want;
}

// Clouds are an engine option, not a resource-pack reload. Remember the user's setting for this options
// object and restore it when the request/lease ends. Never dereference an options object from an old client.
void UpdateCloudOption()
{
    static IOptionRegistry* held = nullptr;
    static bool original = true;
    const auto client = MyClientGame::Client();
    if (!client) { held = nullptr; return; }
    auto& options = client->getOptions();
    if (held && held != &options) held = nullptr;
    const bool wanted = active.load(std::memory_order_relaxed) && noCloudsWanted.load(std::memory_order_relaxed);
    if (wanted) {
        if (!held) { original = options.getRenderClouds(); held = &options; }
        if (options.getRenderClouds()) {
            options.setRenderClouds(false);
            MyClientLog::Get().info("Optimization: clouds hidden through the game's render-clouds option");
        }
    } else if (held) {
        if (options.getRenderClouds() != original) options.setRenderClouds(original);
        held = nullptr;
        MyClientLog::Get().info("Optimization: the original render-clouds option was restored");
    }
}

// Left Hand: your player and the camera view for this frame (see leftHandOwnPlayer).
void UpdateLeftHandView()
{
    const void* own = nullptr;
    bool firstPersonView = false;
    if (leftHandWanted.load(std::memory_order_relaxed) && active.load(std::memory_order_relaxed)) {
        try {
            const auto client = MyClientGame::Client();
            if (client) {
                own = client->getLocalPlayer();
                firstPersonView = own && client->getOptions().getPlayerViewPerspective() == 0;
            }
        } catch (...) { own = nullptr; firstPersonView = false; }
    }
    leftHandOwnPlayer.store(own, std::memory_order_relaxed);
    leftHandFirstPersonView.store(firstPersonView, std::memory_order_relaxed);
}

// Hitbox: the boxes are taken at the game tick, but the game draws an actor between its last two tick
// positions (getInterpolatedPosition with this frame's alpha). Each box is moved by the same amount, so it
// stays on the moving actor instead of jumping 20 times a second. An actor no longer in the level keeps
// its box where it was.
// ---- Replay camera path (HUD -> DLL, ReplayCameraChannel.h; docs/superpowers/specs/2026-11-12-replay-camera-editor-design.md) ----
// While the HUD says the camera follows the path, the free-camera viewer (the local player, a spectator of the
// replay server) is put at the path's pose before every frame is drawn. The position goes in as the eye position:
// the player's own eye offset (getEyePos - position) is taken off, and the collision box moves by the same step
// so the game's next movement tick does not pull the player back. Nothing is touched when the HUD is not playing a
// path (eyes view, paused HUD, no HUD, fewer than two points).
MyClientReplayCameraChannel::Channel replayCameraChannel;
uint64_t replayCameraOpenAt = 0;
bool replayCameraLogged = false;

void ApplyReplayCamera()
{
    const uint64_t tick = GetTickCount64();
    const DWORD pid = GetCurrentProcessId();
    if (!replayCameraChannel.Data())
    {
        if (tick - replayCameraOpenAt < 500) return;
        replayCameraOpenAt = tick;
        if (!replayCameraChannel.Open(pid, false)) return;
    }
    MyClientReplayCameraChannel::Shared shared{};
    if (!replayCameraChannel.Snapshot(pid, shared)) return;
    const uint64_t now = MyClientReplayCameraChannel::NowMicros();
    if (!shared.active || shared.count < 2 || !MyClientReplayCameraChannel::Fresh(shared.at, now)) return;
    static std::vector<MyClientReplayCamera::Keyframe> path;
    static MyClientReplayCameraChannel::Steady steady;
    MyClientReplayCameraChannel::Read(shared, path);
    MyClientReplayCamera::Pose pose{};
    const double t = steady.Next(MyClientReplayCameraChannel::TimeAt(shared, now), shared.paused != 0);
    if (!MyClientReplayCamera::Evaluate(path, t, pose)) return;
    try {
        const auto client = MyClientGame::Client();
        auto* player = client ? client->getLocalPlayer() : nullptr;
        using PosFn = Vec3 (Actor::*)() const;
        static const auto getEye = MyClientGame::ResolveMember<PosFn>(MyClientGame::Symbols::EyePos);
        if (!player || !getEye) return;
        auto& parts = *player->mBuiltInComponents;
        // gsl::not_null pointers (never null); the TypedStorage's * reaches through to the component.
        StateVectorComponent* state = &*parts.mStateVectorComponent;
        ActorRotationComponent* rotation = &*parts.mActorRotationComponent;
        AABBShapeComponent* shape = &*parts.mAABBShapeComponent;
        const Vec3 eye = (player->*getEye)();
        Vec3& position = *state->mPos;
        const Vec3 target{pose.x - (eye.x - position.x), pose.y - (eye.y - position.y), pose.z - (eye.z - position.z)};
        const Vec3 step{target.x - position.x, target.y - position.y, target.z - position.z};
        AABB& box = *shape->mAABB;
        box.min.x += step.x; box.min.y += step.y; box.min.z += step.z;
        box.max.x += step.x; box.max.y += step.y; box.max.z += step.z;
        position = target;
        *state->mPosPrev = target;   // no blend from the old place within the frame
        *state->mPosDelta = Vec3{0.f, 0.f, 0.f};
        *rotation->mRot = Vec2{pose.pitch, pose.yaw};
        *rotation->mRotPrev = Vec2{pose.pitch, pose.yaw};
        if (!replayCameraLogged)
        {
            replayCameraLogged = true;
            MyClientLog::Get().info("Replay camera: following a path of {} points (eye offset {:.2f},{:.2f},{:.2f})", shared.count,
                                    eye.x - position.x + step.x, eye.y - position.y + step.y, eye.z - position.z + step.z);
        }
    } catch (...) {}
}

// ---- Replay editor preview and video export (ReplayVideoChannel.h; spec parts 3 and 5) ----
// Every frame: the editor's preview box goes to the in-game drawing; an export runs one frame at a time: the HUD asks
// for frame N (after moving the replay on), the frame `waitFrames` presents later is read back, shrunk and encoded,
// and frame N is reported done. Any failure stops it, deletes the file and says why.
MyClientReplayVideo::Channel videoChannel;
uint64_t videoOpenAt = 0;
struct VideoExportState {
    uint32_t id = 0;                    // the export running (0 = none)
    uint32_t requested = MyClientReplayVideo::NoFrame;
    uint32_t done = MyClientReplayVideo::NoFrame;
    uint32_t state = MyClientReplayVideo::Idle;
    uint32_t width = 0, height = 0;     // the game picture of the first frame
    uint64_t requestedAt = 0;           // when the HUD's request for `pending` came in
    uint32_t pending = MyClientReplayVideo::NoFrame;   // asked by the HUD, not yet handed to the in-game drawing
    std::string error;
} video;
// Before a frame is captured the game gets at least this long (its own tick is 50 ms) to take in the records the
// step sent, on top of a few presents (review 2026-11-12: three presents at 144 fps are only 21 ms).
constexpr uint64_t VideoSettleMs = 70;

void TickReplayVideo()
{
    const uint64_t tick = GetTickCount64();
    const DWORD pid = GetCurrentProcessId();
    if (!videoChannel.IsOpen())
    {
        if (tick - videoOpenAt < 500) { if (video.id) { MyClientVideo::Cancel(); video = {}; } return; }
        videoOpenAt = tick;
        if (!videoChannel.Open(pid, false)) { MyClientCompositor::SetPreview(false, 0, 0, 0, 0); return; }
    }
    MyClientReplayVideo::Shared s{};
    if (!videoChannel.With([&](MyClientReplayVideo::Shared& live) { s = live; })) return;
    if (!MyClientReplayVideo::Valid(s, pid)) return;
    const bool hudFresh = MyClientReplayVideo::Fresh(s.hudAt, tick, 3000);
    MyClientCompositor::SetPreview(hudFresh && MyClientReplayVideo::PreviewWanted(s.previewX, s.previewY, s.previewW, s.previewH),
                                   s.previewX, s.previewY, s.previewW, s.previewH);

    const auto fail = [&](const std::string& why) {
        MyClientVideo::Cancel();
        MyClientCompositor::ResetCapture();
        video.state = MyClientReplayVideo::Failed;
        video.error = why;
        MyClientLog::Get().warn("Replay export stopped: {}", why);
    };
    // A new export.
    if (s.exportId && s.exportId != video.id)
    {
        if (MyClientVideo::Running()) MyClientVideo::Cancel();
        MyClientCompositor::ResetCapture();   // nothing of an earlier export may come in as this one's frame
        video = {};
        video.id = s.exportId;
        std::string error;
        std::wstring path(s.path);
        if (MyClientVideo::Begin(path, static_cast<int>(s.exportWidth), static_cast<int>(s.exportHeight), static_cast<int>(s.exportFps),
                                 s.exportBitrate, error))
        {
            video.state = MyClientReplayVideo::Running;
            MyClientLog::Get().info("Replay export started: {}x{} at {} fps, {} bit/s", s.exportWidth, s.exportHeight, s.exportFps, s.exportBitrate);
        }
        else fail(error);
    }
    if (video.id && video.id == s.exportId && video.state == MyClientReplayVideo::Running)
    {
        if (s.finish == MyClientReplayVideo::Cancel)
        {
            MyClientVideo::Cancel();
            MyClientCompositor::ResetCapture();
            video.state = MyClientReplayVideo::Idle;
            video.error = "cancelled";
        }
        else if (!hudFresh) fail("the HUD stopped answering");
        else
        {
            std::vector<uint32_t> pixels;
            uint32_t w = 0, h = 0;
            std::string error;
            switch (MyClientCompositor::TakeCapture(pixels, w, h, error))
            {
            case MyClientCompositor::CaptureResult::Ready:
                if (video.requested == MyClientReplayVideo::NoFrame) break;   // not asked for in this export
                if (video.width && (w != video.width || h != video.height)) { fail("the game window changed size during the export"); break; }
                video.width = w; video.height = h;
                if (!MyClientVideo::Write(pixels.data(), static_cast<int>(w), static_cast<int>(h), static_cast<int>(w * 4), video.requested, error)) fail(error);
                else video.done = video.requested;
                break;
            case MyClientCompositor::CaptureResult::Failed:
                fail(error);
                break;
            default:
                break;
            }
            if (video.state == MyClientReplayVideo::Running && s.requestFrame != MyClientReplayVideo::NoFrame &&
                s.requestFrame != video.requested && s.requestFrame != video.pending && video.done == video.requested)
            {
                video.pending = s.requestFrame;
                video.requestedAt = tick;
            }
            if (video.state == MyClientReplayVideo::Running && video.pending != MyClientReplayVideo::NoFrame && tick - video.requestedAt >= VideoSettleMs)
            {
                video.requested = video.pending;
                video.pending = MyClientReplayVideo::NoFrame;
                MyClientCompositor::RequestCapture(s.waitFrames);
            }
            if (video.state == MyClientReplayVideo::Running && s.finish == MyClientReplayVideo::Close && video.done == video.requested)
            {
                if (MyClientVideo::Finish(error)) { video.state = MyClientReplayVideo::Done; MyClientLog::Get().info("Replay export finished: {} frames", video.done + 1); }
                else fail(error);
            }
        }
    }
    videoChannel.With([&](MyClientReplayVideo::Shared& live) {
        if (!MyClientReplayVideo::Valid(live, pid)) return;
        live.dllAt = tick;
        live.dllExportId = video.id;
        live.state = video.state;
        live.doneFrame = video.done;
        live.backWidth = video.width; live.backHeight = video.height;
        MyClientReplayVideo::SetError(live, video.error);
    });
}

void UpdateHitboxShifts(float alpha)
{
    if (!MyClientGame::HitboxRequested()) return;
    std::lock_guard g(hitboxActorsMutex);
    if (hitboxActors.empty() || hitboxActors.size() != hitboxActorPositions.size()) return;
    using LerpFn = Vec3 (Actor::*)(float) const;
    static const auto getLerp = MyClientGame::ResolveMember<LerpFn>(MyClientGame::Symbols::InterpolatedPosition);
    if (!getLerp) return;
    try {
        auto level = MyClientGame::WorldLevel();
        if (!level) return;
        std::vector<float> shifts(hitboxActors.size() * 3, 0.0f);
        for (Actor* actor : level->getRuntimeActorList()) {
            if (!actor) continue;
            for (size_t i = 0; i < hitboxActors.size(); i++) {
                if (hitboxActors[i] != actor) continue;
                const Vec3 smooth = (actor->*getLerp)(alpha);
                const Vec3& then = hitboxActorPositions[i];
                const float dx = smooth.x - then.x, dy = smooth.y - then.y, dz = smooth.z - then.z;
                // More than a few blocks means a teleport (or a bad read): leave that box where it is.
                if (std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz) && dx * dx + dy * dy + dz * dz < 16.0f) {
                    shifts[i * 3] = dx; shifts[i * 3 + 1] = dy; shifts[i * 3 + 2] = dz;
                }
                break;
            }
        }
        MyClientCompositor::SetBoxShifts(shifts.data(), static_cast<uint32_t>(hitboxActors.size()));
    } catch (...) {}
}

std::string RenderSkipSummary();   // below, with the Optimization render hooks
void TickReplayJoin(void* gameArgs);   // below, with the replay recorder
MYCLIENT_TYPE_INSTANCE_HOOK(FrameHook, ll::memory::HookPriority::Normal, GameRenderer,
                      ll::memory::SymbolView{"?renderCurrentFrame@GameRenderer@@QEAAXM@Z"}, void, float alpha)
{
    // The game's client for MyClientGame::Client() (GameState.h), instead of LeviLamina's getClientInstance().
    // mPrimaryClient is a NotNullNonOwnerPtr<IClientInstance> (gsl::not_null around a NonOwnerPointer).
    {
        const auto* primary = reinterpret_cast<Bedrock::NonOwnerPointer<IClientInstance> const*>(static_cast<void const*>(std::addressof(this->mPrimaryClient)));
        if (primary && primary->mPointer) MyClientGame::NoteClient(static_cast<ClientInstance*>(primary->mPointer));
    }
    // Before the frame: the Compositor may present it while the game is still inside renderCurrentFrame.
    UpdateOwnTag();   // also runs when inactive, so the original bytes come back
    UpdateLeftHandView();
    try { UpdateCloudOption(); }
    catch (...) { /* A failed option update must not interrupt the game's frame. */ }
    if (active.load()) UpdateHitboxShifts(alpha);
    if (active.load()) ApplyReplayCamera();   // before the frame is drawn: this frame already shows the path
    { MyClientProfile::GameScope prof(MyClientProfile::GameFrame); origin(alpha); }
    if (!active.load()) return;
    {
        MyClientProfile::Scope prof(MyClientProfile::Frame);
        PublishFps();
        static int observedDistance = -1;
        static uint64_t distanceChecked = 0, distanceSampleAt = 0;
        const auto distanceNow = GetTickCount64();
        if (distanceNow - distanceChecked >= 500) {
            distanceChecked = distanceNow;
            try { if (const auto client = MyClientGame::Client()) {
                const int distance = client->getOptions().getViewDistanceChunks();
                if (distance != observedDistance || distanceNow - distanceSampleAt >= 5000) {
                    float fps = 0; uint64_t at = 0; frames.Read(distanceNow, fps, at);
                    MyClientLog::Get().info("Render Distance audit: option {} -> {} chunks; HUD Optimization flag={} FPS={} fpsAt={} (input source not inferred)",
                        observedDistance, distance, optimizationAuditMaster.load(), fps, at);
                    observedDistance = distance; distanceSampleAt = distanceNow;
                }
            }} catch (...) {}
        }
        TrackGameScreens();
        RunPendingTextureReload();
        if (const auto client = MyClientGame::Client()) {
            try { PollTitleResourceCompletion(*client); ObserveInitialChatProfile(*client); MyClientJavaChat::Tick(*client); }
            catch (...) { MyClientLog::Get().warn("Resources: completion/chat publication could not be observed this frame"); }
        }
        TickReplayJoin(nullptr);
        try { TickReplayVideo(); } catch (...) {}
    }
    renderedFrames.fetch_add(1, std::memory_order_relaxed);
    // Perf: once a minute (general cost, not tied to a check in progress).
    const std::string perf = MyClientProfile::Summary(GetTickCount64(), 60000);
    if (!perf.empty()) {
        auto& log = MyClientLog::Get();
        log.info("{}", perf);
        const std::string skips = RenderSkipSummary();
        if (!skips.empty()) log.info("{}", skips);
        // Left Hand is still being checked in the game (VERIFY.md 5): its counters, only while it is on.
        const uint32_t calls = statLeftHandCalls.exchange(0), passes = statFirstPersonPasses.exchange(0),
                       first = statLeftHandFirst.exchange(0), restored = statLeftHandRestored.exchange(0);
        if (leftHandWanted.load())
            log.info("Left Hand check: wanted={} hooked={} itemDraws={} ownDraws={} firstPersonView={} firstPersonPasses={} "
                     "firstPersonMeshes={} firstPerson={} mirroredMeshes={} restored={} itemInHandPasses={} mirroredPasses={}",
                     true, leftHandHooked, calls, statLeftHandOwn.exchange(0), leftHandFirstPersonView.load(), passes,
                     statFirstPersonMeshes.exchange(0), first, statLeftHand.exchange(0), restored,
                     statItemInHandPasses.exchange(0), statItemInHandMirrored.exchange(0));
    }
}

// ---- HUD cost (measurement only: each hook calls the game's function unchanged and times it) ----
// For the Perf line: how much of a frame the game's own HUD takes, split into its per-frame update
// (UIScene::frameUpdate, with HudScreenController::tick and the item slots' update inside it) and its
// drawing (UIScene::render, with the item icons' InventoryItemRenderer::render inside it).
// Mangled from the SDK declarations (virtual public members, MSVC x64); a name this game build lacks is skipped.
inline uintptr_t UiUpdateSymbol()
{
    return MyClientGame::ResolveFirst({"?frameUpdate@UIScene@@UEAAXAEAVMinecraftUIFrameUpdateContext@@@Z"});
}
inline uintptr_t UiRenderSymbol()
{
    return MyClientGame::ResolveFirst({"?render@UIScene@@UEAAXAEAVScreenContext@@AEBUFrameRenderObject@@@Z"});
}
inline uintptr_t HudTickSymbol()
{
    return MyClientGame::ResolveFirst({"?tick@HudScreenController@@UEAA?AW4DirtyFlag@ui@@XZ",
                                       "?tick@HudScreenController@@MEAA?AW4DirtyFlag@ui@@XZ"});
}
inline uintptr_t ItemUpdateSymbol()
{
    return MyClientGame::ResolveFirst({"?update@InventoryItemRenderer@@UEAA_NAEAVIClientInstance@@AEAVUIControl@@AEBVUIScene@@@Z"});
}
inline uintptr_t ItemRenderSymbol()
{
    return MyClientGame::ResolveFirst(
        {"?render@InventoryItemRenderer@@UEAAXAEAVMinecraftUIRenderContext@@AEAVIClientInstance@@AEAVUIControl@@H@Z"});
}

MYCLIENT_INSTANCE_HOOK(UiUpdateHook, ll::memory::HookPriority::Normal, UiUpdateSymbol(), void, MinecraftUIFrameUpdateContext& context)
{
    MyClientProfile::GameScope prof(MyClientProfile::UiUpdate);
    origin(context);
}

MYCLIENT_INSTANCE_HOOK(UiRenderHook, ll::memory::HookPriority::Normal, UiRenderSymbol(), void,
                 ScreenContext& screenContext, FrameRenderObject const& renderObj)
{
    MyClientProfile::GameScope prof(MyClientProfile::UiRender);
    origin(screenContext, renderObj);
}

MYCLIENT_INSTANCE_HOOK(HudTickHook, ll::memory::HookPriority::Normal, HudTickSymbol(), ui::DirtyFlag)
{
    MyClientProfile::GameScope prof(MyClientProfile::HudTick);
    return origin();
}

MYCLIENT_INSTANCE_HOOK(ItemUpdateHook, ll::memory::HookPriority::Normal, ItemUpdateSymbol(), bool,
                 IClientInstance& client, UIControl& owner, UIScene const& scene)
{
    MyClientProfile::GameScope prof(MyClientProfile::ItemUpdate);
    return origin(client, owner, scene);
}

MYCLIENT_INSTANCE_HOOK(ItemRenderHook, ll::memory::HookPriority::Normal, ItemRenderSymbol(), void,
                 MinecraftUIRenderContext& renderContext, IClientInstance& client, UIControl& owner, int pass)
{
    MyClientProfile::GameScope prof(MyClientProfile::ItemRender);
    origin(renderContext, client, owner, pass);
}
bool hudCostHooked = false;

MYCLIENT_TYPE_INSTANCE_HOOK(FovHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{"?getFov@LevelRendererPlayer@@QEBAMM_N@Z"}, float, float alpha, bool variable)
{
    const float original = origin(alpha, variable);
    if (!active.load()) return original;
    MyClientProfile::Scope prof(MyClientProfile::Fov);
    bool playable = false;
    try {
        const auto client = MyClientGame::Client();
        // Never affect a secondary/split-screen camera or an inventory/chat menu.
        playable = client && &mClientInstance == &*client && client->getLocalPlayer() &&
            client->isInWorldAndNotShowingAnyMenuScreens();
    } catch (...) { playable = false; }
    MyClientFeatures::Command command{};
    command.at = zoomCommandAt.load(std::memory_order_acquire);
    const auto packed = zoomCommandFields.load(std::memory_order_relaxed);
    command.key = (packed >> 16) & 0xFFu;
    command.zoom = (packed >> 24) & 1u;
    command.reserved = packed & 0xFFFFu;
    const auto now = GetTickCount64();
    DWORD foregroundPid = 0;
    bool keyDown = false;
    if (command.zoom && playable && MyClientFeatures::Fresh(command.at, now, MyClientFeatures::CommandLeaseMs)) {
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
        keyDown = command.key > 0 && command.key < 255 && (GetAsyncKeyState(static_cast<int>(command.key)) & 0x8000);
    }
    const bool zoom = MyClientFeatures::WantsZoom(command, now, foregroundPid == GetCurrentProcessId(), playable, keyDown);
    const float result = MyClientFeatures::ZoomFov(original, zoom, MyClientFeatures::ZoomFactor(command.reserved & 0xFFFFu));
    MyClientFeatures::Telemetry camera{};
    if (variable && playable)
    {
        try {
            const auto client2 = MyClientGame::Client();
            using PosFn = Vec3 (Actor::*)() const;
            using LerpFn = Vec3 (Actor::*)(float) const;
            static const auto getEye = MyClientGame::ResolveMember<PosFn>(MyClientGame::Symbols::EyePos);
            static const auto getLerp = MyClientGame::ResolveMember<LerpFn>(MyClientGame::Symbols::InterpolatedPosition);
            auto* player = client2 && getEye && getLerp ? client2->getLocalPlayer() : nullptr;
            if (player)
            {
                // Interpolated eye position (smooth between ticks) + the game's own rotation.
                const auto eye = (player->*getEye)();
                const auto pos = player->getPosition();
                const auto smooth = (player->*getLerp)(alpha);
                auto& t = camera;
                t.camX = smooth.x + (eye.x - pos.x);
                t.camY = smooth.y + (eye.y - pos.y);
                t.camZ = smooth.z + (eye.z - pos.z);
                const auto& rot = player->getRotation();
                t.camPitch = rot.x;
                t.camYaw = rot.y;
                t.camFov = result;
                t.camAt = now;
                MyClientCompositor::SetCamera(t.camX, t.camY, t.camZ, t.camYaw, t.camPitch, t.camFov);
            }
        } catch (...) {}
    }
    // Optional telemetry must not stall rendering when the tick sampler/HUD owns the channel.
    std::unique_lock guard(featureMutex, std::try_to_lock);
    if (guard.owns_lock() && active.load() && channel.Data() && channel.Lock()) {
        auto& telemetry = channel.Data()->telemetry;
        if (camera.camAt) {
            telemetry.camAt = camera.camAt;
            telemetry.camX = camera.camX; telemetry.camY = camera.camY; telemetry.camZ = camera.camZ;
            telemetry.camPitch = camera.camPitch; telemetry.camYaw = camera.camYaw; telemetry.camFov = camera.camFov;
        }
        telemetry.zoomAt = now;
        telemetry.zoomActive = result != original ? 1u : 0u;
        channel.Unlock();
    }
    return result;
}

// ---- Block Outline colour -------------------------------------------------------------------
// The game draws the black block selection outline in LevelRendererPlayer::_renderOutlineSelection.
// While the HUD lease is fresh we (1) mark "inside the outline code" for this thread, (2) set the
// game's global ShaderColor to the chosen colour, and (3) replace the RGB of Tessellator::color()
// calls made meanwhile. Alpha is left as the game chose it. Everything is restored afterwards.
// None of this is confirmed against the game binary: the access level of the member changes its
// mangled name (three variants are tried), and the game may colour the outline some other way.
bool OverrideOutlineColor(float& r, float& g, float& b)
{
    if (!insideOutline) return false;
    statTessColor.fetch_add(1, std::memory_order_relaxed);
    const uint32_t rgb = outlineRgbValue.load(std::memory_order_relaxed);
    r = static_cast<float>((rgb >> 16) & 0xFFu) / 255.f;
    g = static_cast<float>((rgb >> 8) & 0xFFu) / 255.f;
    b = static_cast<float>(rgb & 0xFFu) / 255.f;
    outlineColoredAt.store(GetTickCount64(), std::memory_order_relaxed);
    return true;
}

struct OutlineScope {
    ShaderColor* shader = nullptr;
    mce::Color savedColor{};
    bool savedDirty = false;
    bool wasInside = false;
    OutlineScope() {
        outlineRanAt.store(GetTickCount64(), std::memory_order_relaxed);
        wasInside = insideOutline;
        insideOutline = true;
        try {
            const auto client = MyClientGame::Client();
            if (client) {
                shader = &client->getShaderColor();
                savedColor = shader->color.get();
                savedDirty = shader->dirty;
                float r = 0, g = 0, b = 0;
                OverrideOutlineColor(r, g, b);
                auto& c = shader->color.get();
                c.r = r; c.g = g; c.b = b;          // alpha stays as it was
                shader->dirty = true;
            }
        } catch (...) { shader = nullptr; }
    }
    ~OutlineScope() {
        insideOutline = wasInside;
        try {
            if (shader) {
                shader->color.get() = savedColor;
                shader->dirty = true;               // make the engine re-upload the original colour
            }
        } catch (...) {}
    }
};

template <class Call>
void RunOutline(Call&& call)
{
    { MyClientProfile::Scope prof(MyClientProfile::Outline); }
    statOutline.fetch_add(1, std::memory_order_relaxed);
    if (!outlineWanted.load(std::memory_order_relaxed)) { call(); return; }
    OutlineScope scope;
    call();
}

#define MYCLIENT_OUTLINE_SYMBOL(ACCESS) \
    "?_renderOutlineSelection@LevelRendererPlayer@@" ACCESS "EBAXAEAVBaseActorRenderContext@@AEBVBlock@@AEAVBlockSource@@AEBVBlockPos@@@Z"

constexpr const char* OutlineSymbolPublic    = MYCLIENT_OUTLINE_SYMBOL("Q");
constexpr const char* OutlineSymbolPrivate   = MYCLIENT_OUTLINE_SYMBOL("A");
constexpr const char* OutlineSymbolProtected = MYCLIENT_OUTLINE_SYMBOL("I");

MYCLIENT_TYPE_INSTANCE_HOOK(OutlineHookPublic, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{OutlineSymbolPublic}, void,
                      BaseActorRenderContext& renderContext, Block const& block, BlockSource& region, BlockPos const& pos)
{
    RunOutline([&] { origin(renderContext, block, region, pos); });
}
MYCLIENT_TYPE_INSTANCE_HOOK(OutlineHookPrivate, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{OutlineSymbolPrivate}, void,
                      BaseActorRenderContext& renderContext, Block const& block, BlockSource& region, BlockPos const& pos)
{
    RunOutline([&] { origin(renderContext, block, region, pos); });
}
MYCLIENT_TYPE_INSTANCE_HOOK(OutlineHookProtected, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{OutlineSymbolProtected}, void,
                      BaseActorRenderContext& renderContext, Block const& block, BlockSource& region, BlockPos const& pos)
{
    RunOutline([&] { origin(renderContext, block, region, pos); });
}

// 26.51 never calls _renderOutlineSelection on its own (the HUD showed 0 calls per second): the selection
// is drawn from renderHitSelect, with _renderHighlightSelection for the "highlight" style. Wrapping both
// covers whichever path the game takes; nested scopes are harmless.
constexpr const char* HitSelectSymbol =
    "?renderHitSelect@LevelRendererPlayer@@QEBAXAEAVBaseActorRenderContext@@AEAVBlockSource@@AEBVBlockPos@@_N@Z";
constexpr const char* HighlightSymbol =
    "?_renderHighlightSelection@LevelRendererPlayer@@QEBAXAEAVBaseActorRenderContext@@AEAVBlockSource@@AEBVBlock@@AEBVBlockPos@@_N4@Z";
bool hitSelectHooked = false, highlightHooked = false;

void CaptureWorldCamera(BaseActorRenderContext& context);   // below, with the camera hooks

MYCLIENT_TYPE_INSTANCE_HOOK(HitSelectHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{HitSelectSymbol}, void,
                      BaseActorRenderContext& renderContext, BlockSource& region, BlockPos const& pos, bool fancyGraphics)
{
    CaptureWorldCamera(renderContext);
    // Keep Minecraft's own geometry, occlusion and line width. Only change its colour.
    RunOutline([&] { origin(renderContext, region, pos, fancyGraphics); });
}

// The world camera at the moment the game draws world objects: the ScreenContext's camera (not the one
// for the hand or the UI) and the camera position of this frame (third person included). Taken once
// per frame from the first world draw call that has it; the Compositor projects its lines with it.
uint64_t cameraCapturedFrame = 0;   // render thread
// The frame's camera position lives in the render context from 26.51 on; older SDKs do not have it.
template <class ImplT>
bool CameraPositionOf(ImplT* impl, float out[3])
{
    if constexpr (requires(ImplT* i) { i->mCameraPosition; }) {
        const Vec3& p = impl->mCameraPosition;
        out[0] = p.x; out[1] = p.y; out[2] = p.z;
        return true;
    } else {
        (void)impl; (void)out;
        return false;
    }
}
void CaptureWorldCamera(BaseActorRenderContext& context)
{
    const uint64_t frame = renderedFrames.load(std::memory_order_relaxed);
    if (frame == cameraCapturedFrame) return;
    try {
        ScreenContext& screen = context.mScreenContext;
        mce::MeshContext& mesh = screen;
        if (mesh.isDrawingUI || mesh.isDrawingFirstPersonObjects) return;
        // TypedStorage's * and -> reach through the unique_ptr; read the pointer itself to check for null.
        auto& owner = reinterpret_cast<std::unique_ptr<BaseActorRenderContext::Impl>&>(context.mImpl);
        BaseActorRenderContext::Impl* impl = owner.get();
        if (!impl) return;
        mce::Camera& camera = mesh.camera;
        const glm::mat4x4& view = *camera.viewMatrixStack->stack->back()._m;
        const glm::mat4x4& proj = *camera.projectionMatrixStack->stack->back()._m;
        float pos[3] = {};
        CameraPositionOf(impl, pos);   // stays 0 on older games: the Compositor then uses the eye position
        MyClientCompositor::SetMatrices(&view[0][0], &proj[0][0], pos[0], pos[1], pos[2]);
        lastCameraPos[0] = pos[0]; lastCameraPos[1] = pos[1]; lastCameraPos[2] = pos[2];
        {
            // The view matrix's third row is the camera's backward axis in world space (right-handed view).
            const float f[3] = {-view[0][2], -view[1][2], -view[2][2]};
            const float length = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            cameraForwardKnown = length > 0.5f && (pos[0] != 0.f || pos[1] != 0.f || pos[2] != 0.f);
            if (cameraForwardKnown)
                for (int i = 0; i < 3; i++) cameraForward[i] = f[i] / length;
        }
        cameraCapturedFrame = frame;
        statCameraCaptures.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {}
}

// Diagnostic only now (calls per second in the log): the frame's camera comes from CaptureWorldCamera.
MYCLIENT_TYPE_INSTANCE_HOOK(CameraHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{"?setupCamera@LevelRendererPlayer@@QEAAXAEAVCamera@mce@@M@Z"}, void,
                      mce::Camera& camera, float a)
{
    origin(camera, a);
    try {
        MyClientProfile::Scope prof(MyClientProfile::Fov);
        const glm::mat4x4& view = *camera.viewMatrixStack->stack->back()._m;   // top(); older SDKs lack the helper
        const glm::mat4x4& proj = *camera.projectionMatrixStack->stack->back()._m;
        (void)view; (void)proj;
        statSetupCamera.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {}
}
bool cameraHooked = false;
MYCLIENT_TYPE_INSTANCE_HOOK(HighlightHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{HighlightSymbol}, void,
                      BaseActorRenderContext& renderContext, BlockSource& region, Block const& block, BlockPos const& pos,
                      bool renderSecondPart, bool fancyGraphics)
{
    RunOutline([&] { origin(renderContext, region, block, pos, renderSecondPart, fancyGraphics); });
}

constexpr const char* TessColorFloatSymbol = "?color@Tessellator@@QEAAXMMMM@Z";
constexpr const char* TessColorObjectSymbol = "?color@Tessellator@@QEAAXAEBVColor@mce@@@Z";

MYCLIENT_TYPE_INSTANCE_HOOK(TessColorFloatHook, ll::memory::HookPriority::Normal, Tessellator,
                      ll::memory::SymbolView{TessColorFloatSymbol}, void, float r, float g, float b, float a)
{
    { MyClientProfile::Scope prof(MyClientProfile::TessColor); OverrideOutlineColor(r, g, b); }
    origin(r, g, b, a);
}
MYCLIENT_TYPE_INSTANCE_HOOK(TessColorObjectHook, ll::memory::HookPriority::Normal, Tessellator,
                      ll::memory::SymbolView{TessColorObjectSymbol}, void, mce::Color const& color)
{
    float r = color.r, g = color.g, b = color.b;
    bool changed;
    { MyClientProfile::Scope prof(MyClientProfile::TessColor); changed = OverrideOutlineColor(r, g, b); }
    if (changed) {
        const mce::Color replaced(r, g, b, color.a);
        origin(replaced);
    } else {
        origin(color);
    }
}

// Nametag: Actor::canShowNameTag() (virtual, declared in the SDK; Player does not override it) is
// the game's own "may this actor show a name tag" question. The local player normally gets "no"
// in third-person view. While the HUD lease is fresh we answer "yes" for the local player ONLY,
// leaving every other actor and the original answer untouched. NOT confirmed against the game
// binary: if the symbol is missing the feature is reported UNAVAILABLE, and if the game also
// skips the local player elsewhere, the HUD stays at "WAITING" because the hook is never reached.
constexpr const char* NameTagSymbol = "?canShowNameTag@Actor@@UEBA_NXZ";

MYCLIENT_TYPE_INSTANCE_HOOK(NameTagHook, ll::memory::HookPriority::Normal, Actor,
                      ll::memory::SymbolView{NameTagSymbol}, bool)
{
    const bool original = origin();
    MyClientProfile::Scope prof(MyClientProfile::NameTag);
    statNameTag.fetch_add(1, std::memory_order_relaxed);
    if (!active.load(std::memory_order_relaxed)) return original;
    try {
        const auto client = MyClientGame::Client();
        const Actor* local = client ? static_cast<const Actor*>(client->getLocalPlayer()) : nullptr;
        if (local && local == static_cast<const Actor*>(this)) {
            // Diagnostics: does the game ever ask about the local player at all?
            statNameTagLocal.fetch_add(1, std::memory_order_relaxed);
            if (!original && nametagWanted.load(std::memory_order_relaxed)) {
                nametagAppliedAt.store(GetTickCount64());
                return true;
            }
        }
    } catch (...) {}
    return original;
}

// Nick: Actor::getNameTag() (non-virtual, MSVC x64 mangling of the SDK declaration) is the text the game
// shows above an actor. For the LOCAL player only, and only while Nick is on, we return the chosen
// name; everyone else (and the game's own data) is untouched. Client side only: other players still see
// your real tag. If the game inlines the call somewhere, that spot keeps the real name (nothing breaks).
constexpr const char* NickSymbol =
    "?getNameTag@Actor@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ";

// Entity render distance: ActorRenderDispatcher::render draws one actor. Actors farther from the local
// player than the chosen distance are simply not drawn this frame (they still exist, move and collide).
// The local player is always drawn. Other players have their own range (Shared.optimize; the older "players too"
// bit reuses the entity range); everything else is hidden only for the kinds that are switched on: mobs, dropped
// items, and projectiles / everything else (arrows, boats, minecarts, XP orbs...).
bool CullActor(Actor& actor)
{
    float range2 = cullRange2.load(std::memory_order_relaxed);
    const float playerRange2 = cullPlayerRange2.load(std::memory_order_relaxed);
    const bool behind = cullBehindWanted.load(std::memory_order_relaxed) && cameraForwardKnown;
    if (range2 <= 0.f && playerRange2 <= 0.f && !behind) return false;
    if (static_cast<const void*>(&actor) == cullLocal.load(std::memory_order_relaxed)) return false;
    if (behind) {
        const auto at = actor.getPosition();
        const float where[3] = {at.x, at.y, at.z};
        if (MyClientFeatures::BehindCamera(lastCameraPos, cameraForward, where)) {
            statBehindSkipped.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    if (range2 <= 0.f && playerRange2 <= 0.f) return false;
    if (MyClientGame::IsPlayer(actor)) {
        if (playerRange2 > 0.f) range2 = playerRange2;
        else if (!cullPlayers.load(std::memory_order_relaxed)) return false;
    } else {
        const uint32_t kinds = cullKinds.load(std::memory_order_relaxed);
        const uint32_t kind = actor.hasCategory(ActorCategory::Item) ? MyClientFeatures::CullItemsBit
                            : actor.hasCategory(ActorCategory::Mob)  ? MyClientFeatures::CullMobsBit
                                                                     : MyClientFeatures::CullMiscBit;
        if ((kinds & kind) == 0) return false;
    }
    if (range2 <= 0.f) return false;
    const auto pos = actor.getPosition();
    const float dx = pos.x - cullX.load(std::memory_order_relaxed), dy = pos.y - cullY.load(std::memory_order_relaxed),
                dz = pos.z - cullZ.load(std::memory_order_relaxed);
    if (dx * dx + dy * dy + dz * dz <= range2) return false;
    statCulled.fetch_add(1, std::memory_order_relaxed);
    return true;
}

constexpr const char* ActorRenderSymbol = "?render@ActorRenderDispatcher@@QEAAXAEAVBaseActorRenderContext@@AEAVActor@@_N@Z";
constexpr const char* ActorRenderAtSymbol =
    "?render@ActorRenderDispatcher@@QEAAXAEAVBaseActorRenderContext@@AEAVActor@@AEBVVec3@@2AEBVVec2@@_N@Z";

MYCLIENT_TYPE_INSTANCE_HOOK(ActorRenderHook, ll::memory::HookPriority::Normal, ActorRenderDispatcher,
                      MyClientGame::Target(ActorRenderSymbol), void,
                      BaseActorRenderContext& renderContext, Actor& entity, bool ignoreLighting)
{
    bool skip = false;
    { MyClientProfile::Scope prof(MyClientProfile::ActorRender); CaptureWorldCamera(renderContext); try { skip = CullActor(entity); } catch (...) {} }
    if (!skip) origin(renderContext, entity, ignoreLighting);
}

MYCLIENT_TYPE_INSTANCE_HOOK(ActorRenderAtHook, ll::memory::HookPriority::Normal, ActorRenderDispatcher,
                      ll::memory::SymbolView{ActorRenderAtSymbol}, void,
                      BaseActorRenderContext& renderContext, Actor& entity, Vec3 const& cameraTargetPos,
                      Vec3 const& pos, Vec2 const& rot, bool ignoreLighting)
{
    bool skip = false;
    { MyClientProfile::Scope prof(MyClientProfile::ActorRender); CaptureWorldCamera(renderContext); try { skip = CullActor(entity); } catch (...) {} }
    if (!skip) origin(renderContext, entity, cameraTargetPos, pos, rot, ignoreLighting);
}

// ---- Optimization rows that need render hooks (2026-11-04) ----
// Vertical render distance: each sub-chunk (16x16x16) the game decided to draw goes through
// LevelRendererCamera::queueChunk, after its own cave culling and the view frustum. Rows more than N sub-chunks
// above or below the camera are not queued (the measurement hook of 4b66d03 showed the name and arguments).
constexpr const char* QueueChunkSymbol =
    "?queueChunk@LevelRendererCamera@@QEAAXAEAUChunkRenderObjectCollection@@AEBVRenderChunkInstanced@@MMAEBV"
    "TerrainMaterialVariationManager@@V?$optional_ref@$$CBVTerrainMaterialVariationManager@@@@@Z";
MYCLIENT_TYPE_INSTANCE_HOOK(QueueChunkHook, ll::memory::HookPriority::Normal, LevelRendererCamera,
                      MyClientGame::Target(QueueChunkSymbol), void,
                      ChunkRenderObjectCollection& collection, RenderChunkInstanced const& renderChunkInstanced,
                      float farDistance2, float currentTime, TerrainMaterialVariationManager const& terrainVariationMgr,
                      optional_ref<TerrainMaterialVariationManager const> fadeVariationMgr)
{
    const uint32_t rows = verticalRowsWanted.load(std::memory_order_relaxed);
    if (rows) {
        bool skip = false;
        try {
            const RenderChunkShared* shared = renderChunkInstanced.mRenderChunkShared.get();   // the shared_ptr's pointer
            if (shared) {
                const Vec3& camera = mCameraPos.get();
                const BlockPos& pos = shared->mPosition.get();
                skip = MyClientFeatures::OutsideVerticalRows(static_cast<int>(std::floor(pos.y / 16.0f)),
                                                             static_cast<int>(std::floor(camera.y / 16.0f)), rows);
            }
        } catch (...) { skip = false; }
        if (skip) {
            statChunksSkipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    origin(collection, renderChunkInstanced, farDistance2, currentTime, terrainVariationMgr, fadeVariationMgr);
}

// Block entity distance: BlockActorRenderDispatcher::render (the overload with the block's world position) draws one
// chest / sign / banner / bed / skull... Farther than the chosen distance from the player: not drawn this frame.
// Hand-made MSVC name (no game build has confirmed it yet): when it does not resolve, the log says so.
// The last argument, std::optional<dragon::RenderMetadata> by value, is passed (x64 ABI) as a pointer to the
// caller's copy, which the called function destroys: the hook takes that pointer and hands it on untouched (the
// game does not export RenderMetadata's copy / destructor, so it cannot be a real by-value parameter here).
constexpr const char* BlockActorRenderAtSymbol =
    "?render@BlockActorRenderDispatcher@@QEAAXAEAVBaseActorRenderContext@@AEAVBlockSource@@"
    "AEAVIVanillaRenderBlockActorComponent@@AEBVBlock@@AEBVVec3@@AEBVBlockPos@@_NAEBVMaterialPtr@mce@@"
    "PEBUClientTexture@9@HV?$optional@URenderMetadata@dragon@@@std@@@Z";
MYCLIENT_TYPE_INSTANCE_HOOK(BlockActorRenderHook, ll::memory::HookPriority::Normal, BlockActorRenderDispatcher,
                      ll::memory::SymbolView{BlockActorRenderAtSymbol}, void,
                      BaseActorRenderContext& entityRenderContext, BlockSource& renderSource,
                      IVanillaRenderBlockActorComponent& renderComponent, Block const& block, Vec3 const& renderPos,
                      BlockPos const& worldPos, bool renderAlphaLayer, mce::MaterialPtr const& forcedMat,
                      mce::ClientTexture const* forceTex, int breakingAmount, void* renderMetadata)
{
    const float range2 = blockEntityRange2.load(std::memory_order_relaxed);
    if (range2 > 0.f) {
        const float dx = worldPos.x + 0.5f - cullX.load(std::memory_order_relaxed);
        const float dy = worldPos.y + 0.5f - cullY.load(std::memory_order_relaxed);
        const float dz = worldPos.z + 0.5f - cullZ.load(std::memory_order_relaxed);
        if (dx * dx + dy * dy + dz * dz > range2) {
            statBlockEntitiesSkipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    origin(entityRenderContext, renderSource, renderComponent, block, renderPos, worldPos, renderAlphaLayer, forcedMat,
           forceTex, breakingAmount, renderMetadata);
}

// No sky (full): LevelRendererCamera::renderSky draws the sky's colour and gradient (the sun and moon are hidden by
// the OriginX Lite pack already). Skipped while the row is on: the clear colour stays behind the world.
constexpr const char* RenderSkySymbol =
    "?renderSky@LevelRendererCamera@@QEBAXAEAVScreenContext@@AEBUViewRenderObject@@AEBUBaseSceneDirectionalLightRenderData@@@Z";
MYCLIENT_TYPE_INSTANCE_HOOK(RenderSkyHook, ll::memory::HookPriority::Normal, LevelRendererCamera,
                      ll::memory::SymbolView{RenderSkySymbol}, void,
                      ScreenContext& screenContext, ViewRenderObject const& renderObj,
                      BaseSceneDirectionalLightRenderData const& sceneDirectionalLightRenderData)
{
    if (noSkyWanted.load(std::memory_order_relaxed)) {
        statSkySkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    origin(screenContext, renderObj, sceneDirectionalLightRenderData);
}

// One log line a minute while any of these rows is on: how much each one left out.
std::string RenderSkipSummary()
{
    const uint32_t chunks = statChunksSkipped.exchange(0), blocks = statBlockEntitiesSkipped.exchange(0);
    const uint32_t sky = statSkySkipped.exchange(0), behind = statBehindSkipped.exchange(0);
    if (!verticalRowsWanted.load() && blockEntityRange2.load() <= 0.f && !noSkyWanted.load() && !cullBehindWanted.load())
        return {};
    char line[256];
    std::snprintf(line, sizeof line,
                  "Optimization render skips (60 s): sub-chunks=%u (rows %u, hook=%d) block entities=%u (hook=%d) sky=%u (hook=%d) "
                  "behind camera=%u (camera=%d)",
                  chunks, verticalRowsWanted.load(), queueChunkHooked, blocks, blockActorHooked, sky, skyHooked, behind,
                  cameraForwardKnown);
    return line;
}

// ---- Replay files (all called with replayMutex held) ----
std::wstring ReplayFolder()
{
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    std::wstring dir = n > 0 && n < MAX_PATH ? std::wstring(base) + L"\\MyClient" : L"";
    if (dir.empty()) { wchar_t tmp[MAX_PATH]{}; GetTempPathW(MAX_PATH, tmp); dir = std::wstring(tmp) + L"MyClient"; }
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\Replays";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
std::wstring LocalStamp()
{
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t text[32];
    swprintf(text, 32, L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return text;
}
std::string ToUtf8(const std::wstring& text)
{
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 1 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &out[0], len, nullptr, nullptr);
    return out;
}
// "Recording starts here": the session file becomes a recording named after this moment.
void MarkReplay(uint64_t now)
{
    if (!replayWriter.Mark(now)) { replayOpen.store(false); return; }
    replayMarked = true;
    replayMarkedAt = now;
    replayPathW = ReplayFolder() + L"\\replay-" + LocalStamp() + L".mcrec";
    replayPathUtf8 = ToUtf8(replayPathW);
}
// A new session file (at StartGame, or when recording starts with no buffer). `mark` = record from now on.
void OpenReplaySession(uint64_t now, bool mark)
{
    replayStartedAt = now;
    replayPartW = ReplayFolder() + L"\\session-" + LocalStamp() + L"-" + std::to_wstring(now % 100000) + L".part";
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    const uint64_t unixMs = ((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull) / 10000ull;
    if (!replayWriter.Open(replayPartW.c_str(), unixMs, now, "")) return;
    replayOpen.store(true);
    replayMarked = false;
    replaySawStartGame = false;
    if (mark) MarkReplay(now);
}
// Closes the session file: kept as a recording when it was marked (and `keep`), otherwise deleted.
void EndReplaySession(bool keep)
{
    if (!replayWriter.IsOpen()) return;
    replayWriter.Close();
    replayOpen.store(false);
    if (replayMarked && keep && MoveFileExW(replayPartW.c_str(), replayPathW.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        MyClientLog::Get().info("Replay saved: {}", replayPathUtf8);
    else if (!(replayMarked && keep))
        DeleteFileW(replayPartW.c_str());
    replayMarked = false;
}
// Session files left by a crash: kept as recordings when they contain a mark, deleted otherwise.
void RecoverReplayParts()
{
    std::lock_guard guard(replayMutex);
    const std::wstring dir = ReplayFolder();
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\session-*.part").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring path = dir + L"\\" + fd.cFileName;
        bool marked = false;
        if (FILE* f = _wfopen(path.c_str(), L"rb")) {
            uint32_t head[2];
            if (std::fseek(f, static_cast<long>(sizeof(MyClientReplay::Header)), SEEK_SET) == 0)
                while (std::fread(head, 4, 2, f) == 2) {
                    if (head[0] == 0) { marked = true; break; }
                    if (_fseeki64(f, head[0] & ~MyClientReplay::SentFlag, SEEK_CUR) != 0) break;
                }
            std::fclose(f);
        }
        if (marked) {
            std::wstring name = fd.cFileName;   // session-YYYYMMDD-HHMMSS-n.part -> replay-YYYYMMDD-HHMMSS-n.mcrec
            name = L"replay-" + name.substr(8, name.size() - 8 - 5) + L".mcrec";
            MoveFileExW(path.c_str(), (dir + L"\\" + name).c_str(), MOVEFILE_COPY_ALLOWED);
        } else
            DeleteFileW(path.c_str());
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

// ---- Hit Effect ----
// Crit particles around the target's body (the built-in Crit type, flying outward like Java's), and the
// pack's "myclient:enchanted_hit" sparks when the held item is enchanted. Only this client sees them.
void SpawnHitEffect(Player& player, Actor& target)
{
    const AABB& box = *MyClientGame::AabbOf(target);
    const float cx = (box.min.x + box.max.x) * 0.5f, cy = (box.min.y + box.max.y) * 0.5f, cz = (box.min.z + box.max.z) * 0.5f;
    const float hx = (box.max.x - box.min.x) * 0.5f, hy = (box.max.y - box.min.y) * 0.5f, hz = (box.max.z - box.min.z) * 0.5f;
    static std::mt19937 random{std::random_device{}()};
    std::uniform_real_distribution<float> unit(-1.f, 1.f);
    Level& level = target.getLevel();
    for (int i = 0; hitEffectCrit.load() && i < 12; i++) {
        const float rx = unit(random), ry = unit(random), rz = unit(random);
        const Vec3 pos{cx + rx * hx, cy + ry * hy, cz + rz * hz};
        const Vec3 dir{rx * 0.5f, ry * 0.4f + 0.2f, rz * 0.5f};
        level.addParticle(ParticleType::Crit, pos, dir, 0, nullptr, false);
    }
    if (!hitEffectMagic.load()) return;
    const ItemStack* held = MyClientGame::SelectedItemOf(player);
    if (!held || !MyClientGame::IsEnchanted(*held)) return;
    using DimensionFn = Dimension& (Actor::*)() const;
    static const auto getDimension = MyClientGame::ResolveMember<DimensionFn>("?getDimension@Actor@@QEBAAEAVDimension@@XZ");
    if (!getDimension) return;
    level.spawnParticleEffect(LitePack::kEnchantedHitId, Vec3{cx, cy, cz}, &(target.*getDimension)());
}

MYCLIENT_TYPE_INSTANCE_HOOK(HitEffectHook, ll::memory::HookPriority::Normal, GameMode,
                      ll::memory::SymbolView{"?attack@GameMode@@UEAA_NAEAVActor@@AEBVVec3@@@Z"}, bool,
                      Actor& entity, Vec3 const& hitPosition)
{
    const bool result = origin(entity, hitPosition);
    MyClientProfile::Scope prof(MyClientProfile::Hit);
    if (comboAttackCalls.fetch_add(1, std::memory_order_relaxed) + 1 == 20) {
        uint32_t read;
        { std::lock_guard g(comboMutex); read = comboAttackPackets; }
        if (read == 0) MyClientLog::Get().warn("Combo Counter: 20 attacks but no attack packet was read; hits will not be counted");
    }
    if ((hitEffectCrit.load(std::memory_order_relaxed) || hitEffectMagic.load(std::memory_order_relaxed)) && active.load(std::memory_order_relaxed)) {
        try {
            Player& player = mPlayer;
            const auto client = MyClientGame::Client();
            if (client && static_cast<Actor*>(client->getLocalPlayer()) == static_cast<Actor*>(&player))
                SpawnHitEffect(player, entity);
        } catch (...) {}
    }
    return result;
}

// Left Hand: the game places the item in your first-person hand in ItemInHandRenderer (fixed in its code),
// so the pack's mirrored animations moved only the bare arm and held items stayed on the right. While the
// game draws that item, the top of the world matrix stack is mirrored across the middle of the screen
// (LeftHand.h) and put back afterwards. Only the first-person pass is touched; the world and third person
// are not. renderItem may call renderItemNew: only the outermost call mirrors.
// The flags and MeshContext::isDrawingFirstPersonObjects never mark that pass in this game (build-dll #76 log:
// itemDraws in the thousands, firstPerson=0), so ItemInHandRenderer::renderFirstPerson is hooked too: what is
// drawn while it runs is the first-person hand. renderObject is hooked as well, in case that pass draws its
// items without going through renderItem / renderItemNew.
//
// Mirroring the matrix at the start of renderItem did nothing visible (2026-10-28: "not a pixel"): the item's own
// placement (the hand offset, its JSON transforms) is multiplied in below inside that call, and V^-1 S V W S L
// pictures the same as V W L when W is camera-aligned, the reflection and the screen mirror cancel. So the
// mirror is made at the very last step, on the full world matrix of each mesh the first-person item draws
// (LeftHandMeshHook), where nothing is multiplied in after it.
std::atomic<ItemInHandRenderer*> schemItemRenderer{nullptr};   // last ItemInHandRenderer seen (Schematic spike)
thread_local int leftHandDepth = 0;
thread_local int firstPersonDepth = 0;
thread_local int leftHandItemDepth = 0;   // inside a first-person held-item draw: its meshes are mirrored
template <class Fn>
void RunLeftHand(BaseActorRenderContext& renderContext, const Actor* entity, ItemContextFlags itemFlags, Fn&& draw)
{
    if (leftHandDepth > 0 || !leftHandWanted.load(std::memory_order_relaxed) || !active.load(std::memory_order_relaxed)) {
        draw();
        return;
    }
    statLeftHandCalls.fetch_add(1, std::memory_order_relaxed);
    const bool own = entity && entity == leftHandOwnPlayer.load(std::memory_order_relaxed);
    if (own) statLeftHandOwn.fetch_add(1, std::memory_order_relaxed);
    bool firstPerson = false;
    try {
        ScreenContext& screen = renderContext.mScreenContext;
        mce::MeshContext& mesh = screen;
        firstPerson = !mesh.isDrawingUI && (firstPersonDepth > 0 || mesh.isDrawingFirstPersonObjects ||
            (own && leftHandFirstPersonView.load(std::memory_order_relaxed)) ||
            (static_cast<uint8_t>(itemFlags) & static_cast<uint8_t>(ItemContextFlags::FirstPersonPass)) != 0);
    } catch (...) { firstPerson = false; }
    if (firstPerson) statLeftHandFirst.fetch_add(1, std::memory_order_relaxed);
    ++leftHandDepth;
    if (firstPerson) ++leftHandItemDepth;
    draw();
    if (firstPerson) --leftHandItemDepth;
    --leftHandDepth;
}

// One mesh of the first-person held item: its full world matrix mirrored across the middle of the screen
// (LeftHand.h) for this draw only.
// renderFirstPerson draws its item without going through renderItem / renderItemNew / renderObject (2026-10-28 log:
// firstPersonPasses=14306, firstPerson=0, mirroredMeshes=0), so every mesh drawn inside it is mirrored too. The
// arm is a skinned mesh whose placement is in its bone matrices: the mirror leaves it where it is (the pack's
// animations move it).
thread_local bool projectionMirrored = false;   // inside a pass mirrored as a whole (ItemInHandPassHook)
template <class Fn>
void MirrorMesh(mce::MeshContext& mesh, Fn&& draw)
{
    if (projectionMirrored) { draw(); return; }   // already on the other side: a second mirror would undo it
    const bool inPass = firstPersonDepth > 0 && leftHandWanted.load(std::memory_order_relaxed) &&
                        active.load(std::memory_order_relaxed);
    if (inPass) statFirstPersonMeshes.fetch_add(1, std::memory_order_relaxed);
    if ((leftHandItemDepth <= 0 && !inPass) || mesh.isDrawingUI) { draw(); return; }
    MatrixStack* worldStack = nullptr;
    size_t depth = 0;
    glm::mat4x4 saved{};
    try {
        mce::Camera& camera = mesh.camera;
        if (!camera.viewMatrixStack->stack->empty() && !camera.worldMatrixStack->stack->empty()) {
            const glm::mat4x4& view = *camera.viewMatrixStack->stack->back()._m;
            glm::mat4x4& world = *camera.worldMatrixStack->stack->back()._m;
            const glm::mat4x4 before = world;
            if (MyClientLeftHand::MirrorInView(&view[0][0], &world[0][0])) {
                worldStack = &*camera.worldMatrixStack;
                depth = worldStack->stack->size();
                saved = before;
                worldStack->_isDirty = true;
                statLeftHand.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } catch (...) { worldStack = nullptr; }
    draw();
    if (worldStack && worldStack->stack->size() == depth) {
        *worldStack->stack->back()._m = saved;
        worldStack->_isDirty = true;
        statLeftHandRestored.fetch_add(1, std::memory_order_relaxed);
    }
}

// mce::Mesh::_renderMesh. Its by-value textures (static_vector) and render metadata (optional) go as pointers to
// copies the callee destroys (x64 ABI), so they are passed through untouched as pointers.
// Not in the SDK's import library either: found by name.
#define MYCLIENT_RENDERMESH(access)                                                                                     \
    "?_renderMesh@Mesh@mce@@" access "XAEAVMeshContext@2@AEBVMaterialPtr@2@V?$static_vector@V?$variant@Umonostate@std@@" \
    "VTexturePtr@mce@@UClientTexture@4@UServerTexture@4@@std@@$07@brstd@@IIAEBUOffscreenCaptureDescription@@"            \
    "PEBVIndexBufferContainer@2@V?$optional@URenderMetadata@dragon@@@std@@@Z"
inline uintptr_t RenderMeshTarget()
{
    return MyClientGame::ResolveFirst({MYCLIENT_RENDERMESH("QEBA"), MYCLIENT_RENDERMESH("AEBA"), MYCLIENT_RENDERMESH("IEBA")});
}
#undef MYCLIENT_RENDERMESH
MYCLIENT_TYPE_INSTANCE_HOOK(LeftHandMeshHook, ll::memory::HookPriority::Normal, mce::Mesh, RenderMeshTarget(), void,
                      mce::MeshContext& meshContext, void const* materialPtr, void* textures, uint startOffset, uint count,
                      void const* offscreenCaptureDescription, void const* overrideIndexBuffer, void* renderMetadata)
{
    MirrorMesh(meshContext, [&] {
        origin(meshContext, materialPtr, textures, startOffset, count, offscreenCaptureDescription, overrideIndexBuffer,
               renderMetadata);
    });
}

constexpr const char* RenderItemSymbol =
    "?renderItem@ItemInHandRenderer@@QEAAXAEAVBaseActorRenderContext@@AEAVActor@@AEBVItemStack@@_NW4ItemContextFlags@@33@Z";
constexpr const char* RenderItemNewSymbol =
    "?renderItemNew@ItemInHandRenderer@@QEAAXAEAVBaseActorRenderContext@@AEAVActor@@AEBVItemStack@@W4ItemContextFlags@@UBrightness@@@Z";
constexpr const char* RenderFirstPersonSymbol =
    "?renderFirstPerson@ItemInHandRenderer@@QEAAXAEAVBaseActorRenderContext@@AEBVMatrix@@W4ItemContextFlags@@@Z";
constexpr const char* RenderObjectSymbol =
    "?renderObject@ItemInHandRenderer@@QEAAXAEAVBaseActorRenderContext@@AEBVItemRenderCall@@AEBURenderMetadata@dragon@@W4ItemContextFlags@@@Z";

MYCLIENT_TYPE_INSTANCE_HOOK(LeftHandItemHook, ll::memory::HookPriority::Normal, ItemInHandRenderer,
                      ll::memory::SymbolView{RenderItemSymbol}, void,
                      BaseActorRenderContext& renderContext, Actor& entity, ItemStack const& item, bool posAndRotSetByJSON,
                      ItemContextFlags itemFlags, bool useMatrixAsIs, bool renderingMainHand)
{
    RunLeftHand(renderContext, &entity, itemFlags, [&] {
        origin(renderContext, entity, item, posAndRotSetByJSON, itemFlags, useMatrixAsIs, renderingMainHand);
    });
}

MYCLIENT_TYPE_INSTANCE_HOOK(LeftHandItemNewHook, ll::memory::HookPriority::Normal, ItemInHandRenderer,
                      MyClientGame::Target(RenderItemNewSymbol), void,
                      BaseActorRenderContext& renderContext, Actor& entity, ItemStack const& item, ItemContextFlags itemFlags,
                      Brightness lightEmission)
{
    RunLeftHand(renderContext, &entity, itemFlags, [&] { origin(renderContext, entity, item, itemFlags, lightEmission); });
}

MYCLIENT_TYPE_INSTANCE_HOOK(LeftHandFirstPersonHook, ll::memory::HookPriority::Normal, ItemInHandRenderer,
                      ll::memory::SymbolView{RenderFirstPersonSymbol}, void,
                      BaseActorRenderContext& renderContext, Matrix const& prevProj, ItemContextFlags itemFlags)
{
    statFirstPersonPasses.fetch_add(1, std::memory_order_relaxed);
    schemItemRenderer.store(this, std::memory_order_relaxed);
    ++firstPersonDepth;
    origin(renderContext, prevProj, itemFlags);
    --firstPersonDepth;
}

// InGamePlayScreen::_renderItemInHand: the game's first-person pass (your arm and held item). renderFirstPerson ran
// every frame but no item or mesh draw was seen inside it (2026-10-28 log: firstPersonPasses=14731, ownDraws=0,
// firstPersonMeshes=0): the arm and item are drawn by a path the per-mesh mirror never reaches. So this whole pass
// is mirrored across the middle of the screen: arm and item both come out on the left, as left-handed. Put back
// afterwards; the world and the HUD are not touched.
inline uintptr_t ItemInHandPassSymbol()
{
    return MyClientGame::ResolveFirst({"?_renderItemInHand@InGamePlayScreen@@UEAAXAEAVScreenContext@@AEAVPlayer@@@Z",
                                       "?_renderItemInHand@InGamePlayScreen@@MEAAXAEAVScreenContext@@AEAVPlayer@@@Z",
                                       "?_renderItemInHand@InGamePlayScreen@@EEAAXAEAVScreenContext@@AEAVPlayer@@@Z"});
}
struct ItemInHandPass {};
MYCLIENT_TYPE_INSTANCE_HOOK(ItemInHandPassHook, ll::memory::HookPriority::Normal, ItemInHandPass, ItemInHandPassSymbol(), void,
                      ScreenContext& screenContext, Player& player)
{
    if (!leftHandWanted.load(std::memory_order_relaxed) || !active.load(std::memory_order_relaxed) || projectionMirrored) {
        origin(screenContext, player);
        return;
    }
    statItemInHandPasses.fetch_add(1, std::memory_order_relaxed);
    // 2026-10-28 log: the projection was mirrored in every pass (mirroredPasses = itemInHandPasses) and nothing
    // moved: the pass makes its own projection. The world matrix is multiplied into instead (MirrorWorldInView).
    MatrixStack* stack = nullptr;
    size_t depth = 0;
    glm::mat4x4 saved{};
    try {
        mce::MeshContext& mesh = screenContext;
        mce::Camera& camera = mesh.camera;
        if (!camera.viewMatrixStack->stack->empty() && !camera.worldMatrixStack->stack->empty()) {
            const glm::mat4x4& view = *camera.viewMatrixStack->stack->back()._m;
            glm::mat4x4& world = *camera.worldMatrixStack->stack->back()._m;
            const glm::mat4x4 before = world;
            if (MyClientLeftHand::MirrorWorldInView(&view[0][0], &world[0][0])) {
                saved = before;
                stack = &*camera.worldMatrixStack;
                depth = stack->stack->size();
                stack->_isDirty = true;
                statItemInHandMirrored.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } catch (...) { stack = nullptr; }
    projectionMirrored = stack != nullptr;
    origin(screenContext, player);
    projectionMirrored = false;
    if (stack && stack->stack->size() == depth) {
        *stack->stack->back()._m = saved;
        stack->_isDirty = true;
    }
}

MYCLIENT_TYPE_INSTANCE_HOOK(LeftHandObjectHook, ll::memory::HookPriority::Normal, ItemInHandRenderer,
                      ll::memory::SymbolView{RenderObjectSymbol}, void,
                      BaseActorRenderContext& renderContext, ItemRenderCall const& renderObject,
                      dragon::RenderMetadata const& renderMetadata, ItemContextFlags itemFlags)
{
    schemItemRenderer.store(this, std::memory_order_relaxed);
    RunLeftHand(renderContext, nullptr, itemFlags, [&] { origin(renderContext, renderObject, renderMetadata, itemFlags); });
}

// ---- Schematic spike (docs/schematic-s0-report.md step 5; docs/superpowers/specs/2026-11-17-schematic-design.md §3) ----
// A test only, off unless %LOCALAPPDATA%\MyClient\schematic_spike.txt exists at game start (no HUD row, nothing in the
// shared config). Its first word is "after" (draw after the game's world draw, the default) or "before"; "off" stops it.
// The file is read again every 2 s. Two blocks (stone, oak_stairs facing east, not upside down) are made once from
// their palette NBT with the game's own functions (tryGetBlockFromNBT -> _tessellateBlockItem -> Tessellator::end) and
// drawn every frame 3 blocks in front of the camera with ItemInHandRenderer's mMatBlendBlock and the block atlas.
// Anything missing (a function, the item renderer, a Block, the atlas) or a fault is logged once and the spike stops;
// the game is never stopped. Log lines start with "Schematic spike:". Meshes are kept for the run (a test build).
constexpr const char* SchemTryGetBlockSymbol =
    "?tryGetBlockFromNBT@BlockSerializationUtils@@YA?AU?$pair@W4NBTState@BlockSerializationUtils@@PEBVBlock@@@std@@AEBVCompoundTag@@PEAUNbtToBlockCache@1@@Z";
constexpr const char* SchemTessellateSymbol =
    "?_tessellateBlockItem@ItemInHandRenderer@@QEAAXAEAVTessellator@@AEAVBlockTessellator@@AEBVBlock@@AEBUFrameLightingModelCapabilities@framebuilder@mce@@@Z";
constexpr const char* SchemTessEndSymbol =
    "?end@Tessellator@@QEAA?AVMesh@mce@@W4UploadMode@1@V?$basic_string_view@DU?$char_traits@D@std@@@std@@W4SupplementaryFieldAutoGenerationMode@@@Z";
constexpr const char* SchemRenderMeshSymbol =
    "?renderMesh@Mesh@mce@@QEBAXAEAVMeshContext@2@AEBVMaterialPtr@2@AEBV?$variant@Umonostate@std@@VTexturePtr@mce@@UClientTexture@4@UServerTexture@4@@std@@IIAEBUOffscreenCaptureDescription@@PEBVIndexBufferContainer@2@@Z";
// x64: a returned object goes through a hidden pointer (first for a free function, right after `this` for a member).
using SchemTryGetBlockFn = void* (*)(void* result, void const* tag, void* cache);
using SchemTessellateFn = void (*)(ItemInHandRenderer* self, Tessellator* tessellator, BlockTessellator* blocks,
                                   Block const* block, void const* lightingCaps);
using SchemTessEndFn = void* (*)(Tessellator* self, void* mesh, int uploadMode, std::string_view const* debugName, int fieldMode);
using SchemRenderMeshFn = void (*)(mce::Mesh const* self, mce::MeshContext* context, mce::MaterialPtr const* material,
                                   void const* texture, uint startOffset, uint count, void const* offscreen, void const* indexBuffer);

// The palette NBT is built here, in the game's own CompoundTag layout, without the game's Tag vtables (the tags are
// only read by tryGetBlockFromNBT; their vtable pointers stay null). The layout is checked against the SDK below.
namespace schem_nbt {
struct Opaque8 { void* vt = nullptr; };
template <size_t N> struct alignas(8) Raw { unsigned char bytes[N]{}; };
struct StringT { void* vt = nullptr; std::string text; };
struct Variant;
struct CompoundT { void* vt = nullptr; std::map<std::string, Variant, std::less<void>> tags; };
struct ByteT { void* vt = nullptr; uint8_t value = 0; };
struct IntT { void* vt = nullptr; int value = 0; };
struct Variant {
    std::variant<Opaque8, ByteT, Raw<sizeof(::ShortTag)>, IntT, Raw<sizeof(::Int64Tag)>, Raw<sizeof(::FloatTag)>,
                 Raw<sizeof(::DoubleTag)>, Raw<sizeof(::ByteArrayTag)>, StringT, Raw<sizeof(::ListTag)>, CompoundT> value;
};
static_assert(sizeof(Variant) == sizeof(::CompoundTagVariant) && alignof(Variant) == alignof(::CompoundTagVariant));
static_assert(sizeof(CompoundT) == sizeof(::CompoundTag) && sizeof(StringT) == sizeof(::StringTag));
static_assert(sizeof(ByteT) == sizeof(::ByteTag) && sizeof(IntT) == sizeof(::IntTag) && sizeof(Opaque8) == sizeof(::EndTag));
}

struct SchemTestBlock { const char* label; Block const* block = nullptr; alignas(16) unsigned char mesh[sizeof(mce::Mesh)]{}; bool meshMade = false; };
SchemTestBlock schemBlocks[2] = {{"minecraft:stone"}, {"minecraft:oak_stairs"}};
enum class SchemMode { Off, Before, After };
SchemMode schemMode = SchemMode::Off;   // render thread
uint64_t schemModeReadAt = 0;
bool schemStopped = false, schemReady = false, schemViewLogged = false;
SchemTryGetBlockFn schemTryGetBlock = nullptr;
SchemTessellateFn schemTessellate = nullptr;
SchemTessEndFn schemTessEnd = nullptr;
SchemRenderMeshFn schemRenderMesh = nullptr;
uint64_t schemStatAt = 0, schemStatFrames = 0;
double schemStatTotalUs = 0, schemStatMaxUs = 0;

std::filesystem::path SchemSpikeFile()
{
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return std::filesystem::path(base) / L"MyClient" / L"schematic_spike.txt";
}
SchemMode ReadSchemMode()
{
    const auto file = SchemSpikeFile();
    if (file.empty()) return SchemMode::Off;
    std::ifstream in(file);
    if (!in) return SchemMode::Off;
    std::string word;
    in >> word;
    if (word == "off") return SchemMode::Off;
    return word == "before" ? SchemMode::Before : SchemMode::After;
}
void SchemStop(const char* why)
{
    if (schemStopped) return;
    schemStopped = true;
    MyClientLog::Get().warn("Schematic spike: stopped ({})", why);
}

// The two blocks from their palette NBT. Once; the result of each call is logged.
bool SchemMakeBlocks()
{
    using namespace schem_nbt;
    auto& log = MyClientLog::Get();
    for (int i = 0; i < 2; ++i) {
        CompoundT tag;
        tag.tags["name"].value.emplace<StringT>().text = schemBlocks[i].label;
        auto& states = tag.tags["states"].value.emplace<CompoundT>();
        if (i == 1) {
            states.tags["weirdo_direction"].value.emplace<IntT>().value = 0;
            states.tags["upside_down_bit"].value.emplace<ByteT>().value = 0;
        }
        tag.tags["version"].value.emplace<IntT>().value = 18168865;   // 1.21.60.33, the palette version of current exports
        struct { int state; Block const* block; } result{-1, nullptr};
        schemTryGetBlock(&result, &tag, nullptr);
        schemBlocks[i].block = result.block;
        log.info("Schematic spike: tryGetBlockFromNBT({}) -> NBTState={} block={}", schemBlocks[i].label, result.state,
                 result.block ? "non-null" : "null");
    }
    return schemBlocks[0].block && schemBlocks[1].block;
}

// The meshes, once, on the render thread (as _rebuildItem makes them), with the screen's tessellator.
bool SchemMakeMeshes(ScreenContext& screen, ItemInHandRenderer& items)
{
    auto& log = MyClientLog::Get();
    Tessellator& tessellator = screen.tessellator;
    BlockTessellator& blocks = items.mBlockTessellator;
    alignas(16) unsigned char caps[64]{};   // FrameLightingModelCapabilities: none (plain lighting)
    const std::string_view name{"MyClient schematic spike"};
    for (auto& entry : schemBlocks) {
        schemTessellate(&items, &tessellator, &blocks, entry.block, caps);
        schemTessEnd(&tessellator, entry.mesh, 0, &name, 2);
        entry.meshMade = true;
        const auto& made = *reinterpret_cast<mce::Mesh const*>(entry.mesh);
        const std::optional<uint>& vertices = made.mVertexCount;
        log.info("Schematic spike: mesh for {} made (vertices={})", entry.label, vertices ? static_cast<int>(*vertices) : -1);
    }
    return true;
}

// SEH guard: a fault inside the game's functions stops the spike instead of the game. No C++ objects with
// destructors live in these frames.
template <class Fn>
bool SchemGuarded(Fn* fn, void* arg)
{
    __try { return fn(arg); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct SchemDrawArgs { ScreenContext* screen; ItemInHandRenderer* items; };
bool SchemDrawUnguarded(void* raw)
{
    auto& args = *static_cast<SchemDrawArgs*>(raw);
    mce::MeshContext& mesh = *args.screen;
    if (mesh.isDrawingUI) return true;
    mce::Camera& camera = mesh.camera;
    if (camera.viewMatrixStack->stack->empty() || camera.worldMatrixStack->stack->empty()) return true;
    const glm::mat4x4& view = *camera.viewMatrixStack->stack->back()._m;
    if (!schemViewLogged) {
        schemViewLogged = true;
        MyClientLog::Get().info("Schematic spike: view translation=({:.2f}, {:.2f}, {:.2f}) captured camera=({:.2f}, {:.2f}, {:.2f})",
                                view[3][0], view[3][1], view[3][2], lastCameraPos[0], lastCameraPos[1], lastCameraPos[2]);
    }
    // Camera position: the one captured from the world draw this run, else the local player's eye.
    float cam[3] = {lastCameraPos[0], lastCameraPos[1], lastCameraPos[2]};
    if (cam[0] == 0.f && cam[1] == 0.f && cam[2] == 0.f) {
        const auto client = MyClientGame::Client();
        auto* player = client ? client->getLocalPlayer() : nullptr;
        if (!player) return true;
        const Vec3 p = player->getPosition();
        cam[0] = p.x; cam[1] = p.y; cam[2] = p.z;
    }
    float f[3] = {-view[0][2], 0.f, -view[2][2]};   // forward on the ground
    const float length = std::sqrt(f[0] * f[0] + f[2] * f[2]);
    if (length < 0.1f) { f[0] = 0.f; f[2] = 1.f; } else { f[0] /= length; f[2] /= length; }
    const float base[3] = {std::floor(cam[0] + f[0] * 3.f), std::floor(cam[1] - 1.62f), std::floor(cam[2] + f[2] * 3.f)};
    // Stone at the feet's level, the stairs one block to its +X side.
    alignas(8) unsigned char texture[0x28]{};   // variant<monostate, TexturePtr, ...> holding the block atlas
    static_assert(sizeof(mce::TexturePtr) == 0x20);
    const auto client = MyClientGame::Client();
    LevelRenderer* renderer = client ? client->getLevelRenderer() : nullptr;
    if (!renderer) return true;
    std::memcpy(texture, &static_cast<mce::TexturePtr&>(renderer->mAtlasTexture), 0x20);   // not owned: never destroyed
    texture[0x20] = 1;
    alignas(16) unsigned char offscreen[64]{};
    mce::MaterialPtr const& material = args.items->mMatBlendBlock;
    MatrixStack& worldStack = *camera.worldMatrixStack;
    glm::mat4x4& world = *worldStack.stack->back()._m;
    const glm::mat4x4 saved = world;
    for (int i = 0; i < 2; ++i) {
        if (!schemBlocks[i].meshMade) continue;
        glm::mat4x4 placed(1.f);
        placed[3][0] = base[0] + static_cast<float>(i) - cam[0];
        placed[3][1] = base[1] - cam[1];
        placed[3][2] = base[2] - cam[2];
        world = placed;
        worldStack._isDirty = true;
        schemRenderMesh(reinterpret_cast<mce::Mesh const*>(schemBlocks[i].mesh), &mesh, &material, texture, 0, 0, offscreen, nullptr);
    }
    world = saved;
    worldStack._isDirty = true;
    return true;
}
bool SchemSetupUnguarded(void* raw)
{
    auto& args = *static_cast<SchemDrawArgs*>(raw);
    if (!SchemMakeBlocks()) return false;
    return SchemMakeMeshes(*args.screen, *args.items);
}

void SchematicSpikeFrame(ScreenContext& screen, bool beforeWorld)
{
    if (schemStopped) return;
    const uint64_t now = GetTickCount64();
    if (beforeWorld && now - schemModeReadAt >= 2000) {
        schemModeReadAt = now;
        const SchemMode mode = ReadSchemMode();
        if (mode != schemMode) MyClientLog::Get().info("Schematic spike: mode {}", mode == SchemMode::Off ? "off" : mode == SchemMode::Before ? "before" : "after");
        schemMode = mode;
    }
    if (schemMode == SchemMode::Off || (schemMode == SchemMode::Before) != beforeWorld) return;
    const auto client = MyClientGame::Client();
    if (!client || !client->getLocalPlayer()) return;
    ItemInHandRenderer* items = schemItemRenderer.load(std::memory_order_relaxed);
    if (!items) return;   // the Left Hand hooks see it on the first frame that draws the hand pass
    SchemDrawArgs args{&screen, items};
    if (!schemReady) {
        if (!SchemGuarded(&SchemSetupUnguarded, &args)) { SchemStop("the blocks or their meshes could not be made"); return; }
        schemReady = true;
    }
    LARGE_INTEGER t0{}, t1{}, freq{};
    QueryPerformanceCounter(&t0);
    if (!SchemGuarded(&SchemDrawUnguarded, &args)) { SchemStop("a fault while drawing"); return; }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    const double us = freq.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1e6 / double(freq.QuadPart) : 0.0;
    schemStatTotalUs += us; schemStatFrames++; schemStatMaxUs = (std::max)(schemStatMaxUs, us);
    if (!schemStatAt) schemStatAt = now;
    if (now - schemStatAt >= 5000) {
        MyClientLog::Get().info("Schematic spike: draw time avg={:.1f} us max={:.1f} us over {} frames", schemStatTotalUs / double(schemStatFrames),
                                schemStatMaxUs, schemStatFrames);
        schemStatAt = now; schemStatFrames = 0; schemStatTotalUs = 0; schemStatMaxUs = 0;
    }
}

// At start-up, only when the test file is there: resolve the four functions and install the scene hook.
void InstallSchematicSpike()
{
    auto& log = MyClientLog::Get();
    if (ReadSchemMode() == SchemMode::Off) return;
    schemTryGetBlock = MyClientGame::ResolveFunction<SchemTryGetBlockFn>(SchemTryGetBlockSymbol);
    schemTessellate = MyClientGame::ResolveFunction<SchemTessellateFn>(SchemTessellateSymbol);
    schemTessEnd = MyClientGame::ResolveFunction<SchemTessEndFn>(SchemTessEndSymbol);
    schemRenderMesh = MyClientGame::ResolveFunction<SchemRenderMeshFn>(SchemRenderMeshSymbol);
    const bool scene = EnsureSceneRenderHook();
    log.info("Schematic spike: functions tryGetBlockFromNBT={} _tessellateBlockItem={} Tessellator::end={} renderMesh={} sceneHook={}",
             schemTryGetBlock != nullptr, schemTessellate != nullptr, schemTessEnd != nullptr, schemRenderMesh != nullptr, scene);
    if (!schemTryGetBlock || !schemTessellate || !schemTessEnd || !schemRenderMesh || !scene) {
        SchemStop("a function is missing on this game build");
        return;
    }
    schematicSpikeWanted.store(true);
}

// --- Cape HD spike (docs/superpowers/plans/2026-11-19-cape-hd-s0.md, design 2026-11-19-cape-hd-render-design.md) ----
// Only while %LOCALAPPDATA%\MyClient\cape_hd_spike.txt exists. Its first word is the cape (1-6, default 2); a second
// word "ext" asks for "<id>.png" instead of "<id>" (which one the game's loader wants is part of what S0 finds out).
// The HUD writes the original 2048x1024 PNGs to capes\hd\<id>.png. The picture is asked from the game's texture group
// as a Raw resource location, a board of 4 vertices is made once with Tessellator::begin / vertex / end, and it is
// drawn every frame where the camera was looking the first time (3 blocks ahead, 1.25 x 2 blocks, both sides). Any
// missing function or a fault is logged once and the spike stops; the game is never stopped. Lines: "Cape HD spike:".
constexpr const char* CapeHdBeginSymbol = "?begin@Tessellator@@QEAAXUDebugContextCallback@1@W4PrimitiveMode@mce@@H_N@Z";
constexpr const char* CapeHdVertexSymbol = "?vertex@Tessellator@@QEAAXMMM@Z";
using CapeHdBeginFn = void (*)(Tessellator* self, char debugContext, uint8_t mode, int reservedVertices, bool buildFaceData);
using CapeHdVertexFn = void (*)(Tessellator* self, float x, float y, float z);
using CapeHdColorFn = void (*)(Tessellator* self, float r, float g, float b, float a);
// TextureGroupBase's second virtual (after the destructor): getTexture(location, forceReload, loadOrder, layer).
using CapeHdGetTextureFn = void* (*)(void* self, void* result, void const* location, bool forceReload,
                                      std::optional<uint32_t> loadOrder, uint8_t layer);
// ResourceLocation without its game constructor: the same fields. The hashes are only used to file it in the texture
// group's own map, so any value works as long as this location keeps it.
struct CapeHdLocation { int fileSystem = 2; std::string path; uint64_t pathHash = 0, fullHash = 0; };
static_assert(sizeof(CapeHdLocation) == sizeof(::ResourceLocation));
CapeHdBeginFn capeHdBegin = nullptr;
CapeHdVertexFn capeHdVertex = nullptr;
CapeHdColorFn capeHdColor = nullptr;
bool capeHdStopped = false, capeHdMeshMade = false, capeHdPlaced = false, capeHdAsked = false;
uint32_t capeHdCape = 2;
bool capeHdExt = false;
CapeHdLocation* capeHdLocation = nullptr;   // kept for the run: the texture group may keep pointing at it
alignas(16) unsigned char capeHdMesh[sizeof(mce::Mesh)]{};
alignas(8) unsigned char capeHdTexture[0x28]{};   // variant<monostate, TexturePtr, ...>: the TexturePtr, then index 1
float capeHdBase[3]{}, capeHdSide[2]{};
uint64_t capeHdStatAt = 0, capeHdFrames = 0;
double capeHdTotalUs = 0, capeHdMaxUs = 0;

uint64_t CapeHdHash(std::string_view text)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : text) { h ^= c; h *= 0x100000001b3ull; }
    return h;
}
std::filesystem::path CapeHdSpikeFile()
{
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return std::filesystem::path(base) / L"MyClient" / L"cape_hd_spike.txt";
}
void CapeHdStop(const char* why)
{
    if (capeHdStopped) return;
    capeHdStopped = true;
    MyClientLog::Get().warn("Cape HD spike: stopped ({})", why);
}
// UV of the next vertex: Tessellator::mNextUV[0] (std::optional<glm::vec2>), set inline by the game.
void CapeHdUv(Tessellator& t, float u, float v)
{
    auto& uv = (*reinterpret_cast<std::optional<glm::vec2>(*)[3]>(&t.mNextUV))[0];
    uv = glm::vec2(u, v);
}
bool CapeHdMakeUnguarded(void* raw)
{
    auto& screen = *static_cast<ScreenContext*>(raw);
    Tessellator& t = screen.tessellator;
    capeHdBegin(&t, 0, 1 /* QuadList */, 8, false);
    if (capeHdColor) capeHdColor(&t, 1.f, 1.f, 1.f, 1.f);
    // The cape's outer face in the 64x32 layout: x 1..11, y 1..17 (the HD picture is the same layout x32).
    const float u0 = 1.f / 64.f, u1 = 11.f / 64.f, v0 = 1.f / 32.f, v1 = 17.f / 32.f;
    const float w = 1.25f, h = 2.f;
    const float x[4] = {0.f, w, w, 0.f}, y[4] = {0.f, 0.f, h, h}, u[4] = {u0, u1, u1, u0}, v[4] = {v1, v1, v0, v0};
    for (int i = 0; i < 4; ++i) { CapeHdUv(t, u[i], v[i]); capeHdVertex(&t, x[i], y[i], 0.f); }
    for (int i = 3; i >= 0; --i) { CapeHdUv(t, u[i], v[i]); capeHdVertex(&t, x[i], y[i], 0.f); }   // the other side
    const std::string_view name{"MyClient cape HD spike"};
    schemTessEnd(&t, capeHdMesh, 0, &name, 2);
    capeHdMeshMade = true;
    const auto& made = *reinterpret_cast<mce::Mesh const*>(capeHdMesh);
    const std::optional<uint>& vertices = made.mVertexCount;
    MyClientLog::Get().info("Cape HD spike: board made (vertices={}, uv offset=0x{:X})", vertices ? static_cast<int>(*vertices) : -1,
                            reinterpret_cast<char*>(&t.mNextUV) - reinterpret_cast<char*>(&t));
    return true;
}
bool CapeHdTextureUnguarded(void*)
{
    const auto client = MyClientGame::Client();
    if (!client) return true;
    const auto group = client->getTextureGroup();
    if (!group) { MyClientLog::Get().warn("Cape HD spike: no texture group"); return false; }
    ::mce::TextureGroupBase* base = group.get();
    auto** vtable = *reinterpret_cast<void***>(base);
    const auto get = reinterpret_cast<CapeHdGetTextureFn>(vtable[1]);
    std::memset(capeHdTexture, 0, sizeof capeHdTexture);
    get(base, capeHdTexture, capeHdLocation, false, std::nullopt, 0 /* Color */);
    capeHdTexture[0x20] = 1;
    uint64_t words[4]{};
    std::memcpy(words, capeHdTexture, sizeof words);
    MyClientLog::Get().info("Cape HD spike: texture asked for '{}' -> {:#x} {:#x} {:#x} {:#x}", capeHdLocation->path, words[0], words[1],
                            words[2], words[3]);
    return true;
}
struct CapeHdDrawArgs { ScreenContext* screen; ItemInHandRenderer* items; };
bool CapeHdDrawUnguarded(void* raw)
{
    auto& args = *static_cast<CapeHdDrawArgs*>(raw);
    mce::MeshContext& mesh = *args.screen;
    if (mesh.isDrawingUI) return true;
    mce::Camera& camera = mesh.camera;
    if (camera.viewMatrixStack->stack->empty() || camera.worldMatrixStack->stack->empty()) return true;
    const auto client = MyClientGame::Client();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return true;
    float cam[3] = {lastCameraPos[0], lastCameraPos[1], lastCameraPos[2]};
    if (cam[0] == 0.f && cam[1] == 0.f && cam[2] == 0.f) { const Vec3 p = player->getPosition(); cam[0] = p.x; cam[1] = p.y; cam[2] = p.z; }
    if (!capeHdPlaced) {
        const glm::mat4x4& view = *camera.viewMatrixStack->stack->back()._m;
        float f[2] = {-view[0][2], -view[2][2]};
        const float length = std::sqrt(f[0] * f[0] + f[1] * f[1]);
        if (length < 0.1f) { f[0] = 0.f; f[1] = 1.f; } else { f[0] /= length; f[1] /= length; }
        capeHdBase[0] = cam[0] + f[0] * 3.f; capeHdBase[1] = std::floor(cam[1] - 1.62f) + 1.f; capeHdBase[2] = cam[2] + f[1] * 3.f;
        capeHdSide[0] = -f[1]; capeHdSide[1] = f[0];   // the board faces the camera
        capeHdPlaced = true;
        MyClientLog::Get().info("Cape HD spike: board placed at ({:.1f}, {:.1f}, {:.1f})", capeHdBase[0], capeHdBase[1], capeHdBase[2]);
    }
    mce::MaterialPtr const& material = args.items->mMatBlendBlock;
    MatrixStack& worldStack = *camera.worldMatrixStack;
    glm::mat4x4& world = *worldStack.stack->back()._m;
    const glm::mat4x4 saved = world;
    glm::mat4x4 placed(1.f);
    placed[0][0] = capeHdSide[0]; placed[0][2] = capeHdSide[1];   // x axis along the board
    placed[2][0] = -capeHdSide[1]; placed[2][2] = capeHdSide[0];
    placed[3][0] = capeHdBase[0] - capeHdSide[0] * 0.625f - cam[0];
    placed[3][1] = capeHdBase[1] - cam[1];
    placed[3][2] = capeHdBase[2] - capeHdSide[1] * 0.625f - cam[2];
    world = placed;
    worldStack._isDirty = true;
    alignas(16) unsigned char offscreen[64]{};
    schemRenderMesh(reinterpret_cast<mce::Mesh const*>(capeHdMesh), &mesh, &material, capeHdTexture, 0, 0, offscreen, nullptr);
    world = saved;
    worldStack._isDirty = true;
    return true;
}
void CapeHdSpikeFrame(ScreenContext& screen)
{
    if (capeHdStopped) return;
    const auto client = MyClientGame::Client();
    if (!client || !client->getLocalPlayer()) return;
    ItemInHandRenderer* items = schemItemRenderer.load(std::memory_order_relaxed);
    if (!items) return;   // the Left Hand hooks see it on the first frame that draws the hand pass
    if (!capeHdAsked) {
        capeHdAsked = true;
        if (!SchemGuarded(&CapeHdTextureUnguarded, nullptr)) { CapeHdStop("a fault while asking for the picture"); return; }
    }
    if (!capeHdMeshMade && !SchemGuarded(&CapeHdMakeUnguarded, &screen)) { CapeHdStop("a fault while making the board"); return; }
    LARGE_INTEGER t0{}, t1{}, freq{};
    QueryPerformanceCounter(&t0);
    CapeHdDrawArgs args{&screen, items};
    if (!SchemGuarded(&CapeHdDrawUnguarded, &args)) { CapeHdStop("a fault while drawing"); return; }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    const double us = freq.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1e6 / double(freq.QuadPart) : 0.0;
    capeHdTotalUs += us; capeHdFrames++; capeHdMaxUs = (std::max)(capeHdMaxUs, us);
    const uint64_t now = GetTickCount64();
    if (!capeHdStatAt) capeHdStatAt = now;
    if (now - capeHdStatAt >= 5000) {
        MyClientLog::Get().info("Cape HD spike: draw time avg={:.1f} us max={:.1f} us over {} frames", capeHdTotalUs / double(capeHdFrames),
                                capeHdMaxUs, capeHdFrames);
        capeHdStatAt = now; capeHdFrames = 0; capeHdTotalUs = 0; capeHdMaxUs = 0;
    }
}
void InstallCapeHdSpike()
{
    auto& log = MyClientLog::Get();
    const auto file = CapeHdSpikeFile();
    std::ifstream in(file);
    if (file.empty() || !in) return;
    unsigned cape = 2;
    std::string word;
    if (in >> cape) { if (cape < 1 || cape > 6) cape = 2; } else cape = 2;
    in.clear();
    if (in >> word) capeHdExt = word == "ext";
    capeHdCape = cape;
    capeHdBegin = MyClientGame::ResolveFunction<CapeHdBeginFn>(CapeHdBeginSymbol);
    capeHdVertex = MyClientGame::ResolveFunction<CapeHdVertexFn>(CapeHdVertexSymbol);
    capeHdColor = MyClientGame::ResolveFunction<CapeHdColorFn>(TessColorFloatSymbol);
    if (!schemTessEnd) schemTessEnd = MyClientGame::ResolveFunction<SchemTessEndFn>(SchemTessEndSymbol);
    if (!schemRenderMesh) schemRenderMesh = MyClientGame::ResolveFunction<SchemRenderMeshFn>(SchemRenderMeshSymbol);
    const bool scene = EnsureSceneRenderHook();
    log.info("Cape HD spike: cape {} ({}) begin={} vertex={} color={} end={} renderMesh={} sceneHook={}", cape, capeHdExt ? "with .png" : "no extension",
             capeHdBegin != nullptr, capeHdVertex != nullptr, capeHdColor != nullptr, schemTessEnd != nullptr, schemRenderMesh != nullptr, scene);
    if (!capeHdBegin || !capeHdVertex || !schemTessEnd || !schemRenderMesh || !scene) { CapeHdStop("a function is missing on this game build"); return; }
    auto picture = (file.parent_path() / L"capes" / L"hd" / (std::to_wstring(cape) + (capeHdExt ? L".png" : L""))).wstring();
    capeHdLocation = new CapeHdLocation{};
    capeHdLocation->path = WideUtf8(picture);
    capeHdLocation->pathHash = CapeHdHash(capeHdLocation->path);
    capeHdLocation->fullHash = capeHdLocation->pathHash ^ 2u;
    capeHdSpikeWanted.store(true);
}

// One packet into the session file (network threads; takes replayMutex). A session starts at the server's
// first packet of a connection (NetworkSettings), or at StartGame when that was not seen (local worlds).
// While recording, it runs on through server transfers.
// Combo Counter: one finished packet from the Replay hooks. Never changes or delays the packet.
void ComboPacket(const std::string& data, bool sent)
{
    if (data.empty() || !active.load(std::memory_order_relaxed)) return;
    const auto p = sent ? MyClientCombo::ParseSent(data.data(), data.size()) : MyClientCombo::ParseReceived(data.data(), data.size());
    using Kind = MyClientCombo::Packet::Kind;
    if (p.kind == Kind::Other) return;
    const uint64_t now = GetTickCount64();
    bool logHurt = false, logHit = false;
    uint32_t combo = 0;
    {
        std::lock_guard g(comboMutex);
        switch (p.kind) {
        case Kind::Start: comboLocalId = p.runtimeId; comboCounter.Reset(); break;
        case Kind::Attack: ++comboAttackPackets; comboCounter.Attack(p.runtimeId, now); break;
        case Kind::Motion: if (comboLocalId && p.runtimeId == comboLocalId) comboCounter.Motion(now); break;
        case Kind::Hurt:
            if (comboLocalId && p.runtimeId == comboLocalId) {
                comboCounter.Hurt(p.runtimeId, now, true);
                logHurt = comboHurtLogs < 12 && ++comboHurtLogs;
            } else {
                const size_t before = comboCounter.Pending();
                comboCounter.Hurt(p.runtimeId, now, false);
                logHit = comboCounter.Pending() < before && comboHitLogs < 5 && ++comboHitLogs;
                combo = comboCounter.Value(now);
            }
            break;
        default: break;
        }
    }
    // Design §3a / §7: the data value of your own Hurt events tells whether the server marks the cause.
    if (logHurt) MyClientLog::Get().info("Combo Counter: you were hurt, ActorEvent data={}", p.data);
    if (logHit) MyClientLog::Get().info("Combo Counter: hit confirmed by the server's Hurt event (combo {})", combo);
}

void RecordPacket(const std::string& data, bool sent)
{
    if (data.empty() || !active.load(std::memory_order_relaxed)) return;
    const uint32_t id = MyClientReplay::PacketId(data.data(), data.size());
    if (sent && id == MyClientReplay::Ids::Login) return;   // carries the account's sign-in tokens: never stored
    const bool connectionStart = !sent && id == MyClientReplay::Ids::NetworkSettings;
    const bool startGame = !sent && id == MyClientReplay::Ids::StartGame;
    if (!connectionStart && !startGame && !replayOpen.load(std::memory_order_relaxed)) return;
    try {
        std::lock_guard guard(replayMutex);
        const uint64_t now = GetTickCount64();
        if (!replayMarked && (connectionStart || (startGame && (!replayWriter.IsOpen() || replaySawStartGame)))) {
            // A new connection / world: the buffer starts over.
            EndReplaySession(false);
            if (replayBufferAllowed.load() || replayWanted.load()) OpenReplaySession(now, replayWanted.load());
        }
        if (startGame) replaySawStartGame = true;
        if (replayWriter.IsOpen()) {
            if (!replayWriter.Append(data.data(), data.size(), now, sent))
                replayOpen.store(false);   // full or write error: the frame hook closes the file
            else if (!replayMarked && replayWriter.Bytes() > kReplayBufferMax)
                EndReplaySession(false);   // a very long session nobody recorded: drop the buffer
        }
    } catch (...) {}
}

// Replay: BatchedNetworkPeer::_receivePacket hands the game ONE finished (decrypted, decompressed, unbatched)
// packet per call, and sendPacket takes one the client sends. Both are copied into the session file;
// what the game sends and receives is never changed.
MYCLIENT_TYPE_INSTANCE_HOOK(ReplayReceiveHook, ll::memory::HookPriority::Normal, BatchedNetworkPeer,
                      ll::memory::SymbolView{MyClientGame::Symbols::ReceivePacket}, NetworkPeer::DataStatus,
                      std::string& outData,
                      std::shared_ptr<std::chrono::steady_clock::time_point> const& timepointPtr)
{
    const auto status = origin(outData, timepointPtr);
    if (status == NetworkPeer::DataStatus::HasData) {
        MyClientProfile::Scope prof(MyClientProfile::Replay);
        try { ComboPacket(outData, false); } catch (...) {}
        RecordPacket(outData, false);
    }
    return status;
}

MYCLIENT_TYPE_INSTANCE_HOOK(ReplaySendHook, ll::memory::HookPriority::Normal, BatchedNetworkPeer,
                      ll::memory::SymbolView{MyClientGame::Symbols::SendPacket}, void,
                      std::string const& data, NetworkPeer::Reliability reliability, Compressibility compressible)
{
    { MyClientProfile::Scope prof(MyClientProfile::Replay); try { ComboPacket(data, true); } catch (...) {} RecordPacket(data, true); }
    origin(data, reliability, compressible);
}

// ---- Replay playback: joining the HUD's local replay server ----
// The HUD used to open minecraft://connect/?serverUrl=127.0.0.1&serverPort=N, which did not take the game to the
// replay (2026-11-04 user report). Now it asks this DLL (ChatChannel.h, MyClient.ReplayJoin.v1.<pid>) and the game
// joins from inside: GameArguments::_deeplinkToServer is what that link runs, called here without its confirmation.
// From a world the game leaves it first (ClientInstance::requestLeaveGame) and joins once it is on the menus.
// Everything runs in GameArguments::tick (the game's main thread); the frame hook only times out a request.
class ReplayGameArguments {};   // GameArguments: `this` is only passed through
MyClientChat::Channel replayJoinChannel;
std::mutex replayJoinMutex;
struct PendingReplayJoin {
    uint32_t serial = 0;
    std::string host;
    uint16_t port = 0;
    uint64_t since = 0, leftAt = 0;
    bool leaving = false;
};
PendingReplayJoin replayJoin;
bool gameArgsTickHooked = false;
std::atomic<uint64_t> gameArgsTickAt{0};   // when GameArguments::tick last ran

using DeeplinkFn = void (*)(void*, std::string const&, int, bool);
DeeplinkFn DeeplinkFunction()
{
    static const auto fn = reinterpret_cast<DeeplinkFn>(MyClientGame::ResolveFirst({
        "?_deeplinkToServer@GameArguments@@AEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@H_N@Z",
        "?_deeplinkToServer@GameArguments@@IEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@H_N@Z",
        "?_deeplinkToServer@GameArguments@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@H_N@Z"}));
    return fn;
}

void FinishReplayJoin(MyClientChat::Result result, const char* why)
{
    replayJoinChannel.Finish(GetCurrentProcessId(), replayJoin.serial, result);
    MyClientLog::Get().info("Replay: joining {}:{} - {}", replayJoin.host, replayJoin.port, why);
    replayJoin = {};
}

// `gameArgs` = GameArguments (from its tick), or nullptr from the frame hook (timeouts only).
void TickReplayJoin(void* gameArgs)
{
    std::lock_guard guard(replayJoinMutex);
    const DWORD pid = GetCurrentProcessId();
    const uint64_t now = GetTickCount64();
    if (!replayJoinChannel.IsOpen()) {
        static uint64_t lastTry = 0;
        if (now - lastTry < 2000) return;
        lastTry = now;
        if (!replayJoinChannel.Open(pid, true, MyClientChat::ReplayJoinName)) return;
    }
    if (!replayJoin.serial) {
        std::string line;
        uint32_t serial = 0;
        if (!replayJoinChannel.TakePending(pid, line, serial)) return;
        replayJoin.serial = serial;
        replayJoin.since = now;
        if (!MyClientChat::ParseHostPort(line, replayJoin.host, replayJoin.port)) {
            replayJoin.host = line;
            FinishReplayJoin(MyClientChat::Result::Failed, "not an address");
            return;
        }
        MyClientLog::Get().info("Replay: the HUD asked to join its replay server at {}:{} (GameArguments tick hook={}, _deeplinkToServer={})",
                                                        replayJoin.host, replayJoin.port, gameArgsTickHooked, DeeplinkFunction() != nullptr);
    }
    if (!DeeplinkFunction() || !gameArgsTickHooked) { FinishReplayJoin(MyClientChat::Result::Failed, "this game build cannot be asked (the HUD opens the link instead)"); return; }
    if (now - replayJoin.since > 25000) { FinishReplayJoin(MyClientChat::Result::Failed, replayJoin.leaving ? "the world was not left within 25 s" : "GameArguments::tick did not run"); return; }
    if (!gameArgs) {
        // The tick is expected every frame; if it does not run, the HUD should not wait the whole 25 s.
        if (now - replayJoin.since > 3000 && now - gameArgsTickAt.load() > 3000)
            FinishReplayJoin(MyClientChat::Result::Failed, "GameArguments::tick does not run (the HUD opens the link instead)");
        return;
    }
    try {
        const auto client = MyClientGame::Client();
        if (!client) return;
        if (client->getLocalPlayer()) {
            if (!replayJoin.leaving) {
                replayJoin.leaving = true;
                MyClientLog::Get().info("Replay: leaving the world first");
                client->requestLeaveGame(true, false);
            }
            replayJoin.leftAt = 0;
            return;
        }
        // Left (or never in one): a moment on the menus so the old connection is fully gone.
        if (replayJoin.leaving) {
            if (!replayJoin.leftAt) replayJoin.leftAt = now;
            if (now - replayJoin.leftAt < 1500) return;
        }
        DeeplinkFunction()(gameArgs, replayJoin.host, replayJoin.port, false);
        FinishReplayJoin(MyClientChat::Result::Sent, replayJoin.leaving ? "asked the game to connect (after leaving the world)" : "asked the game to connect");
    } catch (...) {
        FinishReplayJoin(MyClientChat::Result::Failed, "the game threw");
    }
}

#define MYCLIENT_GAMEARGS_TICK_HOOK(NAME, SYMBOL)                                                               \
    MYCLIENT_TYPE_INSTANCE_HOOK(NAME, ll::memory::HookPriority::Normal, ReplayGameArguments,                          \
                          ll::memory::SymbolView{SYMBOL}, void)                                                  \
    {                                                                                                            \
        origin();                                                                                                \
        gameArgsTickAt.store(GetTickCount64(), std::memory_order_relaxed);                                       \
        if (active.load(std::memory_order_relaxed)) TickReplayJoin(this);                                        \
    }
MYCLIENT_GAMEARGS_TICK_HOOK(GameArgsTickPublicHook, "?tick@GameArguments@@UEAAXXZ")
MYCLIENT_GAMEARGS_TICK_HOOK(GameArgsTickProtectedHook, "?tick@GameArguments@@MEAAXXZ")
MYCLIENT_GAMEARGS_TICK_HOOK(GameArgsTickPrivateHook, "?tick@GameArguments@@EEAAXXZ")

// Starts / stops the recording from the per-frame hook (the channel is locked by the caller).
void UpdateReplay(bool wanted, MyClientFeatures::Shared& shared, uint64_t now)
{
    std::lock_guard guard(replayMutex);
    if (wanted && !replayMarked) {
        if (replayWriter.IsOpen() && replayOpen.load())
            MarkReplay(now);                 // switched on mid-game: everything since joining is kept
        else if (!replayWriter.IsOpen() && replayHooked && now - replayStartedAt > 2000)
            OpenReplaySession(now, true);    // no buffer (option off, or joined before this DLL): from now on
    }
    if (replayMarked && (!wanted || !replayOpen.load()))
        EndReplaySession(true);
    else if (replayWriter.IsOpen() && !replayMarked && (!replayBufferAllowed.load() || !replayOpen.load()))
        EndReplaySession(false);
    auto& t = shared.telemetry;
    if (replayWriter.IsOpen() && replayMarked) {
        // To disk about once a second (every frame was needless disk work); the 1 MiB buffer holds the rest.
        static uint64_t lastFlush = 0;
        if (now - lastFlush >= 1000) { lastFlush = now; replayWriter.Flush(); }
        t.recAt = now; t.recBytes = replayWriter.Bytes(); t.recPackets = replayWriter.Packets();
        t.recSeconds = static_cast<uint32_t>((now - replayMarkedAt) / 1000);
        std::strncpy(shared.recPath, replayPathUtf8.c_str(), sizeof(shared.recPath) - 1);
        shared.recPath[sizeof(shared.recPath) - 1] = 0;
    } else {
        t.recAt = 0;
        shared.recPath[0] = 0;
    }
}

MYCLIENT_TYPE_INSTANCE_HOOK(NickHook, ll::memory::HookPriority::Normal, Actor,
                      ll::memory::SymbolView{NickSymbol}, std::string const&)
{
    std::string const& original = origin();
    MyClientProfile::Scope prof(MyClientProfile::Nick);
    if (!nickWanted.load(std::memory_order_relaxed) || !active.load(std::memory_order_relaxed)) return original;
    try {
        const auto client = MyClientGame::Client();
        const Actor* local = client ? static_cast<const Actor*>(client->getLocalPlayer()) : nullptr;
        if (local && local == static_cast<const Actor*>(this) && !nickName.empty()) {
            localTagColor.store(MyClientAutoRq::NameColor(original));   // Auto RQ still needs the team colour
            { std::lock_guard g(realNameMutex); localRealName = original; }
            return nickName;
        }
    } catch (...) {}
    return original;
}

// Mangled from the SDK declarations (virtual public member, MSVC x64). NOT yet confirmed
// against the game binary: if either name is absent in a game build, that hook fails to
// install and Full Bright is reported UNAVAILABLE for it (nothing else is affected).
constexpr const char* BuildImageSymbol =
    "?buildImage@BaseLightTextureImageBuilder@@UEAA_NAEBVBaseLightData@@PEAUImage@mce@@IMM_N@Z";
constexpr const char* BuildImageSplitSymbol =
    "?buildImageSplit@BaseLightTextureImageBuilder@@UEAA_NAEBVBaseLightData@@PEAUImage@mce@@IM_N@Z";

// Returns true (and fills the saved values) when this call should see night vision.
bool BeginFullbright(BaseLightData& light, bool& savedActive, float& savedScale)
{
    MyClientProfile::Scope prof(MyClientProfile::Light);
    if (!active.load()) return false;
    std::lock_guard guard(featureMutex);
    if (!active.load() || !channel.Data() || !channel.Lock()) return false;
    if (!MyClientFeatures::Valid(*channel.Data(), GetCurrentProcessId())) ResetChannel();
    const auto now = GetTickCount64();
    const bool wanted = MyClientFeatures::WantsFullbright(channel.Data()->command, now);
    channel.Data()->telemetry.fullbrightAt = now;
    channel.Data()->telemetry.fullbrightActive = MyClientFeatures::WithFlag(
        channel.Data()->telemetry.fullbrightActive, MyClientFeatures::FlagFullbright, wanted);
    channel.Unlock();
    if (!wanted) return false;
    savedActive = light.mNightvisionActive;
    savedScale = light.mNightvisionScale;
    light.mNightvisionScale = MyClientFeatures::FullbrightScale(savedScale, true);
    light.mNightvisionActive = true;
    return true;
}

MYCLIENT_TYPE_INSTANCE_HOOK(LightHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
                      ll::memory::SymbolView{BuildImageSymbol}, bool,
                      BaseLightData const& lightData, mce::Image* image, uint length, float a,
                      float ambientBoost, bool clampToMinimum)
{
    statLightBuild.fetch_add(1, std::memory_order_relaxed);
    auto& light = const_cast<BaseLightData&>(lightData);
    bool savedActive = false; float savedScale = 0.f;
    const bool applied = BeginFullbright(light, savedActive, savedScale);
    const bool result = origin(lightData, image, length, a, ambientBoost, clampToMinimum);
    if (applied) { light.mNightvisionActive = savedActive; light.mNightvisionScale = savedScale; }
    return result;
}

MYCLIENT_TYPE_INSTANCE_HOOK(LightSplitHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
                      ll::memory::SymbolView{BuildImageSplitSymbol}, bool,
                      BaseLightData const& lightData, mce::Image* image, uint length, float a,
                      bool clampToMinimum)
{
    statLightSplit.fetch_add(1, std::memory_order_relaxed);
    auto& light = const_cast<BaseLightData&>(lightData);
    bool savedActive = false; float savedScale = 0.f;
    const bool applied = BeginFullbright(light, savedActive, savedScale);
    const bool result = origin(lightData, image, length, a, clampToMinimum);
    if (applied) { light.mNightvisionActive = savedActive; light.mNightvisionScale = savedScale; }
    return result;
}

// The game builds a fresh BaseLightData for the light map and only rebuilds the map when that data
// changed. Boosting the data HERE (and leaving it boosted) makes the game see "night vision" as
// part of the light state, so it rebuilds the light map by itself when the switch is toggled.
// The two buildImage hooks above only change values while a map is being built, which can be rare.
// std::unique_ptr<BaseLightData> is returned by value (hidden return pointer), hence "?AV?$unique_ptr...".
constexpr const char* LightCreateSymbol =
    "?createBaseLightTextureData@BaseLightTextureImageBuilder@@UEAA?AV?$unique_ptr@VBaseLightData@@U?$default_delete@VBaseLightData@@@std@@@std@@PEAVIClientInstance@@AEBVBaseLightData@@@Z";

MYCLIENT_TYPE_INSTANCE_HOOK(LightCreateHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
                      ll::memory::SymbolView{LightCreateSymbol}, std::unique_ptr<BaseLightData>,
                      IClientInstance* client, BaseLightData const& currentData)
{
    statLightCreate.fetch_add(1, std::memory_order_relaxed);
    auto created = origin(client, currentData);
    if (created) {
        bool savedActive = false; float savedScale = 0.f;
        BeginFullbright(*created, savedActive, savedScale);   // intentionally NOT restored
    }
    return created;
}
}

namespace MyClientGame {
bool StartDrawing(bool probe)
{
    MyClientCompositor::Host host{};
    host.info = [](const std::string& text) { MyClientLog::Get().info("{}", text); };
    host.warn = [](const std::string& text) { MyClientLog::Get().warn("{}", text); };
    host.hook = [](void* target, void* detour, void** original) {
#if MYCLIENT_OWN_HOOKS
        return MyClientHook::Install(target, detour, original);
#else
        return ll::memory::hook(target, detour, original, ll::memory::HookPriority::Normal);
#endif
    };
    return MyClientCompositor::Start(host, probe);   // hooks once; later calls return the state
}
bool EnableFeatures()
{
    // Hooks remain installed until process exit. Disabled detours call origin;
    // the DLL cannot be dynamically unloaded, avoiding in-flight trampolines.
    auto& log = MyClientLog::Get();
    if (!sourcesInitAttempted) {
        sourcesInitAttempted = true;
        const bool hooked = MyClientGame::Target(SourcesInitSymbol) && SourcesInitHook::hook() == 0;
        log.info("Texture packs: repository source initialization hook={}", hooked);
    }
    // Every game hook is installed on its own and its result is written to the game log, so one
    // missing symbol no longer switches every other feature off (and the log says which one failed).
    if (!damageValue) damageValue = MyClientGame::ResolveFunction<DamageFn>("?getDamageValue@ItemStackBase@@QEBAFXZ");
    if (!damageValue) log.warn("Armor durability unavailable: symbol ?getDamageValue@ItemStackBase@@QEBAFXZ was not found.");
    // Manual registration checks errors; the macro's automatic-use counter is unused here.
    (void)FrameHook::_AutoHookCount; (void)FovHook::_AutoHookCount; (void)LightHook::_AutoHookCount; (void)LightSplitHook::_AutoHookCount; (void)LightCreateHook::_AutoHookCount;
    (void)NameTagHook::_AutoHookCount;
    (void)NickHook::_AutoHookCount;
    (void)ReplayReceiveHook::_AutoHookCount; (void)ReplaySendHook::_AutoHookCount; (void)HitEffectHook::_AutoHookCount;
    (void)GameArgsTickPublicHook::_AutoHookCount; (void)GameArgsTickProtectedHook::_AutoHookCount; (void)GameArgsTickPrivateHook::_AutoHookCount;
    (void)LeftHandItemHook::_AutoHookCount; (void)LeftHandItemNewHook::_AutoHookCount;
    (void)LeftHandFirstPersonHook::_AutoHookCount; (void)LeftHandObjectHook::_AutoHookCount;
    (void)LeftHandMeshHook::_AutoHookCount;
    (void)PacksChangedHook::_AutoHookCount; (void)PacksChangedInnerHook::_AutoHookCount; (void)AtlasReloadedHook::_AutoHookCount;
    (void)AtlasUpdatedHook::_AutoHookCount;
    (void)ActorRenderHook::_AutoHookCount; (void)ActorRenderAtHook::_AutoHookCount;
    (void)HitSelectHook::_AutoHookCount; (void)HighlightHook::_AutoHookCount; (void)CameraHook::_AutoHookCount;
    (void)NameTagsHook::_AutoHookCount;
    (void)MouseFeedHook::_AutoHookCount;
    (void)OutlineHookPublic::_AutoHookCount; (void)OutlineHookPrivate::_AutoHookCount; (void)OutlineHookProtected::_AutoHookCount;
    (void)TessColorFloatHook::_AutoHookCount; (void)TessColorObjectHook::_AutoHookCount;
    (void)UiUpdateHook::_AutoHookCount; (void)UiRenderHook::_AutoHookCount; (void)HudTickHook::_AutoHookCount;
    (void)ItemUpdateHook::_AutoHookCount; (void)ItemRenderHook::_AutoHookCount;
    if (!renderHooked) {
        renderHooked = FrameHook::hook() == 0;
        if (!renderHooked) log.error("Per-frame hook failed. FPS, the Zoom lease and Nametag need it.");
    }
    if (!fovHooked) {
        fovHooked = FovHook::hook() == 0;
        if (!fovHooked) log.error("Zoom hook failed (LevelRendererPlayer::getFov).");
    }
    // Optional hooks: failure only makes that feature report UNAVAILABLE.
    if (!brightHooked) {
        // Either light-map path is enough to report Full Bright as available.
        const bool a = MyClientGame::Target(BuildImageSymbol) && LightHook::hook() == 0;
        const bool b = MyClientGame::Target(BuildImageSplitSymbol) && LightSplitHook::hook() == 0;
        const bool c = MyClientGame::Target(LightCreateSymbol) && LightCreateHook::hook() == 0;
        if (!c) log.warn("Full Bright: createBaseLightTextureData hook not found; only the buildImage hooks are used.");
        brightHooked = a || b || c;
        if (!brightHooked) log.error("Full Bright hook failed (BaseLightTextureImageBuilder::buildImage / buildImageSplit / createBaseLightTextureData).");
    }
    if (!cullHooked) {
        // Entity render distance: either overload is enough; the game may route through one or both.
        const bool a = MyClientGame::Target(ActorRenderSymbol) && ActorRenderHook::hook() == 0;
        const bool b = MyClientGame::Target(ActorRenderAtSymbol) && ActorRenderAtHook::hook() == 0;
        cullHooked = a || b;
        if (!cullHooked) log.warn("Entity render distance unavailable: ActorRenderDispatcher::render not found.");
    }
    if (!queueChunkHooked && !blockActorHooked && !skyHooked) {
        // Optimization rows on render hooks (once): each can be missing on its own.
        queueChunkHooked = MyClientGame::Target(QueueChunkSymbol) && QueueChunkHook::hook() == 0;
        blockActorHooked = MyClientGame::Target(BlockActorRenderAtSymbol) && BlockActorRenderHook::hook() == 0;
        skyHooked = MyClientGame::Target(RenderSkySymbol) && RenderSkyHook::hook() == 0;
        log.info("Optimization render hooks: vertical distance (queueChunk)={} block entity distance={} no sky={} "
                 "(behind-camera culling uses the entity hook={})", queueChunkHooked, blockActorHooked, skyHooked, cullHooked);
    }
    if (!replayHooked) {
        replayHooked = ReplayReceiveHook::hook() == 0;
        if (!replayHooked) log.warn("Replay hook failed (BatchedNetworkPeer::_receivePacket); recording unavailable.");
        else RecoverReplayParts();
    }
    if (!hitHooked) {
        hitHooked = HitEffectHook::hook() == 0;
        if (!hitHooked) log.warn("Hit Effect hook failed (GameMode::attack); Hit Effect will not show particles.");
    }
    if (!leftHandHooked) {
        // Either path is enough; the game may draw the held item through one or both.
        const bool a = MyClientGame::Target(RenderItemSymbol) && LeftHandItemHook::hook() == 0;
        const bool b = MyClientGame::Target(RenderItemNewSymbol) && LeftHandItemNewHook::hook() == 0;
        const bool object = MyClientGame::Target(RenderObjectSymbol) && LeftHandObjectHook::hook() == 0;
        // Without renderFirstPerson the first-person pass cannot be told apart (the flags never mark it).
        const bool pass = MyClientGame::Target(RenderFirstPersonSymbol) && LeftHandFirstPersonHook::hook() == 0;
        const bool mesh = RenderMeshTarget() && LeftHandMeshHook::hook() == 0;   // where the mirror is made (MirrorMesh)
        const bool inHand = ItemInHandPassSymbol() && ItemInHandPassHook::hook() == 0;   // the whole pass (projection)
        if (!inHand) log.warn("Left Hand: InGamePlayScreen::_renderItemInHand could not be hooked.");
        if (!mesh) log.warn("Left Hand: mce::Mesh::_renderMesh could not be hooked; held items stay on the right.");
        leftHandHooked = ((a || b || object) && mesh) || inHand;
        if (!leftHandHooked) log.warn("Left Hand: ItemInHandRenderer::renderItem / renderItemNew / renderObject not found; held items stay on the right.");
        if (!pass) log.warn("Left Hand: ItemInHandRenderer::renderFirstPerson not found; the first-person item may not be found.");
        log.info("Left Hand hooks: renderItem={} renderItemNew={} renderObject={} renderFirstPerson={} renderMesh={} renderItemInHand={}",
                 a, b, object, pass, mesh, inHand);
    }
    static bool schematicSpikeTried = false;
    if (!schematicSpikeTried) { schematicSpikeTried = true; InstallSchematicSpike(); InstallCapeHdSpike(); }
    if (!hudCostHooked) {
        // Measurement only (Perf line); each part is optional and the log says which ones are measured.
        const bool update = UiUpdateSymbol() && UiUpdateHook::hook() == 0;
        const bool render = UiRenderSymbol() && UiRenderHook::hook() == 0;
        const bool tick = HudTickSymbol() && HudTickHook::hook() == 0;
        const bool itemUpdate = ItemUpdateSymbol() && ItemUpdateHook::hook() == 0;
        const bool itemRender = ItemRenderSymbol() && ItemRenderHook::hook() == 0;
        hudCostHooked = true;   // install once: a part that is missing now is missing for this game build
        log.info("HUD cost measurement: uiUpdate={} uiRender={} hudTick={} itemUpdate={} itemRender={}", update, render, tick,
                 itemUpdate, itemRender);
    }
    if (replayHooked && !replaySendHooked) {
        replaySendHooked = ReplaySendHook::hook() == 0;
        if (!replaySendHooked) log.warn("Replay send hook failed (BatchedNetworkPeer::sendPacket); recordings will not include your own movement.");
    }
    if (!nickHooked) {
        nickHooked = MyClientGame::Target(NickSymbol) && NickHook::hook() == 0;
        if (!nickHooked) log.warn("Nick hook failed (Actor::getNameTag); Nick will not change the name.");
    }
    if (!nameTagsHooked) {
        nameTagsHooked = MyClientGame::Target("?extractNameTags@LevelRendererPlayer@@UEBA?AUNameTagRenderObjectCollection@@AEAVScreenContext@@@Z") && NameTagsHook::hook() == 0;
        if (!nameTagsHooked) log.warn("Nick: LevelRendererPlayer::extractNameTags not found; Nick can only change the old name path.");
    }
    if (!nametagHooked) {
        nametagHooked = MyClientGame::Target(NameTagSymbol) && NameTagHook::hook() == 0;
        if (!nametagHooked) log.error("Nametag hook failed (Actor::canShowNameTag).");
    }
    if (!outlineHooked) {
        // The member's access level (public/private/protected) changes its mangled name: try each.
        if (MyClientGame::Target(OutlineSymbolPublic) && OutlineHookPublic::hook() == 0) outlineHooked = true;
        else if (MyClientGame::Target(OutlineSymbolPrivate) && OutlineHookPrivate::hook() == 0) outlineHooked = true;
        else if (MyClientGame::Target(OutlineSymbolProtected) && OutlineHookProtected::hook() == 0) outlineHooked = true;
        hitSelectHooked = MyClientGame::Target(HitSelectSymbol) && HitSelectHook::hook() == 0;
        highlightHooked = MyClientGame::Target(HighlightSymbol) && HighlightHook::hook() == 0;
        if (!hitSelectHooked) log.warn("Block Outline: LevelRendererPlayer::renderHitSelect was not found.");
        if (!highlightHooked) log.warn("Block Outline: LevelRendererPlayer::_renderHighlightSelection was not found.");
        outlineHooked = outlineHooked || hitSelectHooked || highlightHooked;
        if (!outlineHooked) log.error("Block Outline hook failed: no selection-drawing function was found.");
    }
    if (!tessColorHooked) {
        const bool a = MyClientGame::Target(TessColorFloatSymbol) && TessColorFloatHook::hook() == 0;
        const bool b = MyClientGame::Target(TessColorObjectSymbol) && TessColorObjectHook::hook() == 0;
        tessColorHooked = a || b;
        if (!tessColorHooked) log.warn("Tessellator::color hooks were not found; Block Outline relies on the shader colour only.");
    }
    if (!textureReload) {
        // The access level (public / protected / private) is part of the mangled name and differs between
        // game builds; the first one this build has wins (resolve(true): a miss is not logged as fatal).
        textureReload = reinterpret_cast<TextureReloadFn>(MyClientGame::ResolveFirst({
            "?_loadLastGlobalResourcePacksFromFile@MinecraftGame@@AEAAX_N@Z",
            "?_loadLastGlobalResourcePacksFromFile@MinecraftGame@@IEAAX_N@Z",
            "?_loadLastGlobalResourcePacksFromFile@MinecraftGame@@QEAAX_N@Z"}));
        if (!textureReload) log.warn("Texture pack reload function (MinecraftGame::_loadLastGlobalResourcePacksFromFile) was not found.");
    }
    if (!focusLostHooked) {
        focusLostHooked = MyClientGame::Target("?onAppFocusLost@MinecraftGame@@UEAAXXZ") && FocusLostHook::hook() == 0;
        if (!focusLostHooked) log.warn("MinecraftGame::onAppFocusLost not found; opening the HUD menu shows the pause screen for a moment.");
    }
    if (!gameArgsTickHooked) {
        // Replay playback joins through GameArguments (TickReplayJoin); the virtual's access level is not known.
        if (MyClientGame::Target("?tick@GameArguments@@UEAAXXZ") && GameArgsTickPublicHook::hook() == 0) gameArgsTickHooked = true;
        else if (MyClientGame::Target("?tick@GameArguments@@MEAAXXZ") && GameArgsTickProtectedHook::hook() == 0) gameArgsTickHooked = true;
        else if (MyClientGame::Target("?tick@GameArguments@@EEAAXXZ") && GameArgsTickPrivateHook::hook() == 0) gameArgsTickHooked = true;
        log.info("Replay playback: GameArguments::tick hook={} _deeplinkToServer={} (without them the HUD opens the minecraft:// link)",
                 gameArgsTickHooked, DeeplinkFunction() != nullptr);
    }
    if (!mouseFeedHooked) {
        mouseFeedHooked = MyClientGame::Target(MouseFeedSymbol) && MouseFeedHook::hook() == 0;
        log.info("HUD menu: MouseDevice::feed hook {} (camera kept still behind the menu).", mouseFeedHooked ? "installed" : "NOT found");
    }
    if (textureReload && !packsChangedHooked) {
        static bool outer = false, inner = false;
        if (!outer) outer = MyClientGame::Target("?onActiveResourcePacksChanged@MinecraftGame@@UEAAXAEAVResourcePackManager@@@Z") &&
                            PacksChangedHook::hook() == 0;
        if (!inner) inner = PacksChangedInnerSymbol() && PacksChangedInnerHook::hook() == 0;
        static bool setStackHooked = false;
        if (!setStackHooked) {
            setStackHooked = MyClientGame::Target(SetStackSymbol) && SetStackHook::hook() == 0;
            log.info("Texture packs: server pack keeper (setStack) hooked={}", setStackHooked);
        }
        // Both are needed to tell "the game did not reload" apart from "it reloaded another way".
        packsChangedHooked = outer && inner;
        if (!packsChangedHooked) log.warn("Textures: MinecraftGame::onActiveResourcePacksChanged not found; a switch in a world may need a rejoin.");
    }
    if (textureReload && !atlasHooked) {
        const bool common = AtlasUpdatedSymbol() && AtlasUpdatedHook::hook() == 0;
        commonTerrainHooked = common;
        const bool legacy = AtlasReloadedSymbol() && AtlasReloadedHook::hook() == 0;
        atlasHooked = common || legacy;
        log.info("Texture packs: completion hooks commonTerrain={} legacyWrapper={}", common, legacy);
        log.info("Texture diag: block texture table reload watched={}", InstallBlockTexReloadDiag());
        if (!atlasHooked) log.warn("Textures: terrain completion hooks not found; chunks are rebuilt after a fixed wait.");
    }
    // In-game drawing of the HUD picture and the 3D lines (replaces the overlay window when it works).
    const bool composited = StartDrawing(true);
    static bool notedLive = false;
    if (!notedLive) {
        notedLive = true;
        // Nothing is hooked here (r42 did not patch the world render/prep/visibility callbacks: they go in when a
        // switch is first applied in a world): only say whether this game build is identified. See EnsureLiveTextureHooks.
        liveDrawingComposited = composited;
        liveTextureKnown = composited && textureReload && packsChangedHooked && atlasHooked &&
                           MyClientGame::RvaFallback(LiveInvalidSymbol) && MyClientGame::RvaFallback(LiveInGameSymbol) &&
                           MyClientGame::RvaFallback(LiveRenderSymbol) && MyClientGame::RvaFallback(LivePrepSymbol) &&
                           MyClientGame::RvaFallback(LiveVisibilitySymbol) && MyClientGame::RvaFallback(LoadsCompleteSymbol);
        log.info("Texture packs: in-world apply possible={} (a switch made in a world is applied there; otherwise at the title screen)", liveTextureKnown);
    }
    if (composited && !cameraHooked) {
        cameraHooked = MyClientGame::Target("?setupCamera@LevelRendererPlayer@@QEAAXAEAVCamera@mce@@M@Z") &&
                       CameraHook::hook() == 0;
        if (!cameraHooked) log.warn("In-game drawing: LevelRendererPlayer::setupCamera not found; lines use the estimated camera.");
    }
    log.info("In-game drawing: {} (game camera={})", composited ? "hooked" : "unavailable (the HUD keeps its window)", cameraHooked);
    log.info("Game hooks: frame={} fov={} fullbright={} nametag={} armor={} outline={} (hitSelect={} highlight={}) tessColor={} textures={} (packsChanged={} atlas={}) hit={} leftHand={}",
             renderHooked, fovHooked, brightHooked, nametagHooked, damageValue != nullptr, outlineHooked, hitSelectHooked,
             highlightHooked, tessColorHooked, textureReload != nullptr, packsChangedHooked, atlasHooked, hitHooked, leftHandHooked);
    if (!(renderHooked || fovHooked || brightHooked || nametagHooked || outlineHooked)) return false;
    std::lock_guard guard(featureMutex);
    if (!channel.Open(GetCurrentProcessId(), true)) return false;
    javaChatAvailable = MyClientJavaChat::Enable();
    javaChatChecked = true;
    channel.Data()->telemetry.fullbrightAvailable = AvailabilityBits();
    frames.Reset(); active.store(true); return true;
}
void DisableFeatures()
{
    MyClientJavaChat::Disable();
    active.store(false);
    {
        std::lock_guard guard(replayMutex);
        EndReplaySession(true);
    }
    nametagWanted.store(false);
    outlineWanted.store(false);
    std::lock_guard guard(featureMutex);
    if (channel.Data() && channel.Lock()) { ResetChannel(); channel.Unlock(); }
    channel.Close(); frames.Reset();
}
void ClearArmor()
{
    std::lock_guard guard(featureMutex);
    if (!active.load() || !channel.Data() || !channel.Lock()) return;
    if (!MyClientFeatures::Valid(*channel.Data(), GetCurrentProcessId())) ResetChannel();
    auto& data = channel.Data()->telemetry;
    data.armorAt = data.zoomAt = data.fullbrightAt = 0; data.zoomActive = data.fullbrightActive = 0;
    nametagWanted.store(false);
    outlineWanted.store(false);
    for (auto& slot : data.armor) slot = {};
    channel.Data()->command = {};
    channel.Data()->server = {};
    channel.Data()->clockAt = 0;   // Day Timer: no world, no clock
    channel.Data()->combo = 0; channel.Data()->comboFlags = 0; channel.Data()->comboAt = 0;
    channel.Data()->debug = {};
    channel.Data()->look = {};
    channel.Unlock();
}
uint32_t AutoRqMode() { return autoRqModeValue.load(); }

// Render Distance: sets the game's own view-distance option through the option registry, the same way
// a line of options.txt is read. The game's value before and after is logged so the result can be checked.
// r57-test31: loading "gfx_viewdistance:N" never changed the setting (4, 14, 44, 46 all stayed 7). The setting
// itself is used now: found once by its save tag, its range and lock logged, and set with IntOption::set.
IntOption* ViewDistanceOption(ClientInstance& client)
{
    static IntOption* found = nullptr;
    static bool looked = false;
    if (looked) return found;
    looked = true;
    auto& options = client.getOptions();
    for (int id = 0; id < 1024 && !found; ++id) {
        auto option = options.getIfValid(static_cast<OptionID>(id));
        if (!option || !*option || !(*option)->mImpl.get()) continue;
        if ((*option)->mImpl.get()->mSaveTag.get() == "gfx_viewdistance") found = static_cast<IntOption*>(*option);
    }
    if (found)
        MyClientLog::Get().info("Render Distance: setting found (value={} min={} max={} clamp={} choices={} lock={})",
                                found->mValue, found->VALUE_MIN, found->VALUE_MAX, found->mClampToRange,
                                found->mPossibleValues.get().size(), found->mImpl.get()->mLock.get() != nullptr ? "locked-capable" : "no lock");
    else MyClientLog::Get().warn("Render Distance: the gfx_viewdistance setting was not found");
    return found;
}
void ApplyRenderDistance()
{
    auto& log = MyClientLog::Get();
    if (renderDistanceRestoreWanted.exchange(false) && renderDistanceOriginal > 0) {
        try { if (const auto client = MyClientGame::Client()) {
            auto& options = client->getOptions();
            options.loadOptionsFromString(MyClientFeatures::RenderDistanceOption(static_cast<uint32_t>(renderDistanceOriginal)));
            if (renderDistanceOriginalValue >= 0)
                if (IntOption* option = ViewDistanceOption(*client)) static_cast<Option*>(option)->load(std::to_string(renderDistanceOriginalValue));
            renderDistanceOriginalValue = -1;
            client->updateChunkRadius();
            log.info("Render Distance: Optimization OFF; your own {} chunks put back (now {})", renderDistanceOriginal, options.getViewDistanceChunks());
        } } catch (...) {}
        renderDistanceOriginal = -1;
    }
    const uint32_t chunks = renderDistancePending.exchange(0);
    if (!chunks) return;
    try {
        const auto client = MyClientGame::Client();
        if (!client) return;
        auto& options = client->getOptions();
        const int before = options.getViewDistanceChunks();
        if (renderDistanceOriginal < 0) renderDistanceOriginal = before;
        log.info("Render Distance audit: command request={} chunks; optionBefore={} chunks (setting value, not measured radius)", chunks, before);
        options.loadOptionsFromString(MyClientFeatures::RenderDistanceOption(chunks));
        int after = options.getViewDistanceChunks();
        // r57-test30: 4 chunks was refused every time ("7 to 7"; earlier "5 to 5"): below the game's minimum, the
        // option keeps its old value. Step up to the smallest distance the game takes that is still nearer.
        if (after == before && static_cast<int>(chunks) != before) {
            if (IntOption* option = ViewDistanceOption(*client)) {
                // test32 log: the setting counts chunks (value=7 min=5 max=76); 4*16=64 made it 64 chunks.
                const int wanted = std::max(static_cast<int>(chunks), static_cast<int>(option->VALUE_MIN));
                // The setting may only take listed values: use the nearest listed one at or above the request.
                int value = wanted;
                const auto& choices = option->mPossibleValues.get();
                if (!choices.empty()) {
                    int best = -1;
                    for (int c : choices) if (c >= wanted && (best < 0 || c < best)) best = c;
                    if (best < 0) best = choices.back();
                    value = best;
                }
                if (renderDistanceOriginalValue < 0) renderDistanceOriginalValue = option->mValue;
                static_cast<Option*>(option)->load(std::to_string(value));   // virtual: no import needed
                after = options.getViewDistanceChunks();
                log.info("Render Distance: setting set to {} chunks (wanted {}); the game now says {} chunks (value={})",
                         value, wanted, after, option->mValue);
            }
        }
        for (int tryChunks = static_cast<int>(chunks) + 1; after == before && tryChunks < before; ++tryChunks) {
            options.loadOptionsFromString(MyClientFeatures::RenderDistanceOption(static_cast<uint32_t>(tryChunks)));
            after = options.getViewDistanceChunks();
            if (after != before) log.info("Render Distance: {} chunks refused; the game took {} chunks", chunks, after);
        }
        log.info("Render Distance: requested {} chunks; the game went from {} to {} chunks.", chunks, before, after);
        // 26.51 (r57-test8..11): the game's own setting did not move ("80 to 80") while the world was drawn much
        // farther than the setting said (5 chunks), and the frame rate fell until the player set the distance in the
        // game's settings again. When the setting does not take the new value, write the old one back so the game
        // is left as the player set it.
        if (after == before && static_cast<uint32_t>(before) != chunks) {
            options.loadOptionsFromString(MyClientFeatures::RenderDistanceOption(static_cast<uint32_t>(before)));
            log.warn("Render Distance: the game's setting did not change; requested restoration of {} chunks ({} blocks); observed {} chunks. World radius unverified.",
                     before, static_cast<int64_t>(before) * 16, options.getViewDistanceChunks());
        }
        // r57-test27 (D): the frame rate only rose after the player picked a distance in the game's settings again,
        // even the same one ("5 to 5"). Picking it makes the game recompute the client's chunk radius; loading the
        // option string does not. Ask for that recompute the same way and log it, so the next log shows whether
        // this is the missing step (and FPS before/after comes from the audit lines).
        try {
            client->updateChunkRadius();
            log.info("Render Distance: asked the game to recompute the chunk radius (ClientInstance::updateChunkRadius); option now {} chunks",
                     options.getViewDistanceChunks());
        } catch (...) { log.warn("Render Distance: chunk radius recompute failed"); }
    } catch (...) {
        log.warn("Render Distance: the game rejected the change ({} chunks).", chunks);
    }
}

// Hive draws your name tag in your team's colour. The name tag is read through Actor::getNameTag; while
// Nick replaces it, the Nick hook has already recorded the real tag's colour.
char LocalTeamColor()
{
    using NameTagFn = std::string const& (*)(Actor const*);   // reference return: same ABI as `this` first
    static const auto getNameTag = MyClientGame::ResolveFunction<NameTagFn>(NickSymbol);
    try {
        const auto client = MyClientGame::Client();
        const Actor* local = client ? static_cast<const Actor*>(client->getLocalPlayer()) : nullptr;
        if (!local || !getNameTag) return 0;
        std::string const& tag = getNameTag(local);
        if (nickWanted.load() && active.load()) return localTagColor.load();
        return MyClientAutoRq::NameColor(tag);
    } catch (...) { return 0; }
}
bool ShulkerHoverWanted() { return shulkerHoverWanted.load(); }
bool UncapFpsWanted() { return uncapFpsWanted.load(); }
uint32_t FpsLimitWanted() { return fpsLimitWanted.load(); }
bool TakeSkinRefresh()
{
    if (skinRefreshWanted.exchange(false)) return true;
    uint64_t at = skinRefreshAt.load();
    if (!at || GetTickCount64() < at) return false;
    return skinRefreshAt.compare_exchange_strong(at, 0);
}
uint32_t FrameGenMultiplier(uint32_t field) { return MyClientFeatures::FrameGenMultiplierOf(field, frameGenWide.load()); }
bool TextureReloadHeld() { return liveTextureBusy.load() || titleResourcesHeld.load(); }
bool TitleAvatarRefreshPending() { return titleAvatarRefreshWanted.load(); }
bool TakeTitleAvatarRefresh() { return titleAvatarRefreshWanted.exchange(false); }
uint64_t AvatarResourceEpoch() {
    return (uint64_t(statPacksChanged.load()) << 32) | terrainAtlasUpdates.load();
}
uint32_t FrameGenField(bool& paused)
{
    const uint32_t screens = gameScreenBits.load();
    // Only while playing: menus, loading and the title screen change the whole picture at once.
    // The HUD menu too: FocusLostHook keeps the game's pause screen away, so it no longer shows up as GameMenuOpen.
    const bool inWorld = (screens & MyClientFeatures::GameInWorld) != 0;
    const bool gameMenu = (screens & MyClientFeatures::GameMenuOpen) != 0;
    const bool clientMenu = hudMenuWanted.load();
    const bool reload = liveTextureBusy.load();
    paused = !inWorld || gameMenu || clientMenu || reload;
    const uint32_t field = active.load() ? frameGenField.load() : 0u;
    if (MyClientFeatures::FrameGenMultiplierOf(field, frameGenWide.load()) >= 2) {
        const uint32_t reason = (!inWorld ? 1u : 0u) | (gameMenu ? 2u : 0u) |
                                (clientMenu ? 4u : 0u) | (reload ? 8u : 0u);
        static std::atomic<uint32_t> lastReason{~0u};
        static std::atomic<uint64_t> lastNote{0};
        const uint64_t now = GetTickCount64();
        uint64_t before = lastNote.load();
        if (reason != lastReason.load() && now - before >= 1000 && lastNote.compare_exchange_strong(before, now)) {
            lastReason.store(reason);
            MyClientLog::Get().info("Frame Generation gate: paused={} inWorld={} gameMenu={} clientMenu={} resourceReload={}",
                                  paused, inWorld, gameMenu, clientMenu, reload);
        }
    }
    return field;
}
bool HitboxRequested() {
    const auto screens = gameScreenBits.load();
    return hitboxRequestedValue.load() && hitboxFilterValue.load() != 0 && !hudMenuWanted.load() &&
           !liveTextureBusy.load() && (screens & MyClientFeatures::GameInWorld) && !(screens & MyClientFeatures::GameMenuOpen);
}
void NoteHitboxActors(void* const* actors, const float* positions, uint32_t count)
{
    std::lock_guard g(hitboxActorsMutex);
    hitboxActors.clear();
    hitboxActorPositions.clear();
    if (!actors || !positions) return;
    for (uint32_t i = 0; i < count; i++) {
        hitboxActors.push_back(static_cast<Actor*>(actors[i]));
        hitboxActorPositions.push_back(Vec3{positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]});
    }
}
bool OutlineWanted() {
    const auto screens = gameScreenBits.load();
    return outlineWanted.load() && !hudMenuWanted.load() && !liveTextureBusy.load() &&
           (screens & MyClientFeatures::GameInWorld) && !(screens & MyClientFeatures::GameMenuOpen);
}
uint32_t OutlineRgb() { return outlineRgbValue.load() & 0xFFFFFFu; }
uint32_t HitboxFilter() { return hitboxFilterValue.load(); }
void HitboxColors(uint32_t out[4])
{
    if (!out) return;
    for (int k = 0; k < 4; k++) out[k] = hitboxRgbValue[k].load(std::memory_order_relaxed);
}
// Day Timer: the world's time of day, every game tick (the HUD counts down to night / morning from it).
// health: Shared.clockReserved (EncodeHealth, or 0 when unknown).
void SampleClock(int time, const MyClientFeatures::Debug& debug, const MyClientFeatures::Look& look, uint32_t health)
{
    if (!active.load()) return;
    std::lock_guard guard(featureMutex);
    if (!active.load() || !channel.Data() || !channel.Lock()) return;
    if (!MyClientFeatures::Valid(*channel.Data(), GetCurrentProcessId())) ResetChannel();
    channel.Data()->clockTime = time;
    channel.Data()->debug = debug;
    channel.Data()->look = look;
    channel.Data()->clockReserved = health;
    const uint64_t now = GetTickCount64();
    channel.Data()->clockAt = now;
    {
        std::lock_guard g(comboMutex);
        channel.Data()->combo = comboCounter.Value(now);
    }
    channel.Data()->comboFlags = (replayHooked && replaySendHooked) ? 1u : 0u;   // bit0: both packet hooks are in
    channel.Data()->comboAt = now;
    channel.Unlock();
}
void SampleArmor()
{
    if (!active.load()) return;
    MyClientFeatures::Armor armor[4]{};
    char items[4][32]{};
    const auto client = MyClientGame::Client();
    const auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) { ClearArmor(); return; }
    for (int i = 0; i < 4; ++i) {
        const ItemStack* armorStack = MyClientGame::ArmorOf(*player, i);
        if (!armorStack) { armor[i] = MyClientFeatures::MakeArmor(false, 0, 0); continue; }
        const auto& stack = *armorStack;
        if (!stack.mCount) armor[i] = MyClientFeatures::MakeArmor(false, 0, 0);
        else if (stack.mItem && damageValue) armor[i] = MyClientFeatures::MakeArmor(true, stack.mItem->getMaxDamage(), damageValue(&stack));
        else armor[i] = MyClientFeatures::MakeArmor(false, 0, 0);
        if (stack.mCount) {
            // Item id for the HUD's icon; "minecraft:" dropped, cut to fit (armor ids are short).
            std::string id = MyClientGame::TypeNameOf(stack);
            if (const auto colon = id.find(':'); colon != std::string::npos) id.erase(0, colon + 1);
            std::memcpy(items[i], id.data(), std::min<size_t>(id.size(), sizeof(items[i]) - 1));
        }
    }
    std::lock_guard guard(featureMutex);
    if (!active.load() || !channel.Data() || !channel.Lock()) return;
    if (!MyClientFeatures::Valid(*channel.Data(), GetCurrentProcessId())) ResetChannel();
    for (int i = 0; i < 4; ++i) channel.Data()->telemetry.armor[i] = armor[i];
    std::memcpy(channel.Data()->armorItems, items, sizeof(items));
    channel.Data()->telemetry.armorAt = GetTickCount64();
    channel.Unlock();
}
}

// Title screen version text (2026-11-04 user request: OriginX instead of LeviLamina's "v26.51/LeviLamina-26.51.6").
// LeviLamina rewrites the "#version" binding in its own ScreenController::bindString hook (Normal priority). This
// hook runs inside it (Low priority), so it receives LeviLamina's text and replaces it once more. It goes through
// LeviLamina's hook API like every other MyClient hook, so it follows whatever game version LeviLamina supports.
MYCLIENT_AUTO_TYPE_INSTANCE_HOOK(
    TitleVersionHook,
    ll::memory::HookPriority::Low,
    ScreenController,
    // By name: the import library has no ScreenController::bindString (only LeviLamina's own exports).
    ll::memory::SymbolView{"?bindString@ScreenController@@QEAAXAEBVStringHash@@V?$move_only_function@$$A8@@EBA?AV?$basic_string@"
                           "DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ@brstd@@V?$move_only_function@$$A8@@EBA_NXZ@4@@Z"},
    void,
    StringHash const&                                  bindingName,
    brstd::move_only_function<::std::string() const> callback,
    brstd::move_only_function<bool() const>          condition
) {
    static const StringHash versionHash("#version");
    if (bindingName == versionHash) {
        origin(
            bindingName,
            []() -> std::string {
                try {
                    const auto game = ll::getGameVersion();
                    // "OriginX 26.51" (user request 2026-11-18). The standalone DLL finds bindString through
                    // sigs.json (it is not exported); the old OriginX_HomeBrand pack is no longer written.
                    return "OriginX " + std::to_string(game.minor) + "." + std::to_string(game.patch);
                } catch (...) {
                    return "OriginX";
                }
            },
            []() -> bool { return true; }
        );
        return;
    }
    origin(bindingName, std::move(callback), std::move(condition));
}

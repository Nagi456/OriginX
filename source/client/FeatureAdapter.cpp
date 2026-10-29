#include "FeatureAdapter.h"
#include "FeatureChannel.h"
#include "ReplayFormat.h"
#include "Profile.h"
#include "Compositor.h"
#include "mc/resources/IResourcePackRepository.h"
#include "mc/resources/InvalidPack.h"
#include "mc/platform/UUID.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePackListener.h"
#include "mc/resources/ResourcePackStack.h"
#include "mc/client/game/IMinecraftGame.h"
#include "mc/client/gui/screens/models/IContentManager.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/network/BatchedNetworkPeer.h"
#include "GameSymbols.h"
#include "AutoRq.h"
#include "mc/network/NetworkPeer.h"
#include "PartyAdapter.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/level/Level.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/phys/AABB.h"
#include "mc/comprehensive/ParticleType.h"
#include "LitePack.h"
#include <random>
#include "mc/client/renderer/game/GameRenderer.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
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
#include <fstream>
#include <iterator>
#include <vector>
#include <new>
#include <cmath>
#include <cstring>
#include <string>
#include <memory>
#include <mutex>

// Game classes the HUD cost hooks only pass through (by reference).
class MinecraftUIFrameUpdateContext;
struct FrameRenderObject;
class MinecraftUIRenderContext;
class IClientInstance;
class UIControl;
class UIScene;

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
// What has to follow a reload while you are in a world (the game re-read the list, but blocks and animations
// kept their old look until the world was joined again). Counters come from hooks on the game's own reload steps.
std::atomic<uint32_t> statPacksChanged{0};
std::atomic<uint32_t> statPacksChangedInner{0};   // MinecraftGame::_onActiveResourcePacksChanged only     // MinecraftGame::onActiveResourcePacksChanged: the game reloads the packs
std::atomic<bool> atlasReloaded{false};        // MinecraftGame::_onTerrainAtlasReloaded: the block textures are new
bool packsChangedHooked = false, atlasHooked = false;
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
std::atomic<uint32_t> statCulled{0};
bool cullHooked = false;
std::atomic<bool> nickWanted{false};               // Nick on (bit 21 of command.reserved)
std::atomic<char> localTagColor{0};                // colour of the local player's REAL name tag, seen by the Nick hook
std::string nickName;                              // written only by the per-frame hook, read by the name hook (same thread)
std::atomic<uint64_t> renderedFrames{0};           // frames counted by the frame hook
std::atomic<uint32_t> statSetupCamera{0}, statCameraCaptures{0}, statNameTagLists{0};
float lastCameraPos[3] = {};                       // render thread: camera position of the last captured frame
std::mutex realNameMutex;
std::string localRealName;                         // the local player's own name tag text (before Nick), for the name tag list
std::atomic<bool> nametagWanted{false};            // refreshed once per frame from the HUD's command lease
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
        | (textureReload ? MyClientFeatures::FlagTextures : 0u);
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
        if (!MyClientGame::CurrentServer(serverHost, serverPort, serverTransport)) {
            serverHost.clear(); serverPort = 0; serverTransport = 1;
        }
    }
    std::lock_guard guard(featureMutex);
    if (!active.load()) return;
    const auto now = GetTickCount64();
    frames.Frame(now);
    if (!channel.Data() || !channel.Lock()) return;
    if (!MyClientFeatures::Valid(*channel.Data(), GetCurrentProcessId())) ResetChannel();
    auto& data = channel.Data()->telemetry;
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
    if (MyClientFeatures::TextureReloadRequested(channel.Data()->command, now, textureSeen, texturePrimed))
        textureReloadPending.store(true);
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
                ll::mod::NativeMod::current()->getLogger().warn("HUD menu: no command from the HUD for {} ms (still treated as open).", age);
            }
            if (age <= MyClientFeatures::CommandLeaseMs) staleLogged = false;
            hudMenuWanted.store(menuFresh && menuFlag);
        }
        frameGenField.store(fresh ? (field & 0xF8000000u) : 0u);
        // Render Distance: one request per change of the HUD's counter (never on the first sight of it).
        if (fresh) {
            const uint32_t counter = MyClientFeatures::RenderCounterOf(field);
            if (!renderDistancePrimed) { renderDistancePrimed = true; renderDistanceSeen = counter; }
            else if (counter != renderDistanceSeen) {
                renderDistanceSeen = counter;
                if (const uint32_t chunks = MyClientFeatures::RenderChunksOf(field)) renderDistancePending.store(chunks);
            }
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
        frameGenWide.store(fresh ? channel.Data()->frameGenMultiplier : 0u);
        const uint32_t playerRange = MyClientFeatures::PlayerDistanceBlocks(optimize);
        float range2 = 0.f, playerRange2 = 0.f;
        try {
            const auto client = ll::service::getClientInstance();
            auto* local = (range || playerRange) && client ? client->getLocalPlayer() : nullptr;
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
        cullRange2.store(range2);
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
using SetStackFn = bool (*)(ResourcePackManager*, std::unique_ptr<ResourcePackStack>*, ResourcePackStackType);
SetStackFn SetStackFunction()
{
    static const auto setStack = MyClientGame::ResolveFunction<SetStackFn>(
        "?setStack@ResourcePackManager@@QEAA_NV?$unique_ptr@VResourcePackStack@@U?$default_delete@VResourcePackStack@@@std@@@std@@W4ResourcePackStackType@@@Z");
    return setStack;
}
bool SetStackFound() { return SetStackFunction() != nullptr; }

void RestackGlobalPacks(IMinecraftGame& game, ll::io::Logger& log)
{
    ResourcePackManager& manager = game.getResourcePackManager();
    // MinecraftGame::_deserializeGlobalResourcePacks (IMinecraftGame is MinecraftGame's first base): the stack
    // comes back through the hidden return pointer, after `this`.
    using DeserializeFn = std::unique_ptr<ResourcePackStack>* (*)(IMinecraftGame*, std::unique_ptr<ResourcePackStack>*);
    static const auto deserialize = reinterpret_cast<DeserializeFn>(MyClientGame::ResolveFirst({
        "?_deserializeGlobalResourcePacks@MinecraftGame@@AEAA?AV?$unique_ptr@VResourcePackStack@@U?$default_delete@VResourcePackStack@@@std@@@std@@XZ",
        "?_deserializeGlobalResourcePacks@MinecraftGame@@IEAA?AV?$unique_ptr@VResourcePackStack@@U?$default_delete@VResourcePackStack@@@std@@@std@@XZ",
        "?_deserializeGlobalResourcePacks@MinecraftGame@@QEAA?AV?$unique_ptr@VResourcePackStack@@U?$default_delete@VResourcePackStack@@@std@@@std@@XZ"}));
    bool set = false;
    if (deserialize) {
        std::unique_ptr<ResourcePackStack> stack;
        deserialize(&game, &stack);
        // ResourcePackManager::setStack / _composeFullStack are not in the SDK's import library: found by name.
        // The stack goes by value, which the x64 ABI passes as a pointer to a copy the callee destroys, so it is
        // handed over in storage this side never destroys.
        const auto setStack = SetStackFunction();
        if (stack && setStack) {
            alignas(std::unique_ptr<ResourcePackStack>) unsigned char arg[sizeof(std::unique_ptr<ResourcePackStack>)];
            auto* owned = new (arg) std::unique_ptr<ResourcePackStack>(std::move(stack));
            set = setStack(&manager, owned, ResourcePackStackType::Global);
        }
    }
    using ComposeFn = void (*)(ResourcePackManager*);
    static const auto compose = reinterpret_cast<ComposeFn>(MyClientGame::ResolveFirst({
        "?_composeFullStack@ResourcePackManager@@AEAAXXZ", "?_composeFullStack@ResourcePackManager@@IEAAXXZ",
        "?_composeFullStack@ResourcePackManager@@QEAAXXZ"}));
    if (compose) compose(&manager);
    log.info("Texture packs in a world: deserialize={} setStack={} globalStackSet={} composed={}", deserialize != nullptr,
             SetStackFound(), set, compose != nullptr);
}

// A reload made while in a world, followed over the next frames.
struct TextureFollowUp {
    uint64_t at = 0;              // 0 = none
    uint32_t changesBefore = 0;   // statPacksChanged before the reload
    bool notified = false;        // NotifyPacksChanged was used
};
TextureFollowUp textureFollowUp;

// Chunks keep the block textures they were built with: build them again with the new ones.
bool RebuildChunks(ClientInstance& client)
{
    if (!client.getLocalPlayer()) return false;
    LevelRenderer* renderer = client.getLevelRenderer();
    if (!renderer) return false;
    renderer->rebuildAllRenderChunkGeometry();
    return true;
}

inline uintptr_t PacksChangedInnerSymbol();

void RunTextureFollowUp(ClientInstance& client, IMinecraftGame& game)
{
    auto& f = textureFollowUp;
    if (!f.at) return;
    auto& log = ll::mod::NativeMod::current()->getLogger();
    const uint64_t now = GetTickCount64();
    if (!client.getLocalPlayer()) { f = {}; return; }   // left the world
    if (now - f.at > 20000) {
        log.warn("Texture packs: the block textures were not reloaded in this world within 20 s (a rejoin applies them).");
        f = {};
        return;
    }
    // The game re-read the list but did not reload the packs (in a world it waited for the next join). With the
    // atlas hooked, "no new block textures yet" is the sign; a pack-change call alone did not mean the new
    // packs were in use (it could reload the old full stack).
    if (!f.notified && now - f.at >= 1500 && (atlasHooked || (packsChangedHooked && statPacksChanged.load() == f.changesBefore))) {
        f.notified = true;
        f.at = now;
        atlasReloaded.store(false);
        try { RestackGlobalPacks(game, log); } catch (...) { log.warn("Texture packs: re-stacking the global packs failed."); }
        const uint32_t outerBefore = statPacksChanged.load(), innerBefore = statPacksChangedInner.load();
        NotifyPacksChanged(game.getResourcePackManager());
        const bool told = statPacksChanged.load() != outerBefore, reloaded = statPacksChangedInner.load() != innerBefore;
        // 2026-10-28 log: told, but the block textures never came (no "chunks rebuilt" in 20 s). When the listener call
        // did not reach the step that reloads, that step is called directly.
        bool direct = false;
        if (!reloaded) {
            using InnerFn = void (*)(IMinecraftGame*, ResourcePackManager*, bool);
            static const auto inner = reinterpret_cast<InnerFn>(PacksChangedInnerSymbol());
            if (inner) {
                try { inner(&game, &game.getResourcePackManager(), true); direct = true; } catch (...) { direct = false; }
            }
        }
        log.info("Texture packs: the game did not reload them in this world; asked it to (listener {}, reload step {}{}).",
                 told ? "ran" : "did not run", reloaded ? "ran" : "did not run", direct ? ", called it directly" : "");
        return;
    }
    // Once the block textures are reloaded (or after a while when that step could not be hooked).
    const bool atlasDone = atlasReloaded.exchange(false);
    if (atlasDone || (!atlasHooked && now - f.at >= 4000)) {
        if (RebuildChunks(client)) log.info("Texture packs: chunks rebuilt with the new block textures.");
        // Animations and models are not reloaded here: MinecraftGame::reloadAnimationAndGeometryData in a world
        // left your own player without a model (invisible) until a rejoin.
        f = {};
    }
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

LL_TYPE_INSTANCE_HOOK(MouseFeedHook, ll::memory::HookPriority::Normal, MouseDevice,
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
        ll::mod::NativeMod::current()->getLogger().warn("HUD menu: could not hook the game window's input (error {}).", GetLastError());
        gameWndHooked = best.first;   // do not retry every second
        return;
    }
    gameWndOriginal = previous;
    gameWndHooked = best.first;
    wchar_t cls[64]{};
    GetClassNameW(best.first, cls, 64);
    char name[64]{};
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, name, sizeof name, nullptr, nullptr);
    ll::mod::NativeMod::current()->getLogger().info("HUD menu: game window input hooked ({}).", name);
}

// Opening the HUD menu takes the focus from the game, and the game answers a lost focus with its pause screen.
// While the HUD menu is open the lost focus is not passed on, so the menu opens straight over play.
// onAppFocusLost is an AppPlatformListener function of MinecraftGame; `this` is only passed through.
LL_TYPE_INSTANCE_HOOK(FocusLostHook, ll::memory::HookPriority::Normal, IMinecraftGame,
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
    ll::mod::NativeMod::current()->getLogger().info(
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
        const auto client = ll::service::getClientInstance();
        if (!client) { gameScreenBits.store(0); return; }
        const bool inWorld = client->getLocalPlayer() != nullptr;
        const std::string name = client->getTopScreenName();
        // For the HUD's in-world display: also no loading / saving / disconnect / title screen on top
        // (the player and level can outlive the world for a moment while it is left or joined).
        const bool playing = inWorld && ll::service::getMultiPlayerLevel() &&
                             name.find("progress") == std::string::npos && name.find("loading") == std::string::npos &&
                             name.find("start_screen") == std::string::npos && name.find("disconnect") == std::string::npos;
        // Playing = the HUD screen on top ("hud_screen"); anything else over it is a game menu.
        const bool menu = inWorld && client->isShowingMenu() && name.find("hud_screen") == std::string::npos;
        const bool pause = menu && name.find("pause") != std::string::npos;
        // The title screen itself (for the HUD's Replays button), not the Play / Settings screens opened from it.
        const bool title = !inWorld && name.find("start_screen") != std::string::npos;
        gameScreenBits.store(MyClientFeatures::GameScreenKnown | (menu ? MyClientFeatures::GameMenuOpen : 0u) |
                             (pause ? MyClientFeatures::GamePaused : 0u) | MyClientFeatures::GameWorldKnown |
                             (playing ? MyClientFeatures::GameInWorld : 0u) | MyClientFeatures::GameTitleKnown |
                             (title ? MyClientFeatures::GameOnTitle : 0u));
        if (name != lastName) {
            ll::mod::NativeMod::current()->getLogger().info("Game screen: {} (menu={})", name, menu);
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
            ll::mod::NativeMod::current()->getLogger().info("Closed the pause screen the HUD menu caused ({}).", name);
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
            ll::mod::NativeMod::current()->getLogger().info("Pause screen {} ms after the HUD menu closed ({}).", now - lastClose, name);
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
    auto& log = ll::mod::NativeMod::current()->getLogger();
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

// Runs after a frame has been rendered, on the game's render/main thread. IMinecraftGame is the first
// base of MinecraftGame, so the interface reference is also the MinecraftGame address.
void RunPendingTextureReload()
{
    // Once per game start, on the title screen a few seconds after the mods are enabled: re-read the
    // global pack list, as opening the game's resource pack screen and going back does. Global packs
    // were otherwise often missing after a start until that screen had been visited.
    static uint64_t firstSeen = 0;
    static bool startupDone = false;
    if (!startupDone && textureReload) {
        const uint64_t now = GetTickCount64();
        if (!firstSeen) firstSeen = now;
        if (now - firstSeen >= 4000) {
            try {
                const auto client = ll::service::getClientInstance();
                if (client && !client->getLocalPlayer()) {   // title screen (never in a world)
                    startupDone = true;
                    textureReloadPending.store(true);
                    ll::mod::NativeMod::current()->getLogger().info("Reloading the global resource packs once after start.");
                }
            } catch (...) { startupDone = true; }
        }
    }
    if (textureFollowUp.at) {
        try {
            const auto client = ll::service::getClientInstance();
            if (client) RunTextureFollowUp(*client, client->getMinecraftGame_DEPRECATED());
            else textureFollowUp = {};
        } catch (...) { textureFollowUp = {}; }
    }
    // First let the pack repository pick up packs written or changed since the game started (the
    // client pack is rewritten by the HUD). Without this the global list names packs the repository
    // does not know yet, the game drops the whole list and no global pack is applied at all.
    // The repository takes a moment to see a new pack folder: reloading right after refreshPacks still logged
    // "Configured pack (OriginX Lite) was not found and was ignored" (2026-10-28), so the reload waits 600 ms.
    static uint64_t reloadAt = 0;
    if (textureReloadPending.exchange(false) && textureReload) {
        try {
            const auto client = ll::service::getClientInstance();
            if (client) {
                // refreshPacks alone never picked up OriginX Lite ("Configured pack ... was not found" on every reload,
                // 2026-10-28): the user pack folders are re-read only on requestReloadUserPacks.
                auto& repository = client->getResourcePackRepository();
                try { repository.requestReloadUserPacks(); } catch (...) {}
                repository.refreshPacks();
                reloadAt = GetTickCount64() + 600;
            }
        } catch (...) {}
        return;
    }
    if (!reloadAt || GetTickCount64() < reloadAt || !textureReload) return;
    reloadAt = 0;
    try {
        const auto client = ll::service::getClientInstance();
        if (!client) return;
        client->getResourcePackRepository().refreshPacks();
        IMinecraftGame& game = client->getMinecraftGame_DEPRECATED();
        const uint32_t changesBefore = statPacksChanged.load();
        textureReload(&game, false);
        const auto count = statTextureReloads.fetch_add(1) + 1;
        ll::mod::NativeMod::current()->getLogger().info("Texture packs were reloaded (request #{}).", count);
        try { NoteLitePack(*client); } catch (...) {}
        try { RefreshPackScreens(game); } catch (...) {}
        if (client->getLocalPlayer()) {
            atlasReloaded.store(false);
            textureFollowUp = {GetTickCount64(), changesBefore, false};
        }
    } catch (...) {}
}

// The game's own reload steps, only counted (see RunTextureFollowUp). onActiveResourcePacksChanged is a
// ResourcePackListener function, so `this` is that base of MinecraftGame.
LL_TYPE_INSTANCE_HOOK(PacksChangedHook, ll::memory::HookPriority::Normal, ResourcePackListener,
                      ll::memory::SymbolView{"?onActiveResourcePacksChanged@MinecraftGame@@UEAAXAEAVResourcePackManager@@@Z"},
                      void, ResourcePackManager& manager)
{
    statPacksChanged.fetch_add(1);
    origin(manager);
}

// The step behind it (the game may also call this one directly). Either counts as "the game reloaded".
inline uintptr_t PacksChangedInnerSymbol()
{
    return MyClientGame::ResolveFirst({"?_onActiveResourcePacksChanged@MinecraftGame@@AEAAXAEAVResourcePackManager@@_N@Z",
                                       "?_onActiveResourcePacksChanged@MinecraftGame@@IEAAXAEAVResourcePackManager@@_N@Z",
                                       "?_onActiveResourcePacksChanged@MinecraftGame@@QEAAXAEAVResourcePackManager@@_N@Z"});
}

LL_TYPE_INSTANCE_HOOK(PacksChangedInnerHook, ll::memory::HookPriority::Normal, IMinecraftGame, PacksChangedInnerSymbol(),
                      void, ResourcePackManager& manager, bool fullVanillaPackOnStack)
{
    statPacksChanged.fetch_add(1);
    statPacksChangedInner.fetch_add(1);
    origin(manager, fullVanillaPackOnStack);
}

inline uintptr_t AtlasReloadedSymbol()
{
    return MyClientGame::ResolveFirst({"?_onTerrainAtlasReloaded@MinecraftGame@@AEAAXXZ",
                                       "?_onTerrainAtlasReloaded@MinecraftGame@@IEAAXXZ",
                                       "?_onTerrainAtlasReloaded@MinecraftGame@@QEAAXXZ"});
}

LL_TYPE_INSTANCE_HOOK(AtlasReloadedHook, ll::memory::HookPriority::Normal, IMinecraftGame, AtlasReloadedSymbol(), void)
{
    origin();
    atlasReloaded.store(true);
}

// Name tags: in this game the tags drawn above actors are built by LevelRendererPlayer::extractNameTags
// (the old per-actor hooks are hardly called). Nick replaces your real name in that list.
std::mutex nameTagSampleMutex;
std::string nameTagSample;   // a description of one tag of the last list, for the log
std::string NameTagSample() { std::lock_guard g(nameTagSampleMutex); return nameTagSample; }

LL_TYPE_INSTANCE_HOOK(NameTagsHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{"?extractNameTags@LevelRendererPlayer@@UEBA?AUNameTagRenderObjectCollection@@AEAVScreenContext@@@Z"},
                      NameTagRenderObjectCollection*, NameTagRenderObjectCollection* out, ScreenContext& screenContext)
{
    // Returned by value: under the MSVC ABI that is a hidden pointer to the caller's buffer (after `this`),
    // which is also returned. Taking it as such means this code never owns or destroys a collection
    // (NameTagRenderObject's destructor is not exported).
    NameTagRenderObjectCollection* filled = origin(out, screenContext);
    if (!filled) return filled;
    NameTagRenderObjectCollection& result = *filled;
    statNameTagLists.fetch_add(1, std::memory_order_relaxed);
    try {
        auto& tags = *result.mNameTags;
        // Your own name: the local player's name (Actor::getNameTag is inlined here, so the Nick hook that
        // used to record it is hardly ever reached); the name it recorded is only a fallback.
        std::string real;
        if (const auto client = ll::service::getClientInstance())
            if (auto* local = client->getLocalPlayer()) real = local->mName.get();
        if (real.empty()) { std::lock_guard g(realNameMutex); real = localRealName; }
        const bool nick = nickWanted.load(std::memory_order_relaxed) && active.load(std::memory_order_relaxed) && !nickName.empty();
        bool ownTag = false;
        const bool hideOthers = hideNameTagsWanted.load(std::memory_order_relaxed) && active.load(std::memory_order_relaxed);
        for (auto& tag : tags) {
            std::string& text = *tag.mNameTag;
            const size_t at = real.empty() ? std::string::npos : text.find(real);
            // No name tags (Optimization): every tag but your own is drawn at size 0 and fully transparent (the
            // list itself is left alone: removing entries would need NameTagRenderObject's destructor).
            if (hideOthers && at == std::string::npos) {
                tag.mScale = 0.f;
                tag.mTagColor->a = 0.f;
                tag.mTextColor->a = 0.f;
                continue;
            }
            if (at == std::string::npos) continue;
            ownTag = true;
            if (nick) {
                localTagColor.store(MyClientAutoRq::NameColor(text));   // Auto RQ still needs the team colour
                text.replace(at, real.size(), nickName);
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

void LocateOwnTagJump()
{
    if (ownTagSearched) return;
    ownTagSearched = true;
    auto& log = ll::mod::NativeMod::current()->getLogger();
    try {
        void* fn = ll::memory::SymbolView{"?extractNameTags@LevelRendererPlayer@@UEBA?AUNameTagRenderObjectCollection@@AEAVScreenContext@@@Z"}
                       .resolve(true);
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
            const auto client = ll::service::getClientInstance();
            // 0 = first person: your own tag would sit in front of the camera.
            want = client && client->getLocalPlayer() && client->getOptions().getPlayerViewPerspective() != 0;
        } catch (...) { want = false; }
    }
    if (want) LocateOwnTagJump();
    if (!ownTagJump || want == ownTagPatched) return;
    static const uint8_t nops[OwnTagJumpSize] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (WriteCode(ownTagJump, want ? nops : ownTagOriginal, OwnTagJumpSize)) ownTagPatched = want;
}

// Left Hand: your player and the camera view for this frame (see leftHandOwnPlayer).
void UpdateLeftHandView()
{
    const void* own = nullptr;
    bool firstPersonView = false;
    if (leftHandWanted.load(std::memory_order_relaxed) && active.load(std::memory_order_relaxed)) {
        try {
            const auto client = ll::service::getClientInstance();
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
void UpdateHitboxShifts(float alpha)
{
    if (!MyClientGame::HitboxRequested()) return;
    std::lock_guard g(hitboxActorsMutex);
    if (hitboxActors.empty() || hitboxActors.size() != hitboxActorPositions.size()) return;
    using LerpFn = Vec3 (Actor::*)(float) const;
    static const auto getLerp = MyClientGame::ResolveMember<LerpFn>(MyClientGame::Symbols::InterpolatedPosition);
    if (!getLerp) return;
    try {
        auto level = ll::service::getMultiPlayerLevel();
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

LL_TYPE_INSTANCE_HOOK(FrameHook, ll::memory::HookPriority::Normal, GameRenderer,
                      ll::memory::SymbolView{"?renderCurrentFrame@GameRenderer@@QEAAXM@Z"}, void, float alpha)
{
    // Before the frame: the Compositor may present it while the game is still inside renderCurrentFrame.
    UpdateOwnTag();   // also runs when inactive, so the original bytes come back
    UpdateLeftHandView();
    if (active.load()) UpdateHitboxShifts(alpha);
    { MyClientProfile::GameScope prof(MyClientProfile::GameFrame); origin(alpha); }
    if (!active.load()) return;
    {
        MyClientProfile::Scope prof(MyClientProfile::Frame);
        PublishFps();
        TrackGameScreens();
        RunPendingTextureReload();
    }
    renderedFrames.fetch_add(1, std::memory_order_relaxed);
    // Perf: once a minute (general cost, not tied to a check in progress).
    const std::string perf = MyClientProfile::Summary(GetTickCount64(), 60000);
    if (!perf.empty()) {
        auto& log = ll::mod::NativeMod::current()->getLogger();
        log.info("{}", perf);
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

LL_INSTANCE_HOOK(UiUpdateHook, ll::memory::HookPriority::Normal, UiUpdateSymbol(), void, MinecraftUIFrameUpdateContext& context)
{
    MyClientProfile::GameScope prof(MyClientProfile::UiUpdate);
    origin(context);
}

LL_INSTANCE_HOOK(UiRenderHook, ll::memory::HookPriority::Normal, UiRenderSymbol(), void,
                 ScreenContext& screenContext, FrameRenderObject const& renderObj)
{
    MyClientProfile::GameScope prof(MyClientProfile::UiRender);
    origin(screenContext, renderObj);
}

LL_INSTANCE_HOOK(HudTickHook, ll::memory::HookPriority::Normal, HudTickSymbol(), ui::DirtyFlag)
{
    MyClientProfile::GameScope prof(MyClientProfile::HudTick);
    return origin();
}

LL_INSTANCE_HOOK(ItemUpdateHook, ll::memory::HookPriority::Normal, ItemUpdateSymbol(), bool,
                 IClientInstance& client, UIControl& owner, UIScene const& scene)
{
    MyClientProfile::GameScope prof(MyClientProfile::ItemUpdate);
    return origin(client, owner, scene);
}

LL_INSTANCE_HOOK(ItemRenderHook, ll::memory::HookPriority::Normal, ItemRenderSymbol(), void,
                 MinecraftUIRenderContext& renderContext, IClientInstance& client, UIControl& owner, int pass)
{
    MyClientProfile::GameScope prof(MyClientProfile::ItemRender);
    origin(renderContext, client, owner, pass);
}
bool hudCostHooked = false;

LL_TYPE_INSTANCE_HOOK(FovHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{"?getFov@LevelRendererPlayer@@QEBAMM_N@Z"}, float, float alpha, bool variable)
{
    const float original = origin(alpha, variable);
    if (!active.load()) return original;
    MyClientProfile::Scope prof(MyClientProfile::Fov);
    bool playable = false;
    try {
        const auto client = ll::service::getClientInstance();
        // Never affect a secondary/split-screen camera or an inventory/chat menu.
        playable = client && &mClientInstance == &*client && client->getLocalPlayer() &&
            client->isInWorldAndNotShowingAnyMenuScreens();
    } catch (...) { playable = false; }
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    std::lock_guard guard(featureMutex);
    if (!active.load() || !channel.Data() || !channel.Lock()) return original;
    if (!MyClientFeatures::Valid(*channel.Data(), GetCurrentProcessId())) ResetChannel();
    const auto command = channel.Data()->command;
    const auto now = GetTickCount64();
    const bool keyDown = command.key > 0 && command.key < 255 && (GetAsyncKeyState(static_cast<int>(command.key)) & 0x8000);
    const bool zoom = MyClientFeatures::WantsZoom(command, now, foregroundPid == GetCurrentProcessId(), playable, keyDown);
    const float result = MyClientFeatures::ZoomFov(original, zoom, MyClientFeatures::ZoomFactor(command.reserved & 0xFFFFu));
    if (variable && playable)
    {
        try {
            const auto client2 = ll::service::getClientInstance();
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
                auto& t = channel.Data()->telemetry;
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
    channel.Data()->telemetry.zoomAt = now;
    channel.Data()->telemetry.zoomActive = result != original ? 1u : 0u;
    channel.Unlock();
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
            const auto client = ll::service::getClientInstance();
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

LL_TYPE_INSTANCE_HOOK(OutlineHookPublic, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{OutlineSymbolPublic}, void,
                      BaseActorRenderContext& renderContext, Block const& block, BlockSource& region, BlockPos const& pos)
{
    RunOutline([&] { origin(renderContext, block, region, pos); });
}
LL_TYPE_INSTANCE_HOOK(OutlineHookPrivate, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{OutlineSymbolPrivate}, void,
                      BaseActorRenderContext& renderContext, Block const& block, BlockSource& region, BlockPos const& pos)
{
    RunOutline([&] { origin(renderContext, block, region, pos); });
}
LL_TYPE_INSTANCE_HOOK(OutlineHookProtected, ll::memory::HookPriority::Normal, LevelRendererPlayer,
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

LL_TYPE_INSTANCE_HOOK(HitSelectHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{HitSelectSymbol}, void,
                      BaseActorRenderContext& renderContext, BlockSource& region, BlockPos const& pos, bool fancyGraphics)
{
    CaptureWorldCamera(renderContext);
    // While the coloured outline is drawn in game, the game's own black one is left out.
    if (outlineWanted.load(std::memory_order_relaxed) && MyClientCompositor::DrawingOutline()) {
        statOutline.fetch_add(1, std::memory_order_relaxed);
        return;
    }
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
        cameraCapturedFrame = frame;
        statCameraCaptures.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {}
}

// Diagnostic only now (calls per second in the log): the frame's camera comes from CaptureWorldCamera.
LL_TYPE_INSTANCE_HOOK(CameraHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
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
LL_TYPE_INSTANCE_HOOK(HighlightHook, ll::memory::HookPriority::Normal, LevelRendererPlayer,
                      ll::memory::SymbolView{HighlightSymbol}, void,
                      BaseActorRenderContext& renderContext, BlockSource& region, Block const& block, BlockPos const& pos,
                      bool renderSecondPart, bool fancyGraphics)
{
    RunOutline([&] { origin(renderContext, region, block, pos, renderSecondPart, fancyGraphics); });
}

constexpr const char* TessColorFloatSymbol = "?color@Tessellator@@QEAAXMMMM@Z";
constexpr const char* TessColorObjectSymbol = "?color@Tessellator@@QEAAXAEBVColor@mce@@@Z";

LL_TYPE_INSTANCE_HOOK(TessColorFloatHook, ll::memory::HookPriority::Normal, Tessellator,
                      ll::memory::SymbolView{TessColorFloatSymbol}, void, float r, float g, float b, float a)
{
    { MyClientProfile::Scope prof(MyClientProfile::TessColor); OverrideOutlineColor(r, g, b); }
    origin(r, g, b, a);
}
LL_TYPE_INSTANCE_HOOK(TessColorObjectHook, ll::memory::HookPriority::Normal, Tessellator,
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

LL_TYPE_INSTANCE_HOOK(NameTagHook, ll::memory::HookPriority::Normal, Actor,
                      ll::memory::SymbolView{NameTagSymbol}, bool)
{
    const bool original = origin();
    MyClientProfile::Scope prof(MyClientProfile::NameTag);
    statNameTag.fetch_add(1, std::memory_order_relaxed);
    if (!active.load(std::memory_order_relaxed)) return original;
    try {
        const auto client = ll::service::getClientInstance();
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
    if (range2 <= 0.f && playerRange2 <= 0.f) return false;
    if (static_cast<const void*>(&actor) == cullLocal.load(std::memory_order_relaxed)) return false;
    if (actor.isPlayer()) {
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

LL_TYPE_INSTANCE_HOOK(ActorRenderHook, ll::memory::HookPriority::Normal, ActorRenderDispatcher,
                      ll::memory::SymbolView{ActorRenderSymbol}, void,
                      BaseActorRenderContext& renderContext, Actor& entity, bool ignoreLighting)
{
    bool skip = false;
    { MyClientProfile::Scope prof(MyClientProfile::ActorRender); CaptureWorldCamera(renderContext); try { skip = CullActor(entity); } catch (...) {} }
    if (!skip) origin(renderContext, entity, ignoreLighting);
}

LL_TYPE_INSTANCE_HOOK(ActorRenderAtHook, ll::memory::HookPriority::Normal, ActorRenderDispatcher,
                      ll::memory::SymbolView{ActorRenderAtSymbol}, void,
                      BaseActorRenderContext& renderContext, Actor& entity, Vec3 const& cameraTargetPos,
                      Vec3 const& pos, Vec2 const& rot, bool ignoreLighting)
{
    bool skip = false;
    { MyClientProfile::Scope prof(MyClientProfile::ActorRender); CaptureWorldCamera(renderContext); try { skip = CullActor(entity); } catch (...) {} }
    if (!skip) origin(renderContext, entity, cameraTargetPos, pos, rot, ignoreLighting);
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
        ll::mod::NativeMod::current()->getLogger().info("Replay saved: {}", replayPathUtf8);
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
    const AABB& box = target.getAABB();
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
    if (!player.getSelectedItem().isEnchanted()) return;
    using DimensionFn = Dimension& (Actor::*)() const;
    static const auto getDimension = MyClientGame::ResolveMember<DimensionFn>("?getDimension@Actor@@QEBAAEAVDimension@@XZ");
    if (!getDimension) return;
    level.spawnParticleEffect(LitePack::kEnchantedHitId, Vec3{cx, cy, cz}, &(target.*getDimension)());
}

LL_TYPE_INSTANCE_HOOK(HitEffectHook, ll::memory::HookPriority::Normal, GameMode,
                      ll::memory::SymbolView{"?attack@GameMode@@UEAA_NAEAVActor@@AEBVVec3@@@Z"}, bool,
                      Actor& entity, Vec3 const& hitPosition)
{
    const bool result = origin(entity, hitPosition);
    MyClientProfile::Scope prof(MyClientProfile::Hit);
    if ((hitEffectCrit.load(std::memory_order_relaxed) || hitEffectMagic.load(std::memory_order_relaxed)) && active.load(std::memory_order_relaxed)) {
        try {
            Player& player = mPlayer;
            const auto client = ll::service::getClientInstance();
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
LL_TYPE_INSTANCE_HOOK(LeftHandMeshHook, ll::memory::HookPriority::Normal, mce::Mesh, RenderMeshTarget(), void,
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

LL_TYPE_INSTANCE_HOOK(LeftHandItemHook, ll::memory::HookPriority::Normal, ItemInHandRenderer,
                      ll::memory::SymbolView{RenderItemSymbol}, void,
                      BaseActorRenderContext& renderContext, Actor& entity, ItemStack const& item, bool posAndRotSetByJSON,
                      ItemContextFlags itemFlags, bool useMatrixAsIs, bool renderingMainHand)
{
    RunLeftHand(renderContext, &entity, itemFlags, [&] {
        origin(renderContext, entity, item, posAndRotSetByJSON, itemFlags, useMatrixAsIs, renderingMainHand);
    });
}

LL_TYPE_INSTANCE_HOOK(LeftHandItemNewHook, ll::memory::HookPriority::Normal, ItemInHandRenderer,
                      ll::memory::SymbolView{RenderItemNewSymbol}, void,
                      BaseActorRenderContext& renderContext, Actor& entity, ItemStack const& item, ItemContextFlags itemFlags,
                      Brightness lightEmission)
{
    RunLeftHand(renderContext, &entity, itemFlags, [&] { origin(renderContext, entity, item, itemFlags, lightEmission); });
}

LL_TYPE_INSTANCE_HOOK(LeftHandFirstPersonHook, ll::memory::HookPriority::Normal, ItemInHandRenderer,
                      ll::memory::SymbolView{RenderFirstPersonSymbol}, void,
                      BaseActorRenderContext& renderContext, Matrix const& prevProj, ItemContextFlags itemFlags)
{
    statFirstPersonPasses.fetch_add(1, std::memory_order_relaxed);
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
LL_TYPE_INSTANCE_HOOK(ItemInHandPassHook, ll::memory::HookPriority::Normal, ItemInHandPass, ItemInHandPassSymbol(), void,
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

LL_TYPE_INSTANCE_HOOK(LeftHandObjectHook, ll::memory::HookPriority::Normal, ItemInHandRenderer,
                      ll::memory::SymbolView{RenderObjectSymbol}, void,
                      BaseActorRenderContext& renderContext, ItemRenderCall const& renderObject,
                      dragon::RenderMetadata const& renderMetadata, ItemContextFlags itemFlags)
{
    RunLeftHand(renderContext, nullptr, itemFlags, [&] { origin(renderContext, renderObject, renderMetadata, itemFlags); });
}

// One packet into the session file (network threads; takes replayMutex). A session starts at the server's
// first packet of a connection (NetworkSettings), or at StartGame when that was not seen (local worlds).
// While recording, it runs on through server transfers.
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
LL_TYPE_INSTANCE_HOOK(ReplayReceiveHook, ll::memory::HookPriority::Normal, BatchedNetworkPeer,
                      ll::memory::SymbolView{MyClientGame::Symbols::ReceivePacket}, NetworkPeer::DataStatus,
                      std::string& outData,
                      std::shared_ptr<std::chrono::steady_clock::time_point> const& timepointPtr)
{
    const auto status = origin(outData, timepointPtr);
    if (status == NetworkPeer::DataStatus::HasData) { MyClientProfile::Scope prof(MyClientProfile::Replay); RecordPacket(outData, false); }
    return status;
}

LL_TYPE_INSTANCE_HOOK(ReplaySendHook, ll::memory::HookPriority::Normal, BatchedNetworkPeer,
                      ll::memory::SymbolView{MyClientGame::Symbols::SendPacket}, void,
                      std::string const& data, NetworkPeer::Reliability reliability, Compressibility compressible)
{
    { MyClientProfile::Scope prof(MyClientProfile::Replay); RecordPacket(data, true); }
    origin(data, reliability, compressible);
}

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

LL_TYPE_INSTANCE_HOOK(NickHook, ll::memory::HookPriority::Normal, Actor,
                      ll::memory::SymbolView{NickSymbol}, std::string const&)
{
    std::string const& original = origin();
    MyClientProfile::Scope prof(MyClientProfile::Nick);
    if (!nickWanted.load(std::memory_order_relaxed) || !active.load(std::memory_order_relaxed)) return original;
    try {
        const auto client = ll::service::getClientInstance();
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

LL_TYPE_INSTANCE_HOOK(LightHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
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

LL_TYPE_INSTANCE_HOOK(LightSplitHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
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

LL_TYPE_INSTANCE_HOOK(LightCreateHook, ll::memory::HookPriority::Normal, BaseLightTextureImageBuilder,
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
    host.info = [](const std::string& text) { ll::mod::NativeMod::current()->getLogger().info("{}", text); };
    host.warn = [](const std::string& text) { ll::mod::NativeMod::current()->getLogger().warn("{}", text); };
    host.hook = [](void* target, void* detour, void** original) {
        return ll::memory::hook(target, detour, original, ll::memory::HookPriority::Normal);
    };
    return MyClientCompositor::Start(host, probe);   // hooks once; later calls return the state
}
bool EnableFeatures()
{
    // Hooks remain installed until process exit. Disabled detours call origin;
    // the DLL cannot be dynamically unloaded, avoiding in-flight trampolines.
    auto& log = ll::mod::NativeMod::current()->getLogger();
    // Every game hook is installed on its own and its result is written to the game log, so one
    // missing symbol no longer switches every other feature off (and the log says which one failed).
    if (!damageValue) damageValue = reinterpret_cast<DamageFn>(ll::memory::SymbolView{"?getDamageValue@ItemStackBase@@QEBAFXZ"}.resolve());
    if (!damageValue) log.warn("Armor durability unavailable: symbol ?getDamageValue@ItemStackBase@@QEBAFXZ was not found.");
    // Manual registration checks errors; the macro's automatic-use counter is unused here.
    (void)FrameHook::_AutoHookCount; (void)FovHook::_AutoHookCount; (void)LightHook::_AutoHookCount; (void)LightSplitHook::_AutoHookCount; (void)LightCreateHook::_AutoHookCount;
    (void)NameTagHook::_AutoHookCount;
    (void)NickHook::_AutoHookCount;
    (void)ReplayReceiveHook::_AutoHookCount; (void)ReplaySendHook::_AutoHookCount; (void)HitEffectHook::_AutoHookCount;
    (void)LeftHandItemHook::_AutoHookCount; (void)LeftHandItemNewHook::_AutoHookCount;
    (void)LeftHandFirstPersonHook::_AutoHookCount; (void)LeftHandObjectHook::_AutoHookCount;
    (void)LeftHandMeshHook::_AutoHookCount;
    (void)PacksChangedHook::_AutoHookCount; (void)PacksChangedInnerHook::_AutoHookCount; (void)AtlasReloadedHook::_AutoHookCount;
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
        const bool a = ll::memory::SymbolView{BuildImageSymbol}.resolve() && LightHook::hook() == 0;
        const bool b = ll::memory::SymbolView{BuildImageSplitSymbol}.resolve() && LightSplitHook::hook() == 0;
        const bool c = ll::memory::SymbolView{LightCreateSymbol}.resolve() && LightCreateHook::hook() == 0;
        if (!c) log.warn("Full Bright: createBaseLightTextureData hook not found; only the buildImage hooks are used.");
        brightHooked = a || b || c;
        if (!brightHooked) log.error("Full Bright hook failed (BaseLightTextureImageBuilder::buildImage / buildImageSplit / createBaseLightTextureData).");
    }
    if (!cullHooked) {
        // Entity render distance: either overload is enough; the game may route through one or both.
        const bool a = ll::memory::SymbolView{ActorRenderSymbol}.resolve(true) && ActorRenderHook::hook() == 0;
        const bool b = ll::memory::SymbolView{ActorRenderAtSymbol}.resolve(true) && ActorRenderAtHook::hook() == 0;
        cullHooked = a || b;
        if (!cullHooked) log.warn("Entity render distance unavailable: ActorRenderDispatcher::render not found.");
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
        const bool a = ll::memory::SymbolView{RenderItemSymbol}.resolve(true) && LeftHandItemHook::hook() == 0;
        const bool b = ll::memory::SymbolView{RenderItemNewSymbol}.resolve(true) && LeftHandItemNewHook::hook() == 0;
        const bool object = ll::memory::SymbolView{RenderObjectSymbol}.resolve(true) && LeftHandObjectHook::hook() == 0;
        // Without renderFirstPerson the first-person pass cannot be told apart (the flags never mark it).
        const bool pass = ll::memory::SymbolView{RenderFirstPersonSymbol}.resolve(true) && LeftHandFirstPersonHook::hook() == 0;
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
        nickHooked = ll::memory::SymbolView{NickSymbol}.resolve() && NickHook::hook() == 0;
        if (!nickHooked) log.warn("Nick hook failed (Actor::getNameTag); Nick will not change the name.");
    }
    if (!nameTagsHooked) {
        nameTagsHooked = ll::memory::SymbolView{"?extractNameTags@LevelRendererPlayer@@UEBA?AUNameTagRenderObjectCollection@@AEAVScreenContext@@@Z"}
                             .resolve(true) && NameTagsHook::hook() == 0;
        if (!nameTagsHooked) log.warn("Nick: LevelRendererPlayer::extractNameTags not found; Nick can only change the old name path.");
    }
    if (!nametagHooked) {
        nametagHooked = ll::memory::SymbolView{NameTagSymbol}.resolve() && NameTagHook::hook() == 0;
        if (!nametagHooked) log.error("Nametag hook failed (Actor::canShowNameTag).");
    }
    if (!outlineHooked) {
        // The member's access level (public/private/protected) changes its mangled name: try each.
        if (ll::memory::SymbolView{OutlineSymbolPublic}.resolve() && OutlineHookPublic::hook() == 0) outlineHooked = true;
        else if (ll::memory::SymbolView{OutlineSymbolPrivate}.resolve() && OutlineHookPrivate::hook() == 0) outlineHooked = true;
        else if (ll::memory::SymbolView{OutlineSymbolProtected}.resolve() && OutlineHookProtected::hook() == 0) outlineHooked = true;
        hitSelectHooked = ll::memory::SymbolView{HitSelectSymbol}.resolve(true) && HitSelectHook::hook() == 0;
        highlightHooked = ll::memory::SymbolView{HighlightSymbol}.resolve(true) && HighlightHook::hook() == 0;
        if (!hitSelectHooked) log.warn("Block Outline: LevelRendererPlayer::renderHitSelect was not found.");
        if (!highlightHooked) log.warn("Block Outline: LevelRendererPlayer::_renderHighlightSelection was not found.");
        outlineHooked = outlineHooked || hitSelectHooked || highlightHooked;
        if (!outlineHooked) log.error("Block Outline hook failed: no selection-drawing function was found.");
    }
    if (!tessColorHooked) {
        const bool a = ll::memory::SymbolView{TessColorFloatSymbol}.resolve() && TessColorFloatHook::hook() == 0;
        const bool b = ll::memory::SymbolView{TessColorObjectSymbol}.resolve() && TessColorObjectHook::hook() == 0;
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
        focusLostHooked = ll::memory::SymbolView{"?onAppFocusLost@MinecraftGame@@UEAAXXZ"}.resolve(true) && FocusLostHook::hook() == 0;
        if (!focusLostHooked) log.warn("MinecraftGame::onAppFocusLost not found; opening the HUD menu shows the pause screen for a moment.");
    }
    if (!mouseFeedHooked) {
        mouseFeedHooked = ll::memory::SymbolView{MouseFeedSymbol}.resolve(true) && MouseFeedHook::hook() == 0;
        log.info("HUD menu: MouseDevice::feed hook {} (camera kept still behind the menu).", mouseFeedHooked ? "installed" : "NOT found");
    }
    if (textureReload && !packsChangedHooked) {
        static bool outer = false, inner = false;
        if (!outer) outer = ll::memory::SymbolView{"?onActiveResourcePacksChanged@MinecraftGame@@UEAAXAEAVResourcePackManager@@@Z"}.resolve(true) &&
                            PacksChangedHook::hook() == 0;
        if (!inner) inner = PacksChangedInnerSymbol() && PacksChangedInnerHook::hook() == 0;
        // Both are needed to tell "the game did not reload" apart from "it reloaded another way".
        packsChangedHooked = outer && inner;
        if (!packsChangedHooked) log.warn("Textures: MinecraftGame::onActiveResourcePacksChanged not found; a switch in a world may need a rejoin.");
    }
    if (textureReload && !atlasHooked) {
        atlasHooked = AtlasReloadedSymbol() && AtlasReloadedHook::hook() == 0;
        if (!atlasHooked) log.warn("Textures: MinecraftGame::_onTerrainAtlasReloaded not found; chunks are rebuilt after a fixed wait.");
    }
    // In-game drawing of the HUD picture and the 3D lines (replaces the overlay window when it works).
    const bool composited = StartDrawing(true);
    if (composited && !cameraHooked) {
        cameraHooked = ll::memory::SymbolView{"?setupCamera@LevelRendererPlayer@@QEAAXAEAVCamera@mce@@M@Z"}.resolve(true) &&
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
    channel.Data()->telemetry.fullbrightAvailable = AvailabilityBits();
    frames.Reset(); active.store(true); return true;
}
void DisableFeatures()
{
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
    channel.Data()->debug = {};
    channel.Data()->look = {};
    channel.Unlock();
}
uint32_t AutoRqMode() { return autoRqModeValue.load(); }

// Render Distance: sets the game's own view-distance option through the option registry, the same way
// a line of options.txt is read. The game's value before and after is logged so the result can be checked.
void ApplyRenderDistance()
{
    const uint32_t chunks = renderDistancePending.exchange(0);
    if (!chunks) return;
    auto& log = ll::mod::NativeMod::current()->getLogger();
    try {
        const auto client = ll::service::getClientInstance();
        if (!client) return;
        auto& options = client->getOptions();
        const int before = options.getViewDistanceChunks();
        options.loadOptionsFromString(MyClientFeatures::RenderDistanceOption(chunks));
        const int after = options.getViewDistanceChunks();
        log.info("Render Distance: requested {} chunks; the game went from {} to {} chunks.", chunks, before, after);
    } catch (...) {
        log.warn("Render Distance: the game rejected the change ({} chunks).", chunks);
    }
}

// Hive draws your name tag in your team's colour. The name tag is read through Actor::getNameTag; while
// Nick replaces it, the Nick hook has already recorded the real tag's colour.
char LocalTeamColor()
{
    using NameTagFn = std::string const& (*)(Actor const*);   // reference return: same ABI as `this` first
    static const auto getNameTag = reinterpret_cast<NameTagFn>(ll::memory::SymbolView{NickSymbol}.resolve(true));
    try {
        const auto client = ll::service::getClientInstance();
        const Actor* local = client ? static_cast<const Actor*>(client->getLocalPlayer()) : nullptr;
        if (!local || !getNameTag) return 0;
        std::string const& tag = getNameTag(local);
        if (nickWanted.load() && active.load()) return localTagColor.load();
        return MyClientAutoRq::NameColor(tag);
    } catch (...) { return 0; }
}
bool ShulkerHoverWanted() { return shulkerHoverWanted.load(); }
uint32_t FrameGenMultiplier(uint32_t field) { return MyClientFeatures::FrameGenMultiplierOf(field, frameGenWide.load()); }
uint32_t FrameGenField(bool& paused)
{
    const uint32_t screens = gameScreenBits.load();
    // Only while playing: menus, loading and the title screen change the whole picture at once.
    // The HUD menu too: FocusLostHook keeps the game's pause screen away, so it no longer shows up as GameMenuOpen.
    paused = (screens & MyClientFeatures::GameInWorld) == 0 || (screens & MyClientFeatures::GameMenuOpen) != 0 ||
             hudMenuWanted.load();
    return active.load() ? frameGenField.load() : 0u;
}
bool HitboxRequested() { return hitboxRequestedValue.load() && hitboxFilterValue.load() != 0; }
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
bool OutlineWanted() { return outlineWanted.load(); }
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
    channel.Data()->clockAt = GetTickCount64();
    channel.Unlock();
}
void SampleArmor()
{
    if (!active.load()) return;
    MyClientFeatures::Armor armor[4]{};
    char items[4][32]{};
    const auto client = ll::service::getClientInstance();
    const auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) { ClearArmor(); return; }
    for (int i = 0; i < 4; ++i) {
        const auto& stack = player->getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(i));
        if (!stack.mCount) armor[i] = MyClientFeatures::MakeArmor(false, 0, 0);
        else if (stack.mItem && damageValue) armor[i] = MyClientFeatures::MakeArmor(true, stack.mItem->getMaxDamage(), damageValue(&stack));
        else armor[i] = MyClientFeatures::MakeArmor(false, 0, 0);
        if (stack.mCount) {
            // Item id for the HUD's icon; "minecraft:" dropped, cut to fit (armor ids are short).
            std::string id = stack.getTypeName();
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

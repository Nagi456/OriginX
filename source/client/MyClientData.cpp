#include "OriginMarks.h"
#include "GameData.h"
#include "MapChannel.h"
#include "CapeChannel.h"
#include "CapeApply.h"
#include "MapData.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/LevelData.h"
#include "OwnHook.h"
#include "GameState.h"
#include "GameAccess.h"
#include "Log.h"
#include "FeatureAdapter.h"
#include "TextureReloadPolicy.h"
#include "PartyAdapter.h"
#include "../HiveStats.h"
#include "../PartyData.h"
#include "ResourceChannel.h"
#include "SelfCheck.h"
#include "ShulkerChannel.h"
#include "ItemCountChannel.h"
#include "TabListChannel.h"
#include "SkinChannel.h"
#include "GameSymbols.h"
#include "Profile.h"
namespace MyClientProfile {
// At most one stutter line every 2 s, and 200 per run, so a slow machine does not fill the log.
void LogSpike(const std::string& line)
{
    static std::atomic<uint64_t> lastAt{0};
    static std::atomic<unsigned> count{0};
    const uint64_t now = GetTickCount64();
    const uint64_t last = lastAt.load(std::memory_order_relaxed);
    if ((last && now - last < 2000) || count.load(std::memory_order_relaxed) >= 200) return;
    lastAt.store(now, std::memory_order_relaxed);
    count.fetch_add(1, std::memory_order_relaxed);
    MyClientLog::Get().info("{}", line);
}
}  // namespace MyClientProfile
#include "GameDataMirror.h"
#include "Compositor.h"
#include "SignatureDump.h"
#include "GameImageDump.h"
#include "HangWatch.h"
#include "CrashDiagnostics.h"
#include <intrin.h>
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/gui/controls/renderers/PaperDollRenderer.h"
#include "mc/client/gui/controls/renderers/IPaperDollUpdateContext.h"
#include "mc/world/actor/animation/AnimationComponent.h"

#include "ll/api/Versions.h"
#if !MYCLIENT_STANDALONE
#include "ll/api/mod/NativeMod.h"
#endif
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IMinecraftGame.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/models/ContainerModel.h"
#include "mc/world/level/Level.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/item/Item.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/scores/DisplayObjective.h"
#include "mc/world/scores/Objective.h"
#include "mc/world/scores/Scoreboard.h"
#include "mc/world/actor/player/PlayerListEntry.h"
#include "mc/world/phys/HitResult.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/core/image/Image.h"
#include "mc/util/ThreadOwner.h"
#include "mc/world/actor/player/SerializedSkinImpl.h"
#include "mc/world/actor/player/SerializedSkinRef.h"
#include "mc/world/actor/player/TrustedSkinFlag.h"
#include "mc/world/actor/player/AnimatedImageData.h"
#include "mc/world/actor/player/SerializedPersonaPieceHandle.h"
#include "mc/deps/core_graphics/helpers/TintMapColor.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/PacketSender.h"
#include "mc/network/packet/PlayerSkinPacket.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/deps/core/math/Color.h"
// 26.60 removed PaperDollPlayerSkinInfo and IPaperDollUpdateContext::getSkinFromPlayer (the doll no longer asks the
// context for a player's skin), so the hook on it exists only for the versions that still have it.
#if __has_include("mc/client/gui/controls/renderers/PaperDollPlayerSkinInfo.h")
#include "mc/client/gui/controls/renderers/PaperDollPlayerSkinInfo.h"
#define MYCLIENT_DOLL_SKIN_FROM_PLAYER 1
#else
#define MYCLIENT_DOLL_SKIN_FROM_PLAYER 0
#endif
#include "mc/client/gui/controls/renderers/SkinImages.h"
#include "mc/world/actor/player/SkinImage.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <cmath>
#include <functional>
#include <utility>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <chrono>
#include <thread>
#include <cctype>
#include <fstream>
#include <set>
#include <optional>
#include <cstddef>
#include <string_view>

namespace {
std::atomic<bool> enabled{false};
std::atomic<bool> enableFinished{false};   // the enable step ran to its end, worked or not (HangWatch)
std::mutex callbackMutex;
bool worldHooked = false;               // LevelTickHook and LevelExitHook are in (they are never taken out)
std::atomic<bool> loaded{false};   // the load step ran (LeviLamina's ll_mod_load, or the standalone start)
#if !MYCLIENT_STANDALONE
ll::mod::NativeMod* mod = nullptr;
#else
// Without LeviLamina there is no mod object; the code below only asks "has MyClient been loaded" of it.
struct { explicit operator bool() const { return loaded.load(); } } mod;
#endif
bool warned = false;
MyClientResources::Channel resourceChannel;
MyClientShulker::Channel shulkerChannel;
MyClientTabList::Channel tabListChannel;
MyClientSkin::Channel skinChannel;
MyClientSkin::RequestChannel skinRequests;

// The local player's skin picture for the HUD (menu home head, Closet). Checked every 2 s; the channel
// only bumps its serial when the pixels really changed. A persona skin without a classic picture
// publishes "none" and the HUD keeps its default head.
void PublishSkin(Player& player)
{
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (last && now - last < 2000) return;
    last = now;
    try
    {
        std::vector<uint8_t> rgba;
        uint32_t w = 0, h = 0;
        bool classic = true;
        auto const& ref = reinterpret_cast<std::unique_ptr<SerializedSkinRef> const&>(player.mSkin);
        auto const* owner = ref ? reinterpret_cast<std::shared_ptr<SerializedSkinRef::SerializedSkinWrapper> const&>(ref->mSkinImpl).get() : nullptr;
        if (owner)
        {
            auto const& image = reinterpret_cast<mce::Image const&>(owner->mObject.mSkinImage);
            auto const& bytes = image.mImageBytes;
            std::string const& geometry = owner->mObject.mDefaultGeometryName;
            std::string const& patch = owner->mObject.mResourcePatch;
            classic = MyClientSkin::ClassicLayout(owner->mObject.mIsPersona, geometry, patch);
            if (classic && MyClientSkin::ToRgba(static_cast<uint32_t>(image.imageFormat), bytes.data(), bytes.size(), image.mWidth, image.mHeight, rgba))
            {
                w = image.mWidth;
                h = image.mHeight;
            }
            // Once per different skin: which kind it is, for a head that looks wrong.
            static std::string loggedFor;
            const std::string key = std::to_string(image.mWidth) + "x" + std::to_string(image.mHeight) + geometry +
                                    (owner->mObject.mIsPersona ? " persona" : "");
            if (key != loggedFor)
            {
                loggedFor = key;
                if (mod)
                    MyClientLog::Get().info("Skin: {}x{} format {} ({} bytes) geometry='{}' persona={} -> {}", image.mWidth, image.mHeight,
                                          static_cast<uint32_t>(image.imageFormat), bytes.size(), geometry,
                                          static_cast<bool>(owner->mObject.mIsPersona),
                                          w ? "shared" : classic ? "not usable" : "not a classic layout, default head");
            }
        }
        skinChannel.Publish(GetCurrentProcessId(), rgba, w, h, now, classic ? 0 : MyClientSkin::NoClassicPicture);
    }
    catch (...) {}
}

// The Closet: wears the skin the HUD asked for, then tells the server like a Dressing Room change
// The game's client packet handler (LegacyClientNetworkHandler, as the NetEventCallback its handle overrides are
// called on), seen in its handlers of packets every world gets. The skin is also handed to its PlayerSkinPacket
// handler, the way another player's new skin arrives: Player::updateSkin alone changed the data but not the
// model the game draws (2026-10-28: in a world nothing changed, for you or others).
class StartGamePacket;
class SetActorDataPacket;
class MoveActorDeltaPacket;
class SetTimePacket;
struct ClientPacketHandler {};
std::atomic<void*> clientPacketHandler{nullptr};
#define MYCLIENT_HANDLER_HOOK(Name, PacketType)                                                                         \
    MYCLIENT_TYPE_INSTANCE_HOOK(Name, ll::memory::HookPriority::Normal, ClientPacketHandler,                                  \
                          MyClientGame::Target("?handle@LegacyClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBV" \
                                                 #PacketType "@@@Z"),                                                   \
                          void, NetworkIdentifier const& source, PacketType const& packet)                              \
    {                                                                                                                   \
        clientPacketHandler.store(this, std::memory_order_relaxed);                                                     \
        origin(source, packet);                                                                                         \
    }
MYCLIENT_HANDLER_HOOK(HandlerStartGameHook, StartGamePacket)
MYCLIENT_HANDLER_HOOK(HandlerActorDataHook, SetActorDataPacket)
MYCLIENT_HANDLER_HOOK(HandlerMoveDeltaHook, MoveActorDeltaPacket)
MYCLIENT_HANDLER_HOOK(HandlerSetTimeHook, SetTimePacket)
#undef MYCLIENT_HANDLER_HOOK

// The client's PlayerSkinPacket handler checks the skin before taking it (2026-10-28 log: "the skin handler left it",
// Player::updateSkin then changed the arms but not the picture: the handler's path is the one that makes the new
// texture). While the Closet hands it a skin, its two checks are let through; what they said is logged.
struct SkinCheckGate {};
thread_local bool closetHandingSkin = false;
thread_local uint32_t closetApplyDepth = 0;
struct ClosetApplyScope {
    ClosetApplyScope() { ++closetApplyDepth; }
    ~ClosetApplyScope() { --closetApplyDepth; }
};
std::atomic<int> closetCanUse{-1}, closetKeep{-1};   // -1 = not asked, else the game's own answer
constexpr const char* CanUseSkinSymbol =
    "?canUseSkin@ClientLevel@@UEBA_NAEBVSerializedSkinRef@@AEBVNetworkIdentifier@@AEBUActorUniqueID@@@Z";
constexpr const char* KeepSkinSymbol =
    "?_shouldKeepSkinBasedOnMemory@LegacyClientNetworkHandler@@AEBA_NAEBVUUID@mce@@AEBVSerializedSkinRef@@@Z";
MYCLIENT_TYPE_INSTANCE_HOOK(CanUseSkinHook, ll::memory::HookPriority::Normal, SkinCheckGate, ll::memory::SymbolView{CanUseSkinSymbol},
                      bool, SerializedSkinRef const& skin, NetworkIdentifier const& source, ActorUniqueID const& player)
{
    const bool ok = origin(skin, source, player);
    if (!closetHandingSkin) return ok;
    closetCanUse.store(ok ? 1 : 0);
    return true;
}
MYCLIENT_TYPE_INSTANCE_HOOK(KeepSkinHook, ll::memory::HookPriority::Normal, SkinCheckGate, ll::memory::SymbolView{KeepSkinSymbol},
                      bool, mce::UUID const& playerId, SerializedSkinRef const& skin)
{
    const bool ok = origin(playerId, skin);
    if (!closetHandingSkin) return ok;
    closetKeep.store(ok ? 1 : 0);
    return true;
}
const char* SkinCheckText(int verdict) { return verdict < 0 ? "not asked" : verdict ? "yes" : "no (let through)"; }

// --- What the Closet keeps on ------------------------------------------------------------------------------------
// The game sends your account's skin again whenever you join a world (2026-11-03: the Closet's skin was gone after a
// rejoin), so the last skin worn from the Closet is kept (also in a file, for the next game start) and put on again a
// few seconds into every world, until "My own skin" takes it off.
using SkinObject = std::shared_ptr<SerializedSkinRef::SerializedSkinWrapper>;
std::mutex closetWornMutex;
SkinObject closetWornObject;   // the skin object your player wears from the Closet (given to the pause screen's doll)
std::shared_ptr<const MyClientSkin::Request> closetPreviewRequest; // immutable snapshot; closetWornMutex
mce::UUID closetWornUuid;
MyClientSkin::Request closetKept;   // the skin to keep on (~256 KB: not on the stack)
bool closetHaveKept = false;
std::string closetKeptId;
SkinObject closetOriginal;          // your own skin in this world, before the Closet's was put on
std::atomic<bool> closetNewWorld{true};
ULONGLONG closetWorldSince = 0;
MyClientSkin::ReapplyBudget closetReapply;
bool closetMismatchLogged = false, closetLimitLogged = false;
unsigned closetCapeResultsLogged = 0;
std::atomic<unsigned> closetMismatchKind{0}; // generated ID / cape present; never record account IDs

void ResetClosetReapply()
{
    closetReapply.Reset();
    closetMismatchLogged = closetLimitLogged = false;
    closetCapeResultsLogged = 0;
}

void LogOwnCapeResult(MyClientSkin::Request const& request, bool ok)
{
    if (!request.capeId || !mod) return;
    const unsigned bit = ok ? 1 : 2;
    if (closetCapeResultsLogged & bit) return;
    closetCapeResultsLogged |= bit;
    MyClientLog::Get().info("Capes: own cape {} request {} (local application and server skin send; visual result unconfirmed)",
                            request.capeId, ok ? "completed" : "failed");
}
std::atomic<uint32_t> closetDollAsked{0}, closetDollGiven{0}, closetDollEmpty{0};
std::atomic<uint64_t> skinInstanceSerial{1};
std::atomic<bool> closetWaitingForList{false};

bool LocalSkinListReady(Level& level, Player& player)
{
    const auto& list = level.getPlayerList();
    const auto entry = list.find(MyClientGame::UuidOf(player));
    return entry != list.end() && entry->second.mSkin->mSkinImpl.get();
}

// The pause screen's doll does not draw your player: it asks for your skin by UUID (paper_doll_renderer,
// use_selected_skin false), which still answered the account's skin (2026-11-03: in the world the new skin, on the
// pause screen the old one). While the Closet's skin is on, that answer is given the Closet's skin object.
// A member function returning an object: on x64 MSVC `this`, then the hidden result pointer, which it returns.
// After a texture reload in a world the inventory's and pause screen's doll of you vanished too (2026-11-03): the
// dolls keep the model made from your skin and the reload dropped its picture. The next doll that asks is told your
// player changed (markLocalPlayerAsDirty), so the dolls build it again.
// 2026-11-03 log (#109): after a Closet skin went on, the pause screen's doll never asked for your skin again (no
// "asked" line at all) and kept the old one; after a texture reload it stayed empty. The doll asks only when it is
// told your player changed, and that was told from inside the asking itself. So every frame a doll updates
// (PaperDollRenderer::_update), a doll that has not seen the latest change is told (markLocalPlayerAsDirty).
std::atomic<uint32_t> dollGeneration{1};   // +1 whenever your skin changed (Closet, texture reload)
std::atomic<uint32_t> dollDirtied{0}, dollUpdates{0}, dollOtherContexts{0};
std::atomic<uint32_t> closetDollBuilt{0}, closetDollPictured{0};
bool dollUpdateHooked = false;
std::atomic<bool> dollReadyHooked{false};
void SkinChangedForDolls() { dollGeneration.fetch_add(1, std::memory_order_relaxed); }

using DirtyFn = void (*)(void*);
DirtyFn DollDirtyFunction()
{
    static const auto dirty = MyClientGame::ResolveFunction<DirtyFn>("?markLocalPlayerAsDirty@PaperDollClientInstanceUpdateContext@@UEAAXXZ");
    return dirty;
}
void* DollContextTable()
{
    static void* const table = reinterpret_cast<void*>(MyClientGame::Target("??_7PaperDollClientInstanceUpdateContext@@6B@"));
    return table;
}

// The doll's context: 2026-11-03 logs showed neither its vftable symbol nor markLocalPlayerAsDirty in its table
// (adjusting thunks, then PreLoader stubs). RTTI names the type (2026-11-04).
std::mutex dollTableMutex;
struct DollTable
{
    DirtyFn fn = nullptr;
    ptrdiff_t toObject = 0;   // RTTI: subtract from the pointer seen to get the whole PaperDollClientInstanceUpdateContext
};
std::unordered_map<void*, DollTable> dollTables;   // context table -> how to tell it (fn null = not the client's type)

bool ReadableBytes(const void* at, size_t size)
{
    if (!at) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(at, &info, sizeof info) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    const auto end = static_cast<const char*>(info.BaseAddress) + info.RegionSize;
    return static_cast<const char*>(at) + size <= end;
}

// The class name MSVC's RTTI gives a vftable (".?AVName@@"), empty when it has none or it cannot be read.
// 2026-11-04 (v5 signature dump): PreLoader hands out stubs for every symbol and the reverse lookup is empty, so
// neither the vftable symbol nor markLocalPlayerAsDirty's address can tell the doll's context apart. RTTI can.
std::string RttiName(void** table, ptrdiff_t* offset)
{
    if (!ReadableBytes(table - 1, sizeof(void*))) return {};
    const auto col = static_cast<const uint32_t*>(table[-1]);
    // x64 CompleteObjectLocator: signature 1, offset, cdOffset, pTypeDescriptor, pClassDescriptor, pSelf (RVAs).
    if (!ReadableBytes(col, 24) || col[0] != 1) return {};
    const auto base = reinterpret_cast<uintptr_t>(col) - col[5];
    const auto type = reinterpret_cast<const char*>(base + col[3]);
    if (!ReadableBytes(type, 16 + 96)) return {};
    std::string name;
    for (int i = 0; i < 96 && type[16 + i]; i++) name += type[16 + i];
    if (offset) *offset = static_cast<ptrdiff_t>(col[1]);
    return name;
}

// How to tell this doll context your player changed (fn null when it is not the client's own type).
DollTable DollDirtyFor(void* context)
{
    if (!context) return {};
    void** table = *static_cast<void***>(context);
    if (!table) return {};
    {
        std::lock_guard lock(dollTableMutex);
        if (auto it = dollTables.find(table); it != dollTables.end()) return it->second;
    }
    DollTable found;
    const auto dirty = DollDirtyFunction();
    std::string how;
    if (void* symbol = DollContextTable(); dirty && symbol && table == symbol) { found.fn = dirty; how = "vftable symbol"; }
    ptrdiff_t offset = 0;
    const std::string rtti = RttiName(table, &offset);
    if (!found.fn && dirty && rtti == ".?AVPaperDollClientInstanceUpdateContext@@")
    {
        found.fn = dirty;            // PreLoader's stub for markLocalPlayerAsDirty, called on the whole object
        found.toObject = offset;
        how = "RTTI (offset " + std::to_string(offset) + ")";
    }
    if (!found.fn && dirty && ReadableBytes(table, 48 * sizeof(void*)))
        for (int i = 0; i < 48 && !found.fn; i++)
            if (table[i] == reinterpret_cast<void*>(dirty)) { found.fn = dirty; how = "its table"; }
    std::lock_guard lock(dollTableMutex);
    if (dollTables.size() > 32) dollTables.clear();
    dollTables[table] = found;
    if (mod && (found.fn || dollTables.size() <= 4))
        MyClientLog::Get().info("Closet: a doll context's type is '{}': {}.", rtti.empty() ? "?" : rtti,
                              found.fn ? "told through markLocalPlayerAsDirty, found by " + how : "not the client's own (not told)");
    return found;
}

// Fallback when PaperDollRenderer::_update could not be hooked: the next doll that asks is told once.
uint32_t dollAskSeen = 1;
void MarkDollsDirty(void* context)
{
    if (dollUpdateHooked || !context) return;
    const uint32_t gen = dollGeneration.load(std::memory_order_relaxed);
    if (gen == dollAskSeen) return;
    dollAskSeen = gen;
    const auto dirty = DollDirtyFunction();
    if (!dirty) return;
    try { dirty(context); dollDirtied.fetch_add(1, std::memory_order_relaxed); } catch (...) {}
}

struct DollRendererGate {};
std::mutex dollSeenMutex;
std::unordered_map<const void*, uint32_t> dollSeen;   // doll context -> the change it was last told about
std::unordered_map<const void*, uint64_t> dollRouteSeen;   // renderer -> generation and selected route
std::unordered_map<const void*, uint32_t> pauseDollSeen;
struct DollMenuState : MyClientSkin::AvatarLifecycle {
    MyClientSkin::AvatarResources resources;
    MyClientSkin::AvatarLoadRetry loadRetry;
    bool retryBuild = false;
    uint64_t reloadRefreshAt = 0;
    bool reloadBuildObserved = false;
    const void* context = nullptr;
    std::shared_ptr<PlayerSkinPacket> packet; // keep the exact native ref/images alive across menu updates
    SkinObject accountSkin; // owned original ref; never mutate the account's object
    const AnimationComponent* initializedAnimation = nullptr; // identity only; use the live renderer's owner
    std::string fullId;
};
std::unordered_map<const void*, std::shared_ptr<DollMenuState>> menuDollStates; // dollSeenMutex
std::unordered_set<std::string> accountAvatarKeys; // closetWornMutex; observed from own selected title avatar only
std::atomic<uint32_t> dollRouteNotes{0};
thread_local void* pauseDollContext = nullptr;
thread_local const SerializedSkinRef* pauseDollSkin = nullptr;
thread_local uint64_t pauseDollBuildSerial = 0;
thread_local std::string dollBuiltFullId;
thread_local const MyClientSkin::Request* dollPreviewRequest = nullptr;
thread_local bool dollTitleScope = false;
thread_local bool dollProfileScope = false;
thread_local std::shared_ptr<DollMenuState> dollMenuState;
void TakeTitleSkinRequest();
inline uintptr_t DollReadyTarget();
bool RebuildRetainedMenuSkin(void*, PaperDollRenderer*, const std::shared_ptr<DollMenuState>&);
template <class Context>
bool DollAnimationsReady(Context* context) {
    if (!context) return false;
    if constexpr (requires { context->isRequiredPlayerAnimationLoaded(); })
        return context->isRequiredPlayerAnimationLoaded(); // 26.51's second native readiness condition
    else return true; // 26.60 folds readiness into isSkinReadyToRender
}
bool DollNativeReady(void* context, const std::string& key) {
    if (!context || key.empty()) return false;
    using Ready = bool (*)(void*, std::string_view);
    const auto ready = reinterpret_cast<Ready>(DollReadyTarget());
    return ready && ready(context, std::string_view(key)) &&
           DollAnimationsReady(static_cast<IPaperDollUpdateContext*>(context));
}
void ApplyDollReadiness(void* context, PaperDollRenderer* renderer, bool ready) {
    if (!context || !renderer) return;
    const auto animation = renderer->mAnimationComponent;
    ready = ready && animation != nullptr;
    if (ready && static_cast<bool>(renderer->mWaitForSkinInitialize)) {
        // Native _update's ready transition calls AnimationComponent::setDirty (26.51
        // 0x8DE6A73-88). Polling a skipped native branch must perform that step too.
        animation->setDirty();
        if (renderer->mMarkLocalPlayerAsDirty) {
            static_cast<IPaperDollUpdateContext*>(context)->markLocalPlayerAsDirty();
            renderer->mMarkLocalPlayerAsDirty = false;
        }
    }
    renderer->mWaitForSkinInitialize = !ready;
}
void FillClosetSkin(SerializedSkinImpl&, SerializedSkinImpl&, bool, MyClientSkin::Request const&, char (&)[48]);
struct PauseDollScope {
    void* previousContext = pauseDollContext;
    const SerializedSkinRef* previousSkin = pauseDollSkin;
    std::string previousFullId = std::move(dollBuiltFullId);
    const MyClientSkin::Request* previousRequest = dollPreviewRequest;
    bool previousTitle = dollTitleScope;
    bool previousProfile = dollProfileScope;
    std::shared_ptr<DollMenuState> previousMenu = dollMenuState;
    PauseDollScope(void* context, const SerializedSkinRef* skin, const MyClientSkin::Request* preview = nullptr,
                   bool title = false, std::shared_ptr<DollMenuState> menu = {}, bool profile = false) {
        pauseDollContext = context;
        pauseDollSkin = skin;
        dollBuiltFullId = skin && skin->mSkinImpl ? skin->mSkinImpl->mObject.mFullId.get() : menu ? menu->fullId : std::string{};
        dollPreviewRequest = preview;
        dollTitleScope = title;
        dollProfileScope = profile;
        dollMenuState = std::move(menu);
    }
    ~PauseDollScope() {
        pauseDollContext = previousContext;
        pauseDollSkin = previousSkin;
        dollBuiltFullId = std::move(previousFullId);
        dollPreviewRequest = previousRequest;
        dollTitleScope = previousTitle;
        dollProfileScope = previousProfile;
        dollMenuState = std::move(previousMenu);
    }
};
template <class Renderer>
int DollUuidMode(Renderer const& renderer)
{
    if constexpr (requires { renderer.mUseUUID; }) return static_cast<bool>(renderer.mUseUUID) ? 1 : 0;
    else return -1;   // not present in the 26.60 SDK; never guess its offset
}
constexpr const char* DollUpdateSymbol =
    "?_update@PaperDollRenderer@@QEAAXAEAVIPaperDollUpdateContext@@AEBUPaperDollUpdateOwnerData@@@Z";
MYCLIENT_TYPE_INSTANCE_HOOK(DollUpdateHook, ll::memory::HookPriority::Normal, DollRendererGate, MyClientGame::Target(DollUpdateSymbol),
                      void, void* context, void const* data)
{
    if (!this || !context || !data) return;
    dollUpdates.fetch_add(1, std::memory_order_relaxed);
    bool titleRefreshThisUpdate = false;
    if (!MyClientGame::TextureReloadHeld() && MyClientGame::TitleAvatarRefreshPending()) {
        const auto client = MyClientGame::Client();
        const bool ownTitle = client && MyClientTexturePolicy::IsTitleForReload(client->getLocalPlayer() != nullptr,
            bool(MyClientGame::WorldLevel()), client->getTopScreenName()) &&
            static_cast<bool>(reinterpret_cast<PaperDollRenderer*>(this)->mUseSelectedSkin);
        if (ownTitle && dollReadyHooked.load() && DollDirtyFor(context).fn && MyClientGame::TakeTitleAvatarRefresh()) {
            titleRefreshThisUpdate = true;
            SkinChangedForDolls();
            MyClientLog::Get().info("Closet: own title avatar rebuild requested once after resource completion");
        }
    }
    if (context && !MyClientGame::TextureReloadHeld())
    {
        const uint32_t gen = dollGeneration.load(std::memory_order_relaxed);
        bool tell = false;
        {
            std::lock_guard lock(dollSeenMutex);
            // Keyed by the context, not the renderer (2026-11-03 log, r15: "told your skin changed 0 times (0 other
            // contexts)"). The pause screen makes a new doll renderer each time it opens, which was taken as "builds
            // from the current skin anyway" and never told; but the old skin stays in the long-lived context. A
            // context first seen after a change is told too (it starts at the first generation).
            auto it = dollSeen.find(context);
            if (it == dollSeen.end())
            {
                if (dollSeen.size() > 64) dollSeen.clear();
                it = dollSeen.emplace(context, 1u).first;
            }
            if (it->second != gen)
            {
                it->second = gen;
                tell = true;
            }
        }
        if (tell)
        {
            // Only the client's own context type: its markLocalPlayerAsDirty is the function resolved here.
            const auto dirty = DollDirtyFor(context);
            if (dirty.fn)
            {
                void* self = static_cast<char*>(context) - dirty.toObject;
                try { dirty.fn(self); dollDirtied.fetch_add(1, std::memory_order_relaxed); } catch (...) {}
            }
            else
                dollOtherContexts.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // The pause doll in r40 has selected=false/byName=false/uuid=false: it builds from an account skin pack,
    // bypassing getSkinFromPlayer. Supply the local player's already-applied skin only during this doll update.
    // Title's own avatar has a separate saved-request path below; shop/other-player previews are excluded.
    const SerializedSkinRef* localSkin = nullptr;
    SkinObject keepSkinAlive;
    std::shared_ptr<const MyClientSkin::Request> titlePreview;
    std::shared_ptr<DollMenuState> menuState;
    std::shared_ptr<PlayerSkinPacket> retiredMenuPacket; // release only after native removal/rebuild this update
    bool rebuildMenu = false;
    bool titleOwnDoll = false;
    bool profileOwnDoll = false;
    if (this && context) {
        const auto client = MyClientGame::Client();
        auto* player = client ? client->getLocalPlayer() : nullptr;
        const std::string screen = client ? client->getTopScreenName() : std::string{};
        profileOwnDoll = screen.find("DressingRoom_PersonaProfile") != std::string::npos ||
                         screen.find("MultiItemPage_PersonaCharacterCreator") != std::string::npos ||
                         screen.find("MultiItemPage_PersonaSkinSelector") != std::string::npos;
        // Profile dolls additionally require the title's observed account identity in DollBuildHook.
        // Skin shop, arbitrary previews and inventory are not selected by this screen gate.
        const bool title = MyClientTexturePolicy::IsTitleForReload(player != nullptr, bool(MyClientGame::WorldLevel()), screen) &&
                           static_cast<bool>(reinterpret_cast<PaperDollRenderer*>(this)->mUseSelectedSkin);
        if (dollReadyHooked.load() && client && (title || profileOwnDoll) && !MyClientGame::TextureReloadHeld() && DollDirtyFor(context).fn) {
            titleOwnDoll = true;
            if (!player && !MyClientGame::WorldLevel()) TakeTitleSkinRequest();
            std::lock_guard lock(closetWornMutex);
            titlePreview = closetPreviewRequest;
        }
        if (dollReadyHooked.load() && player && !MyClientGame::TextureReloadHeld() &&
            client->getTopScreenName().find("pause") != std::string::npos && DollDirtyFor(context).fn) {
            mce::UUID who;
            {
                std::lock_guard lock(closetWornMutex);
                keepSkinAlive = closetWornObject;
                who = closetWornUuid;
            }
            const bool samePlayer = keepSkinAlive && MyClientGame::UuidOf(*player) == who;
            const bool sameObject = keepSkinAlive && player->mSkin && player->mSkin->mSkinImpl.get() == keepSkinAlive.get();
            // Use the game-normalized skin that already works in inventory. updateSkin may copy its
            // wrapper, so validate the skin ID and player UUID rather than the shared_ptr address.
            const auto playerObject = player->mSkin ? player->mSkin->mSkinImpl : SkinObject{};
            const bool sameSkin = samePlayer && playerObject &&
                playerObject->mObject.mId.get() == keepSkinAlive->mObject.mId.get();
            if (sameSkin) {
                keepSkinAlive = playerObject;
                localSkin = &*player->mSkin;
            }
            const uint32_t gen = dollGeneration.load(std::memory_order_relaxed);
            bool changed = false;
            {
                std::lock_guard lock(dollSeenMutex);
                if (pauseDollSeen.size() > 64) pauseDollSeen.clear();
                auto [it, inserted] = pauseDollSeen.emplace(this, gen);
                changed = inserted || it->second != gen;
                it->second = gen;
            }
            if (changed) {
                MyClientLog::Get().info("Closet: pause doll input savedSkin={} playerSkin={} sameObject={} samePlayer={} overrideReady={}",
                                      keepSkinAlive != nullptr, player->mSkin && player->mSkin->mSkinImpl.get(),
                                      sameObject, samePlayer, localSkin != nullptr);
                auto* renderer = reinterpret_cast<PaperDollRenderer*>(this);
                // _update skips an unchanged pack/index and timestamp before createOrUpdateSkin. Invalidate
                // these cached keys once per skin generation, also when "My own skin" removes the override.
                renderer->mPaperDollSkinIdx = (-2147483647 - 1);
                renderer->mSkinLastUpdated = std::chrono::steady_clock::time_point::min();
            }
        }
    }
    if (titleOwnDoll) {
        const uint32_t gen = dollGeneration.load(std::memory_order_relaxed);
        std::lock_guard lock(dollSeenMutex);
        if (menuDollStates.size() > 64) menuDollStates.clear();
        auto& state = menuDollStates[this];
        if (!state) state = std::make_shared<DollMenuState>();
        const auto* renderer = reinterpret_cast<const PaperDollRenderer*>(this);
        if (state->context != context || (state->nativeReady && renderer->mSkinName.get().empty()) ||
            (!state->fullId.empty() && renderer->mSkinName.get() != state->fullId))
            *state = DollMenuState{}; // allocator reused a renderer address, or native state was reset
        state->context = context;
        menuState = state;
        const bool resourceChanged = state->resources.Changed(MyClientGame::AvatarResourceEpoch());
        if (resourceChanged) {
            // Invalidate current texture readiness, but retain the owned source and the
            // identity of the animation that actually initialized before this reload.
            state->nativeReady = false;
            state->overriding = false;
            state->loadRetry = {};
            state->retryBuild = true;
            MyClientLog::Get().info("Closet: menu avatar resources changed; rebuilding model (profile={} initialized={})",
                                  profileOwnDoll, state->initialized);
        }
        const auto& animation = renderer->mAnimationComponent;
        const bool retainedNative = !profileOwnDoll && state->accountSkin && animation &&
                                    state->initializedAnimation == animation.get();
        const auto change = state->Select(gen, titlePreview != nullptr, retainedNative, titleRefreshThisUpdate);
        if (titleRefreshThisUpdate) {
            state->reloadRefreshAt = GetTickCount64();
            state->reloadBuildObserved = false;
            MyClientLog::Get().info("Closet: title post-reload rebuild started (chosen={} initialized={}); invalidating native pack/index cache", titlePreview != nullptr, state->initialized);
        }
        if (change.changed) state->loadRetry = {};
        const bool retry = state->retryBuild;
        state->retryBuild = false;
        if (change.changed || retry) {
            retiredMenuPacket = std::move(state->packet);
            state->fullId.clear();
            if (change.rebuild || retry) {
                rebuildMenu = true;
                auto* renderer = reinterpret_cast<PaperDollRenderer*>(this);
                renderer->mPaperDollSkinIdx = (-2147483647 - 1);
                renderer->mSkinLastUpdated = std::chrono::steady_clock::time_point::min();
            }
        }
    }
    PauseDollScope pauseScope(localSkin || titleOwnDoll ? context : nullptr, localSkin,
                             menuState && menuState->overriding ? titlePreview.get() : nullptr,
                             titleOwnDoll, menuState, profileOwnDoll);
    const uint64_t buildsBefore = pauseDollBuildSerial;
    origin(context, data);
    // Native selected-pack lookup can return null / index -1 after a reload. In that
    // case invalidating its timestamp cannot reach createOrUpdateSkin. Only a title
    // animation which previously initialized may rebuild from the retained account ref.
    if (rebuildMenu && menuState && menuState->overriding && !profileOwnDoll &&
        pauseDollBuildSerial == buildsBefore)
        RebuildRetainedMenuSkin(context, reinterpret_cast<PaperDollRenderer*>(this), menuState);
    if (menuState && menuState->overriding && !profileOwnDoll && dollBuiltFullId.empty() &&
        menuState->loadRetry.Retry(GetTickCount64(), false)) {
        menuState->retryBuild = true;
        MyClientLog::Get().warn("Closet: title avatar build still pending without a key; retrying (attempt={})",
                               menuState->loadRetry.attempts);
    }
    if (menuState && !menuState->overriding) {
        auto* renderer = reinterpret_cast<PaperDollRenderer*>(this);
        // Initialize the account avatar/animations normally first. Do not replace its first pending build.
        const bool wasReady = menuState->nativeReady;
        const auto& nativeKey = renderer->mSkinName.get();
        if (!nativeKey.empty()) ApplyDollReadiness(context, renderer, DollNativeReady(context, nativeKey));
        menuState->ObserveNative(!static_cast<bool>(renderer->mWaitForSkinInitialize) && !renderer->mSkinName.get().empty());
        if (menuState->nativeReady) menuState->initializedAnimation = renderer->mAnimationComponent.get();
        // Startup/return can stall BEFORE the Closet override is built. Recover that native phase too.
        // Profile screens share these native resources, so they must not keep a permanently pending model.
        if (menuState->loadRetry.Retry(GetTickCount64(), menuState->nativeReady)) {
            menuState->retryBuild = true;
            MyClientLog::Get().warn("Closet: native menu avatar initialization stalled; rebuilding (profile={} attempt={} keyEmpty={})",
                                    profileOwnDoll, menuState->loadRetry.attempts, nativeKey.empty());
        }
        if (!wasReady && menuState->nativeReady)
            MyClientLog::Get().info("Closet: menu avatar native initialization completed (profile={})", profileOwnDoll);
    }
    if (this && !dollBuiltFullId.empty()) {
        auto* renderer = reinterpret_cast<PaperDollRenderer*>(this);
        const auto& fullId = dollBuiltFullId;
        // Native _update assigns the account pack's FullId AFTER createOrUpdateSkin. Our build hook
        // changed the model/texture key to the player's FullId, so retaining the account key makes
        // _render look up a different texture. Keep the renderer's lookup/removal key with its build.
        if (!fullId.empty()) {
            const bool matched = renderer->mSkinName.get() == fullId;
            renderer->mSkinName = fullId;
            // Native _update may skip its readiness query when its pack/index cache is unchanged.
            // Poll the actual native texture state even on those frames; never pretend it is ready.
            if (menuState && menuState->overriding && !profileOwnDoll) {
                const bool ready = DollNativeReady(context, fullId);
                ApplyDollReadiness(context, renderer, ready);
                if (menuState->loadRetry.Retry(GetTickCount64(), ready)) {
                    menuState->retryBuild = true;
                    MyClientLog::Get().warn("Closet: title avatar texture load stalled; rebuilding private skin (attempt={})",
                                            menuState->loadRetry.attempts);
                }
            }
            if (!matched || pauseDollBuildSerial != buildsBefore)
                MyClientLog::Get().info("Closet: own doll render key synchronized (menu={} profile={} alreadyMatched={} waiting={})",
                                      titleOwnDoll, profileOwnDoll, matched, static_cast<bool>(renderer->mWaitForSkinInitialize));
        }
    }
    if (menuState && menuState->reloadRefreshAt) {
        const auto* renderer = reinterpret_cast<const PaperDollRenderer*>(this);
        const auto& key = renderer->mSkinName.get();
        const bool ready = !key.empty() && !static_cast<bool>(renderer->mWaitForSkinInitialize) && DollNativeReady(context, key);
        const auto elapsed = GetTickCount64() - menuState->reloadRefreshAt;
        if (ready || elapsed >= 10000) {
            MyClientLog::Get().info("Closet: title post-reload rebuild observation (nativeBuildCalled={} nativeReady={} keyPresent={} elapsedMs={}; visual result unconfirmed)",
                menuState->reloadBuildObserved, ready, !key.empty(), elapsed);
            menuState->reloadRefreshAt = 0; // bounded diagnostic, never force native readiness
        }
    }
    // A dirty notification is not evidence that the doll asked for (or built) the Closet skin. The selected
    // account skin bypasses getSkinFromPlayer entirely. Capture the route after the actual update, including
    // pause-screen updates immediately before exit (the 30-second world-tick summary can miss those).
    if (this && context && dollRouteNotes.load(std::memory_order_relaxed) < 32) {
        const auto* renderer = reinterpret_cast<const PaperDollRenderer*>(this);
        const bool selected = static_cast<bool>(renderer->mUseSelectedSkin);
        const bool byName = static_cast<bool>(renderer->mUseSkinByName);
        const int uuid = DollUuidMode(*renderer);
        const uint64_t route = (uint64_t(dollGeneration.load()) << 4) | (selected ? 1u : 0u) |
                               (byName ? 2u : 0u) | (uuid > 0 ? 4u : 0u) | (uuid < 0 ? 8u : 0u);
        bool note = false;
        {
            std::lock_guard lock(dollSeenMutex);
            auto [it, inserted] = dollRouteSeen.emplace(this, route);
            note = inserted || it->second != route;
            it->second = route;
        }
        if (note && dollRouteNotes.fetch_add(1, std::memory_order_relaxed) < 32)
            MyClientLog::Get().info("Closet: doll route selected={} byName={} uuid={} asked={} given={} empty={} pictured={}",
                                  selected, byName, uuid, closetDollAsked.load(), closetDollGiven.load(),
                                  closetDollEmpty.load(), closetDollPictured.load());
    }
}

#if MYCLIENT_DOLL_SKIN_FROM_PLAYER
constexpr const char* DollSkinSymbol =
    "?getSkinFromPlayer@PaperDollClientInstanceUpdateContext@@UEAA?AV?$optional@UPaperDollPlayerSkinInfo@@@std@@AEBVUUID@mce@@@Z";
MYCLIENT_STATIC_HOOK(DollSkinHook, ll::memory::HookPriority::Normal, ll::memory::SymbolView{DollSkinSymbol}, void*,
               void* self, void* out, mce::UUID const& id)
{
    void* result = origin(self, out, id);
    MarkDollsDirty(self);
    try
    {
        SkinObject worn;
        mce::UUID who;
        {
            std::lock_guard lock(closetWornMutex);
            worn = closetWornObject;
            who = closetWornUuid;
        }
        auto* info = static_cast<std::optional<PaperDollPlayerSkinInfo>*>(result);
        if (worn && info && id == who)
        {
            closetDollAsked.fetch_add(1, std::memory_order_relaxed);
            if (info->has_value() && (*info)->mSkin->mSkinImpl.get())
            {
                closetDollGiven.fetch_add(1, std::memory_order_relaxed);
            }
            else
                closetDollEmpty.fetch_add(1, std::memory_order_relaxed);
        }
    }
    catch (...) {}
    return result;
}

#endif

// A player skin and its images must come from the same applied object. Native ClientInstance's
// model builder falls back to a default skin when classic skins have no valid image override;
// passing null image pointers is not a fix for the r44 transparent pause doll.
constexpr const char* DollBuildSymbol =
    "?createOrUpdateSkin@PaperDollClientInstanceUpdateContext@@UEAAXAEAVAnimationComponent@@AEBVSerializedSkinRef@@AEBUSkinImages@@@Z";
MYCLIENT_STATIC_HOOK(DollBuildHook, ll::memory::HookPriority::Normal, MyClientGame::Target(DollBuildSymbol), void,
               void* self, AnimationComponent& animation, SerializedSkinRef const& skin, SkinImages const& images)
{
    if (!self) return;
    MarkDollsDirty(self);
    const auto* sourceOwner = skin.mSkinImpl.get();
    if (self == pauseDollContext && dollTitleScope && sourceOwner) {
        const std::string sourceKey = sourceOwner->mObject.mFullId.get();
        bool ours = true;
        {
            std::lock_guard lock(closetWornMutex);
            if (!dollProfileScope && !sourceKey.empty()) {
                if (accountAvatarKeys.size() > 32) accountAvatarKeys.clear();
                accountAvatarKeys.insert(sourceKey);
            } else if (dollProfileScope) ours = accountAvatarKeys.contains(sourceKey);
        }
        if (!ours) {
            // A profile screen also renders other profiles/catalog tiles. Only the selected account identity
            // observed on the title is ours; a matching screen/context alone is not sufficient.
            dollBuiltFullId.clear();
            if (dollMenuState) dollMenuState->fullId.clear();
            return origin(self, animation, skin, images);
        }
        if (dollMenuState) {
            dollMenuState->accountSkin = skin.mSkinImpl;
            if (dollMenuState->reloadRefreshAt) dollMenuState->reloadBuildObserved = true;
        }
    }
    const auto* applied = self && self == pauseDollContext ? pauseDollSkin : nullptr;
    std::shared_ptr<PlayerSkinPacket> titlePacket; // native constructor + shared ownership; never sent
    try {
        if (self == pauseDollContext && !applied && dollPreviewRequest && skin.mSkinImpl) {
            using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
            static const auto createPacket = MyClientGame::ResolveFunction<PacketFactory>(
                "?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z");
            if (dollMenuState) titlePacket = dollMenuState->packet;
            if (createPacket && MyClientSkin::UsableRequest(*dollPreviewRequest)) {
                if (!titlePacket) {
                    if (auto packet = createPacket(MinecraftPacketIds::PlayerSkin))
                        titlePacket = std::static_pointer_cast<PlayerSkinPacket>(packet);
                    if (titlePacket && titlePacket->mSkin->mSkinImpl &&
                        titlePacket->mSkin->mSkinImpl.get() != skin.mSkinImpl.get()) {
                        char id[48];
                        FillClosetSkin(titlePacket->mSkin->mSkinImpl->mObject, skin.mSkinImpl->mObject, true, *dollPreviewRequest, id);
                        if (dollMenuState) dollMenuState->packet = titlePacket;
                    }
                }
                if (titlePacket && titlePacket->mSkin->mSkinImpl &&
                    titlePacket->mSkin->mSkinImpl.get() != skin.mSkinImpl.get()) {
                    applied = &*titlePacket->mSkin;
                }
            }
        }
    } catch (...) {
        applied = nullptr;
        titlePacket.reset();
        MyClientLog::Get().warn("Closet: title skin could not be prepared; keeping the account avatar.");
    }
    const auto* owner = applied ? applied->mSkinImpl.get() : nullptr;
    if (self && self == pauseDollContext && dollTitleScope) {
        // The world-tick publisher cannot update the OriginX home/Closet while no player exists.
        const auto* shown = owner ? owner : skin.mSkinImpl.get();
        if (shown) {
            const auto& impl = shown->mObject;
            const auto& image = impl.mSkinImage.get();
            const auto& bytes = image.mImageBytes;
            std::vector<uint8_t> rgba;
            const bool classic = MyClientSkin::ClassicLayout(impl.mIsPersona, impl.mDefaultGeometryName.get(), impl.mResourcePatch.get());
            if (classic && MyClientSkin::ToRgba(static_cast<uint32_t>(image.imageFormat), bytes.data(), bytes.size(),
                                               image.mWidth, image.mHeight, rgba))
                skinChannel.Publish(GetCurrentProcessId(), rgba, image.mWidth, image.mHeight, GetTickCount64());
            else
                skinChannel.Publish(GetCurrentProcessId(), {}, 0, 0, GetTickCount64(), MyClientSkin::NoClassicPicture);
        }
    }
    if (!owner) return origin(self, animation, skin, images);
    SkinImages mine = images;
    mine.mSkinImage = static_cast<mce::Image const*>(&owner->mObject.mSkinImage.get());
    mine.mCapeImage = static_cast<mce::Image const*>(&owner->mObject.mCapeImage.get());
    closetDollBuilt.fetch_add(1, std::memory_order_relaxed);
    closetDollPictured.fetch_add(1, std::memory_order_relaxed);
    MyClientLog::Get().info("Closet: own doll native build uses chosen skin and matching images (title={} resourceReloadHeld={} geometry='{}' slim={})",
                          dollTitleScope, MyClientGame::TextureReloadHeld(), owner->mObject.mDefaultGeometryName.get(),
                          owner->mObject.mArmSizeType == SharedTypes::persona::ArmSizeType::Slim);
    origin(self, animation, *applied, mine);
    // Native _update subsequently writes the account FullId before checking readiness. Retain the built
    // key for that check as well as the final renderer key; changing it after _update was too late.
    dollBuiltFullId = owner->mObject.mFullId.get();
    if (dollMenuState) dollMenuState->fullId = dollBuiltFullId;
    ++pauseDollBuildSerial;
}

bool RebuildRetainedMenuSkin(void* context, PaperDollRenderer* renderer,
                             const std::shared_ptr<DollMenuState>& state)
{
    if (!context || !renderer || !state || !state->initialized || !state->overriding ||
        !state->accountSkin || !dollPreviewRequest || dollProfileScope || context != pauseDollContext) return false;
    const auto animation = renderer->mAnimationComponent; // hold the actual current animation alive
    if (!animation || state->initializedAnimation != animation.get()) return false;
    using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
    static const auto createPacket = MyClientGame::ResolveFunction<PacketFactory>(
        "?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z");
    if (!createPacket) return false;
    try {
        const auto packet = createPacket(MinecraftPacketIds::PlayerSkin);
        if (!packet) return false;
        const auto seed = std::static_pointer_cast<PlayerSkinPacket>(packet);
        if (!seed) return false;
        // Use a native-constructed ref instead of SerializedSkinRef's unavailable copy constructor.
        seed->mSkin->mSkinImpl = state->accountSkin;
        const auto owner = state->accountSkin;
        if (!owner) return false;
        SkinImages images{};
        images.mSkinImage = static_cast<mce::Image const*>(&owner->mObject.mSkinImage.get());
        images.mCapeImage = static_cast<mce::Image const*>(&owner->mObject.mCapeImage.get());
        const std::string oldKey = renderer->mSkinName.get();
        DollBuildHook::detour(context, *animation, *seed->mSkin, images);
        if (state->fullId.empty()) return false;
        // Native _update did not reach its removal/build branch. Retire the old exact
        // key after a fresh independently owned model was submitted, never the new key.
        auto& remove = renderer->mRemoveSkin.get();
        if (remove && !oldKey.empty() && oldKey != state->fullId) remove(std::string_view(oldKey));
        renderer->mSkinName = state->fullId;
        renderer->mWaitForSkinInitialize = true; // actual native readiness is still polled below / next update
        MyClientLog::Get().info("Closet: title avatar rebuild submitted from retained skin after native update skipped build");
        return true;
    } catch (...) {
        MyClientLog::Get().warn("Closet: retained title skin rebuild failed; keeping native readiness pending");
        return false;
    }
}

constexpr const char* DollReadySymbol51 = "?isAnySkinOfNameInitialized@PaperDollClientInstanceUpdateContext@@UEBA_NV?$basic_string_view@DU?$char_traits@D@std@@@std@@@Z";
constexpr const char* DollReadySymbol60 = "?isSkinReadyToRender@PaperDollClientInstanceUpdateContext@@UEBA_NV?$basic_string_view@DU?$char_traits@D@std@@@std@@@Z";
inline uintptr_t DollReadyTarget()
{
#if MYCLIENT_DOLL_SKIN_FROM_PLAYER
    return MyClientGame::Target(DollReadySymbol51);
#else
    return MyClientGame::Target(DollReadySymbol60);
#endif
}
MYCLIENT_STATIC_HOOK(DollReadyHook, ll::memory::HookPriority::Normal, DollReadyTarget(), bool,
                    void* self, std::string_view name)
{
    if (!self) return false;
    if (self != pauseDollContext || dollBuiltFullId.empty()) return origin(self, name);
    const bool ready = origin(self, std::string_view(dollBuiltFullId)); // never force readiness to true
    static std::mutex noteMutex;
    static std::unordered_map<std::string, bool> notes;
    bool note = false;
    {
        std::lock_guard lock(noteMutex);
        if (notes.size() > 64) notes.clear();
        auto [it, inserted] = notes.emplace(dollBuiltFullId, ready);
        note = inserted || it->second != ready;
        it->second = ready;
    }
    if (note) MyClientLog::Get().info("Closet: own doll readiness uses built key (menu={} profile={} requestedMatches={} ready={})",
                                    dollTitleScope, dollProfileScope, name == dollBuiltFullId, ready);
    return ready;
}

// SerializedSkinImpl::mPlayFabId exists only before 26.60.
template <class Skin>
void CopyPlayFabId(Skin& to, Skin const& from)
{
    if constexpr (requires { to.mPlayFabId; }) to.mPlayFabId = from.mPlayFabId.get();
}

std::filesystem::path ClosetFile()
{
    wchar_t base[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(base) / L"MyClient" / L"closet_skin.bin";
}

std::string ClosetSkinId(MyClientSkin::Request const& request)
{
    std::vector<uint8_t> pixels(request.rgba, request.rgba + static_cast<size_t>(request.width) * request.height * 4);
    char id[48];
    std::snprintf(id, sizeof(id), "MyClient_%08x%s", MyClientSkin::Hash(pixels, request.width, request.height),
                  request.slim ? "_slim" : "");
    return std::string(id) + (request.capeId ? "_c" + std::to_string(request.capeId) : "");
}

// Resolve independent skin/cape edits into one immutable classic appearance snapshot.
// Never flatten a persona/custom geometry into a humanoid just to attach a cape.
bool PrepareClosetRequest(MyClientSkin::Request& request, SerializedSkinImpl* source)
{
    if (request.operation > static_cast<uint32_t>(MyClientSkin::Operation::Cape) || !MyClientSkin::KnownCape(request.capeId)) return false;
    const bool capeEdit = request.operation == static_cast<uint32_t>(MyClientSkin::Operation::Cape);
    const uint32_t requestedCape = request.capeId;
    std::vector<uint8_t> cape;
    if (capeEdit && requestedCape) cape.assign(request.capeRgba, request.capeRgba + MyClientSkin::CapeBytes);
    if (capeEdit && closetHaveKept) {
        request = closetKept;
    } else if (capeEdit || (!request.width && closetHaveKept && closetKept.capeId)) {
        const auto original = closetOriginal;
        if (original) source = &original->mObject;
        if (capeEdit && !requestedCape && !closetHaveKept) {
            request.width = request.height = request.skinOverride = request.capeId = 0;
        } else {
            if (!source || !MyClientSkin::ClassicLayout(source->mIsPersona, source->mDefaultGeometryName.get(), source->mResourcePatch.get())) return false;
            const auto& image = static_cast<mce::Image const&>(source->mSkinImage.get());
            const auto& bytes = image.mImageBytes;
            std::vector<uint8_t> pixels;
            if (!MyClientSkin::ToRgba(static_cast<uint32_t>(image.imageFormat), bytes.data(), bytes.size(), image.mWidth, image.mHeight, pixels)) return false;
            request.width = image.mWidth;
            request.height = image.mHeight;
            request.slim = source->mArmSizeType == SharedTypes::persona::ArmSizeType::Slim ? 1 : 0;
            request.skinOverride = 0;
            std::memcpy(request.rgba, pixels.data(), pixels.size());
            std::snprintf(request.name, sizeof(request.name), "%s", "Account skin");
        }
    }
    if (capeEdit) {
        request.capeId = requestedCape;
        if (requestedCape) std::memcpy(request.capeRgba, cape.data(), cape.size());
        if (!request.skinOverride && !requestedCape) request.width = request.height = 0;
    } else if (closetHaveKept) {
        request.capeId = closetKept.capeId;
        std::memcpy(request.capeRgba, closetKept.capeRgba, MyClientSkin::CapeBytes);
    }
    request.operation = static_cast<uint32_t>(MyClientSkin::Operation::Skin);
    return (!request.width && !request.height) || MyClientSkin::UsableRequest(request);
}

void KeepClosetSkin(MyClientSkin::Request const& request, MyClientLog::Logger& log)
{
    ResetClosetReapply(); // an explicit user choice gets a fresh budget; native updates do not
    {
        std::lock_guard lock(closetWornMutex);
        closetPreviewRequest = std::make_shared<MyClientSkin::Request>(request);
    }
    std::memcpy(&closetKept, &request, sizeof(closetKept));
    closetKept.name[sizeof(closetKept.name) - 1] = 0;
    closetHaveKept = true;
    closetKeptId = ClosetSkinId(closetKept);
    skinRequests.PublishCapeChoice(GetCurrentProcessId(), closetKept.capeId);
    SkinChangedForDolls(); // publish the new menu snapshot before its generation becomes visible
    std::error_code ec;
    const auto file = ClosetFile();
    if (file.empty()) return;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&closetKept), offsetof(MyClientSkin::Request, rgba));
    out.write(reinterpret_cast<const char*>(closetKept.rgba), static_cast<std::streamsize>(closetKept.width) * closetKept.height * 4);
    out.write(reinterpret_cast<const char*>(&closetKept.operation), sizeof(closetKept) - offsetof(MyClientSkin::Request, operation));
    if (!out) log.warn("Closet: the skin could not be saved for the next game start ({}).", file.string());
}

void ForgetClosetSkin()
{
    closetHaveKept = false;
    closetKeptId.clear();
    skinRequests.PublishCapeChoice(GetCurrentProcessId(), 0);
    {
        std::lock_guard lock(closetWornMutex);
        closetWornObject.reset();
        closetPreviewRequest.reset();
    }
    SkinChangedForDolls();
    std::error_code ec;
    const auto file = ClosetFile();
    if (!file.empty()) std::filesystem::remove(file, ec);
}

void LoadClosetSkin(MyClientLog::Logger& log)
{
    const auto file = ClosetFile();
    if (file.empty()) return;
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    static MyClientSkin::Request saved;
    std::memset(&saved, 0, sizeof(saved));
    in.read(reinterpret_cast<char*>(&saved), offsetof(MyClientSkin::Request, rgba));
    if (!in || saved.magic != MyClientSkin::RequestMagic || (saved.version != 1 && saved.version != 2 && saved.version != MyClientSkin::RequestVersion) ||
        !MyClientSkin::UsableRequest(saved))
        return;
    in.read(reinterpret_cast<char*>(saved.rgba), static_cast<std::streamsize>(saved.width) * saved.height * 4);
    if (!in) return;
    if (saved.version == 1) saved.skinOverride = 1;
    else if (saved.version == 2) {
        // Before HD capes: the four numbers, then a 64x32 cape.
        in.read(reinterpret_cast<char*>(&saved.operation), offsetof(MyClientSkin::Request, capeRgba) - offsetof(MyClientSkin::Request, operation));
        static uint8_t classic[MyClientSkin::ClassicCapeBytes];
        in.read(reinterpret_cast<char*>(classic), sizeof(classic));
        if (!in || !MyClientSkin::UsableRequest(saved) || saved.operation || saved.skinOverride > 1) return;
        MyClientSkin::UpscaleClassicCape(classic, saved.capeRgba);
    }
    else {
        in.read(reinterpret_cast<char*>(&saved.operation), sizeof(saved) - offsetof(MyClientSkin::Request, operation));
        if (!in || !MyClientSkin::UsableRequest(saved) || saved.operation || saved.skinOverride > 1) return;
    }
    saved.version = MyClientSkin::RequestVersion;
    saved.size = sizeof(saved);
    saved.name[sizeof(saved.name) - 1] = 0;
    std::memcpy(&closetKept, &saved, sizeof(closetKept));
    closetHaveKept = true;
    closetKeptId = ClosetSkinId(closetKept);
    skinRequests.PublishCapeChoice(GetCurrentProcessId(), closetKept.capeId);
    {
        std::lock_guard lock(closetWornMutex);
        closetPreviewRequest = std::make_shared<MyClientSkin::Request>(closetKept);
    }
    log.info("Closet: \"{}\" ({}x{}) is put on again in every world (saved; \"My own skin\" takes it off).",
             closetKept.name, closetKept.width, closetKept.height);
}

bool HookPlayerUpdateSkin();

void TakeTitleSkinRequest()
{
    // Never compete with world sampling or hold up rendering while a callback is in progress.
    std::unique_lock lock(callbackMutex, std::try_to_lock);
    if (!lock.owns_lock() || !enabled.load() || !mod) return;
    static MyClientSkin::Request request;
    const DWORD pid = GetCurrentProcessId();
    if (!skinRequests.TakePending(pid, request)) return;
    try {
        const uint32_t serial = request.serial;
        SkinObject source;
        {
            std::lock_guard seen(dollSeenMutex);
            for (const auto& [renderer, state] : menuDollStates)
                if (state && state->accountSkin) { source = state->accountSkin; break; }
        }
        if (!PrepareClosetRequest(request, source ? &source->mObject : nullptr)) {
            skinRequests.Finish(pid, serial, MyClientSkin::Result::Unsupported);
            return;
        }
        request.serial = serial;
        if (request.width == 0 && request.height == 0) {
            ForgetClosetSkin();
            skinRequests.Finish(pid, request.serial, MyClientSkin::Result::Saved);
        } else if (MyClientSkin::UsableRequest(request)) {
            KeepClosetSkin(request, MyClientLog::Get());
            SkinChangedForDolls();
            skinRequests.Finish(pid, request.serial, MyClientSkin::Result::Saved);
        } else {
            skinRequests.Finish(pid, request.serial, MyClientSkin::Result::Failed);
        }
    } catch (...) {
        skinRequests.Finish(pid, request.serial, MyClientSkin::Result::Failed);
        MyClientLog::Get().warn("Closet: title skin request could not be saved.");
    }
}

void HookClientPacketHandler(MyClientLog::Logger& log)
{
    {
#if MYCLIENT_DOLL_SKIN_FROM_PLAYER
        const bool doll = MyClientGame::Target(DollSkinSymbol) && DollSkinHook::hook() == 0;
#else
        const bool doll = false;  // no getSkinFromPlayer in this game version
#endif
        const bool build = MyClientGame::Target(DollBuildSymbol) && DollBuildHook::hook() == 0;
        const bool ready = DollReadyTarget() && DollReadyHook::hook() == 0;
        dollReadyHooked.store(ready);
        log.info("Closet: own doll readiness hook={}", ready);
        dollUpdateHooked = MyClientGame::Target(DollUpdateSymbol) && DollUpdateHook::hook() == 0;
        log.info("Closet: pause screen doll hook={} picture hook={} update hook={} (dirty={} context table={}; without it the table "
                 "is found from markLocalPlayerAsDirty)", doll, build, dollUpdateHooked, DollDirtyFunction() != nullptr,
                 DollContextTable() != nullptr);
    }
    {
        const bool canUse = MyClientGame::Target(CanUseSkinSymbol) && CanUseSkinHook::hook() == 0;
        const bool keep = MyClientGame::Target(KeepSkinSymbol) && KeepSkinHook::hook() == 0;
        log.info("Closet: skin checks hooked canUseSkin={} keepByMemory={}", canUse, keep);
        log.info("Closet: observe native skin changes and restore after player registration: hook={}", HookPlayerUpdateSkin());
    }
    (void)HandlerStartGameHook::_AutoHookCount; (void)HandlerActorDataHook::_AutoHookCount;
    (void)HandlerMoveDeltaHook::_AutoHookCount; (void)HandlerSetTimeHook::_AutoHookCount;
    const int hooked = (HandlerStartGameHook::hook() == 0) + (HandlerActorDataHook::hook() == 0) +
                       (HandlerMoveDeltaHook::hook() == 0) + (HandlerSetTimeHook::hook() == 0);
    log.info("Closet: client packet handler hooks {}/4", hooked);
}

// Hands `packet` to the client's own PlayerSkinPacket handler. False when the handler was not seen yet.
bool HandleSkinLocally(PlayerSkinPacket const& packet)
{
    using HandleFn = void (*)(void*, NetworkIdentifier const&, PlayerSkinPacket const&);
    static const auto handle = MyClientGame::ResolveFunction<HandleFn>(
        "?handle@LegacyClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVPlayerSkinPacket@@@Z");
    void* handler = clientPacketHandler.load(std::memory_order_relaxed);
    if (!handle || !handler) return false;
    struct Handing {
        Handing() { closetHandingSkin = true; }
        ~Handing() { closetHandingSkin = false; }
    } handing;
    // An empty identifier (zero bytes: no NetherNet id, RakNet type). Not constructed: NetworkIdentifier's
    // constructor needs NetherNet::NetworkID's, which the SDK's import library lacks.
    alignas(NetworkIdentifier) static const unsigned char source[sizeof(NetworkIdentifier)] = {};
    handle(handler, *reinterpret_cast<NetworkIdentifier const*>(source), packet);
    return true;
}

// (PlayerSkinPacket), so every player sees it. The player's own skin is changed in place into a
// classic skin: new picture, the built-in humanoid model (wide or slim arms), persona parts removed.
// A request made outside a world waits here until the next world tick.
// The skin the game draws a player with comes from the world's player list (as PlayerListPacket hands out every
// player's skin), not only from Player::mSkin: both said the new skin in the log while the model kept the old one
// (2026-10-28, also after a rejoin). The list entry of your player is given the new skin object too.
bool PutSkinInPlayerList(Level& level, Player& player, std::shared_ptr<SerializedSkinRef::SerializedSkinWrapper> const& skin)
{
    if (!skin) return false;
    auto& list = const_cast<std::unordered_map<mce::UUID, PlayerListEntry>&>(level.getPlayerList());
    const auto entry = list.find(MyClientGame::UuidOf(player));
    if (entry == list.end()) return false;
    entry->second.mSkin->mSkinImpl = skin;
    return true;
}

// Cape HD diagnosis (TODO "cape HD, option 2"): %LOCALAPPDATA%\\MyClient\\cape_trial_size.txt may hold 64, 128, 256 or 512 (the
// cape's width) so one build can try every size, in any order, without a rebuild. Missing or invalid = 64, as before.
uint32_t CapeTrialWidth()
{
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) return 64;
    std::ifstream in(std::filesystem::path(base) / L"MyClient" / L"cape_trial_size.txt");
    unsigned value = 0;
    if (!(in >> value) || (value != 64 && value != 128 && value != 256 && value != 512)) return 64;
    return value;
}
std::atomic<unsigned> capeFillSerial{0};   // which cape fill of this run a log line belongs to

// The Closet's skin written into a skin object: `old` is the skin it replaces (cape and account data are kept from it).
// `own` = impl is a skin object of its own (not old itself). id gets the skin's MyClient_ id.
void FillClosetSkin(SerializedSkinImpl& impl, SerializedSkinImpl& old, bool own, MyClientSkin::Request const& request, char (&id)[48])
{
    const uint32_t w = request.width, h = request.height;
    const bool slim = request.slim != 0;
    const bool wasPersona = old.mIsPersona;
    mce::Image image(old.mSkinImage.get());   // keeps the usage flags
    image.imageFormat = mce::ImageFormat::RGBA8Unorm;
    image.mWidth = w;
    image.mHeight = h;
    image.mDepth = 1;
    image.mImageBytes = mce::Blob(request.rgba, static_cast<size_t>(w) * h * 4);
    if (own)
    {
        CopyPlayFabId(impl, old);   // 26.60 dropped the member
        const auto original = closetOriginal;
        const auto& capeSource = !request.capeId && original ? original->mObject : old;
        impl.mCapeId = capeSource.mCapeId.get();
        mce::Image cape(capeSource.mCapeImage.get());
        if (request.capeId) {
            // r57-test37: the 512x256 cape was never drawn, 64x32 was. r57-test38 trial: one size per cape to find the
            // largest the game draws (Founder 64x32, Member 128x64, Patron 256x128, Creator and others 512x256).
            // r57-test38 result (user): 64x32 and 128x64 are drawn, 256x128 and 512x256 are not. Every cape is 128x64.
            // r57-test39: at 128x64 no cape was drawn (test38's Member showed only after a 64x32 one). 64x32 until the
            // size limit is found (TODO: cape HD, option 2).
            const uint32_t outW = CapeTrialWidth();
            const uint32_t outH = outW / 2;
            static std::vector<uint8_t> scaled;
            scaled.assign(static_cast<size_t>(outW) * outH * 4, 0);
            const uint32_t sx = MyClientSkin::CapeWidth / outW, sy = MyClientSkin::CapeHeight / outH;
            for (uint32_t y = 0; y < outH; ++y)
                for (uint32_t x = 0; x < outW; ++x)
                    for (uint32_t c = 0; c < 4; ++c) {
                        uint32_t sum = 0;
                        for (uint32_t dy = 0; dy < sy; ++dy)
                            for (uint32_t dx = 0; dx < sx; ++dx)
                                sum += request.capeRgba[((y * sy + dy) * MyClientSkin::CapeWidth + (x * sx + dx)) * 4 + c];
                        scaled[(static_cast<size_t>(y) * outW + x) * 4 + c] = static_cast<uint8_t>(sum / (sx * sy));
                    }
            cape.imageFormat = mce::ImageFormat::RGBA8Unorm;
            cape.mWidth = outW;
            cape.mHeight = outH;
            cape.mDepth = 1;
            cape.mImageBytes = mce::Blob(scaled.data(), scaled.size());
            impl.mCapeId = std::string("OriginX_trial_cape_") + std::to_string(request.capeId);
            MyClientLog::Get().info("Capes fill #{}: cape {} written as {}x{} (trial size file; skin cape image replaced)",
                                    capeFillSerial.fetch_add(1) + 1, request.capeId, outW, outH);
        }
        static_cast<mce::Image&>(impl.mCapeImage.get()) = std::move(cape);
        impl.mSkinColor = old.mSkinColor.get();
        impl.mIsPrimaryUser = static_cast<bool>(old.mIsPrimaryUser);
        impl.mAnimationData = std::string{};
    }
    static_cast<mce::Image&>(impl.mSkinImage.get()) = std::move(image);

    const std::string instance = MyClientSkin::InstanceId(ClosetSkinId(request), skinInstanceSerial.fetch_add(1));
    std::snprintf(id, sizeof(id), "%s", instance.c_str());
    impl.mId = std::string(id);
    impl.mFullId = std::string(id) + impl.mCapeId.get();
    impl.mResourcePatch = std::string(MyClientSkin::ResourcePatch(slim));
    impl.mDefaultGeometryName = std::string(MyClientSkin::NativeGeometryName(slim));
    impl.mArmSizeType = slim ? SharedTypes::persona::ArmSizeType::Slim : SharedTypes::persona::ArmSizeType::Wide;
    impl.mSkinAnimatedImages->clear();   // a persona's blinking eyes would cover the new face
    impl.mPersonaPieces->clear();
    impl.mPieceTintColors->clear();
    impl.mIsPersonaCapeOnClassicSkin = wasPersona && !impl.mCapeId->empty();
    impl.mIsPersona = false;
    impl.mIsPremium = false;
    impl.mIsTrustedSkin = TrustedSkinFlag::True;   // an untrusted skin may be drawn as a default one
}

// Serialized input only: neither a cape id nor a ready skin proves a dedicated GPU cape registration.
void NoteCapeRenderInput(const char* who, const SerializedSkinImpl* skin)
{
    if (!mod) return;
    if (!skin) {
        MyClientLog::Get().info("Capes render input: who={} skinPresent=false nativeCapeTexture=unknown nativeCapeGeometry=unknown", who);
        return;
    }
    const auto& image = skin->mCapeImage.get();
    MyClientLog::Get().info("Capes render input: who={} skinPresent=true capeIdPresent={} originXCape={} width={} height={} format={} bytes={} capePatchDeclared={} fill#={} nativeCapeTexture=unknown nativeCapeGeometry=unknown (serialized data only)",
        who, !skin->mCapeId->empty(), MyClientCapes::IsOriginXCape(skin->mCapeId.get()), image.mWidth, image.mHeight,
        static_cast<uint32_t>(image.imageFormat), image.mImageBytes.size(), skin->mResourcePatch->find("\"cape\"") != std::string::npos, capeFillSerial.load());
}

void NoteClosetWorld(Player& player);
std::atomic<bool> closetReapplyWanted{false};
bool closetSentThisWorld = false;   // the Closet's skin was sent to the server in this world

// Let the native account/packet transaction finish with its original ref. Mutating that const ref in place
// made the caller's texture/model bookkeeping disagree with Player::mSkin after returning. Reapply the
// saved choice from the world tick only after native player-list registration has completed.
MYCLIENT_TYPE_INSTANCE_HOOK(PlayerUpdateSkinHook, ll::memory::HookPriority::Normal, Player,
                      MyClientGame::Target("?updateSkin@Player@@QEAAXAEBVSerializedSkinRef@@H@Z"), void,
                      SerializedSkinRef const& skin, int clientSubId)
{
    if (!this) return;
    const auto nativeCaller = _ReturnAddress();
    std::shared_ptr<PlayerSkinPacket> capeEcho; // independent native object; never mutate the const input
    if (!closetApplyDepth && mod) {
        try {
            std::shared_ptr<const MyClientSkin::Request> desired;
            { std::lock_guard lock(closetWornMutex); desired = closetPreviewRequest; }
            const auto client = MyClientGame::Client();
            auto* local = client ? client->getLocalPlayer() : nullptr;
            const auto incoming = skin.mSkinImpl;
            if (local && static_cast<Player*>(local) == this && desired && incoming &&
                incoming->mObject.mCapeId->empty() && !incoming->mObject.mIsPersona) {
                // r57-test24: the pixel comparison never matched (no "echo matched" line while the cape-less copy
                // came back 5 times). The copy carries the Closet's own generated id ("MyClient_..."), which only the
                // Closet makes, so that id with no cape while a cape is wanted is the echo.
                // r57-test27: replacing the echo with a freshly made skin left the player transparent (the new
                // object's picture never reached the renderer) and still no cape. The echo is now let through; the
                // watcher below queues the normal reapply, which puts the cape on locally and is not sent to the
                // server again within 16 s (so no new echo comes back).
                // 2026-11-19 (test58, author: the cape flickers and is gone): letting the echo through took the cape
                // off about 50 ms after it was put on, and the reapply that followed got the next echo, over and
                // over. When this player already wears the same generated skin with the cape, the echo is dropped
                // (it carries the same picture, only without the cape), so the cape stays on.
                const auto* wornNow = this->mSkin ? this->mSkin->mSkinImpl.get() : nullptr;
                if (desired->capeId != 0 && incoming->mObject.mId->rfind("MyClient_", 0) == 0 && wornNow &&
                    wornNow != incoming.get() && !wornNow->mObject.mCapeId->empty() &&
                    wornNow->mObject.mId.get() == incoming->mObject.mId.get()) {
                    static ULONGLONG dropLogAt = 0;
                    if (GetTickCount64() - dropLogAt > 10000) {
                        dropLogAt = GetTickCount64();
                        MyClientLog::Get().info("Closet: cape-less echo of our skin dropped; the worn skin keeps its cape");
                    }
                    return;
                } else if (desired->capeId != 0 && incoming->mObject.mId->rfind("MyClient_", 0) == 0) {
                    static ULONGLONG echoLogAt = 0;
                    if (GetTickCount64() - echoLogAt > 10000) {
                        echoLogAt = GetTickCount64();
                        MyClientLog::Get().info("Closet: cape-less echo of our skin seen; local reapply without a server send");
                    }
                } else if (false) {
                    using Factory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
                    static const auto create = MyClientGame::ResolveFunction<Factory>("?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z");
                    if (create) {
                        if (auto packet = create(MinecraftPacketIds::PlayerSkin))
                            capeEcho = std::static_pointer_cast<PlayerSkinPacket>(packet);
                    }
                    if (capeEcho && capeEcho->mSkin->mSkinImpl && capeEcho->mSkin->mSkinImpl != incoming) {
                        char id[48];
                        FillClosetSkin(capeEcho->mSkin->mSkinImpl->mObject, incoming->mObject, true, *desired, id);
                        HMODULE callerModule = nullptr;
                        if (nativeCaller) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                           reinterpret_cast<LPCWSTR>(nativeCaller), &callerModule);
                        const HMODULE gameModule = GetModuleHandleW(nullptr);
                        MyClientLog::Get().info("Closet: cape-less native echo matched saved picture; private local cape restored (caller={} gameRva={} packet origin unconfirmed)",
                                               nativeCaller, callerModule && callerModule == gameModule ?
                                               reinterpret_cast<uintptr_t>(nativeCaller) - reinterpret_cast<uintptr_t>(gameModule) : 0);
                    } else capeEcho.reset();
                }
            }
        } catch (...) { capeEcho.reset(); }
    }
    if (capeEcho) {
        ClosetApplyScope applying;
        origin(*capeEcho->mSkin, clientSubId);
        std::lock_guard lock(closetWornMutex);
        closetWornObject = this->mSkin ? this->mSkin->mSkinImpl : SkinObject{};
    } else origin(skin, clientSubId);
    if (!mod) return;
    try {
        std::shared_ptr<const MyClientSkin::Request> desired;
        {
            std::lock_guard lock(closetWornMutex);
            desired = closetPreviewRequest;
        }
        if (!desired) return;
        const auto client = MyClientGame::Client();
        auto* local = client ? client->getLocalPlayer() : nullptr;
        const auto* current = this->mSkin ? this->mSkin->mSkinImpl.get() : nullptr;
        if (local && static_cast<Player*>(local) == this && desired->capeId) {
            const auto incoming = skin.mSkinImpl;
            MyClientLog::Get().info("Closet trace: updateSkin returned (deliberate={} inputPresent={} inputCape={} inputOriginXCape={} localPresent={} localCape={} localOriginXCape={} wantedCape={})",
                                   closetApplyDepth != 0, bool(incoming),
                                   incoming && !incoming->mObject.mCapeId->empty(),
                                   incoming && MyClientCapes::IsOriginXCape(incoming->mObject.mCapeId.get()), current != nullptr,
                                   current && !current->mObject.mCapeId->empty(),
                                   current && MyClientCapes::IsOriginXCape(current->mObject.mCapeId.get()), desired->capeId);
        }
        if (closetApplyDepth) return; // diagnostics must not enqueue our deliberate application
        if (local && static_cast<Player*>(local) == this && current &&
            !MyClientSkin::MatchesSelection(current->mObject.mId.get(), ClosetSkinId(*desired))) {
            const auto& observed = current->mObject;
            closetMismatchKind.store((observed.mId->rfind("MyClient_", 0) == 0 ? 1u : 0u) |
                                     (!observed.mCapeId->empty() ? 2u : 0u), std::memory_order_relaxed);
            closetReapplyWanted.store(true);
        }
    } catch (...) {}
}

bool HookPlayerUpdateSkin() { return PlayerUpdateSkinHook::hook() == 0; }

MyClientSkin::Result WearSkin(ClientInstance& client, Level& level, Player& player, MyClientSkin::Request& request,
                              const char* when)
{
    ClosetApplyScope applying; // do not intercept this deliberate choice using the previous saved selection
    auto& log = MyClientLog::Get();
    MyClientSkin::Result result = MyClientSkin::Result::Failed;
    try
    {
        using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
        using UpdateSkinFn = void (*)(Player*, SerializedSkinRef const&, int);
        static const auto createPacket = MyClientGame::ResolveFunction<PacketFactory>("?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z");
        static const auto updateSkin = MyClientGame::ResolveFunction<UpdateSkinFn>("?updateSkin@Player@@QEAAXAEBVSerializedSkinRef@@H@Z");
        auto& ref = player.mSkin;
        auto* owner = ref ? ref->mSkinImpl.get() : nullptr;
        const uint32_t w = request.width, h = request.height;
        request.name[sizeof(request.name) - 1] = 0;
        const std::string name = request.name;
        if (!owner || !MyClientSkin::UsableRequest(request))
        {
            log.warn("Closet: cannot wear \"{}\" ({}x{}): {}", name, w, h, owner ? "not a classic skin size" : "the player has no skin object");
        }
        else
        {
            // The new skin goes into a skin object of its own (the packet's fresh one), not into the player's: the
            // model's picture is kept per skin object, so changing the player's object in place re-made the arms
            // (they are read from it every frame) but never the picture (2026-10-28: only the arm width changed).
            std::shared_ptr<PlayerSkinPacket> skinPacket;
            if (createPacket)
                if (auto packet = createPacket(MinecraftPacketIds::PlayerSkin))
                    skinPacket = std::static_pointer_cast<PlayerSkinPacket>(packet);
            std::shared_ptr<SerializedSkinRef::SerializedSkinWrapper> fresh;
            if (skinPacket) fresh = skinPacket->mSkin->mSkinImpl;
            const bool own = fresh && fresh != ref->mSkinImpl;
            if (!own) {
                log.warn("Closet: no private skin object was available; the player's current skin was kept.");
                LogOwnCapeResult(request, false);
                return MyClientSkin::Result::Failed;
            }
            SerializedSkinImpl& old = owner->mObject;
            SerializedSkinImpl& impl = own ? fresh->mObject : old;
            if (own && old.mId.get().rfind("MyClient_", 0) != 0) closetOriginal = ref->mSkinImpl;   // your own skin, for "My own skin"
            const bool slim = request.slim != 0;
            const bool wasPersona = old.mIsPersona;
            char id[48];
            FillClosetSkin(impl, old, own, request, id);

            const auto wearing = [&] {
                auto* now = ref ? ref->mSkinImpl.get() : nullptr;
                return now && now->mObject.mId.get() == id;
            };
            // Only booleans and the public cape selection are logged: no UUID, skin IDs or image bytes.
            const auto traceCape = [&](const char* phase) {
                if (!request.capeId) return;
                const auto local = ref ? ref->mSkinImpl : SkinObject{};
                log.info("Closet trace: {} (wantedCape={} packetCape={} packetOriginXCape={} localPresent={} localCape={} localOriginXCape={} sharesPacket={} fill#={} packetCapeSize={}x{} localCapeSize={}x{})",
                         phase, request.capeId, !impl.mCapeId->empty(), MyClientCapes::IsOriginXCape(impl.mCapeId.get()), bool(local),
                         local && !local->mObject.mCapeId->empty(),
                         local && MyClientCapes::IsOriginXCape(local->mObject.mCapeId.get()), local == fresh,
                         capeFillSerial.load(), impl.mCapeImage.get().mWidth, impl.mCapeImage.get().mHeight,
                         local ? local->mObject.mCapeImage.get().mWidth : 0u, local ? local->mObject.mCapeImage.get().mHeight : 0u);
            };
            bool sent = false, handled = false, viaHandler = false, viaUpdate = false, listed = false, direct = false;
            closetCanUse.store(-1);
            closetKeep.store(-1);
            if (skinPacket)
            {
                skinPacket->mUUID = MyClientGame::UuidOf(player);
                skinPacket->mSkin->mSkinImpl = own ? fresh : ref->mSkinImpl;
                skinPacket->mLocalizedNewSkinName = name;
                skinPacket->mLocalizedOldSkinName = std::string{};
                if (client.getPacketSender().isInitialized())
                {
                    // OriginX capes are seen by OriginX users only (capes design 2026-11-12): the server gets the skin
                    // with your own (account) cape, and the OriginX cape is put back for this game only below.
                    const uint32_t originxCape = request.capeId;
                    if (originxCape) {
                        char plainId[48];
                        request.capeId = 0;
                        FillClosetSkin(impl, old, own, request, plainId);
                        request.capeId = originxCape;
                    }
                    traceCape("before server send");
                    // Servers limit skin changes (Hive: "Please wait 15 seconds between changing skins", r57-test24).
                    // The same skin again within 16 s (a reapply after the game put another skin on locally) is put
                    // on locally only: the server already has it.
                    static std::string lastSentId;
                    static ULONGLONG lastSentAt = 0;
                    const std::string sentId = impl.mId.get();
                    const ULONGLONG sendNow = GetTickCount64();
                    // r57-test25: the reapply makes a new id each time, so "same id" never held and Hive still
                    // refused; any send within 16 s of the last one is skipped (the server keeps the first).
                    (void)lastSentId;
                    // Only a reapply in the same world is held back (a new world or a choice made in the Closet is sent).
                    const bool reapply = closetReapply.attempts > 1;
                    if (!reapply || lastSentAt == 0 || sendNow - lastSentAt >= 16000) {
                        client.getPacketSender().sendToServer(*skinPacket);
                        lastSentId = sentId;
                        lastSentAt = sendNow;
                    } else if (mod) MyClientLog::Get().info("Closet: a skin was sent {} ms ago; put on locally, not sent to the server again", sendNow - lastSentAt);
                    traceCape("after server send");
                    if (originxCape) FillClosetSkin(impl, old, own, request, id);
                    traceCape("after local cape refill");
                    sent = true;
                    closetSentThisWorld = true;
                }
                try { handled = HandleSkinLocally(*skinPacket); } catch (...) { handled = false; }
                traceCape("after local handler");
                viaHandler = handled && wearing();
                if (!viaHandler && updateSkin)
                {
                    try { updateSkin(&player, *skinPacket->mSkin, 0); } catch (...) {}
                    traceCape("after local updateSkin");
                    viaUpdate = wearing();
                }
                try { listed = PutSkinInPlayerList(level, player, skinPacket->mSkin->mSkinImpl); } catch (...) { listed = false; }
                traceCape("after player list update");
            }
            if (wearing())
            {
                std::lock_guard lock(closetWornMutex);
                closetWornObject = ref->mSkinImpl;
                closetWornUuid = MyClientGame::UuidOf(player);
            }
            if (request.capeId) NoteCapeRenderInput("own", ref && ref->mSkinImpl ? &ref->mSkinImpl->mObject : nullptr);
            SkinChangedForDolls();
            log.info("Closet: wearing \"{}\"{} ({}x{}, {} arms{}){}{}{}{}{}", name, when, w, h, slim ? "slim" : "wide",
                     wasPersona ? ", was a persona skin" : "", sent ? ", sent to the server" : ", NOT sent (no packet sender)",
                     viaHandler ? ", applied by the client's skin handler"
                     : viaUpdate ? ", applied by Player::updateSkin (the skin handler left it)"
                     : handled  ? ", the skin handler and Player::updateSkin did not take it"
                                : ", the client's skin handler was not found",
                     updateSkin ? "" : ", Player::updateSkin not found",
                     wearing() ? (direct ? ", set on the player directly" : "") : ", NOT on the player",
                     listed ? ", put in the player list" : ", NOT in the player list");
            log.info("Closet: the skin handler's checks: canUseSkin={} keepByMemory={}; new skin object={}",
                     SkinCheckText(closetCanUse.load()), SkinCheckText(closetKeep.load()), own);
            result = sent && wearing() ? MyClientSkin::Result::Sent : MyClientSkin::Result::Failed;
        }
    }
    catch (...)
    {
        log.error("Closet: wearing the skin failed with an exception.");
    }
    LogOwnCapeResult(request, result == MyClientSkin::Result::Sent);
    return result;
}

// "My own skin": your own skin object (kept from before the Closet's) goes back on, the same way a skin is worn.
MyClientSkin::Result TakeOffSkin(ClientInstance& client, Level& level, Player& player)
{
    auto& log = MyClientLog::Get();
    ForgetClosetSkin();
    SkinObject original = std::move(closetOriginal);
    closetOriginal.reset();
    auto& ref = player.mSkin;
    if (!original || !ref)
    {
        log.info("Closet: the Closet's skin is off (your own skin was already on in this world).");
        return MyClientSkin::Result::Sent;
    }
    try
    {
        using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
        using UpdateSkinFn = void (*)(Player*, SerializedSkinRef const&, int);
        static const auto createPacket = MyClientGame::ResolveFunction<PacketFactory>("?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z");
        static const auto updateSkin = MyClientGame::ResolveFunction<UpdateSkinFn>("?updateSkin@Player@@QEAAXAEBVSerializedSkinRef@@H@Z");
        std::shared_ptr<PlayerSkinPacket> skinPacket;
        if (createPacket)
            if (auto packet = createPacket(MinecraftPacketIds::PlayerSkin))
                skinPacket = std::static_pointer_cast<PlayerSkinPacket>(packet);
        bool sent = false, listed = false;
        if (skinPacket)
        {
            skinPacket->mUUID = MyClientGame::UuidOf(player);
            skinPacket->mSkin->mSkinImpl = original;
            skinPacket->mLocalizedNewSkinName = std::string{};
            skinPacket->mLocalizedOldSkinName = std::string{};
            if (client.getPacketSender().isInitialized())
            {
                client.getPacketSender().sendToServer(*skinPacket);
                sent = true;
            }
            try { HandleSkinLocally(*skinPacket); } catch (...) {}
            if (ref->mSkinImpl != original && updateSkin)
                try { updateSkin(&player, *skinPacket->mSkin, 0); } catch (...) {}
        }
        if (ref->mSkinImpl.get() == nullptr || ref->mSkinImpl->mObject.mId.get().rfind("MyClient_", 0) == 0) ref->mSkinImpl = original;
        try { listed = PutSkinInPlayerList(level, player, ref->mSkinImpl); } catch (...) { listed = false; }
        log.info("Closet: your own skin is back on{}{}", sent ? ", sent to the server" : ", NOT sent (no packet sender)",
                 listed ? ", put in the player list" : ", NOT in the player list");
        return sent ? MyClientSkin::Result::Sent : MyClientSkin::Result::Failed;
    }
    catch (...)
    {
        log.error("Closet: putting your own skin back failed with an exception.");
        return MyClientSkin::Result::Failed;
    }
}

// --- OriginX capes on the other OriginX users in your world (capes design 2026-11-12, CapeChannel.h) ------------
// Once a second: the XUIDs of the players in the player list go to the HUD, which asks the Worker which capes they
// wear; a player whose cape changed gets their own skin again with that cape, handed to this game's own skin packet
// handler only (HandleSkinLocally): nothing is sent to the server, so only you see it. Persona skins and skins of a
// size the Closet cannot hold are left as they are. The pictures are the HUD's (%LOCALAPPDATA%\MyClient\capes\<id>.rgba).
MyClientCapes::Channel capeChannel;
std::unordered_map<std::string, uint32_t> capesApplied;   // xuid -> cape put on in this world
const Level* capesLevel = nullptr;

const std::vector<uint8_t>* CapePicture(uint32_t id)
{
    static std::vector<uint8_t> pictures[7];
    if (id < 1 || id > 6) return nullptr;
    auto& picture = pictures[id];
    if (picture.size() != MyClientSkin::CapeBytes) {
        wchar_t base[MAX_PATH]{};
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        if (!n || n >= MAX_PATH) return nullptr;
        std::ifstream in(std::filesystem::path(base) / L"MyClient" / L"capes" / (std::to_wstring(id) + L".rgba"), std::ios::binary);
        std::vector<uint8_t> bytes(MyClientSkin::CapeBytes);
        if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return nullptr;
        picture = std::move(bytes);
    }
    return &picture;
}

bool PutCapeOn(Level& level, PlayerListEntry& entry, uint32_t cape)
{
    using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
    static const auto createPacket = MyClientGame::ResolveFunction<PacketFactory>("?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z");
    const auto* picture = CapePicture(cape);
    if (!createPacket || !picture) return false;
    auto* owner = entry.mSkin->mSkinImpl.get();   // mSkin is held by value; its skin object may be null (checked)
    if (!owner) return false;
    SerializedSkinImpl& old = owner->mObject;
    if (old.mIsPersona) return false;
    auto const& skin = reinterpret_cast<mce::Image const&>(old.mSkinImage);
    auto const& bytes = skin.mImageBytes;
    const uint32_t w = skin.mWidth, h = skin.mHeight;
    if (!MyClientCapes::UsableSkinSize(w, h)) return false;
    static std::vector<uint8_t> rgba;
    if (!MyClientSkin::ToRgba(static_cast<uint32_t>(skin.imageFormat), bytes.data(), bytes.size(), w, h, rgba) ||
        rgba.size() < static_cast<size_t>(w) * h * 4) return false;
    static MyClientSkin::Request request;   // ~1 MB: not on the stack
    std::memset(&request, 0, sizeof(request));
    request.width = w; request.height = h;
    const std::string& geometry = old.mDefaultGeometryName;
    request.slim = geometry.find("Slim") != std::string::npos || geometry.find("slim") != std::string::npos;
    std::memcpy(request.rgba, rgba.data(), static_cast<size_t>(w) * h * 4);
    std::snprintf(request.name, sizeof(request.name), "OriginX cape");
    request.capeId = cape;
    std::memcpy(request.capeRgba, picture->data(), MyClientSkin::CapeBytes);
    const auto packet = std::static_pointer_cast<PlayerSkinPacket>(createPacket(MinecraftPacketIds::PlayerSkin));
    if (!packet) return false;
    const auto fresh = packet->mSkin->mSkinImpl;
    if (!fresh || fresh == entry.mSkin->mSkinImpl) return false;
    char id[48];
    FillClosetSkin(fresh->mObject, old, true, request, id);
    packet->mUUID = entry.mUUID.get();
    packet->mLocalizedNewSkinName = std::string{};
    packet->mLocalizedOldSkinName = std::string{};
    const auto uuid = entry.mUUID.get();
    const bool handled = HandleSkinLocally(*packet); // this game only, never sendToServer
    // The handler can rebuild the player list: reacquire by UUID instead of retaining entry across it.
    const auto& list = level.getPlayerList();
    const auto applied = list.find(uuid);
    const auto object = applied != list.end() ? applied->second.mSkin->mSkinImpl : SkinObject{};
    NoteCapeRenderInput("other", object ? &object->mObject : nullptr);
    return handled;
}

void PublishOriginMarks(const OriginMarks::Table& table, uint64_t now)
{
    OriginMarks::Publish(table);
    static unsigned lastCount = 0;
    static uint64_t lastLog = 0;
    const auto alloc = OriginMarks::statMarkAlloc.load(std::memory_order_relaxed);
    const unsigned count = table.count + (table.self.tier != 0);
    if (count != lastCount || (count && now - lastLog >= 30000)) {
        MyClientLog::Get().info("Origin marks: marks={} alloc={}", count, alloc);
        lastCount = count; lastLog = now;
    }
}

// Cape HD diagnosis (TODO "マント高画質（案 2）"): 26.51's query.has_cape reads player+0xAB0 -> [0] -> +0xF8 (non-zero =
// has a cape). Logs that slot and its neighbours when they change (and every 30 s), so a 64x32 cape and a 128x64 one
// can be compared: whether the game keeps a cape object at all for the larger size. 26.51 only (offsets from its image).
#ifndef MYCLIENT_GAME_VERSION
#define MYCLIENT_GAME_VERSION "1.26.51"
#endif
void LogNativeCapeSlot(Player& player, uint64_t now)
{
    if (std::string_view(MYCLIENT_GAME_VERSION) != "1.26.51") return;
    static uint64_t lastLog = 0;
    static uintptr_t lastSlot[6]{};
    static bool lastOk = false;
    const auto* bytes = reinterpret_cast<const char*>(&player);
    uintptr_t holder = 0, object = 0, slot[6]{};
    bool ok = ReadableBytes(bytes + 0xAB0, 8);
    if (ok) { std::memcpy(&holder, bytes + 0xAB0, 8); ok = ReadableBytes(reinterpret_cast<const void*>(holder), 8); }
    if (ok) { std::memcpy(&object, reinterpret_cast<const void*>(holder), 8); ok = ReadableBytes(reinterpret_cast<const char*>(object) + 0xE8, 6 * 8); }
    if (ok) std::memcpy(slot, reinterpret_cast<const char*>(object) + 0xE8, sizeof slot);
    const bool changed = ok != lastOk || std::memcmp(slot, lastSlot, sizeof slot) != 0;
    if (!changed && now - lastLog < 30000) return;
    lastLog = now;
    lastOk = ok;
    std::memcpy(lastSlot, slot, sizeof slot);
    if (!ok) { MyClientLog::Get().info("Capes native slot: unreadable (holder={:#x} object={:#x})", holder, object); return; }
    // +0xF8 (has_cape) and, if it looks like a pointer, the first 4 qwords it points at.
    uintptr_t inner[4]{};
    const bool innerOk = slot[2] && ReadableBytes(reinterpret_cast<const void*>(slot[2]), sizeof inner);
    if (innerOk) std::memcpy(inner, reinterpret_cast<const void*>(slot[2]), sizeof inner);
    MyClientLog::Get().info("Capes native slot: +E8={:#x} +F0={:#x} +F8={:#x} (has_cape={}) +100={:#x} +108={:#x} +110={:#x} "
                            "[+F8]={} {:#x} {:#x} {:#x} {:#x} fill#={}",
                            slot[0], slot[1], slot[2], slot[2] != 0, slot[3], slot[4], slot[5],
                            innerOk ? "read" : "none", inner[0], inner[1], inner[2], inner[3], capeFillSerial.load());
}

void TickCapes(Level& level, Player& player)
{
    static uint64_t last = 0;
    const uint64_t now = GetTickCount64();
    if (now - last < 1000) return;
    last = now;
    LogNativeCapeSlot(player, now);
    if (capesLevel != &level) { capesApplied.clear(); capesLevel = &level; }
    const DWORD pid = GetCurrentProcessId();
    if (!capeChannel.Data() && !capeChannel.Open(pid, true)) { PublishOriginMarks({}, now); return; }
    auto& list = const_cast<std::unordered_map<mce::UUID, PlayerListEntry>&>(level.getPlayerList());
    const mce::UUID self = MyClientGame::UuidOf(player);
    std::string own;
    std::vector<PlayerListEntry*> others;
    for (auto& [uuid, entry] : list) {
        if (uuid == self) { own = entry.mXUID.get(); continue; }
        if (others.size() < MyClientCapes::MaxPlayers && !entry.mXUID->empty()) others.push_back(&entry);
    }
    std::vector<uint32_t> wanted(others.size(), 0);
    if (!capeChannel.Lock()) { PublishOriginMarks({}, now); return; }
    auto& shared = *capeChannel.Data();
    OriginMarks::Table marks;
    if (!list.empty() && MyClientCapes::Valid(shared, pid) && MyClientCapes::AnswerFresh(shared.hudAt, now)) {
        const auto fill = [](OriginMarks::Row& row, std::string_view name, uint32_t tier) {
            if (!tier || tier > 7 || name.empty() || name.size() >= OriginMarks::NameMax) return false;
            std::memcpy(row.name, name.data(), name.size());
            row.tier = static_cast<uint8_t>(tier);
            return true;
        };
        if (!own.empty() && own == shared.ownXuid) fill(marks.self, player.mName.get(), shared.ownTier);
        if (shared.showMarks) {
            for (const auto* entry : others) {
                for (uint32_t i = 0; i < shared.count; ++i) {
                    if (entry->mXUID.get() != shared.xuids[i]) continue;
                    if (marks.count < OriginMarks::MaxUsers &&
                        fill(marks.rows[marks.count], entry->mName.get(), shared.flags[i] & 15)) ++marks.count;
                    break;
                }
            }
        }
    }
    // The HUD's answers belong to the list as it was: read them before the list is written again.
    // Kept between ticks (review I6): a HUD answer stays good for AnswerKeepMs, so a slow HUD loop does not take every
    // cape off and put it back again.
    static std::unordered_map<std::string, uint32_t> answered;
    static uint64_t answeredAt = 0;
    if (MyClientCapes::Valid(shared, pid) && MyClientCapes::AnswerFresh(shared.hudAt, now) && shared.hudAt != answeredAt) {
        answeredAt = shared.hudAt;
        for (uint32_t i = 0; i < shared.count; ++i) answered[shared.xuids[i]] = shared.capes[i];
    }
    if (!MyClientCapes::AnswerFresh(answeredAt, now)) answered.clear();
    if (own != shared.ownXuid) shared.ownTier = 0;
    MyClientCapes::CopyXuid(shared.ownXuid, own);
    uint8_t reorderedFlags[MyClientCapes::MaxPlayers]{};
    if (MyClientCapes::Valid(shared, pid) && MyClientCapes::AnswerFresh(shared.hudAt, now)) {
        for (size_t j = 0; j < others.size(); ++j)
            for (uint32_t i = 0; i < shared.count; ++i)
                if (others[j]->mXUID.get() == shared.xuids[i]) { reorderedFlags[j] = shared.flags[i]; break; }
    }
    std::memcpy(shared.flags, reorderedFlags, sizeof shared.flags);
    shared.count = static_cast<uint32_t>(others.size());
    for (size_t i = 0; i < others.size(); ++i) {
        MyClientCapes::CopyXuid(shared.xuids[i], others[i]->mXUID.get());
        const auto it = answered.find(shared.xuids[i]);
        shared.capes[i] = it == answered.end() ? 0 : static_cast<uint8_t>(it->second);
        wanted[i] = shared.capes[i];
    }
    shared.dllAt = now;
    capeChannel.Unlock();
    PublishOriginMarks(marks, now);
    std::vector<uint32_t> applied(others.size(), 0);
    for (size_t i = 0; i < others.size(); ++i) {
        const auto it = capesApplied.find(others[i]->mXUID.get());
        applied[i] = it == capesApplied.end() ? 0 : it->second;
    }
    for (size_t i : MyClientCapes::NeedsApply(wanted, applied)) {
        if (!wanted[i]) { capesApplied.erase(others[i]->mXUID.get()); continue; }   // the server's skin comes back with their next skin change
        bool ok = false;
        try { ok = PutCapeOn(level, *others[i], wanted[i]); } catch (...) { ok = false; }
        capesApplied[others[i]->mXUID.get()] = wanted[i];   // tried once per change, whatever the result
        MyClientLog::Get().info("Capes: other player's cape {} local handler {} (visual result unconfirmed)",
                               wanted[i], ok ? "accepted" : "failed (persona, unusual skin, or unavailable resources)");
    }
}

// After the texture packs were reloaded in a world your arms (first person) were drawn transparent (2026-11-03): the
// reload dropped the picture made from your skin and nothing asked for it again. Your skin is handed to the game's
// skin handler again, locally (nothing is sent to the server). The handler skips a skin the player already wears,
// so for that moment the player holds an empty skin object (the packet's own), then the handler or
// Player::updateSkin puts yours back and makes its picture again.
void RefreshOwnSkinLook(Level& level, Player& player)
{
    if (!mod) return;
    auto& log = MyClientLog::Get();
    try
    {
        using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
        using UpdateSkinFn = void (*)(Player*, SerializedSkinRef const&, int);
        static const auto createPacket = MyClientGame::ResolveFunction<PacketFactory>("?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z");
        static const auto updateSkin = MyClientGame::ResolveFunction<UpdateSkinFn>("?updateSkin@Player@@QEAAXAEBVSerializedSkinRef@@H@Z");
        auto& ref = player.mSkin;
        if (!ref || !createPacket) return;
        const SkinObject current = ref->mSkinImpl;
        if (!current) return;
        std::shared_ptr<PlayerSkinPacket> skinPacket;
        if (auto packet = createPacket(MinecraftPacketIds::PlayerSkin))
            skinPacket = std::static_pointer_cast<PlayerSkinPacket>(packet);
        if (!skinPacket) return;
        const SkinObject blank = skinPacket->mSkin->mSkinImpl;
        if (!blank || blank == current) return;
        ref->mSkinImpl = blank;
        skinPacket->mUUID = MyClientGame::UuidOf(player);
        skinPacket->mSkin->mSkinImpl = current;
        skinPacket->mLocalizedNewSkinName = std::string{};
        skinPacket->mLocalizedOldSkinName = std::string{};
        bool handled = false, viaUpdate = false;
        try { handled = HandleSkinLocally(*skinPacket); } catch (...) { handled = false; }
        const bool viaHandler = handled && ref->mSkinImpl == current;
        if (!viaHandler && updateSkin)
        {
            try { updateSkin(&player, *skinPacket->mSkin, 0); } catch (...) {}
            viaUpdate = ref->mSkinImpl == current;
        }
        if (ref->mSkinImpl != current) ref->mSkinImpl = current;   // never leave the empty one on
        bool listed = false;
        try { listed = PutSkinInPlayerList(level, player, current); } catch (...) { listed = false; }
        SkinChangedForDolls();
        log.info("Texture packs: your skin was handed to the game again after the reload ({}{})",
                 viaHandler ? "by the skin handler" : viaUpdate ? "by Player::updateSkin" : "neither took it, put back as it was",
                 listed ? ", in the player list" : "");
    }
    catch (...)
    {
        log.warn("Texture packs: handing your skin to the game again failed with an exception.");
    }
}

// A new world (or a new player object): the Closet starts over there. Called before the request is looked at.
void NoteClosetWorld(Player& player)
{
    static const void* seen = nullptr;
    if (!closetNewWorld.exchange(false) && seen == &player) return;
    seen = &player;
    closetOriginal.reset();
    closetWorldSince = GetTickCount64();
    ResetClosetReapply();
    closetSentThisWorld = false;
    closetWaitingForList.store(false);
    std::lock_guard lock(closetWornMutex);
    closetWornObject.reset();
}

void WearRequestedSkin(ClientInstance& client, Level& level, Player& player)
{
    if (!LocalSkinListReady(level, player)) return; // leave the request pending until native registration exists
    static MyClientSkin::Request request;   // ~256 KB: not on the stack
    const DWORD pid = GetCurrentProcessId();
    if (!mod || !skinRequests.TakePending(pid, request)) return;
    const uint32_t serial = request.serial;
    const auto current = player.mSkin ? player.mSkin->mSkinImpl : SkinObject{};
    if (!PrepareClosetRequest(request, current ? &current->mObject : nullptr)) {
        LogOwnCapeResult(request, false);
        skinRequests.Finish(pid, serial, MyClientSkin::Result::Unsupported);
        return;
    }
    request.serial = serial;
    MyClientSkin::Result result;
    if (request.width == 0 && request.height == 0)
    {
        result = TakeOffSkin(client, level, player);
        SkinChangedForDolls();
    }
    else
    {
        if (MyClientSkin::UsableRequest(request)) {
            // The user's chosen source survives native menu/profile changes even if application must retry.
            KeepClosetSkin(request, MyClientLog::Get());
            result = WearSkin(client, level, player, request, "");
            if (result == MyClientSkin::Result::Failed) result = MyClientSkin::Result::Saved;
        } else result = MyClientSkin::Result::Failed;
    }
    skinRequests.Finish(pid, request.serial, result);
}

// Tells the server the skin your player wears now (the Closet's, put on locally in place of the account's).
void SendWornSkin(ClientInstance& client, Player& player)
{
    closetSentThisWorld = true;   // once per world, whatever happens
    try
    {
        using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
        static const auto createPacket = MyClientGame::ResolveFunction<PacketFactory>("?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z");
        auto& ref = player.mSkin;
        if (!createPacket || !ref || !ref->mSkinImpl) return;
        std::shared_ptr<PlayerSkinPacket> skinPacket;
        if (auto packet = createPacket(MinecraftPacketIds::PlayerSkin)) skinPacket = std::static_pointer_cast<PlayerSkinPacket>(packet);
        if (!skinPacket) return;
        skinPacket->mUUID = MyClientGame::UuidOf(player);
        if (MyClientCapes::IsOriginXCape(ref->mSkinImpl->mObject.mCapeId.get())) {
            // OriginX capes are for OriginX users only (review C1): the server gets the same skin with your own cape.
            const auto fresh = skinPacket->mSkin->mSkinImpl;
            if (!fresh || fresh == ref->mSkinImpl || !closetHaveKept) return;
            static MyClientSkin::Request plain;
            std::memcpy(&plain, &closetKept, sizeof(plain));
            plain.capeId = 0;
            char id[48];
            FillClosetSkin(fresh->mObject, ref->mSkinImpl->mObject, true, plain, id);
        }
        else skinPacket->mSkin->mSkinImpl = ref->mSkinImpl;
        skinPacket->mLocalizedNewSkinName = std::string(closetKept.name);
        skinPacket->mLocalizedOldSkinName = std::string{};
        if (MyClientCapes::IsOriginXCape(skinPacket->mSkin->mSkinImpl->mObject.mCapeId.get())) return;   // never to the server
        client.getPacketSender().sendToServer(*skinPacket);
        if (mod) MyClientLog::Get().info("Closet: \"{}\" sent to the server for this world.", closetKept.name);
    }
    catch (...)
    {
        if (mod) MyClientLog::Get().warn("Closet: sending the skin to the server failed with an exception.");
    }
}

// Puts the kept skin on again a few seconds into every world (the game sent the account's skin on joining), and
// tells in the log what the pause screen's doll was given.
void KeepClosetSkinOn(ClientInstance& client, Level& level, Player& player)
{
    if (!mod) return;
    static ULONGLONG dollLogAt = 0;
    static uint32_t dollLogged = 0, updatesLogged = 0;
    const ULONGLONG now = GetTickCount64();
    const uint32_t asked = closetDollAsked.load(std::memory_order_relaxed);
    const uint32_t updates = dollUpdates.load(std::memory_order_relaxed);
    if ((asked != dollLogged || (updates != updatesLogged && dollGeneration.load() > 1)) && now - dollLogAt >= 30000)
    {
        dollLogged = asked;
        updatesLogged = updates;
        dollLogAt = now;
        MyClientLog::Get().info("Closet: dolls updated {} frames, told your skin changed {} times ({} other contexts); "
                              "the doll asked for your skin {} times: given the Closet's {}, empty {}; "
                              "built with the Closet's picture {} times",
                              updates, dollDirtied.load(), dollOtherContexts.load(), asked, closetDollGiven.load(),
                              closetDollEmpty.load(), closetDollPictured.load());
    }
    // A native notification must not reset the same world's retry interval or attempt limit.
    if (closetReapplyWanted.exchange(false)) {
        if (!closetMismatchLogged) {
            closetMismatchLogged = true;
            const unsigned kind = closetMismatchKind.load(std::memory_order_relaxed);
            MyClientLog::Get().info("Closet: native skin differs from saved choice (generatedId={} capePresent={} wantedCape={}); bounded reapply queued",
                                   (kind & 1) != 0, (kind & 2) != 0, closetKept.capeId);
        }
    }
    if (!closetHaveKept) return;
    if (!clientPacketHandler.load(std::memory_order_relaxed)) return;   // the world's handler is not known yet
    auto& ref = player.mSkin;
    auto* wearing = ref ? ref->mSkinImpl.get() : nullptr;
    if (!wearing) return;
    if (!LocalSkinListReady(level, player)) {
        if (!closetWaitingForList.exchange(true))
            MyClientLog::Get().info("Closet: keeping the account skin until local player skin registration is ready");
        return;
    }
    if (closetWaitingForList.exchange(false))
        MyClientLog::Get().info("Closet: local player skin registration ready; applying the saved choice");
    if (MyClientSkin::MatchesSelection(wearing->mObject.mId.get(), closetKeptId))
    {
        {
            std::lock_guard lock(closetWornMutex);
            if (!closetWornObject)
            {
                closetWornObject = ref->mSkinImpl;
                closetWornUuid = MyClientGame::UuidOf(player);
                SkinChangedForDolls();
            }
        }
        // The current native instance already belongs to the saved choice; send once for this world if needed.
        if (!closetSentThisWorld && client.getPacketSender().isInitialized()) SendWornSkin(client, player);
        return;
    }
    if (!closetReapply.CanTry(now)) {
        if (closetReapply.attempts >= 5 && !closetLimitLogged) {
            closetLimitLogged = true;
            MyClientLog::Get().warn("Closet: saved choice reapply stopped after 5 attempts in this world; choose again to retry");
        }
        return;
    }
    closetReapply.Tried(now);
    WearSkin(client, level, player, closetKept, closetReapply.attempts == 1 ? " again in this world" : " again in this world (it was taken off)");
}

void PublishTabList(const MyClientTabList::Shared& snapshot, bool known)
{
    tabListChannel.Publish(GetCurrentProcessId(), snapshot, known ? GetTickCount64() : 0);
}

void PublishShulkers(const MyClientShulker::Shared& snapshot, bool known)
{
    // at == 0 tells the HUD there is no trustworthy data.
    shulkerChannel.Publish(GetCurrentProcessId(), snapshot, known ? GetTickCount64() : 0);
}

// Reads the stacks stored inside a shulker box item (NBT list "Items").
void ReadShulker(MyClientShulker::Collector& shulkers, ItemStack const& stack)
{
    if (!shulkers.Begin(MyClientGame::TypeNameOf(stack))) return;
    // LeviLamina exposes NBT as plain containers (no getList/getCompound helpers); read mTags directly.
    auto const* tag = stack.mUserData.get();
    if (!tag) return;
    auto const items = tag->mTags.find("Items");
    if (items == tag->mTags.end() || !items->second.hold(Tag::List)) return;
    int read = 0;
    for (auto const& entry : items->second.get<ListTag>())
    {
        if (read++ >= 27) break;
        if (!entry.hold(Tag::Compound)) continue;
        auto const& fields = static_cast<CompoundTag const&>(*entry).mTags;
        auto const name = fields.find("Name");
        auto const count = fields.find("Count");
        if (name == fields.end() || count == fields.end()) continue;
        if (!name->second.hold(Tag::String) || !count->second.hold(Tag::Byte)) continue;
        const uint32_t n = static_cast<unsigned char>(count->second.get<ByteTag>().data);
        if (n > 0) shulkers.Add(name->second.get<StringTag>(), n);
    }
}

// --- Shulker hover popup ---------------------------------------------------------------------
// ContainerScreenController (inventory, chests, ...) reports the slot under the pointer. When that
// slot holds a shulker box its contents go to the HUD, which draws the popup at the mouse cursor.
// Hooks stay installed until process exit; `hoverOn` gates them while the mod is disabled.
MyClientShulker::HoverChannel hoverChannel;
std::mutex hoverMutex;
std::atomic<bool> hoverOn{false};
MyClientShulker::HoverBox hoverBox{};
bool hoverActive = false;
const void* hoverOwner = nullptr;   // the screen that reported the hover; compared, never dereferenced
// Item Counter: the slot under the pointer, whatever it holds (hoverMutex).
std::string countCollection;
int countIndex = -1;
const void* countOwner = nullptr;
// Member function returning a reference: same x64 ABI as a free function taking `this` first.
using SlotItemFn = ItemStack const& (*)(ContainerScreenController const*, std::string const&, int);
SlotItemFn slotItem = nullptr;

// Hover popup: the stacks of a shulker box item by slot (NBT "Slot"), so the HUD can draw the box's grid.
// False when an entry cannot be trusted (the popup then stays hidden).
bool ReadShulkerSlots(MyClientShulker::HoverBox& box, ItemStack const& stack)
{
    box = {};
    MyClientShulker::CopyName(box.name, sizeof(box.name), MyClientGame::TypeNameOf(stack));
    auto const* tag = stack.mUserData.get();
    if (!tag) return true;   // never filled: empty box
    auto const items = tag->mTags.find("Items");
    if (items == tag->mTags.end() || !items->second.hold(Tag::List)) return true;
    for (auto const& entry : items->second.get<ListTag>())
    {
        if (!entry.hold(Tag::Compound)) continue;
        auto const& fields = static_cast<CompoundTag const&>(*entry).mTags;
        auto const name = fields.find("Name");
        auto const count = fields.find("Count");
        auto const slot = fields.find("Slot");
        if (name == fields.end() || count == fields.end() || slot == fields.end()) continue;
        if (!name->second.hold(Tag::String) || !count->second.hold(Tag::Byte) || !slot->second.hold(Tag::Byte)) continue;
        const uint32_t n = static_cast<unsigned char>(count->second.get<ByteTag>().data);
        const int at = static_cast<unsigned char>(slot->second.get<ByteTag>().data);
        if (!MyClientShulker::PutSlot(box, at, name->second.get<StringTag>(), n)) return false;
    }
    return true;
}

void PublishHover(uint64_t now)
{
    hoverChannel.Publish(GetCurrentProcessId(), hoverBox, hoverActive, hoverActive ? now : 0);
}

void OnSlotHovered(ContainerScreenController const* screen, std::string const& collection, int index)
{
    MyClientShulker::HoverBox box{};
    bool found = false;
    if (slotItem && index >= 0)
    {
        auto const& stack = slotItem(screen, collection, index);
        if (stack.mCount && MyClientShulker::IsShulkerBox(MyClientGame::TypeNameOf(stack))) found = ReadShulkerSlots(box, stack);
    }
    std::lock_guard guard(hoverMutex);
    hoverActive = found;
    hoverBox = found ? box : MyClientShulker::HoverBox{};
    hoverOwner = screen;
    countCollection = collection;
    countIndex = index;
    countOwner = screen;
    PublishHover(GetTickCount64());
}

void OnSlotUnhovered(ContainerScreenController const* screen)
{
    std::lock_guard guard(hoverMutex);
    if (countOwner == screen) { countIndex = -1; countOwner = nullptr; }
    if (hoverOwner != screen) return;
    hoverActive = false;
    hoverBox = {};
    PublishHover(0);
}

// Keeps the popup alive while its screen is open. A closed screen stops ticking, so the HUD drops
// the popup after HoverStaleMs even when no "unhovered" call arrives.
void OnScreenTick(ContainerScreenController const* screen)
{
    std::lock_guard guard(hoverMutex);
    if (hoverActive && hoverOwner == screen) PublishHover(GetTickCount64());
}

// The HUD draws its own popup over a hovered shulker box: the game's tooltip (which also lists the contents)
// is hidden for that slot so the two do not overlap. Other items keep their tooltip.
bool itemCountShown = false;          // the Item Counter popup is up (hoverMutex)
const void* itemCountScreen = nullptr;

void HideGameTooltip(ContainerScreenController* screen)
{
    if (!screen) return;
    std::lock_guard guard(hoverMutex);
    if (MyClientGame::ShulkerHoverWanted() && hoverActive && hoverOwner == screen) screen->mShowHoverText = false;
    if (itemCountShown && itemCountScreen == screen) screen->mShowHoverText = false;   // Item Counter's popup replaces it
}

// --- Item Counter -----------------------------------------------------------------------------
// While Alt is held over a slot (and the HUD has the feature on), the item there is counted in the inventory,
// inside shulker boxes (in the inventory and in the open container) and in the open container itself.
// The open container's items come from the player's ContainerManagerModel: its models whose name is not one of
// the player's own (ItemCountChannel.h IsOpenContainerModel). The names are logged once each, to check them.
MyClientItemCount::Channel itemCountChannel;
uint64_t itemCountAt = 0;

uint64_t CountInShulker(ItemStack const& stack, std::string const& type)
{
    auto const* tag = stack.mUserData.get();
    if (!tag) return 0;
    auto const items = tag->mTags.find("Items");
    if (items == tag->mTags.end() || !items->second.hold(Tag::List)) return 0;
    uint64_t total = 0;
    int read = 0;
    for (auto const& entry : items->second.get<ListTag>())
    {
        if (read++ >= 64) break;
        if (!entry.hold(Tag::Compound)) continue;
        auto const& fields = static_cast<CompoundTag const&>(*entry).mTags;
        auto const name = fields.find("Name");
        auto const count = fields.find("Count");
        if (name == fields.end() || count == fields.end()) continue;
        if (!name->second.hold(Tag::String) || !count->second.hold(Tag::Byte)) continue;
        if (static_cast<std::string const&>(name->second.get<StringTag>()) != type) continue;
        total += static_cast<unsigned char>(count->second.get<ByteTag>().data);
    }
    return total;
}

void AddCounted(ItemStack const& stack, std::string const& type, uint64_t& direct, uint64_t& inShulkers)
{
    const uint32_t count = stack.mCount;
    if (!count) return;
    const std::string name = MyClientGame::TypeNameOf(stack);
    if (name == type) direct += count;
    if (name != type && MyClientShulker::IsShulkerBox(name)) inShulkers += CountInShulker(stack, type);
}

// The open container's stacks. False = no container model (only the inventory is open).
bool CountOpenContainer(Player& player, std::string const& type, MyClientItemCount::Counts& counts, std::string& seen)
{
    std::shared_ptr<ContainerManagerModel> const& manager = player.mContainerManager;
    if (!manager) return false;
    bool any = false;
    for (auto const& [name, model] : manager->mContainers.get())
    {
        if (!model) continue;
        const int size = model->getContainerSize();
        if (seen.size() < 400) seen += name + "(" + std::to_string(size) + ") ";
        if (!MyClientItemCount::IsOpenContainerModel(name) || size <= 0 || size > 256) continue;
        any = true;
        for (int i = 0; i < size; ++i) AddCounted(model->getItemStack(i), type, counts.container, counts.shulkers);
    }
    return any;
}

void TickItemCount(ContainerScreenController const* screen)
{
    if (!itemCountChannel.IsOpen() || !slotItem) return;
    const DWORD pid = GetCurrentProcessId();
    const uint64_t now = GetTickCount64();
    std::string collection;
    int index = -1;
    {
        std::lock_guard guard(hoverMutex);
        if (countOwner == screen) { collection = countCollection; index = countIndex; }
    }
    const auto hide = [&] {
        std::lock_guard guard(hoverMutex);
        if (itemCountShown) { itemCountChannel.Hide(pid); itemCountShown = false; itemCountScreen = nullptr; }
    };
    if (index < 0) { std::lock_guard guard(hoverMutex); if (itemCountScreen == screen) { itemCountChannel.Hide(pid); itemCountShown = false; itemCountScreen = nullptr; } return; }
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) == 0 || !itemCountChannel.Wanted(pid, now)) { hide(); return; }
    if (now - itemCountAt < 100) return;   // counted a moment ago (the popup stays fresh for StaleMs)
    itemCountAt = now;
    auto const& stack = slotItem(screen, collection, index);
    if (!stack.mCount) { hide(); return; }
    const auto client = MyClientGame::Client();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) { hide(); return; }
    const std::string type = MyClientGame::TypeNameOf(stack);
    MyClientItemCount::Counts counts;
    Inventory* inventory = MyClientGame::InventoryOf(*player);
    if (!inventory) { hide(); return; }
    const int slots = inventory->getContainerSize();
    if (slots > 0 && slots <= 256)
        for (int slot = 0; slot < slots; ++slot) AddCounted(inventory->getItem(slot), type, counts.inventory, counts.shulkers);
    if (ItemStack const* offhand = MyClientGame::OffhandOf(*player))
        AddCounted(*offhand, type, counts.inventory, counts.shulkers);
    std::string seen;
    const bool container = CountOpenContainer(*player, type, counts, seen);
    // ItemStackBase::getName / getMaxStackSize are not in LeviLamina's import library: use the Item's
    // virtual buildDescriptionName and its mMaxStackSize field instead.
    Item const* item = stack.mItem.get();
    std::string name = item ? item->buildDescriptionName(stack) : std::string{};   // virtual
    if (name.empty()) name = std::string(MyClientShulker::StripNamespace(type));
    const int maxStack = item ? static_cast<int>(item->mMaxStackSize) : 64;
    itemCountChannel.Publish(pid, type, name, counts, maxStack, container, now);
    {
        std::lock_guard guard(hoverMutex);
        itemCountShown = true;
        itemCountScreen = screen;
    }
    // Once per item kind and screen (at most 30 lines): what was counted, and the container models' names.
    static const void* loggedScreen = nullptr;
    static std::string loggedType;
    static int logged = 0;
    if ((loggedScreen != screen || loggedType != type) && logged < 30)
    {
        loggedScreen = screen; loggedType = type; ++logged;
        MyClientLog::Get().info(
            "Item Counter: {} '{}' inventory={} shulkers={} container={} (open container={}, max stack {}); container models: {}",
            type, name, counts.inventory, counts.shulkers, counts.container, container, maxStack, seen);
    }
}

void ClearHover()
{
    std::lock_guard guard(hoverMutex);
    hoverActive = false;
    hoverBox = {};
    hoverOwner = nullptr;
    PublishHover(0);
}

MYCLIENT_TYPE_INSTANCE_HOOK(SlotHoveredHook, ll::memory::HookPriority::Normal, ContainerScreenController,
                      MyClientGame::Symbols::SlotHovered(), ui::ViewRequest, std::string const& collectionName, int index)
{
    const auto result = origin(collectionName, index);
    if (hoverOn.load(std::memory_order_relaxed))
        try {
            MyClientProfile::Scope prof(MyClientProfile::Screen);
            OnSlotHovered(this, collectionName, index);
            TickItemCount(this);
            HideGameTooltip(this);
        } catch (...) {}
    return result;
}

MYCLIENT_TYPE_INSTANCE_HOOK(SlotUnhoveredHook, ll::memory::HookPriority::Normal, ContainerScreenController,
                      MyClientGame::Symbols::SlotUnhovered(), ui::ViewRequest, std::string const& collectionName, int index)
{
    const auto result = origin(collectionName, index);
    if (hoverOn.load(std::memory_order_relaxed))
        try { OnSlotUnhovered(this); } catch (...) {}
    return result;
}

MYCLIENT_TYPE_INSTANCE_HOOK(ScreenTickHook, ll::memory::HookPriority::Normal, ContainerScreenController,
                      MyClientGame::Symbols::ScreenTick(), ui::DirtyFlag)
{
    const auto result = origin();
    if (hoverOn.load(std::memory_order_relaxed))
        try {
            MyClientProfile::Scope prof(MyClientProfile::Screen);
            OnScreenTick(this);
            TickItemCount(this);
            HideGameTooltip(this);   // after the game's tick, which may switch its tooltip back on
        } catch (...) {}
    return result;
}

// Returns false when the popup cannot work in this game build (the reason is logged).
bool EnableShulkerHover(MyClientLog::Logger& log)
{
    static bool hovered = false, unhovered = false, ticked = false;
    if (!hoverChannel.Data() && !hoverChannel.Open(GetCurrentProcessId(), true))
    {
        log.warn("Shulker hover popup unavailable: shared memory could not be created (Windows error {}).", GetLastError());
        return false;
    }
    if (!slotItem) slotItem = reinterpret_cast<SlotItemFn>(MyClientGame::Symbols::SlotItem());
    if (!slotItem)
    {
        log.warn("Shulker hover popup unavailable: ContainerScreenController::_getItemStack not found.");
        return false;
    }
    // Manual registration checks errors; the macro's automatic-use counter is unused here.
    (void)SlotHoveredHook::_AutoHookCount; (void)SlotUnhoveredHook::_AutoHookCount; (void)ScreenTickHook::_AutoHookCount;
    if (!hovered) hovered = MyClientGame::Symbols::SlotHovered() && SlotHoveredHook::hook() == 0;
    if (!unhovered) unhovered = MyClientGame::Symbols::SlotUnhovered() && SlotUnhoveredHook::hook() == 0;
    if (!ticked) ticked = MyClientGame::Symbols::ScreenTick() && ScreenTickHook::hook() == 0;
    if (!hovered)
    {
        log.warn("Shulker hover popup unavailable: ContainerScreenController::_onContainerSlotHovered hook failed.");
        return false;
    }
    if (!unhovered) log.warn("Shulker hover popup: unhover hook failed; the popup closes only by timeout.");
    if (!ticked) log.warn("Shulker hover popup: screen tick hook failed; each hover shows for {} ms only.", MyClientShulker::HoverStaleMs);
    hoverOn.store(true);
    log.info("Shulker hover popup ready.");
    return true;
}

void PublishResources(const MyClientResources::Counts& counts, bool known)
{
    // at == 0 tells the HUD there is no trustworthy data (it shows ---).
    resourceChannel.Publish(GetCurrentProcessId(), counts, known ? GetTickCount64() : 0);
}

MyClientMap::Channel mapChannel;
MyClientMap::Shared mapState{};
Level* mapLevel = nullptr;
std::string mapWorldIdentity,mapServerIdentity;
size_t mapCursor = 0;
void ClearMap() {
    mapState = {}; mapLevel = nullptr; mapCursor = 0; mapWorldIdentity.clear();mapServerIdentity.clear();
    if (mapChannel.Data() && mapChannel.Lock()) {
        mapChannel.Data()->at = 0; mapChannel.Data()->mobCount = 0;
        mapChannel.Data()->locality = 0; mapChannel.Data()->realm = 0;
        mapChannel.Unlock();
    }
}
// The game's save folders (...\games\com.mojang\minecraftWorlds) on this PC.
std::vector<std::filesystem::path> WorldFolders() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots; std::error_code ec;
    if (const wchar_t* appData = _wgetenv(L"APPDATA"))
        for (fs::directory_iterator it(fs::path(appData) / L"Minecraft Bedrock" / L"Users", ec), end; !ec && it != end; it.increment(ec))
            roots.push_back(it->path() / L"games" / L"com.mojang" / L"minecraftWorlds");
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"))
        roots.push_back(fs::path(local) / L"Packages" / L"Microsoft.MinecraftUWP_8wekyb3d8bbwe" / L"LocalState" / L"games" / L"com.mojang" / L"minecraftWorlds");
    return roots;
}

// A save folder on this PC for this world: by its id when the game gives one, else by its name (levelname.txt).
// Only asked when the game says it is not on a server or a Realm, so a server never matches by its name.
bool LocalWorldOnThisPc(const std::string& id, const std::string& name) {
    static std::string checkedKey; static bool checked = false;
    const std::string key = id + "\n" + name;
    if (key == checkedKey) return checked;
    namespace fs = std::filesystem;
    bool byId = false, byName = false; int scanned = 0;
    for (auto const& root : WorldFolders()) {
        std::error_code ec;
        if (MyClientMap::ValidLevelId(id) && fs::exists(root / fs::path(id) / L"level.dat", ec) && !ec) { byId = true; break; }
        if (name.empty()) continue;
        for (fs::directory_iterator it(root, ec), end; !ec && it != end && scanned < 500 && !byName; it.increment(ec), ++scanned) {
            std::ifstream in(it->path() / L"levelname.txt", std::ios::binary);
            std::string line; if (in && std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                byName = line == name;
            }
        }
    }
    checkedKey = key; checked = byId || byName;
    MyClientLog::Get().info("Minimap: own-world check id='{}' byId={} byName={} (scanned {} folders)", id, byId, byName, scanned);
    return checked;
}

void SampleMap(ClientInstance& client, Level& level, Player& player) {
    using namespace MyClientMap;
    if (!mapChannel.Data() && !mapChannel.Open(GetCurrentProcessId(), true)) return;
    bool want = false, mobs = false;
    if (!mapChannel.Lock()) return;
    auto* shared = mapChannel.Data();
    if (Valid(*shared, GetCurrentProcessId()) && Fresh(shared->hudAt, GetTickCount64())) {
        want = shared->wantMap != 0; mobs = shared->wantMobs != 0;
    }
    mapChannel.Unlock();
    if (!want) { ClearMap(); return; }
    using RegionFn = BlockSource& (Actor::*)() const;
    static const auto getRegion = MyClientGame::ResolveMember<RegionFn>(MyClientGame::Symbols::DimensionBlockSource);
    auto* region = getRegion ? &(player.*getRegion)() : client.getRegion();
    if (!region) { ClearMap(); return; }
    const auto at = player.getPosition();
    if (!std::isfinite(at.x) || !std::isfinite(at.z) || !std::isfinite(at.y) ||
        std::abs(at.x) > 30000000 || std::abs(at.z) > 30000000) { ClearMap(); return; }
    const int dimension = static_cast<Dimension&>(region->mDimension).mId->mValue;
    std::string levelId; try { levelId = level.getLevelId(); } catch (...) { levelId.clear(); }
    const std::string levelName = level.getLevelData().mLevelName.get();
    const std::string world = MyClientMap::WorldIdentity(levelName, levelId, sizeof(mapState.world));
    // Recheck eligibility each tick: never carry a previous server's permission through a transition.
    uint32_t locality = 0; bool realm = false; std::string host;
    try { locality = MyClientGame::MapConnectionLocality(); } catch (...) { locality = 0; }
    try { realm = client.isInRealm(); } catch (...) { realm = false; }
    try { uint32_t port = 0, transport = 0; MyClientGame::CurrentServer(host, port, transport); } catch (...) { host.clear(); }
    {
        static std::string lastDecision;
        const std::string decision = "locality=" + std::to_string(locality) + " host='" + host + "' realm=" + std::to_string(realm) +
                                     " levelId='" + levelId + "'";
        if (decision != lastDecision) { lastDecision = decision; MyClientLog::Get().info("Minimap: connection {}", decision); }
    }
    // Unknown connection, no server address, the game says it is not on a server or a Realm, and this world's
    // save folder is on this PC => the player's own world.
    if (locality == 0 && host.empty() && !realm) {
        bool inServer = true;
        try { inServer = client.getMinecraftGame_DEPRECATED().isInServer(); } catch (...) { inServer = true; }
        static int loggedServer = -1;
        if (loggedServer != int(inServer)) { loggedServer = int(inServer); MyClientLog::Get().info("Minimap: game says in a server={}", inServer); }
        if (!inServer && LocalWorldOnThisPc(levelId, levelName)) locality = 1;
    }
    if (mapLevel != &level || mapState.dimension != dimension || world != mapWorldIdentity || host != mapServerIdentity) {
        mapState = {}; mapCursor = 0; mapLevel = &level;mapWorldIdentity=world;mapServerIdentity=host;
        CopyText(mapState.world, sizeof(mapState.world), world);
        CopyText(mapState.server, sizeof(mapState.server), host);
        mapState.originX = int(std::floor(at.x)) - Window/2;
        mapState.originZ = int(std::floor(at.z)) - Window/2;
    }
    int ox = int(std::floor(at.x)) - Window/2, oz = int(std::floor(at.z)) - Window/2;
    Shift(mapState, ox-mapState.originX, oz-mapState.originZ);
    mapState.originX=ox; mapState.originZ=oz; mapState.dimension=dimension;
    mapState.x=at.x;mapState.y=at.y;mapState.z=at.z;mapState.yaw=player.getRotation().y;
    mapState.locality=locality;mapState.realm=realm?1u:0u;mapState.mobCount=0;
    const int minHeight=region->getMinHeight();
    for(int n=0;n<768;++n) {
        const size_t i=mapCursor++ % (Window*Window);
        const int x=ox+int(i%Window),z=oz+int(i/Window);
        int y=region->getAboveTopSolidBlock(x,z,true,true);
        if(y<=minHeight) {mapState.cells[i]=0;continue;}
        if(dimension==1) {
            y=int(std::floor(at.y))-1; bool found=false;
            for(int down=0;down<64 && y>minHeight;++down,--y) {
                auto const& b=region->getBlock(BlockPos{x,y,z});
                if(b.getBlockType().mMapColor->a>0) {found=true;break;}
            }
            if(!found){mapState.cells[i]=0;continue;}
        } else --y;
        auto const& block=region->getBlock(BlockPos{x,y,z});
        auto const& color=block.getBlockType().mMapColor.get();
        auto byte=[](float v){return uint32_t(std::clamp(v,0.f,1.f)*255.f);};
        uint32_t rgb=(byte(color.r)<<16)|(byte(color.g)<<8)|byte(color.b);
        if(color.a<=0 || rgb==0) {
            // Biome-tinted blocks (grass, leaves, water...) keep no fixed map colour; use a fixed one by name.
            const std::string name=MyClientGame::TypeNameOf(block);
            rgb=MyClientMap::FallbackColor(name);
            static std::set<std::string> logged;
            if(logged.size()<16 && logged.insert(name).second)
                MyClientLog::Get().info("Minimap: '{}' has no map colour (a={:.2f}); fallback {:06x}", name, color.a, rgb);
            if(!rgb){mapState.cells[i]=0;continue;}
        }
        unsigned depth=0;
        if(MyClientGame::TypeNameOf(block).find("water")!=std::string::npos) {
            for(int d=1;d<=16 && y-d>=minHeight;++d) {
                if(MyClientGame::TypeNameOf(region->getBlock(BlockPos{x,y-d,z})).find("water")==std::string::npos)break;
                ++depth;
            }
        }
        mapState.cells[i]=Cell(rgb,depth,true);
        mapState.heights[i]=static_cast<int16_t>(std::clamp(y,-32768,32767));
    }
    if(mobs && AutoMobs(locality,realm)) {
        using TypeFn=ActorType (Actor::*)() const;
        static const auto getType=MyClientGame::ResolveMember<TypeFn>(MyClientGame::Symbols::EntityTypeId);
        if(getType) for(auto* other:level.getRuntimeActorList()) {
            if(!other || other==&player || mapState.mobCount>=MaxMobs)continue;
            bool isPlayer=MyClientGame::IsPlayer(*other);
            if(!isPlayer && !other->hasCategory(ActorCategory::Mob))continue;
            auto p=other->getPosition();float dx=p.x-at.x,dz=p.z-at.z;
            if(!std::isfinite(dx) || !std::isfinite(dz) || dx*dx+dz*dz>64*64)continue;
            mapState.mobs[mapState.mobCount++]={p.x,p.z,uint32_t((other->*getType)()),
                uint8_t(other->hasCategory(ActorCategory::Monster)),uint8_t(isPlayer),0};
        }
    }
    mapState.at=GetTickCount64();
    if(mapChannel.Lock()) {
        auto* dest=mapChannel.Data();auto hudAt=dest->hudAt;auto wantMap=dest->wantMap, wantMobs=dest->wantMobs;
        *dest=mapState;dest->magic=Magic;dest->version=Version;dest->size=sizeof(Shared);dest->pid=GetCurrentProcessId();
        dest->hudAt=hudAt;dest->wantMap=wantMap;dest->wantMobs=wantMobs;mapChannel.Unlock();
    }
}

void PublishEmpty()
{
    PublishOriginMarks({}, GetTickCount64());
    ClearMap();
    PublishResources({}, false);
    PublishShulkers({}, false);
    ClearHover();
    PublishTabList({}, false);
    MyClientGame::ClearArmor();
    MyClientGame::ClearParty();
    const auto frame = MyClientGame::EmptyFrame();
    MyClientBridgePublish(&frame);
}

void CopyName(char* out, size_t size, std::string const& name)
{
    if (!out || !size) return;
    const size_t n = std::min(name.size(), size - 1);
    std::memcpy(out, name.data(), n);
    out[n] = 0;
}

// Health Warning: the local player's health in points (1 = half a heart) for Shared.clockReserved, 0 = unknown.
uint32_t SampleHealth(Player& player)
{
    try
    {
        // ActorAttribute::getHealth is what Actor::getHealth (LeviLamina) calls; looked up so a game build
        // without it only switches the warning off.
        using HealthFn = int (*)(EntityContext const&);
        static const auto getHealth = MyClientGame::ResolveFunction<HealthFn>(MyClientGame::Symbols::Health);
        static bool reported = false;
        if (!reported)
        {
            reported = true;
            MyClientLog::Get().info("Health Warning: health={}", getHealth != nullptr);
        }
        if (!getHealth) return 0;
        return MyClientFeatures::EncodeHealth(getHealth(MyClientGame::EntityOf(player)));
    }
    catch (...) {}
    return 0;
}

// Debug Menu: the block under the crosshair, the biome and the light where the player stands.
MyClientFeatures::Debug SampleDebug(ClientInstance& client, Player& player, HitResult const& hit)
{
    MyClientFeatures::Debug debug{};
    debug.lookFace = MyClientFeatures::NoLook;
    try
    {
        using RegionFn = BlockSource& (Actor::*)() const;
        // BlockSource has several bases: called as plain functions (see ResolveFunction).
        using BiomeFn = Biome const* (*)(BlockSource const*, BlockPos const*);
        using LightFn = BrightnessPair* (*)(BlockSource const*, BrightnessPair*, BlockPos const*);
        static const auto getRegion = MyClientGame::ResolveMember<RegionFn>(MyClientGame::Symbols::DimensionBlockSource);
        static const auto getBiome = MyClientGame::ResolveFunction<BiomeFn>(MyClientGame::Symbols::Biome);
        static const auto getLight = MyClientGame::ResolveFunction<LightFn>(MyClientGame::Symbols::BrightnessPair);
        static bool reported = false;
        if (!reported)
        {
            reported = true;
            MyClientLog::Get().info("Debug Menu: biome={} light={}", getBiome != nullptr, getLight != nullptr);
        }
        BlockSource* region = getRegion ? &(player.*getRegion)() : client.getRegion();
        if (!region) return debug;
        if (hit.mType == HitResultType::Tile)
        {
            debug.lookX = hit.mBlock.x; debug.lookY = hit.mBlock.y; debug.lookZ = hit.mBlock.z;
            debug.lookFace = hit.mFacing <= 5 ? hit.mFacing : MyClientFeatures::NoLook;
            CopyName(debug.lookBlock, sizeof(debug.lookBlock), MyClientGame::TypeNameOf(region->getBlock(hit.mBlock)));
        }
        // The feet: the bottom of the player's box (the position of a player is at the eyes).
        const AABB& box = *MyClientGame::AabbOf(player);
        const BlockPos feet{(int)std::floor((box.min.x + box.max.x) * 0.5f), (int)std::floor(box.min.y + 0.01f),
                            (int)std::floor((box.min.z + box.max.z) * 0.5f)};
        if (getBiome)
            if (const Biome* biome = getBiome(region, &feet))
                CopyName(debug.biome, sizeof(debug.biome), biome->mHash.get().getString());
        if (getLight)
        {
            BrightnessPair light{};
            if (getLight(region, &light, &feet))
            {
                debug.skyLight = light.sky.get().mValue;
                debug.blockLight = light.block.get().mValue;
                debug.lightKnown = 1;
            }
        }
    }
    catch (...) {}
    return debug;
}

// Block Details: the name, tags and tool of the block under the crosshair, and whether the held item harvests it.
// Read again only when the block or the held item changes (the same Block object = the same block state).
MyClientFeatures::Look SampleLook(ClientInstance& client, Player& player, HitResult const& hit)
{
    static Block const* lastBlock = nullptr;
    static std::string lastHeld;
    static MyClientFeatures::Look last{};
    MyClientFeatures::Look look{};
    try
    {
        if (hit.mType != HitResultType::Tile) { lastBlock = nullptr; return look; }
        using RegionFn = BlockSource& (Actor::*)() const;
        using NameFn = std::string (Block::*)() const;
        static const auto getRegion = MyClientGame::ResolveMember<RegionFn>(MyClientGame::Symbols::DimensionBlockSource);
        static const auto getName = MyClientGame::ResolveMember<NameFn>(MyClientGame::Symbols::BlockDescriptionName);
        BlockSource* region = getRegion ? &(player.*getRegion)() : client.getRegion();
        if (!region) return look;
        Block const& block = region->getBlock(hit.mBlock);
        ItemStack const* held = MyClientGame::SelectedItemOf(player);
        const std::string heldId = held && held->mItem ? MyClientGame::TypeNameOf(*held) : std::string{};
        if (&block == lastBlock && heldId == lastHeld) return last;

        BlockType const& type = block.getBlockType();
        if (getName) CopyName(look.name, sizeof(look.name), (block.*getName)());
        else CopyName(look.name, sizeof(look.name), "@" + type.mDescriptionId.get());   // the HUD finds it in the game's .lang
        std::string tags;
        const auto addTags = [&](std::vector<HashedString> const& list) {
            for (const auto& tag : list)
            {
                const std::string& text = tag.getString();
                if (text.empty() || tags.size() + text.size() + 1 >= sizeof(look.tags)) continue;
                if (("," + tags + ",").find("," + text + ",") != std::string::npos) continue;
                if (!tags.empty()) tags += ',';
                tags += text;
            }
        };
        addTags(type.mTags.get());
        addTags(block.mTags.get());
        CopyName(look.tags, sizeof(look.tags), tags);
        CopyName(look.held, sizeof(look.held), heldId);
        look.requiresTool = type.mRequiresCorrectToolForDrops ? 1 : 0;
        look.canHarvest = !look.requiresTool || (held && held->mItem && held->mItem->canDestroySpecial(block)) ? 1 : 0;   // virtual
        look.known = 1;

        static std::vector<std::string> logged;
        const std::string& id = MyClientGame::TypeNameOf(block);
        if (logged.size() < 40 && std::find(logged.begin(), logged.end(), id) == logged.end())
        {
            logged.push_back(id);
            MyClientLog::Get().info("Block Details: {} name='{}' requiresTool={} harvest={} held={} tags={}", id,
                                                            look.name, look.requiresTool, look.canHarvest, heldId, tags);
        }
        lastBlock = &block;
        lastHeld = heldId;
        last = look;
    }
    catch (...) { look = {}; }
    return look;
}

void AddStack(MyClientGame::ArrowCounter& counter, MyClientResources::Counter& resources, ItemStack const& stack)
{
    const uint32_t count = stack.mCount;
    if (!count) return;
    const auto name = MyClientGame::TypeNameOf(stack);
    counter.Add(name, count);
    resources.Add(name, count);
}

// Hive Statistic: which server and which sidebar this is. Logged once per change, so the real Hive sidebar
// strings can be collected for the game table. The host is checked at most once a second (it copies the
// connection info). Returns whether this is The Hive (the HUD shows the stats only there).
bool LogHiveDetection(const std::string& display, const std::string& name)
{
    static std::string lastHost, lastDisplay, lastName;
    static bool first = true;
    static uint64_t checkedAt = 0;
    const uint64_t now = GetTickCount64();
    std::string host = lastHost;
    if (first || now - checkedAt >= 1000) { checkedAt = now; host = MyClientGame::ConnectionHost(); }
    const bool hostChanged = first || host != lastHost;
    const bool hive = MyClientParty::HiveDomain(host);
    if (!hostChanged && display == lastDisplay && name == lastName) return hive;
    first = false;
    if (hostChanged)
        MyClientLog::Get().info("Hive判定: {} ({})", hive ? "true" : "false", host.empty() ? "local" : host);
    const std::string both = display + " " + name;
    const int game = both.size() > 1 ? HiveStats::DetectGame(both) : -1;
    // The result only: the scoreboard's own text can hold player names (release-checklist 2026-11-20, item 4).
    MyClientLog::Get().info("ゲーム判定: {}", game >= 0 ? HiveStats::kGames[game].id : "none");
    MyClientLog::Get().debug("ゲーム判定の元: scoreboard mDisplayName=\"{}\" mName=\"{}\"", display, name);
    lastHost = host; lastDisplay = display; lastName = name;
    return hive;
}

void Sample(Level& tickedLevel)
{
    MyClientProfile::Scope prof(MyClientProfile::Tick);
    std::lock_guard lock(callbackMutex);
    if (!enabled.load()) return;
    auto frame = MyClientGame::EmptyFrame();
    try
    {
        auto client = MyClientGame::Client();
        auto level = MyClientGame::WorldLevel();
        // Never read the integrated server's player, another world or a cached
        // player from before a world transition. Resolve everything per tick.
        if (!client || !level || &tickedLevel != &*level)
        {
            PublishEmpty();
            return;
        }
        auto* player = client->getLocalPlayer();
        if (!player || !player->mInventory || !player->mInventory->mInventory)
        {
            PublishEmpty();
            return;
        }

        {
            // The game's own player list of this world / server (names only).
            MyClientTabList::Collector tab;
            for (auto const& entry : level->getPlayerList())
                tab.Add(entry.second.mName.get());
            std::string sidebarDisplay, sidebarName;
            try
            {
                // Sidebar title / objective name: lets the HUD tell which Hive game this is.
                using DisplayFn = DisplayObjective const* (Scoreboard::*)(std::string const&) const;
                static const auto getDisplay = MyClientGame::ResolveMember<DisplayFn>(MyClientGame::Symbols::DisplayObjective);
                auto const& scoreboard = level->getScoreboard();
                if (auto const* display = getDisplay ? (scoreboard.*getDisplay)("sidebar") : nullptr;
                    display && display->mObjective)
                {
                    auto const* objective = display->mObjective;
                    tab.SetTitle(objective->mDisplayName.get() + " " + objective->mName.get());
                    sidebarDisplay = objective->mDisplayName.get();
                    sidebarName = objective->mName.get();
                }
            }
            catch (...) {}
            tab.SetHive(LogHiveDetection(sidebarDisplay, sidebarName));
            MyClientGame::NoteHitboxActors(nullptr, nullptr, 0);
            if (MyClientGame::HitboxRequested())
            {
                try
                {
                    // Other players' hitboxes. Each edge is cut in 8 segments and a segment only counts as
                    // visible when a block-ray from our eye reaches it, so nothing behind a wall is shown.
                    // Any block (glass included) stops the ray: the check errs on the side of hiding.
                    using EyeFn = Vec3 (Actor::*)() const;
                    using RegionFn = BlockSource& (Actor::*)() const;
                    using TypeFn = ActorType (Actor::*)() const;
                    using SpectatorFn = bool (Actor::*)() const;
                    static const auto getEye = MyClientGame::ResolveMember<EyeFn>(MyClientGame::Symbols::EyePos);
                    static const auto getRegion = MyClientGame::ResolveMember<RegionFn>(MyClientGame::Symbols::DimensionBlockSource);
                    static const auto getType = MyClientGame::ResolveMember<TypeFn>(MyClientGame::Symbols::EntityTypeId);
                    static const auto isSpectator = MyClientGame::ResolveMember<SpectatorFn>(MyClientGame::Symbols::IsSpectator);
                    auto* self = client->getLocalPlayer();
                    // getEyePos / getDimensionBlockSource are not exported in every build (the latter is a
                    // folded function): fall back to the actor's position (a player's is at the eyes) and the client's own region.
                    BlockSource* regionPtr = self && getRegion ? &(self->*getRegion)() : client->getRegion();
                    static bool reported = false;
                    if (!reported) {
                        reported = true;
                        MyClientLog::Get().info(
                            "Hitbox: eye={} region={} type={} spectator={}", getEye ? "game" : "position",
                            getRegion ? "actor" : (regionPtr ? "client" : "none"), getType != nullptr, isSpectator != nullptr);
                    }
                    if (self && regionPtr)
                    {
                        const Vec3 eye = getEye ? (self->*getEye)() : self->getPosition();   // a player's position is at the eyes
                        auto const& region = *regionPtr;
                        const std::function<bool(BlockSource const&, Block const&, bool)> anyBlock =
                            [](BlockSource const&, Block const&, bool) { return true; };
                        const uint32_t filter = MyClientGame::HitboxFilter();
                        struct Candidate { float d2; Actor* actor; uint8_t kind; };
                        std::vector<Candidate> candidates;
                        for (Actor* other : level->getRuntimeActorList())
                        {
                            if (!other || other == static_cast<Actor*>(self) || other->isInvisible()) continue;
                            uint8_t kind;
                            if (MyClientGame::IsPlayer(*other)) kind = 0;
                            else if (other->hasCategory(ActorCategory::Item) || (getType && (other->*getType)() == ActorType::ItemEntity)) kind = 1;
                            else if (other->hasCategory(ActorCategory::Mob)) kind = 3;
                            else kind = 2;
                            if (!((filter >> kind) & 1u)) continue;
                            if (kind == 0 && isSpectator && (other->*isSpectator)()) continue;
                            const AABB& bb = *MyClientGame::AabbOf(*other);
                            const float cx = (bb.min.x + bb.max.x) * 0.5f - eye.x, cz = (bb.min.z + bb.max.z) * 0.5f - eye.z;
                            const float d2 = cx * cx + cz * cz;
                            if (d2 <= 64.f * 64.f) candidates.push_back({d2, other, kind});
                        }
                        std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.d2 < b.d2; });
                        if (candidates.size() > MyClientTabList::MaxBoxes) candidates.resize(MyClientTabList::MaxBoxes);
                        // The game draws each actor between its last two tick positions; the actor and its
                        // position now let the per-frame hook move the box along with it (see NoteHitboxActors).
                        std::vector<void*> boxActors;
                        std::vector<float> boxPositions;
                        for (auto const& cand : candidates) {
                            const AABB& aabb = *MyClientGame::AabbOf(*cand.actor);
                            const Vec3 at = cand.actor->getPosition();
                            boxActors.push_back(cand.actor);
                            boxPositions.insert(boxPositions.end(), {at.x, at.y, at.z});
                            MyClientTabList::Box box{};
                            box.min[0] = aabb.min.x; box.min[1] = aabb.min.y; box.min[2] = aabb.min.z;
                            box.max[0] = aabb.max.x; box.max[1] = aabb.max.y; box.max[2] = aabb.max.z;
                            box.kind = cand.kind;
                            for (int e = 0; e < 12; e++)
                            {
                                float a[3], b[3];
                                MyClientTabList::Corner(box, MyClientTabList::kEdges[e][0], a);
                                MyClientTabList::Corner(box, MyClientTabList::kEdges[e][1], b);
                                uint8_t mask = 0;
                                for (int s = 0; s < (int)MyClientTabList::BoxSegments; s++)
                                {
                                    const float t = (s + 0.5f) / MyClientTabList::BoxSegments;
                                    Vec3 p{a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
                                    // Step a little towards the eye so a box touching a wall is not hidden by that wall.
                                    Vec3 toEye{eye.x - p.x, eye.y - p.y, eye.z - p.z};
                                    const float len = std::sqrt(toEye.x * toEye.x + toEye.y * toEye.y + toEye.z * toEye.z);
                                    if (len < 0.1f) { mask |= uint8_t(1u << s); continue; }
                                    const float k = 0.06f / len;
                                    p = Vec3{p.x + toEye.x * k, p.y + toEye.y * k, p.z + toEye.z * k};
                                    const HitResult hit = region.clip(eye, p, false, ShapeType::Collision, 64, false, false,
                                                                      nullptr, anyBlock, false);
                                    if (!hit) mask |= uint8_t(1u << s);
                                }
                                box.visible[e] = mask;
                            }
                            tab.AddBox(box);
                        }
                        MyClientGame::NoteHitboxActors(boxActors.data(), boxPositions.data(), static_cast<uint32_t>(boxActors.size()));
                    }
                }
                catch (...) {}
            }
            PublishTabList(tab.Result(), true);
            MyClientCompositor::SetBoxes(tab.Result().boxes, std::min<uint32_t>(tab.Result().boxCount, MyClientTabList::MaxBoxes));
        }

        try { SampleMap(*client, *level, *player); } catch (...) { ClearMap(); }
        MyClientGame::TickParty();
        MyClientGame::TickAutoRq();
        MyClientGame::TickChatHotkeys();
        MyClientGame::ApplyRenderDistance();
        MyClientGame::SampleArmor();
        MyClientGame::SampleClock(level->getTime(), SampleDebug(*client, *player, level->getHitResult()),   // getTime: virtual (ILevel)
                                  SampleLook(*client, *player, level->getHitResult()), SampleHealth(*player));
        NoteClosetWorld(*player);
        WearRequestedSkin(*client, *level, *player);
        KeepClosetSkinOn(*client, *level, *player);
        if (MyClientGame::TakeSkinRefresh()) RefreshOwnSkinLook(*level, *player);
        PublishSkin(*player);
        try { TickCapes(*level, *player); } catch (...) { PublishOriginMarks({}, GetTickCount64()); }
        Inventory& inventory = *MyClientGame::InventoryOf(*player);   // checked non-null above
        const int slots = inventory.getContainerSize();
        // Bound traversal even if the layout or container is unexpected.
        if (slots > 0 && slots <= 256)
        {
            MyClientGame::ArrowCounter counter;
            MyClientResources::Counter resources;
            MyClientShulker::Collector shulkers;
            // Shulker contents (NBT) are read 5 times a second, not every tick: the HUD keeps them for
            // StaleMs (2 s), so this is never visible, and it saves most of the per-tick NBT work.
            static unsigned shulkerTick = 0;
            const bool scanShulkers = shulkerTick++ % 4 == 0;
            for (int slot = 0; slot < slots; ++slot)
            {
                auto const& stack = inventory.getItem(slot);
                AddStack(counter, resources, stack);
                if (scanShulkers && stack.mCount && MyClientShulker::IsShulkerBox(MyClientGame::TypeNameOf(stack)))
                    ReadShulker(shulkers, stack);
            }
            if (scanShulkers) PublishShulkers(shulkers.Result(), shulkers.Valid());
            if (ItemStack const* offhand = MyClientGame::OffhandOf(*player)) AddStack(counter, resources, *offhand);
            counter.Write(frame);
            PublishResources(resources.Result(), resources.Valid());
        }
        else
        {
            PublishResources({}, false);
            PublishShulkers({}, false);
        }

        const auto& hit = level->getHitResult();
        if (hit.mType == HitResultType::Tile)
            MyClientGame::SetTarget(frame, hit.mBlock.x, hit.mBlock.y, hit.mBlock.z);
        MyClientCompositor::SetTarget(hit.mType == HitResultType::Tile, hit.mBlock.x, hit.mBlock.y, hit.mBlock.z);

        if (!MyClientBridgePublish(&frame) && !warned)
        {
            warned = true;
            MyClientLog::Get().warn("HUD publication failed (Windows error {}).", GetLastError());
        }
    }
    catch (...)
    {
        PublishEmpty();
        if (!warned)
        {
            warned = true;
            MyClientLog::Get().error("Game data read failed; the HUD snapshot was cleared.");
        }
    }
}

void OnExit()
{
    std::lock_guard lock(callbackMutex);
    if (enabled.load()) PublishEmpty();
    clientPacketHandler.store(nullptr);   // the next world's StartGame brings the new one
    closetNewWorld.store(true);
}

// The world tick and leaving the world, hooked by MyClient instead of LeviLamina's ClientLevelTickEvent /
// ClientExitLevelEvent (same places: ll/api/event/world/ClientLevelTickEvent.cpp, client/ClientExitLevelEvent.cpp,
// and like them before the game's own code). On 26.51 the addresses are in GameRvaTable.inc.
MYCLIENT_TYPE_INSTANCE_HOOK(LevelTickHook, ll::memory::HookPriority::Normal, ClientLevel,
                            ll::memory::SymbolView{"?$_subTick@ClientLevel@@QEAAXXZ"}, void)
{
    Level& level = *this;
    MyClientGame::NoteWorldTick(&level);
    if (enabled.load())
    {
        Sample(level);
    }
    origin();
}

MYCLIENT_TYPE_INSTANCE_HOOK(LevelExitHook, ll::memory::HookPriority::Normal, ClientInstance,
                            ll::memory::SymbolView{"?$onLevelExit@ClientInstance@@QEAAXXZ"}, void)
{
    MyClientGame::NoteWorldExit();
    OnExit();
    origin();
}

void Disable()
{
    enabled.store(false);
    MyClientGame::DisableFeatures();
    MyClientGame::DisableParty();
    // The world hooks stay in (MyClient's hooks are never taken out); they see enabled=false.
    // Wait for a running sample before closing shared memory.
    std::lock_guard lock(callbackMutex);
    MyClientBridgeShutdown();
    resourceChannel.Close();
    shulkerChannel.Close();
    tabListChannel.Close();
    skinChannel.Close();
    skinRequests.Close();
    hoverOn.store(false);
    {
        std::lock_guard guard(hoverMutex);
        hoverActive = false;
        hoverOwner = nullptr;
        countOwner = nullptr;
        itemCountShown = false;
        itemCountScreen = nullptr;
        hoverChannel.Close();
        itemCountChannel.Close();
    }
}
}

#ifndef MYCLIENT_GAME_VERSION
#define MYCLIENT_GAME_VERSION "1.26.51"
#endif
#ifndef MYCLIENT_LOADER_VERSION
#define MYCLIENT_LOADER_VERSION "26.51.6"
#endif

// Copies the textures the HUD's icons need out of the game folder (GameDataMirror.h), once per game exe,
// on a low-priority thread. Nothing is copied when the stamp already matches.
void StartGameDataMirror()
{
    std::thread([]
    {
        // Below normal, not lowest: r57-test56 logs never showed the copy finishing (no HUD icons) while the game used
        // every core; at the lowest priority it may never get to run before the game is closed.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        namespace fs = std::filesystem;
        const auto utf8 = [](const std::wstring& w) {
            std::string out;
            const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
            if (size > 0) { out.resize(size); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), size, nullptr, nullptr); }
            return out;
        };
        wchar_t exe[MAX_PATH * 2] = {};
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
        const fs::path root = MyClientGameData::MirrorRoot();
        if (!n || n >= MAX_PATH * 2 || root.empty()) {
            MyClientLog::Get().warn("Game textures for the HUD: no game exe path or LOCALAPPDATA; nothing copied.");
            return;
        }
        const fs::path gameDir = fs::path(exe).parent_path();
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExW(exe, GetFileExInfoStandard, &info)) {
            MyClientLog::Get().warn("Game textures for the HUD: the game exe cannot be read (error {}); nothing copied.", GetLastError());
            return;
        }
        const std::string stamp = "v3|" + utf8(gameDir.wstring()) + "|" +
            std::to_string((static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) + "|" +
            std::to_string((static_cast<unsigned long long>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime);
        if (MyClientGameData::ReadStamp(root) == stamp && MyClientGameData::HasLooseItemTextures(root.wstring())) {
            MyClientLog::Get().info("Game textures for the HUD: copy in '{}' is up to date.", utf8(root.wstring()));
            return;
        }
        try
        {
            std::error_code ec;
            const fs::path from = gameDir / L"data" / L"resource_packs";
            const fs::path to = root / L"data" / L"resource_packs";
            MyClientLog::Get().info("Game textures for the HUD: copying from the game folder (runs in the background).");
            fs::remove(root / L"stamp.txt", ec);
            if (ec) {
                MyClientLog::Get().warn("Game textures for the HUD: stamp cleanup failed ({}); archives 0 (entries 0, extracted 0, bad 0).", ec.message());
                return;
            }
            fs::remove_all(root / L"data", ec);
            if (ec) {
                // Keep going: loose files are copied with overwrite_existing, so they still win over old extracted ones,
                // and stopping here would leave no stamp, which makes the HUD ignore the mirror (TextureFolder).
                MyClientLog::Get().warn("Game textures for the HUD: old mirror cleanup failed ({}); copying over it.", ec.message());
                ec.clear();
            }
            size_t files = 0, failed = 0, packs = 0;
            size_t archives = 0, entries = 0, extracted = 0, bad = 0;
            struct ArchiveSource { fs::path file, packTarget; std::string dir; };
            std::vector<ArchiveSource> pendingArchives;
            unsigned long long bytes = 0;
            for (fs::directory_iterator pack(from, ec), end; !ec && pack != end; pack.increment(ec))
            {
                std::error_code inner;
                const bool directory = pack->is_directory(inner);
                if (inner) { ++failed; continue; }
                if (!directory) continue;
                packs++;
                for (fs::recursive_directory_iterator it(pack->path(), inner), stop; !inner && it != stop; it.increment(inner))
                {
                    std::error_code fileError;
                    const bool regular = it->is_regular_file(fileError);
                    if (fileError) { ++failed; continue; }
                    if (!regular) continue;
                    const fs::path relative = fs::relative(it->path(), pack->path(), fileError);
                    if (fileError) { ++failed; continue; }
                    std::string rel = utf8(relative.generic_wstring());
                    std::transform(rel.begin(), rel.end(), rel.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    if (MyClientGameData::StartsWith(rel, "__brarchive/") && MyClientGameData::EndsWith(rel, ".brarchive")) {
                        ++archives;
                        const auto dir = MyClientGameData::BrArchiveDir(rel);
                        if (dir.empty()) ++bad;
                        else pendingArchives.push_back({it->path(), to / pack->path().filename(), dir});
                        continue;
                    }
                    if (!MyClientGameData::Wanted(rel)) continue;
                    const auto size = it->file_size(fileError);
                    if (fileError) { ++failed; continue; }
                    const fs::path target = to / pack->path().filename() / relative;
                    fs::create_directories(target.parent_path(), fileError);
                    if (fileError) { ++failed; continue; }
                    if (fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, fileError)) { ++files; bytes += size; }
                    else ++failed;
                }
                if (inner) ++failed;
            }
            // All loose files are copied first, regardless of directory iteration order.
            // Parse the entire archive before any writes; keep at most one <=64 MiB payload in memory.
            for (const auto& archive : pendingArchives) {
                std::error_code readError;
                const auto size = fs::file_size(archive.file, readError);
                if (readError) { ++failed; continue; } // transient I/O must be retried next start
                if (size > 64ull * 1024 * 1024) { ++bad; continue; }
                std::ifstream input(archive.file, std::ios::binary);
                std::vector<uint8_t> data(static_cast<size_t>(size));
                if (!input || !input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
                    ++failed; continue;
                }
                std::vector<MyClientGameData::BrEntry> contents;
                size_t dataStart = 0;
                if (!MyClientGameData::ParseBrArchive(data.data(), data.size(), contents, dataStart)) { ++bad; continue; }
                entries += contents.size();
                for (const auto& entry : contents) {
                    std::string name = entry.name;
                    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                    const std::string rel = archive.dir + "/" + name;
                    if (!MyClientGameData::Wanted(rel)) continue;
                    const fs::path target = archive.packTarget / fs::path(rel);
                    std::error_code writeError;
                    fs::create_directories(target.parent_path(), writeError);
                    if (writeError) { ++failed; continue; }
                    // CREATE_NEW enforces loose-file priority, including Windows case-insensitive aliases.
                    HANDLE output = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (output == INVALID_HANDLE_VALUE) {
                        const auto error = GetLastError();
                        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) ++failed;
                        continue;
                    }
                    DWORD written = 0;
                    const bool ok = WriteFile(output, data.data() + dataStart + entry.offset, entry.size, &written, nullptr) && written == entry.size;
                    CloseHandle(output);
                    if (ok) { ++files; ++extracted; bytes += entry.size; }
                    else { ++failed; fs::remove(target, writeError); } // never leave a truncated extracted image
                }
            }
            if (ec || !files)
            {
                MyClientLog::Get().warn("Game textures for the HUD: nothing copied from '{}' ({}); archives {} (entries {}, extracted {}, bad {}).",
                                      utf8(from.wstring()), ec ? ec.message() : "no files", archives, entries, extracted, bad);
                return;
            }
            // The HUD uses the mirror only while stamp.txt is not empty (TextureFolder), so a copy with some
            // failures still gets a stamp. "|partial" makes it differ from the expected stamp, so the next start
            // retries the copy, while the HUD keeps the icons that were copied.
            const bool partial = failed != 0;
            {
                std::ofstream out(root / L"stamp.txt", std::ios::binary);
                out << stamp << (partial ? "|partial" : "");
                out.close();
                if (!out) ++failed;
            }
            MyClientLog::Get().info("Game textures for the HUD: copied {} files ({} KB, {} failed) of {} packs to '{}'; archives {} (entries {}, extracted {}, bad {}).{}",
                                  files, bytes / 1024, failed, packs, utf8(root.wstring()), archives, entries, extracted, bad,
                                  failed ? " Incomplete copy; retried on next start." : "");
        }
        catch (const std::exception& e)
        {
            MyClientLog::Get().warn("Game textures for the HUD: copy stopped: {}", e.what());
        }
    }).detach();
}

// The two steps of starting MyClient, whoever calls them: LeviLamina's mod loader (ll_mod_load / ll_mod_enable
// below) or MyClient's own start without LeviLamina (client/Standalone.cpp).
namespace MyClientStart {
bool Load();
bool Enable();
}

#if !MYCLIENT_STANDALONE
extern "C" __declspec(dllexport) bool ll_mod_load(ll::mod::NativeMod& self)
{
    mod = &self;
    const auto game = ll::getGameVersion();
    const auto loader = ll::getLoaderVersion();
    // Expected versions come from tools/sdks/<game>.lock.json at build time (default: 26.51).
    unsigned gm = 0, gn = 0, gp = 0, lm = 0, ln = 0, lp = 0;
    const bool parsed = std::sscanf(MYCLIENT_GAME_VERSION, "%u.%u.%u", &gm, &gn, &gp) == 3 &&
                        std::sscanf(MYCLIENT_LOADER_VERSION, "%u.%u.%u", &lm, &ln, &lp) == 3;
    if (!ll::isClient() || !parsed ||
        game.major != gm || game.minor != gn || game.patch != gp || game.preRelease ||
        loader.major != lm || loader.minor != ln || loader.patch != lp || loader.preRelease)
    {
        MyClientLog::Get().error(
            "Requires Minecraft {} and LeviLamina Client {}; found game {}, loader {}.",
            MYCLIENT_GAME_VERSION, MYCLIENT_LOADER_VERSION, game.to_string(), loader.to_string());
        return false;
    }
    // 2026-11-03: latest.log copied a few minutes after the test ended at a line cut in half; the file sink keeps
    // lines in memory (flush level Off) until its buffer fills or the game closes. Every line is written at once.
    try { self.getLogger().setFlushLevel(ll::io::LogLevel::Info); } catch (...) {}
    return MyClientStart::Load();
}
extern "C" __declspec(dllexport) bool ll_mod_enable(ll::mod::NativeMod&) { return MyClientStart::Enable(); }
#endif

bool MyClientStart::Load()
{
    // Before anything else is touched: is this a game build sigs.json supports, and does every fatal signature
    // match? If not, nothing is hooked (docs/version-update.md says what to do with the logs).
    const MyClientSigs::Verdict verdict = MyClientSigs::Check(MYCLIENT_GAME_VERSION, true);
    if (!verdict.start)
    {
        MyClientLog::Get().error("OriginX does not start on this Minecraft ({}): {}. See logs/startup.log and logs/sigcheck.log.",
                                 verdict.version, verdict.why);
        MyClientSigs::ShowNotStarted(verdict.version, verdict.why);
        return false;
    }
    MyClientLog::Get().info("Crash diagnostics: unhandled-exception minidump installed={} (local logs folder; best effort)", MyClientCrash::Install());
    MyClientLog::Get().info("Crash diagnostics: reload AV observer installed={} (first chance only; four candidate slots plus one unhandled slot)", MyClientCrash::vectoredHandler != nullptr);
    loaded.store(true);
    // A start-up that stops before OriginX is enabled writes every thread's call stack to logs/startup.log (HangWatch.h).
    // Standalone: the enable step waits for the game window, up to 120 s after load (Standalone.cpp), so a slow
    // first start is not a hang there before 150 s.
#if MYCLIENT_STANDALONE
    MyClientDiag::StartHangWatch(enableFinished, 150);
#else
    MyClientDiag::StartHangWatch(enableFinished);
#endif
    // Before any of MyClient's hooks patch them: the first bytes of every game function MyClient uses.
    MyClientGame::SnapshotSignatures(MYCLIENT_GAME_VERSION);
    // The game's code as it sits in memory, once per game build, for finding functions without LeviLamina (GameImageDump.cpp).
    MyClientGame::DumpGameImage(MYCLIENT_GAME_VERSION, [](const std::string& line) {
        try { MyClientLog::Get().info("{}", line); } catch (...) {}
    });
    MyClientLog::Get().info("OriginX loaded (built " __DATE__ " " __TIME__ " UTC): arrows, target coordinates, render FPS, armor, hold-to-zoom, full bright and Hive party acceptance.");
    StartGameDataMirror();
    // Before the game makes its window's swap chain (enable comes ~8 s later, after it): only its creation is
    // hooked here, a D3D12 device cannot be made this early.
    if (!MyClientGame::StartDrawing(false))
        MyClientLog::Get().warn("In-game drawing could not start at load; it is tried again when the mod is enabled.");
    return true;
}

bool MyClientStart::Enable()
{
    if (enabled.load()) return true;
    struct Finished { ~Finished() { enableFinished.store(true); } } finished;   // every return below counts
    // Before any hook: LeviLamina's stubs still lead to the game here (SignatureDump.cpp).
    MyClientGame::ResolveSignaturesBeforeHooks();
    // Real addresses of the functions only called (not hooked), for running without LeviLamina (SignatureDump.cpp).
    MyClientGame::FindRealAddresses(MYCLIENT_GAME_VERSION, [](const std::string& line) {
        try { MyClientLog::Get().info("{}", line); } catch (...) {}
    });
    if (!MyClientBridgeInitialize())
    {
        MyClientLog::Get().error("Cannot create HUD channel (Windows error {}).", GetLastError());
        return false;
    }
    warned = false;
    if (!resourceChannel.Open(GetCurrentProcessId(), true))
        MyClientLog::Get().warn("Resource counter unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!shulkerChannel.Open(GetCurrentProcessId(), true))
        MyClientLog::Get().warn("Shulker preview unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!tabListChannel.Open(GetCurrentProcessId(), true))
        MyClientLog::Get().warn("Tab list unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!skinChannel.Open(GetCurrentProcessId(), true))
        MyClientLog::Get().warn("Skin picture unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!itemCountChannel.Open(GetCurrentProcessId(), true))
        MyClientLog::Get().warn("Item Counter unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!skinRequests.Open(GetCurrentProcessId(), true))
        MyClientLog::Get().warn("Closet skin switching unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    try
    {
        if (!MyClientGame::EnableFeatures())
            MyClientLog::Get().error("Zoom/FPS/armor unavailable: game symbol, hook or feature channel initialization failed. Arrow/target sampling remains enabled.");
        EnableShulkerHover(MyClientLog::Get());
        HookClientPacketHandler(MyClientLog::Get());
        LoadClosetSkin(MyClientLog::Get());
        if (!MyClientGame::EnableParty())
            MyClientLog::Get().error("Hive party unavailable: receive hook or party channel initialization failed.");
        if (!worldHooked)
        {
            static bool tickIn = false, exitIn = false;
            const int tick = tickIn ? 0 : LevelTickHook::hook();
            const int exit = exitIn ? 0 : LevelExitHook::hook();
            tickIn = tick == 0;
            exitIn = exit == 0;
            worldHooked = tickIn && exitIn;
            if (!worldHooked)
                MyClientLog::Get().error("World hooks failed (ClientLevel::_subTick {}, ClientInstance::onLevelExit {}).", tick, exit);
        }
    }
    catch (...)
    {
        Disable();
        MyClientLog::Get().error("Client event registration failed; rolled back subscriptions.");
        return false;
    }
    if (!worldHooked)
    {
        Disable();
        MyClientLog::Get().error("Cannot follow the client's world (tick / exit hooks).");
        return false;
    }
    enabled.store(true);
    MyClientLog::Get().info("OriginX enabled; waiting for a world.");
    // Functions LeviLamina has no symbol for, by address (GameRva.cpp). Some hooks go in later (in a world, or when
    // a feature is first switched on), so the count is written again a minute later.
    MyClientLog::Get().info("Game functions by address: {}", MyClientGame::RvaReport());
#if MYCLIENT_OWN_HOOKS
    MyClientLog::Get().info("Own hooks (MinHook, not LeviLamina's): {} put in{}{}; MyClient's own log: {}", MyClientHook::Installed(),
                            *MyClientHook::LastError() ? "; last refusal " : "", MyClientHook::LastError(),
                            std::filesystem::path(MyClientLog::FilePath()).string());
#endif
#if MYCLIENT_OWN_HOOKS
    if (const std::string off = MyClientHook::DisabledReport(); !off.empty())
        MyClientLog::Get().warn("Hooks left out by disabled-hooks.txt (diagnostics; their features stay off): {}", off);
#endif
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        try { if (loaded.load()) MyClientLog::Get().info("Game functions by address (60 s): {}", MyClientGame::RvaReport()); } catch (...) {}
#if MYCLIENT_OWN_HOOKS
        try { MyClientLog::Get().info("Own hooks (60 s): {} put in{}{}", MyClientHook::Installed(),
                                      *MyClientHook::LastError() ? "; last refusal " : "", MyClientHook::LastError()); } catch (...) {}
        // The busiest hooks, every 30 s from now on (the FPS regression of 2026-11: which hook runs how often).
        (void)MyClientHook::CallReport(1.0);   // start counting from here
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            try { MyClientLog::Get().info("Hook calls per second (30 s): {}", MyClientHook::CallReport(30.0)); } catch (...) {}
        }
#endif
    }).detach();
    // Bytes at each game function MyClient uses, for signatures that work without LeviLamina (SignatureDump.cpp).
    MyClientGame::StartSignatureDump(MYCLIENT_GAME_VERSION, [](const std::string& line) {
        try { if (loaded.load()) MyClientLog::Get().info("{}", line); } catch (...) {}
    });
    MyClientGame::StartPatchScan(MYCLIENT_GAME_VERSION, [](const std::string& line) {
        try { if (loaded.load()) MyClientLog::Get().info("{}", line); } catch (...) {}
    });
    return true;
}

#if !MYCLIENT_STANDALONE
extern "C" __declspec(dllexport) bool ll_mod_disable(ll::mod::NativeMod&)
{
    Disable();
    // The 60 s snapshot may precede joining a world. Include functions first used during play.
    try { MyClientLog::Get().info("Game functions by address (disable): {}", MyClientGame::RvaReport()); } catch (...) {}
#if MYCLIENT_OWN_HOOKS
    try { MyClientLog::Get().info("Own hooks (disable): {} put in", MyClientHook::Installed()); } catch (...) {}
#endif
    return true;
}
#endif

// No ll_mod_unload: MyClient's hooks stay in the game's code.
// Disable is supported; unload/replacement requires restarting Minecraft.

#include "GameData.h"
#include "FeatureAdapter.h"
#include "PartyAdapter.h"
#include "ResourceChannel.h"
#include "ShulkerChannel.h"
#include "TabListChannel.h"
#include "SkinChannel.h"
#include "GameSymbols.h"
#include "Profile.h"
#include "Compositor.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"

#include "ll/api/Versions.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/ItemStack.h"
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

#include <algorithm>
#include <atomic>
#include <memory>
#include <cmath>
#include <functional>
#include <utility>
#include <cstdio>
#include <mutex>

namespace {
std::atomic<bool> enabled{false};
std::mutex callbackMutex;
ll::event::ListenerPtr tickListener;
ll::event::ListenerPtr exitListener;
ll::mod::NativeMod* mod = nullptr;
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
                    mod->getLogger().info("Skin: {}x{} format {} ({} bytes) geometry='{}' persona={} -> {}", image.mWidth, image.mHeight,
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
    LL_TYPE_INSTANCE_HOOK(Name, ll::memory::HookPriority::Normal, ClientPacketHandler,                                  \
                          ll::memory::SymbolView{"?handle@LegacyClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBV" \
                                                 #PacketType "@@@Z"},                                                   \
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
std::atomic<int> closetCanUse{-1}, closetKeep{-1};   // -1 = not asked, else the game's own answer
constexpr const char* CanUseSkinSymbol =
    "?canUseSkin@ClientLevel@@UEBA_NAEBVSerializedSkinRef@@AEBVNetworkIdentifier@@AEBUActorUniqueID@@@Z";
constexpr const char* KeepSkinSymbol =
    "?_shouldKeepSkinBasedOnMemory@LegacyClientNetworkHandler@@AEBA_NAEBVUUID@mce@@AEBVSerializedSkinRef@@@Z";
LL_TYPE_INSTANCE_HOOK(CanUseSkinHook, ll::memory::HookPriority::Normal, SkinCheckGate, ll::memory::SymbolView{CanUseSkinSymbol},
                      bool, SerializedSkinRef const& skin, NetworkIdentifier const& source, ActorUniqueID const& player)
{
    const bool ok = origin(skin, source, player);
    if (!closetHandingSkin) return ok;
    closetCanUse.store(ok ? 1 : 0);
    return true;
}
LL_TYPE_INSTANCE_HOOK(KeepSkinHook, ll::memory::HookPriority::Normal, SkinCheckGate, ll::memory::SymbolView{KeepSkinSymbol},
                      bool, mce::UUID const& playerId, SerializedSkinRef const& skin)
{
    const bool ok = origin(playerId, skin);
    if (!closetHandingSkin) return ok;
    closetKeep.store(ok ? 1 : 0);
    return true;
}
const char* SkinCheckText(int verdict) { return verdict < 0 ? "not asked" : verdict ? "yes" : "no (let through)"; }

void HookClientPacketHandler(ll::io::Logger& log)
{
    {
        const bool canUse = ll::memory::SymbolView{CanUseSkinSymbol}.resolve(true) && CanUseSkinHook::hook() == 0;
        const bool keep = ll::memory::SymbolView{KeepSkinSymbol}.resolve(true) && KeepSkinHook::hook() == 0;
        log.info("Closet: skin checks hooked canUseSkin={} keepByMemory={}", canUse, keep);
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
    const auto entry = list.find(player.getUuid());
    if (entry == list.end()) return false;
    entry->second.mSkin->mSkinImpl = skin;
    return true;
}

void WearRequestedSkin(ClientInstance& client, Level& level, Player& player)
{
    static MyClientSkin::Request request;   // ~256 KB: not on the stack
    const DWORD pid = GetCurrentProcessId();
    if (!mod || !skinRequests.TakePending(pid, request)) return;
    auto& log = mod->getLogger();
    MyClientSkin::Result result = MyClientSkin::Result::Failed;
    try
    {
        using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
        using UpdateSkinFn = void (*)(Player*, SerializedSkinRef const&, int);
        static const auto createPacket = reinterpret_cast<PacketFactory>(ll::memory::SymbolView{
            "?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z"}.resolve());
        static const auto updateSkin = reinterpret_cast<UpdateSkinFn>(ll::memory::SymbolView{
            "?updateSkin@Player@@QEAAXAEBVSerializedSkinRef@@H@Z"}.resolve());
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
            SerializedSkinImpl& old = owner->mObject;
            SerializedSkinImpl& impl = own ? fresh->mObject : old;
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
                impl.mPlayFabId = old.mPlayFabId.get();
                impl.mCapeId = old.mCapeId.get();
                mce::Image cape(old.mCapeImage.get());
                static_cast<mce::Image&>(impl.mCapeImage.get()) = std::move(cape);
                impl.mSkinColor = old.mSkinColor.get();
                impl.mIsPrimaryUser = static_cast<bool>(old.mIsPrimaryUser);
                impl.mAnimationData = std::string{};
            }
            static_cast<mce::Image&>(impl.mSkinImage.get()) = std::move(image);

            std::vector<uint8_t> pixels(request.rgba, request.rgba + static_cast<size_t>(w) * h * 4);
            char id[48];
            std::snprintf(id, sizeof(id), "MyClient_%08x%s", MyClientSkin::Hash(pixels, w, h), slim ? "_slim" : "");
            impl.mId = std::string(id);
            impl.mFullId = std::string(id) + impl.mCapeId.get();
            impl.mResourcePatch = std::string(MyClientSkin::ResourcePatch(slim));
            impl.mDefaultGeometryName = std::string(slim ? "geometry.humanoid.customSlim" : "geometry.humanoid.custom");
            impl.mArmSizeType = slim ? SharedTypes::persona::ArmSizeType::Slim : SharedTypes::persona::ArmSizeType::Wide;
            impl.mSkinAnimatedImages->clear();   // a persona's blinking eyes would cover the new face
            impl.mPersonaPieces->clear();
            impl.mPieceTintColors->clear();
            impl.mIsPersonaCapeOnClassicSkin = wasPersona && !impl.mCapeId->empty();
            impl.mIsPersona = false;
            impl.mIsPremium = false;
            impl.mIsTrustedSkin = TrustedSkinFlag::True;   // an untrusted skin may be drawn as a default one

            const auto wearing = [&] {
                auto* now = ref ? ref->mSkinImpl.get() : nullptr;
                return now && now->mObject.mId.get() == id;
            };
            bool sent = false, handled = false, viaHandler = false, viaUpdate = false, listed = false, direct = false;
            closetCanUse.store(-1);
            closetKeep.store(-1);
            if (skinPacket)
            {
                skinPacket->mUUID = player.getUuid();
                skinPacket->mSkin->mSkinImpl = own ? fresh : ref->mSkinImpl;
                skinPacket->mLocalizedNewSkinName = name;
                skinPacket->mLocalizedOldSkinName = std::string{};
                if (client.getPacketSender().isInitialized())
                {
                    client.getPacketSender().sendToServer(*skinPacket);
                    sent = true;
                }
                try { handled = HandleSkinLocally(*skinPacket); } catch (...) { handled = false; }
                viaHandler = handled && wearing();
                if (!viaHandler && updateSkin)
                {
                    try { updateSkin(&player, *skinPacket->mSkin, 0); } catch (...) {}
                    viaUpdate = wearing();
                }
                if (!wearing() && own)
                {
                    ref->mSkinImpl = fresh;   // the player holds the new skin object at least
                    direct = true;
                }
                try { listed = PutSkinInPlayerList(level, player, skinPacket->mSkin->mSkinImpl); } catch (...) { listed = false; }
            }
            log.info("Closet: wearing \"{}\" ({}x{}, {} arms{}){}{}{}{}{}", name, w, h, slim ? "slim" : "wide",
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
            result = sent ? MyClientSkin::Result::Sent : MyClientSkin::Result::Failed;
        }
    }
    catch (...)
    {
        log.error("Closet: wearing the skin failed with an exception.");
    }
    skinRequests.Finish(pid, request.serial, result);
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
    if (!shulkers.Begin(stack.getTypeName())) return;
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
// Member function returning a reference: same x64 ABI as a free function taking `this` first.
using SlotItemFn = ItemStack const& (*)(ContainerScreenController const*, std::string const&, int);
SlotItemFn slotItem = nullptr;

// Hover popup: the stacks of a shulker box item by slot (NBT "Slot"), so the HUD can draw the box's grid.
// False when an entry cannot be trusted (the popup then stays hidden).
bool ReadShulkerSlots(MyClientShulker::HoverBox& box, ItemStack const& stack)
{
    box = {};
    MyClientShulker::CopyName(box.name, sizeof(box.name), stack.getTypeName());
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
        if (stack.mCount && MyClientShulker::IsShulkerBox(stack.getTypeName())) found = ReadShulkerSlots(box, stack);
    }
    std::lock_guard guard(hoverMutex);
    hoverActive = found;
    hoverBox = found ? box : MyClientShulker::HoverBox{};
    hoverOwner = screen;
    PublishHover(GetTickCount64());
}

void OnSlotUnhovered(ContainerScreenController const* screen)
{
    std::lock_guard guard(hoverMutex);
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
void HideGameTooltip(ContainerScreenController* screen)
{
    if (!screen || !MyClientGame::ShulkerHoverWanted()) return;
    std::lock_guard guard(hoverMutex);
    if (hoverActive && hoverOwner == screen) screen->mShowHoverText = false;
}

void ClearHover()
{
    std::lock_guard guard(hoverMutex);
    hoverActive = false;
    hoverBox = {};
    hoverOwner = nullptr;
    PublishHover(0);
}

LL_TYPE_INSTANCE_HOOK(SlotHoveredHook, ll::memory::HookPriority::Normal, ContainerScreenController,
                      MyClientGame::Symbols::SlotHovered(), ui::ViewRequest, std::string const& collectionName, int index)
{
    const auto result = origin(collectionName, index);
    if (hoverOn.load(std::memory_order_relaxed))
        try {
            MyClientProfile::Scope prof(MyClientProfile::Screen);
            OnSlotHovered(this, collectionName, index);
            HideGameTooltip(this);
        } catch (...) {}
    return result;
}

LL_TYPE_INSTANCE_HOOK(SlotUnhoveredHook, ll::memory::HookPriority::Normal, ContainerScreenController,
                      MyClientGame::Symbols::SlotUnhovered(), ui::ViewRequest, std::string const& collectionName, int index)
{
    const auto result = origin(collectionName, index);
    if (hoverOn.load(std::memory_order_relaxed))
        try { OnSlotUnhovered(this); } catch (...) {}
    return result;
}

LL_TYPE_INSTANCE_HOOK(ScreenTickHook, ll::memory::HookPriority::Normal, ContainerScreenController,
                      MyClientGame::Symbols::ScreenTick(), ui::DirtyFlag)
{
    const auto result = origin();
    if (hoverOn.load(std::memory_order_relaxed))
        try {
            MyClientProfile::Scope prof(MyClientProfile::Screen);
            OnScreenTick(this);
            HideGameTooltip(this);   // after the game's tick, which may switch its tooltip back on
        } catch (...) {}
    return result;
}

// Returns false when the popup cannot work in this game build (the reason is logged).
bool EnableShulkerHover(ll::io::Logger& log)
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

void PublishEmpty()
{
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
            ll::mod::NativeMod::current()->getLogger().info("Health Warning: health={}", getHealth != nullptr);
        }
        if (!getHealth) return 0;
        return MyClientFeatures::EncodeHealth(getHealth(player.getEntityContext()));
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
            ll::mod::NativeMod::current()->getLogger().info("Debug Menu: biome={} light={}", getBiome != nullptr, getLight != nullptr);
        }
        BlockSource* region = getRegion ? &(player.*getRegion)() : client.getRegion();
        if (!region) return debug;
        if (hit.mType == HitResultType::Tile)
        {
            debug.lookX = hit.mBlock.x; debug.lookY = hit.mBlock.y; debug.lookZ = hit.mBlock.z;
            debug.lookFace = hit.mFacing <= 5 ? hit.mFacing : MyClientFeatures::NoLook;
            CopyName(debug.lookBlock, sizeof(debug.lookBlock), region->getBlock(hit.mBlock).getTypeName());
        }
        // The feet: the bottom of the player's box (the position of a player is at the eyes).
        const AABB& box = player.getAABB();
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
        ItemStack const& held = player.getSelectedItem();
        const std::string heldId = held.mItem ? held.getTypeName() : std::string{};
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
        look.canHarvest = !look.requiresTool || (held.mItem && held.mItem->canDestroySpecial(block)) ? 1 : 0;   // virtual
        look.known = 1;

        static std::vector<std::string> logged;
        const std::string& id = block.getTypeName();
        if (logged.size() < 40 && std::find(logged.begin(), logged.end(), id) == logged.end())
        {
            logged.push_back(id);
            ll::mod::NativeMod::current()->getLogger().info("Block Details: {} name='{}' requiresTool={} harvest={} held={} tags={}", id,
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
    const auto name = stack.getTypeName();
    counter.Add(name, count);
    resources.Add(name, count);
}

void Sample(ll::event::ClientLevelTickEvent& event)
{
    MyClientProfile::Scope prof(MyClientProfile::Tick);
    std::lock_guard lock(callbackMutex);
    if (!enabled.load()) return;
    auto frame = MyClientGame::EmptyFrame();
    try
    {
        auto client = ll::service::getClientInstance();
        auto level = ll::service::getMultiPlayerLevel();
        // Never read the integrated server's player, another world or a cached
        // player from before a world transition. Resolve everything per tick.
        if (!client || !level || &event.level() != &*level)
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
                }
            }
            catch (...) {}
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
                    // folded function): fall back to LeviLamina's getHeadPos and the client's own region.
                    BlockSource* regionPtr = self && getRegion ? &(self->*getRegion)() : client->getRegion();
                    static bool reported = false;
                    if (!reported) {
                        reported = true;
                        ll::mod::NativeMod::current()->getLogger().info(
                            "Hitbox: eye={} region={} type={} spectator={}", getEye ? "game" : "getHeadPos",
                            getRegion ? "actor" : (regionPtr ? "client" : "none"), getType != nullptr, isSpectator != nullptr);
                    }
                    if (self && regionPtr)
                    {
                        const Vec3 eye = getEye ? (self->*getEye)() : self->getHeadPos();
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
                            if (other->isPlayer()) kind = 0;
                            else if (other->hasCategory(ActorCategory::Item) || (getType && (other->*getType)() == ActorType::ItemEntity)) kind = 1;
                            else if (other->hasCategory(ActorCategory::Mob)) kind = 3;
                            else kind = 2;
                            if (!((filter >> kind) & 1u)) continue;
                            if (kind == 0 && isSpectator && (other->*isSpectator)()) continue;
                            const AABB& bb = other->getAABB();
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
                            const AABB& aabb = cand.actor->getAABB();
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

        MyClientGame::TickParty();
        MyClientGame::TickAutoRq();
        MyClientGame::ApplyRenderDistance();
        MyClientGame::SampleArmor();
        MyClientGame::SampleClock(level->getTime(), SampleDebug(*client, *player, level->getHitResult()),   // getTime: virtual (ILevel)
                                  SampleLook(*client, *player, level->getHitResult()), SampleHealth(*player));
        WearRequestedSkin(*client, *level, *player);
        PublishSkin(*player);
        auto& inventory = player->getInventory();
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
                if (scanShulkers && stack.mCount && MyClientShulker::IsShulkerBox(stack.getTypeName()))
                    ReadShulker(shulkers, stack);
            }
            if (scanShulkers) PublishShulkers(shulkers.Result(), shulkers.Valid());
            AddStack(counter, resources, player->getOffhandSlot());
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
            mod->getLogger().warn("HUD publication failed (Windows error {}).", GetLastError());
        }
    }
    catch (...)
    {
        PublishEmpty();
        if (!warned)
        {
            warned = true;
            mod->getLogger().error("Game data read failed; the HUD snapshot was cleared.");
        }
    }
}

void OnExit(ll::event::ClientExitLevelEvent&)
{
    std::lock_guard lock(callbackMutex);
    if (enabled.load()) PublishEmpty();
    clientPacketHandler.store(nullptr);   // the next world's StartGame brings the new one
}

void Disable()
{
    enabled.store(false);
    MyClientGame::DisableFeatures();
    MyClientGame::DisableParty();
    auto& bus = ll::event::EventBus::getInstance();
    if (tickListener) bus.removeListener(tickListener);
    if (exitListener) bus.removeListener(exitListener);
    tickListener.reset();
    exitListener.reset();
    // Removal does not cancel callbacks already copied by EventBus. They see
    // enabled=false. Wait for a running sample before closing shared memory.
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
        hoverChannel.Close();
    }
}
}

#ifndef MYCLIENT_GAME_VERSION
#define MYCLIENT_GAME_VERSION "1.26.51"
#endif
#ifndef MYCLIENT_LOADER_VERSION
#define MYCLIENT_LOADER_VERSION "26.51.6"
#endif

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
        self.getLogger().error(
            "Requires Minecraft {} and LeviLamina Client {}; found game {}, loader {}.",
            MYCLIENT_GAME_VERSION, MYCLIENT_LOADER_VERSION, game.to_string(), loader.to_string());
        return false;
    }
    self.getLogger().info("MyClientData loaded: arrows, target coordinates, render FPS, armor, hold-to-zoom, full bright and Hive party acceptance (live test pending).");
    // Before the game makes its window's swap chain (enable comes ~8 s later, after it): only its creation is
    // hooked here, a D3D12 device cannot be made this early.
    if (!MyClientGame::StartDrawing(false))
        self.getLogger().warn("In-game drawing could not start at load; it is tried again when the mod is enabled.");
    return true;
}

extern "C" __declspec(dllexport) bool ll_mod_enable(ll::mod::NativeMod& self)
{
    if (enabled.load()) return true;
    if (!MyClientBridgeInitialize())
    {
        self.getLogger().error("Cannot create HUD channel (Windows error {}).", GetLastError());
        return false;
    }
    warned = false;
    if (!resourceChannel.Open(GetCurrentProcessId(), true))
        self.getLogger().warn("Resource counter unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!shulkerChannel.Open(GetCurrentProcessId(), true))
        self.getLogger().warn("Shulker preview unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!tabListChannel.Open(GetCurrentProcessId(), true))
        self.getLogger().warn("Tab list unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!skinChannel.Open(GetCurrentProcessId(), true))
        self.getLogger().warn("Skin picture unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    if (!skinRequests.Open(GetCurrentProcessId(), true))
        self.getLogger().warn("Closet skin switching unavailable: shared memory could not be created (Windows error {}).", GetLastError());
    try
    {
        if (!MyClientGame::EnableFeatures())
            self.getLogger().error("Zoom/FPS/armor unavailable: game symbol, hook or feature channel initialization failed. Arrow/target sampling remains enabled.");
        EnableShulkerHover(self.getLogger());
        HookClientPacketHandler(self.getLogger());
        if (!MyClientGame::EnableParty())
            self.getLogger().error("Hive party unavailable: receive hook or party channel initialization failed.");
        auto& bus = ll::event::EventBus::getInstance();
        tickListener = bus.emplaceListener<ll::event::ClientLevelTickEvent>(Sample);
        exitListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>(OnExit);
    }
    catch (...)
    {
        Disable();
        self.getLogger().error("Client event registration failed; rolled back subscriptions.");
        return false;
    }
    if (!tickListener || !exitListener)
    {
        Disable();
        self.getLogger().error("Cannot subscribe to client world events.");
        return false;
    }
    enabled.store(true);
    self.getLogger().info("MyClientData enabled; waiting for a world.");
    return true;
}

extern "C" __declspec(dllexport) bool ll_mod_disable(ll::mod::NativeMod&)
{
    Disable();
    return true;
}

// No ll_mod_unload: EventBus snapshots may still hold callbacks after removal.
// Disable is supported; unload/replacement requires restarting Minecraft.

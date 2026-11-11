#pragma once
// Small reads of the game's objects that MyClient used to import from LeviLamina.dll (2026-11-06, step 4 of leaving
// LeviLamina on 26.51). They are not the game's code: LeviLamina wrote them itself from the same member layouts
// (src/mc/world/actor/Actor.cpp, player/Player.cpp, item/ItemStackBase.cpp, level/block/BlockType.cpp,
// client/renderer/game/LevelRenderer.cpp of LeviLamina 26.51.6), so MyClient does the same reads here.
//
// Components come from the actor's EnTT registry (EntityContext::tryGetComponent, header only). EnTT keys a
// component by the hash of its type name: the game's (26.51.1) image holds the hashes of the bare names
// ("PlayerComponent" 705 times, "UserEntityIdentifierComponent" 433, "ActorEquipmentComponent" 17; the
// "struct ..." forms never), which is what clang-cl's __PRETTY_FUNCTION__ gives this DLL too.
//
// Each returns a pointer (nullptr = not there) where LeviLamina returned a reference, so callers check it.
#include <cstdint>
#include <random>
#include <string>

#include "GameSymbols.h"
#include "mc/client/renderer/chunks/RenderChunkCoordinator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/entity/components/ActorEquipmentComponent.h"
#include "mc/entity/components/UserEntityIdentifierComponent.h"
#include "mc/platform/UUID.h"
#include "mc/world/SimpleContainer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/ContainerID.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

namespace MyClientGame {

inline EntityContext& EntityOf(Actor& actor) { return actor.mEntityContext.get(); }
inline EntityContext const& EntityOf(Actor const& actor) { return actor.mEntityContext.get(); }

// Actor::getAABB
inline AABB const* AabbOf(Actor const& actor)
{
    gsl::not_null<AABBShapeComponent*> const& stored = actor.mBuiltInComponents->mAABBShapeComponent;
    AABBShapeComponent const* shape = stored.get();
    if (!shape) return nullptr;
    AABB const& aabb = shape->mAABB; // Convert TypedStorage to its contained AABB first.
    return &aabb;
}

// Actor::isPlayer (LeviLamina: has a PlayerComponent; the actor's categories carry the same)
inline bool IsPlayer(Actor const& actor) { return actor.hasCategory(ActorCategory::Player); }

// Player::getInventory
inline Inventory* InventoryOf(Player const& player)
{
    PlayerInventory* inventory = player.mInventory.get();
    if (!inventory) return nullptr;
    return inventory->mInventory.get();
}

// Player::getSelectedItem (nullptr where LeviLamina gave ItemStack::EMPTY_ITEM)
inline ItemStack const* SelectedItemOf(Player const& player)
{
    PlayerInventory* inventory = player.mInventory.get();
    if (!inventory || !inventory->mInventory) return nullptr;
    if (inventory->mSelectedContainerId != ContainerID::Inventory) return nullptr;
    return &inventory->mInventory->getItem(inventory->mSelected);   // Container::getItem is virtual
}

// Actor::getOffhandSlot / getArmor: ActorEquipment::getHandContainer(...).getItem(1) / getArmorContainer(...).getItem(slot)
inline ItemStack const* OffhandOf(Actor const& actor)
{
    auto equipment = EntityOf(actor).tryGetComponent<ActorEquipmentComponent>();
    if (!equipment || !equipment->mHand) return nullptr;
    return &equipment->mHand->getItem(1);
}
inline ItemStack const* ArmorOf(Actor const& actor, int slot)
{
    auto equipment = EntityOf(actor).tryGetComponent<ActorEquipmentComponent>();
    if (!equipment || !equipment->mArmor) return nullptr;
    return &equipment->mArmor->getItem(slot);
}

// Player::getUuid (the UserEntityIdentifierComponent's client UUID; zero when the player has none)
inline mce::UUID UuidOf(Player const& player)
{
    auto identifier = EntityOf(player).tryGetComponent<UserEntityIdentifierComponent>();
    if (!identifier) return {};
    return identifier->mClientUUID;
}

// ItemStackBase::getTypeName ("minecraft:stone"; LeviLamina: Item::getSerializedName = the item's full name)
inline std::string TypeNameOf(ItemStackBase const& stack)
{
    Item const* item = stack.mItem.get();
    if (!item) return {};
    return item->mFullName->getString();
}

// BlockType::getTypeName / Block::getTypeName
inline std::string const& TypeNameOf(BlockType const& type) { return type.mNameInfo->mFullName->getString(); }
inline std::string const& TypeNameOf(Block const& block) { return TypeNameOf(block.getBlockType()); }

// ItemStackBase::isEnchanted (the item's NBT has an "ench" tag)
inline bool IsEnchanted(ItemStackBase const& stack)
{
    CompoundTag const* tag = stack.mUserData.get();
    return tag && tag->contains("ench");
}

// mce::UUID::random
inline mce::UUID RandomUuid()
{
    thread_local std::mt19937_64 random{std::random_device{}() ^ (static_cast<uint64_t>(std::random_device{}()) << 32)};
    return mce::UUID{random(), random()};
}

// LevelRenderer::rebuildAllRenderChunkGeometry: every dimension's chunks dirty (RenderChunkCoordinator::_setAllDirty,
// the game's code, looked up by name). false = the game function was not found.
inline bool RebuildAllChunks(LevelRenderer& renderer)
{
    using SetAllDirty = void (*)(RenderChunkCoordinator*, bool, bool);
    static const auto setAllDirty = ResolveFunction<SetAllDirty>("?_setAllDirty@RenderChunkCoordinator@@QEAAX_N0@Z");
    if (!setAllDirty) return false;
    bool requested = false;
    for (auto& [dimension, coordinator] : renderer.mRenderChunkCoordinators.get())
        if (coordinator) {
            setAllDirty(coordinator.get(), false, false);
            requested = true;
        }
    return requested;
}

}  // namespace MyClientGame

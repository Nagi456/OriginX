#pragma once
#include "ll/api/memory/Symbol.h"
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Game functions the SDK declares (MCAPI) but LeviLamina.dll does not export: linking them directly
// fails, so they are looked up by their MSVC-mangled name at runtime. A null result means this game
// build lacks the symbol; callers must check it and switch the feature off instead of crashing.
namespace MyClientGame {
template <class Fn>
Fn ResolveMember(const char* symbol)
{
    static_assert(sizeof(Fn) == sizeof(void*), "expects a single-inheritance member function pointer");
    Fn fn{};
    if (void* address = ll::memory::SymbolView{symbol}.resolve(true)) std::memcpy(&fn, &address, sizeof(address));
    return fn;
}

// A member function of a class with several bases (its member pointers are 16 bytes), called as a plain function:
// on x64 MSVC `this` comes first and a returned object goes through a hidden pointer right after it.
template <class Fn>
Fn ResolveFunction(const char* symbol)
{
    return reinterpret_cast<Fn>(ll::memory::SymbolView{symbol}.resolve(true));
}

// First symbol of the list that this game build has, as a hook target (0 = none). Used where the
// game's access level (public / protected / private changes the mangled name) is not known for sure.
inline uintptr_t ResolveFirst(std::initializer_list<const char*> symbols)
{
    for (const char* symbol : symbols)
        if (void* address = ll::memory::SymbolView{symbol}.resolve(true)) return reinterpret_cast<uintptr_t>(address);
    return 0;
}

namespace Symbols {
inline constexpr const char* EyePos = "?getEyePos@Actor@@QEBA?AVVec3@@XZ";
inline constexpr const char* InterpolatedPosition = "?getInterpolatedPosition@Actor@@QEBA?AVVec3@@M@Z";
inline constexpr const char* DimensionBlockSource = "?getDimensionBlockSource@Actor@@QEBAAEAVBlockSource@@XZ";
inline constexpr const char* EntityTypeId = "?getEntityTypeId@Actor@@QEBA?AW4ActorType@@XZ";
inline constexpr const char* IsSpectator = "?isSpectator@Actor@@QEBA_NXZ";
// Debug Menu: the biome and the light (sky / block) at a block.
inline constexpr const char* Health = "?getHealth@ActorAttribute@@YAHAEBVEntityContext@@@Z";
inline constexpr const char* Biome = "?getBiome@BlockSource@@QEBAAEBVBiome@@AEBVBlockPos@@@Z";
inline constexpr const char* BrightnessPair = "?getBrightnessPair@BlockSource@@QEBA?AUBrightnessPair@@AEBVBlockPos@@@Z";
// Block Details: a block's name in the game's language.
inline constexpr const char* BlockDescriptionName = "?buildDescriptionName@Block@@QEBA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ";
inline constexpr const char* DisplayObjective =
    "?getDisplayObjective@Scoreboard@@QEBAPEBVDisplayObjective@@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z";
inline constexpr const char* ReceivePacket =
    "?_receivePacket@BatchedNetworkPeer@@UEAA?AW4DataStatus@NetworkPeer@@AEAV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"
    "AEBV?$shared_ptr@V?$time_point@Usteady_clock@chrono@std@@V?$duration@_JU?$ratio@$00$0DLJKMKAA@@std@@@23@@chrono@std@@@5@@Z";
inline constexpr const char* SendPacket =
    "?sendPacket@BatchedNetworkPeer@@UEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"
    "W4Reliability@NetworkPeer@@W4Compressibility@@@Z";

// ContainerScreenController (inventory, chests, ...): slot hover callbacks, the screen tick and the
// slot item lookup. Variants differ only in the access letter (U/M/E virtual, Q/I/A non-virtual).
#define MYCLIENT_STRING_ARG "AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@H@Z"
#define MYCLIENT_HOVER_SYMBOL(NAME, ACCESS) "?" NAME "@ContainerScreenController@@" ACCESS "EAA?AW4ViewRequest@ui@@" MYCLIENT_STRING_ARG
#define MYCLIENT_GETITEM_SYMBOL(ACCESS) "?_getItemStack@ContainerScreenController@@" ACCESS "EBAAEBVItemStack@@" MYCLIENT_STRING_ARG
inline uintptr_t SlotHovered()
{
    return ResolveFirst({MYCLIENT_HOVER_SYMBOL("_onContainerSlotHovered", "M"), MYCLIENT_HOVER_SYMBOL("_onContainerSlotHovered", "U"),
                         MYCLIENT_HOVER_SYMBOL("_onContainerSlotHovered", "E")});
}
inline uintptr_t SlotUnhovered()
{
    return ResolveFirst({MYCLIENT_HOVER_SYMBOL("_onContainerSlotUnhovered", "M"), MYCLIENT_HOVER_SYMBOL("_onContainerSlotUnhovered", "U"),
                         MYCLIENT_HOVER_SYMBOL("_onContainerSlotUnhovered", "E")});
}
inline uintptr_t ScreenTick()
{
    return ResolveFirst({"?tick@ContainerScreenController@@UEAA?AW4DirtyFlag@ui@@XZ", "?tick@ContainerScreenController@@MEAA?AW4DirtyFlag@ui@@XZ"});
}
inline uintptr_t SlotItem()
{
    return ResolveFirst({MYCLIENT_GETITEM_SYMBOL("I"), MYCLIENT_GETITEM_SYMBOL("Q"), MYCLIENT_GETITEM_SYMBOL("A")});
}
#undef MYCLIENT_HOVER_SYMBOL
#undef MYCLIENT_GETITEM_SYMBOL
#undef MYCLIENT_STRING_ARG
} // namespace Symbols
} // namespace MyClientGame

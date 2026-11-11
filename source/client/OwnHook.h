#pragma once
// MyClient's own function hooks (2026-11-05, step 1 of leaving LeviLamina on 26.51).
//
// MYCLIENT_TYPE_INSTANCE_HOOK / MYCLIENT_INSTANCE_HOOK / MYCLIENT_STATIC_HOOK / MYCLIENT_AUTO_TYPE_INSTANCE_HOOK are
// written like LeviLamina's LL_* hook macros (same arguments, `origin(...)`, `Name::hook()` returning 0 when
// hooked, `Name::_AutoHookCount`), so every hook in client/ switched by renaming. With MYCLIENT_OWN_HOOKS (the
// default) they patch the game with MinHook (client/vendor/minhook, OwnHook.cpp); with MYCLIENT_OWN_HOOKS=0 they
// are LeviLamina's macros again, to compare the two. The priority argument is kept for that and otherwise unused.
// Hooks are never removed: the DLL is not unloaded while the game runs (see EnableFeatures).
//
// The target is a symbol (ll::memory::SymbolView, still looked up through LeviLamina until step 2) or an address
// (uintptr_t / void*). A target that cannot be found makes hook() return -1, as before.
#ifndef MYCLIENT_OWN_HOOKS
#define MYCLIENT_OWN_HOOKS 1
#endif

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#if !MYCLIENT_OWN_HOOKS
#include "ll/api/memory/Hook.h"
#define MYCLIENT_TYPE_INSTANCE_HOOK LL_TYPE_INSTANCE_HOOK
#define MYCLIENT_INSTANCE_HOOK LL_INSTANCE_HOOK
#define MYCLIENT_STATIC_HOOK LL_STATIC_HOOK
#define MYCLIENT_AUTO_TYPE_INSTANCE_HOOK LL_AUTO_TYPE_INSTANCE_HOOK
#else
#ifndef MYCLIENT_HOOK_NO_LEVILAMINA   // tests/own_hook.cpp builds without it
#include "ll/api/memory/Hook.h"   // ll::memory::HookPriority / SymbolView in the hook arguments (step 2 drops it)
#endif

namespace MyClientGame {
uintptr_t RvaFallback(const char* symbol);   // GameRva.cpp: the game's own code for a name, 0 = not in the table
}

namespace MyClientHook {
// Puts a jump to detour at target. *original gets the way back to the game's code (a trampoline) before the
// jump is written, so a call that arrives at once already has it. 0 = hooked; otherwise a MinHook status (> 0)
// or -1 (no target / no detour). Thread-safe; the game's other threads are paused while the bytes change.
int Install(void* target, void* detour, void** original);
// The address is inside Minecraft.Windows.exe's own image. A symbol LeviLamina looks up through PreLoader is a
// jump stub in PreLoader's read-only mapped memory, not the game's code (2026-11-05, r33 log: nearly every hook
// MH_ERROR_MEMORY_PROTECT): MinHook cannot patch it, LeviLamina's hook can (it follows the stub). Such targets
// still go through LeviLamina until MyClient knows the real address itself (GameRva).
bool InGameImage(const void* address);
// How many hooks were put in, and the text of the last failure, for the log.
unsigned Installed();
const char* LastError();

template <class Fn>
void* ToPointer(Fn fn)
{
    // A member function pointer's first word is the code address (the rest, when there is a rest, is a
    // this-adjustment, zero for the hook structs which derive from one class only).
    void* address = nullptr;
    static_assert(sizeof(Fn) >= sizeof(void*));
    std::memcpy(&address, &fn, sizeof(address));
    return address;
}
inline void* Resolve(uintptr_t address) { return reinterpret_cast<void*>(address); }
inline void* Resolve(void* address) { return address; }
#ifndef MYCLIENT_HOOK_NO_LEVILAMINA
// By name: the real address from the table first (GameRva), else LeviLamina's lookup (a PreLoader stub).
inline void* Resolve(ll::memory::SymbolView symbol)
{
    const std::string name(symbol.raw());
    if (const uintptr_t address = ::MyClientGame::RvaFallback(name.c_str())) return reinterpret_cast<void*>(address);
    return symbol.resolve(true);
}
#endif

struct Empty {};

template <class Hook>
struct AutoInstall
{
    AutoInstall() noexcept { if (++Hook::_AutoHookCount == 1) Hook::hook(); }
};
} // namespace MyClientHook

#ifndef MYCLIENT_HOOK_NO_LEVILAMINA
#define MYCLIENT_HOOK_THROUGH_LEVILAMINA(DEF_TYPE)                                                                     \
    if (!::MyClientHook::InGameImage(_HookTarget))                                                                     \
        return ::ll::memory::hook(_HookTarget, ::MyClientHook::ToPointer(&DEF_TYPE::detour),                           \
                                  reinterpret_cast<void**>(&_OriginalFunc), ::ll::memory::HookPriority::Normal);
#else
#define MYCLIENT_HOOK_THROUGH_LEVILAMINA(DEF_TYPE)
#endif

// MSVC-style preprocessors pass __VA_ARGS__ on as one argument unless it is expanded again (as LL_VA_EXPAND does)
#define MYCLIENT_VA_EXPAND(...) __VA_ARGS__

#define MYCLIENT_HOOK_IMPL(REGISTER, FUNC_PTR, STATIC, CALL, DEF_TYPE, TYPE, IDENTIFIER, RET_TYPE, ...)               \
    struct DEF_TYPE : public TYPE {                                                                                    \
        [[maybe_unused]] inline static ::std::atomic_uint _AutoHookCount{};                                                            \
                                                                                                                       \
    private:                                                                                                           \
        using _OriginFuncType = RET_TYPE FUNC_PTR(__VA_ARGS__);                                                        \
        inline static void*           _HookTarget{};                                                                   \
        inline static _OriginFuncType _OriginalFunc{};                                                                 \
                                                                                                                       \
    public:                                                                                                            \
        template <class... Args>                                                                                       \
        STATIC RET_TYPE origin(Args&&... params) {                                                                     \
            return CALL(::std::forward<Args>(params)...);                                                              \
        }                                                                                                              \
        STATIC RET_TYPE detour(__VA_ARGS__);                                                                           \
        static int hook(bool = true) {                                                                                 \
            if (!_HookTarget) _HookTarget = ::MyClientHook::Resolve(IDENTIFIER);                                       \
            if (!_HookTarget) return -1;                                                                               \
            MYCLIENT_HOOK_THROUGH_LEVILAMINA(DEF_TYPE)                                                                 \
            return ::MyClientHook::Install(_HookTarget, ::MyClientHook::ToPointer(&DEF_TYPE::detour),                  \
                                           reinterpret_cast<void**>(&_OriginalFunc));                                  \
        }                                                                                                              \
    };                                                                                                                 \
    REGISTER;                                                                                                          \
    RET_TYPE DEF_TYPE::detour(__VA_ARGS__)

#define MYCLIENT_TYPE_INSTANCE_HOOK(DEF_TYPE, PRIORITY, TYPE, IDENTIFIER, RET_TYPE, ...)                               \
    MYCLIENT_VA_EXPAND(MYCLIENT_HOOK_IMPL(, (TYPE::*), , (this->*_OriginalFunc), DEF_TYPE, TYPE, IDENTIFIER, RET_TYPE, __VA_ARGS__))
#define MYCLIENT_INSTANCE_HOOK(DEF_TYPE, PRIORITY, IDENTIFIER, RET_TYPE, ...)                                          \
    MYCLIENT_VA_EXPAND(MYCLIENT_HOOK_IMPL(, (::MyClientHook::Empty::*), , (this->*_OriginalFunc), DEF_TYPE, ::MyClientHook::Empty,      \
                       IDENTIFIER, RET_TYPE, __VA_ARGS__))
#define MYCLIENT_STATIC_HOOK(DEF_TYPE, PRIORITY, IDENTIFIER, RET_TYPE, ...)                                            \
    MYCLIENT_VA_EXPAND(MYCLIENT_HOOK_IMPL(, (*), static, _OriginalFunc, DEF_TYPE, ::MyClientHook::Empty, IDENTIFIER, RET_TYPE,         \
                       __VA_ARGS__))
#define MYCLIENT_AUTO_TYPE_INSTANCE_HOOK(DEF_TYPE, PRIORITY, TYPE, IDENTIFIER, RET_TYPE, ...)                          \
    MYCLIENT_VA_EXPAND(MYCLIENT_HOOK_IMPL(inline ::MyClientHook::AutoInstall<DEF_TYPE> DEF_TYPE##AutoRegister, (TYPE::*), ,             \
                       (this->*_OriginalFunc), DEF_TYPE, TYPE, IDENTIFIER, RET_TYPE, __VA_ARGS__))
#endif

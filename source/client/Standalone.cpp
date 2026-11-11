// MyClient without LeviLamina (2026-11-10, the last step of leaving it): built with MYCLIENT_STANDALONE=1
// (tools/build_client.py --standalone), the DLL does not import LeviLamina.dll at all.
//
// What LeviLamina did, and what stands in for it here:
//   * loading the DLL: the launcher starts the game suspended and loads this DLL into it (launcher/StandaloneWin.h);
//   * ll_mod_load / ll_mod_enable: DllMain starts one thread; it runs MyClientStart::Load at once (the game's own
//     start-up has not run yet, so the swap chain's creation is hooked in time) and MyClientStart::Enable when
//     the game's window is up (LeviLamina enabled its mods about 8 s after load, after the window);
//   * game functions by name (ll::memory::SymbolView::resolve): only MyClient's own table (GameRva.cpp). A name
//     that is not in the table for this game build is simply not found, as a missing symbol always was;
//   * ll::memory::hook: MinHook (OwnHook.cpp), which is all MyClient uses once every address is the game's own;
//   * the allocator behind operator new / delete: LeviLamina's client allocator is malloc / free /
//     _aligned_malloc (its StdMemoryAllocator; it does not replace the game's allocator on the client), and this
//     DLL is built /MD like LeviLamina.dll, so the same heap (ucrtbase) is used as before;
//   * versions: the start-up self check (GameSigs.cpp, from tools/sigs.json) decides; it refuses another build.
#if MYCLIENT_STANDALONE
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>

#include "mc/deps/core/memory/IMemoryAllocator.h"

#include "ll/api/Versions.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/memory/Symbol.h"

#include "GameRva.h"
#include "Log.h"
#include "OwnHook.h"

#ifndef MYCLIENT_GAME_VERSION
#error "MYCLIENT_GAME_VERSION is set by tools/build_client.py"
#endif

namespace MyClientStart {
bool Load();
bool Enable();
}

namespace {
// MYCLIENT_GAME_VERSION ("1.26.51") as numbers, for ll::getGameVersion below.
unsigned Part(int index)
{
    unsigned parts[3] = {0, 0, 0};
    std::sscanf(MYCLIENT_GAME_VERSION, "%u.%u.%u", &parts[0], &parts[1], &parts[2]);
    return parts[index];
}

// A top-level visible window of this process: the game is past its start-up.
bool GameWindowUp()
{
    struct Find { DWORD pid; bool found; } find{GetCurrentProcessId(), false};
    EnumWindows([](HWND window, LPARAM param) -> BOOL {
        auto& state = *reinterpret_cast<Find*>(param);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == state.pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) { state.found = true; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&find));
    return find.found;
}

std::atomic<bool> started{false};

DWORD WINAPI Run(LPVOID)
{
    auto& log = MyClientLog::Get();
    try
    {
        log.info("OriginX (standalone, without LeviLamina) for Minecraft {}: loading.", MYCLIENT_GAME_VERSION);
        // The version and signature check is the first thing Load does (logs/startup.log, logs/sigcheck.log).
        const bool loadedOk = MyClientStart::Load();
        // Tell the launcher the load step is over, done or refused: it resumes the game's start-up only then
        // (launcher/StandaloneWin.h). A refused load hooked nothing, so the game simply runs without MyClient.
        wchar_t name[64];
        std::swprintf(name, 64, L"Local\\MyClientStandaloneLoaded-%lu", static_cast<unsigned long>(GetCurrentProcessId()));
        if (HANDLE event = CreateEventW(nullptr, TRUE, FALSE, name)) SetEvent(event);   // kept open for the process' life
        if (!loadedOk) { log.error("OriginX (standalone): not started (see logs/startup.log)."); return 0; }
        // Enable when the window is up (and has been for a moment), or after two minutes whatever happened.
        const ULONGLONG begin = GetTickCount64();
        ULONGLONG seen = 0;
        for (;;)
        {
            Sleep(250);
            const ULONGLONG now = GetTickCount64();
            if (!seen && GameWindowUp()) seen = now;
            if ((seen && now - seen >= 3000) || now - begin >= 120000) break;
        }
        log.info("OriginX (standalone): enabling ({} ms after load, window {}).", GetTickCount64() - begin, seen ? "up" : "not seen");
        if (!MyClientStart::Enable()) log.error("OriginX (standalone): the enable step failed.");
    }
    catch (...) { log.error("OriginX (standalone): an exception stopped the start."); }
    return 0;
}

class StdMemoryAllocator : public ::Bedrock::Memory::IMemoryAllocator
{
public:
    void* _allocate(uint64 size) override { return std::malloc(size ? size : 1); }
    void _release(::gsl::not_null<void*> ptr) override { std::free(ptr); }
    void* _alignedAllocate(uint64 size, uint64 alignment) override { return _aligned_malloc(size ? size : 1, alignment); }
    void _alignedRelease(::gsl::not_null<void*> ptr) override { _aligned_free(ptr); }
    uint64 _getUsableSize(::gsl::not_null<void*> ptr, bool) override { return _msize(ptr); }
    void* _realloc(::gsl::not_null<void*> ptr, uint64 newSize) override { return std::realloc(ptr, newSize); }
    void* _alignedRealloc(::gsl::not_null<void*> ptr, uint64 newSize, uint64 alignment) override { return _aligned_realloc(ptr, newSize, alignment); }
};
} // namespace

namespace MyClientStandalone {
// From DllMain (MyClientBridge.cpp): only starts the thread, nothing runs under the loader lock.
void Attach()
{
    if (started.exchange(true)) return;
    if (HANDLE thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr)) CloseHandle(thread);
}
} // namespace MyClientStandalone

// ---- LeviLamina's functions that the SDK headers and MyClient's code still name ----
namespace ll {
data::Version getGameVersion() { return data::Version{static_cast<std::uint16_t>(Part(0)), static_cast<std::uint16_t>(Part(1)), static_cast<std::uint16_t>(Part(2))}; }
data::Version getLoaderVersion() { return data::Version{0, 0, 0}; }
bool isClient() { return true; }

namespace memory {
void* SymbolView::resolve(bool) const
{
    const std::string name(sym);   // the view is not always zero-terminated
    return reinterpret_cast<void*>(::MyClientGame::RvaFallback(name.c_str()));
}

int hook(FuncPtr target, FuncPtr detour, FuncPtr* originalFunc, HookPriority, bool)
{
    return ::MyClientHook::Install(target, detour, originalFunc);
}

bool unhook(FuncPtr, FuncPtr, bool) { return false; }   // MyClient's hooks are never taken out

::Bedrock::Memory::IMemoryAllocator& getDefaultAllocator()
{
    static StdMemoryAllocator allocator;
    return allocator;
}

[[noreturn]] void throwMemoryException(size_t size)
{
    if (size == std::numeric_limits<size_t>::max()) throw std::bad_array_new_length();
    throw std::bad_alloc();
}
} // namespace memory
} // namespace ll
#endif

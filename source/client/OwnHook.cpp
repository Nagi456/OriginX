// MyClient's own function hooks: see OwnHook.h. The patching itself is MinHook's (client/vendor/minhook).
#include "OwnHook.h"

#if MYCLIENT_OWN_HOOKS
#include "vendor/minhook/include/MinHook.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <utility>

#include <windows.h>

namespace {
std::mutex mutex;
bool initialized = false;
std::atomic<unsigned> installed{0};
char lastError[160] = "";
} // namespace

namespace MyClientHook {
int Install(void* target, void* detour, void** original)
{
    if (!target || !detour || !original) return -1;
    std::lock_guard lock(mutex);
    if (!initialized)
    {
        const MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        {
            std::snprintf(lastError, sizeof(lastError), "MH_Initialize: %s", MH_StatusToString(status));
            return static_cast<int>(status) > 0 ? static_cast<int>(status) : 1;
        }
        initialized = true;
    }
    // MH_CreateHook writes the trampoline to *original before anything in the game changes; MH_EnableHook then
    // writes the jump with the other threads paused (and moved off the bytes being replaced).
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK)
    {
        status = MH_EnableHook(target);
        if (status != MH_OK) MH_RemoveHook(target);   // nothing is left half made
    }
    if (status != MH_OK)
    {
        // MH_ERROR_ALREADY_CREATED: MyClient hooks that function already (one hook per function)
        std::snprintf(lastError, sizeof(lastError), "%p: %s", target, MH_StatusToString(status));
        return static_cast<int>(status) > 0 ? static_cast<int>(status) : 1;
    }
    installed.fetch_add(1);
    return 0;
}

bool InGameImage(const void* address)
{
    static const auto range = [] {
        const auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        size_t size = 0;
        if (base)
        {
            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            size = nt->OptionalHeader.SizeOfImage;
        }
        return std::pair<const uint8_t*, size_t>{base, size};
    }();
    const auto at = static_cast<const uint8_t*>(address);
    return range.first && at >= range.first && at < range.first + range.second;
}

unsigned Installed() { return installed.load(); }
const char* LastError() { return lastError; }
} // namespace MyClientHook
#endif

// MyClient's own function hooks: see OwnHook.h. The patching itself is MinHook's (client/vendor/minhook).
#include "OwnHook.h"

#if MYCLIENT_OWN_HOOKS
#include "vendor/minhook/include/MinHook.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

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

namespace {
struct Tracked { const char* name; std::atomic<std::uint64_t>* calls; std::uint64_t last; };
std::mutex trackMutex;
std::vector<Tracked> tracked;
std::vector<std::string> disabledNames, disabledSeen;

const std::vector<std::string>& DisabledList()
{
    static std::once_flag once;
    std::call_once(once, [] {
        wchar_t local[MAX_PATH]{};
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        if (!n || n >= MAX_PATH) return;
        std::ifstream file(std::filesystem::path(std::wstring(local) + L"\\MyClient\\disabled-hooks.txt"));
        std::string line;
        while (std::getline(file, line))
        {
            const size_t hash = line.find('#');
            if (hash != std::string::npos) line.resize(hash);
            const size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            const size_t last = line.find_last_not_of(" \t\r\n");
            disabledNames.push_back(line.substr(first, last - first + 1));
        }
    });
    return disabledNames;
}
} // namespace

bool Disabled(const char* name)
{
    if (!name) return false;
    const auto& list = DisabledList();
    if (std::find(list.begin(), list.end(), name) == list.end()) return false;
    std::lock_guard lock(trackMutex);
    if (std::find(disabledSeen.begin(), disabledSeen.end(), name) == disabledSeen.end()) disabledSeen.emplace_back(name);
    return true;
}

void Track(const char* name, std::atomic<std::uint64_t>* calls)
{
    if (!name || !calls) return;
    std::lock_guard lock(trackMutex);
    for (const auto& t : tracked) if (t.calls == calls) return;
    tracked.push_back({name, calls, calls->load(std::memory_order_relaxed)});
}

std::string CallReport(double seconds, std::size_t top)
{
    std::vector<std::pair<std::uint64_t, const char*>> rows;
    {
        std::lock_guard lock(trackMutex);
        for (auto& t : tracked)
        {
            const std::uint64_t now = t.calls->load(std::memory_order_relaxed);
            rows.emplace_back(now - t.last, t.name);
            t.last = now;
        }
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string text;
    char item[160];
    for (std::size_t i = 0; i < rows.size() && i < top; ++i)
    {
        std::snprintf(item, sizeof item, "%s%s=%.0f/s", text.empty() ? "" : ", ", rows[i].second,
                      seconds > 0 ? static_cast<double>(rows[i].first) / seconds : 0.0);
        text += item;
    }
    return text.empty() ? "(no hook calls counted)" : text;
}

std::string DisabledReport()
{
    const auto& list = DisabledList();
    std::lock_guard lock(trackMutex);
    std::string text;
    for (const auto& name : list)
    {
        const bool seen = std::find(disabledSeen.begin(), disabledSeen.end(), name) != disabledSeen.end();
        text += (text.empty() ? "" : ", ") + name + (seen ? "" : " (no hook of this name was asked for)");
    }
    return text;
}
} // namespace MyClientHook
#endif

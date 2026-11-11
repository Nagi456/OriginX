#pragma once
// Item Counter: hold Alt over an item in an inventory / container screen and the HUD shows how many of that item
// you have: in your inventory, inside the shulker boxes (in your inventory and in the open container), and in the
// open container (chest, barrel, ...), with the total and how many stacks that is.
// Own mapping (MyClient.ItemCount.v1.<pid>, the DLL creates it), so an older DLL or HUD only loses this feature.
//   HUD -> DLL: wantedAt (the feature is on; refreshed every HUD tick, stale after WantedMs).
//   DLL -> HUD: the counts for the hovered slot while Alt is held, refreshed every screen tick; at == 0 = nothing.
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace MyClientItemCount {
constexpr uint32_t Magic = 0x4d434943, Version = 1;   // "MCIC"
constexpr uint64_t StaleMs = 300;    // the DLL refreshes `at` every screen tick while Alt is held over the slot
constexpr uint64_t WantedMs = 1000;

struct Shared {
    uint32_t magic, version, size, pid;
    uint64_t wantedAt;              // HUD: Item Counter is on
    uint64_t at;                    // DLL: when the counts below were taken (0 = nothing to show)
    char type[48];                  // "minecraft:tnt"
    char name[64];                  // the game's name for it, UTF-8 ("TNT")
    uint32_t inventory, shulkers, container, maxStack;
    uint32_t hasContainer, pad;     // hasContainer: a container (chest, ...) is open next to the inventory
};
static_assert(sizeof(Shared) == 168);
static_assert(std::is_trivially_copyable<Shared>::value);

inline bool Valid(const Shared& data, uint32_t pid) {
    return data.magic == Magic && data.version == Version && data.size == sizeof(Shared) && data.pid == pid;
}
inline bool Fresh(uint64_t at, uint64_t now, uint64_t limit) { return at != 0 && at <= now && now - at <= limit; }

inline void CopyText(char* out, size_t size, std::string_view s) {
    if (!size) return;
    size_t n = s.size() < size - 1 ? s.size() : size - 1;
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;   // whole UTF-8 characters
    std::memcpy(out, s.data(), n);
    out[n] = 0;
}

// Stacks needed for `total` items (an unstackable item counts one stack each).
inline uint32_t Stacks(uint64_t total, uint32_t maxStack) {
    if (!maxStack) maxStack = 1;
    return static_cast<uint32_t>((total + maxStack - 1) / maxStack);
}

// A ContainerManagerModel model that belongs to the open container (chest "container_items", hopper, ...), not
// to the player (inventory / hotbar / armor / offhand / cursor) or to the UI (crafting, creative, results).
inline bool IsOpenContainerModel(std::string_view name) {
    static constexpr std::string_view kPlayerOrUi[] = {"inventory", "hotbar", "armor", "offhand", "cursor", "crafting",
                                                       "creative", "recipe", "result", "output"};
    for (std::string_view word : kPlayerOrUi)
        if (name.find(word) != std::string_view::npos) return false;
    return name.find("container") != std::string_view::npos ||
           (name.size() > 6 && name.substr(name.size() - 6) == "_items");
}

// The counts for one item, as they are added up in the DLL.
struct Counts {
    uint64_t inventory = 0, shulkers = 0, container = 0;
    uint64_t Total() const { return inventory + shulkers + container; }
};

#ifdef _WIN32
class Channel {
    HANDLE mapping_ = nullptr, mutex_ = nullptr;
    Shared* data_ = nullptr;
public:
    Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    ~Channel() { Close(); }
    bool IsOpen() const { return data_ != nullptr; }
    void Close() {
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        data_ = nullptr; mapping_ = mutex_ = nullptr;
    }
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = L"Local\\MyClient.ItemCount.v1." + std::to_wstring(pid);
        mutex_ = producer ? CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str())
            : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Lock").c_str());
        if (!mutex_) return false;
        mapping_ = producer ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name.c_str())
            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping_) data_ = static_cast<Shared*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!data_) { Close(); return false; }
        if (producer) {
            const DWORD result = WaitForSingleObject(mutex_, 20);
            if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) { Close(); return false; }
            Reset(pid); ReleaseMutex(mutex_);
        }
        return true;
    }
    void Reset(DWORD pid) {
        std::memset(data_, 0, sizeof(Shared));
        data_->magic = Magic; data_->version = Version; data_->size = sizeof(Shared); data_->pid = pid;
    }
    bool Lock(DWORD timeout = 0) {
        if (!data_ || !mutex_) return false;
        const DWORD result = WaitForSingleObject(mutex_, timeout);
        if (result == WAIT_ABANDONED) { Reset(data_->pid); ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    // HUD: the feature is on.
    void SetWanted(DWORD pid, uint64_t now) {
        if (!Lock()) return;
        if (Valid(*data_, pid)) data_->wantedAt = now;
        Unlock();
    }
    // DLL: is the feature on?
    bool Wanted(DWORD pid, uint64_t now) {
        if (!Lock()) return false;
        const bool wanted = Valid(*data_, pid) && Fresh(data_->wantedAt, now, WantedMs);
        Unlock();
        return wanted;
    }
    // DLL: the counts (at == 0 hides the popup). wantedAt is the HUD's and is kept.
    void Publish(DWORD pid, std::string_view type, std::string_view name, const Counts& c, uint32_t maxStack,
                 bool hasContainer, uint64_t at) {
        if (!Lock()) return;
        if (!Valid(*data_, pid)) Reset(pid);
        CopyText(data_->type, sizeof(data_->type), type);
        CopyText(data_->name, sizeof(data_->name), name);
        const auto clamp = [](uint64_t v) { return static_cast<uint32_t>(v > 0xFFFFFFFFull ? 0xFFFFFFFFull : v); };
        data_->inventory = clamp(c.inventory);
        data_->shulkers = clamp(c.shulkers);
        data_->container = clamp(c.container);
        data_->maxStack = maxStack;
        data_->hasContainer = hasContainer ? 1u : 0u;
        data_->at = at;
        Unlock();
    }
    void Hide(DWORD pid) {
        if (!Lock()) return;
        if (Valid(*data_, pid)) data_->at = 0;
        Unlock();
    }
    // HUD: a copy, when it is fresh.
    bool Read(DWORD pid, uint64_t now, Shared& out) {
        if (!Lock()) return false;
        const bool ok = Valid(*data_, pid) && Fresh(data_->at, now, StaleMs);
        if (ok) out = *data_;
        Unlock();
        return ok;
    }
};
#endif
}

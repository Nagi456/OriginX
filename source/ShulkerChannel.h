#pragma once
// Contents of the shulker boxes in the player's inventory, shared from the game DLL to
// the HUD. Own mapping (like the resource counter), so an older DLL or HUD simply shows
// "no data" without affecting anything else.
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace MyClientShulker {
constexpr uint32_t Magic = 0x4d435331, Version = 1;
constexpr uint64_t StaleMs = 2000;
constexpr uint32_t MaxBoxes = 6, MaxTypes = 12;

struct Entry { char name[28]; uint32_t count; };
struct Box { char name[32]; uint32_t used, typeCount; Entry items[MaxTypes]; };
struct Shared { uint32_t magic, version, size, pid; uint64_t at; uint32_t boxCount, pad; Box boxes[MaxBoxes]; };
static_assert(sizeof(Entry) == 32 && sizeof(Box) == 424 && sizeof(Shared) == 32 + 6 * 424);
static_assert(std::is_trivially_copyable<Shared>::value);

inline bool Valid(const Shared& data, uint32_t pid) {
    return data.magic == Magic && data.version == Version && data.size == sizeof(Shared) &&
           data.pid == pid && data.boxCount <= MaxBoxes;
}
inline bool Fresh(uint64_t at, uint64_t now) { return at != 0 && at <= now && now - at <= StaleMs; }

// The shulker box under the mouse in an inventory / container screen (hover popup). Separate mapping,
// so the list above keeps its v1 layout and mixed old/new DLL+HUD pairs only lose the popup.
// The DLL refreshes `at` every screen tick while the slot stays hovered; a closed screen stops the
// ticks, so the popup disappears on its own after HoverStaleMs.
// v2: one entry per slot (the popup draws the box's 9x3 grid like the game's own screen).
constexpr uint32_t HoverMagic = 0x4d435348, HoverVersion = 2;
constexpr uint64_t HoverStaleMs = 300;
constexpr uint32_t Slots = 27;
struct Slot { char name[39]; uint8_t count; };   // count 0 = empty slot
struct HoverBox { char name[32]; uint32_t used, pad; Slot slots[Slots]; };
struct Hover { uint32_t magic, version, size, pid; uint64_t at; uint32_t active, pad; HoverBox box; };
static_assert(sizeof(Slot) == 40 && sizeof(HoverBox) == 40 + 27 * 40 && sizeof(Hover) == 32 + 1120);
static_assert(std::is_trivially_copyable<Hover>::value);

inline bool Valid(const Hover& data, uint32_t pid) {
    return data.magic == HoverMagic && data.version == HoverVersion && data.size == sizeof(Hover) && data.pid == pid;
}
inline bool HoverFresh(uint64_t at, uint64_t now) { return at != 0 && at <= now && now - at <= HoverStaleMs; }

inline std::string_view StripNamespace(std::string_view s) {
    const auto p = s.find(':');
    return p == std::string_view::npos ? s : s.substr(p + 1);
}
inline void CopyName(char* out, size_t size, std::string_view s) {
    s = StripNamespace(s);
    const size_t n = s.size() < size - 1 ? s.size() : size - 1;
    std::memcpy(out, s.data(), n);
    out[n] = 0;
}
inline bool IsShulkerBox(std::string_view type) {
    return StripNamespace(type).find("shulker_box") != std::string_view::npos;
}

// Hover popup: puts one stored stack into its slot (NBT "Slot" 0..26). False for a bad entry, which
// makes the whole snapshot untrustworthy; a second stack for a taken slot is ignored.
inline bool PutSlot(HoverBox& box, int slot, std::string_view type, uint32_t count) {
    if (!count) return true;
    if (slot < 0 || slot >= static_cast<int>(Slots) || type.empty() || count > 255) return false;
    Slot& s = box.slots[slot];
    if (s.count) return true;
    CopyName(s.name, sizeof(s.name), type);
    s.count = static_cast<uint8_t>(count);
    box.used++;
    return true;
}

// Builds the snapshot: Begin() a box, Add() each stored stack, then read Result().
class Collector {
    Shared data_{};
    bool valid_ = true;
    Box* cur_ = nullptr;
public:
    // False when the maximum number of boxes is already collected.
    bool Begin(std::string_view boxType) {
        if (data_.boxCount >= MaxBoxes) { cur_ = nullptr; return false; }
        cur_ = &data_.boxes[data_.boxCount++];
        CopyName(cur_->name, sizeof(cur_->name), boxType);
        return true;
    }
    void Add(std::string_view type, uint32_t count) {
        if (!cur_ || !count) return;
        if (type.empty() || count > 255) { valid_ = false; return; }
        cur_->used++;
        const std::string_view name = StripNamespace(type);
        for (uint32_t i = 0; i < cur_->typeCount; i++)
            if (name == cur_->items[i].name) { cur_->items[i].count += count; return; }
        if (cur_->typeCount < MaxTypes) {
            CopyName(cur_->items[cur_->typeCount].name, sizeof(cur_->items[0].name), name);
            cur_->items[cur_->typeCount].count = count;
            cur_->typeCount++;
        }
        // More distinct item types than fit: the rest are still counted in `used`.
    }
    bool Valid() const { return valid_; }
    const Shared& Result() const { return data_; }
};

#ifdef _WIN32
template <class T> struct ChannelTraits;
template <> struct ChannelTraits<Shared> {
    static constexpr const wchar_t* Name = L"Local\\MyClient.Shulker.v1.";
    static constexpr uint32_t Magic = MyClientShulker::Magic, Version = MyClientShulker::Version;
};
template <> struct ChannelTraits<Hover> {
    static constexpr const wchar_t* Name = L"Local\\MyClient.ShulkerHover.v2.";
    static constexpr uint32_t Magic = HoverMagic, Version = HoverVersion;
};

template <class T>
class BasicChannel {
    HANDLE mapping_ = nullptr, mutex_ = nullptr;
    T* data_ = nullptr;
public:
    BasicChannel() = default;
    BasicChannel(const BasicChannel&) = delete;
    BasicChannel& operator=(const BasicChannel&) = delete;
    ~BasicChannel() { Close(); }
    T* Data() { return data_; }
    void Close() {
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        data_ = nullptr; mapping_ = mutex_ = nullptr;
    }
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = std::wstring(ChannelTraits<T>::Name) + std::to_wstring(pid);
        mutex_ = producer ? CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str())
            : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Lock").c_str());
        if (!mutex_) return false;
        mapping_ = producer ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(T), name.c_str())
            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping_) data_ = static_cast<T*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(T)));
        if (!data_) { Close(); return false; }
        if (producer) {
            const DWORD result = WaitForSingleObject(mutex_, 20);
            if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) { Close(); return false; }
            Reset(pid); ReleaseMutex(mutex_);
        }
        return true;
    }
    void Reset(DWORD pid) {
        *data_ = {}; data_->magic = ChannelTraits<T>::Magic; data_->version = ChannelTraits<T>::Version;
        data_->size = sizeof(T); data_->pid = pid;
    }
    bool Lock() {
        const DWORD result = WaitForSingleObject(mutex_, 0);
        if (result == WAIT_ABANDONED) { *data_ = {}; ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    // Producer side. `at` == 0 marks the snapshot as "no data".
    bool Publish(DWORD pid, const Shared& snapshot, uint64_t at) {   // list channel only
        if (!data_ || !Lock()) return false;
        if (!Valid(*data_, pid)) Reset(pid);
        for (uint32_t i = 0; i < MaxBoxes; i++) data_->boxes[i] = snapshot.boxes[i];
        data_->boxCount = snapshot.boxCount;
        data_->at = at;
        Unlock();
        return true;
    }
    // Hover popup: `active` == 0 or `at` == 0 hides it.
    bool Publish(DWORD pid, const HoverBox& box, bool active, uint64_t at) {   // hover channel only
        if (!data_ || !Lock()) return false;
        if (!Valid(*data_, pid)) Reset(pid);
        data_->box = active ? box : HoverBox{};
        data_->active = active ? 1u : 0u;
        data_->at = at;
        Unlock();
        return true;
    }
};
using Channel = BasicChannel<Shared>;
using HoverChannel = BasicChannel<Hover>;
#endif
}

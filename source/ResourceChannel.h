#pragma once
// Resource totals (iron / gold / diamond / emerald) shared from the game DLL to the
// HUD. This is its own mapping, independent from the arrow and feature channels, so
// an older DLL or HUD simply shows "---" instead of breaking anything else.
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace MyClientResources {
constexpr uint32_t Magic = 0x4d435231, Version = 1;
constexpr uint64_t StaleMs = 2000;
struct Counts { uint32_t iron, gold, diamond, emerald; };
struct Shared { uint32_t magic, version, size, pid; uint64_t at; Counts counts; };
static_assert(sizeof(Counts) == 16 && sizeof(Shared) == 40);
static_assert(std::is_trivially_copyable<Shared>::value);

inline bool Valid(const Shared& data, uint32_t pid) {
    return data.magic == Magic && data.version == Version && data.size == sizeof(Shared) && data.pid == pid;
}
inline bool Fresh(uint64_t at, uint64_t now) { return at != 0 && at <= now && now - at <= StaleMs; }

class Counter {
    Counts counts_{};
    bool valid_ = true;
    void Plus(uint32_t& total, uint32_t count) {
        if (count > UINT32_MAX - total) valid_ = false; else total += count;
    }
public:
    void Add(std::string_view type, uint32_t count) {
        if (!count) return; // empty slots have no identifier
        if (type.empty() || count > 255) { valid_ = false; return; }
        if (type == "minecraft:iron_ingot") Plus(counts_.iron, count);
        else if (type == "minecraft:gold_ingot") Plus(counts_.gold, count);
        else if (type == "minecraft:diamond") Plus(counts_.diamond, count);
        else if (type == "minecraft:emerald") Plus(counts_.emerald, count);
    }
    bool Valid() const { return valid_; }
    const Counts& Result() const { return counts_; }
};

class Channel {
    HANDLE mapping_ = nullptr, mutex_ = nullptr;
    Shared* data_ = nullptr;
public:
    Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    ~Channel() { Close(); }
    Shared* Data() { return data_; }
    void Close() {
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        data_ = nullptr; mapping_ = mutex_ = nullptr;
    }
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = L"Local\\MyClient.Resources.v1." + std::to_wstring(pid);
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
    void Reset(DWORD pid) { *data_ = {}; data_->magic = Magic; data_->version = Version; data_->size = sizeof(Shared); data_->pid = pid; }
    bool Lock() {
        const DWORD result = WaitForSingleObject(mutex_, 0);
        if (result == WAIT_ABANDONED) { *data_ = {}; ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    // Producer side. `at` == 0 marks the snapshot as "no data" (HUD shows ---).
    bool Publish(DWORD pid, const Counts& counts, uint64_t at) {
        if (!data_ || !Lock()) return false;
        if (!Valid(*data_, pid)) Reset(pid);
        data_->counts = counts;
        data_->at = at;
        Unlock();
        return true;
    }
};
}

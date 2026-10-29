#pragma once
// Names of the players in the current world / server (the game's own player list), shared from the
// game DLL to the HUD. Own mapping, so an older DLL or HUD simply shows "no data".
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace MyClientTabList {
constexpr uint32_t Magic = 0x4d435431, Version = 3;
constexpr uint64_t StaleMs = 2000;
constexpr uint32_t MaxPlayers = 64, NameSize = 32, TitleSize = 160, MaxBoxes = 24, BoxSegments = 8;

// Hitbox of another player: the AABB plus, for each of its 12 edges, a bit mask of the 8 segments that the
// local player can actually see (a block-ray from the eye reaches the segment). Hidden segments are not drawn.
struct Box { float min[3], max[3]; uint8_t visible[12]; uint8_t kind; uint8_t pad[3]; };   // kind: 0 player, 1 item, 2 entity, 3 mob
static_assert(sizeof(Box) == 40);
// Corner i of a box: bit 0 = x, bit 1 = y, bit 2 = z (0 = min, 1 = max). Edges join corners differing in one bit.
constexpr int kEdges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
inline void Corner(const Box& b, int i, float out[3]) {
    out[0] = (i & 1) ? b.max[0] : b.min[0];
    out[1] = (i & 2) ? b.max[1] : b.min[1];
    out[2] = (i & 4) ? b.max[2] : b.min[2];
}

struct Shared { uint32_t magic, version, size, pid; uint64_t at; uint32_t count, total; char names[MaxPlayers][NameSize]; char title[TitleSize];
                uint32_t boxCount, boxPad[3]; Box boxes[MaxBoxes]; };
static_assert(sizeof(Shared) == 32 + 64 * 32 + 160 + 16 + 24 * 40);
static_assert(std::is_trivially_copyable<Shared>::value);

inline bool Valid(const Shared& data, uint32_t pid) {
    return data.magic == Magic && data.version == Version && data.size == sizeof(Shared) &&
           data.pid == pid && data.count <= MaxPlayers;
}
inline bool Fresh(uint64_t at, uint64_t now) { return at != 0 && at <= now && now - at <= StaleMs; }

// Cuts a UTF-8 name to NameSize-1 bytes without splitting a multi-byte character.
inline void CopyName(char* out, std::string_view s) {
    size_t n = s.size() < NameSize - 1 ? s.size() : NameSize - 1;
    if (n < s.size()) while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
    std::memcpy(out, s.data(), n);
    out[n] = 0;
}

// Builds the snapshot: Add() each player name, then read Result().
class Collector {
    Shared data_{};
public:
    void Add(std::string_view name) {
        if (name.empty()) return;
        data_.total++;
        if (data_.count >= MaxPlayers) return;
        CopyName(data_.names[data_.count++], name);
    }
    // Sidebar scoreboard title (+ objective name), used by the HUD to tell which Hive game this is.
    void SetTitle(std::string_view s) {
        size_t n = s.size() < TitleSize - 1 ? s.size() : TitleSize - 1;
        if (n < s.size()) while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
        std::memcpy(data_.title, s.data(), n);
        data_.title[n] = 0;
    }
    void AddBox(const Box& box) { if (data_.boxCount < MaxBoxes) data_.boxes[data_.boxCount++] = box; }
    const Shared& Result() const { return data_; }
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
    Shared* Data() { return data_; }
    void Close() {
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        data_ = nullptr; mapping_ = mutex_ = nullptr;
    }
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = L"Local\\MyClient.TabList.v3." + std::to_wstring(pid);
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
    // Producer side. `at` == 0 marks the snapshot as "no data".
    bool Publish(DWORD pid, const Shared& snapshot, uint64_t at) {
        if (!data_ || !Lock()) return false;
        if (!Valid(*data_, pid)) Reset(pid);
        std::memcpy(data_->names, snapshot.names, sizeof(data_->names));
        data_->count = snapshot.count;
        data_->total = snapshot.total;
        std::memcpy(data_->title, snapshot.title, sizeof(data_->title));
        data_->boxCount = snapshot.boxCount;
        std::memcpy(data_->boxes, snapshot.boxes, sizeof(data_->boxes));
        data_->at = at;
        Unlock();
        return true;
    }
};
#endif
}

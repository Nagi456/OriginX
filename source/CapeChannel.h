#pragma once
// OriginX capes between OriginX users (docs/superpowers/specs/2026-11-12-capes-design.md), HUD <-> DLL:
// the DLL lists your own XUID and the XUIDs of the players in your world; the HUD asks the Worker which capes they
// wear and writes them back (capes[i] for xuids[i], 0 = none) with your own chosen cape. The DLL owns the mapping.
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace MyClientCapes {
constexpr uint32_t Magic = 0x4d434350, Version = 2;   // "MCCP"; v2: role tiers and independent visibility
constexpr uint32_t MaxPlayers = 64, XuidSize = 24;
struct Shared {
    uint32_t magic, version, size, pid;
    uint64_t dllAt, hudAt;          // DLL: when it last wrote the list; HUD: when it last wrote the capes
    char ownXuid[XuidSize];         // DLL: your own XUID ("" = not known)
    uint32_t count;                 // DLL: players in xuids
    char xuids[MaxPlayers][XuidSize];
    uint8_t capes[MaxPlayers];      // HUD: cape id of xuids[i] (0 = none / not known)
    uint32_t ownCape;               // HUD: the cape you chose (0 = your own / none)
    uint8_t flags[MaxPlayers];     // HUD: low 4 bits = mark tier 0..15; upper bits must be zero
    uint32_t ownHideCape, ownHideMark, showMarks; // HUD: booleans (0/1)
    uint32_t ownTier;              // HUD: local player role tier (0..15)
};
static_assert(offsetof(Shared, ownTier) == 1740);
static_assert(offsetof(Shared, flags) == 1664 && offsetof(Shared, ownHideCape) == 1728);
static_assert(offsetof(Shared, ownHideMark) == 1732 && offsetof(Shared, showMarks) == 1736);
static_assert(sizeof(Shared) == 1744 && offsetof(Shared, capes) == 1596);
static_assert(std::is_trivially_copyable<Shared>::value);

inline bool Valid(const Shared& s, uint32_t pid) {
    if (s.magic != Magic || s.version != Version || s.size != sizeof(Shared) || s.pid != pid) return false;
    if (s.count > MaxPlayers || s.ownCape > 6 || s.ownHideCape > 1 || s.ownHideMark > 1 || s.showMarks > 1 || s.ownTier > 15) return false;
    for (uint8_t flag : s.flags) if (flag & 0xF0u) return false;
    return true;
}
inline bool Fresh(uint64_t at, uint64_t now) { return at && at <= now && now - at <= 3000; }
// The HUD's answers stay good this long (review I6: its loop with two HTTPS calls can take longer than 3 s).
constexpr uint64_t AnswerKeepMs = 30000;
inline bool AnswerFresh(uint64_t at, uint64_t now) { return at && at <= now && now - at <= AnswerKeepMs; }
// An XUID is 1..20 digits; anything else is stored as "".
inline void CopyXuid(char (&out)[XuidSize], std::string_view xuid) {
    out[0] = 0;
    if (xuid.empty() || xuid.size() > 20) return;
    for (char c : xuid) if (c < '0' || c > '9') return;
    std::memcpy(out, xuid.data(), xuid.size());
    out[xuid.size()] = 0;
}

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
    bool Lock() {
        if (!mutex_) return false;
        const DWORD r = WaitForSingleObject(mutex_, 0);
        if (r == WAIT_ABANDONED) { if (data_) data_->dllAt = data_->hudAt = 0; ReleaseMutex(mutex_); return false; }
        return r == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = std::wstring(L"Local\\MyClient.Capes.v2.") + std::to_wstring(pid);
        mutex_ = producer ? CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str()) : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Lock").c_str());
        if (!mutex_) return false;
        mapping_ = producer ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name.c_str())
                            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping_) data_ = static_cast<Shared*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!data_) { Close(); return false; }
        if (producer) {
            if (!Lock()) { Close(); return false; }
            *data_ = {}; data_->magic = Magic; data_->version = Version; data_->size = sizeof(Shared); data_->pid = pid; data_->showMarks = 1;
            Unlock();
        }
        return true;
    }
};
#endif
} // namespace MyClientCapes

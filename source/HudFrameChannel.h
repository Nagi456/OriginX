#pragma once
// The finished HUD picture, handed from MyClient.exe to the game DLL, which draws it inside the game's
// own frame (client/Compositor.cpp). Drawing the HUD in a window on top of the game made Windows
// compose the game through the desktop and capped it at the monitor refresh rate.
//
// One mapping per game process: a Header followed by width*height pixels (premultiplied BGRA, the
// layout OverlayAlpha::Convert produces). There is no mutex: the HUD (only writer) makes `seq` odd while
// it writes and even when done, the DLL copies only between two equal, even reads of `seq`.
// Pure logic, so the writer/reader pair is unit-tested on the host (tests/hud_frame.cpp).
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>
#ifdef _WIN32
#include <windows.h>
#include <string>
#endif

namespace MyClientHudFrame {
constexpr uint32_t Magic = 0x4d434846, Version = 1;   // "MCHF"
constexpr uint32_t MaxWidth = 3840, MaxHeight = 2160;
constexpr uint64_t StaleMs = 1000;   // a picture older than this is not drawn (HUD closed or in its menu)

// DLL states (dllState)
enum DllState : uint32_t { NotReady = 0, Ready = 1, Drawing = 2, Unsupported = 3 };

struct Header {
    uint32_t magic, version, size, pid;   // pid of the game
    uint64_t hudAt;        // HUD: time of the last picture (GetTickCount64); 0 = draw nothing
    uint64_t dllAt;        // DLL: last time the compositor ran (the HUD only hands pictures over while fresh)
    std::atomic<uint32_t> seq;   // odd while the HUD writes
    uint32_t width, height;      // picture size in pixels
    uint32_t dirtyTop, dirtyBottom;   // rows [top, bottom) changed by the write that made `seq` even
    uint32_t dllState, dllError;      // DllState, and a code for the log when Unsupported
    uint32_t backWidth, backHeight;   // the game's back buffer (for the HUD's status line)
    uint32_t reserved[5];
};
static_assert(sizeof(Header) == 88 && sizeof(std::atomic<uint32_t>) == 4);
constexpr size_t MappingBytes = sizeof(Header) + size_t(MaxWidth) * MaxHeight * 4;

inline uint32_t* Pixels(Header* h) { return reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(h) + sizeof(Header)); }
inline const uint32_t* Pixels(const Header* h) { return reinterpret_cast<const uint32_t*>(reinterpret_cast<const char*>(h) + sizeof(Header)); }
inline bool Fresh(uint64_t at, uint64_t now, uint64_t limit = StaleMs) { return at != 0 && at <= now && now - at <= limit; }

// HUD side: copies rows [top, bottom) of a width*height picture. A new size always copies everything.
inline bool Write(Header* h, const uint32_t* picture, uint32_t width, uint32_t height, uint32_t top, uint32_t bottom, uint64_t now) {
    if (!h || width == 0 || height == 0 || width > MaxWidth || height > MaxHeight) return false;
    if (width != h->width || height != h->height) { top = 0; bottom = height; }
    if (bottom > height) bottom = height;
    if (top > bottom) top = bottom;
    h->seq.fetch_add(1, std::memory_order_acq_rel);   // odd: writing
    h->width = width; h->height = height;
    if (bottom > top)
        std::memcpy(Pixels(h) + size_t(top) * width, picture + size_t(top) * width, size_t(bottom - top) * width * 4);
    h->dirtyTop = top; h->dirtyBottom = bottom;
    h->hudAt = now;
    h->seq.fetch_add(1, std::memory_order_acq_rel);   // even: done
    return true;
}

// DLL side. Tracks what it already has; Take() reports which rows to (re)upload.
struct Reader {
    uint32_t seenSeq = 0, width = 0, height = 0;
    bool have = false;
    // Returns true when something changed: rows [top, bottom) of `out` (width*height) were refreshed.
    // `out` must hold MaxWidth*MaxHeight pixels or at least width*height of the current picture.
    bool Take(const Header* h, uint32_t* out, uint32_t& top, uint32_t& bottom) {
        const uint32_t s1 = h->seq.load(std::memory_order_acquire);
        if (s1 & 1u) return false;               // the HUD is writing right now: next frame
        if (have && s1 == seenSeq) return false; // nothing new
        const uint32_t w = h->width, ht = h->height;
        if (w == 0 || ht == 0 || w > MaxWidth || ht > MaxHeight) return false;
        // Exactly one write since our copy: only its rows. Otherwise (missed writes, new size): everything.
        const bool one = have && s1 == seenSeq + 2 && w == width && ht == height;
        uint32_t t = one ? h->dirtyTop : 0, b = one ? h->dirtyBottom : ht;
        if (b > ht) b = ht;
        if (t > b) t = b;
        if (b > t) std::memcpy(out + size_t(t) * w, Pixels(h) + size_t(t) * w, size_t(b - t) * w * 4);
        if (h->seq.load(std::memory_order_acquire) != s1) { have = false; return false; }   // torn: retry in full
        seenSeq = s1; width = w; height = ht; have = true;
        top = t; bottom = b;
        return true;
    }
};

#ifdef _WIN32
// The game DLL creates the mapping (producer), the HUD opens it.
class Channel {
    HANDLE mapping_ = nullptr;
    Header* data_ = nullptr;
public:
    Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    ~Channel() { Close(); }
    Header* Data() { return data_; }
    void Close() {
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        data_ = nullptr; mapping_ = nullptr;
    }
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = L"Local\\MyClient.HudFrame.v1." + std::to_wstring(pid);
        mapping_ = producer
            ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT,
                                 static_cast<DWORD>(uint64_t(MappingBytes) >> 32), static_cast<DWORD>(MappingBytes), name.c_str())
            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping_) data_ = static_cast<Header*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, MappingBytes));
        if (!data_) { Close(); return false; }
        if (producer) {
            std::memset(static_cast<void*>(data_), 0, sizeof(Header));
            data_->magic = Magic; data_->version = Version; data_->size = sizeof(Header); data_->pid = pid;
        } else if (data_->magic != Magic || data_->version != Version || data_->size != sizeof(Header) || data_->pid != pid) {
            Close();
            return false;
        }
        return true;
    }
};
#endif
}  // namespace MyClientHudFrame

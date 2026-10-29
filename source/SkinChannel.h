#pragma once
// The local player's skin picture, shared from the game DLL to the HUD (the head in the middle of the
// menu's home screen, the Closet page). Its own mapping: an older DLL simply leaves the default head.
// The DLL writes only when the skin changes (`serial` goes up); the HUD copies the pixels only then.
// A second mapping (Request) goes the other way: the Closet asks the DLL to wear a skin.
// Pure helpers (format conversion, face extraction) are unit-tested: tests/skin.cpp.
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <string>
#include <algorithm>
#endif

namespace MyClientSkin {
constexpr uint32_t Magic = 0x4d43534b, Version = 1;   // "MCSK"
constexpr uint32_t MaxSide = 256;                     // 64x64 classic, 128x128 HD; bigger skins are skipped
struct Shared {
    uint32_t magic, version, size, pid;
    uint64_t at;              // when the DLL last confirmed the skin (0 = no player)
    uint32_t serial;          // goes up whenever the pixels change
    uint32_t width, height;   // 0 = no classic skin picture (e.g. a persona skin the game builds itself)
    uint32_t flags;           // NoClassicPicture: the player is in a world but wears a skin the head can't be read from
    uint8_t rgba[MaxSide * MaxSide * 4];   // straight (not premultiplied) RGBA, rows top to bottom
};
static_assert(sizeof(Shared) == 40 + MaxSide * MaxSide * 4);
static_assert(std::is_trivially_copyable<Shared>::value);

constexpr uint32_t NoClassicPicture = 1;   // Shared.flags (older DLLs leave 0)

// Is the skin's head where a classic skin has it (8,8 face, 40,8 hat)? Persona (character creator) skins and
// skins with their own model (geometry.<pack>.<name>) lay their picture out in their own way.
inline bool ClassicLayout(bool persona, const std::string& geometry, const std::string& resourcePatch) {
    if (persona) return false;
    if (!geometry.empty()) return geometry.find("geometry.humanoid") == 0;
    return resourcePatch.empty() || resourcePatch.find("\"geometry.humanoid") != std::string::npos;
}

inline bool Valid(const Shared& data, uint32_t pid) {
    return data.magic == Magic && data.version == Version && data.size == sizeof(Shared) && data.pid == pid;
}

// Skin bytes from the game (mce::ImageFormat: 3 = RGB8, 4 = RGBA8) -> RGBA. False when the picture is
// not a usable skin: unknown format, too few bytes, not square-ish (64x32 legacy, 64x64, 128x128 ...).
inline bool ToRgba(uint32_t format, const uint8_t* bytes, size_t size, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    out.clear();
    if (!bytes || w < 64 || w > MaxSide || w % 64 || (h != w && h != w / 2)) return false;
    const size_t pixels = static_cast<size_t>(w) * h;
    if (format == 4) {
        if (size < pixels * 4) return false;
        out.assign(bytes, bytes + pixels * 4);
        return true;
    }
    if (format == 3) {
        if (size < pixels * 3) return false;
        out.resize(pixels * 4);
        for (size_t i = 0; i < pixels; i++) {
            out[i * 4] = bytes[i * 3]; out[i * 4 + 1] = bytes[i * 3 + 1]; out[i * 4 + 2] = bytes[i * 3 + 2]; out[i * 4 + 3] = 255;
        }
        return true;
    }
    return false;
}

inline uint32_t Hash(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    uint32_t x = 2166136261u ^ w ^ (h << 16);
    for (uint8_t b : rgba) { x ^= b; x *= 16777619u; }
    return x;
}

// One square part of the skin picture (face, hat ...), given in 64-wide skin units and scaled to the
// picture's real width. Out: side*side RGBA (side = w / 8 for the face).
inline bool Region(const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t ux, uint32_t uy, uint32_t uside, std::vector<uint8_t>& out) {
    out.clear();
    if (!rgba || w < 64 || w % 64) return false;
    const uint32_t k = w / 64, x0 = ux * k, y0 = uy * k, side = uside * k;
    if (x0 + side > w || y0 + side > h) return false;
    out.resize(static_cast<size_t>(side) * side * 4);
    for (uint32_t y = 0; y < side; y++)
        std::memcpy(&out[static_cast<size_t>(y) * side * 4], rgba + (static_cast<size_t>(y0 + y) * w + x0) * 4, static_cast<size_t>(side) * 4);
    return true;
}

// The head as seen from the front: the face (8,8) with the hat layer (40,8) over it (alpha-blended;
// fully transparent hat pixels leave the face). Out: opaque RGBA, side = w / 8.
inline bool Head(const uint8_t* rgba, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    std::vector<uint8_t> hat;
    if (!Region(rgba, w, h, 8, 8, 8, out)) return false;
    if (Region(rgba, w, h, 40, 8, 8, hat))
        for (size_t i = 0; i + 3 < out.size(); i += 4) {
            const uint32_t a = hat[i + 3];
            for (int c = 0; c < 3; c++) out[i + c] = static_cast<uint8_t>((hat[i + c] * a + out[i + c] * (255 - a)) / 255);
        }
    for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    return true;
}

// Thin (3 px) arms? A 64x64 skin made for the slim model leaves the 4th column of each arm's front
// and back unused: x 54..55, y 20..31 (right arm) stays fully transparent. 64x32 skins are always wide.
inline bool LooksSlim(const uint8_t* rgba, uint32_t w, uint32_t h) {
    if (!rgba || w < 64 || w % 64 || h != w) return false;
    const uint32_t k = w / 64;
    for (uint32_t y = 20 * k; y < 32 * k; y++)
        for (uint32_t x = 54 * k; x < 56 * k; x++)
            if (rgba[(static_cast<size_t>(y) * w + x) * 4 + 3] != 0) return false;
    return true;
}

// The resource patch a classic skin is worn with (the game's built-in humanoid models).
inline const char* ResourcePatch(bool slim) {
    return slim ? "{\"geometry\":{\"default\":\"geometry.humanoid.customSlim\"}}"
                : "{\"geometry\":{\"default\":\"geometry.humanoid.custom\"}}";
}

// HUD -> DLL: "wear this skin" (the Closet). Like Shared, the DLL owns the mapping; the HUD writes a
// request (serial goes up) and the DLL applies it on the next world tick, then answers in handled/result.
constexpr uint32_t RequestMagic = 0x4d435352, RequestVersion = 1;   // "MCSR"
enum class Result : uint32_t { None = 0, Sent = 1, NoPlayer = 2, Failed = 3 };
struct Request {
    uint32_t magic, version, size, pid;
    uint32_t serial;          // HUD: goes up with every new request
    uint32_t handled;         // DLL: the serial it last answered
    uint32_t result;          // DLL: Result for `handled`
    uint32_t width, height;
    uint32_t slim;            // 1 = thin arms (geometry.humanoid.customSlim)
    char name[64];            // UTF-8 skin name, NUL-terminated
    uint8_t rgba[MaxSide * MaxSide * 4];   // straight RGBA, rows top to bottom
};
static_assert(sizeof(Request) == 104 + MaxSide * MaxSide * 4);
static_assert(std::is_trivially_copyable<Request>::value);

inline bool Valid(const Request& data, uint32_t pid) {
    return data.magic == RequestMagic && data.version == RequestVersion && data.size == sizeof(Request) && data.pid == pid;
}

// The skin of a request, checked: the size must be a classic skin shape and the bytes must fit.
inline bool UsableRequest(const Request& r) {
    return r.width >= 64 && r.width <= MaxSide && r.width % 64 == 0 && (r.height == r.width || r.height == r.width / 2);
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
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = L"Local\\MyClient.Skin.v1." + std::to_wstring(pid);
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
    bool Lock() {
        const DWORD result = WaitForSingleObject(mutex_, 0);
        if (result == WAIT_ABANDONED) { Reset(data_->pid); ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    // Producer side: a new picture (w == 0: none). The serial only moves when the pixels or flags changed.
    bool Publish(DWORD pid, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, uint64_t at, uint32_t flags = 0) {
        if (!data_ || !Lock()) return false;
        if (!Valid(*data_, pid)) Reset(pid);
        const size_t bytes = static_cast<size_t>(w) * h * 4;
        if (w > MaxSide || h > MaxSide || rgba.size() < bytes) { w = h = 0; }
        if (data_->width != w || data_->height != h || data_->flags != flags ||
            (w && std::memcmp(data_->rgba, rgba.data(), static_cast<size_t>(w) * h * 4) != 0)) {
            data_->width = w; data_->height = h; data_->flags = flags;
            if (w) std::memcpy(data_->rgba, rgba.data(), static_cast<size_t>(w) * h * 4);
            data_->serial++;
        }
        data_->at = at;
        Unlock();
        return true;
    }
    // Consumer side: copies the picture only when its serial differs from `serial` (updated). True = copied.
    bool TakeIfNew(DWORD pid, uint32_t& serial, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h, uint32_t& flags) {
        if (!data_ || !Lock()) return false;
        bool taken = false;
        if (Valid(*data_, pid) && data_->serial != serial) {
            serial = data_->serial;
            w = data_->width; h = data_->height; flags = data_->flags;
            rgba.assign(data_->rgba, data_->rgba + static_cast<size_t>(w) * h * 4);
            taken = true;
        }
        Unlock();
        return taken;
    }
};

// The Request mapping. Producer = the DLL (creates it), consumer = the HUD (opens it, writes requests).
class RequestChannel {
    HANDLE mapping_ = nullptr, mutex_ = nullptr;
    Request* data_ = nullptr;
public:
    RequestChannel() = default;
    RequestChannel(const RequestChannel&) = delete;
    RequestChannel& operator=(const RequestChannel&) = delete;
    ~RequestChannel() { Close(); }
    Request* Data() { return data_; }
    void Close() {
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        data_ = nullptr; mapping_ = mutex_ = nullptr;
    }
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = L"Local\\MyClient.SkinRequest.v1." + std::to_wstring(pid);
        mutex_ = producer ? CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str())
            : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Lock").c_str());
        if (!mutex_) return false;
        mapping_ = producer ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Request), name.c_str())
            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping_) data_ = static_cast<Request*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Request)));
        if (!data_) { Close(); return false; }
        if (producer) {
            const DWORD result = WaitForSingleObject(mutex_, 20);
            if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) { Close(); return false; }
            Reset(pid); ReleaseMutex(mutex_);
        }
        return true;
    }
    void Reset(DWORD pid) {
        std::memset(data_, 0, sizeof(Request));
        data_->magic = RequestMagic; data_->version = RequestVersion; data_->size = sizeof(Request); data_->pid = pid;
    }
    bool Lock(DWORD timeout = 0) {
        const DWORD result = WaitForSingleObject(mutex_, timeout);
        if (result == WAIT_ABANDONED) { Reset(data_->pid); ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    // HUD: a new request. Out: its serial (to match the DLL's answer). False = not written (try again).
    bool Send(DWORD pid, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, bool slim, const std::string& name, uint32_t& serial) {
        if (!data_ || w > MaxSide || h > MaxSide || rgba.size() < static_cast<size_t>(w) * h * 4 || !Lock(20)) return false;
        bool sent = false;
        if (Valid(*data_, pid)) {
            data_->width = w; data_->height = h; data_->slim = slim ? 1 : 0;
            std::memset(data_->name, 0, sizeof(data_->name));
            std::memcpy(data_->name, name.data(), std::min(name.size(), sizeof(data_->name) - 1));
            std::memcpy(data_->rgba, rgba.data(), static_cast<size_t>(w) * h * 4);
            serial = ++data_->serial;
            sent = true;
        }
        Unlock();
        return sent;
    }
    // HUD: the DLL's answer to `serial` (Result::None while it has not answered yet).
    Result Answer(DWORD pid, uint32_t serial) {
        if (!data_ || !Lock()) return Result::None;
        const Result r = Valid(*data_, pid) && data_->handled == serial ? static_cast<Result>(data_->result) : Result::None;
        Unlock();
        return r;
    }
    // DLL: copies a request it has not answered yet. True = `out` holds one; answer it with Finish.
    bool TakePending(DWORD pid, Request& out) {
        if (!data_ || !Lock()) return false;
        const bool pending = Valid(*data_, pid) && data_->serial != data_->handled;
        if (pending) std::memcpy(&out, data_, sizeof(Request));
        Unlock();
        return pending;
    }
    void Finish(DWORD pid, uint32_t serial, Result result) {
        if (!data_ || !Lock(20)) return;
        if (Valid(*data_, pid)) { data_->handled = serial; data_->result = static_cast<uint32_t>(result); }
        Unlock();
    }
};
#endif
}

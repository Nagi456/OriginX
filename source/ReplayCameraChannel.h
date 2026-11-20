#pragma once
// Replay camera path, HUD -> DLL (2026-11-12, docs/superpowers/specs/2026-11-12-replay-camera-editor-design.md): the
// camera points of the recording being played and the replay clock. The HUD creates the mapping (named after the
// game's pid) and refreshes it every HUD tick; the DLL reads it every frame and, while `active`, puts the free-camera
// viewer where the path is at that time. Own mapping: an older DLL or HUD simply never moves the camera.
#ifdef _WIN32
#include <windows.h>
#endif
#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>
#include "ReplayCamera.h"

namespace MyClientReplayCameraChannel {
constexpr uint32_t Magic = 0x4d435243, Version = 1;
constexpr uint64_t StaleMs = 500;   // the playback refreshes it constantly; older = it stopped, the DLL lets the camera go
constexpr uint32_t MaxPoints = static_cast<uint32_t>(MyClientReplayCamera::MaxKeyframes);

struct Point { uint64_t t; float x, y, z, pitch, yaw; uint8_t ease, pad[3]; };
struct Shared {
    uint32_t magic, version, size, pid;
    uint64_t at;          // when the replay position below was taken (NowMicros, microseconds)
    double timeMs;        // the replay's position at `at` (milliseconds; a fraction for export frames)
    float speed;          // playback speed (1 = real time)
    uint8_t active;       // 1 = the camera follows the path now (free camera, playing, not taken back by the viewer)
    uint8_t paused, pad[2];
    uint32_t count, pad2;
    Point points[MaxPoints];
};
static_assert(sizeof(Point) == 32 && sizeof(Shared) == 48 + 64 * 32);
static_assert(std::is_trivially_copyable<Shared>::value);

inline bool Valid(const Shared& s, uint32_t pid) {
    return s.magic == Magic && s.version == Version && s.size == sizeof(Shared) && s.pid == pid && s.count <= MaxPoints;
}
// A fine clock every process of the PC shares (QueryPerformanceCounter), in microseconds. `at` is on it: the
// 15.6 ms steps of GetTickCount64 made the camera stand still for frames and then jump (review, 2026-11-12).
inline uint64_t NowMicros() {
#ifdef _WIN32
    LARGE_INTEGER count{}, frequency{};
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart <= 0) return 0;
    return static_cast<uint64_t>(count.QuadPart / frequency.QuadPart) * 1000000ull +
           static_cast<uint64_t>(count.QuadPart % frequency.QuadPart) * 1000000ull / static_cast<uint64_t>(frequency.QuadPart);
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
// `at` and `now` in microseconds (NowMicros).
inline bool Fresh(uint64_t at, uint64_t now) { return at != 0 && at <= now && now - at <= StaleMs * 1000; }

// The replay's position at `now` (microseconds): `timeMs` was the position when it was taken at `at`; it runs on at
// `speed` unless paused (never backwards).
inline double TimeAt(const Shared& s, uint64_t now) {
    if (s.paused || now <= s.at) return s.timeMs;
    return s.timeMs + static_cast<double>(now - s.at) / 1000.0 * static_cast<double>(s.speed);
}

// The path time never steps back a little while playing: two samples of the replay position are taken at slightly
// different moments, which could pull the camera back a few milliseconds. A jump back of more than 250 ms is a
// seek and is followed; while paused the time is used as it is.
struct Steady {
    double last = -1.0;
    double Next(double t, bool paused) {
        if (!paused && last >= 0.0 && t < last && last - t < 250.0) return last;
        last = t;
        return t;
    }
};

inline void Fill(Shared& s, const std::vector<MyClientReplayCamera::Keyframe>& path) {
    s.count = 0;
    for (const auto& k : path) {
        if (s.count >= MaxPoints) break;
        Point& p = s.points[s.count++];
        p = Point{k.t, k.pose.x, k.pose.y, k.pose.z, k.pose.pitch, k.pose.yaw, static_cast<uint8_t>(k.ease), {0, 0, 0}};
    }
}
inline void Read(const Shared& s, std::vector<MyClientReplayCamera::Keyframe>& path) {
    path.clear();
    for (uint32_t i = 0; i < s.count && i < MaxPoints; ++i) {
        const Point& p = s.points[i];
        const uint8_t e = p.ease <= static_cast<uint8_t>(MyClientReplayCamera::Ease::Cut) ? p.ease : static_cast<uint8_t>(MyClientReplayCamera::Ease::InOut);
        path.push_back(MyClientReplayCamera::Keyframe{p.t, MyClientReplayCamera::Pose{p.x, p.y, p.z, p.pitch, p.yaw}, static_cast<MyClientReplayCamera::Ease>(e)});
    }
}

#ifdef _WIN32
// The HUD is the producer (Open(gamePid, true)), the DLL the consumer (Open(GetCurrentProcessId(), false)).
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
        const auto name = std::wstring(L"Local\\MyClient.ReplayCamera.v1.") + std::to_wstring(pid);
        mutex_ = producer ? CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str())
            : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Lock").c_str());
        if (!mutex_) return false;
        mapping_ = producer ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name.c_str())
            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping_) data_ = static_cast<Shared*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!data_) { Close(); return false; }
        return true;
    }
    bool Lock() {
        const DWORD result = WaitForSingleObject(mutex_, 0);
        if (result == WAIT_ABANDONED) { ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    // HUD: the whole state, every tick. active = false still refreshes `at` (the DLL then leaves the camera alone).
    bool Publish(DWORD pid, const std::vector<MyClientReplayCamera::Keyframe>& path, bool active, double timeMs,
                 float speed, bool paused, uint64_t at) {
        if (!data_ || !Lock()) return false;
        Shared& s = *data_;
        s.magic = Magic; s.version = Version; s.size = sizeof(Shared); s.pid = pid;
        Fill(s, path);
        s.active = active ? 1 : 0; s.paused = paused ? 1 : 0; s.speed = speed; s.timeMs = timeMs; s.at = at;
        Unlock();
        return true;
    }
    // DLL: a copy, or false (no mapping, busy this instant, not valid).
    bool Snapshot(DWORD pid, Shared& out) {
        if (!data_ || !Lock()) return false;
        out = *data_;
        Unlock();
        return Valid(out, pid);
    }
};
#endif
}  // namespace MyClientReplayCameraChannel

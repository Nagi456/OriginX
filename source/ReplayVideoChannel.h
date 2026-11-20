#pragma once
// Replay editor preview and video export, HUD <-> DLL (2026-11-12, docs/superpowers/specs/2026-11-12-replay-camera-
// editor-design.md parts 3 and 5). The HUD creates the mapping (named after the game's pid) and writes its half; the
// DLL writes its own half. Both lock the mutex for every read or write.
//   HUD -> DLL: the preview box (where the editor shows the game picture, shrunk), and the export: a new exportId
//               starts one (size, fps, bitrate, path); requestFrame asks for the next frame; finish ends it.
//   DLL -> HUD: the export's state, the last frame written, the game picture's size, and why it failed.
// Own mapping: an older DLL or HUD simply never previews or exports.
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace MyClientReplayVideo {
constexpr uint32_t Magic = 0x4d435256, Version = 1;
constexpr uint32_t NoFrame = 0xFFFFFFFFu;
constexpr size_t PathChars = 260;
enum State : uint32_t { Idle = 0, Running = 1, Done = 2, Failed = 3 };
enum Finish : uint32_t { KeepGoing = 0, Close = 1, Cancel = 2 };

struct Shared {
    uint32_t magic, version, size, pid;
    // HUD -> DLL
    uint64_t hudAt;                                // GetTickCount64 when the HUD wrote
    float previewX, previewY, previewW, previewH;  // the editor's preview box, 0..1 of the game picture (w 0 = none)
    uint32_t exportId;                             // changes for every new export (0 = none)
    uint32_t exportWidth, exportHeight, exportFps, exportBitrate;
    uint32_t requestFrame;                         // the frame to capture next (NoFrame = none)
    uint32_t waitFrames;                           // game frames to let pass after the request before capturing
    uint32_t finish;                               // Finish
    wchar_t path[PathChars];                       // the .mp4 to write
    // DLL -> HUD
    uint64_t dllAt;
    uint32_t dllExportId;                          // the export the fields below belong to
    uint32_t state;                                // State
    uint32_t doneFrame;                            // the last frame written (NoFrame = none)
    uint32_t backWidth, backHeight;                // the game picture's size when the export started
    char error[160];                               // why it failed (UTF-8)
};
static_assert(std::is_trivially_copyable<Shared>::value);
static_assert(sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4);

inline void Reset(Shared& s, uint32_t pid) {
    std::memset(&s, 0, sizeof s);
    s.magic = Magic; s.version = Version; s.size = sizeof(Shared); s.pid = pid;
    s.requestFrame = NoFrame; s.doneFrame = NoFrame;
}
inline bool Valid(const Shared& s, uint32_t pid) {
    return s.magic == Magic && s.version == Version && s.size == sizeof(Shared) && s.pid == pid;
}
inline bool Fresh(uint64_t at, uint64_t now, uint64_t limitMs) { return at != 0 && at <= now && now - at <= limitMs; }

// A box inside the picture, at least 5% of it each way.
inline bool PreviewWanted(float x, float y, float w, float h) {
    return w >= 0.05f && h >= 0.05f && x >= 0.f && y >= 0.f && x + w <= 1.0001f && y + h <= 1.0001f;
}

inline void SetPath(Shared& s, const std::wstring& path) {
    const size_t n = path.size() < PathChars - 1 ? path.size() : PathChars - 1;
    for (size_t i = 0; i < n; ++i) s.path[i] = path[i];
    s.path[n] = 0;
}
inline void SetError(Shared& s, const std::string& text) {
    const size_t n = text.size() < sizeof s.error - 1 ? text.size() : sizeof s.error - 1;
    std::memcpy(s.error, text.data(), n);
    s.error[n] = 0;
}

#ifdef _WIN32
// The HUD is the producer (Open(gamePid, true)), the DLL opens it (Open(GetCurrentProcessId(), false)).
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
        const auto name = std::wstring(L"Local\\MyClient.ReplayVideo.v1.") + std::to_wstring(pid);
        mutex_ = producer ? CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str())
            : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Lock").c_str());
        if (!mutex_) return false;
        mapping_ = producer ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name.c_str())
            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping_) data_ = static_cast<Shared*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!data_) { Close(); return false; }
        if (producer && With([pid](Shared& s) { if (!Valid(s, pid)) Reset(s, pid); })) {}
        return true;
    }
    // Runs f on the shared data under the lock (waits up to 5 ms). False when not open or busy.
    template <class F> bool With(F&& f) {
        if (!data_) return false;
        const DWORD result = WaitForSingleObject(mutex_, 5);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) return false;
        f(*data_);
        ReleaseMutex(mutex_);
        return true;
    }
};
#endif
}  // namespace MyClientReplayVideo

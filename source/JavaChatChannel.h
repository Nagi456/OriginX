#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace MyClientJavaChat {
constexpr uint32_t Magic = 0x4f584a43, Version = 1, MaxLines = 12, MaxBytes = 768;
struct Snapshot { uint64_t at; uint32_t ready, count; char lines[MaxLines][MaxBytes]; };
struct Shared { uint32_t magic, version, size, pid; uint64_t requestedAt; uint32_t wanted, reserved; Snapshot snapshot; };
inline bool Fresh(uint64_t at, uint64_t now) { return at && now >= at && now - at < 500; }
inline bool NativeChatName(const std::string& name) {
    return name == "chat_panel" || name == "chat_grid" || name == "chat_stack_panel" ||
        name == "chat_background" || name == "chat_text" || name == "chat_label" || name == "chat_grid_item" ||
        name == "chat_message" || name == "chat_message_panel";
}
inline std::string BoundedUtf8(const std::string& text) {
    size_t n = std::min(text.size(), size_t(MaxBytes - 1));
    // Do not cut a UTF-8 character in half at the fixed shared-memory boundary.
    if (n < text.size()) while (n && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n;
    return text.substr(0, n);
}
#ifdef _WIN32
class Channel {
    HANDLE mapping_ = nullptr, mutex_ = nullptr;
    Shared* data_ = nullptr;
public:
    ~Channel() { Close(); }
    void Close() {
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        data_ = nullptr; mapping_ = mutex_ = nullptr;
    }
    bool IsOpen() const { return data_ != nullptr; }
    bool Open(DWORD pid, bool producer) {
        Close();
        const auto name = L"Local\\OriginX.JavaChat.v1." + std::to_wstring(pid);
        mutex_ = producer ? CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str()) :
            OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Lock").c_str());
        if (!mutex_) return false;
        mapping_ = producer ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name.c_str()) :
            OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping_) data_ = static_cast<Shared*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!data_) { Close(); return false; }
        if (producer) {
            if (!Lock(20)) { Close(); return false; }
            *data_ = {}; data_->magic = Magic; data_->version = Version; data_->size = sizeof(Shared); data_->pid = pid;
            Unlock();
        }
        return true;
    }
    bool Lock(DWORD ms) {
        if (!data_ || !mutex_) return false;
        const DWORD result = WaitForSingleObject(mutex_, ms);
        if (result == WAIT_ABANDONED) { data_->requestedAt = 0; data_->wanted = 0; data_->snapshot = {}; ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { if (mutex_) ReleaseMutex(mutex_); }
    bool Valid(DWORD pid) const { return data_ && data_->magic == Magic && data_->version == Version && data_->size == sizeof(Shared) && data_->pid == pid; }
    bool ReadAndRequest(DWORD pid, bool wanted, Snapshot& out) {
        if (!Lock(5)) return false;
        const bool ok = Valid(pid);
        if (ok) { data_->requestedAt = GetTickCount64(); data_->wanted = wanted ? 1 : 0; out = data_->snapshot; }
        Unlock(); return ok;
    }
    bool Wanted(DWORD pid) {
        if (!Lock(0)) return false;
        const bool wanted = Valid(pid) && data_->wanted && Fresh(data_->requestedAt, GetTickCount64());
        Unlock(); return wanted;
    }
    void Publish(DWORD pid, const Snapshot& snapshot) {
        if (!Lock(0)) return;
        if (Valid(pid)) data_->snapshot = snapshot;
        Unlock();
    }
};
#endif
} // namespace MyClientJavaChat

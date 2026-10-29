#pragma once
#include <windows.h>
#include <string>
#include "FeatureData.h"

namespace MyClientFeatures {
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
        const auto name = L"Local\\MyClient.Features.v1." + std::to_wstring(pid);
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
    // `waitMs` 0 = never block (the game's render thread); the HUD waits a moment so a busy lock does not
    // drop its read or its command.
    bool Lock(DWORD waitMs = 0) {
        const DWORD result = WaitForSingleObject(mutex_, waitMs);
        if (result == WAIT_ABANDONED) { *data_ = {}; ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
};
}

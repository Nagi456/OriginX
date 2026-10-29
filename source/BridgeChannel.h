#pragma once

#include "FrameValidation.h"
#include <string>

namespace MyClientIpc {
constexpr uint32_t Magic = 0x4d434231;
constexpr ULONGLONG StaleAfterMs = 2000;

struct SharedData {
    uint32_t magic;
    uint32_t abiVersion;
    uint32_t size;
    uint32_t processId;
    uint64_t publishedAt;
    LONG hasFrame;
    MyClientFrame frame;
};
static_assert(sizeof(SharedData) == 144, "Shared memory ABI changed");
static_assert(offsetof(SharedData, frame) == 28, "Shared layout changed");

class Channel {
public:
    Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    ~Channel() { Close(); }

    bool Open(DWORD pid, bool producer)
    {
        Close();
        const std::wstring name = L"Local\\MyClient.Bridge.v2." + std::to_wstring(pid);
        mutex_ = producer
            ? CreateMutexW(nullptr, FALSE, (name + L".Lock").c_str())
            : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE,
                         (name + L".Lock").c_str());
        if (!mutex_) return false;

        const DWORD access = producer ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
        mapping_ = producer
            ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                 0, sizeof(SharedData), name.c_str())
            : OpenFileMappingW(access, FALSE, name.c_str());
        if (mapping_)
            data_ = static_cast<SharedData*>(MapViewOfFile(mapping_, access, 0, 0,
                                                         sizeof(SharedData)));
        if (!data_)
        {
            const DWORD error = GetLastError();
            Close();
            SetLastError(error);
            return false;
        }
        return true;
    }

    void Close()
    {
        if (data_) UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        data_ = nullptr;
        mapping_ = mutex_ = nullptr;
    }

    // WAIT_ABANDONED also grants ownership: callers must discard/reset data.
    DWORD Lock(DWORD timeout) { return WaitForSingleObject(mutex_, timeout); }
    void Unlock() { ReleaseMutex(mutex_); }
    SharedData* Data() { return data_; }

private:
    HANDLE mutex_ = nullptr;
    HANDLE mapping_ = nullptr;
    SharedData* data_ = nullptr;
};

inline bool HasValidHeader(const SharedData& data, DWORD pid)
{
    return data.magic == Magic && data.abiVersion == MYCLIENT_ABI_VERSION &&
           data.size == sizeof(SharedData) && data.processId == pid &&
           (data.hasFrame == 0 || data.hasFrame == 1);
}
} // namespace MyClientIpc

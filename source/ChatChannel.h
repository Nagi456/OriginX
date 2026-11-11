#pragma once
// HUD -> DLL: "say this in the chat" (Command Hotkeys). The DLL owns the mapping; the HUD writes a line
// (serial goes up) and the DLL sends it on the next world tick, then answers in handled/result.
// A line starting with '/' goes to the server as a command, anything else as a chat message.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace MyClientChat {
constexpr uint32_t Magic = 0x4d434348, Version = 1;   // "MCCH"
constexpr size_t MaxText = 256;                       // UTF-8 bytes, NUL included
enum class Result : uint32_t { None = 0, Sent = 1, NoPlayer = 2, Failed = 3, Empty = 4 };

struct Request {
    uint32_t magic, version, size, pid;
    uint32_t serial;      // HUD: goes up with every new line
    uint32_t handled;     // DLL: the serial it last answered
    uint32_t result;      // DLL: Result for `handled`
    uint32_t reserved;
    char text[MaxText];   // UTF-8, NUL-terminated
};
static_assert(sizeof(Request) == 32 + MaxText);
static_assert(std::is_trivially_copyable<Request>::value);

inline bool Valid(const Request& data, uint32_t pid) {
    return data.magic == Magic && data.version == Version && data.size == sizeof(Request) && data.pid == pid;
}

// The line as it is sent: no line breaks (they would end the chat line), spaces trimmed at both ends,
// cut to MaxText - 1 bytes without splitting a UTF-8 character.
inline std::string Clean(const std::string& in) {
    std::string out;
    for (char c : in) out += (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    out = out.substr(first, out.find_last_not_of(' ') - first + 1);
    if (out.size() > MaxText - 1) {
        size_t cut = MaxText - 1;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    return out;
}

inline bool IsCommand(const std::string& line) { return !line.empty() && line[0] == '/'; }

// The same request / answer shape, second use: HUD -> DLL "join the replay server at <host>:<port>" (Replay Mod).
// Its own mapping (Channel::Open with this name); Sent = the game was asked to connect, Failed = it cannot be
// (the HUD then falls back to the minecraft:// link).
constexpr const wchar_t* ChatName = L"MyClient.Chat.v1.";
constexpr const wchar_t* ReplayJoinName = L"MyClient.ReplayJoin.v1.";

// "127.0.0.1:19150" -> host and port. False for anything else.
inline bool ParseHostPort(const std::string& text, std::string& host, uint16_t& port) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size() || text.size() - colon > 6) return false;
    unsigned value = 0;
    for (size_t i = colon + 1; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10 + static_cast<unsigned>(text[i] - '0');
    }
    if (value == 0 || value > 65535) return false;
    host = text.substr(0, colon);
    port = static_cast<uint16_t>(value);
    return true;
}

#ifdef _WIN32
// Producer = the DLL (creates it), consumer = the HUD (opens it, writes lines).
class Channel {
    HANDLE mapping_ = nullptr, mutex_ = nullptr;
    Request* data_ = nullptr;
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
    bool Open(DWORD pid, bool producer, const wchar_t* base = ChatName) {
        Close();
        const auto name = std::wstring(L"Local\\") + base + std::to_wstring(pid);
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
        data_->magic = Magic; data_->version = Version; data_->size = sizeof(Request); data_->pid = pid;
    }
    bool Lock(DWORD timeout = 0) {
        if (!data_ || !mutex_) return false;
        const DWORD result = WaitForSingleObject(mutex_, timeout);
        if (result == WAIT_ABANDONED) { Reset(data_->pid); ReleaseMutex(mutex_); return false; }
        return result == WAIT_OBJECT_0;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    // HUD: a new line (already Clean). False = not written (no DLL, or the last line is still waiting).
    bool Send(DWORD pid, const std::string& line, uint32_t& serial) {
        if (!data_ || line.empty() || !Lock(20)) return false;
        bool sent = false;
        if (Valid(*data_, pid) && data_->serial == data_->handled) {
            std::memset(data_->text, 0, sizeof(data_->text));
            std::memcpy(data_->text, line.data(), std::min(line.size(), sizeof(data_->text) - 1));
            serial = ++data_->serial;
            sent = true;
        }
        Unlock();
        return sent;
    }
    // HUD: the DLL's answer to `serial` (Result::None while it has not answered yet).
    Result Answer(DWORD pid, uint32_t serial) {
        if (!Lock()) return Result::None;
        const Result r = Valid(*data_, pid) && data_->handled == serial ? static_cast<Result>(data_->result) : Result::None;
        Unlock();
        return r;
    }
    // DLL: the line it has not answered yet. True = `line` / `serial` hold one; answer it with Finish.
    bool TakePending(DWORD pid, std::string& line, uint32_t& serial) {
        if (!Lock()) return false;
        const bool pending = Valid(*data_, pid) && data_->serial != data_->handled;
        if (pending) {
            data_->text[MaxText - 1] = 0;
            line = data_->text;
            serial = data_->serial;
        }
        Unlock();
        return pending;
    }
    void Finish(DWORD pid, uint32_t serial, Result result) {
        if (!Lock(20)) return;
        if (Valid(*data_, pid)) { data_->handled = serial; data_->result = static_cast<uint32_t>(result); }
        Unlock();
    }
};
#endif
}

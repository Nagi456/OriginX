// Game functions by address when LeviLamina has no symbol for them: see GameRva.h.
#include "GameRva.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
enum : uint8_t { NotAsked = 0, Used = 1, Refused = 2 };
std::atomic<uint8_t> rowState[GameRva::kRowCount ? GameRva::kRowCount : 1];

struct Running
{
    uintptr_t base = 0;   // 0 = the running exe is none of the table's builds
    int exe = -1;
    uint32_t timestamp = 0;
};

// The running exe, and which of the table's builds it is.
const Running& MatchingExe()
{
    static const Running running = [] () -> Running {
        Running out;
        const auto module = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!module) return out;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return out;
        out.timestamp = nt->FileHeader.TimeDateStamp;
        out.exe = GameRva::FindExe(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        if (out.exe >= 0) out.base = reinterpret_cast<uintptr_t>(module);
        return out;
    }();
    return running;
}

bool Executable(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    if (info.State != MEM_COMMIT) return false;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(info.Protect & exec) || (info.Protect & PAGE_GUARD)) return false;
    const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return address + GameRva::kCheckBytes <= end;
}
} // namespace

namespace MyClientGame {
uintptr_t RvaFallback(const char* symbol)
{
    const Running& running = MatchingExe();
    if (!running.base) return 0;
    const int index = GameRva::FindRow(GameRva::kRows, GameRva::kRowCount, running.exe, symbol);
    if (index < 0) return 0;
    const uintptr_t base = running.base;
    const GameRva::Row& row = GameRva::kRows[index];
    const uintptr_t address = base + row.rva;
    if (!Executable(address) || !GameRva::BytesMatch(reinterpret_cast<const uint8_t*>(address), row))
    {
        rowState[index].store(Refused);
        return 0;
    }
    uint8_t expected = NotAsked;
    rowState[index].compare_exchange_strong(expected, Used);
    return rowState[index].load() == Used ? address : 0;
}

// The start-up self check found this signature NG (GameSigs.cpp): its rows are never handed out, so only the
// feature that needs it stays off.
void RefuseRvaLabel(const char* label)
{
    const Running& running = MatchingExe();
    if (!running.base || !label) return;
    for (size_t i = 0; i < GameRva::kRowCount; ++i)
        if (GameRva::kRows[i].exe == running.exe && std::strcmp(GameRva::kRows[i].label, label) == 0) rowState[i].store(Refused);
}

// For the log: "N of M used; refused (bytes differ): a, b" or why none apply.
std::string RvaReport()
{
    const Running& running = MatchingExe();
    if (!running.base)
    {
        char line[160];
        std::snprintf(line, sizeof(line), "no table for this game build (exe %08X), none used", running.timestamp);
        return line;
    }
    size_t used = 0, rows = 0;
    std::string refused, notAsked;
    for (size_t i = 0; i < GameRva::kRowCount; ++i)
    {
        if (GameRva::kRows[i].exe != running.exe) continue;
        ++rows;
        const uint8_t state = rowState[i].load();
        if (state == Used) ++used;
        std::string& list = state == Refused ? refused : notAsked;
        if (state == Used) continue;
        if (!list.empty()) list += ", ";
        list += GameRva::kRows[i].label;
    }
    std::string out = std::to_string(used) + " of " + std::to_string(rows) + " used";
    if (!refused.empty()) out += "; refused, the bytes differ: " + refused;
    if (!notAsked.empty()) out += "; not asked (the feature did not hook it): " + notAsked;
    return out;
}
} // namespace MyClientGame

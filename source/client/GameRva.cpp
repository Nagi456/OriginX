// Game functions by address when LeviLamina has no symbol for them: see GameRva.h.
#include "GameRva.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
enum : uint8_t { NotAsked = 0, Used = 1, Refused = 2, Relocated = 3 };
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
// The row's function when the table's address no longer holds it: the bytes the row compares (after its
// unchecked start, e.g. a hook's jump when it was dumped) are searched for in the exe's code. Used only when they
// occur exactly once and sit at a function's start (16-byte aligned, after padding), else 0.
std::string relocateNote;   // what the last search found, for the log
uintptr_t Relocate(uintptr_t base, const GameRva::Row& row)
{
    size_t first = 0;
    while (first < GameRva::kCheckBytes && !row.mask[first]) ++first;
    size_t fixed = 0;
    for (size_t i = first; i < GameRva::kCheckBytes; ++i) fixed += row.mask[i] ? 1 : 0;
    if (first == 0 || first > 8 || fixed < 8) return 0;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    uintptr_t found = 0; int matches = 0, rawMatches = 0;
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section)
    {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto* begin = reinterpret_cast<const uint8_t*>(base + section->VirtualAddress);
        const size_t size = section->Misc.VirtualSize;
        if (size < GameRva::kCheckBytes + 16) continue;
        for (size_t at = 16; at + GameRva::kCheckBytes <= size; ++at)
        {
            if (begin[at] != static_cast<uint8_t>(row.code[first])) continue;
            bool same = true;
            for (size_t i = first; i < GameRva::kCheckBytes && same; ++i)
                same = !row.mask[i] || begin[at + i - first] == static_cast<uint8_t>(row.code[i]);
            if (!same) continue;
            // The function's start: the nearest 16-byte boundary at or before where the row says it is, after
            // padding (the prologue can be longer than the bytes the row skipped).
            ++rawMatches;
            uintptr_t start = 0;
            for (uintptr_t candidate = (reinterpret_cast<uintptr_t>(begin + at) - first) & ~uintptr_t(15);
                 candidate + 32 >= reinterpret_cast<uintptr_t>(begin + at) && candidate > reinterpret_cast<uintptr_t>(begin);
                 candidate -= 16)
            {
                const uint8_t before = *reinterpret_cast<const uint8_t*>(candidate - 1);
                if (before == 0xCC || before == 0xC3 || before == 0x90) { start = candidate; break; }
            }
            if (!start) continue;
            if (++matches > 1) break;
            found = start;
        }
    }
    relocateNote += "; searched: " + std::to_string(rawMatches) + " byte matches, " + std::to_string(matches) +
                   " at a function start" + (matches == 1 ? " (used)" : " (not used: needs exactly 1)");
    return matches == 1 ? found : 0;
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
        // Someone else's hook at the function's start (a jump: E9 rel32, FF 25 abs, 48 B8 .. FF E0) replaced the
        // bytes the row checks (r57-test9: createPacket on the shipped 26.51 exe). The table's address is for this
        // exact exe (timestamp and size), and calling through the patch still reaches the function.
        if (Executable(address)) {
            const auto* code = reinterpret_cast<const uint8_t*>(address);
            if (relocateNote.empty()) {
                char hex[3 * 16 + 1]{};
                for (int i = 0; i < 16; ++i) std::snprintf(hex + i * 3, 4, "%02X ", code[i]);
                relocateNote = std::string("bytes at the table's address: ") + hex;
            }
            const bool patched = code[0] == 0xE9 || (code[0] == 0xFF && code[1] == 0x25) ||
                                 (code[0] == 0x48 && code[1] == 0xB8 && code[10] == 0xFF && code[11] == 0xE0);
            bool leadingUnchecked = true;
            for (size_t i = 0; i < 5 && leadingUnchecked; ++i) leadingUnchecked = !row.mask[i];
            if (patched && leadingUnchecked) {
                rowState[index].store(Relocated);
                return address;
            }
        }
        static std::atomic<uintptr_t> moved[GameRva::kRowCount ? GameRva::kRowCount : 1];
        static std::atomic<uint8_t> searched[GameRva::kRowCount ? GameRva::kRowCount : 1];
        if (!searched[index].exchange(1)) moved[index].store(Relocate(base, row));
        if (const uintptr_t relocated = moved[index].load(); relocated && Executable(relocated))
        {
            rowState[index].store(Relocated);
            return relocated;
        }
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
    std::string refused, notAsked, relocated;
    for (size_t i = 0; i < GameRva::kRowCount; ++i)
    {
        if (GameRva::kRows[i].exe != running.exe) continue;
        ++rows;
        const uint8_t state = rowState[i].load();
        if (state == Used || state == Relocated) ++used;
        if (state == Relocated) { if (!relocated.empty()) relocated += ", "; relocated += GameRva::kRows[i].label; continue; }
        std::string& list = state == Refused ? refused : notAsked;
        if (state == Used) continue;
        if (!list.empty()) list += ", ";
        list += GameRva::kRows[i].label;
    }
    std::string out = std::to_string(used) + " of " + std::to_string(rows) + " used";
    if (!relocated.empty()) out += "; used although their bytes differ (another hook at the start, or found elsewhere by their bytes): " + relocated;
    if (!refused.empty()) out += "; refused, the bytes differ: " + refused + (relocateNote.empty() ? "" : " [" + relocateNote + "]");
    if (!notAsked.empty()) out += "; not asked (the feature did not hook it): " + notAsked;
    return out;
}
} // namespace MyClientGame

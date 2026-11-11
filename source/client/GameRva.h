#pragma once
// Game functions found by address, for a game build whose LeviLamina has no symbol for them (2026-11-05, step 3
// of leaving LeviLamina: 26.60's LeviLamina names about a tenth of the functions 26.51's did).
// tools/match_functions.py found them by comparing the 26.51 and 26.60 game images; tools/make_rva_table.py
// wrote client/GameRvaTable.inc: for one exe (timestamp and size), each function's names, RVA and first bytes.
// An address is used only when the running exe is that one and the bytes there are the expected ones, so a
// different build (or a wrong match) is never hooked: the feature is just missing, as with a missing symbol.
// The Windows part (reading the running exe) is client/GameRva.cpp; this part is tested in tests/game_rva.cpp.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace GameRva {
inline constexpr size_t kCheckBytes = 16;
inline constexpr size_t kMaxNames = 4;

struct Exe
{
    uint32_t timestamp;   // the exe's PE timestamp and SizeOfImage: which game build a row is for
    uint32_t imageSize;
};

struct Row
{
    int exe;                       // index into kExes
    const char* label;             // the MyClient hook it was found for
    const char* names[kMaxNames];  // the names the DLL looks it up by (nullptr after the last)
    uint32_t rva;
    const char* code;              // kCheckBytes bytes as in the dumped image
    const char* mask;              // kCheckBytes: 0 = not compared (relocated, or a hook's jump when dumped)
};

#define MYCLIENT_RVA_NAMES(...) __VA_ARGS__
#define MYCLIENT_RVA_EXE(index, timestamp, size)
#define MYCLIENT_RVA_ROW(exe, label, names, rva, code, mask) {exe, label, {MYCLIENT_RVA_NAMES names}, rva, code, mask},
inline constexpr Row kRows[] = {
#include "GameRvaTable.inc"
};
#undef MYCLIENT_RVA_EXE
#undef MYCLIENT_RVA_ROW
#define MYCLIENT_RVA_EXE(index, timestamp, size) {timestamp, size},
#define MYCLIENT_RVA_ROW(exe, label, names, rva, code, mask)
inline constexpr Exe kExes[] = {
#include "GameRvaTable.inc"
};
#undef MYCLIENT_RVA_EXE
#undef MYCLIENT_RVA_ROW
#undef MYCLIENT_RVA_NAMES
inline constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);
inline constexpr size_t kExeCount = sizeof(kExes) / sizeof(kExes[0]);

// Index into kExes of the build with that timestamp and size, or -1.
inline int FindExe(uint32_t timestamp, uint32_t imageSize)
{
    for (size_t i = 0; i < kExeCount; ++i)
        if (kExes[i].timestamp == timestamp && kExes[i].imageSize == imageSize) return static_cast<int>(i);
    return -1;
}

// Index of the row for that game build (exe index) one of whose names is symbol, or -1.
inline int FindRow(const Row* rows, size_t count, int exe, const char* symbol)
{
    if (!rows || !symbol || exe < 0) return -1;
    for (size_t i = 0; i < count; ++i)
    {
        if (rows[i].exe != exe) continue;
        for (const char* name : rows[i].names)
        {
            if (!name) break;
            if (std::strcmp(name, symbol) == 0) return static_cast<int>(i);
        }
    }
    return -1;
}

// The function's bytes in memory are the expected ones. A function that something hooked already (its first
// byte a jmp: LeviLamina's or another MyClient hook) is compared after that jump's 5 bytes.
inline bool BytesMatch(const uint8_t* memory, const Row& row)
{
    if (!memory || !row.code || !row.mask) return false;
    const size_t skip = memory[0] == 0xE9 ? 5 : 0;
    size_t compared = 0;
    for (size_t i = skip; i < kCheckBytes; ++i)
    {
        if (!row.mask[i]) continue;
        if (memory[i] != static_cast<uint8_t>(row.code[i])) return false;
        ++compared;
    }
    return compared >= 8;   // too few bytes left to tell the function apart: not used
}
} // namespace GameRva

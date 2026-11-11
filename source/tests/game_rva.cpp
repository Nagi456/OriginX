#include "../client/GameRva.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

int main()
{
    using namespace GameRva;
    // The tables made from the 26.51.1 and 26.60.5 images.
    const int v51 = FindExe(0x6AA482FDu, 0x12C0C000u), v60 = FindExe(0x6AD79EE4u, 0x13846000u);
    assert(v51 >= 0 && v60 >= 0 && v51 != v60);
    assert(FindExe(0x6AA482FDu, 0x1000u) == -1);   // same timestamp, other size: not that build
    // Preliminary matching reports for these versions must not silently activate unverified SDK layouts.
    assert(FindExe(0x6A8378BAu, 0x12888000u) == -1);
    assert(FindExe(0x6AEA437Eu, 0x1383D000u) == -1);
    size_t rows51 = 0, rows60 = 0;
    for (size_t i = 0; i < kRowCount; ++i)
    {
        const Row& row = kRows[i];
        (row.exe == v51 ? rows51 : rows60)++;
        assert(row.names[0]);
        for (const char* name : row.names)
            if (name) assert(FindRow(kRows, kRowCount, row.exe, name) == static_cast<int>(i));
        uint8_t copy[kCheckBytes];
        std::memcpy(copy, row.code, kCheckBytes);
        assert(BytesMatch(copy, row));
    }
    assert(rows51 >= 45 && rows60 >= 40);
    const char* updateSkin = "?updateSkin@Player@@QEAAXAEBVSerializedSkinRef@@H@Z";
    const int a = FindRow(kRows, kRowCount, v51, updateSkin), b = FindRow(kRows, kRowCount, v60, updateSkin);
    assert(a >= 0 && b >= 0 && kRows[a].rva == 0x210140u && kRows[b].rva == 0x2E83C0u);
    assert(FindRow(kRows, kRowCount, v51, "?nothing@@YAXXZ") == -1);
    assert(FindRow(kRows, kRowCount, -1, updateSkin) == -1);
    assert(FindRow(kRows, kRowCount, v51, nullptr) == -1);
    for (const char* required : {
             "?onFullPackStackInvalid@MinecraftGame@@UEAA_NXZ",
             "?isInGame@MinecraftGame@@UEBA_NXZ",
             "?isComplete@ResourceLoadManager@@QEBA_NXZ",
             "?_setAllDirty@RenderChunkCoordinator@@QEAAX_N0@Z",
             "?renderLevel@LevelRenderer@@QEAAXAEAVScreenContext@@AEBUFrameRenderObject@@@Z"}) {
        const int old = FindRow(kRows, kRowCount, v51, required), newer = FindRow(kRows, kRowCount, v60, required);
        assert(old >= 0 && newer >= 0 && kRows[old].rva != kRows[newer].rva);
    }

    const Row row{0, "Test", {"?a@@YAXXZ", nullptr}, 0x1000,
                  "\x55\x41\x57\x41\x56\x41\x55\x41\x54\x56\x57\x53\x48\x81\xec\xf8",
                  "\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01"};
    uint8_t memory[kCheckBytes];
    std::memcpy(memory, row.code, kCheckBytes);
    assert(BytesMatch(memory, row));
    memory[9] ^= 0xFF;
    assert(!BytesMatch(memory, row));
    memory[9] ^= 0xFF;
    memory[0] = 0xE9; memory[1] = 0x12; memory[2] = 0x34; memory[3] = 0x56; memory[4] = 0x78;
    assert(BytesMatch(memory, row));   // hooked by someone else first: the jump is not compared
    memory[12] ^= 0xFF;
    assert(!BytesMatch(memory, row));
    const Row sparse{0, "Sparse", {"?b@@YAXXZ", nullptr}, 0x1000, row.code,
                     "\x01\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00"};
    std::memcpy(memory, row.code, kCheckBytes);
    assert(!BytesMatch(memory, sparse));
    assert(!BytesMatch(nullptr, row));
    std::printf("game_rva ok (26.51: %zu functions, 26.60: %zu)\n", rows51, rows60);
    return 0;
}

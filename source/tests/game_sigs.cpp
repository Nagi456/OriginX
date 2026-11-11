// sigs (client/GameSigs.h, generated from tools/sigs.json): the tables are complete and the pattern work is right.
#include "../client/GameSigs.h"
#include "../client/GameRva.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    using namespace GameSigs;
    assert(kBuildCount >= 2 && kSigCount >= 80);
    const int b51 = FindBuild(0x6AA482FDu, 0x12C0C000u), b60 = FindBuild(0x6AD79EE4u, 0x13846000u);
    assert(b51 >= 0 && b60 >= 0 && b51 != b60 && FindBuild(0x6AA482FDu, 1) == -1 && FindBuild(0, 0) == -1);
    // every build of sigs.json is a build of the address table, and the other way round
    for (const Build& b : kBuilds) assert(GameRva::FindExe(b.timestamp, b.imageSize) == b.index);
    size_t fatal = 0;
    for (const Sig& s : kSigs)
    {
        assert(s.name && *s.name && s.purpose && *s.purpose && s.breaks && *s.breaks && s.lastUpdated && *s.lastUpdated);
        fatal += s.fatal;
        bool any = false;
        for (const Build& b : kBuilds)
            if (const At* at = AtFor(s.index, b.index))
            {
                any = true;
                std::vector<int> p;
                assert(ParsePattern(at->pattern, p) && p.size() >= 3);
                // the address table has this signature at the same RVA for that build
                bool inTable = false;
                for (const GameRva::Row& row : GameRva::kRows)
                    if (row.exe == b.index && std::strcmp(row.label, s.name) == 0) { inTable = row.rva == at->rva; }
                assert(inTable);
            }
        assert(any && Newest(s.index));
    }
    assert(fatal >= 3);   // the frame, the world tick and the world exit: without them MyClient cannot run
    uint32_t value = 0;
    assert(OffsetFor("MinecraftGame.ResourcePackListener", b51, value) && value == 0xC0);
    assert(OffsetFor("MinecraftGame.ResourcePackListener", b60, value) && value == 0xB8);
    assert(!OffsetFor("MinecraftGame.ResourcePackListener", -1, value) && !OffsetFor("nothing", b51, value));

    // patterns
    std::vector<int> p;
    assert(ParsePattern("48 8B ?? 05", p) && p == (std::vector<int>{0x48, 0x8B, -1, 0x05}));
    assert(ParsePattern("e8 ? ? ? ? c3", p) && p.size() == 6 && p[1] == -1);
    assert(!ParsePattern("48 8G", p) && p.empty());
    assert(!ParsePattern("488B", p) && !ParsePattern("", p) && !ParsePattern(nullptr, p));
    const uint8_t code[] = {0x90, 0x48, 0x8B, 0x11, 0x05, 0x90, 0x48, 0x8B, 0x22, 0x05, 0x48, 0x8B, 0x33, 0x06};
    ParsePattern("48 8B ?? 05", p);
    assert(MatchAt(code + 1, sizeof code - 1, p) && !MatchAt(code + 10, 4, p) && !MatchAt(code + 11, 3, p));
    auto hits = Scan(code, sizeof code, p, 4);
    assert((hits == std::vector<size_t>{1, 6}));
    assert(Scan(code, sizeof code, p, 1).size() == 1);
    ParsePattern("48 8B ?? 06", p);
    assert((Scan(code, sizeof code, p) == std::vector<size_t>{10}));
    ParsePattern("?? ?? 8B 33 06", p);
    assert((Scan(code, sizeof code, p) == std::vector<size_t>{9}));
    ParsePattern("8B 33 06 00", p);   // runs past the end
    assert(Scan(code, sizeof code, p).empty());
    {   // hooked by another module first: the 5-byte jump is not compared, the rest must match
        ParsePattern("48 89 5C 24 08 57 48 83 EC 20 48 8B", p);
        uint8_t fn[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B};
        bool hooked = true;
        assert(MatchAtMaybeHooked(fn, sizeof fn, p, hooked) && !hooked);
        fn[0] = 0xE9; fn[1] = 0x11; fn[2] = 0x22; fn[3] = 0x33; fn[4] = 0x44;
        assert(MatchAtMaybeHooked(fn, sizeof fn, p, hooked) && hooked);
        fn[7] = 0x00;
        assert(!MatchAtMaybeHooked(fn, sizeof fn, p, hooked) && !hooked);
        fn[7] = 0x83; fn[0] = 0xCC;
        assert(!MatchAtMaybeHooked(fn, sizeof fn, p, hooked));
    }
    const std::string line = ReportLine("NG", kSigs[0], "rva 0x10", "48 8B");
    assert(line.rfind("NG\t", 0) == 0 && line.find(kSigs[0].name) != std::string::npos);
    std::printf("game_sigs ok (%zu signatures, %zu fatal, %zu builds)\n", kSigCount, fatal, kBuildCount);
    return 0;
}

#include "../ReplayFormat.h"
#include <cassert>
#include <vector>
int main()
{
    using namespace MyClientReplay;
    const char* path = "/tmp/claude-replay-test.mcrec";
    {
        Writer w;
        assert(w.Open(path, 1700000000000ull, 1000, "26.51.1"));
        assert(w.Append("abc", 3, 1100));
        std::string big(5000, 'x');
        assert(w.Append(big.data(), big.size(), 4350));
        assert(w.Append("", 0, 1400));          // empty packets are ignored
        assert(w.Packets() == 2 && w.Bytes() == sizeof(Header) + 3 + 8 + 5000 + 8);
        assert(w.Mark(4500));                   // "recording starts here"
        assert(w.Append("z", 1, 4600, true));    // sent by the client
        assert(w.Packets() == 3 && w.Bytes() == sizeof(Header) + 3 + 8 + 5000 + 8 + 8 + 1 + 8);
        w.Close();
    }
    FILE* f = std::fopen(path, "rb");
    assert(f);
    Header h{};
    assert(std::fread(&h, sizeof h, 1, f) == 1);
    assert(h.durationSec == 3 && std::memcmp(h.magic, "MCREC001", 8) == 0 && h.version == Version && h.startUnixMs == 1700000000000ull);
    uint32_t sz, delta;
    assert(std::fread(&sz, 4, 1, f) == 1 && std::fread(&delta, 4, 1, f) == 1 && sz == 3 && delta == 100);
    char buf[16]{};
    assert(std::fread(buf, 1, 3, f) == 3 && std::string(buf) == "abc");
    assert(std::fread(&sz, 4, 1, f) == 1 && std::fread(&delta, 4, 1, f) == 1 && sz == 5000 && delta == 3250);
    std::vector<char> skip(5000);
    assert(std::fread(skip.data(), 1, 5000, f) == 5000);
    assert(std::fread(&sz, 4, 1, f) == 1 && std::fread(&delta, 4, 1, f) == 1 && sz == 0 && delta == 150);   // the mark
    assert(std::fread(&sz, 4, 1, f) == 1 && std::fread(&delta, 4, 1, f) == 1 && sz == (1u | SentFlag) && delta == 100);
    // packet ids: one-byte and two-byte varints
    assert(PacketId("\x0b", 1) == Ids::StartGame && PacketId("\x8f\x01", 2) == Ids::NetworkSettings && PacketId("", 0) == 0xFFFFFFFFu);
    std::fclose(f);
    std::remove(path);
    std::puts("replay ok");
}

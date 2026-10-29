#include "../HudFrameChannel.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace MyClientHudFrame;

int main()
{
    std::vector<char> mapping(MappingBytes);
    auto* h = reinterpret_cast<Header*>(mapping.data());
    std::vector<uint32_t> picture(4 * 3), out(MaxWidth * MaxHeight);
    for (uint32_t i = 0; i < picture.size(); i++) picture[i] = 100 + i;
    Reader r;
    uint32_t top = 9, bottom = 9;
    assert(!r.Take(h, out.data(), top, bottom));                 // nothing written yet (size 0)
    // first write: whole picture even if only row 1 is "dirty"
    assert(Write(h, picture.data(), 4, 3, 1, 2, 1000));
    assert(h->seq.load() == 2 && h->dirtyTop == 0 && h->dirtyBottom == 3 && h->hudAt == 1000);
    assert(r.Take(h, out.data(), top, bottom) && top == 0 && bottom == 3);
    assert(out[0] == 100 && out[11] == 111);
    assert(!r.Take(h, out.data(), top, bottom));                 // no new write
    // one write of row 2 only: the reader copies just that row
    picture[8] = 7; picture[0] = 5;                               // row 0 changed too, but not announced
    assert(Write(h, picture.data(), 4, 3, 2, 3, 1010));
    assert(r.Take(h, out.data(), top, bottom) && top == 2 && bottom == 3);
    assert(out[8] == 7 && out[0] == 100);
    // two writes missed: everything is copied again
    assert(Write(h, picture.data(), 4, 3, 1, 2, 1020));
    assert(Write(h, picture.data(), 4, 3, 1, 2, 1030));
    assert(r.Take(h, out.data(), top, bottom) && top == 0 && bottom == 3 && out[0] == 100 && out[8] == 7);
    // a write in progress (odd seq) is skipped
    h->seq.fetch_add(1);
    assert(!r.Take(h, out.data(), top, bottom));
    h->seq.fetch_add(1);
    // a new size copies everything and bad sizes are refused
    std::vector<uint32_t> small(2 * 2, 42);
    assert(Write(h, small.data(), 2, 2, 1, 1, 1040));
    assert(r.Take(h, out.data(), top, bottom) && top == 0 && bottom == 2 && r.width == 2 && out[3] == 42);
    assert(!Write(h, small.data(), MaxWidth + 1, 2, 0, 2, 1050));
    assert(Fresh(1000, 1500) && !Fresh(1000, 2500) && !Fresh(0, 10));
    std::puts("hud frame ok");
}

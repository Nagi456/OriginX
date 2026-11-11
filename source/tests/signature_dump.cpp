#include "../client/SignatureDump.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    using namespace SignatureDump;
    const uint8_t bytes[] = {0x48, 0x8B, 0x0A};
    assert(Hex(bytes, 3) == "48 8B 0A");
    assert(Hex(nullptr, 3).empty());

    std::vector<uint8_t> text(4096, 0xCC);
    uint32_t state = 12345;
    for (auto& b : text) { state = state * 1664525u + 1013904223u; b = static_cast<uint8_t>(state >> 24); }
    // A function whose first 16 bytes also appear elsewhere, but not its first 24.
    const size_t a = 1000, b = 3000;
    std::memcpy(&text[b], &text[a], 16);
    text[b + 16] = static_cast<uint8_t>(text[a + 16] ^ 0xFF);
    const std::vector<uint8_t> original(text.begin() + a, text.begin() + a + 128);
    assert(UniquePrefix(text.data(), text.size(), a, original.data(), original.size()) == 24);
    // Hooked: its own first bytes became a jump, the copied pattern still decides.
    text[a] = 0xE9;
    assert(UniquePrefix(text.data(), text.size(), a, original.data(), original.size()) == 24);
    // Unique from its very first bytes.
    assert(UniquePrefix(text.data(), text.size(), 2000, &text[2000], 128) == 8);
    // A copy shorter than the needed prefix, or shorter than the shortest one.
    assert(UniquePrefix(text.data(), text.size(), a, original.data(), 16) == 0);
    assert(UniquePrefix(text.data(), text.size(), a, original.data(), 4) == 0);
    // Repeated for all 128 bytes: never unique.
    std::vector<uint8_t> flat(1024, 0x90);
    assert(UniquePrefix(flat.data(), flat.size(), 100, &flat[100], 128) == 0);
    assert(UniquePrefix(nullptr, 10, 0, &flat[0], 128) == 0);
    // Jumps a thunk or a hook starts with.
    const uint8_t rel32[] = {0xE9, 0x10, 0x00, 0x00, 0x00};
    assert(JumpTarget(rel32, 5, 0x1000, nullptr) == 0x1015);
    const uint8_t back[] = {0xE9, 0xF0, 0xFF, 0xFF, 0xFF};
    assert(JumpTarget(back, 5, 0x1000, nullptr) == 0x0FF5);
    const uint8_t rel8[] = {0xEB, 0xFE};
    assert(JumpTarget(rel8, 2, 0x2000, nullptr) == 0x2000);
    const uint8_t indirect[] = {0xFF, 0x25, 0x02, 0x00, 0x00, 0x00};
    uintptr_t asked = 0;
    auto read = [&](uintptr_t at) -> uint64_t { asked = at; return 0x7FF600001234ull; };
    assert(JumpTarget(indirect, 6, 0x3000, read) == 0x7FF600001234ull && asked == 0x3008);
    const uint8_t rexIndirect[] = {0x48, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
    assert(JumpTarget(rexIndirect, 7, 0x3000, read) == 0x7FF600001234ull && asked == 0x3007);
    assert(JumpTarget(indirect, 6, 0x3000, nullptr) == 0);
    const uint8_t movJmp[] = {0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xE0};
    assert(JumpTarget(movJmp, 12, 0, nullptr) == 0x1122334455667788ull);
    const uint8_t pushRet[] = {0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x50, 0xC3};
    assert(JumpTarget(pushRet, 12, 0, nullptr) == 0x1122334455667788ull);
    const uint8_t r11[] = {0x49, 0xBB, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x41, 0xFF, 0xE3};
    assert(JumpTarget(r11, 13, 0, nullptr) == 0x0102030405060708ull);
    const uint8_t r10wrong[] = {0x49, 0xBA, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x41, 0xFF, 0xE3};
    assert(JumpTarget(r10wrong, 13, 0, nullptr) == 0);
    const uint8_t push32[] = {0x68, 0x44, 0x33, 0x22, 0x11, 0xC7, 0x44, 0x24, 0x04, 0xF6, 0x7F, 0x00, 0x00, 0xC3};
    assert(JumpTarget(push32, 14, 0, nullptr) == 0x00007FF611223344ull);
    const uint8_t stubPage[] = {0x50, 0x48, 0xC7, 0xC0, 0x2C, 0x04, 0x00, 0x00, 0x53, 0x48, 0xBB, 0x00, 0x20, 0x29, 0x2D,
                                0x65, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x04, 0x03, 0x5B};
    assert(JumpTarget(stubPage, 24, 0, nullptr) == 0x1652D29242Cull);
    assert(JumpTarget(stubPage, 23, 0, nullptr) == 0);
    const uint8_t plain[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57};
    assert(JumpTarget(plain, 6, 0x1000, read) == 0);
    assert(JumpTarget(nullptr, 6, 0x1000, read) == 0);

    // RVA -> file offset by sections.
    std::vector<FileSection> sections{{0x1000, 0x5000, 0x400, 0x5000}, {0x6000, 0x3000, 0x5400, 0x1000}};
    assert(FileOffset(sections, 0x1000) == 0x400);
    assert(FileOffset(sections, 0x1234) == 0x634);
    assert(FileOffset(sections, 0x6800) == 0x5C00);
    assert(FileOffset(sections, 0x7800) == SIZE_MAX); // beyond the bytes stored in the file
    assert(FileOffset(sections, 0x500) == SIZE_MAX);
    assert(FileOffset(sections, 0x20000) == SIZE_MAX);
    std::puts("signature_dump ok");
    return 0;
}

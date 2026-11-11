#pragma once
// Pieces of the signature dump that do not touch the game: hex text and "how many bytes from this
// function's start are needed before they appear only once in the game's code". Tested in
// tests/signature_dump.cpp; the dump itself is client/SignatureDump.cpp.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace SignatureDump {
// Prefix lengths tried, shortest first. The dump writes up to the last one.
inline constexpr size_t kLengths[] = {8, 12, 16, 24, 32, 48, 64, 96, 128};
inline constexpr size_t kMaxBytes = 128;
// Positions of the shortest prefix kept while narrowing; prefixes matching more are reported as not unique.
inline constexpr size_t kMaxCandidates = 200000;

inline std::string Hex(const uint8_t* bytes, size_t count)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    if (!bytes) return out;
    out.reserve(count * 3);
    for (size_t i = 0; i < count; ++i)
    {
        if (i) out += ' ';
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 15];
    }
    return out;
}

// Shortest length L from kLengths (at most patternSize) such that pattern[0, L) occurs nowhere in text
// except at self, the function's own place (0 = none of them: the bytes repeat elsewhere). The function's own
// place is skipped rather than required: once hooked, its first bytes in memory are a jump, not the pattern.
inline size_t UniquePrefix(const uint8_t* text, size_t size, size_t self, const uint8_t* pattern, size_t patternSize)
{
    const size_t first = kLengths[0];
    if (!text || !pattern || patternSize < first) return 0;
    std::vector<size_t> hits;
    const std::boyer_moore_horspool_searcher searcher(pattern, pattern + first);
    for (const uint8_t* at = text; at < text + size;)
    {
        const auto found = std::search(at, text + size, searcher);
        if (found == text + size) break;
        const auto offset = static_cast<size_t>(found - text);
        if (offset != self) hits.push_back(offset);
        if (hits.size() > kMaxCandidates) return 0;
        at = found + 1;
    }
    for (const size_t length : kLengths)
    {
        if (length > patternSize) return 0;
        hits.erase(std::remove_if(hits.begin(), hits.end(),
                                  [&](size_t at) { return at + length > size || !std::equal(pattern, pattern + length, text + at); }),
                   hits.end());
        if (hits.empty()) return length;
    }
    return 0;
}
// Where a jump at code goes (0 = not a jump this follows). at is the code's address; read(address) returns the
// 8 bytes stored there for an indirect jump (0 when unreadable). Covers what thunks and hooks start with:
// jmp rel32 (E9), jmp rel8 (EB), jmp [rip+disp32] (FF 25 / 48 FF 25) and mov rax, imm64; jmp rax (48 B8 .. FF E0).
inline uintptr_t JumpTarget(const uint8_t* code, size_t size, uintptr_t at, const std::function<uint64_t(uintptr_t)>& read)
{
    if (!code || size < 2) return 0;
    auto rel32 = [&](size_t i) {
        int32_t value;
        std::memcpy(&value, code + i, 4);
        return static_cast<int64_t>(value);
    };
    if (code[0] == 0xE9 && size >= 5) return static_cast<uintptr_t>(static_cast<int64_t>(at) + 5 + rel32(1));
    if (code[0] == 0xEB) return static_cast<uintptr_t>(static_cast<int64_t>(at) + 2 + static_cast<int8_t>(code[1]));
    size_t skip = code[0] == 0x48 ? 1 : 0;
    if (size >= skip + 6 && code[skip] == 0xFF && code[skip + 1] == 0x25)
    {
        const auto slot = static_cast<uintptr_t>(static_cast<int64_t>(at) + static_cast<int64_t>(skip) + 6 + rel32(skip + 2));
        return read ? static_cast<uintptr_t>(read(slot)) : 0;
    }
    auto imm64 = [&](size_t i) {
        uint64_t value;
        std::memcpy(&value, code + i, 8);
        return static_cast<uintptr_t>(value);
    };
    // mov rax, imm64; jmp rax | mov rax, imm64; push rax; ret
    if (size >= 12 && code[0] == 0x48 && code[1] == 0xB8 && code[10] == 0xFF && code[11] == 0xE0) return imm64(2);
    if (size >= 12 && code[0] == 0x48 && code[1] == 0xB8 && code[10] == 0x50 && code[11] == 0xC3) return imm64(2);
    // mov r10 / r11, imm64; jmp r10 / r11
    if (size >= 13 && code[0] == 0x49 && (code[1] == 0xBA || code[1] == 0xBB) && code[10] == 0x41 && code[11] == 0xFF &&
        code[12] == (code[1] == 0xBA ? 0xE2 : 0xE3))
        return imm64(2);
    // PreLoader's stub page (2026-11-04 v4 dump): push rax; mov rax, imm32; push rbx; mov rbx, imm64;
    // lea rax, [rbx+rax]; pop rbx; ... = goes to imm64 + imm32.
    if (size >= 24 && code[0] == 0x50 && code[1] == 0x48 && code[2] == 0xC7 && code[3] == 0xC0 && code[8] == 0x53 && code[9] == 0x48 &&
        code[10] == 0xBB && code[19] == 0x48 && code[20] == 0x8D && code[21] == 0x04 && code[22] == 0x03 && code[23] == 0x5B)
    {
        int32_t offset;
        std::memcpy(&offset, code + 4, 4);
        return static_cast<uintptr_t>(static_cast<int64_t>(imm64(11)) + offset);
    }
    // push imm32; mov dword [rsp+4], imm32; ret
    if (size >= 14 && code[0] == 0x68 && code[5] == 0xC7 && code[6] == 0x44 && code[7] == 0x24 && code[8] == 0x04 && code[13] == 0xC3)
    {
        uint32_t low, high;
        std::memcpy(&low, code + 1, 4);
        std::memcpy(&high, code + 9, 4);
        return static_cast<uintptr_t>((static_cast<uint64_t>(high) << 32) | low);
    }
    return 0;
}

struct FileSection
{
    uint32_t rva = 0, virtualSize = 0, fileOffset = 0, fileSize = 0;
};

// File offset of rva in an executable laid out by sections (SIZE_MAX = not stored in the file).
inline size_t FileOffset(const std::vector<FileSection>& sections, uint64_t rva)
{
    for (const auto& s : sections)
        if (rva >= s.rva && rva < static_cast<uint64_t>(s.rva) + std::max(s.virtualSize, s.fileSize))
        {
            const uint64_t inside = rva - s.rva;
            if (inside >= s.fileSize) return SIZE_MAX;
            return static_cast<size_t>(s.fileOffset + inside);
        }
    return SIZE_MAX;
}
} // namespace SignatureDump

namespace MyClientGame {
// Copies the first bytes of every listed game function (call from ll_mod_load, before any hook patches them).
// Does nothing when this game build's file was already written.
void SnapshotSignatures(const std::string& gameVersion);
// Resolves again the names not found at load, before MyClient's hooks replace the stubs' targets (call first
// thing in ll_mod_enable).
void ResolveSignaturesBeforeHooks();
// Starts the dump on a background thread once per process (it waits 60 s, then runs at low priority and
// skips the work when this game build's file already exists). log is called from that thread.
void StartSignatureDump(std::string gameVersion, std::function<void(const std::string&)> log);
// Once per game build, with %LOCALAPPDATA%\MyClient\dumps present: the real address of every game function
// MyClient uses, found by hooking each one through LeviLamina for a moment (call at the start of ll_mod_enable,
// before MyClient's hooks). Writes dumps\addresses-<game>-<exe timestamp>.txt.
void FindRealAddresses(const std::string& gameVersion, const std::function<void(const std::string&)>& log);
} // namespace MyClientGame

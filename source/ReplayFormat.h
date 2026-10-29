#pragma once
// Replay recording file (.mcrec): every packet the game receives from the server, with a timestamp.
//   Header (48 bytes): magic "MCREC001", version, reserved, start time (unix ms), game version text.
//   Then records: u32 size, u32 delta (ms since the previous record), `size` bytes of one raw game packet
//   (packet id varint + body, already decrypted/decompressed/unbatched by the game).
//   Version 3: bit 31 of the size marks a packet the client SENT (your own movement, actions); the
//   rest are packets received from the server. The client's Login packet is never stored (sign-in tokens).
//   Version 2: a record with size 0 marks where the player pressed record. Packets before it come from the
//   buffer kept since joining (the world data the server only sends at the start); playback starts at the mark.
// Playback: ReplayServer.h (a local server the game joins), started by the HUD through ReplayPlayer.h.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

namespace MyClientReplay {
constexpr uint32_t Version = 3;
constexpr uint32_t SentFlag = 0x80000000u;   // in a record's size: sent by the client
constexpr uint64_t MaxBytes = 1ull << 30;   // stop recording at 1 GiB
constexpr uint32_t MaxPacket = 32u << 20;   // a single packet above this is skipped (never happens in practice)

// `durationSec` stays 0 until the recording is closed normally (a crash leaves it 0 = unknown).
struct Header { char magic[8]; uint32_t version, durationSec; uint64_t startUnixMs; char game[16]; };
static_assert(sizeof(Header) == 40 && std::is_trivially_copyable<Header>::value);

// Packet id = low 10 bits of the leading varint (one or two bytes for every id the game uses).
inline uint32_t PacketId(const char* data, size_t size) {
    if (size == 0) return 0xFFFFFFFFu;
    const auto b0 = static_cast<uint8_t>(data[0]);
    if ((b0 & 0x80) && size > 1) return ((b0 & 0x7Fu) | ((static_cast<uint8_t>(data[1]) & 0x7Fu) << 7)) & 0x3FFu;
    return b0 & 0x3FFu;
}
namespace Ids {
constexpr uint32_t Login = 1, StartGame = 11, NetworkSettings = 143;
}

class Writer {
    FILE* file_ = nullptr;
    uint64_t bytes_ = 0, lastMs_ = 0, startMs_ = 0;
    uint32_t packets_ = 0;
public:
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() { Close(); }
    bool IsOpen() const { return file_ != nullptr; }
    uint64_t Bytes() const { return bytes_; }
    uint32_t Packets() const { return packets_; }

#ifdef _WIN32
    bool Open(const wchar_t* path, uint64_t unixMs, uint64_t nowMs, const char* game) {
        Close();
        file_ = _wfopen(path, L"wb");
#else
    bool Open(const char* path, uint64_t unixMs, uint64_t nowMs, const char* game) {
        Close();
        file_ = std::fopen(path, "wb");
#endif
        if (!file_) return false;
        std::setvbuf(file_, nullptr, _IOFBF, 1 << 20);
        Header h{};
        std::memcpy(h.magic, "MCREC001", 8);
        h.version = Version;
        h.startUnixMs = unixMs;
        if (game) std::strncpy(h.game, game, sizeof(h.game) - 1);
        if (std::fwrite(&h, sizeof h, 1, file_) != 1) { Close(); return false; }
        bytes_ = sizeof h; packets_ = 0; lastMs_ = nowMs; startMs_ = nowMs;
        return true;
    }
    // Returns false when the file is closed, full or a write failed (the caller then stops recording).
    // `sent` = a packet the client sent to the server.
    bool Append(const char* data, size_t size, uint64_t nowMs, bool sent = false) {
        if (!file_ || size == 0 || size > MaxPacket) return file_ != nullptr;
        if (bytes_ + size + 8 > MaxBytes) return false;
        const uint32_t sz = static_cast<uint32_t>(size) | (sent ? SentFlag : 0u);
        const uint64_t d = nowMs >= lastMs_ ? nowMs - lastMs_ : 0;
        const uint32_t delta = d > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(d);
        if (std::fwrite(&sz, 4, 1, file_) != 1 || std::fwrite(&delta, 4, 1, file_) != 1 ||
            std::fwrite(data, 1, size, file_) != size) return false;
        bytes_ += size + 8; packets_++; lastMs_ = nowMs;
        return true;
    }
    // "Recording starts here" (a size-0 record). False when the write failed.
    bool Mark(uint64_t nowMs) {
        if (!file_) return false;
        const uint32_t sz = 0;
        const uint64_t d = nowMs >= lastMs_ ? nowMs - lastMs_ : 0;
        const uint32_t delta = d > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(d);
        if (std::fwrite(&sz, 4, 1, file_) != 1 || std::fwrite(&delta, 4, 1, file_) != 1) return false;
        bytes_ += 8; lastMs_ = nowMs;
        return true;
    }
    void Flush() { if (file_) std::fflush(file_); }
    void Close() {
        if (!file_) return;
        const uint32_t sec = lastMs_ > startMs_ ? static_cast<uint32_t>((lastMs_ - startMs_) / 1000) : 0;
        if (std::fseek(file_, static_cast<long>(offsetof(Header, durationSec)), SEEK_SET) == 0)
            std::fwrite(&sec, 4, 1, file_);
        std::fclose(file_); file_ = nullptr;
    }
};
}

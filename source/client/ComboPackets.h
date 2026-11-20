#pragma once
// Combo Counter: the few Bedrock packets it reads (docs: source/docs/superpowers/specs/2026-11-17-combo-counter-design.md §2).
// Input is one finished packet as BatchedNetworkPeer hands it over (header varint, then the body), the same bytes the
// Replay hooks already see. Windows-independent and allocation-free, so it is tested on Linux.
#include <cstddef>
#include <cstdint>

namespace MyClientCombo {
namespace PacketIds {
constexpr uint32_t StartGame = 11, ActorEvent = 27, InventoryTransaction = 30, SetActorMotion = 40;
}
constexpr uint32_t ActorEventHurt = 2;
constexpr uint32_t TransactionUseItemOnEntity = 3, UseItemOnEntityAttack = 1;

class Reader {
    const uint8_t* p_; size_t n_, at_ = 0; bool ok_ = true;
public:
    Reader(const void* data, size_t size) : p_(static_cast<const uint8_t*>(data)), n_(size) {}
    bool Ok() const { return ok_; }
    uint32_t U8() { if (!ok_ || at_ >= n_) { ok_ = false; return 0; } return p_[at_++]; }
    uint64_t VarU64() {
        uint64_t v = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            const uint32_t c = U8();
            if (!ok_) return 0;
            v |= static_cast<uint64_t>(c & 0x7F) << shift;
            if (!(c & 0x80)) return v;
        }
        ok_ = false; return 0;
    }
    uint32_t VarU32() { const uint64_t v = VarU64(); if (v > 0xFFFFFFFFull) ok_ = false; return static_cast<uint32_t>(v); }
    int32_t VarI32() { const uint32_t v = VarU32(); return static_cast<int32_t>((v >> 1) ^ (~(v & 1) + 1)); }
    void Skip(size_t k) { if (!ok_ || n_ - at_ < k) { ok_ = false; return; } at_ += k; }
};

// The packet id (low 10 bits of the header varint); the reader is left at the body.
inline uint32_t ReadHeader(Reader& in) { return in.VarU32() & 0x3FFu; }

struct Packet {
    enum class Kind : uint8_t { Other, Start, Hurt, Motion, Attack } kind = Kind::Other;
    uint64_t runtimeId = 0;   // Start: the local player; Hurt / Motion: who it is about; Attack: the target
    int32_t data = 0;         // Hurt: ActorEventPacket's data field (logged to tell the damage causes apart, §3a)
};

// Received packets: StartGame, ActorEvent (Hurt only), SetActorMotion.
inline Packet ParseReceived(const void* bytes, size_t size)
{
    Packet out;
    Reader in(bytes, size);
    const uint32_t id = ReadHeader(in);
    if (!in.Ok()) return out;
    if (id == PacketIds::StartGame) {
        in.VarU64();                       // actor unique id (zigzag)
        const uint64_t runtime = in.VarU64();
        if (in.Ok()) { out.kind = Packet::Kind::Start; out.runtimeId = runtime; }
    } else if (id == PacketIds::ActorEvent) {
        const uint64_t runtime = in.VarU64();
        const uint32_t event = in.U8();
        const int32_t data = in.VarI32();
        if (in.Ok() && event == ActorEventHurt) { out.kind = Packet::Kind::Hurt; out.runtimeId = runtime; out.data = data; }
    } else if (id == PacketIds::SetActorMotion) {
        const uint64_t runtime = in.VarU64();
        if (in.Ok()) { out.kind = Packet::Kind::Motion; out.runtimeId = runtime; }
    }
    return out;
}

// Sent packets: InventoryTransaction / UseItemOnEntity / Attack (the attack the server judges).
// With server-authoritative inventories (vanilla servers and Hive) the action list is empty; a transaction that
// carries actions is not read (kind stays Other) rather than parsing item stacks.
inline Packet ParseSent(const void* bytes, size_t size)
{
    Packet out;
    Reader in(bytes, size);
    if (ReadHeader(in) != PacketIds::InventoryTransaction || !in.Ok()) return out;
    const int32_t legacyRequest = in.VarI32();
    if (legacyRequest != 0) {
        const uint32_t slots = in.VarU32();
        if (slots > 64) return out;
        for (uint32_t i = 0; i < slots && in.Ok(); ++i) { in.U8(); in.Skip(in.VarU32()); }
    }
    const uint32_t type = in.VarU32();
    const uint32_t actions = in.VarU32();
    if (!in.Ok() || type != TransactionUseItemOnEntity || actions != 0) return out;
    const uint64_t target = in.VarU64();
    const uint32_t action = in.VarU32();
    if (in.Ok() && action == UseItemOnEntityAttack) { out.kind = Packet::Kind::Attack; out.runtimeId = target; }
    return out;
}
}

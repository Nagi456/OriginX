// cd source && g++ -std=c++17 -Wall -Wextra -Werror -I. tests/combo_packets.cpp -o /tmp/combo_packets && /tmp/combo_packets
#include "client/ComboPackets.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace MyClientCombo;

static void VarU(std::string& s, uint64_t v) { while (v >= 0x80) { s += char((v & 0x7F) | 0x80); v >>= 7; } s += char(v); }
static void VarI(std::string& s, int32_t v) { VarU(s, (uint32_t(v) << 1) ^ uint32_t(v >> 31)); }
static std::string Head(uint32_t id) { std::string s; VarU(s, id); return s; }

int main()
{
    {   // StartGame: unique id (zigzag), runtime id, then the rest (ignored)
        std::string p = Head(PacketIds::StartGame); VarU(p, 2 * 12345); VarU(p, 300); p += "rest";
        const Packet r = ParseReceived(p.data(), p.size());
        assert(r.kind == Packet::Kind::Start && r.runtimeId == 300);
    }
    {   // ActorEvent Hurt with data -7, and a 2-byte header (sub-client bits set)
        std::string p; VarU(p, PacketIds::ActorEvent | (1u << 10)); VarU(p, 987654321ull); p += char(ActorEventHurt); VarI(p, -7);
        const Packet r = ParseReceived(p.data(), p.size());
        assert(r.kind == Packet::Kind::Hurt && r.runtimeId == 987654321ull && r.data == -7);
    }
    {   // ActorEvent that is not Hurt
        std::string p = Head(PacketIds::ActorEvent); VarU(p, 5); p += char(3); VarI(p, 0);
        assert(ParseReceived(p.data(), p.size()).kind == Packet::Kind::Other);
    }
    {   // cut short
        std::string p = Head(PacketIds::ActorEvent); VarU(p, 5);
        assert(ParseReceived(p.data(), p.size()).kind == Packet::Kind::Other);
        assert(ParseReceived(p.data(), 0).kind == Packet::Kind::Other);
    }
    {   // SetActorMotion
        std::string p = Head(PacketIds::SetActorMotion); VarU(p, 77); p += std::string(12, '\0');
        const Packet r = ParseReceived(p.data(), p.size());
        assert(r.kind == Packet::Kind::Motion && r.runtimeId == 77);
    }
    {   // attack: no legacy request, UseItemOnEntity, no actions, target, Attack
        std::string p = Head(PacketIds::InventoryTransaction); VarI(p, 0); VarU(p, TransactionUseItemOnEntity); VarU(p, 0);
        VarU(p, 4242); VarU(p, UseItemOnEntityAttack); p += "item...";
        const Packet r = ParseSent(p.data(), p.size());
        assert(r.kind == Packet::Kind::Attack && r.runtimeId == 4242);
    }
    {   // interact, not attack
        std::string p = Head(PacketIds::InventoryTransaction); VarI(p, 0); VarU(p, TransactionUseItemOnEntity); VarU(p, 0);
        VarU(p, 4242); VarU(p, 0);
        assert(ParseSent(p.data(), p.size()).kind == Packet::Kind::Other);
    }
    {   // legacy request with one slot entry, then attack
        std::string p = Head(PacketIds::InventoryTransaction); VarI(p, -3); VarU(p, 1); p += char(12); VarU(p, 2); p += "ab";
        VarU(p, TransactionUseItemOnEntity); VarU(p, 0); VarU(p, 9); VarU(p, UseItemOnEntityAttack);
        const Packet r = ParseSent(p.data(), p.size());
        assert(r.kind == Packet::Kind::Attack && r.runtimeId == 9);
    }
    {   // with actions: not read
        std::string p = Head(PacketIds::InventoryTransaction); VarI(p, 0); VarU(p, TransactionUseItemOnEntity); VarU(p, 1); p += "xxxx";
        assert(ParseSent(p.data(), p.size()).kind == Packet::Kind::Other);
    }
    {   // other transaction type / other packet
        std::string p = Head(PacketIds::InventoryTransaction); VarI(p, 0); VarU(p, 2); VarU(p, 0); VarU(p, 1); VarU(p, 1);
        assert(ParseSent(p.data(), p.size()).kind == Packet::Kind::Other);
        std::string q = Head(PacketIds::ActorEvent); VarU(q, 1); q += char(2); VarI(q, 0);
        assert(ParseSent(q.data(), q.size()).kind == Packet::Kind::Other);
    }
    std::printf("combo_packets: all passed\n");
}

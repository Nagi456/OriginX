// Replay playback server: a fake game client joins it and receives a recording.
#include "../ReplayServer.h"
#include <cassert>
#include <cmath>
using namespace MyClientReplayServer;

namespace {
std::string Packet(uint32_t id, const std::string& body) { return Header(id) + body; }

// The client side: what a Bedrock client sends, and everything it gets back.
struct Client {
    Session& s;
    uint64_t now = 1000;
    uint32_t seq = 0, reliable = 0, order = 0;
    std::vector<std::string> messages;            // connected messages received (reassembled)
    std::map<uint16_t, std::vector<std::string>> splits;
    std::set<uint32_t> gotReliable;
    bool dropNext = false;                        // lose the next data datagram (no ACK) once

    void Datagram(const std::string& d) { s.OnDatagram(d.data(), d.size(), 0x7f000001, 50000, now); Collect(); }
    void Send(const std::string& message) {
        Frame f; f.reliability = 3; f.reliableIndex = reliable++; f.orderIndex = order++; f.body = message;
        Out o; o.U8(0x84); o.U24le(seq++); PutFrame(o, f);
        Datagram(o.b);
    }
    void SendSplit(const std::string& message, size_t piece) {
        const uint32_t count = static_cast<uint32_t>((message.size() + piece - 1) / piece);
        const uint32_t ord = order++;
        for (uint32_t i = count; i-- > 0;) {   // out of order on purpose
            Frame f; f.reliability = 3; f.split = true; f.splitCount = count; f.splitId = 9; f.splitIndex = i;
            f.reliableIndex = reliable++; f.orderIndex = ord; f.body = message.substr(i * piece, piece);
            Out o; o.U8(0x84); o.U24le(seq++); PutFrame(o, f);
            Datagram(o.b);
        }
    }
    void Pump(uint64_t ms) { now += ms; s.Pump(now); Collect(); }
    void Collect() {
        std::vector<uint32_t> acks;
        for (const auto& d : s.out) {
            In in(d);
            const uint32_t id = in.U8();
            if (!(id & 0x80) || (id & 0x40)) { messages.push_back(d); continue; }   // offline replies / our ACKs
            const uint32_t ds = in.U24le();
            if (dropNext) { dropNext = false; continue; }
            acks.push_back(ds);
            std::vector<Frame> frames;
            assert(ReadFrames(in, frames));
            for (auto& f : frames) {
                if (IsReliable(f.reliability) && !gotReliable.insert(f.reliableIndex).second) continue;
                if (!f.split) { messages.push_back(f.body); continue; }
                auto& parts = splits[f.splitId];
                parts.resize(f.splitCount);
                parts[f.splitIndex] = f.body;
                bool all = true; for (auto& p : parts) all = all && !p.empty();
                if (all) { std::string m; for (auto& p : parts) m += p; messages.push_back(m); splits.erase(f.splitId); }
            }
        }
        s.out.clear();
        if (!acks.empty()) { const auto a = AckPacket(0xC0, acks); s.OnDatagram(a.data(), a.size(), 0x7f000001, 50000, now); }
    }
    // Game packets received so far (in order), `prefix` after NetworkSettings.
    std::vector<std::string> Games() {
        std::vector<std::string> all;
        for (const auto& m : messages) {
            if (m.empty() || static_cast<uint8_t>(m[0]) != Msg::Game) continue;
            std::vector<std::string> packets;
            if (!ReadBatch(m, false, packets)) { packets.clear(); assert(ReadBatch(m, true, packets)); }
            for (auto& p : packets) all.push_back(p);
        }
        return all;
    }
};
}

int main()
{
    // Varints, ACK ranges and frames survive a round trip.
    {
        Out o; o.VarU64(300); o.VarU64(193);
        In in(o.b); assert(in.VarU64() == 300 && in.VarU64() == 193 && in.Left() == 0);
        const std::string ack = AckPacket(0xC0, {5, 3, 4, 9, 4});
        In a(ack); a.U8(); const auto seqs = ReadAck(a);
        assert((seqs == std::vector<uint32_t>{3, 4, 5, 9}));
        Frame f; f.reliability = 3; f.reliableIndex = 7; f.orderIndex = 2; f.split = true; f.splitCount = 3; f.splitId = 4; f.splitIndex = 1; f.body = "hello";
        Out fo; PutFrame(fo, f); In fi(fo.b); std::vector<Frame> back;
        assert(ReadFrames(fi, back) && back.size() == 1 && back[0].body == "hello" && back[0].reliableIndex == 7 && back[0].splitIndex == 1 && back[0].splitCount == 3);
    }

    // A recording: setup packets, StartGame (runtime id 7), a chunk, the mark, then movement and a chat line.
    const char* path = "/tmp/myclient-replay-server-test.mcrec";
    const std::string settings = Packet(Id::NetworkSettings, std::string("\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00", 10));
    const std::string info = Packet(Id::PacksInfo, "INFO"), stack = Packet(Id::PackStack, "STACK");
    Out sg; sg.VarU64(14); sg.VarU64(7); sg.Raw("rest-of-startgame");
    const std::string startGame = Packet(Id::StartGame, sg.b);
    const std::string chunk = Packet(58, std::string(20000, 'c'));
    Out pai; pai.F32le(10.f); pai.F32le(90.f); pai.F32le(1.5f); pai.F32le(65.62f); pai.F32le(-3.f); pai.F32le(0); pai.F32le(0); pai.F32le(91.f); pai.Raw("more");
    const std::string authInput = Packet(Id::PlayerAuthInput, pai.b);
    const std::string text = Packet(9, "chat");
    {
        MyClientReplay::Writer w;
        assert(w.Open(path, 1700000000000ull, 0, "26.51.1"));
        assert(w.Append(settings.data(), settings.size(), 0));
        assert(w.Append(PlayStatus(0).data(), PlayStatus(0).size(), 5));
        assert(w.Append(info.data(), info.size(), 10));
        assert(w.Append(stack.data(), stack.size(), 20));
        assert(w.Append(startGame.data(), startGame.size(), 100));
        assert(w.Append(chunk.data(), chunk.size(), 5000));
        assert(w.Append(PlayStatus(3).data(), PlayStatus(3).size(), 6000));
        assert(w.Mark(60000));
        assert(w.Append(authInput.data(), authInput.size(), 60500, true));
        assert(w.Append(text.data(), text.size(), 62000));
        w.Close();
    }

    Session s;
    s.port = 19150;
    assert(s.Open(std::fopen(path, "rb")));
    assert(s.Rec().hasMark && s.Rec().markMs == 60000 && s.Rec().LengthMs() == 2000 && s.Rec().runtimeId == 7);
    assert(s.Rec().packsInfo == info && s.Rec().packStack == stack && s.Rec().networkSettings == settings);

    Client c{s, 1000, 0, 0, 0, {}, {}, {}, false};
    // Offline handshake.
    { Out o; o.U8(Msg::UnconnectedPing); o.U64be(1); PutMagic(o); o.U64be(2); c.Datagram(o.b); }
    assert(c.messages.size() == 1 && static_cast<uint8_t>(c.messages[0][0]) == Msg::UnconnectedPong && c.messages[0].find("MyClient Replay") != std::string::npos);
    { Out o; o.U8(Msg::OpenRequest1); PutMagic(o); o.U8(11); o.Raw(std::string(1300, '\0')); c.Datagram(o.b); }
    assert(static_cast<uint8_t>(c.messages.back()[0]) == Msg::OpenReply1);
    { In r(c.messages.back()); r.U8(); r.Skip(16); r.U64be(); r.U8(); assert(r.U16be() == 1300 + 1 + 16 + 1 + 28); }
    { Out o; o.U8(Msg::OpenRequest2); PutMagic(o); PutAddress(o, 0x7f000001, 19150); o.U16be(1200); o.U64be(2); c.Datagram(o.b); }
    assert(static_cast<uint8_t>(c.messages.back()[0]) == Msg::OpenReply2 && s.GetState() == State::Connecting);
    // Connected handshake.
    { Out o; o.U8(Msg::ConnectionRequest); o.U64be(2); o.U64be(1234); o.U8(0); c.Send(o.b); }
    c.Pump(1);
    assert(static_cast<uint8_t>(c.messages.back()[0]) == Msg::ConnectionAccepted);
    { Out o; o.U8(Msg::NewIncoming); c.Send(o.b); }
    assert(s.GetState() == State::Settings);
    { Out o; o.U8(Msg::ConnectedPing); o.U64be(77); c.Send(o.b); c.Pump(1); }
    assert(static_cast<uint8_t>(c.messages.back()[0]) == Msg::ConnectedPong);
    // Game handshake: settings, login (split, compressed = unreadable), packs.
    c.Send(Batch({Packet(Id::RequestNetworkSettings, std::string("\x00\x00\x03\x20", 4))}, false));
    c.Pump(1);
    assert(s.GetState() == State::Login && c.Games().back() == settings);
    std::string login = "\xfe\x00" + std::string(5000, 'L');   // zlib-compressed batch
    c.SendSplit(login, 1000);
    c.Pump(1);
    assert(s.GetState() == State::Packs);
    { auto g = c.Games(); assert(g.size() == 3 && g[1] == PlayStatus(0) && g[2] == info); }
    c.Send(Batch({Packet(Id::PackResponse, std::string("\x03\x00\x00", 3))}, true));
    c.Pump(1);
    assert(s.GetState() == State::Stack && c.Games().back() == stack);
    c.Send(Batch({Packet(Id::PackResponse, std::string("\x04\x00\x00", 3))}, true));
    c.dropNext = true;   // one datagram of the burst gets lost: it must come again
    c.Pump(1);
    assert(s.GetState() == State::Playing);
    c.Pump(450);   // past the 400 ms resend time, before the movement at +500 ms
    {
        // Before the mark: everything at once, without the recorded setup packets.
        auto g = c.Games();
        assert(g.size() == 7 && g[4] == startGame && g[5] == chunk && g[6] == PlayStatus(3));
    }
    assert(s.PositionMs() == 450);
    // After the mark: the movement at +500 ms, the chat line at +2000 ms.
    c.Pump(500);
    {
        auto g = c.Games();
        assert(g.size() == 8 && GameId(g[7]) == Id::MovePlayer);
        In m = Body(g[7]);
        assert(m.VarU64() == 7);
        const float x = m.F32le(), y = m.F32le(), z = m.F32le(), pitch = m.F32le(), yaw = m.F32le(), head = m.F32le();
        assert(x == 1.5f && std::fabs(y - 65.62f) < 1e-4 && z == -3.f && pitch == 10.f && yaw == 90.f && head == 91.f);
        assert(m.U8() == 2);
    }
    s.SetPaused(true);
    c.Pump(5000);
    assert(c.Games().size() == 8);   // paused: nothing more
    s.SetPaused(false);
    s.SetSpeed(2.f);
    c.Pump(800);                     // 800 ms at x2 passes the chat line
    assert(c.Games().size() == 9 && c.Games()[8] == text);
    c.Pump(10);
    assert(s.GetState() == State::Finished && s.PositionMs() == 2000);
    s.Stop(c.now);
    assert(s.GetState() == State::Closed);

    // Files the player refuses.
    {
        FILE* f = std::fopen(path, "wb"); std::fputs("not a replay", f); std::fclose(f);
        Session bad; assert(!bad.Open(std::fopen(path, "rb")) && !bad.Rec().error.empty());
    }
    std::remove(path);
    std::puts("replay server ok");
}

#pragma once
// Replay playback: a tiny Bedrock server on 127.0.0.1 that plays a .mcrec recording back to the game.
// The HUD starts it and opens minecraft://connect/?serverUrl=127.0.0.1&serverPort=<port>; the game joins it
// like any server and receives the recorded server packets with their original timing.
//
//   RakNet: offline handshake (ping/pong, open connection 1/2), then datagrams of frames. Everything we send is
//           reliable ordered on channel 0 and resent until acknowledged; the client's frames are acknowledged,
//           de-duplicated and reassembled from splits.
//   Game:   RequestNetworkSettings -> the recorded NetworkSettings (no encryption is ever started), Login ->
//           PlayStatus(LoginSuccess) + the recorded ResourcePacksInfo, pack answer -> the recorded
//           ResourcePackStack, pack answer -> the recording (from the start; before the record mark it is sent
//           at once, after it with the recorded delays). Our batches are never compressed (prefix 0xFF).
//   Player: the client's own movement (recorded PlayerAuthInput) becomes a MovePlayer teleport of the local
//           player, so the replay is seen from the recorded player's eyes.
//
// No sockets here: Session takes datagrams in and hands datagrams out, so the whole exchange is unit-tested on the
// host (tests/replay_server.cpp). ReplayPlayer.h runs it on a Windows UDP socket.
#include "ReplayFormat.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace MyClientReplayServer {

// ---- Byte buffers ----
struct Out {
    std::string b;
    void U8(uint32_t v) { b.push_back(static_cast<char>(v & 0xFF)); }
    void U16be(uint32_t v) { U8(v >> 8); U8(v); }
    void U16le(uint32_t v) { U8(v); U8(v >> 8); }
    void U24le(uint32_t v) { U8(v); U8(v >> 8); U8(v >> 16); }
    void U32be(uint32_t v) { U8(v >> 24); U8(v >> 16); U8(v >> 8); U8(v); }
    void U32le(uint32_t v) { U8(v); U8(v >> 8); U8(v >> 16); U8(v >> 24); }
    void U64be(uint64_t v) { U32be(static_cast<uint32_t>(v >> 32)); U32be(static_cast<uint32_t>(v)); }
    void F32le(float f) { uint32_t v; std::memcpy(&v, &f, 4); U32le(v); }
    void VarU64(uint64_t v) { while (v >= 0x80) { U8(static_cast<uint32_t>(v) | 0x80); v >>= 7; } U8(static_cast<uint32_t>(v)); }
    void Raw(const std::string& s) { b += s; }
    void Raw(const char* p, size_t n) { b.append(p, n); }
};

struct In {
    const char* p; size_t n, at = 0; bool ok = true;
    In(const char* data, size_t size) : p(data), n(size) {}
    explicit In(const std::string& s) : p(s.data()), n(s.size()) {}
    size_t Left() const { return ok ? n - at : 0; }
    bool Need(size_t k) { if (!ok || n - at < k) ok = false; return ok; }
    uint32_t U8() { if (!Need(1)) return 0; return static_cast<uint8_t>(p[at++]); }
    uint32_t U16be() { uint32_t a = U8(); return (a << 8) | U8(); }
    uint32_t U16le() { uint32_t a = U8(); return a | (U8() << 8); }
    uint32_t U24le() { uint32_t a = U8(), b = U8(); return a | (b << 8) | (U8() << 16); }
    uint32_t U32be() { uint32_t a = U16be(); return (a << 16) | U16be(); }
    uint32_t U32le() { uint32_t a = U16le(); return a | (U16le() << 16); }
    uint64_t U64be() { uint64_t a = U32be(); return (a << 32) | U32be(); }
    float F32le() { const uint32_t v = U32le(); float f; std::memcpy(&f, &v, 4); return f; }
    uint64_t VarU64() {
        uint64_t v = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            const uint32_t c = U8();
            if (!ok) return 0;
            v |= static_cast<uint64_t>(c & 0x7F) << shift;
            if (!(c & 0x80)) return v;
        }
        ok = false; return 0;
    }
    std::string Bytes(size_t k) { if (!Need(k)) return {}; std::string s(p + at, k); at += k; return s; }
    void Skip(size_t k) { if (Need(k)) at += k; }
};

// ---- RakNet ----
constexpr uint8_t kMagic[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};
namespace Msg {
constexpr uint8_t ConnectedPing = 0x00, UnconnectedPing = 0x01, UnconnectedPingOpen = 0x02, ConnectedPong = 0x03,
                  OpenRequest1 = 0x05, OpenReply1 = 0x06, OpenRequest2 = 0x07, OpenReply2 = 0x08,
                  ConnectionRequest = 0x09, ConnectionAccepted = 0x10, NewIncoming = 0x13, Disconnect = 0x15,
                  UnconnectedPong = 0x1c, Game = 0xfe;
}
inline void PutMagic(Out& o) { o.Raw(reinterpret_cast<const char*>(kMagic), 16); }
inline bool HasMagic(In& in) { const std::string m = in.Bytes(16); return in.ok && std::memcmp(m.data(), kMagic, 16) == 0; }
// IPv4 address: 4, the octets inverted, port big-endian. `ip` in host order (127.0.0.1 = 0x7f000001).
inline void PutAddress(Out& o, uint32_t ip, uint16_t port) {
    o.U8(4); o.U8(~(ip >> 24)); o.U8(~(ip >> 16)); o.U8(~(ip >> 8)); o.U8(~ip); o.U16be(port);
}
inline void SkipAddress(In& in) { in.U8() == 6 ? in.Skip(2 + 2 + 4 + 16 + 4) : in.Skip(4 + 2); }

inline bool IsReliable(uint8_t r) { return r == 2 || r == 3 || r == 4 || r == 6 || r == 7; }
inline bool IsSequenced(uint8_t r) { return r == 1 || r == 4; }
inline bool IsOrdered(uint8_t r) { return r == 1 || r == 3 || r == 4 || r == 7; }

struct Frame {
    uint8_t reliability = 0;
    bool split = false;
    uint32_t reliableIndex = 0, sequenceIndex = 0, orderIndex = 0;
    uint8_t orderChannel = 0;
    uint32_t splitCount = 0, splitIndex = 0;
    uint16_t splitId = 0;
    std::string body;
};

inline void PutFrame(Out& o, const Frame& f) {
    o.U8(static_cast<uint32_t>(f.reliability << 5) | (f.split ? 0x10u : 0u));
    o.U16be(static_cast<uint32_t>(f.body.size() * 8));
    if (IsReliable(f.reliability)) o.U24le(f.reliableIndex);
    if (IsSequenced(f.reliability)) o.U24le(f.sequenceIndex);
    if (IsOrdered(f.reliability)) { o.U24le(f.orderIndex); o.U8(f.orderChannel); }
    if (f.split) { o.U32be(f.splitCount); o.U16be(f.splitId); o.U32be(f.splitIndex); }
    o.Raw(f.body);
}
inline size_t FrameSize(const Frame& f) { return 3 + 3 + 4 + (f.split ? 10 : 0) + f.body.size(); }

// Frames of one data datagram (after the flags byte and the sequence number). False on a malformed one.
inline bool ReadFrames(In& in, std::vector<Frame>& out) {
    while (in.Left() > 0) {
        Frame f;
        const uint32_t flags = in.U8();
        f.reliability = static_cast<uint8_t>(flags >> 5);
        f.split = (flags & 0x10) != 0;
        const size_t bytes = (in.U16be() + 7) / 8;
        if (IsReliable(f.reliability)) f.reliableIndex = in.U24le();
        if (IsSequenced(f.reliability)) f.sequenceIndex = in.U24le();
        if (IsOrdered(f.reliability)) { f.orderIndex = in.U24le(); f.orderChannel = static_cast<uint8_t>(in.U8()); }
        if (f.split) { f.splitCount = in.U32be(); f.splitId = static_cast<uint16_t>(in.U16be()); f.splitIndex = in.U32be(); }
        f.body = in.Bytes(bytes);
        if (!in.ok) return false;
        out.push_back(std::move(f));
    }
    return true;
}

// ACK / NAK body: ranges of datagram sequence numbers.
inline std::string AckPacket(uint8_t id, std::vector<uint32_t> seqs) {
    std::sort(seqs.begin(), seqs.end());
    seqs.erase(std::unique(seqs.begin(), seqs.end()), seqs.end());
    Out records; uint32_t count = 0;
    for (size_t i = 0; i < seqs.size();) {
        size_t j = i;
        while (j + 1 < seqs.size() && seqs[j + 1] == seqs[j] + 1) ++j;
        if (i == j) { records.U8(1); records.U24le(seqs[i]); }
        else { records.U8(0); records.U24le(seqs[i]); records.U24le(seqs[j]); }
        ++count; i = j + 1;
    }
    Out o; o.U8(id); o.U16be(count); o.Raw(records.b);
    return o.b;
}
inline std::vector<uint32_t> ReadAck(In& in) {
    std::vector<uint32_t> seqs;
    const uint32_t count = in.U16be();
    for (uint32_t i = 0; i < count && in.ok; ++i) {
        const bool single = in.U8() != 0;
        const uint32_t a = in.U24le(), b = single ? a : in.U24le();
        if (!in.ok || b < a || b - a > 4096) break;
        for (uint32_t s = a; s <= b; ++s) seqs.push_back(s);
    }
    return seqs;
}

// ---- Game packets ----
namespace Id {
constexpr uint32_t Login = 1, PlayStatus = 2, ServerToClientHandshake = 3, Disconnect = 5, PacksInfo = 6, PackStack = 7,
                   PackResponse = 8, StartGame = 11, MovePlayer = 19, PackDataInfo = 82, PackChunkData = 83, Transfer = 85,
                   NetworkSettings = 143, PlayerAuthInput = 144, RequestNetworkSettings = 193;
}
namespace PackStatus { constexpr uint32_t SendPacks = 2, HaveAllPacks = 3, Completed = 4; }

inline uint32_t GameId(const std::string& packet) { return MyClientReplay::PacketId(packet.data(), packet.size()); }
// The packet body after its header varint.
inline In Body(const std::string& packet) { In in(packet); in.VarU64(); return in; }

inline std::string Header(uint32_t id) { Out o; o.VarU64(id); return o.b; }
inline std::string PlayStatus(uint32_t status) { Out o; o.Raw(Header(Id::PlayStatus)); o.U32be(status); return o.b; }
// Used only when the recording has no NetworkSettings: the client then compresses nothing below 64 KiB.
inline std::string DefaultNetworkSettings() {
    Out o; o.Raw(Header(Id::NetworkSettings)); o.U16le(0xFFFF); o.U16le(0); o.U8(0); o.U8(0); o.F32le(0.f); return o.b;
}
// Local player teleported to the recorded eye position (MovePlayer mode 2 = teleport).
inline std::string MovePlayer(uint64_t runtimeId, float x, float y, float z, float pitch, float yaw, float headYaw, uint64_t tick) {
    Out o; o.Raw(Header(Id::MovePlayer));
    o.VarU64(runtimeId); o.F32le(x); o.F32le(y); o.F32le(z); o.F32le(pitch); o.F32le(yaw); o.F32le(headYaw);
    o.U8(2); o.U8(1); o.VarU64(0); o.U32le(0); o.U32le(0); o.VarU64(tick);
    return o.b;
}
struct Pose { float pitch, yaw, x, y, z, headYaw; };
// PlayerAuthInput starts with pitch, yaw, position, move vector, head yaw (stable since 1.16).
inline bool ReadPose(const std::string& packet, Pose& p) {
    In in = Body(packet);
    p.pitch = in.F32le(); p.yaw = in.F32le(); p.x = in.F32le(); p.y = in.F32le(); p.z = in.F32le();
    in.F32le(); in.F32le(); p.headYaw = in.F32le();
    return in.ok && p.x == p.x && p.y == p.y && p.z == p.z && p.pitch == p.pitch && p.yaw == p.yaw && p.headYaw == p.headYaw;
}
// StartGame: entity unique id (zigzag varint), then the runtime id of the local player.
inline bool ReadRuntimeId(const std::string& packet, uint64_t& id) {
    In in = Body(packet); in.VarU64(); id = in.VarU64(); return in.ok;
}

// A batch (0xFE game message): each packet behind its varint length. `prefix`: compression is on (0xFF = none).
inline std::string Batch(const std::vector<std::string>& packets, bool prefix) {
    Out o; o.U8(Msg::Game); if (prefix) o.U8(0xFF);
    for (const auto& p : packets) { o.VarU64(p.size()); o.Raw(p); }
    return o.b;
}
// Packets of a client batch. False when it is compressed (we never read those; the handshake then goes on time).
inline bool ReadBatch(const std::string& message, bool prefix, std::vector<std::string>& packets) {
    In in(message);
    if (in.U8() != Msg::Game) return false;
    if (prefix && in.U8() != 0xFF) return false;
    while (in.ok && in.Left() > 0) {
        const uint64_t len = in.VarU64();
        if (!in.ok || len > in.Left()) return false;
        packets.push_back(in.Bytes(static_cast<size_t>(len)));
    }
    return in.ok;
}

// Server packets of the recording that are not played back: the connection setup (sent by the handshake
// instead), encryption, packs data, and anything that would end or move the connection.
inline bool Replayed(const std::string& packet) {
    switch (GameId(packet)) {
    case Id::NetworkSettings: case Id::ServerToClientHandshake: case Id::Disconnect: case Id::Transfer:
    case Id::PacksInfo: case Id::PackStack: case Id::PackDataInfo: case Id::PackChunkData:
        return false;
    case Id::PlayStatus: { In in = Body(packet); return in.U32be() == 3 && in.ok; }   // only PlayerSpawn
    default: return true;
    }
}

// ---- Recording ----
struct Record { bool sent = false, mark = false; uint32_t delta = 0; std::string data; };

// Reads a .mcrec file record by record. The FILE is owned (closed by the destructor).
class Recording {
    FILE* file_ = nullptr;
public:
    MyClientReplay::Header header{};
    std::string networkSettings, packsInfo, packStack;   // first recorded copies (full packets)
    bool hasStartGame = false, hasMark = false;
    uint64_t runtimeId = 0;
    uint64_t markMs = 0, totalMs = 0;   // record mark, end (ms from the first record); played length = total - mark
    std::string error;

    Recording() = default;
    Recording(const Recording&) = delete;
    Recording& operator=(const Recording&) = delete;
    ~Recording() { if (file_) std::fclose(file_); }

    static bool Seek(FILE* f, uint64_t at) {
#ifdef _WIN32
        return _fseeki64(f, static_cast<long long>(at), SEEK_SET) == 0;
#else
        return fseeko(f, static_cast<off_t>(at), SEEK_SET) == 0;
#endif
    }
    // Takes the file and scans it once (headers only, plus the few setup packets).
    bool Open(FILE* f) {
        file_ = f;
        if (!file_) { error = "Could not open the file."; return false; }
        if (std::fread(&header, sizeof header, 1, file_) != 1 || std::memcmp(header.magic, "MCREC001", 8) != 0 ||
            header.version < 1 || header.version > MyClientReplay::Version) { error = "Not a replay file."; return false; }
        uint64_t at = sizeof header, ms = 0;
        for (;;) {
            uint32_t head[2];
            if (std::fread(head, 4, 2, file_) != 2) break;
            const bool sent = (head[0] & MyClientReplay::SentFlag) != 0;
            const uint32_t size = head[0] & ~MyClientReplay::SentFlag;
            ms += head[1];
            at += 8;
            if (size == 0) { if (!hasMark) { hasMark = true; markMs = ms; } continue; }
            if (size > MyClientReplay::MaxPacket) break;
            if (!sent) {
                char idBytes[2] = {};
                const size_t got = std::fread(idBytes, 1, size < 2 ? size : 2, file_);
                const uint32_t id = MyClientReplay::PacketId(idBytes, got);
                std::string* keep = id == Id::NetworkSettings ? &networkSettings : id == Id::PacksInfo ? &packsInfo :
                                    id == Id::PackStack ? &packStack : nullptr;
                if ((keep && keep->empty() && size <= (1u << 20)) || (id == Id::StartGame && !hasStartGame)) {
                    std::string packet(size, '\0');
                    if (!Seek(file_, at) || std::fread(&packet[0], 1, size, file_) != size) break;
                    if (keep) *keep = packet;
                    else { hasStartGame = true; ReadRuntimeId(packet, runtimeId); }
                }
            }
            at += size;
            if (!Seek(file_, at)) break;
            totalMs = ms;
        }
        if (!hasStartGame) { error = "This recording has no world data (StartGame)."; return false; }
        if (packsInfo.empty() || packStack.empty()) { error = "This recording started mid-connection (no pack list); it cannot be played."; return false; }
        if (!hasMark) markMs = 0;
        return Seek(file_, sizeof header);
    }
    uint64_t LengthMs() const { return totalMs > markMs ? totalMs - markMs : 0; }
    // Next record; false at the end (or a damaged tail).
    bool Next(Record& r) {
        uint32_t head[2];
        if (!file_ || std::fread(head, 4, 2, file_) != 2) return false;
        r.sent = (head[0] & MyClientReplay::SentFlag) != 0;
        const uint32_t size = head[0] & ~MyClientReplay::SentFlag;
        r.delta = head[1];
        r.mark = size == 0;
        if (size > MyClientReplay::MaxPacket) return false;
        r.data.assign(size, '\0');
        return size == 0 || std::fread(&r.data[0], 1, size, file_) == size;
    }
};

// ---- The connection to the game ----
enum class State { Waiting, Connecting, Settings, Login, Packs, Stack, Playing, Finished, Closed };

class Session {
public:
    // Datagrams to send to the client (the wrapper empties it after every call).
    std::vector<std::string> out;

    explicit Session(uint64_t guid = 0x4d79436c52706c79ull) : guid_(guid) {}
    bool Open(FILE* f) { return rec_.Open(f); }
    const Recording& Rec() const { return rec_; }
    State GetState() const { return state_; }
    uint64_t PositionMs() const { return playMs_ < rec_.LengthMs() ? static_cast<uint64_t>(playMs_) : rec_.LengthMs(); }
    bool Paused() const { return paused_; }
    void SetPaused(bool p) { paused_ = p; }
    float Speed() const { return speed_; }
    void SetSpeed(float s) { speed_ = s < 0.25f ? 0.25f : s > 8.f ? 8.f : s; }
    bool PacksRequested() const { return packsRequested_; }
    uint16_t port = 0;   // our port (shown in the ping answer)

    // Ends the connection (the game shows its disconnected screen).
    void Stop(uint64_t now) {
        if (state_ == State::Closed || state_ == State::Waiting) { state_ = State::Closed; return; }
        Out o; o.U8(Msg::Disconnect);
        Queue(o.b, 0);
        Flush(now);
        state_ = State::Closed;
    }

    // One datagram from the client at `ip`:`port` (host order).
    void OnDatagram(const char* data, size_t size, uint32_t ip, uint16_t clientPort, uint64_t now) {
        if (size == 0 || state_ == State::Closed) return;
        In in(data, size);
        const uint32_t id = in.U8();
        if (!(id & 0x80)) { Offline(id, in, ip, clientPort, now); return; }
        if (state_ == State::Waiting) return;
        lastHeard_ = now;
        if (id & 0x40) {   // ACK
            for (uint32_t s : ReadAck(in)) unacked_.erase(s);
            return;
        }
        if (id & 0x20) {   // NAK: send those again now
            for (uint32_t s : ReadAck(in)) {
                auto it = unacked_.find(s);
                if (it == unacked_.end()) continue;
                Resend(it->second, now);
                unacked_.erase(it);
            }
            return;
        }
        const uint32_t seq = in.U24le();
        std::vector<Frame> frames;
        if (!in.ok || !ReadFrames(in, frames)) return;
        acks_.push_back(seq);
        for (auto& f : frames) OnFrame(std::move(f), now);
    }

    // Advances the replay and the transport; call often (every few ms).
    void Pump(uint64_t now) {
        if (state_ == State::Closed || state_ == State::Waiting) return;
        if (state_ != State::Connecting && lastHeard_ && now - lastHeard_ > 15000) { state_ = State::Closed; return; }
        if (!acks_.empty()) { out.push_back(AckPacket(0xC0, acks_)); acks_.clear(); }
        // Handshake steps the client answers with a (maybe compressed) pack response: go on after a while anyway.
        if (state_ == State::Packs && now - stateAt_ > 2500) SendStack(now);
        else if (state_ == State::Stack && now - stateAt_ > 2500) StartPlaying(now);
        if (state_ == State::Playing) Play(now);
        // Resend what was not acknowledged within 400 ms (loopback answers in a few).
        for (auto it = unacked_.begin(); it != unacked_.end();) {
            if (now - it->second.sentAt > 400) { Resend(it->second, now); it = unacked_.erase(it); }
            else ++it;
        }
        Flush(now);
    }

    size_t Backlog() const { return queuedBytes_ + unacked_.size() * mtu_; }

private:
    struct Sent { std::vector<Frame> frames; uint64_t sentAt; };

    Recording rec_;
    uint64_t guid_;
    State state_ = State::Waiting;
    uint64_t stateAt_ = 0, lastHeard_ = 0;
    uint32_t clientIp_ = 0; uint16_t clientPort_ = 0;
    size_t mtu_ = 1400;
    // send side
    uint32_t datagramSeq_ = 0, reliableIndex_ = 0, orderIndex_ = 0; uint16_t splitId_ = 0;
    std::deque<Frame> queue_;
    size_t queuedBytes_ = 0;
    std::map<uint32_t, Sent> unacked_;
    // receive side
    std::vector<uint32_t> acks_;
    std::set<uint32_t> seenReliable_;
    struct Split { uint32_t count = 0, got = 0; std::vector<std::string> parts; };
    std::map<uint16_t, Split> splits_;
    bool compression_ = false;   // NetworkSettings sent: every batch now carries the compression byte
    bool packsRequested_ = false;
    // playback
    Record pending_; bool hasPending_ = false, ended_ = false;
    uint64_t fileMs_ = 0;        // time of the last record taken from the file
    double playMs_ = 0;          // playback position (ms after the mark)
    uint64_t lastPlayAt_ = 0;
    bool paused_ = false; float speed_ = 1.f;
    uint64_t tick_ = 0;

    void Offline(uint32_t id, In& in, uint32_t ip, uint16_t clientPort, uint64_t now) {
        Out o;
        if (id == Msg::UnconnectedPing || id == Msg::UnconnectedPingOpen) {
            const uint64_t time = in.U64be();
            if (!HasMagic(in)) return;
            const std::string p = std::to_string(port);
            const std::string motd = "MCPE;MyClient Replay;0;0.0.0;0;1;" + std::to_string(guid_) + ";Replay;Survival;1;" + p + ";" + p + ";0;";
            o.U8(Msg::UnconnectedPong); o.U64be(time); o.U64be(guid_); PutMagic(o); o.U16be(static_cast<uint32_t>(motd.size())); o.Raw(motd);
        }
        else if (id == Msg::OpenRequest1) {
            if (!HasMagic(in)) return;
            mtu_ = std::min<size_t>(std::max<size_t>(in.n + 28, 576), 1400);
            o.U8(Msg::OpenReply1); PutMagic(o); o.U64be(guid_); o.U8(0); o.U16be(static_cast<uint32_t>(mtu_));
        }
        else if (id == Msg::OpenRequest2) {
            if (!HasMagic(in)) return;
            SkipAddress(in);
            const uint32_t mtu = in.U16be();
            if (!in.ok) return;
            if (mtu >= 576 && mtu < mtu_) mtu_ = mtu;
            o.U8(Msg::OpenReply2); PutMagic(o); o.U64be(guid_); PutAddress(o, ip, clientPort); o.U16be(static_cast<uint32_t>(mtu_)); o.U8(0);
            if (state_ == State::Waiting) { state_ = State::Connecting; stateAt_ = now; lastHeard_ = now; clientIp_ = ip; clientPort_ = clientPort; }
        }
        else return;
        out.push_back(o.b);
    }

    void OnFrame(Frame f, uint64_t now) {
        if (IsReliable(f.reliability)) {
            if (!seenReliable_.insert(f.reliableIndex).second) return;   // a resend we already have
            while (seenReliable_.size() > 4096) seenReliable_.erase(seenReliable_.begin());
        }
        if (f.split) {
            if (f.splitCount == 0 || f.splitCount > 8192 || f.splitIndex >= f.splitCount) return;
            Split& s = splits_[f.splitId];
            if (s.count != f.splitCount) { s = Split{}; s.count = f.splitCount; s.parts.resize(f.splitCount); }
            if (s.parts[f.splitIndex].empty() && !f.body.empty()) { s.parts[f.splitIndex] = std::move(f.body); ++s.got; }
            if (s.got < s.count) return;
            std::string whole;
            for (auto& part : s.parts) whole += part;
            splits_.erase(f.splitId);
            OnMessage(whole, now);
            return;
        }
        OnMessage(f.body, now);
    }

    void OnMessage(const std::string& m, uint64_t now) {
        if (m.empty()) return;
        In in(m);
        const uint32_t id = in.U8();
        if (id == Msg::ConnectedPing) {
            const uint64_t time = in.U64be();
            Out o; o.U8(Msg::ConnectedPong); o.U64be(time); o.U64be(now);
            Queue(o.b, 0);
        }
        else if (id == Msg::ConnectionRequest) {
            in.U64be();
            const uint64_t time = in.U64be();
            Out o; o.U8(Msg::ConnectionAccepted); PutAddress(o, clientIp_, clientPort_); o.U16be(0);
            for (int i = 0; i < 20; ++i) PutAddress(o, 0, 0);
            o.U64be(time); o.U64be(now);
            Queue(o.b, 3);
        }
        else if (id == Msg::NewIncoming) {
            if (state_ == State::Connecting) { state_ = State::Settings; stateAt_ = now; }
        }
        else if (id == Msg::Disconnect) state_ = State::Closed;
        else if (id == Msg::Game) OnGame(m, now);
    }

    void OnGame(const std::string& m, uint64_t now) {
        std::vector<std::string> packets;
        const bool readable = ReadBatch(m, compression_, packets);
        if (state_ == State::Settings || state_ == State::Connecting) {
            for (const auto& p : packets)
                if (GameId(p) == Id::RequestNetworkSettings) {
                    // The answer to RequestNetworkSettings is the last uncompressed batch.
                    SendGame({rec_.networkSettings.empty() ? DefaultNetworkSettings() : rec_.networkSettings});
                    compression_ = true;
                    state_ = State::Login; stateAt_ = now;
                }
            return;
        }
        if (state_ == State::Login) {
            // Login (or a compressed batch, which here can only be the Login).
            bool login = !readable;
            for (const auto& p : packets) login = login || GameId(p) == Id::Login;
            if (!login) return;
            SendGame({PlayStatus(0), rec_.packsInfo});
            state_ = State::Packs; stateAt_ = now;
            return;
        }
        for (const auto& p : packets) {
            if (GameId(p) != Id::PackResponse) continue;
            In in = Body(p);
            const uint32_t status = in.U8();
            if (!in.ok) continue;
            if (status == PackStatus::SendPacks) packsRequested_ = true;
            if (state_ == State::Packs && (status == PackStatus::HaveAllPacks || status == PackStatus::SendPacks)) SendStack(now);
            else if (state_ == State::Stack && status == PackStatus::Completed) StartPlaying(now);
        }
    }

    void SendStack(uint64_t now) { SendGame({rec_.packStack}); state_ = State::Stack; stateAt_ = now; }
    void StartPlaying(uint64_t now) { state_ = State::Playing; stateAt_ = now; lastPlayAt_ = now; playMs_ = 0; }

    // Takes the records that are due. Before the mark everything goes at once; after it, by the recorded clock.
    void Play(uint64_t now) {
        const uint64_t step = now - lastPlayAt_;
        lastPlayAt_ = now;
        std::vector<std::string> batch;
        size_t batchBytes = 0;
        const auto flushBatch = [&] { if (!batch.empty()) { SendGame(batch); batch.clear(); batchBytes = 0; } };
        bool reachedMark = !rec_.hasMark || fileMs_ >= rec_.markMs;
        if (reachedMark && !paused_) playMs_ += static_cast<double>(step) * speed_;
        // Back-pressure: no more than ~4 MB on the way (the setup burst can be tens of MB).
        while (Backlog() < (4u << 20)) {
            if (!hasPending_) {
                if (ended_ || !rec_.Next(pending_)) { ended_ = true; break; }
                hasPending_ = true;
            }
            const uint64_t at = fileMs_ + pending_.delta;
            reachedMark = !rec_.hasMark || at >= rec_.markMs;
            if (reachedMark && static_cast<double>(at - rec_.markMs) > playMs_) break;   // not due yet
            fileMs_ = at;
            hasPending_ = false;
            if (pending_.mark) continue;
            if (pending_.sent) {
                Pose pose;
                if (GameId(pending_.data) == Id::PlayerAuthInput && ReadPose(pending_.data, pose))
                    batch.push_back(MovePlayer(rec_.runtimeId, pose.x, pose.y, pose.z, pose.pitch, pose.yaw, pose.headYaw, ++tick_));
            }
            else if (Replayed(pending_.data)) {
                batchBytes += pending_.data.size();
                batch.push_back(std::move(pending_.data));
            }
            if (batchBytes > (256u << 10)) flushBatch();
        }
        flushBatch();
        if (ended_ && !hasPending_ && queue_.empty()) state_ = State::Finished;
    }

    void SendGame(const std::vector<std::string>& packets) { Queue(Batch(packets, compression_), 3); }

    // Queues one message, split to fit the datagram size. reliability 3 = reliable ordered, 0 = unreliable.
    void Queue(const std::string& body, uint8_t reliability) {
        const size_t room = mtu_ - 60;   // IP/UDP, datagram and frame headers
        const uint32_t order = reliability ? orderIndex_++ : 0;
        if (body.size() <= room) {
            Frame f; f.reliability = reliability; f.body = body; f.orderIndex = order;
            if (IsReliable(reliability)) f.reliableIndex = reliableIndex_++;
            queuedBytes_ += f.body.size();
            queue_.push_back(std::move(f));
            return;
        }
        const uint32_t count = static_cast<uint32_t>((body.size() + room - 1) / room);
        const uint16_t id = splitId_++;
        for (uint32_t i = 0; i < count; ++i) {
            Frame f; f.reliability = 3; f.split = true; f.splitCount = count; f.splitId = id; f.splitIndex = i;
            f.orderIndex = order; f.reliableIndex = reliableIndex_++;
            f.body = body.substr(i * room, room);
            queuedBytes_ += f.body.size();
            queue_.push_back(std::move(f));
        }
    }

    void Emit(std::vector<Frame> frames, uint64_t now) {
        Out o; o.U8(0x84); o.U24le(datagramSeq_);
        for (const auto& f : frames) PutFrame(o, f);
        out.push_back(o.b);
        bool reliable = false;
        for (const auto& f : frames) reliable = reliable || IsReliable(f.reliability);
        if (reliable) unacked_[datagramSeq_] = Sent{std::move(frames), now};
        datagramSeq_ = (datagramSeq_ + 1) & 0xFFFFFF;
    }
    void Resend(Sent& s, uint64_t now) {
        std::vector<Frame> frames;
        for (auto& f : s.frames) if (IsReliable(f.reliability)) frames.push_back(std::move(f));
        if (!frames.empty()) Emit(std::move(frames), now);
    }
    // Packs queued frames into datagrams (at most 2048 unacknowledged ones on the way).
    void Flush(uint64_t now) {
        while (!queue_.empty() && unacked_.size() < 2048) {
            std::vector<Frame> frames;
            size_t size = 4;
            while (!queue_.empty() && (frames.empty() || size + FrameSize(queue_.front()) <= mtu_ - 28)) {
                size += FrameSize(queue_.front());
                queuedBytes_ -= queue_.front().body.size();
                frames.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
            Emit(std::move(frames), now);
        }
    }
};

}  // namespace MyClientReplayServer

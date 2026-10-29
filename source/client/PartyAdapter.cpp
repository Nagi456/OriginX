#include "Profile.h"
#include "PartyAdapter.h"
#include "PartyChannel.h"
#include "FeatureAdapter.h"
#include "AutoRq.h"
#include "mc/client/player/LocalPlayer.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/network/GameConnectionInfo.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/PacketSender.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/CommandRequestPacket.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <new>
#include <cstddef>

namespace {
std::atomic<bool> active{false};
std::mutex mutex;
MyClientParty::Channel channel;
MyClientParty::Gate gate;
bool hooked = false;
using PacketFactory = std::shared_ptr<Packet> (*)(MinecraftPacketIds);
using ConnectionDtor = void (*)(Social::GameConnectionInfo*);
PacketFactory createPacket = nullptr;
ConnectionDtor destroyConnection = nullptr;
// The SDK returns an owning optional, but its game's destructor is not exported
// by the loader. Construct in aligned storage with guaranteed copy elision and
// release its contained game object through the resolved game destructor.
class ConnectionSnapshot {
    using Optional = std::optional<Social::GameConnectionInfo>;
    alignas(Optional) std::byte storage[sizeof(Optional)];
    Optional* value;
public:
    explicit ConnectionSnapshot(IClientInstance& client)
        : value(::new(static_cast<void*>(storage)) Optional(client.getGameConnectionInfo())) {}
    ~ConnectionSnapshot() { if (value->has_value()) destroyConnection(&value->value()); }
    ConnectionSnapshot(const ConnectionSnapshot&) = delete;
    ConnectionSnapshot& operator=(const ConnectionSnapshot&) = delete;
    const Optional& Get() const { return *value; }
};
struct Pending { std::string tag, host; IClientInstance* client; uint64_t at; };
std::optional<Pending> pending;
std::string currentHost;
MyClientParty::Status state = MyClientParty::Status::Waiting;
uint32_t sent = 0;
uint64_t lastUnknownLog = 0;
std::string Hive(IClientInstance& client) {
    ConnectionSnapshot snapshot(client);
    const auto& connection = snapshot.Get();
    if (!connection) return {};
    const auto& host = connection->mUnresolvedUrl.get();
    if (MyClientParty::HiveHost(host)) return MyClientParty::Lower(host);
    const auto& ip = connection->mHostIpAddress.get();
    return MyClientParty::HiveHost(ip) ? MyClientParty::Lower(ip) : std::string{};
}
MyClientParty::Config ReadConfig() {
    MyClientParty::Config config{};
    if (channel.Data() && channel.Lock()) {
        if (MyClientParty::Valid(*channel.Data(), GetCurrentProcessId())) config = channel.Data()->config;
        else channel.Reset(GetCurrentProcessId());
        channel.Unlock();
    }
    return config;
}
void Publish() {
    if (!channel.Data() || !channel.Lock()) return;
    channel.Data()->at = GetTickCount64();
    channel.Data()->status = state; channel.Data()->sent = sent;
    channel.Unlock();
}
// A message that looks party-related but is not read as an invite (wrong packet type, localized
// text, or sent by a player). Logs the packet type only; chat contents are never logged.
void NoteSkipped(unsigned type, bool localized, bool authored, const std::string& message) {
    const auto lower = MyClientParty::Lower(MyClientParty::Clean(message));
    if (lower.find("party") == std::string::npos ||
        (lower.find("invit") == std::string::npos && lower.find("join") == std::string::npos)) return;
    std::lock_guard guard(mutex);
    const auto now = GetTickCount64();
    if (lastUnknownLog && now - lastUnknownLog < 30000) return;
    lastUnknownLog = now;
    ll::mod::NativeMod::current()->getLogger().warn(
        "A party-related message was not read as an invite (packet type {}, localized {}, from a player {}). Chat contents are not logged.",
        type, localized, authored);
}
// Auto RQ team modes: the last "<Colour> Team has been eliminated!" server message (time and the colour
// code of the team name). Written on the network thread, read by TickAutoRq on the game thread.
std::atomic<uint64_t> eliminatedAt{0};
std::atomic<char> eliminatedColor{0};
std::atomic<char> ownTeamColor{0};   // last colour seen on your own name tag while alive
// Server (not player) lines only. Each elimination line is logged with the colours compared, so a
// mismatch in the field can be diagnosed; player chat is never logged.
std::atomic<uint64_t> gameOverAt{0};   // last "... is the WINNER!" line (the game ended, whoever won)
std::atomic<uint64_t> partyOutAt{0};   // last "All members of your party are now spectating" line
void NoteElimination(const std::string& message) {
    if (MyClientAutoRq::PartyAllSpectating(message)) {
        partyOutAt.store(GetTickCount64());
        ll::mod::NativeMod::current()->getLogger().info("Auto RQ: your whole party is spectating");
        return;
    }
    if (MyClientAutoRq::GameOver(message)) {
        gameOverAt.store(GetTickCount64());
        ll::mod::NativeMod::current()->getLogger().info("Auto RQ: game over (\"{}\")", MyClientAutoRq::Decode(message).text);
        return;
    }
    const auto e = MyClientAutoRq::TeamEliminated(message);
    if (!e.found) return;
    eliminatedColor.store(e.color);
    eliminatedAt.store(GetTickCount64());
    const char own = ownTeamColor.load();
    ll::mod::NativeMod::current()->getLogger().info(
        "Auto RQ: \"{}\" (team colour {}, your colour {}{})", e.text,
        e.color ? std::string(1, e.color) : std::string("none"), own ? std::string(1, own) : std::string("unknown"),
        e.color && e.color == own ? ", your team" : "");
}
void Receive(IClientInstance& client, TextPacket const& packet) {
    if (!active.load()) return;
    try {
        // Auto RQ team modes look at every server line before the invite filters below.
        if (const auto* t = std::get_if<TextPacketPayload::MessageAndParams>(&packet.mBody.get()))
            NoteElimination(t->mMessage.get());
        else if (const auto* m = std::get_if<TextPacketPayload::MessageOnly>(&packet.mBody.get());
                 m && packet.mXuid->empty() && packet.mPlatformId->empty())
            NoteElimination(m->mMessage.get());
    } catch (...) {}
    if (const auto* translated = std::get_if<TextPacketPayload::MessageAndParams>(&packet.mBody.get())) {
        NoteSkipped(static_cast<unsigned>(translated->mType), true, false, translated->mMessage.get());
        return;
    }
    const auto* body = std::get_if<TextPacketPayload::MessageOnly>(&packet.mBody.get());
    if (!body) return;
    const unsigned type = static_cast<unsigned>(body->mType);
    const bool authored = !packet.mXuid->empty() || !packet.mPlatformId->empty();
    if (packet.mLocalize || authored || (type != 0 && type != 6)) {
        NoteSkipped(type, packet.mLocalize, authored, body->mMessage.get());
        return;
    }
    auto host = Hive(client);
    if (host.empty()) return;
    auto tag = MyClientParty::Invite(body->mMessage.get(), type, false, false);
    std::lock_guard guard(mutex);
    if (!active.load()) return;
    const auto now = GetTickCount64();
    const auto config = ReadConfig();
    if (!MyClientParty::Active(config, now)) return;
    if (!tag.empty()) {
        if (MyClientParty::Registered(config, tag, now))
            pending = Pending{std::move(tag), std::move(host), &client, now};
        return;
    }
    const auto clean = MyClientParty::Clean(body->mMessage.get());
    const auto lower = MyClientParty::Lower(clean);
    if ((lower.find("party") != std::string::npos && (lower.find("invit") != std::string::npos || lower.find("join") != std::string::npos)) ||
        (clean.find("パーティ") != std::string::npos && clean.find("招待") != std::string::npos)) {
        state = MyClientParty::Status::UnknownFormat;
        if (!lastUnknownLog || now - lastUnknownLog >= 30000) {
            lastUnknownLog = now;
            ll::mod::NativeMod::current()->getLogger().warn("Hive party message format not recognized ({} bytes); ignored. Chat contents are not logged.", clean.size());
        }
        Publish();
    }
}
// Pinned SDK virtual handle signature; symbol resolution is checked on enable.
LL_TYPE_INSTANCE_HOOK(TextHook, ll::memory::HookPriority::Normal, ClientNetworkHandler,
    ll::memory::SymbolView{"?handle@ClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVTextPacket@@@Z"},
    void, NetworkIdentifier const& source, TextPacket const& packet)
{
    origin(source, packet);
    try { MyClientProfile::Scope prof(MyClientProfile::Chat); Receive(mClient, packet); } catch (...) { /* Preserve vanilla chat handling. */ }
}
}
namespace MyClientGame {
// GameConnectionInfo::mTransportType exists only in newer game versions (26.51+). Older versions do
// not have the member (the build fails there), so it is read only when it exists; otherwise the
// connection is treated as the standard UDP (RakNet) kind, which is what those versions use.
template <class Info>
uint32_t TransportOf(const Info& info)
{
    if constexpr (requires { info.mTransportType; })
        return static_cast<uint32_t>(info.mTransportType);
    else
        return 0u;
}

bool CurrentServer(std::string& host, uint32_t& port, uint32_t& transport)
{
    // A snapshot can only be released through the game destructor resolved by EnableParty().
    if (!destroyConnection) return false;
    const auto client = ll::service::getClientInstance();
    if (!client) return false;
    ConnectionSnapshot snapshot(*client);
    const auto& connection = snapshot.Get();
    if (!connection) return false;
    host = connection->mHostIpAddress.get();
    if (host.empty()) host = connection->mUnresolvedUrl.get();
    port = static_cast<uint32_t>(connection->mPort);
    transport = TransportOf(*connection);
    return !host.empty() && port != 0;
}

bool EnableParty() {
    if (!createPacket) createPacket = reinterpret_cast<PacketFactory>(ll::memory::SymbolView{
        "?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z"}.resolve());
    if (!destroyConnection) destroyConnection = reinterpret_cast<ConnectionDtor>(ll::memory::SymbolView{
        "??1GameConnectionInfo@Social@@QEAA@XZ"}.resolve());
    if (!createPacket || !destroyConnection) return false;
    (void)TextHook::_AutoHookCount;
    if (!hooked) { if (TextHook::hook() != 0) return false; hooked = true; }
    std::lock_guard guard(mutex);
    if (!channel.Open(GetCurrentProcessId(), true)) return false;
    pending.reset(); gate.Reset(); currentHost.clear(); sent = 0;
    state = MyClientParty::Status::Waiting; active.store(true); return true;
}
void ClearParty() {
    std::lock_guard guard(mutex);
    pending.reset(); gate.Reset(); currentHost.clear();
    state = MyClientParty::Status::Waiting;
    if (channel.Data() && channel.Lock()) { channel.Data()->config = {}; channel.Unlock(); }
    Publish();
}
void DisableParty() {
    active.store(false); ClearParty();
    std::lock_guard guard(mutex); channel.Close();
}
// Auto RQ. Solo modes: the local player went from alive to dead/spectator on Hive -> send "/q <MODE>" once.
// Team modes (Duos, Squads, Mega, Duels 2v2): ignore your own death and wait for Hive's
// "<Colour> Team has been eliminated!" for YOUR team, recognised by your name tag's colour.
void TickAutoRq() {
    static MyClientAutoRq::Gate rqGate;
    static IClientInstance* lastClient = nullptr;
    static uint64_t teamHandled = 0, gameOverHandled = 0, partyOutHandled = 0, lastFiredAt = 0;
    try {
        const auto client = ll::service::getClientInstance();
        auto* player = client ? client->getLocalPlayer() : nullptr;
        if (!player || !createPacket) { rqGate.Reset(); lastClient = nullptr; return; }
        const uint32_t mode = MyClientGame::AutoRqMode();
        if (&*client != lastClient) { lastClient = &*client; rqGate.Reset(); teamHandled = eliminatedAt.load(); gameOverHandled = gameOverAt.load(); partyOutHandled = partyOutAt.load(); ownTeamColor.store(0); }
        if (mode == 0) { rqGate.Reset(); teamHandled = eliminatedAt.load(); gameOverHandled = gameOverAt.load(); partyOutHandled = partyOutAt.load(); return; }
        const auto now = GetTickCount64();
        // Actor::isSpectator is not exported by LeviLamina; resolve it like createPacket (absent -> alive check only).
        using SpectatorFn = bool (*)(const Actor*);
        static const auto isSpectator = reinterpret_cast<SpectatorFn>(
            ll::memory::SymbolView{"?isSpectator@Actor@@QEBA_NXZ"}.resolve());
        const bool dead = !player->isAlive() || (isSpectator && isSpectator(player));
        // The game ended (also when you won): queue the next one in every mode, once per message.
        const uint64_t over = gameOverAt.load();
        const bool gameOver = over != 0 && over != gameOverHandled && now - over <= 10000;
        if (over != gameOverHandled) gameOverHandled = over;
        // Every party member is spectating: your team is out, queue at once in every mode, once per message.
        const uint64_t partyOut = partyOutAt.load();
        const bool partyGone = partyOut != 0 && partyOut != partyOutHandled && now - partyOut <= 10000;
        if (partyOut != partyOutHandled) partyOutHandled = partyOut;
        if (gameOver || partyGone) {
            rqGate.Reset();
        } else if (MyClientAutoRq::TeamMode(mode)) {
            rqGate.Reset();
            if (!dead) {
                // Remember the team colour while alive: a spectator's tag may be recoloured.
                const char color = MyClientGame::LocalTeamColor();
                if (color && color != ownTeamColor.load()) {
                    ownTeamColor.store(color);
                    ll::mod::NativeMod::current()->getLogger().info("Auto RQ: your team colour is {}", std::string(1, color));
                }
            }
            const uint64_t at = eliminatedAt.load();
            // One request per message, only while it is recent, never twice within the solo cooldown.
            if (at == 0 || at == teamHandled || now - at > 10000) return;
            teamHandled = at;
            const char own = ownTeamColor.load();
            if (!own || eliminatedColor.load() != own) return;   // another team, or your colour is unknown
        } else {
            if (!rqGate.Update(true, dead, now)) return;
        }
        // Never twice within the cooldown (e.g. your death and the game-over line moments apart).
        if (lastFiredAt && now - lastFiredAt < MyClientAutoRq::Gate::kCooldownMs) return;
        lastFiredAt = now;
        if (Hive(*client).empty()) return;                      // Hive only
        auto packet = createPacket(MinecraftPacketIds::CommandRequest);
        if (!packet || !client->getPacketSender().isInitialized()) return;
        auto& command = static_cast<CommandRequestPacket&>(*packet);
        const std::string text = MyClientAutoRq::Command(mode);
        command.mCommand = text;
        command.mOrigin->mType = CommandOriginType::Player;
        command.mOrigin->mUUID = mce::UUID::random();
        command.mOrigin->mRequestId = std::string{};
        command.mOrigin->mPlayerId = 0;
        command.mVersion = CurrentCmdVersion::Latest;
        command.mInternalSource = false;
        client->getPacketSender().sendToServer(command);
        ll::mod::NativeMod::current()->getLogger().info("Auto RQ: sent {}", text);
    } catch (...) {}
}
void TickParty() {
    if (!active.load()) return;
    const auto client = ll::service::getClientInstance();
    const auto host = client && client->getLocalPlayer() ? Hive(*client) : std::string{};
    std::optional<Pending> request;
    {
        std::lock_guard guard(mutex);
        if (!active.load()) return;
        const auto now = GetTickCount64();
        const auto config = ReadConfig();
        if (host != currentHost) { currentHost = host; pending.reset(); gate.Reset(); state = MyClientParty::Status::Waiting; }
        if (!MyClientParty::Active(config, now)) { pending.reset(); state = MyClientParty::Status::Disabled; }
        else if (host.empty()) { pending.reset(); state = MyClientParty::Status::OtherServer; }
        else {
            if (state != MyClientParty::Status::UnknownFormat && state != MyClientParty::Status::Sent && state != MyClientParty::Status::Failed)
                state = MyClientParty::Status::Ready;
            if (pending && pending->client == &*client && pending->host == host &&
                MyClientParty::Fresh(pending->at, now, 3000) && MyClientParty::Registered(config, pending->tag, now) && gate.Allow(pending->tag, now)) {
                request = std::move(pending); gate.Sent(request->tag, now);
            }
            pending.reset();
        }
        Publish();
    }
    if (!request) return;
    // Send only on the game tick, outside our mutex to allow game callbacks.
    bool success = false;
    try {
        if (active.load() && Hive(*client) == request->host && client->getLocalPlayer()) {
            auto packet = createPacket(MinecraftPacketIds::CommandRequest);
            bool stillRegistered = false;
            { std::lock_guard guard(mutex); stillRegistered = active.load() &&
                MyClientParty::Registered(ReadConfig(), request->tag, GetTickCount64()); }
            if (stillRegistered && packet && client->getPacketSender().isInitialized()) {
                auto& command = static_cast<CommandRequestPacket&>(*packet);
                command.mCommand = MyClientParty::AcceptCommand(request->tag);
                command.mOrigin->mType = CommandOriginType::Player;
                command.mOrigin->mUUID = mce::UUID::random();
                command.mOrigin->mRequestId = std::string{};
                command.mOrigin->mPlayerId = 0;
                command.mVersion = CurrentCmdVersion::Latest;
                command.mInternalSource = false;
                client->getPacketSender().sendToServer(command); success = true;
            }
        }
    } catch (...) { success = false; }
    std::lock_guard guard(mutex);
    if (active.load()) { state = success ? MyClientParty::Status::Sent : MyClientParty::Status::Failed; if (success) ++sent; Publish(); }
}
}

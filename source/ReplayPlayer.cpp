#include "ReplayPlayer.h"
#include "ReplayServer.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace ReplayPlayer {
namespace {

using MyClientReplayServer::Session;
using MyClientReplayServer::State;

std::mutex mutex;                      // guards everything below
std::unique_ptr<Session> session;
std::thread worker;
std::atomic<bool> running{false};
SOCKET sock = INVALID_SOCKET;
sockaddr_in client{};                  // the game, once it joined
bool haveClient = false;
uint64_t startedAt = 0;
Status last;                           // kept after the playback ends (the error stays readable)

uint64_t Now() { return GetTickCount64(); }

Phase PhaseOf(State s)
{
    switch (s) {
    case State::Waiting: return Phase::Waiting;
    case State::Playing: return Phase::Playing;
    case State::Finished: return Phase::Finished;
    case State::Closed: return Phase::Idle;
    default: return Phase::Joining;
    }
}

void Send(const std::vector<std::string>& datagrams, const sockaddr_in& to)
{
    for (const auto& d : datagrams)
        sendto(sock, d.data(), static_cast<int>(d.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
}

// The socket loop: datagrams in, Pump every few ms, datagrams out.
void Run()
{
    std::vector<char> buffer(65536);
    while (running.load()) {
        fd_set read;
        FD_ZERO(&read);
        FD_SET(sock, &read);
        timeval wait{0, 2000};
        const int ready = select(0, &read, nullptr, nullptr, &wait);
        std::lock_guard guard(mutex);
        if (!session) break;
        if (ready > 0) {
            for (int i = 0; i < 256; ++i) {
                sockaddr_in from{};
                int fromLen = sizeof from;
                const int got = recvfrom(sock, buffer.data(), static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (got <= 0) break;
                // Only this PC; and after the game connected, only that game.
                if (ntohl(from.sin_addr.s_addr) >> 24 != 127) continue;
                if (haveClient && (from.sin_addr.s_addr != client.sin_addr.s_addr || from.sin_port != client.sin_port) &&
                    static_cast<uint8_t>(buffer[0]) >= 0x80) continue;
                session->OnDatagram(buffer.data(), static_cast<size_t>(got), ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), Now());
                if (!haveClient && session->GetState() != State::Waiting) { client = from; haveClient = true; }
                Send(session->out, from);
                session->out.clear();
                u_long pending = 0;
                if (ioctlsocket(sock, FIONREAD, &pending) != 0 || pending == 0) break;
            }
        }
        if (session->GetState() == State::Waiting && Now() - startedAt > 30000) {
            last.error = L"Minecraft did not join the replay (is it on the home screen?).";
            break;
        }
        session->Pump(Now());
        if (haveClient) Send(session->out, client);
        session->out.clear();
        last.phase = PhaseOf(session->GetState());
        last.positionMs = session->PositionMs();
        last.paused = session->Paused();
        last.speed = session->Speed();
        last.packsRequested = session->PacksRequested();
        if (session->GetState() == State::Closed) break;
    }
    std::lock_guard guard(mutex);
    running.store(false);
    last.phase = Phase::Idle;
}

// Says goodbye to the game (it shows its disconnected screen), stops the thread and frees everything.
void Shutdown()
{
    {
        std::lock_guard guard(mutex);
        if (session && sock != INVALID_SOCKET && running.load()) {
            session->Stop(Now());
            if (haveClient) Send(session->out, client);
            session->out.clear();
        }
        running.store(false);
    }
    if (worker.joinable()) worker.join();
    std::lock_guard guard(mutex);
    session.reset();
    haveClient = false;
    last.phase = Phase::Idle;
    if (sock != INVALID_SOCKET) { closesocket(sock); sock = INVALID_SOCKET; }
}

}  // namespace

bool Start(const std::wstring& path, std::wstring& error)
{
    Shutdown();
    auto s = std::make_unique<Session>();
    if (!s->Open(_wfopen(path.c_str(), L"rb"))) {
        const std::string& e = s->Rec().error;
        error.assign(e.begin(), e.end());
        return false;
    }
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { error = L"Network start failed."; return false; }
    SOCKET so = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (so == INVALID_SOCKET) { error = L"Could not open a network socket."; return false; }
    int big = 8 << 20;
    setsockopt(so, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&big), sizeof big);
    setsockopt(so, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&big), sizeof big);
    u_long nonBlocking = 1;
    ioctlsocket(so, FIONBIO, &nonBlocking);
    uint16_t port = 0;
    for (uint16_t p = 19150; p < 19170 && !port; ++p) {
        sockaddr_in at{};
        at.sin_family = AF_INET;
        at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        at.sin_port = htons(p);
        if (bind(so, reinterpret_cast<sockaddr*>(&at), sizeof at) == 0) port = p;
    }
    if (!port) { closesocket(so); error = L"No free local port (19150-19169)."; return false; }
    s->port = port;
    {
        std::lock_guard guard(mutex);
        sock = so;
        last = Status{};
        haveClient = false;
        startedAt = Now();
        last.phase = Phase::Waiting;
        last.lengthMs = s->Rec().LengthMs();
        last.file = path;
        session = std::move(s);
    }
    running.store(true);
    worker = std::thread(Run);
    const std::wstring uri = L"minecraft://connect/?serverUrl=127.0.0.1&serverPort=" + std::to_wstring(port);
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        Shutdown();
        error = L"Could not ask Minecraft to join (minecraft:// link failed).";
        return false;
    }
    return true;
}

void Stop() { Shutdown(); }

Status Get()
{
    std::lock_guard guard(mutex);
    return last;
}

bool Active()
{
    std::lock_guard guard(mutex);
    return session != nullptr && last.phase != Phase::Idle;
}

void SetPaused(bool paused)
{
    std::lock_guard guard(mutex);
    if (session) { session->SetPaused(paused); last.paused = session->Paused(); }
}

void SetSpeed(float speed)
{
    std::lock_guard guard(mutex);
    if (session) { session->SetSpeed(speed); last.speed = session->Speed(); }
}

}  // namespace ReplayPlayer

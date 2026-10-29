#include "PingMonitor.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

namespace PingMonitor {

namespace {
std::mutex lock;
std::string targetHost;
int targetPort = 0;
bool targetWanted = false;
std::atomic<int> latestMs{-1};
std::atomic<ULONGLONG> latestAt{0};
std::atomic<bool> started{false};

void Worker()
{
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in remote{};
    bool resolved = false;
    std::string resolvedHost;
    int resolvedPort = 0;
    std::vector<double> samples;
    int missed = 0;
    const uint64_t guid = (static_cast<uint64_t>(GetCurrentProcessId()) << 32) ^ GetTickCount64() ^ 0x4d79436c69656e74ull;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);

    for (;;) {
        std::string host;
        int port = 0;
        bool wanted = false;
        {
            std::lock_guard guard(lock);
            host = targetHost; port = targetPort; wanted = targetWanted;
        }
        if (!wanted || host.empty() || port <= 0 || port > 65535) {
            latestMs.store(-1);
            samples.clear(); missed = 0;
            if (sock != INVALID_SOCKET) { closesocket(sock); sock = INVALID_SOCKET; }
            resolved = false;
            Sleep(500);
            continue;
        }
        if (!resolved || host != resolvedHost || port != resolvedPort) {
            addrinfo hints{}, *found = nullptr;
            hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP;
            if (getaddrinfo(host.c_str(), nullptr, &hints, &found) == 0 && found) {
                remote = *reinterpret_cast<sockaddr_in*>(found->ai_addr);
                remote.sin_port = htons(static_cast<u_short>(port));
                resolved = true; resolvedHost = host; resolvedPort = port;
                samples.clear(); missed = 0;
            }
            if (found) freeaddrinfo(found);
            if (!resolved) { Sleep(2000); continue; }
        }
        if (sock == INVALID_SOCKET) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock == INVALID_SOCKET) { Sleep(2000); continue; }
            DWORD timeout = 1000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        }

        LARGE_INTEGER sent{}, received{};
        QueryPerformanceCounter(&sent);
        const auto packet = BuildPing(GetTickCount64(), guid);
        const int sentBytes = sendto(sock, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                                     reinterpret_cast<const sockaddr*>(&remote), sizeof remote);
        bool answered = false;
        if (sentBytes == static_cast<int>(packet.size())) {
            uint8_t buffer[1500];
            for (int attempts = 0; attempts < 4 && !answered; ++attempts) {
                sockaddr_in from{};
                int fromLength = sizeof from;
                const int n = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof buffer, 0,
                                       reinterpret_cast<sockaddr*>(&from), &fromLength);
                if (n <= 0) break;   // timeout / error
                uint64_t echoed = 0;
                // Only accept a pong from the address we pinged.
                if (from.sin_addr.s_addr == remote.sin_addr.s_addr && ParsePong(buffer, static_cast<size_t>(n), echoed)) {
                    QueryPerformanceCounter(&received);
                    const double ms = 1000.0 * static_cast<double>(received.QuadPart - sent.QuadPart) / static_cast<double>(frequency.QuadPart);
                    if (PlausibleMs(ms)) {
                        samples.push_back(ms);
                        if (samples.size() > 10) samples.erase(samples.begin());
                        double mean = 0;
                        if (Average(samples, 5, mean)) {
                            latestMs.store(static_cast<int>(std::lround(mean)));
                            latestAt.store(GetTickCount64());
                        }
                        missed = 0; answered = true;
                    }
                }
            }
        }
        if (!answered && ++missed >= 3) { latestMs.store(-1); samples.clear(); }

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double elapsedMs = 1000.0 * static_cast<double>(now.QuadPart - sent.QuadPart) / static_cast<double>(frequency.QuadPart);
        if (elapsedMs < 1000.0) Sleep(static_cast<DWORD>(1000.0 - elapsedMs));
    }
}
}

void SetTarget(const std::string& host, int port, bool wanted)
{
    {
        std::lock_guard guard(lock);
        targetHost = host; targetPort = port; targetWanted = wanted;
    }
    if (wanted && !started.exchange(true)) std::thread(Worker).detach();
}

bool Get(int& ms)
{
    const int value = latestMs.load();
    const auto at = latestAt.load();
    const auto now = GetTickCount64();
    if (value < 0 || at == 0 || now < at || now - at > 4000) return false;
    ms = value;
    return true;
}

}

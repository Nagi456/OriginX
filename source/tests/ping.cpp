#include "FeatureData.h"
#include "PingMonitor.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
    using namespace PingMonitor;
    // ---- RakNet unconnected ping / pong ----
    const auto ping = BuildPing(0x0102030405060708ull, 0xAABBCCDDEEFF0011ull);
    assert(ping.size() == 33 && ping[0] == 0x01);
    assert(ping[1] == 0x01 && ping[8] == 0x08);                       // big-endian time
    assert(std::memcmp(&ping[9], Magic, 16) == 0);
    assert(ping[25] == 0xAA && ping[32] == 0x11);                     // client GUID
    std::vector<uint8_t> pong(33, 0);
    pong[0] = 0x1C;
    for (int i = 0; i < 8; ++i) pong[1 + i] = static_cast<uint8_t>(i + 1);
    std::memcpy(&pong[17], Magic, 16);
    uint64_t echoed = 0;
    assert(ParsePong(pong.data(), pong.size(), echoed) && echoed == 0x0102030405060708ull);
    pong.insert(pong.end(), {0x00, 0x05, 'M', 'O', 'T', 'D', '!'});     // trailing server string is fine
    assert(ParsePong(pong.data(), pong.size(), echoed));
    auto wrongId = pong; wrongId[0] = 0x1D; assert(!ParsePong(wrongId.data(), wrongId.size(), echoed));
    auto wrongMagic = pong; wrongMagic[20] ^= 1; assert(!ParsePong(wrongMagic.data(), wrongMagic.size(), echoed));
    assert(!ParsePong(pong.data(), 32, echoed) && !ParsePong(nullptr, 40, echoed));   // truncated / null
    // ---- smoothing / plausibility ----
    assert(PlausibleMs(0) && PlausibleMs(35.5) && PlausibleMs(5000) && !PlausibleMs(-1) && !PlausibleMs(5001));
    assert(!PlausibleMs(std::nan("")) && !PlausibleMs(INFINITY));
    double mean = 0;
    assert(!Average({}, 5, mean) && !Average({1.0}, 0, mean));
    assert(Average({10, 20, 30}, 5, mean) && mean == 20);
    assert(Average({100, 10, 20, 30, 40, 50}, 5, mean) && mean == 30);   // only the latest 5
    // ---- shared "Server" record (DLL -> HUD) ----
    using namespace MyClientFeatures;
    static_assert(sizeof(Server) == 80 && sizeof(Stats) == 48 && sizeof(Shared) == 1592 && Version == 16);
    Server server{};
    SetServer(server, "1.2.3.4", 19132, 0, 1000);
    char host[64]; uint32_t port = 0;
    assert(PingTarget(server, 1500, host, port) && std::strcmp(host, "1.2.3.4") == 0 && port == 19132);
    assert(!PingTarget(server, 7001, host, port));                    // stale (> 6 s)
    assert(!PingTarget(server, 999, host, port));                     // stamp from the future
    SetServer(server, "geo.hivebedrock.network", 19132, 2, 1000);
    assert(!PingTarget(server, 1500, host, port));                    // not RakNet: nothing is sent
    SetServer(server, "", 19132, 0, 1000);
    assert(!PingTarget(server, 1500, host, port));                    // no server
    SetServer(server, "evil host;rm", 19132, 0, 1000);
    assert(!PingTarget(server, 1500, host, port));                    // odd characters are refused
    SetServer(server, "1.2.3.4", 0, 0, 1000);
    assert(!PingTarget(server, 1500, host, port));
    SetServer(server, "1.2.3.4", 70000, 0, 1000);
    assert(!PingTarget(server, 1500, host, port));
    std::string longHost(100, 'a');
    SetServer(server, longHost.c_str(), 19132, 0, 1000);              // truncated to 63, still valid text
    assert(PingTarget(server, 1500, host, port) && std::strlen(host) == 63);
    Server unterminated{}; unterminated.at = 1000; unterminated.port = 19132;
    for (auto& c : unterminated.host) c = 'a';                        // no NUL at all
    assert(!PingTarget(unterminated, 1500, host, port));
    std::cout << "PASS: RakNet ping/pong packets, smoothing, server record validation\n";
}

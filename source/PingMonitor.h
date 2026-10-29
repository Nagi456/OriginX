#pragma once
#include <cstdint>
#include <string>
#include <cmath>
#include <vector>

// Ping measurement for Bedrock servers: the same lightweight RakNet "Unconnected Ping" the server
// list uses (1 small UDP packet per second). No game memory is touched, and nothing is sent when
// the feature is off. Packet building / parsing is plain C++ so it can be unit-tested anywhere.
namespace PingMonitor {

// RakNet OFFLINE_MESSAGE_DATA_ID
constexpr uint8_t Magic[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                               0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};

// ID_UNCONNECTED_PING (0x01): time(8, big-endian) + magic(16) + client GUID(8) = 33 bytes.
// ID_UNCONNECTED_PONG (0x1C): time(8) + server GUID(8) + magic(16) + string(len16 + text).
// ParsePong returns true and the echoed time when `data` is a well-formed pong.
// PlausibleMs: a value is shown only if it is finite and within 0..5000 ms.
// Average: mean of the most recent samples (at most `window`), or false if there are none.
inline std::vector<uint8_t> BuildPing(uint64_t timeMs, uint64_t guid)
{
    std::vector<uint8_t> packet;
    packet.reserve(33);
    packet.push_back(0x01);
    for (int shift = 56; shift >= 0; shift -= 8) packet.push_back(static_cast<uint8_t>(timeMs >> shift));
    packet.insert(packet.end(), Magic, Magic + 16);
    for (int shift = 56; shift >= 0; shift -= 8) packet.push_back(static_cast<uint8_t>(guid >> shift));
    return packet;
}

inline bool ParsePong(const uint8_t* data, size_t size, uint64_t& echoedTimeMs)
{
    // id(1) + time(8) + serverGuid(8) + magic(16) = 33 bytes minimum; the MOTD string follows.
    if (!data || size < 33 || data[0] != 0x1C) return false;
    for (int i = 0; i < 16; ++i) if (data[17 + i] != Magic[i]) return false;
    uint64_t time = 0;
    for (int i = 0; i < 8; ++i) time = (time << 8) | data[1 + i];
    echoedTimeMs = time;
    return true;
}

inline bool PlausibleMs(double ms) { return std::isfinite(ms) && ms >= 0.0 && ms <= 5000.0; }

inline bool Average(const std::vector<double>& samples, size_t window, double& result)
{
    if (samples.empty() || window == 0) return false;
    const size_t count = samples.size() < window ? samples.size() : window;
    double sum = 0;
    for (size_t i = samples.size() - count; i < samples.size(); ++i) sum += samples[i];
    result = sum / static_cast<double>(count);
    return true;
}


// ---- background worker (Windows only; defined in PingMonitor.cpp) ----
// Tells the worker where to ping. wanted=false (or an empty host) stops all traffic and clears results.
void SetTarget(const std::string& host, int port, bool wanted);
// Latest smoothed ping in milliseconds; false while unknown or stale.
bool Get(int& ms);

}

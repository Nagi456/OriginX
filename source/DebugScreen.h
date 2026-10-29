#pragma once
#include "FeatureData.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Debug Menu: the Java-style F3 screen's text, built from what the HUD knows. Left column: the game and
// the world (FPS, server, position, facing, time), right column: the computer (memory, CPU, display, GPU).
namespace DebugScreen {
constexpr float EyeHeight = 1.62f;   // the camera is this far above the player's feet

struct Info
{
    std::wstring title;
    bool fpsKnown = false, frameGen = false;
    float fps = 0, shownFps = 0;
    bool camKnown = false;
    float eyeX = 0, eyeY = 0, eyeZ = 0, yaw = 0, pitch = 0;
    bool serverKnown = false, pingKnown = false;
    std::string host;
    int port = 0, pingMs = 0;
    bool clockKnown = false, clockMoving = false;
    int32_t time = 0;
    int windowW = 0, windowH = 0, displayW = 0, displayH = 0, displayHz = 0;
    uint64_t gameMemory = 0, ramTotal = 0, ramFree = 0;
    std::wstring cpu, gpu;
    int cores = 0;
    // The GPU the game draws with (from the DLL; `gpu` above is Windows' primary display adapter, the fallback).
    std::string gameGpu;
    uint32_t directX = 0, featureLevel = 0, videoMb = 0, vendor = 0;
    uint64_t driver = 0;
    bool debugKnown = false;   // the DLL's block / biome / light
    MyClientFeatures::Debug debug{};
};

inline int Floor(float v) { return (int)std::floor(v); }
inline int ChunkOf(int block) { return block >= 0 ? block / 16 : -((-block + 15) / 16); }
inline int InChunk(int block) { return ((block % 16) + 16) % 16; }

// Yaw in degrees as the game keeps it: 0 = south (+Z), 90 = west, 180 = north, -90 = east.
inline float NormalYaw(float yaw)
{
    if (!std::isfinite(yaw)) return 0;
    yaw = std::fmod(yaw, 360.f);
    if (yaw >= 180.f) yaw -= 360.f;
    if (yaw < -180.f) yaw += 360.f;
    return yaw;
}
inline std::wstring Facing(float yaw)
{
    const float y = NormalYaw(yaw);
    if (y >= -45.f && y < 45.f) return L"south (Towards positive Z)";
    if (y >= 45.f && y < 135.f) return L"west (Towards negative X)";
    if (y >= -135.f && y < -45.f) return L"east (Towards positive X)";
    return L"north (Towards negative Z)";
}

inline std::wstring Fixed(double v, int digits)
{
    wchar_t buffer[48];
    swprintf(buffer, 48, L"%.*f", digits, std::isfinite(v) ? v : 0.0);
    return buffer;
}
inline std::wstring MinSec(int seconds)
{
    wchar_t buffer[16];
    swprintf(buffer, 16, L"%d:%02d", seconds / 60, seconds % 60);
    return buffer;
}
inline std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // host names are ASCII
// Block and biome ids as the game names them; Java writes the namespace, so do we.
inline std::wstring Id(const char* name)
{
    std::string id(name);
    if (id.empty()) return L"---";
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    return Widen(id);
}
inline const wchar_t* FaceName(uint32_t face)
{
    static const wchar_t* names[] = {L"down", L"up", L"north", L"south", L"west", L"east"};
    return face < 6 ? names[face] : L"---";
}
// D3D feature level 0xc200 -> "12_2".
inline std::wstring FeatureLevel(uint32_t level)
{
    return std::to_wstring(level >> 12) + L"_" + std::to_wstring((level >> 8) & 0xF);
}
// Driver version 31.0.15.4633 (four 16-bit parts, high first); NVIDIA's own number is the last five digits: 546.33.
inline std::wstring DriverVersion(uint64_t driver, uint32_t vendor)
{
    const unsigned a = (unsigned)(driver >> 48), b = (unsigned)(driver >> 32) & 0xFFFF, c = (unsigned)(driver >> 16) & 0xFFFF,
                   d = (unsigned)driver & 0xFFFF;
    std::wstring text = std::to_wstring(a) + L"." + std::to_wstring(b) + L"." + std::to_wstring(c) + L"." + std::to_wstring(d);
    if (vendor == 0x10DE)
    {
        const unsigned v = (c % 10) * 10000 + d % 10000;
        wchar_t nvidia[32];
        swprintf(nvidia, 32, L" (NVIDIA %u.%02u)", v / 100, v % 100);
        text += nvidia;
    }
    return text;
}
inline std::wstring Utf8(const std::string& s);   // below
inline std::wstring Mb(uint64_t bytes) { return std::to_wstring(bytes / (1024 * 1024)) + L" MB"; }

inline std::vector<std::wstring> LeftLines(const Info& i)
{
    std::vector<std::wstring> lines{i.title};
    if (!i.fpsKnown) lines.push_back(L"--- fps");
    else if (i.frameGen)
        lines.push_back(std::to_wstring(std::lround(i.shownFps)) + L" fps (" + std::to_wstring(std::lround(i.fps)) + L" game, Frame Generation)");
    else lines.push_back(std::to_wstring(std::lround(i.fps)) + L" fps");
    if (!i.serverKnown) lines.push_back(L"Server: none (local world)");
    else lines.push_back(L"Server: " + Widen(i.host) + L":" + std::to_wstring(i.port) +
                         (i.pingKnown ? L", " + std::to_wstring(i.pingMs) + L" ms ping" : std::wstring(L", ping ---")));
    lines.push_back(L"");
    if (i.camKnown)
    {
        const float feetY = i.eyeY - EyeHeight;
        const int bx = Floor(i.eyeX), by = Floor(feetY), bz = Floor(i.eyeZ);
        lines.push_back(L"XYZ: " + Fixed(i.eyeX, 3) + L" / " + Fixed(feetY, 5) + L" / " + Fixed(i.eyeZ, 3));
        lines.push_back(L"Block: " + std::to_wstring(bx) + L" " + std::to_wstring(by) + L" " + std::to_wstring(bz));
        lines.push_back(L"Chunk: " + std::to_wstring(InChunk(bx)) + L" " + std::to_wstring(InChunk(by)) + L" " + std::to_wstring(InChunk(bz)) +
                        L" in " + std::to_wstring(ChunkOf(bx)) + L" " + std::to_wstring(ChunkOf(by)) + L" " + std::to_wstring(ChunkOf(bz)));
        lines.push_back(L"Facing: " + Facing(i.yaw) + L" (" + Fixed(NormalYaw(i.yaw), 1) + L" / " + Fixed(i.pitch, 1) + L")");
    }
    else lines.push_back(L"XYZ: ---");
    if (i.debugKnown && i.debug.lightKnown)
        lines.push_back(L"Client Light: " + std::to_wstring(std::max(i.debug.skyLight, i.debug.blockLight)) + L" (" +
                        std::to_wstring(i.debug.skyLight) + L" sky, " + std::to_wstring(i.debug.blockLight) + L" block)");
    else lines.push_back(L"Client Light: ---");
    lines.push_back(L"Biome: " + (i.debugKnown ? Id(i.debug.biome) : std::wstring(L"---")));
    lines.push_back(L"");
    if (!i.clockKnown) lines.push_back(L"Time: ---");
    else
    {
        const int32_t t = MyClientFeatures::TimeOfDay(i.time);
        const int64_t day = (int64_t)i.time >= 0 ? (int64_t)i.time / MyClientFeatures::DayTicks : 0;
        std::wstring line = L"Day " + std::to_wstring(day) + L", time " + std::to_wstring(t);
        if (!i.clockMoving) line += L" (time stopped)";
        else if (MyClientFeatures::IsNight(i.time)) line += L" (night, morning in " + MinSec(MyClientFeatures::SecondsUntilMorning(i.time)) + L")";
        else line += L" (day, night in " + MinSec(MyClientFeatures::SecondsUntilNight(i.time)) + L")";
        lines.push_back(line);
    }
    return lines;
}

inline std::vector<std::wstring> RightLines(const Info& i)
{
    std::vector<std::wstring> lines;
    lines.push_back(L"Game memory: " + (i.gameMemory ? Mb(i.gameMemory) : std::wstring(L"---")));
    if (i.ramTotal)
    {
        const uint64_t used = i.ramTotal > i.ramFree ? i.ramTotal - i.ramFree : 0;
        lines.push_back(L"RAM: " + std::to_wstring(used * 100 / i.ramTotal) + L"% " + std::to_wstring(used / (1024 * 1024)) + L"/" +
                        std::to_wstring(i.ramTotal / (1024 * 1024)) + L"MB");
    }
    lines.push_back(L"");
    if (!i.cpu.empty()) lines.push_back(L"CPU: " + std::to_wstring(i.cores) + L"x " + i.cpu);
    lines.push_back(L"");
    if (i.displayW) lines.push_back(L"Display: " + std::to_wstring(i.displayW) + L"x" + std::to_wstring(i.displayH) +
                                    (i.displayHz > 1 ? L" (" + std::to_wstring(i.displayHz) + L" Hz)" : std::wstring()));
    if (i.windowW) lines.push_back(L"Window: " + std::to_wstring(i.windowW) + L"x" + std::to_wstring(i.windowH));
    if (!i.gameGpu.empty())
    {
        lines.push_back(Utf8(i.gameGpu));
        std::wstring api = L"DirectX " + std::to_wstring(i.directX);
        if (i.featureLevel) api += L" (feature level " + FeatureLevel(i.featureLevel) + L")";
        lines.push_back(api);
        std::wstring memory = i.videoMb ? L"VRAM: " + std::to_wstring(i.videoMb) + L" MB" : std::wstring();
        if (i.driver) memory += (memory.empty() ? L"" : L", ") + std::wstring(L"Driver ") + DriverVersion(i.driver, i.vendor);
        if (!memory.empty()) lines.push_back(memory);
    }
    else if (!i.gpu.empty()) lines.push_back(i.gpu);
    if (i.debugKnown && i.debug.lookFace != MyClientFeatures::NoLook)
    {
        lines.push_back(L"");
        lines.push_back(L"Targeted Block: " + std::to_wstring(i.debug.lookX) + L", " + std::to_wstring(i.debug.lookY) + L", " +
                        std::to_wstring(i.debug.lookZ));
        lines.push_back(Id(i.debug.lookBlock));
        lines.push_back(L"Face: " + std::wstring(FaceName(i.debug.lookFace)));
    }
    return lines;
}
// UTF-8 -> UTF-16 (GPU names), without Windows: the HUD's tests run on Linux.
inline std::wstring Utf8(const std::string& s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = (unsigned char)s[i];
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) { out += L'?'; i++; continue; }
        uint32_t cp = n == 1 ? c : c & (0x7F >> n);
        for (int k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        if (cp >= 0x10000 && sizeof(wchar_t) == 2) { cp -= 0x10000; out += (wchar_t)(0xD800 + (cp >> 10)); out += (wchar_t)(0xDC00 + (cp & 0x3FF)); }
        else out += (wchar_t)cp;
        i += n;
    }
    return out;
}
}

#include "DebugScreen.h"
#include <cassert>
#include <iostream>
using namespace DebugScreen;
using MyClientFeatures::NoLook;
int main()
{
    // Chunks: floor division by 16, also for negative blocks.
    assert(ChunkOf(0) == 0 && ChunkOf(15) == 0 && ChunkOf(16) == 1 && ChunkOf(-1) == -1 && ChunkOf(-16) == -1 && ChunkOf(-17) == -2);
    assert(InChunk(-1) == 15 && InChunk(17) == 1 && InChunk(-16) == 0);
    // Facing: 0 south, 90 west, 180 / -180 north, -90 east, any number of turns.
    assert(Facing(0).rfind(L"south", 0) == 0 && Facing(90).rfind(L"west", 0) == 0 && Facing(180).rfind(L"north", 0) == 0);
    assert(Facing(-180).rfind(L"north", 0) == 0 && Facing(-90).rfind(L"east", 0) == 0 && Facing(270).rfind(L"east", 0) == 0);
    assert(Facing(720 + 10).rfind(L"south", 0) == 0 && NormalYaw(190) == -170.f);
    Info i;
    i.title = L"T"; i.fpsKnown = true; i.fps = 230.4f;
    i.camKnown = true; i.eyeX = -0.5f; i.eyeY = 65.62f; i.eyeZ = 31.9f; i.yaw = 180; i.pitch = 12.25f;
    i.clockKnown = i.clockMoving = true; i.time = 24000 * 3 + 13000;
    const auto left = LeftLines(i);
    assert(left[1] == L"230 fps" && left[2] == L"Server: none (local world)");
    assert(left[4] == L"XYZ: -0.500 / 64.00000 / 31.900" && left[5] == L"Block: -1 64 31" && left[6] == L"Chunk: 15 0 15 in -1 4 1");
    assert(left[7] == L"Facing: north (Towards negative Z) (-180.0 / 12.3)" || left[7] == L"Facing: north (Towards negative Z) (-180.0 / 12.2)");
    assert(left[11] == L"Day 3, time 13000 (night, morning in 9:10)");
    assert(left[8] == L"Client Light: ---" && left[9] == L"Biome: ---" && left[11].rfind(L"Day 3", 0) == 0);
    i.debugKnown = true; i.debug.lightKnown = 1; i.debug.skyLight = 12; i.debug.blockLight = 14;
    std::snprintf(i.debug.biome, sizeof i.debug.biome, "plains");
    i.debug.lookFace = 2; i.debug.lookX = 10; i.debug.lookY = 64; i.debug.lookZ = -3;
    std::snprintf(i.debug.lookBlock, sizeof i.debug.lookBlock, "minecraft:stone");
    const auto withDebug = LeftLines(i);
    assert(withDebug[8] == L"Client Light: 14 (12 sky, 14 block)" && withDebug[9] == L"Biome: minecraft:plains");
    const auto look = RightLines(i);
    assert(look[look.size() - 3] == L"Targeted Block: 10, 64, -3" && look[look.size() - 2] == L"minecraft:stone" && look.back() == L"Face: north");
    i.debug = {}; i.debug.lookFace = NoLook;
    i.ramTotal = 16ull << 30; i.ramFree = 4ull << 30; i.cpu = L"CPU"; i.cores = 8;
    const auto right = RightLines(i);
    assert(right[0] == L"Game memory: ---" && right[1] == L"RAM: 75% 12288/16384MB" && right[3] == L"CPU: 8x CPU");
    // GPU: the game's adapter, DirectX version and feature level, VRAM and the driver (NVIDIA's own number too).
    assert(FeatureLevel(0xc200) == L"12_2" && FeatureLevel(0xb000) == L"11_0");
    assert(DriverVersion((31ull << 48) | (15ull << 16) | 4633ull, 0x10DE) == L"31.0.15.4633 (NVIDIA 546.33)");
    assert(DriverVersion((31ull << 48) | (15ull << 16) | 4633ull, 0x1002) == L"31.0.15.4633");
    i.gameGpu = "NVIDIA GeForce RTX 4050 Laptop GPU"; i.directX = 12; i.featureLevel = 0xc200; i.videoMb = 5921; i.vendor = 0x10DE;
    i.driver = (32ull << 48) | (15ull << 16) | 6094ull; i.gpu = L"Intel(R) UHD Graphics";
    const auto gpuLines = RightLines(i);
    assert(gpuLines[gpuLines.size() - 3] == L"NVIDIA GeForce RTX 4050 Laptop GPU" && gpuLines[gpuLines.size() - 2] == L"DirectX 12 (feature level 12_2)");
    assert(gpuLines.back() == L"VRAM: 5921 MB, Driver 32.0.15.6094 (NVIDIA 560.94)");
    assert(Utf8("A\xC3\xA9") == L"A\u00E9");
    std::cout << "PASS: debug screen chunks, facing, position / time / memory lines\n";
}

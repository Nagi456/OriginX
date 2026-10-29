#pragma once
// Draws inside the game's own frame (D3D12): the HUD picture from MyClient.exe and the 3D lines
// (Hitbox, Block Outline). See Compositor.cpp.
#include <cstdint>
#include <string>
#include "TabListChannel.h"

namespace MyClientCompositor {
// Compositor.cpp includes the real d3d12.h, which clashes with the stand-in D3D12 types of the
// LeviLamina headers, so it includes no LeviLamina header: the caller hands in logging and hooking.
struct Host {
    void (*info)(const std::string& text);
    void (*warn)(const std::string& text);
    int (*hook)(void* target, void* detour, void** original);   // 0 = installed
};
// Hooks the swap chain. At mod load (probe false) only its creation is hooked: the game's first swap chain then
// brings the other hooks in before it hands out its back buffers. At enable (probe true) a game whose swap chain
// already exists is hooked through a throw-away one. False when this game build cannot be drawn into.
bool Start(const Host& host, bool probe);
void Stop();
// Inputs, from whichever game thread has them; the compositor reads the latest on the render thread.
void SetCamera(float x, float y, float z, float yaw, float pitch, float fov);   // every rendered frame
// The game's own view/projection matrices of the frame (column-major) and the camera position
// (LevelRendererPlayer::setupCamera). Preferred over SetCamera when fresh: exact, bobbing included.
void SetMatrices(const float view[16], const float proj[16], float x, float y, float z);
bool DrawingOutline();   // the coloured Block Outline was drawn recently (the game's own can be skipped)
void SetBoxes(const MyClientTabList::Box* boxes, uint32_t count);               // game tick
// Every rendered frame: how far each box of the last SetBoxes must move (x, y, z each) to sit where the game
// draws its actor this frame. Ignored unless count matches that list; SetBoxes clears it.
void SetBoxShifts(const float* shifts, uint32_t count);
void SetTarget(bool has, int x, int y, int z);                                  // game tick
// Frame Generation status for the HUD: frames presented per second (made-up ones included) and a
// MyClientFeatures::FrameGenState value.
void FrameGenStatus(float& presentedFps, uint32_t& state);
// Debug Menu: the GPU the game draws with (see MyClientFeatures::Shared.gpuName...); false before its first frame.
struct Gpu { char name[128]; uint32_t directX, featureLevel, videoMb, vendor; uint64_t driver; };
bool GpuInfo(Gpu& out);
}

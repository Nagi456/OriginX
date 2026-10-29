#pragma once
// Frame Generation on the GPU (D3D12): keeps the last two game frames and makes the in-between pictures
// the Compositor presents before each game frame. See FrameGen.cpp; the timing lives in FrameGenPacing.h.
// Like Compositor.cpp, this file includes the real d3d12.h and no LeviLamina header.
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>
#include <string>

namespace MyClientFrameGen {
struct Gpu {
    ID3D12Device* device;
    ID3D12CommandQueue* queue;   // the game's direct queue
    void(STDMETHODCALLTYPE* execute)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);   // the unhooked ExecuteCommandLists
    void (*warn)(const std::string& text);
};
// Every call below runs on the render thread under the Compositor's lock; `back` is a back buffer in the
// PRESENT state, and is left in that state.
bool Ensure(const Gpu& gpu, UINT width, UINT height, DXGI_FORMAT format);   // (re)creates what this size needs
// The game's frame in `back` becomes the newest frame; with `motion`, also measures how the picture moved
// since the previous one. True when a previous frame exists, so in-between pictures can be made.
bool Capture(const Gpu& gpu, ID3D12Resource* back, bool motion);
// Writes the picture at `phase` (0 = previous frame, 1 = newest) into `back`. method: MyClientFeatures::FrameGenMethod.
bool Generate(const Gpu& gpu, ID3D12Resource* back, D3D12_CPU_DESCRIPTOR_HANDLE rtv, float phase, uint32_t method);
bool Restore(const Gpu& gpu, ID3D12Resource* back);   // copies the newest game frame into `back`
void Forget();     // the next Capture has no previous frame (after a pause or a hitch)
void Release();    // waits for the GPU, then frees everything (resize, swap chain change, stop)
bool Failed();     // something could not be created: Frame Generation stays off
}  // namespace MyClientFrameGen

// In-game drawing (D3D12). MyClient.exe used to show the HUD in a transparent window on top of the
// game; any visible pixel there made Windows compose the game through the desktop and capped it at
// the monitor refresh rate (200 FPS -> 60). Now the HUD hands its finished picture over shared memory
// (HudFrameChannel.h) and this file draws it into the game's back buffer right before the game
// presents it, together with the 3D lines that must follow the camera exactly (Hitbox, Block Outline).
//
// How: the factory's swap chain creation is hooked at mod load; the game's own new swap chain and queue then give
// the addresses of IDXGISwapChain::Present/Present1, ResizeBuffers/ResizeBuffers1 and
// ID3D12CommandQueue::ExecuteCommandLists (the same code serves every swap chain in the process), and those are
// hooked. A game whose swap chain was made earlier is hooked through a throw-away device + swap chain instead.
// The game's direct queue is taken from its swap chain creation or its first ExecuteCommandLists call. Each
// Present then records one small command list: upload the changed rows of the HUD picture, draw it as a
// textured rectangle plus the lines, and execute it on that queue.
// GetBuffer / GetCurrentBackBufferIndex are hooked too: the game draws into our own copies of its back
// buffers ("proxies"), so Frame Generation can present the extra frames and the game frame from its own
// thread at the right moments while the game goes on with its next frame (see PresentProxied).
// A game running on D3D11 is reported as unsupported and the HUD keeps its old window.
#include "Compositor.h"
#include "FeatureAdapter.h"
#include "CompositorGeometry.h"
#include "HudFrameChannel.h"
#include "FrameGen.h"
#include "FrameGenPacing.h"
#include "FeatureData.h"

#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace MyClientCompositor {
namespace {
using Microsoft::WRL::ComPtr;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Resize1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using GetBufferFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, REFIID, void**);
using BufferIndexFn = UINT(STDMETHODCALLTYPE*)(IDXGISwapChain3*);
using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                 const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using CreateCoreFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*,
                                                 IDXGISwapChain1**);
using CreateCompFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*,
                                                 IDXGISwapChain1**);
using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using SerializeRootFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
template <class Fn> Fn Proc(HMODULE module, const char* name) {
    return module ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name))) : nullptr;
}
using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                   ID3DBlob**, ID3DBlob**);

void* presentTarget = nullptr; PresentFn presentOrig = nullptr;
void* present1Target = nullptr; Present1Fn present1Orig = nullptr;
void* resizeTarget = nullptr; ResizeFn resizeOrig = nullptr;
void* resize1Target = nullptr; Resize1Fn resize1Orig = nullptr;
void* executeTarget = nullptr; ExecuteFn executeOrig = nullptr;
void* getBufferTarget = nullptr; GetBufferFn getBufferOrig = nullptr;
void* indexTarget = nullptr; BufferIndexFn indexOrig = nullptr;
bool proxyHooks = false;                    // both GetBuffer and GetCurrentBackBufferIndex are hooked
void* createChainTarget = nullptr; CreateChainFn createChainOrig = nullptr;
void* createHwndTarget = nullptr; CreateHwndFn createHwndOrig = nullptr;
void* createCoreTarget = nullptr; CreateCoreFn createCoreOrig = nullptr;
void* createCompTarget = nullptr; CreateCompFn createCompOrig = nullptr;
bool tearingSupported = false;              // the system can present with tearing (IDXGIFactory5)
std::atomic<uint32_t> tearingChains{0};     // game swap chains made with ALLOW_TEARING added by us

std::atomic<ID3D12CommandQueue*> gameQueue{nullptr};
std::atomic<bool> enabled{false};
std::mutex renderMutex;   // Present / ResizeBuffers may come from different threads

// ---- inputs ----
std::mutex inputMutex;
Camera camera; uint64_t cameraAt = 0;
Matrices matrices{}; float matricesPos[3] = {}; uint64_t matricesAt = 0;
std::atomic<uint64_t> outlineDrawnAt{0};
std::vector<MyClientTabList::Box> boxes; uint64_t boxesAt = 0;
std::vector<float> boxShifts;   // 3 per box of `boxes`, or empty (no shift known yet)
bool hasTarget = false; int targetX = 0, targetY = 0, targetZ = 0; uint64_t targetAt = 0;

// ---- render state (renderMutex) ----
// One per back buffer. Everything the CPU writes for a frame (upload rows, vertices) lives here, so the
// CPU only waits when it comes back to the same back buffer — normally long finished. Waiting for the
// previous frame instead stopped the game overlapping CPU and GPU work and cost a third of the FPS.
struct Frame {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12Resource> back;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    UINT64 fenceValue = 0;                     // signalled when the GPU is done with this frame's list
    ComPtr<ID3D12Resource> upload;             // HUD rows for this frame
    uint8_t* uploadBytes = nullptr;
    UINT64 uploadSize = 0;
    ComPtr<ID3D12Resource> vertexBuffer;
    Vertex* vertexBytes = nullptr;
    ComPtr<ID3D12CommandAllocator> copyAllocator;   // copying the game's proxy picture into this back buffer
    UINT64 copyFenceValue = 0;
};
ComPtr<ID3D12Device> device;
ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;
UINT rtvStep = 0;
std::vector<Frame> frames;
IDXGISwapChain* boundSwap = nullptr;   // identity only, never dereferenced or kept alive
bool boundTearing = false;             // boundSwap may present with DXGI_PRESENT_ALLOW_TEARING
DXGI_FORMAT backFormat = DXGI_FORMAT_UNKNOWN;
UINT backWidth = 0, backHeight = 0;
ComPtr<ID3D12GraphicsCommandList> list, copyList;
ComPtr<ID3D12Fence> fence;
HANDLE fenceEvent = nullptr;
UINT64 fenceValue = 0;
ComPtr<ID3D12RootSignature> rootSignature;
ComPtr<ID3DBlob> vsBlob, psBlob;
ComPtr<ID3D12PipelineState> pipeline;
DXGI_FORMAT pipelineFormat = DXGI_FORMAT_UNKNOWN;
ComPtr<ID3D12Resource> texture;
UINT textureWidth = 0, textureHeight = 0, uploadPitch = 0;
D3D12_RESOURCE_STATES textureState = D3D12_RESOURCE_STATE_COPY_DEST;
bool textureFilled = false;
constexpr UINT kMaxVertices = 16384;   // 24 hitboxes x 96 segments x 6 + outline + picture fits
std::vector<uint32_t> staging;
std::vector<Vertex> vertices;
MyClientHudFrame::Channel hud;
MyClientHudFrame::Reader reader;
bool failed = false;

Host host{};
void Info(const std::string& text) { if (host.info) host.info(text); }
void Warn(const std::string& text) { if (host.warn) host.warn(text); }

void Fail(const char* what, HRESULT hr) {
    if (failed) return;
    failed = true;
    enabled.store(false);
    char code[16];
    std::snprintf(code, sizeof code, "0x%08X", static_cast<uint32_t>(hr));
    Warn(std::string("In-game drawing stopped: ") + what + " failed (" + code + "); the HUD falls back to its window.");
    if (auto* h = hud.Data()) { h->dllState = MyClientHudFrame::Unsupported; h->dllError = static_cast<uint32_t>(hr); }
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return b;
}
D3D12_HEAP_PROPERTIES HeapOf(D3D12_HEAP_TYPE type) { D3D12_HEAP_PROPERTIES h{}; h.Type = type; return h; }
D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1;
    d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

// The swap chain's real back buffers and index; our own GetBuffer / GetCurrentBackBufferIndex hooks would
// hand out the proxies the game draws into.
HRESULT RealBuffer(IDXGISwapChain* swap, UINT i, ComPtr<ID3D12Resource>& out) {
    out.Reset();
    if (!swap) return E_POINTER;
    return getBufferOrig ? getBufferOrig(swap, i, IID_PPV_ARGS(&out)) : swap->GetBuffer(i, IID_PPV_ARGS(&out));
}
UINT RealIndex(IDXGISwapChain3* swap3) { return indexOrig ? indexOrig(swap3) : swap3->GetCurrentBackBufferIndex(); }

// ---- proxies (proxyMutex: only held for a lookup, never while taking renderMutex) ----
// The game's GetBuffer returns these textures (same size and format as the real back buffers) and its
// GetCurrentBackBufferIndex our own index, so the game never touches a real back buffer: each Present copies
// the proxy it drew into to the real one (or Frame Generation takes it), and the real presents can happen later
// on another thread. Only made for swap chains whose buffers the game takes after the hooks are in (creation or
// ResizeBuffers); one taken earlier keeps the real buffers and the old path.
struct Proxy {
    IDXGISwapChain* swap = nullptr;   // identity only
    std::vector<ComPtr<ID3D12Resource>> buffers;
    UINT index = 0;                   // the proxy the game draws into now
};
std::mutex proxyMutex;
std::vector<Proxy> proxies;
Proxy* FindProxy(IDXGISwapChain* swap) {
    for (auto& p : proxies)
        if (p.swap == swap) return &p;
    return nullptr;
}
void ForgetProxies(IDXGISwapChain* swap) {
    std::lock_guard guard(proxyMutex);
    for (size_t i = 0; i < proxies.size(); i++)
        if (proxies[i].swap == swap) { proxies.erase(proxies.begin() + static_cast<std::ptrdiff_t>(i)); return; }
}

void WaitFor(UINT64 value) {
    if (!fence || !value) return;
    if (fence->GetCompletedValue() < value && SUCCEEDED(fence->SetEventOnCompletion(value, fenceEvent)))
        WaitForSingleObject(fenceEvent, 500);
}
void WaitForGpu() { WaitFor(fenceValue); }   // everything submitted so far (resize, new texture size)

void ReleaseTargets() {
    WaitForGpu();
    MyClientFrameGen::Release();   // its copies are sized for these back buffers
    frames.clear();
    boundSwap = nullptr;
}

const char* kShader = R"(
struct VsIn { float2 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };
struct PsIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };
PsIn vs(VsIn i) { PsIn o; o.pos = float4(i.pos, 0, 1); o.uv = i.uv; o.col = i.col; return o; }
Texture2D picture : register(t0);
SamplerState linearClamp : register(s0);
float4 ps(PsIn i) : SV_Target { return i.uv.x < 0 ? i.col : picture.Sample(linearClamp, i.uv) * i.col; }
)";

bool CreateDeviceObjects() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    HRESULT hr = device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(hr)) { Fail("CreateDescriptorHeap(RTV)", hr); return false; }
    rtvStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    hr = device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap));
    if (FAILED(hr)) { Fail("CreateDescriptorHeap(SRV)", hr); return false; }
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) { Fail("CreateFence", hr); return false; }
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // Root signature: one SRV table (the picture) and a linear/clamp sampler.
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{1, &param, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
    auto serialize = Proc<SerializeRootFn>(d3d12, "D3D12SerializeRootSignature");
    if (!serialize) { Fail("D3D12SerializeRootSignature lookup", E_NOINTERFACE); return false; }
    ComPtr<ID3DBlob> blob, error;
    hr = serialize(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr)) { Fail("D3D12SerializeRootSignature", hr); return false; }
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    if (FAILED(hr)) { Fail("CreateRootSignature", hr); return false; }

    // Shaders, compiled once with the system's d3dcompiler_47.dll.
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = Proc<CompileFn>(compiler, "D3DCompile");
    if (!compile) { Fail("loading d3dcompiler_47.dll", E_NOINTERFACE); return false; }
    hr = compile(kShader, std::strlen(kShader), "myclient", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsBlob, &error);
    if (FAILED(hr)) { Fail("compiling the vertex shader", hr); return false; }
    hr = compile(kShader, std::strlen(kShader), "myclient", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psBlob, &error);
    if (FAILED(hr)) { Fail("compiling the pixel shader", hr); return false; }

    return true;
}

// CPU-written memory of one frame: its vertices and (while the HUD picture is shown) its upload rows.
bool EnsureFrameBuffers(Frame& f, bool needUpload) {
    const auto uploadHeap = HeapOf(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RANGE none{0, 0};
    if (!f.vertexBuffer) {
        const auto vb = BufferDesc(UINT64(kMaxVertices) * sizeof(Vertex));
        HRESULT hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vb, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&f.vertexBuffer));
        if (FAILED(hr)) { Fail("creating a vertex buffer", hr); return false; }
        hr = f.vertexBuffer->Map(0, &none, reinterpret_cast<void**>(&f.vertexBytes));
        if (FAILED(hr)) { Fail("mapping a vertex buffer", hr); return false; }
    }
    const UINT64 size = UINT64(uploadPitch) * textureHeight;
    if (needUpload && f.uploadSize != size) {
        f.upload.Reset(); f.uploadBytes = nullptr; f.uploadSize = 0;
        const auto u = BufferDesc(size);
        HRESULT hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &u, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&f.upload));
        if (FAILED(hr)) { Fail("creating a HUD upload buffer", hr); return false; }
        hr = f.upload->Map(0, &none, reinterpret_cast<void**>(&f.uploadBytes));
        if (FAILED(hr)) { Fail("mapping a HUD upload buffer", hr); return false; }
        f.uploadSize = size;
    }
    return true;
}

bool CreatePipeline(DXGI_FORMAT format) {
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = rootSignature.Get();
    p.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    p.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    auto& blend = p.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;   // premultiplied alpha
    blend.SrcBlend = D3D12_BLEND_ONE; blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    p.SampleMask = UINT_MAX;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.DepthStencilState.DepthEnable = FALSE;
    p.DepthStencilState.StencilEnable = FALSE;
    p.InputLayout = {layout, 3};
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = format;
    p.SampleDesc.Count = 1;
    pipeline.Reset();
    const HRESULT hr = device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline));
    if (FAILED(hr)) { Fail("CreateGraphicsPipelineState", hr); return false; }
    pipelineFormat = format;
    return true;
}

bool Bind(IDXGISwapChain* swap) {
    ReleaseTargets();
    DXGI_SWAP_CHAIN_DESC desc{};
    HRESULT hr = swap->GetDesc(&desc);
    if (FAILED(hr)) { Fail("IDXGISwapChain::GetDesc", hr); return false; }
    if (desc.BufferCount == 0 || desc.BufferCount > 16) { Fail("unexpected back-buffer count", E_UNEXPECTED); return false; }
    frames.resize(desc.BufferCount);
    for (UINT i = 0; i < desc.BufferCount; i++) {
        hr = RealBuffer(swap, i, frames[i].back);
        if (FAILED(hr) || !frames[i].back) { Fail("IDXGISwapChain::GetBuffer", FAILED(hr) ? hr : E_POINTER); return false; }
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames[i].allocator));
        if (FAILED(hr)) { Fail("CreateCommandAllocator", hr); return false; }
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames[i].copyAllocator));
        if (FAILED(hr)) { Fail("CreateCommandAllocator", hr); return false; }
        frames[i].rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        frames[i].rtv.ptr += SIZE_T(i) * rtvStep;
        device->CreateRenderTargetView(frames[i].back.Get(), nullptr, frames[i].rtv);
    }
    const auto backDesc = frames[0].back->GetDesc();
    backWidth = static_cast<UINT>(backDesc.Width); backHeight = backDesc.Height; backFormat = backDesc.Format;
    if (!list) {
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&list));
        if (FAILED(hr)) { Fail("CreateCommandList", hr); return false; }
        list->Close();
    }
    if (!copyList) {
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[0].copyAllocator.Get(), nullptr, IID_PPV_ARGS(&copyList));
        if (FAILED(hr)) { Fail("CreateCommandList", hr); return false; }
        copyList->Close();
    }
    if (pipelineFormat != backFormat && !CreatePipeline(backFormat)) return false;
    // Frame Generation switches V-Sync off by presenting with ALLOW_TEARING, allowed only when the swap
    // chain was made for it and is not in exclusive full screen (else the present fails).
    boundTearing = false;
    DXGI_SWAP_CHAIN_DESC1 desc1{};
    BOOL fullscreen = FALSE;
    {
        ComPtr<IDXGISwapChain1> swap1;
        if (SUCCEEDED(swap->QueryInterface(IID_PPV_ARGS(&swap1))) && swap1 && SUCCEEDED(swap1->GetDesc1(&desc1)) &&
            (desc1.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) && SUCCEEDED(swap->GetFullscreenState(&fullscreen, nullptr)))
            boundTearing = !fullscreen;
    }
    boundSwap = swap;
    // The display's refresh rate: frames presented above it are mostly replaced before they are shown.
    DEVMODEW mode{};
    mode.dmSize = sizeof mode;
    const unsigned hz = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) ? static_cast<unsigned>(mode.dmDisplayFrequency) : 0u;
    char chainFlags[96];
    std::snprintf(chainFlags, sizeof chainFlags, " (swap effect %d, flags 0x%X, display %u Hz)", static_cast<int>(desc1.SwapEffect),
                  static_cast<unsigned>(desc1.Flags), hz);
    Info("In-game drawing ready: " + std::to_string(desc.BufferCount) + " back buffers, " + std::to_string(backWidth) + "x" +
         std::to_string(backHeight) + ", format " + std::to_string(static_cast<int>(backFormat)) +
         ", tearing " + (boundTearing ? "allowed" : "not allowed") + chainFlags +
         ((desc1.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? "" :
          !tearingSupported ? " - the system does not support tearing" :
          tearingChains.load() ? "" : " - the swap chain was made before ALLOW_TEARING could be added") + ".");
    return true;
}

bool EnsureTexture(UINT width, UINT height) {
    if (texture && width == textureWidth && height == textureHeight) return true;
    WaitForGpu();
    texture.Reset();
    D3D12_RESOURCE_DESC t{};
    t.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; t.Width = width; t.Height = height; t.DepthOrArraySize = 1;
    t.MipLevels = 1; t.Format = DXGI_FORMAT_B8G8R8A8_UNORM; t.SampleDesc.Count = 1; t.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const auto defaultHeap = HeapOf(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &t, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&texture));
    if (FAILED(hr)) { Fail("creating the HUD texture", hr); return false; }
    textureState = D3D12_RESOURCE_STATE_COPY_DEST;
    uploadPitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(texture.Get(), &srv, srvHeap->GetCPUDescriptorHandleForHeapStart());
    textureWidth = width; textureHeight = height;
    textureFilled = false;
    reader.have = false;   // the next Take copies the whole picture
    staging.assign(size_t(width) * height, 0);
    return true;
}

// Device objects and back buffers of `swap`, created on first use (renderMutex held).
bool Prepare(IDXGISwapChain* swap) {
    if (failed) return false;
    ID3D12CommandQueue* queue = gameQueue.load();
    if (!queue) {
        // A D3D12 game executes command lists before its first Present; after a few hundred frames
        // without one, the game is drawing with D3D11 and there is nothing to draw into here.
        static int presentsWithoutQueue = 0;
        if (++presentsWithoutQueue > 600) Fail("finding the game's D3D12 command queue (the game may be using D3D11)", E_NOINTERFACE);
        return false;
    }
    if (!device) {
        HRESULT hr = swap->GetDevice(IID_PPV_ARGS(&device));
        if (FAILED(hr)) hr = queue->GetDevice(IID_PPV_ARGS(&device));
        if (FAILED(hr)) { Fail("getting the game's D3D12 device", hr); device.Reset(); return false; }
        if (!CreateDeviceObjects()) return false;
    }
    return swap == boundSwap || Bind(swap);
}

void Render(IDXGISwapChain* swap) {
    if (!enabled.load() || failed) return;
    std::lock_guard guard(renderMutex);
    if (!Prepare(swap)) return;
    ID3D12CommandQueue* queue = gameQueue.load();

    const uint64_t now = GetTickCount64();
    auto* h = hud.Data();
    if (h) {
        h->dllAt = now;
        h->backWidth = backWidth; h->backHeight = backHeight;
        if (h->dllState != MyClientHudFrame::Drawing) h->dllState = MyClientHudFrame::Ready;
    }
    const bool showHud = h && MyClientHudFrame::Fresh(h->hudAt, now) && h->width && h->height;

    Camera cam{}; bool haveCamera = false;
    Matrices mats{}; bool haveMatrices = false; float matPos[3] = {};
    std::vector<MyClientTabList::Box> lineBoxes;
    bool outline = false; int ox = 0, oy = 0, oz = 0;
    {
        std::lock_guard in(inputMutex);
        haveCamera = MyClientHudFrame::Fresh(cameraAt, now, 250);
        cam = camera;
        haveMatrices = MyClientHudFrame::Fresh(matricesAt, now, 250);
        mats = matrices;
        matPos[0] = matricesPos[0]; matPos[1] = matricesPos[1]; matPos[2] = matricesPos[2];
        haveCamera = haveCamera || haveMatrices;
        if (haveCamera && MyClientGame::HitboxRequested() && MyClientHudFrame::Fresh(boxesAt, now, 1000)) {
            lineBoxes = boxes;
            if (boxShifts.size() == lineBoxes.size() * 3)
                for (size_t i = 0; i < lineBoxes.size(); i++)
                    for (int k = 0; k < 3; k++) {
                        lineBoxes[i].min[k] += boxShifts[i * 3 + k];
                        lineBoxes[i].max[k] += boxShifts[i * 3 + k];
                    }
        }
        outline = haveCamera && MyClientGame::OutlineWanted() && hasTarget && MyClientHudFrame::Fresh(targetAt, now, 1000);
        ox = targetX; oy = targetY; oz = targetZ;
    }
    if (!showHud && lineBoxes.empty() && !outline) return;

    ComPtr<IDXGISwapChain3> swap3;
    if (FAILED(swap->QueryInterface(IID_PPV_ARGS(&swap3)))) { Fail("IDXGISwapChain3", E_NOINTERFACE); return; }
    const UINT index = RealIndex(swap3.Get());
    if (index >= frames.size()) return;
    Frame& frame = frames[index];
    // Only this back buffer's previous use must be finished (normally long ago): no stall per frame.
    WaitFor(frame.fenceValue);

    bool copyRows = false;
    uint32_t top = 0, bottom = 0;
    if (showHud) {
        if (!EnsureTexture(h->width, h->height) || !EnsureFrameBuffers(frame, true)) return;
        if (reader.Take(h, staging.data(), top, bottom) && bottom > top) {
            for (uint32_t y = top; y < bottom; y++)
                std::memcpy(frame.uploadBytes + size_t(y) * uploadPitch, staging.data() + size_t(y) * textureWidth, size_t(textureWidth) * 4);
            copyRows = true;
            textureFilled = true;
        }
        h->dllState = MyClientHudFrame::Drawing;
    }

    vertices.clear();
    Builder builder(vertices, static_cast<int>(backWidth), static_cast<int>(backHeight));
    if (haveMatrices) {
        // Camera-relative rendering subtracts the camera position before the view matrix; the game's
        // camera keeps it in mPosition (else take the eye from SetCamera). A view matrix that already
        // carries the world translation needs no origin at all.
        const double pl = std::sqrt(double(matPos[0]) * matPos[0] + double(matPos[1]) * matPos[1] + double(matPos[2]) * matPos[2]);
        const float* o = pl > 0.01 ? matPos : &cam.x;
        const double ol = std::sqrt(double(o[0]) * o[0] + double(o[1]) * o[1] + double(o[2]) * o[2]);
        const double tl = std::sqrt(double(mats.view[12]) * mats.view[12] + double(mats.view[13]) * mats.view[13] +
                                    double(mats.view[14]) * mats.view[14]);
        const bool absolute = ol > 4 && tl > 0.5 * ol;
        for (int i = 0; i < 3; i++) mats.origin[i] = absolute ? 0.f : o[i];
    }
    const Projector projector = haveMatrices ? Projector(mats, static_cast<int>(backWidth), static_cast<int>(backHeight))
                                             : Projector(cam, static_cast<int>(backWidth), static_cast<int>(backHeight));
    const double thickness = backHeight >= 1400 ? 3.0 : 2.0;
    if (!lineBoxes.empty()) {
        uint32_t colors[4];
        MyClientGame::HitboxColors(colors);
        AddHitboxes(builder, projector, lineBoxes.data(), static_cast<uint32_t>(lineBoxes.size()), MyClientGame::HitboxFilter(), thickness, colors);
    }
    if (outline) {
        AddBlockOutline(builder, projector, ox, oy, oz, MyClientGame::OutlineRgb(), thickness);
        outlineDrawnAt.store(now);
    }
    if (showHud && textureFilled) builder.Picture();   // last: the HUD sits on top of the lines
    if (vertices.empty() && !copyRows) return;
    if (vertices.size() > kMaxVertices) vertices.resize(kMaxVertices - kMaxVertices % 6);
    if (!EnsureFrameBuffers(frame, false)) return;
    if (!vertices.empty()) std::memcpy(frame.vertexBytes, vertices.data(), vertices.size() * sizeof(Vertex));

    HRESULT hr = frame.allocator->Reset();
    if (FAILED(hr)) { Fail("ID3D12CommandAllocator::Reset", hr); return; }
    hr = list->Reset(frame.allocator.Get(), pipeline.Get());
    if (FAILED(hr)) { Fail("ID3D12GraphicsCommandList::Reset", hr); return; }

    if (copyRows) {
        if (textureState != D3D12_RESOURCE_STATE_COPY_DEST) {
            const auto b = Transition(texture.Get(), textureState, D3D12_RESOURCE_STATE_COPY_DEST);
            list->ResourceBarrier(1, &b);
        }
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = texture.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = frame.upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint = {DXGI_FORMAT_B8G8R8A8_UNORM, textureWidth, textureHeight, 1, uploadPitch};
        const D3D12_BOX box{0, top, 0, textureWidth, bottom, 1};
        list->CopyTextureRegion(&dst, 0, top, 0, &src, &box);
        const auto b = Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &b);
        textureState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (!vertices.empty()) {
        if (texture && textureState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            const auto b = Transition(texture.Get(), textureState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            list->ResourceBarrier(1, &b);
            textureState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        const auto toTarget = Transition(frame.back.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &toTarget);
        list->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
        const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(backWidth), static_cast<float>(backHeight), 0, 1};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(backWidth), static_cast<LONG>(backHeight)};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        list->SetGraphicsRootSignature(rootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        list->SetDescriptorHeaps(1, heaps);
        list->SetGraphicsRootDescriptorTable(0, srvHeap->GetGPUDescriptorHandleForHeapStart());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VERTEX_BUFFER_VIEW view{frame.vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(vertices.size() * sizeof(Vertex)),
                                            sizeof(Vertex)};
        list->IASetVertexBuffers(0, 1, &view);
        list->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
        const auto toPresent = Transition(frame.back.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        list->ResourceBarrier(1, &toPresent);
    }
    hr = list->Close();
    if (FAILED(hr)) { Fail("ID3D12GraphicsCommandList::Close", hr); return; }
    ID3D12CommandList* lists[] = {list.Get()};
    executeOrig(queue, 1, lists);
    queue->Signal(fence.Get(), ++fenceValue);
    frame.fenceValue = fenceValue;
}

// ---- Frame Generation (FrameGen.cpp makes the pictures, FrameGenPacing.h times them) ----
// Each game frame's Present shows (multiplier - 1) in-between pictures first, then the game frame itself;
// the HUD and the 3D lines are drawn on each of them, after the picture is made.
MyClientFrameGen::Pacer pacer;               // Present thread only
MyClientFrameGen::Counter presents;          // Present thread only
std::atomic<float> presentedFps{0};
std::atomic<uint32_t> frameGenState{MyClientFeatures::FrameGenOff};
std::atomic<uint64_t> presentedAt{0};
std::atomic<uint32_t> presentCount{0};       // every real present, for the Frame Generation timing log
HANDLE waitTimer = nullptr;
bool waitTimerTried = false;

double NowMs() {
    static const double perMs = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart / 1000.0; }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return perMs > 0 ? c.QuadPart / perMs : 0;
}
void CountPresent() {
    const double now = NowMs();
    presents.Event(now);
    presentCount.fetch_add(1);
    presentedFps.store(presents.Value(now));
    presentedAt.store(GetTickCount64());
}
// Where a game frame's time goes (Present thread), logged every 60 s: the game's own work between two
// Presents, our HUD drawing, and the time the real Present blocks (waiting for the display shows up here).
struct PresentTiming {
    double start = 0, lastExit = 0, game = 0, hud = 0, wait = 0;
    uint32_t frames = 0, sync = 0, flags = 0;
    void Frame(double entry, double hudDone, double exit, UINT s, UINT f) {
        if (start <= 0) start = entry;
        if (lastExit > 0 && entry > lastExit) game += entry - lastExit;
        hud += hudDone - entry;
        wait += exit - hudDone;
        lastExit = exit; sync = s; flags = f;
        if (++frames >= 30 && exit - start >= 60000) {
            char text[220];
            std::snprintf(text, sizeof text, "Present timing (60 s): %u frames (%.0f FPS), per frame game=%.2f ms hud=%.2f ms present=%.2f ms (sync %u, flags 0x%X)",
                          frames, frames * 1000.0 / (exit - start), game / frames, hud / frames, wait / frames, sync, flags);
            Info(text);
            *this = {};
            lastExit = exit;
        }
    }
};
PresentTiming presentTiming;   // Present thread only
// Waits `ms` precisely: a high-resolution timer for the bulk, then a short spin. With `stop`, the timer sleeps in
// steps of at most 1 ms and the wait ends early once `stop` turns true.
void WaitPrecise(double ms, const std::atomic<bool>* stop = nullptr) {
    if (ms <= 0) return;
    const double until = NowMs() + ms;
    if (!waitTimerTried) {
        waitTimerTried = true;
        waitTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    }
    for (double bulk = until - 0.5 - NowMs(); waitTimer && bulk > 0 && !(stop && stop->load()); bulk = until - 0.5 - NowMs()) {
        const double step = stop ? std::min(bulk, 1.0) : bulk;
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(step * 10000.0);   // 100 ns units, relative
        if (!SetWaitableTimer(waitTimer, &due, 0, nullptr, nullptr, FALSE)) break;
        WaitForSingleObject(waitTimer, static_cast<DWORD>(step) + 5);
    }
    while (NowMs() < until && !(stop && stop->load())) YieldProcessor();
}

MyClientFrameGen::Gpu FrameGenGpu() {
    return {device.Get(), gameQueue.load(), executeOrig, &Warn};
}
// The current back buffer of `swap` (renderMutex held, Prepare done).
Frame* CurrentFrame(IDXGISwapChain* swap) {
    ComPtr<IDXGISwapChain3> swap3;
    if (FAILED(swap->QueryInterface(IID_PPV_ARGS(&swap3))) || !swap3) return nullptr;
    const UINT index = RealIndex(swap3.Get());
    if (index >= frames.size() || !frames[index].back) return nullptr;
    WaitFor(frames[index].fenceValue);   // the HUD's last list on this buffer
    return &frames[index];
}

// Present may reach Present1 inside DXGI (or the other way round): draw once per presented frame.
thread_local int presentDepth = 0;

// Copies the proxy the game drew into to the current real back buffer (renderMutex held, Prepare done).
bool CopyDrawn(IDXGISwapChain* swap, ID3D12Resource* drawn) {
    ID3D12CommandQueue* queue = gameQueue.load();
    if (!drawn || !queue || !copyList || !fence || !executeOrig) return false;
    const auto d = drawn->GetDesc();
    if (d.Width != backWidth || d.Height != backHeight || d.Format != backFormat) return false;
    Frame* frame = CurrentFrame(swap);
    if (!frame || !frame->back || !frame->copyAllocator) return false;
    WaitFor(frame->copyFenceValue);
    if (FAILED(frame->copyAllocator->Reset()) || FAILED(copyList->Reset(frame->copyAllocator.Get(), nullptr))) return false;
    const D3D12_RESOURCE_BARRIER in[2] = {
        Transition(drawn, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE),
        Transition(frame->back.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)};
    copyList->ResourceBarrier(2, in);
    copyList->CopyResource(frame->back.Get(), drawn);
    const D3D12_RESOURCE_BARRIER out[2] = {
        Transition(drawn, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT),
        Transition(frame->back.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)};
    copyList->ResourceBarrier(2, out);
    if (FAILED(copyList->Close())) return false;
    ID3D12CommandList* lists[] = {copyList.Get()};
    executeOrig(queue, 1, lists);
    queue->Signal(fence.Get(), ++fenceValue);
    frame->copyFenceValue = fenceValue;
    return true;
}

// Safety net when the drawing setup failed (Prepare false): the game draws into proxies, so its picture must still
// reach the screen. Needs only the queue; its own allocators, list and fence (renderMutex held).
struct DirectCopy {
    ComPtr<ID3D12CommandAllocator> allocators[4];
    UINT64 done[4] = {};
    UINT next = 0;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    UINT64 value = 0;
    bool broken = false;
};
DirectCopy direct;
bool CopyDrawnDirect(IDXGISwapChain* swap, ID3D12Resource* drawn) {
    ID3D12CommandQueue* queue = gameQueue.load();
    if (!swap || !drawn || !queue || !executeOrig || direct.broken) return false;
    ComPtr<IDXGISwapChain3> swap3;
    if (FAILED(swap->QueryInterface(IID_PPV_ARGS(&swap3))) || !swap3) return false;
    ComPtr<ID3D12Resource> back;
    if (FAILED(RealBuffer(swap, RealIndex(swap3.Get()), back)) || !back) return false;
    const auto bd = back->GetDesc(), dd = drawn->GetDesc();
    if (bd.Width != dd.Width || bd.Height != dd.Height || bd.Format != dd.Format) return false;
    if (!direct.fence) {
        ComPtr<ID3D12Device> dev;
        bool ok = SUCCEEDED(back->GetDevice(IID_PPV_ARGS(&dev))) && dev &&
                  SUCCEEDED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&direct.fence)));
        for (auto& a : direct.allocators)
            ok = ok && SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)));
        ok = ok && SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, direct.allocators[0].Get(), nullptr,
                                                    IID_PPV_ARGS(&direct.list)));
        if (ok) direct.list->Close();
        if (ok) direct.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!ok || !direct.event) {
            direct.broken = true;
            direct.fence.Reset();
            return false;
        }
    }
    const UINT slot = direct.next;
    direct.next = (slot + 1) % 4;
    if (direct.fence->GetCompletedValue() < direct.done[slot] &&
        SUCCEEDED(direct.fence->SetEventOnCompletion(direct.done[slot], direct.event)))
        WaitForSingleObject(direct.event, 500);
    if (FAILED(direct.allocators[slot]->Reset()) || FAILED(direct.list->Reset(direct.allocators[slot].Get(), nullptr))) return false;
    const D3D12_RESOURCE_BARRIER in[2] = {
        Transition(drawn, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE),
        Transition(back.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)};
    direct.list->ResourceBarrier(2, in);
    direct.list->CopyResource(back.Get(), drawn);
    const D3D12_RESOURCE_BARRIER out[2] = {
        Transition(drawn, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT),
        Transition(back.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)};
    direct.list->ResourceBarrier(2, out);
    if (FAILED(direct.list->Close())) return false;
    ID3D12CommandList* lists[] = {direct.list.Get()};
    executeOrig(queue, 1, lists);
    queue->Signal(direct.fence.Get(), ++direct.value);
    direct.done[slot] = direct.value;
    return true;
}

// ---- Frame Generation presenter thread ----
// With proxies the game's Present only captures its picture and hands a job over: this thread presents the
// in-between pictures and then the game frame, spaced over the game's frame interval, while the game already
// works on its next frame. (Waiting on the game's own thread made every game frame that much longer, so the
// game ran at 1/multiplier of its speed and the presented frame rate did not go up at all.)
struct Job {
    ComPtr<IDXGISwapChain> swap;
    UINT flags = 0, tearing = 0;
    uint32_t multiplier = 1, method = 0;
    double spacingMs = 0;   // between two presents of the job
    double intervalMs = 0;  // the game's own frame time: the job must be done before its next frame
    bool generate = false;  // in-between pictures first (else only the game frame)
};
std::mutex jobMutex;
std::condition_variable jobReady, jobDone;
Job pendingJob;                       // jobMutex
bool hasJob = false, jobBusy = false; // jobMutex
HRESULT presenterResult = S_OK;       // jobMutex; a failed present, handed to the game on its next Present
std::atomic<bool> presenterRunning{false};
std::once_flag presenterOnce;
std::atomic<bool> gameFrameWaiting{false};   // the game's next Present is waiting: show the game frame now
std::atomic<uint32_t> madeSkipped{0};        // made-up frames left out so the game is not held (for the log)
std::atomic<uint32_t> madeFailed{0};         // made-up frames that could not be made (for the log)
std::atomic<uint32_t> jobsPlain{0};          // game frames handed over without made-up frames (for the log)
// Why (for the log): a menu was open (the game's or the HUD's) / the frame came late after the previous one / the
// picture could not be captured.
std::atomic<uint32_t> plainMenu{0}, plainLate{0}, plainCapture{0};
MyClientFrameGen::Cost presenterCost;        // presenter thread only
MyClientFrameGen::Extras presenterExtras;    // presenter thread only
MyClientFrameGen::Tuner presenterTuner;      // presenter thread only
std::atomic<uint32_t> presenterChoice{0};    // made-up frames per game frame chosen last (for the log)

HRESULT RunJob(const Job& job) {
    IDXGISwapChain* swap = job.swap.Get();
    if (!swap || !presentOrig) return S_OK;
    const double start = NowMs();
    if (job.generate) {
        // As many made-up frames as fit (MyClientFrameGen::Extras), spread evenly over the game's frame time.
        const uint32_t extra = presenterTuner.Choose(
            job.multiplier, presenterExtras.Plan(job.multiplier, job.intervalMs, presenterCost, start), start);
        presenterChoice.store(extra);
        if (extra + 1 < job.multiplier) madeSkipped.fetch_add(job.multiplier - 1 - extra);
        const uint32_t steps = extra + 1;
        const double spacing = job.spacingMs > 0 ? job.spacingMs * job.multiplier / steps : 0;
        for (uint32_t k = 1; k <= extra; k++) {
            WaitPrecise(start + (k - 1) * spacing - NowMs(), &gameFrameWaiting);
            if (gameFrameWaiting.load() || !MyClientFrameGen::Fits(NowMs(), start, job.intervalMs, presenterCost)) {
                madeSkipped.fetch_add(steps - k);
                break;
            }
            const double madeStart = NowMs();
            bool made = false;
            {
                std::lock_guard guard(renderMutex);
                Frame* frame = Prepare(swap) ? CurrentFrame(swap) : nullptr;
                made = frame && MyClientFrameGen::Generate(FrameGenGpu(), frame->back.Get(), frame->rtv,
                                                           MyClientFrameGen::FramePhase(k, steps), job.method);
            }
            if (!made) { madeFailed.fetch_add(1); break; }
            Render(swap);
            const HRESULT hr = presentOrig(swap, 0u, job.flags | job.tearing);
            CountPresent();
            presenterTuner.Present();
            presenterCost.Made(NowMs(), NowMs() - madeStart);
            if (FAILED(hr)) return hr;
        }
        // The game frame goes at its slot, or at once when the game's next Present is already waiting.
        const double until = start + extra * spacing;
        WaitPrecise(until - NowMs(), &gameFrameWaiting);
    }
    const double realStart = NowMs();
    bool restored = false;
    {
        std::lock_guard guard(renderMutex);
        Frame* frame = Prepare(swap) ? CurrentFrame(swap) : nullptr;
        restored = frame && MyClientFrameGen::Restore(FrameGenGpu(), frame->back.Get());
    }
    if (!restored) return S_OK;   // Frame Generation failed: the next Present copies the game's picture itself
    Render(swap);
    const HRESULT hr = presentOrig(swap, 0u, job.flags | job.tearing);   // V-Sync off while Frame Generation is on
    CountPresent();
    if (job.generate) presenterTuner.Present();
    MyClientFrameGen::Cost::Note(presenterCost.real, NowMs() - realStart);
    return hr;
}
void PresenterLoop() {
    presentDepth = 1;   // DXGI may call our Present1 hook from inside Present: it must go straight through
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);   // the spacing needs a prompt wake-up
    for (;;) {
        Job job;
        {
            std::unique_lock lock(jobMutex);
            jobReady.wait(lock, [] { return hasJob; });
            job = std::move(pendingJob);
            pendingJob = Job{};
            hasJob = false;
        }
        const HRESULT hr = RunJob(job);
        job.swap.Reset();   // before going idle: the game may release its swap chain right after
        {
            std::lock_guard lock(jobMutex);
            if (FAILED(hr)) presenterResult = hr;
            jobBusy = false;
        }
        jobDone.notify_all();
    }
}
// Waits until the presenter has shown everything it was given (game thread: Present, ResizeBuffers). False if it is
// still busy after a second; the caller must then not touch what the job uses. `failure` takes its failed present.
bool WaitPresenterIdle(HRESULT* failure = nullptr) {
    std::unique_lock lock(jobMutex);
    const bool idle = jobDone.wait_for(lock, std::chrono::seconds(1), [] { return !jobBusy; });
    if (failure) { *failure = presenterResult; presenterResult = S_OK; }
    return idle;
}
bool StartJob(Job&& job) {
    std::call_once(presenterOnce, [] {
        try {
            std::thread(PresenterLoop).detach();
            presenterRunning.store(true);
        } catch (...) {
            Warn("Frame Generation: its present thread could not be started.");
        }
    });
    if (!presenterRunning.load()) return false;
    {
        std::lock_guard lock(jobMutex);
        pendingJob = std::move(job);
        hasJob = true;
        jobBusy = true;
    }
    gameFrameWaiting.store(false);
    jobReady.notify_one();
    return true;
}

// Where a game frame's time goes with Frame Generation (game thread), logged every 10 s: the game's own work,
// waiting for the presenter to finish the previous frame, and capturing the picture.
struct FrameGenTiming {
    double start = 0, lastExit = 0, game = 0, wait = 0, capture = 0;
    uint32_t frames = 0, presentsAtStart = 0;
    void Frame(double entry, double waited, double exit, uint32_t multiplier) {
        // Frame Generation was off (out of a world, paused) since the last frame: start a new window, or that
        // time counted as one game frame (2026-10-28 log: game=236 ms, 137696 presented).
        if (lastExit > 0 && entry - lastExit > 1000) *this = {};
        if (start <= 0) { start = entry; presentsAtStart = presentCount.load(); }
        if (lastExit > 0 && entry > lastExit) game += entry - lastExit;
        wait += waited - entry;
        capture += exit - waited;
        lastExit = exit;
        if (++frames >= 30 && exit - start >= 10000) {
            char text[400];
            std::snprintf(text, sizeof text,
                          "Frame Generation timing (10 s): %u game frames, %u presented (x%u, %u made-up skipped, %u failed, "
                          "%u game frames without any: %u menu, %u late, %u not captured, now %u made-up per game frame), per game "
                          "frame game=%.2f ms waitPresenter=%.2f ms capture=%.2f ms, made=%.2f ms real=%.2f ms",
                          frames, presentCount.load() - presentsAtStart, multiplier, madeSkipped.exchange(0),
                          madeFailed.exchange(0), jobsPlain.exchange(0), plainMenu.exchange(0), plainLate.exchange(0),
                          plainCapture.exchange(0), presenterChoice.load(), game / frames,
                          wait / frames, capture / frames, presenterCost.made, presenterCost.real);
            Info(text);
            *this = {};
            lastExit = exit;
        }
    }
};
FrameGenTiming frameGenTiming;   // game thread only

// A game frame drawn into the proxy `drawn`. Frame Generation on: capture it and let the presenter show it.
// Off (or it cannot run): copy it into the real back buffer and present it here, as the game asked.
template <class PresentCall>
HRESULT PresentProxied(IDXGISwapChain* swap, UINT sync, UINT flags, ID3D12Resource* drawn, PresentCall present) {
    const double entry = NowMs();
    HRESULT failure = S_OK;
    gameFrameWaiting.store(true);   // a presenter still on the previous frame skips to its game frame
    const bool idle = WaitPresenterIdle(&failure);
    gameFrameWaiting.store(false);
    if (!idle) return S_OK;   // stuck in a present: drop this frame rather than race the presenter
    if (FAILED(failure)) return failure;            // e.g. the device was removed: the game must hear of it
    const double waited = NowMs();
    bool paused = false;
    const uint32_t field = MyClientGame::FrameGenField(paused);
    const uint32_t multiplier = MyClientGame::FrameGenMultiplier(field);
    const uint32_t method = MyClientFeatures::FrameGenMethodOf(field);
    const bool on = multiplier >= 2 && enabled.load() && !failed && !MyClientFrameGen::Failed();
    if (on) {
        pacer.Frame(entry);
        pacer.Held(waited - entry);
        Job job;
        bool ready = false;
        {
            std::lock_guard guard(renderMutex);
            const auto gpu = FrameGenGpu();
            if (Prepare(swap) && MyClientFrameGen::Ensure(gpu, backWidth, backHeight, backFormat)) {
                const bool late = !pacer.CanGenerate();
                const bool usable = !late && !paused;
                if (!usable) MyClientFrameGen::Forget();   // after a pause or a hitch, start again from this frame
                const bool captured = MyClientFrameGen::Capture(gpu, drawn, method == MyClientFeatures::FrameGenMotion);
                job.generate = captured && usable;
                if (paused) plainMenu.fetch_add(1);
                else if (late) plainLate.fetch_add(1);
                else if (!captured) plainCapture.fetch_add(1);
                ready = !MyClientFrameGen::Failed();
            }
            if (swap == boundSwap && boundTearing) job.tearing = DXGI_PRESENT_ALLOW_TEARING;
        }
        if (ready) {
            static bool told = false;
            if (!told) {
                told = true;
                Info("Frame Generation: extra frames are presented from their own thread (the game is not held).");
            }
            const bool generate = job.generate;
            if (!generate) jobsPlain.fetch_add(1);
            job.swap = swap;
            job.flags = flags;
            job.multiplier = multiplier;
            job.method = method;
            job.spacingMs = generate ? pacer.WaitMs(multiplier, MyClientFeatures::FrameGenPacingOf(field), 0u) : 0;
            job.intervalMs = pacer.AverageMs();
            if (StartJob(std::move(job))) {
                frameGenState.store(generate ? MyClientFeatures::FrameGenRunning : MyClientFeatures::FrameGenPaused);
                frameGenTiming.Frame(entry, waited, NowMs(), multiplier);
                return S_OK;
            }
        }
    }
    frameGenState.store(on ? MyClientFeatures::FrameGenUnavailable : MyClientFeatures::FrameGenOff);
    if (pacer.AverageMs() > 0) { pacer.Reset(); std::lock_guard guard(renderMutex); MyClientFrameGen::Forget(); }
    UINT tearing = 0;
    bool copied = false;
    {
        std::lock_guard guard(renderMutex);
        copied = Prepare(swap) && CopyDrawn(swap, drawn);
        if (!copied) copied = CopyDrawnDirect(swap, drawn);
        // The game's own V-Sync off (sync 0) only reaches past the refresh rate in a window with ALLOW_TEARING.
        if (sync == 0 && swap == boundSwap && boundTearing) tearing = DXGI_PRESENT_ALLOW_TEARING;
    }
    static bool toldCopy = false;
    if (!copied && !toldCopy) {
        toldCopy = true;
        Warn("In-game drawing: the game's picture could not be copied to the screen (one frame shows an old picture).");
    }
    Render(swap);
    const double hudDone = NowMs();
    const HRESULT hr = present(sync, tearing);
    presentTiming.Frame(entry, hudDone, NowMs(), sync, flags);
    CountPresent();
    return hr;
}

// One game frame's Present: `present(sync, flags)` presents the current back buffer with the game's flags plus
// `flags`. Frame Generation needs the game to draw into proxies (PresentProxied). Without them the extra frames
// could only go out by holding the game's thread between them, which left the game slower than with Frame
// Generation off (138 -> 52 FPS), so it then stays off and the HUD shows it as unavailable.
template <class PresentCall>
HRESULT PresentFrame(IDXGISwapChain* swap, UINT sync, UINT flags, PresentCall present) {
    ComPtr<ID3D12Resource> drawn;   // the proxy the game drew this frame into, when it draws into proxies
    {
        std::lock_guard guard(proxyMutex);
        Proxy* p = FindProxy(swap);
        if (p && !p->buffers.empty()) {
            const UINT i = p->index < p->buffers.size() ? p->index : 0u;
            drawn = p->buffers[i];
            p->index = (i + 1) % static_cast<UINT>(p->buffers.size());
        }
    }
    if (drawn) return PresentProxied(swap, sync, flags, drawn.Get(), present);
    WaitPresenterIdle();   // the presenter may still be showing another swap chain's last frame
    bool paused = false;
    const uint32_t field = MyClientGame::FrameGenField(paused);
    const bool on = MyClientGame::FrameGenMultiplier(field) >= 2 && enabled.load() && !failed;
    frameGenState.store(on ? MyClientFeatures::FrameGenUnavailable : MyClientFeatures::FrameGenOff);
    static bool toldNoProxies = false;
    if (on && !toldNoProxies) {
        toldNoProxies = true;
        Info("Frame Generation unavailable: the game took its back buffers before MyClient's hooks. Resizing the game "
             "window once (or F11 twice) turns it on.");
    }
    if (pacer.AverageMs() > 0) { pacer.Reset(); std::lock_guard guard(renderMutex); MyClientFrameGen::Forget(); }
    // The game's own V-Sync off (sync 0) only reaches past the refresh rate in a window with ALLOW_TEARING.
    UINT tearing = 0;
    if (sync == 0) {
        std::lock_guard guard(renderMutex);
        if (swap == boundSwap && boundTearing) tearing = DXGI_PRESENT_ALLOW_TEARING;
    }
    const double entry = NowMs();
    Render(swap);
    const double hudDone = NowMs();
    const HRESULT hr = present(sync, tearing);
    presentTiming.Frame(entry, hudDone, NowMs(), sync, flags);
    CountPresent();
    return hr;
}

// Debug Menu: the GPU and DirectX version of the game's swap chain, looked up once at its first frame.
std::mutex gpuMutex;
MyClientCompositor::Gpu gpu{};
std::atomic<bool> gpuLooked{false}, gpuKnown{false};
void NoteGpu(IDXGISwapChain* swap) {
    if (!swap || gpuLooked.exchange(true)) return;
    MyClientCompositor::Gpu found{};
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D11Device> device11;
    if (SUCCEEDED(swap->GetDevice(IID_PPV_ARGS(&queue))) && queue) {   // a D3D12 swap chain hands out its queue
        ComPtr<ID3D12Device> device;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) return;
        found.directX = 12;
        const D3D_FEATURE_LEVEL levels[] = {(D3D_FEATURE_LEVEL)0xc200, (D3D_FEATURE_LEVEL)0xc100, (D3D_FEATURE_LEVEL)0xc000,
                                            (D3D_FEATURE_LEVEL)0xb100, (D3D_FEATURE_LEVEL)0xb000};
        D3D12_FEATURE_DATA_FEATURE_LEVELS supported{};
        supported.NumFeatureLevels = (UINT)(sizeof(levels) / sizeof(levels[0]));
        supported.pFeatureLevelsRequested = levels;
        // An older runtime rejects 12_2 in the list: ask again without it.
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &supported, sizeof(supported)))) {
            supported.NumFeatureLevels--; supported.pFeatureLevelsRequested = levels + 1;
            if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &supported, sizeof(supported))))
                supported.MaxSupportedFeatureLevel = (D3D_FEATURE_LEVEL)0;
        }
        found.featureLevel = (uint32_t)supported.MaxSupportedFeatureLevel;
        const LUID luid = device->GetAdapterLuid();
        auto factoryFn = Proc<CreateFactoryFn>(GetModuleHandleW(L"dxgi.dll"), "CreateDXGIFactory1");
        ComPtr<IDXGIFactory4> factory;
        if (factoryFn && SUCCEEDED(factoryFn(IID_PPV_ARGS(&factory))) && factory)
            factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
    } else if (SUCCEEDED(swap->GetDevice(IID_PPV_ARGS(&device11))) && device11) {
        found.directX = 11;
        found.featureLevel = (uint32_t)device11->GetFeatureLevel();
        ComPtr<IDXGIDevice> dxgi;
        if (SUCCEEDED(device11.As(&dxgi)) && dxgi) dxgi->GetAdapter(&adapter);
    }
    DXGI_ADAPTER_DESC desc{};
    if (!adapter || FAILED(adapter->GetDesc(&desc))) return;
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, found.name, sizeof found.name - 1, nullptr, nullptr);
    found.videoMb = (uint32_t)(desc.DedicatedVideoMemory >> 20);
    found.vendor = desc.VendorId;
    LARGE_INTEGER driver{};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver))) found.driver = (uint64_t)driver.QuadPart;
    {
        std::lock_guard guard(gpuMutex);
        gpu = found;
    }
    gpuKnown.store(true);
    Info(std::string("Debug Menu: the game draws with ") + found.name + ", DirectX " + std::to_string(found.directX) +
         " (feature level 0x" + [&]() { char hex[16]; std::snprintf(hex, sizeof hex, "%x", found.featureLevel); return std::string(hex); }() +
         ", " + std::to_string(found.videoMb) + " MB).");
}

HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* swap, UINT sync, UINT flags) {
    NoteGpu(swap);
    HRESULT hr;
    if (presentDepth++ == 0 && !(flags & DXGI_PRESENT_TEST))
        hr = PresentFrame(swap, sync, flags, [&](UINT interval, UINT more) { return presentOrig(swap, interval, flags | more); });
    else
        hr = presentOrig(swap, sync, flags);
    presentDepth--;
    return hr;
}
HRESULT STDMETHODCALLTYPE Present1Detour(IDXGISwapChain1* swap, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* params) {
    NoteGpu(swap);
    HRESULT hr;
    if (presentDepth++ == 0 && !(flags & DXGI_PRESENT_TEST))
        hr = PresentFrame(swap, sync, flags, [&](UINT interval, UINT more) { return present1Orig(swap, interval, flags | more, params); });
    else
        hr = present1Orig(swap, sync, flags, params);
    presentDepth--;
    return hr;
}
// Our copies of `swap`'s back buffers, made the first time the game asks for one after the swap chain was made or
// resized; null (the game gets the real buffer) when they cannot be made.
ComPtr<ID3D12Resource> MakeProxies(IDXGISwapChain* swap, UINT index) {
    // The queue that copies the proxies to the screen must be known, or the game's picture would never get there.
    if (!swap || !enabled.load() || failed || !gameQueue.load()) return nullptr;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap->GetDesc(&desc)) || desc.BufferCount == 0 || desc.BufferCount > 16 || index >= desc.BufferCount) return nullptr;
    ComPtr<ID3D12Resource> real;
    if (FAILED(RealBuffer(swap, 0, real)) || !real) return nullptr;   // not a D3D12 swap chain
    const D3D12_RESOURCE_DESC rd = real->GetDesc();
    ComPtr<ID3D12Device> dev;
    if (FAILED(real->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return nullptr;
    real.Reset();
    Proxy made;
    made.swap = swap;
    const auto heap = HeapOf(D3D12_HEAP_TYPE_DEFAULT);
    for (UINT i = 0; i < desc.BufferCount; i++) {
        ComPtr<ID3D12Resource> buffer;
        const HRESULT hr = dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PRESENT, nullptr,
                                                        IID_PPV_ARGS(&buffer));
        if (FAILED(hr) || !buffer) {
            char code[16];
            std::snprintf(code, sizeof code, "0x%08X", static_cast<uint32_t>(hr));
            Warn(std::string("In-game drawing: copies of the back buffers could not be made (") + code + "); the game keeps the real ones.");
            return nullptr;
        }
        made.buffers.push_back(buffer);
    }
    ComPtr<ID3D12Resource> out = made.buffers[index];
    {
        std::lock_guard guard(proxyMutex);
        if (Proxy* p = FindProxy(swap)) return index < p->buffers.size() ? p->buffers[index] : nullptr;   // made meanwhile
        if (proxies.size() >= 4) proxies.erase(proxies.begin());   // swap chains the game has dropped
        proxies.push_back(std::move(made));
    }
    Info("In-game drawing: the game draws into MyClient's copies of its " + std::to_string(desc.BufferCount) + " back buffers (" +
         std::to_string(rd.Width) + "x" + std::to_string(rd.Height) + "), so Frame Generation does not hold the game.");
    return out;
}
HRESULT STDMETHODCALLTYPE GetBufferDetour(IDXGISwapChain* swap, UINT index, REFIID riid, void** out) {
    if (!swap || !out) return getBufferOrig(swap, index, riid, out);
    ComPtr<ID3D12Resource> proxy, first;
    size_t count = 0;
    bool known = false;
    {
        std::lock_guard guard(proxyMutex);
        if (Proxy* p = FindProxy(swap); p && !p->buffers.empty()) {
            known = true;
            count = p->buffers.size();
            first = p->buffers[0];
            if (index < count) proxy = p->buffers[index];
        }
    }
    if (known && first) {
        // A swap chain made again at the same address (or resized past our hook): its old proxies no longer fit.
        DXGI_SWAP_CHAIN_DESC d{};
        const auto rd = first->GetDesc();
        if (SUCCEEDED(swap->GetDesc(&d)) && (d.BufferCount != count || d.BufferDesc.Width != rd.Width || d.BufferDesc.Height != rd.Height)) {
            ForgetProxies(swap);
            known = false;
            proxy.Reset();
        }
    }
    if (!known && proxyHooks) proxy = MakeProxies(swap, index);
    if (proxy) {
        *out = nullptr;
        if (SUCCEEDED(proxy->QueryInterface(riid, out)) && *out) return S_OK;
    }
    return getBufferOrig(swap, index, riid, out);
}
UINT STDMETHODCALLTYPE IndexDetour(IDXGISwapChain3* swap) {
    {
        std::lock_guard guard(proxyMutex);
        if (Proxy* p = FindProxy(swap)) return p->index;
    }
    return indexOrig(swap);
}
// A swap chain made with ALLOW_TEARING must keep it in every ResizeBuffers; the game asks with its own flags.
UINT KeepTearing(IDXGISwapChain* swap, UINT flags) {
    if (!swap) return flags;
    ComPtr<IDXGISwapChain1> swap1;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    if (SUCCEEDED(swap->QueryInterface(IID_PPV_ARGS(&swap1))) && swap1 && SUCCEEDED(swap1->GetDesc1(&desc)) &&
        (desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))
        flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    return flags;
}
// The game asks for its buffers again after a resize: new proxies are made then, at the new size.
HRESULT STDMETHODCALLTYPE ResizeDetour(IDXGISwapChain* swap, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags) {
    WaitPresenterIdle();
    ForgetProxies(swap);
    { std::lock_guard guard(renderMutex); ReleaseTargets(); }   // our back-buffer references would make the resize fail
    return resizeOrig(swap, count, w, h, format, KeepTearing(swap, flags));
}
HRESULT STDMETHODCALLTYPE Resize1Detour(IDXGISwapChain3* swap, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags,
                                        const UINT* mask, IUnknown* const* queues) {
    WaitPresenterIdle();
    ForgetProxies(swap);
    { std::lock_guard guard(renderMutex); ReleaseTargets(); }
    return resize1Orig(swap, count, w, h, format, KeepTearing(swap, flags), mask, queues);
}

// The game's swap chain is made with ALLOW_TEARING added, so presents with V-Sync off are not held until the
// display refreshes (Windows otherwise caps a windowed game at the refresh rate). Only flip-model chains take
// the flag; if the system refuses it the chain is made exactly as the game asked.
bool WantsTearing(DXGI_SWAP_EFFECT effect, UINT flags) {
    return tearingSupported && !(flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) &&
           (effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || effect == DXGI_SWAP_EFFECT_FLIP_DISCARD);
}
// A D3D12 swap chain is made on the queue that presents it, the game's direct queue: known before its first
// ExecuteCommandLists, so its first back buffers can already be proxies.
void NoteQueue(IUnknown* maybeQueue) {
    if (!maybeQueue || gameQueue.load()) return;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(maybeQueue->QueryInterface(IID_PPV_ARGS(&queue))) || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
    ID3D12CommandQueue* expected = nullptr;
    ID3D12CommandQueue* raw = queue.Detach();
    if (!gameQueue.compare_exchange_strong(expected, raw)) raw->Release();
}
template <class Desc, class Make>
HRESULT MakeChain(const Desc* desc, Make make) {
    if (!desc || !WantsTearing(desc->SwapEffect, desc->Flags)) return make(desc);
    Desc changed = *desc;
    changed.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    const HRESULT hr = make(&changed);
    if (SUCCEEDED(hr)) {
        if (tearingChains.fetch_add(1) == 0) Info("Swap chain made with ALLOW_TEARING added (V-Sync can be switched off in a window).");
        return hr;
    }
    char code[16];
    std::snprintf(code, sizeof code, "0x%08X", static_cast<uint32_t>(hr));
    Warn(std::string("Swap chain with ALLOW_TEARING refused (") + code + "); made as the game asked.");
    return make(desc);
}
void HookFromGameChain(IUnknown* chain, IUnknown* device);
thread_local bool probing = false;   // this thread is making the throw-away swap chain of FindTargets

HRESULT STDMETHODCALLTYPE CreateChainDetour(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out) {
    if (probing) return createChainOrig(factory, device, desc, out);
    NoteQueue(device);
    HRESULT hr;
    if (!desc) {
        hr = createChainOrig(factory, device, desc, out);
    } else {
        // Legacy desc: its swap effect lives in SwapEffect, flags in Flags, same as DESC1.
        hr = MakeChain(static_cast<const DXGI_SWAP_CHAIN_DESC*>(desc), [&](const DXGI_SWAP_CHAIN_DESC* d) {
            return createChainOrig(factory, device, const_cast<DXGI_SWAP_CHAIN_DESC*>(d), out);
        });
    }
    if (SUCCEEDED(hr) && out) HookFromGameChain(*out, device);
    return hr;
}
HRESULT STDMETHODCALLTYPE CreateHwndDetour(IDXGIFactory2* factory, IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* desc,
                                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output, IDXGISwapChain1** out) {
    if (probing) return createHwndOrig(factory, device, window, desc, fullscreen, output, out);
    NoteQueue(device);
    const HRESULT hr =
        MakeChain(desc, [&](const DXGI_SWAP_CHAIN_DESC1* d) { return createHwndOrig(factory, device, window, d, fullscreen, output, out); });
    if (SUCCEEDED(hr) && out) HookFromGameChain(*out, device);
    return hr;
}
HRESULT STDMETHODCALLTYPE CreateCoreDetour(IDXGIFactory2* factory, IUnknown* device, IUnknown* window, const DXGI_SWAP_CHAIN_DESC1* desc,
                                           IDXGIOutput* output, IDXGISwapChain1** out) {
    if (probing) return createCoreOrig(factory, device, window, desc, output, out);
    NoteQueue(device);
    const HRESULT hr = MakeChain(desc, [&](const DXGI_SWAP_CHAIN_DESC1* d) { return createCoreOrig(factory, device, window, d, output, out); });
    if (SUCCEEDED(hr) && out) HookFromGameChain(*out, device);
    return hr;
}
HRESULT STDMETHODCALLTYPE CreateCompDetour(IDXGIFactory2* factory, IUnknown* device, const DXGI_SWAP_CHAIN_DESC1* desc,
                                           IDXGIOutput* output, IDXGISwapChain1** out) {
    if (probing) return createCompOrig(factory, device, desc, output, out);
    NoteQueue(device);
    const HRESULT hr = MakeChain(desc, [&](const DXGI_SWAP_CHAIN_DESC1* d) { return createCompOrig(factory, device, d, output, out); });
    if (SUCCEEDED(hr) && out) HookFromGameChain(*out, device);
    return hr;
}
void STDMETHODCALLTYPE ExecuteDetour(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    if (!gameQueue.load() && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        ID3D12CommandQueue* expected = nullptr;
        queue->AddRef();
        if (!gameQueue.compare_exchange_strong(expected, queue)) queue->Release();
    }
    executeOrig(queue, count, lists);
}

LRESULT CALLBACK DummyProc(HWND w, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(w, m, a, b); }

std::string Hex(HRESULT hr) {
    char code[16];
    std::snprintf(code, sizeof code, "0x%08X", static_cast<uint32_t>(hr));
    return code;
}

bool TearingOf(IDXGIFactory2* factory) {
    ComPtr<IDXGIFactory5> factory5;
    BOOL allow = FALSE;
    return factory && SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5))) && factory5 &&
           SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow)) && allow;
}

// The swap chain creation functions, shared by every DXGI factory of the process.
void FactoryTargetsOf(IDXGIFactory2* factory) {
    if (!factory) return;
    tearingSupported = TearingOf(factory);
    void** factoryTable = *reinterpret_cast<void***>(factory);
    createChainTarget = factoryTable[10];   // IDXGIFactory::CreateSwapChain
    createHwndTarget = factoryTable[15];    // IDXGIFactory2::CreateSwapChainForHwnd
    createCoreTarget = factoryTable[16];    // IDXGIFactory2::CreateSwapChainForCoreWindow
    createCompTarget = factoryTable[24];    // IDXGIFactory2::CreateSwapChainForComposition
}

bool HookCreation(IUnknown* made, const char* from);

// A factory of our own: needs dxgi.dll only, no device. At mod load even this can fail (LeviLamina loads mods
// where DXGI refuses to make one), then the factory creation exports are hooked instead (HookFactoryExports).
bool FindFactoryTargets(std::string& why) {
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    auto factoryFn = Proc<CreateFactoryFn>(dxgi, "CreateDXGIFactory1");
    if (!factoryFn) { why = "dxgi.dll not available"; return false; }
    ComPtr<IDXGIFactory2> factory;
    if (const HRESULT hr = factoryFn(IID_PPV_ARGS(&factory)); FAILED(hr) || !factory) {
        why = "CreateDXGIFactory1 failed (" + Hex(hr) + ")";
        return false;
    }
    if (!HookCreation(factory.Get(), "a DXGI factory of MyClient's")) { why = "the creation functions could not be hooked"; return false; }
    return true;
}

// The functions every swap chain and direct queue of the process share, from one of each.
void ChainTargetsOf(IDXGISwapChain3* swap, ID3D12CommandQueue* queue) {
    if (!swap || !queue) return;
    void** swapTable = *reinterpret_cast<void***>(swap);
    presentTarget = swapTable[8];     // IDXGISwapChain::Present
    getBufferTarget = swapTable[9];   // IDXGISwapChain::GetBuffer
    indexTarget = swapTable[36];      // IDXGISwapChain3::GetCurrentBackBufferIndex
    resizeTarget = swapTable[13];     // IDXGISwapChain::ResizeBuffers
    present1Target = swapTable[22];   // IDXGISwapChain1::Present1
    resize1Target = swapTable[39];    // IDXGISwapChain3::ResizeBuffers1
    void** queueTable = *reinterpret_cast<void***>(queue);
    executeTarget = queueTable[10];   // ID3D12CommandQueue::ExecuteCommandLists
}

// The same addresses from a throw-away device and swap chain, for a game that made its swap chain before the
// creation hooks went in.
bool FindTargets(std::string& why) {
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll"), dxgi = LoadLibraryW(L"dxgi.dll");
    auto create = Proc<CreateDeviceFn>(d3d12, "D3D12CreateDevice");
    auto factoryFn = Proc<CreateFactoryFn>(dxgi, "CreateDXGIFactory1");
    if (!create || !factoryFn) { why = "d3d12.dll / dxgi.dll not available"; return false; }
    ComPtr<ID3D12Device> dummyDevice;
    if (const HRESULT hr = create(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dummyDevice)); FAILED(hr)) {
        why = "D3D12CreateDevice failed (" + Hex(hr) + ")";
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> dummyQueue;
    if (FAILED(dummyDevice->CreateCommandQueue(&qd, IID_PPV_ARGS(&dummyQueue)))) { why = "CreateCommandQueue failed"; return false; }
    ComPtr<IDXGIFactory2> factory;
    if (const HRESULT hr = factoryFn(IID_PPV_ARGS(&factory)); FAILED(hr) || !factory) {
        why = "CreateDXGIFactory1 failed (" + Hex(hr) + ")";
        return false;
    }
    tearingSupported = TearingOf(factory.Get());   // FindFactoryTargets may never have run
    WNDCLASSW wc{};
    wc.lpfnWndProc = DummyProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"MyClientCompositorProbe";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) { why = "probe window"; return false; }
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 64; sd.Height = 64; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swap;
    probing = true;   // the creation hooks must not take this swap chain (or its queue) for the game's
    const HRESULT hr = factory->CreateSwapChainForHwnd(dummyQueue.Get(), window, &sd, nullptr, nullptr, &swap);
    probing = false;
    ComPtr<IDXGISwapChain3> swap3;
    if (SUCCEEDED(hr) && SUCCEEDED(swap.As(&swap3)) && swap3) ChainTargetsOf(swap3.Get(), dummyQueue.Get());
    swap3.Reset();
    swap.Reset();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (FAILED(hr)) { why = "CreateSwapChainForHwnd failed"; return false; }
    if (!presentTarget) { why = "the probe swap chain has no IDXGISwapChain3"; return false; }
    return true;
}

template <class Fn>
bool Hook(void* target, Fn detour, Fn& original) {
    return target && host.hook && host.hook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original)) == 0;
}

std::recursive_mutex startMutex;   // Start (game threads at load and enable) and the creation hooks (the game's render thread)
bool started = false;              // shared memory opened and the creation functions hooked (or tried)
bool chainHooks = false;           // the swap chain creation functions are hooked
bool hooking = false;              // the swap chain hooks went in (some may have failed): never hook twice
std::atomic<bool> creationTried{false};   // HookCreation ran (hooked or failed): the first factory is enough
bool exportHooks = false;          // the DXGI factory creation exports are hooked (no factory could be made at load)

// The creation functions of `made` (any DXGI factory) are hooked: ALLOW_TEARING is added to every swap chain made
// from now on, and the game's first one brings the other hooks in.
bool HookCreation(IUnknown* made, const char* from) {
    if (!made) return false;
    std::lock_guard guard(startMutex);
    if (creationTried.load()) return chainHooks;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(made->QueryInterface(IID_PPV_ARGS(&factory))) || !factory) return false;   // an old factory: wait for the next
    creationTried.store(true);
    FactoryTargetsOf(factory.Get());
    const bool a = Hook(createChainTarget, &CreateChainDetour, createChainOrig);
    const bool b = Hook(createHwndTarget, &CreateHwndDetour, createHwndOrig);
    const bool c = Hook(createCoreTarget, &CreateCoreDetour, createCoreOrig);
    const bool d = Hook(createCompTarget, &CreateCompDetour, createCompOrig);
    chainHooks = a || b || c || d;
    if (!chainHooks) Warn(std::string("In-game drawing: swap chain creation could not be hooked (from ") + from + ").");
    else if (enabled.load() || hooking) Info(std::string("In-game drawing: swap chain creation hooked from ") + from + " (after the game's swap chain).");
    else Info(std::string("In-game drawing: swap chain creation hooked from ") + from + "; waiting for the game to make its swap chain.");
    return chainHooks;
}

using CreateFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
CreateFactoryFn factory0Orig = nullptr, factory1Orig = nullptr;
CreateFactory2Fn factory2Orig = nullptr;
HRESULT WINAPI Factory0Detour(REFIID riid, void** out) {
    const HRESULT hr = factory0Orig(riid, out);
    if (SUCCEEDED(hr) && out && *out && !creationTried.load()) HookCreation(static_cast<IUnknown*>(*out), "the first DXGI factory made");
    return hr;
}
HRESULT WINAPI Factory1Detour(REFIID riid, void** out) {
    const HRESULT hr = factory1Orig(riid, out);
    if (SUCCEEDED(hr) && out && *out && !creationTried.load()) HookCreation(static_cast<IUnknown*>(*out), "the first DXGI factory made");
    return hr;
}
HRESULT WINAPI Factory2Detour(UINT flags, REFIID riid, void** out) {
    const HRESULT hr = factory2Orig(flags, riid, out);
    if (SUCCEEDED(hr) && out && *out && !creationTried.load()) HookCreation(static_cast<IUnknown*>(*out), "the first DXGI factory made");
    return hr;
}

// No factory can be made at load: the game's own first factory (it needs one for its swap chain) brings the
// creation hooks in. Only the export addresses are taken here, nothing in DXGI is called.
bool HookFactoryExports() {
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) return false;
    const bool a = Hook(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory")), &Factory0Detour, factory0Orig);
    const bool b = Hook(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")), &Factory1Detour, factory1Orig);
    const bool c = Hook(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")), &Factory2Detour, factory2Orig);
    return a || b || c;
}

// In case the game made its factory before the mod loaded: a factory of our own as soon as DXGI makes one
// (the load call returns first), until the creation is hooked or the mod is enabled.
void RetryFactoryAfterLoad() {
    std::thread([] {
        const ULONGLONG begin = GetTickCount64();
        std::string why;
        for (int i = 0; i < 4000 && !creationTried.load() && !enabled.load(); ++i) {
            if (FindFactoryTargets(why)) {
                Info("In-game drawing: a DXGI factory could be made " + std::to_string(GetTickCount64() - begin) + " ms after load.");
                return;
            }
            Sleep(5);
        }
        if (!creationTried.load() && !enabled.load()) Warn("In-game drawing: still no DXGI factory 20 s after load (" + why + ").");
    }).detach();
}

// Hooks the swap chain / queue functions found by ChainTargetsOf or FindTargets (startMutex held).
bool HookChainFunctions(const char* from) {
    hooking = true;
    const bool ok = Hook(executeTarget, &ExecuteDetour, executeOrig) && Hook(presentTarget, &PresentDetour, presentOrig) &&
                    Hook(resizeTarget, &ResizeDetour, resizeOrig);
    if (!ok) {
        Warn("In-game drawing unavailable: the swap chain could not be hooked.");
        hud.Data()->dllState = MyClientHudFrame::Unsupported;
        return false;
    }
    const bool present1 = Hook(present1Target, &Present1Detour, present1Orig);
    const bool resize1 = Hook(resize1Target, &Resize1Detour, resize1Orig);
    // Both or no proxies: a proxy index with the real buffers (or the other way round) would mix the game's pictures.
    if (!Hook(getBufferTarget, &GetBufferDetour, getBufferOrig)) getBufferOrig = nullptr;
    if (!Hook(indexTarget, &IndexDetour, indexOrig)) indexOrig = nullptr;
    proxyHooks = getBufferOrig && indexOrig;
    hud.Data()->dllState = MyClientHudFrame::NotReady;   // a failed earlier attempt may have said Unsupported
    enabled.store(true);
    Info(std::string("In-game drawing hooks installed from ") + from + " (Present1=" + (present1 ? "true" : "false") +
         " ResizeBuffers1=" + (resize1 ? "true" : "false") + " tearing=" + (tearingSupported ? "supported" : "unsupported") +
         " swapChainCreate=" + (chainHooks ? "hooked" : "not hooked") + " proxyBuffers=" + (proxyHooks ? "hooked" : "not hooked") + ").");
    return true;
}

// Which GPU the game draws with (a laptop may run it on the integrated one).
void LogAdapter(ID3D12CommandQueue* queue) {
    ComPtr<ID3D12Device> dev;
    if (!queue || FAILED(queue->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return;
    const LUID luid = dev->GetAdapterLuid();
    auto factoryFn = Proc<CreateFactoryFn>(GetModuleHandleW(L"dxgi.dll"), "CreateDXGIFactory1");
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    if (!factoryFn || FAILED(factoryFn(IID_PPV_ARGS(&factory))) || !factory ||
        FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))) || !adapter || FAILED(adapter->GetDesc1(&desc)))
        return;
    char name[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name - 1, nullptr, nullptr);
    Info(std::string("In-game drawing: the game draws with ") + name + " (" + std::to_string(desc.DedicatedVideoMemory >> 20) +
         " MB video memory).");
}

// The game just made `chain` on `device` (its direct queue for D3D12): its first back buffers have not been asked
// for yet, so with the hooks in now the game draws into proxies from its first frame on.
void HookFromGameChain(IUnknown* chain, IUnknown* device) {
    if (!chain || !device || enabled.load()) return;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(chain->QueryInterface(IID_PPV_ARGS(&swap))) || !swap) return;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))) || !queue) return;   // a D3D11 swap chain
    std::lock_guard guard(startMutex);
    if (enabled.load() || hooking || !started) return;
    ChainTargetsOf(swap.Get(), queue.Get());
    if (HookChainFunctions("the game's new swap chain")) LogAdapter(queue.Get());
}
}  // namespace

bool Start(const Host& h, bool probe) {
    std::lock_guard guard(startMutex);
    if (enabled.load() || hooking) return enabled.load();
    if (!started) {
        host = h;
        if (!hud.Open(GetCurrentProcessId(), true)) {
            Warn("In-game drawing unavailable: shared HUD picture memory could not be created (" + std::to_string(GetLastError()) + ").");
            return false;
        }
        started = true;
        std::string why;
        if (!FindFactoryTargets(why)) {
            exportHooks = HookFactoryExports();
            Warn("In-game drawing: no DXGI factory at load (" + why + "); " +
                 (exportHooks ? "the game's first factory is waited for." : "the factory creation could not be hooked either."));
            if (exportHooks) RetryFactoryAfterLoad();
        }
    }
    if (!probe) return chainHooks || exportHooks;
    // The game made its swap chain before the creation hooks (or they failed): find the functions another way.
    std::string why;
    if (!FindTargets(why)) {
        Warn("In-game drawing unavailable: " + why + ".");
        hud.Data()->dllState = MyClientHudFrame::Unsupported;
        return false;
    }
    return HookChainFunctions("a probe swap chain (the game's was made earlier)");
}

void Stop() {
    enabled.store(false);
    std::lock_guard guard(renderMutex);
    ReleaseTargets();
    if (auto* h = hud.Data()) h->dllState = MyClientHudFrame::NotReady;
}

void SetCamera(float x, float y, float z, float yaw, float pitch, float fov) {
    std::lock_guard in(inputMutex);
    camera = Camera{x, y, z, yaw, pitch, fov};
    cameraAt = GetTickCount64();
}

void SetMatrices(const float view[16], const float proj[16], float x, float y, float z) {
    std::lock_guard in(inputMutex);
    std::memcpy(matrices.view, view, sizeof matrices.view);
    std::memcpy(matrices.proj, proj, sizeof matrices.proj);
    matricesPos[0] = x; matricesPos[1] = y; matricesPos[2] = z;
    matricesAt = GetTickCount64();
}

bool DrawingOutline() {
    return enabled.load() && !failed && MyClientHudFrame::Fresh(outlineDrawnAt.load(), GetTickCount64(), 300);
}

void SetBoxes(const MyClientTabList::Box* list, uint32_t count) {
    std::lock_guard in(inputMutex);
    boxes.assign(list, list + count);
    boxShifts.clear();
    boxesAt = GetTickCount64();
}

void SetBoxShifts(const float* shifts, uint32_t count) {
    std::lock_guard in(inputMutex);
    if (!shifts || count != boxes.size()) return;
    boxShifts.assign(shifts, shifts + size_t(count) * 3);
}

bool GpuInfo(Gpu& out) {
    if (!gpuKnown.load()) return false;
    std::lock_guard guard(gpuMutex);
    out = gpu;
    return true;
}
void FrameGenStatus(float& fps, uint32_t& state) {
    const bool fresh = MyClientHudFrame::Fresh(presentedAt.load(), GetTickCount64(), 2000);
    fps = fresh ? presentedFps.load() : 0.f;
    state = fresh ? frameGenState.load() : MyClientFeatures::FrameGenOff;
}

void SetTarget(bool has, int x, int y, int z) {
    std::lock_guard in(inputMutex);
    hasTarget = has; targetX = x; targetY = y; targetZ = z;
    targetAt = GetTickCount64();
}
}  // namespace MyClientCompositor

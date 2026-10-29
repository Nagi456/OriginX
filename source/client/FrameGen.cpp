// Frame Generation (D3D12 compute). For each game frame the Compositor calls Capture, then for each
// in-between picture Generate (and presents it), and last Restore (and presents the game frame itself).
//
// Capture copies the back buffer into history[cur] (history[prev] holds the frame before), and for the
// Motion method measures the movement between the two:
//   1. luma:   each frame shrunk to a quarter size, one brightness value per 4x4 pixels;
//   2. motion: for every 8x8 block of the newest quarter-size frame, the offset (up to +-kSearch) where
//              the previous frame matches best (sum of absolute differences, a small cost for long offsets);
//   3. smooth: the median offset of each block and its 8 neighbours, which removes single wrong blocks.
// Generate draws the picture at time `phase`: each pixel takes the previous frame moved forward by
// `phase` of the offset and the newest moved back by the rest, mixed by time; where the two disagree
// (something appeared or was hidden) the frame closer in time wins. Blend only mixes the two frames;
// Copy repeats the newest frame (to try the extra presents alone).
#include "FrameGen.h"
#include "FeatureData.h"

#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>

namespace MyClientFrameGen {
namespace {
using Microsoft::WRL::ComPtr;
using SerializeRootFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                   ID3DBlob**, ID3DBlob**);
template <class Fn> Fn Proc(HMODULE module, const char* name) {
    return module ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name))) : nullptr;
}

constexpr UINT kLumaScale = 4;     // full-size pixels per quarter-size pixel (each way)
constexpr UINT kBlock = 8;         // quarter-size pixels per motion block (each way)
constexpr UINT kSearch = 8;        // quarter-size pixels searched each way (= 32 full-size pixels)
constexpr UINT kSlots = 8;         // command allocators in flight
constexpr UINT kConstants = 12;    // root constants, see the cbuffer in kShaders

const char* kShaders = R"(
cbuffer C : register(b0) {
    uint2 size; uint2 lsize; uint2 bsize; float phase; uint method; float2 inv; uint pad0; uint pad1;
};
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
RWTexture2D<float4> u0 : register(u0);
RWTexture2D<float> lumaOut : register(u1);   // the one-channel texture the luma pass writes
SamplerState linearClamp : register(s0);

[numthreads(8, 8, 1)] void luma(uint3 id : SV_DispatchThreadID) {
    if (id.x >= lsize.x || id.y >= lsize.y) return;
    // Four bilinear taps between pixels: the average of the whole 4x4 square.
    float2 p = float2(id.xy * 4);
    float3 c = t0.SampleLevel(linearClamp, (p + float2(1, 1)) * inv, 0).rgb + t0.SampleLevel(linearClamp, (p + float2(3, 1)) * inv, 0).rgb +
               t0.SampleLevel(linearClamp, (p + float2(1, 3)) * inv, 0).rgb + t0.SampleLevel(linearClamp, (p + float2(3, 3)) * inv, 0).rgb;
    lumaOut[id.xy] = dot(c * 0.25, float3(0.299, 0.587, 0.114));
}

#define B 8
#define R 8
#define W (B + 2 * R)
groupshared float cur[B * B];
groupshared float ref[W * W];
groupshared float bestCost[64];
groupshared uint bestIndex[64];
float LumaAt(Texture2D<float4> t, int2 p) { return t.Load(int3(clamp(p, int2(0, 0), int2(lsize) - 1), 0)).r; }

// t0 = newest quarter-size frame, t1 = previous one. Writes the block's offset in full-size pixels and its cost.
[numthreads(8, 8, 1)] void motion(uint3 g : SV_GroupID, uint3 l : SV_GroupThreadID, uint ti : SV_GroupIndex) {
    int2 origin = int2(g.xy) * B;
    cur[ti] = LumaAt(t0, origin + int2(l.xy));
    for (uint i = ti; i < uint(W * W); i += 64u) ref[i] = LumaAt(t1, origin + int2(int(i % uint(W)) - R, int(i / uint(W)) - R));
    GroupMemoryBarrierWithGroupSync();
    const uint span = uint(2 * R + 1);
    float best = 1e30; uint bestI = span * uint(R) + uint(R);   // zero offset
    for (uint c = ti; c < span * span; c += 64u) {
        uint dx = c % span, dy = c / span;
        float sad = 0;
        for (uint y = 0; y < uint(B); y++)
            for (uint x = 0; x < uint(B); x++) sad += abs(cur[y * uint(B) + x] - ref[(y + dy) * uint(W) + x + dx]);
        sad += 0.02 * float(abs(int(dx) - R) + abs(int(dy) - R));   // prefer short offsets on flat areas
        if (sad < best) { best = sad; bestI = c; }
    }
    bestCost[ti] = best; bestIndex[ti] = bestI;
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 32u; s > 0u; s >>= 1u) {
        if (ti < s && bestCost[ti + s] < bestCost[ti]) { bestCost[ti] = bestCost[ti + s]; bestIndex[ti] = bestIndex[ti + s]; }
        GroupMemoryBarrierWithGroupSync();
    }
    if (ti == 0 && g.x < bsize.x && g.y < bsize.y) {
        float2 d = float2(float(int(bestIndex[0] % span) - R), float(int(bestIndex[0] / span) - R)) * 4.0;
        u0[g.xy] = float4(d, bestCost[0] / float(B * B), 0);
    }
}

void Sort9(inout float v[9]) {
    for (uint i = 0; i < 9; i++)
        for (uint j = 0; j < 8 - i; j++)
            if (v[j] > v[j + 1]) { float t = v[j]; v[j] = v[j + 1]; v[j + 1] = t; }
}
// t0 = raw offsets: each block gets the median of itself and its 8 neighbours.
[numthreads(8, 8, 1)] void smooth(uint3 id : SV_DispatchThreadID) {
    if (id.x >= bsize.x || id.y >= bsize.y) return;
    float xs[9], ys[9]; uint n = 0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            float2 d = t0.Load(int3(clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(bsize) - 1), 0)).xy;
            xs[n] = d.x; ys[n] = d.y; n++;
        }
    Sort9(xs); Sort9(ys);
    u0[id.xy] = float4(xs[4], ys[4], t0.Load(int3(id.xy, 0)).z, 0);
}

// t0 = previous frame, t1 = newest frame, t2 = smoothed offsets. method 0 = motion, 1 = blend.
[numthreads(8, 8, 1)] void synth(uint3 id : SV_DispatchThreadID) {
    if (id.x >= size.x || id.y >= size.y) return;
    float2 pix = float2(id.xy) + 0.5;
    float2 uv = pix * inv;
    float4 a, b;
    if (method == 1) {
        a = t0.SampleLevel(linearClamp, uv, 0); b = t1.SampleLevel(linearClamp, uv, 0);
        u0[id.xy] = float4(lerp(a.rgb, b.rgb, phase), 1);
        return;
    }
    // Offset d: the previous frame at x + d matches the newest frame at x, so the picture moved by -d.
    float2 d = t2.SampleLevel(linearClamp, pix / (4.0 * B) / float2(bsize), 0).xy;
    a = t0.SampleLevel(linearClamp, (pix + phase * d) * inv, 0);
    b = t1.SampleLevel(linearClamp, (pix - (1 - phase) * d) * inv, 0);
    float3 mixed = lerp(a.rgb, b.rgb, phase);
    float disagree = saturate((length(a.rgb - b.rgb) - 0.1) / 0.2);
    u0[id.xy] = float4(lerp(mixed, phase < 0.5 ? a.rgb : b.rgb, disagree), 1);
}

struct VsOut { float4 pos : SV_POSITION; };
VsOut blitVs(uint v : SV_VertexID) {
    VsOut o; float2 p = float2((v << 1) & 2, v & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); return o;
}
float4 blitPs(VsOut i) : SV_Target { return float4(t0.Load(int3(int2(i.pos.xy), 0)).rgb, 1); }
)";

struct Texture {
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};
// Descriptor sets: 4 SRVs (t0..t3) then 2 UAVs (u0..u1), one set per pass and history parity.
enum Set : UINT { LumaSet0, LumaSet1, MotionSet0, MotionSet1, SmoothSet, SynthSet0, SynthSet1, BlitSet, SetCount };
constexpr UINT kSetSize = 6;

struct Slot { ComPtr<ID3D12CommandAllocator> allocator; UINT64 fenceValue = 0; };

bool failed = false;
bool objectsReady = false;
ComPtr<ID3D12RootSignature> rootSignature;
ComPtr<ID3D12PipelineState> lumaPso, motionPso, smoothPso, synthPso, blitPso;
DXGI_FORMAT blitFormat = DXGI_FORMAT_UNKNOWN;
ComPtr<ID3DBlob> blitVsBlob, blitPsBlob;
ComPtr<ID3D12DescriptorHeap> heap;
UINT heapStep = 0;
ComPtr<ID3D12GraphicsCommandList> list;
Slot slots[kSlots];
UINT slotIndex = 0;
ComPtr<ID3D12Fence> fence;
HANDLE fenceEvent = nullptr;
UINT64 fenceValue = 0;

UINT width = 0, height = 0, lumaW = 0, lumaH = 0, blocksW = 0, blocksH = 0;
DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
Texture history[2], luma[2], rawMotion, motionField, generated;
UINT cur = 0;            // history[cur] / luma[cur] = newest frame
bool havePrevious = false, haveCurrent = false, motionReady = false;

void Fail(const Gpu& gpu, const char* what, HRESULT hr) {
    if (failed) return;
    failed = true;
    char text[160];
    std::snprintf(text, sizeof text, "Frame Generation unavailable: %s failed (0x%08X).", what, static_cast<uint32_t>(hr));
    if (gpu.warn) gpu.warn(text);
}

D3D12_HEAP_PROPERTIES HeapOf(D3D12_HEAP_TYPE type) { D3D12_HEAP_PROPERTIES h{}; h.Type = type; return h; }
void To(ID3D12GraphicsCommandList* l, Texture& t, D3D12_RESOURCE_STATES state) {
    if (!t.resource || t.state == state) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.resource.Get();
    b.Transition.StateBefore = t.state; b.Transition.StateAfter = state;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    l->ResourceBarrier(1, &b);
    t.state = state;
}
void BackTo(ID3D12GraphicsCommandList* l, ID3D12Resource* back, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = back;
    b.Transition.StateBefore = before; b.Transition.StateAfter = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    l->ResourceBarrier(1, &b);
}
void UavDone(ID3D12GraphicsCommandList* l, Texture& t) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = t.resource.Get();
    l->ResourceBarrier(1, &b);
}
const D3D12_RESOURCE_STATES kRead = static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

void WaitFor(UINT64 value) {
    if (!fence || !value || !fenceEvent) return;
    if (fence->GetCompletedValue() < value && SUCCEEDED(fence->SetEventOnCompletion(value, fenceEvent)))
        WaitForSingleObject(fenceEvent, 500);
}

bool Compile(const Gpu& gpu, CompileFn compile, const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
    ComPtr<ID3DBlob> error;
    const HRESULT hr = compile(kShaders, std::strlen(kShaders), "myclient_framegen", nullptr, nullptr, entry, target,
                               D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &error);
    if (FAILED(hr)) {
        if (error && gpu.warn) gpu.warn(std::string("Frame Generation shader ") + entry + ": " +
                                        std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize()));
        Fail(gpu, "compiling a Frame Generation shader", hr);
        return false;
    }
    return true;
}

bool ComputePso(const Gpu& gpu, CompileFn compile, const char* entry, ComPtr<ID3D12PipelineState>& pso) {
    ComPtr<ID3DBlob> blob;
    if (!Compile(gpu, compile, entry, "cs_5_0", blob)) return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rootSignature.Get();
    d.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
    const HRESULT hr = gpu.device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) { Fail(gpu, "CreateComputePipelineState", hr); return false; }
    return true;
}

bool CreateObjects(const Gpu& gpu) {
    if (objectsReady) return true;
    ID3D12Device* device = gpu.device;
    // Root signature: 12 constants (b0), one table of 4 SRVs + 2 UAVs, a linear/clamp sampler.
    D3D12_DESCRIPTOR_RANGE ranges[2] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 4},
    };
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = {0, 0, kConstants};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {2, ranges};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rs{2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    auto serialize = Proc<SerializeRootFn>(GetModuleHandleW(L"d3d12.dll"), "D3D12SerializeRootSignature");
    if (!serialize) { Fail(gpu, "D3D12SerializeRootSignature lookup", E_NOINTERFACE); return false; }
    ComPtr<ID3DBlob> blob, error;
    HRESULT hr = serialize(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr)) { Fail(gpu, "D3D12SerializeRootSignature", hr); return false; }
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    if (FAILED(hr)) { Fail(gpu, "CreateRootSignature", hr); return false; }

    auto compile = Proc<CompileFn>(LoadLibraryW(L"d3dcompiler_47.dll"), "D3DCompile");
    if (!compile) { Fail(gpu, "loading d3dcompiler_47.dll", E_NOINTERFACE); return false; }
    if (!ComputePso(gpu, compile, "luma", lumaPso) || !ComputePso(gpu, compile, "motion", motionPso) ||
        !ComputePso(gpu, compile, "smooth", smoothPso) || !ComputePso(gpu, compile, "synth", synthPso))
        return false;
    if (!Compile(gpu, compile, "blitVs", "vs_5_0", blitVsBlob) || !Compile(gpu, compile, "blitPs", "ps_5_0", blitPsBlob)) return false;

    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SetCount * kSetSize, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) { Fail(gpu, "CreateDescriptorHeap(Frame Generation)", hr); return false; }
    heapStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (auto& s : slots) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.allocator));
        if (FAILED(hr)) { Fail(gpu, "CreateCommandAllocator(Frame Generation)", hr); return false; }
    }
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slots[0].allocator.Get(), nullptr, IID_PPV_ARGS(&list));
    if (FAILED(hr)) { Fail(gpu, "CreateCommandList(Frame Generation)", hr); return false; }
    list->Close();
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) { Fail(gpu, "CreateFence(Frame Generation)", hr); return false; }
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) { Fail(gpu, "CreateEvent(Frame Generation)", E_FAIL); return false; }
    objectsReady = true;
    return true;
}

bool BlitPso(const Gpu& gpu, DXGI_FORMAT target) {
    if (blitPso && blitFormat == target) return true;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = rootSignature.Get();
    p.VS = {blitVsBlob->GetBufferPointer(), blitVsBlob->GetBufferSize()};
    p.PS = {blitPsBlob->GetBufferPointer(), blitPsBlob->GetBufferSize()};
    p.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    p.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    p.SampleMask = UINT_MAX;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = target;
    p.SampleDesc.Count = 1;
    blitPso.Reset();
    const HRESULT hr = gpu.device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&blitPso));
    if (FAILED(hr)) { Fail(gpu, "CreateGraphicsPipelineState(Frame Generation)", hr); return false; }
    blitFormat = target;
    return true;
}

bool MakeTexture(const Gpu& gpu, Texture& t, UINT w, UINT h, DXGI_FORMAT f, bool uav, D3D12_RESOURCE_STATES initial) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1;
    d.MipLevels = 1; d.Format = f; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const auto heapProps = HeapOf(D3D12_HEAP_TYPE_DEFAULT);
    t.resource.Reset();
    const HRESULT hr = gpu.device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &d, initial, nullptr, IID_PPV_ARGS(&t.resource));
    if (FAILED(hr)) { Fail(gpu, "creating a Frame Generation texture", hr); return false; }
    t.state = initial;
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT set, UINT slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(set * kSetSize + slot) * heapStep;
    return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE GpuSet(UINT set) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(set * kSetSize) * heapStep;
    return h;
}
void Srv(ID3D12Device* device, Texture* t, DXGI_FORMAT f, D3D12_CPU_DESCRIPTOR_HANDLE at) {
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = f;
    d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(t ? t->resource.Get() : nullptr, &d, at);   // null: an empty slot reads zeros
}
void Uav(ID3D12Device* device, Texture* t, DXGI_FORMAT f, D3D12_CPU_DESCRIPTOR_HANDLE at) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format = f;
    d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(t ? t->resource.Get() : nullptr, nullptr, &d, at);
}
void FillSet(ID3D12Device* device, UINT set, Texture* s0, DXGI_FORMAT f0, Texture* s1, DXGI_FORMAT f1, Texture* s2, DXGI_FORMAT f2,
             Texture* u, DXGI_FORMAT fu) {
    const DXGI_FORMAT any = DXGI_FORMAT_R16G16B16A16_FLOAT;
    Srv(device, s0, s0 ? f0 : any, Cpu(set, 0));
    Srv(device, s1, s1 ? f1 : any, Cpu(set, 1));
    Srv(device, s2, s2 ? f2 : any, Cpu(set, 2));
    Srv(device, nullptr, any, Cpu(set, 3));
    Uav(device, u, u ? fu : any, Cpu(set, 4));
    Uav(device, nullptr, any, Cpu(set, 5));
}

// One command list per call, on its own allocator; returns null when something failed.
ID3D12GraphicsCommandList* Begin(const Gpu& gpu) {
    Slot& s = slots[slotIndex];
    WaitFor(s.fenceValue);
    HRESULT hr = s.allocator->Reset();
    if (FAILED(hr)) { Fail(gpu, "ID3D12CommandAllocator::Reset(Frame Generation)", hr); return nullptr; }
    hr = list->Reset(s.allocator.Get(), nullptr);
    if (FAILED(hr)) { Fail(gpu, "ID3D12GraphicsCommandList::Reset(Frame Generation)", hr); return nullptr; }
    ID3D12DescriptorHeap* heaps[] = {heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    return list.Get();
}
bool Submit(const Gpu& gpu) {
    const HRESULT hr = list->Close();
    if (FAILED(hr)) { Fail(gpu, "ID3D12GraphicsCommandList::Close(Frame Generation)", hr); return false; }
    ID3D12CommandList* lists[] = {list.Get()};
    gpu.execute(gpu.queue, 1, lists);
    gpu.queue->Signal(fence.Get(), ++fenceValue);
    slots[slotIndex].fenceValue = fenceValue;
    slotIndex = (slotIndex + 1) % kSlots;
    return true;
}

void Constants(ID3D12GraphicsCommandList* l, float phase, uint32_t method, bool compute) {
    uint32_t c[kConstants] = {width, height, lumaW, lumaH, blocksW, blocksH, 0, method, 0, 0, 0, 0};
    const float inv[2] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
    std::memcpy(&c[6], &phase, 4);
    std::memcpy(&c[8], &inv[0], 4);
    std::memcpy(&c[9], &inv[1], 4);
    if (compute) l->SetComputeRoot32BitConstants(0, kConstants, c, 0);
    else l->SetGraphicsRoot32BitConstants(0, kConstants, c, 0);
}
UINT Groups(UINT n, UINT per) { return (n + per - 1) / per; }
}  // namespace

bool Failed() { return failed; }

void Forget() { havePrevious = false; haveCurrent = false; motionReady = false; }

bool Ensure(const Gpu& gpu, UINT w, UINT h, DXGI_FORMAT f) {
    if (failed || !gpu.device || !gpu.queue || !gpu.execute) return false;
    if (!CreateObjects(gpu)) return false;
    if (w == width && h == height && f == format && history[0].resource) return true;
    WaitFor(fenceValue);
    width = w; height = h; format = f;
    lumaW = Groups(w, kLumaScale); lumaH = Groups(h, kLumaScale);
    blocksW = Groups(lumaW, kBlock); blocksH = Groups(lumaH, kBlock);
    const DXGI_FORMAT lumaFormat = DXGI_FORMAT_R16_FLOAT, vecFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!MakeTexture(gpu, history[0], w, h, f, false, D3D12_RESOURCE_STATE_COPY_DEST) ||
        !MakeTexture(gpu, history[1], w, h, f, false, D3D12_RESOURCE_STATE_COPY_DEST) ||
        !MakeTexture(gpu, luma[0], lumaW, lumaH, lumaFormat, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !MakeTexture(gpu, luma[1], lumaW, lumaH, lumaFormat, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !MakeTexture(gpu, rawMotion, blocksW, blocksH, vecFormat, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !MakeTexture(gpu, motionField, blocksW, blocksH, vecFormat, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !MakeTexture(gpu, generated, w, h, vecFormat, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        return false;
    ID3D12Device* d = gpu.device;
    for (UINT p = 0; p < 2; p++) {
        const UINT c = p, o = 1 - p;   // parity p: history[p] is the newest
        FillSet(d, LumaSet0 + p, &history[c], f, nullptr, f, nullptr, f, nullptr, vecFormat);
        Uav(d, &luma[c], lumaFormat, Cpu(LumaSet0 + p, 5));   // u1
        FillSet(d, MotionSet0 + p, &luma[c], lumaFormat, &luma[o], lumaFormat, nullptr, f, &rawMotion, vecFormat);
        FillSet(d, SynthSet0 + p, &history[o], f, &history[c], f, &motionField, vecFormat, &generated, vecFormat);
    }
    FillSet(d, SmoothSet, &rawMotion, vecFormat, nullptr, f, nullptr, f, &motionField, vecFormat);
    FillSet(d, BlitSet, &generated, vecFormat, nullptr, f, nullptr, f, nullptr, vecFormat);
    cur = 0;
    Forget();
    return true;
}

bool Capture(const Gpu& gpu, ID3D12Resource* back, bool motion) {
    if (failed || !back || !history[0].resource) return false;
    if (haveCurrent) { cur = 1 - cur; havePrevious = true; }
    auto* l = Begin(gpu);
    if (!l) return false;
    To(l, history[cur], D3D12_RESOURCE_STATE_COPY_DEST);
    BackTo(l, back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->CopyResource(history[cur].resource.Get(), back);
    BackTo(l, back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    To(l, history[cur], kRead);
    haveCurrent = true;
    motionReady = false;
    if (motion) {
        // Quarter-size brightness of the newest frame (the previous one's is kept from its own Capture).
        l->SetComputeRootSignature(rootSignature.Get());
        Constants(l, 0, 0, true);
        To(l, luma[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        l->SetPipelineState(lumaPso.Get());
        l->SetComputeRootDescriptorTable(1, GpuSet(LumaSet0 + cur));
        l->Dispatch(Groups(lumaW, 8), Groups(lumaH, 8), 1);
        UavDone(l, luma[cur]);
        To(l, luma[cur], kRead);
        if (havePrevious && luma[1 - cur].state == kRead) {
            To(l, rawMotion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            l->SetPipelineState(motionPso.Get());
            l->SetComputeRootDescriptorTable(1, GpuSet(MotionSet0 + cur));
            l->Dispatch(blocksW, blocksH, 1);
            UavDone(l, rawMotion);
            To(l, rawMotion, kRead);
            To(l, motionField, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            l->SetPipelineState(smoothPso.Get());
            l->SetComputeRootDescriptorTable(1, GpuSet(SmoothSet));
            l->Dispatch(Groups(blocksW, 8), Groups(blocksH, 8), 1);
            UavDone(l, motionField);
            To(l, motionField, kRead);
            motionReady = true;
        }
    } else {
        // No brightness this frame: the next motion pass must not compare against a stale one.
        To(l, luma[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    return Submit(gpu) && havePrevious;
}

bool Generate(const Gpu& gpu, ID3D12Resource* back, D3D12_CPU_DESCRIPTOR_HANDLE rtv, float phase, uint32_t method) {
    if (failed || !back || !havePrevious) return false;
    if (method == MyClientFeatures::FrameGenCopy) return Restore(gpu, back);
    if (method == MyClientFeatures::FrameGenMotion && !motionReady) method = MyClientFeatures::FrameGenBlend;
    if (!BlitPso(gpu, format)) return false;
    auto* l = Begin(gpu);
    if (!l) return false;
    l->SetComputeRootSignature(rootSignature.Get());
    Constants(l, phase, method == MyClientFeatures::FrameGenBlend ? 1u : 0u, true);
    To(l, generated, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    l->SetPipelineState(synthPso.Get());
    l->SetComputeRootDescriptorTable(1, GpuSet(SynthSet0 + cur));
    l->Dispatch(Groups(width, 8), Groups(height, 8), 1);
    UavDone(l, generated);
    To(l, generated, kRead);
    // Into the back buffer with a full-screen triangle (works for any back-buffer format).
    BackTo(l, back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    l->SetGraphicsRootSignature(rootSignature.Get());
    Constants(l, phase, 0, false);
    l->SetGraphicsRootDescriptorTable(1, GpuSet(BlitSet));
    l->SetPipelineState(blitPso.Get());
    l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    l->RSSetViewports(1, &viewport);
    l->RSSetScissorRects(1, &scissor);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    l->DrawInstanced(3, 1, 0, 0);
    BackTo(l, back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    return Submit(gpu);
}

bool Restore(const Gpu& gpu, ID3D12Resource* back) {
    if (failed || !back || !haveCurrent) return false;
    auto* l = Begin(gpu);
    if (!l) return false;
    To(l, history[cur], D3D12_RESOURCE_STATE_COPY_SOURCE);
    BackTo(l, back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    l->CopyResource(back, history[cur].resource.Get());
    BackTo(l, back, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    To(l, history[cur], kRead);
    return Submit(gpu);
}

void Release() {
    WaitFor(fenceValue);
    for (auto& t : history) t = {};
    for (auto& t : luma) t = {};
    rawMotion = {}; motionField = {}; generated = {};
    width = height = 0; format = DXGI_FORMAT_UNKNOWN;
    Forget();
}
}  // namespace MyClientFrameGen

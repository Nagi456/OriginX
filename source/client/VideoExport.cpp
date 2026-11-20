// See VideoExport.h. The sink writer turns RGB32 into NV12 and encodes H.264 itself (hardware encoder when there is one).
#include "VideoExport.h"

#include <windows.h>
#include <initguid.h>   // the Media Foundation GUIDs below are defined in this file (no mfuuid.lib)
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "../ReplayExport.h"

namespace MyClientVideo {
namespace {
using StartupFn = HRESULT(WINAPI*)(ULONG, DWORD);
using ShutdownFn = HRESULT(WINAPI*)();
using CreateMediaTypeFn = HRESULT(WINAPI*)(IMFMediaType**);
using CreateSampleFn = HRESULT(WINAPI*)(IMFSample**);
using CreateBufferFn = HRESULT(WINAPI*)(DWORD, IMFMediaBuffer**);
using CreateAttributesFn = HRESULT(WINAPI*)(IMFAttributes**, UINT32);
using CreateWriterFn = HRESULT(WINAPI*)(LPCWSTR, IMFByteStream*, IMFAttributes*, IMFSinkWriter**);

struct Api {
    HMODULE plat = nullptr, readwrite = nullptr;
    StartupFn startup = nullptr;
    ShutdownFn shutdown = nullptr;
    CreateMediaTypeFn mediaType = nullptr;
    CreateSampleFn sample = nullptr;
    CreateBufferFn buffer = nullptr;
    CreateAttributesFn attributes = nullptr;
    CreateWriterFn writer = nullptr;
} api;

IMFSinkWriter* writer = nullptr;
DWORD stream = 0;
int width = 0, height = 0, fps = 60;
std::wstring path;
bool started = false;   // MFStartup succeeded
std::vector<uint32_t> scaled;

template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }
// GetProcAddress gives a generic function pointer; going through void* keeps the compiler from warning on the cast.
template <class Fn> Fn Proc(HMODULE module, const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

bool Load(std::string& error) {
    if (api.writer) return true;
    api.plat = LoadLibraryW(L"mfplat.dll");
    api.readwrite = LoadLibraryW(L"mfreadwrite.dll");
    if (!api.plat || !api.readwrite) { error = "Media Foundation is not installed (Windows N: install the Media Feature Pack)"; return false; }
    api.startup = Proc<StartupFn>(api.plat, "MFStartup");
    api.shutdown = Proc<ShutdownFn>(api.plat, "MFShutdown");
    api.mediaType = Proc<CreateMediaTypeFn>(api.plat, "MFCreateMediaType");
    api.sample = Proc<CreateSampleFn>(api.plat, "MFCreateSample");
    api.buffer = Proc<CreateBufferFn>(api.plat, "MFCreateMemoryBuffer");
    api.attributes = Proc<CreateAttributesFn>(api.plat, "MFCreateAttributes");
    api.writer = Proc<CreateWriterFn>(api.readwrite, "MFCreateSinkWriterFromURL");
    if (!api.startup || !api.shutdown || !api.mediaType || !api.sample || !api.buffer || !api.attributes || !api.writer) {
        api.writer = nullptr;
        error = "Media Foundation functions are missing";
        return false;
    }
    return true;
}

UINT64 Pack(UINT32 high, UINT32 low) { return (static_cast<UINT64>(high) << 32) | low; }

std::string Hr(const char* what, HRESULT hr) {
    char text[96];
    std::snprintf(text, sizeof text, "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    return text;
}

void Close(bool remove) {
    Release(writer);
    if (started) { api.shutdown(); started = false; }
    if (remove && !path.empty()) { std::error_code ec; std::filesystem::remove(path, ec); }
    path.clear();
    scaled.clear();
}
} // namespace

bool Running() { return writer != nullptr; }

bool Begin(const std::wstring& file, int w, int h, int rate, uint32_t bitrate, std::string& error) {
    Cancel();
    if (w <= 0 || h <= 0 || (w & 1) || (h & 1) || (rate != 30 && rate != 60)) { error = "bad video size or frame rate"; return false; }
    if (!Load(error)) return false;
    // Media Foundation looks its encoders up through COM; this (the game's frame) thread may not have it yet.
    // RPC_E_CHANGED_MODE (already single-threaded) is fine too.
    // ole32 is loaded by name (the DLL links only kernel32 and user32).
    using CoInitFn = HRESULT(WINAPI*)(LPVOID, DWORD);
    if (HMODULE ole = LoadLibraryW(L"ole32.dll"))
        if (auto init = Proc<CoInitFn>(ole, "CoInitializeEx")) (void)init(nullptr, 0x0 /* COINIT_MULTITHREADED */);
    HRESULT hr = api.startup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) { error = Hr("MFStartup", hr); return false; }
    started = true;
    path = file;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);

    IMFAttributes* attrs = nullptr;
    if (SUCCEEDED(api.attributes(&attrs, 2))) {
        attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    }
    hr = api.writer(file.c_str(), nullptr, attrs, &writer);
    Release(attrs);
    if (FAILED(hr)) { error = Hr("creating the video file", hr); Close(true); return false; }

    IMFMediaType* out = nullptr;
    hr = api.mediaType(&out);
    if (SUCCEEDED(hr)) {
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        out->SetUINT64(MF_MT_FRAME_SIZE, Pack(static_cast<UINT32>(w), static_cast<UINT32>(h)));
        out->SetUINT64(MF_MT_FRAME_RATE, Pack(static_cast<UINT32>(rate), 1));
        out->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, Pack(1, 1));
        hr = writer->AddStream(out, &stream);
    }
    Release(out);
    if (FAILED(hr)) { error = Hr("the H.264 output", hr); Close(true); return false; }

    IMFMediaType* in = nullptr;
    hr = api.mediaType(&in);
    if (SUCCEEDED(hr)) {
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        in->SetUINT64(MF_MT_FRAME_SIZE, Pack(static_cast<UINT32>(w), static_cast<UINT32>(h)));
        in->SetUINT64(MF_MT_FRAME_RATE, Pack(static_cast<UINT32>(rate), 1));
        in->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, Pack(1, 1));
        in->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(w * 4));   // top row first
        hr = writer->SetInputMediaType(stream, in, nullptr);
    }
    Release(in);
    if (FAILED(hr)) { error = Hr("the picture input", hr); Close(true); return false; }
    hr = writer->BeginWriting();
    if (FAILED(hr)) { error = Hr("starting the video", hr); Close(true); return false; }
    width = w; height = h; fps = rate;
    scaled.assign(static_cast<size_t>(w) * h, 0);
    return true;
}

bool Write(const uint32_t* bgra, int srcWidth, int srcHeight, int srcPitch, uint32_t index, std::string& error) {
    if (!writer) { error = "no video is being written"; return false; }
    MyClientReplayExport::Downscale(bgra, srcWidth, srcHeight, srcPitch, scaled.data(), width, height);
    const DWORD bytes = static_cast<DWORD>(scaled.size() * 4);
    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = api.buffer(bytes, &buffer);
    if (FAILED(hr)) { error = Hr("a frame buffer", hr); return false; }
    BYTE* data = nullptr;
    hr = buffer->Lock(&data, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        // RGB32 is BGRX: the alpha byte is ignored, the colour bytes are already in that order.
        std::memcpy(data, scaled.data(), bytes);
        buffer->Unlock();
        hr = buffer->SetCurrentLength(bytes);
    }
    IMFSample* sample = nullptr;
    if (SUCCEEDED(hr)) hr = api.sample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
    const LONGLONG duration = 10000000LL / fps;
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(static_cast<LONGLONG>(index) * duration);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(duration);
    if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample);
    Release(sample);
    Release(buffer);
    if (FAILED(hr)) { error = Hr("writing a frame", hr); return false; }
    return true;
}

bool Finish(std::string& error) {
    if (!writer) { error = "no video is being written"; return false; }
    const HRESULT hr = writer->Finalize();
    const bool ok = SUCCEEDED(hr);
    if (!ok) error = Hr("finishing the video", hr);
    Close(!ok);
    return ok;
}

void Cancel() {
    if (writer || started) Close(true);
}
} // namespace MyClientVideo

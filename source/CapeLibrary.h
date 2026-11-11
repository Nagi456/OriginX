#pragma once
#include "SkinChannel.h"
#include <windows.h>
#include <wincodec.h>

// Trial assets supplied by the user. Keep the HD originals unchanged; the game's classic
// PlayerSkin cape uses a standard 64x32 atlas, decoded once when the Closet first needs it.
namespace CapeLibrary {
inline const wchar_t* Name(uint32_t id) {
    return id == 1 ? L"Founding" : id == 2 ? L"Supporter" : L"元のマント";
}
inline bool Load(uint32_t id, std::vector<uint8_t>& rgba) {
    rgba.clear();
    if (id < 1 || id > 2) return false;
    const HMODULE module = GetModuleHandleW(nullptr);
    if (!module) return false;
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(100 + id), MAKEINTRESOURCEW(10));
    const HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
    const auto* bytes = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
    const DWORD size = resource ? SizeofResource(module, resource) : 0;
    if (!bytes || !size || size > 16 * 1024 * 1024) return false;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return false;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICBitmapSource* converted = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) && factory &&
        SUCCEEDED(factory->CreateStream(&stream)) && stream &&
        SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), size)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) && decoder &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
        UINT w = 0, h = 0;
        if (SUCCEEDED(frame->GetSize(&w, &h)) && w == 2048 && h == 1024 &&
            SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && scaler &&
            SUCCEEDED(scaler->Initialize(frame, MyClientSkin::CapeWidth, MyClientSkin::CapeHeight, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, scaler, &converted)) && converted) {
            rgba.resize(MyClientSkin::CapeBytes);
            ok = SUCCEEDED(converted->CopyPixels(nullptr, MyClientSkin::CapeWidth * 4, MyClientSkin::CapeBytes, rgba.data()));
        }
    }
    if (converted) converted->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    if (!ok) rgba.clear();
    return ok;
}
} // namespace CapeLibrary

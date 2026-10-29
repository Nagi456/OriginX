#pragma once
// Armor HUD icons. The item textures are read from the player's own Minecraft install
// (<game>\data\resource_packs\*\textures\items), so no game art ships with MyClient.
// The pure parts (item id -> texture name, TGA reading) are unit-tested on the host.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#endif

namespace ItemIcons {

// Texture file name (without extension) of an item id: Bedrock names the gold armor textures "gold_*"
// while the items are "golden_*"; every other armor texture has the item's own name.
inline std::string TextureName(std::string id)
{
    if (const auto colon = id.find(':'); colon != std::string::npos) id.erase(0, colon + 1);
    if (id.rfind("golden_", 0) == 0) id = "gold_" + id.substr(7);
    return id;
}

// Premultiplied BGRA pixels, top row first.
struct Image { int w = 0, h = 0; std::vector<uint32_t> bgra; };

inline void Premultiply(Image& image)
{
    for (auto& p : image.bgra) {
        const uint32_t a = p >> 24;
        const uint32_t b = (p & 0xFF) * a / 255, g = ((p >> 8) & 0xFF) * a / 255, r = ((p >> 16) & 0xFF) * a / 255;
        p = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

// Leather armor textures are grey; the game tints them. Undyed leather is #A06540 (dye colours are not sent).
inline void TintLeather(Image& image)
{
    for (auto& p : image.bgra) {
        const uint32_t b = (p & 0xFF) * 0x40 / 255, g = ((p >> 8) & 0xFF) * 0x65 / 255, r = ((p >> 16) & 0xFF) * 0xA0 / 255;
        p = (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
    }
}

// Truecolor TGA (types 2 and 10 = RLE), 24 or 32 bpp, either row order. Some leather armor textures are TGA.
// straight = false keeps the colour under see-through pixels (overlay masks, SlotIcons.h Overlay).
inline bool DecodeTga(const std::string& data, Image& out, bool premultiply = true)
{
    if (data.size() < 18) return false;
    const auto u8 = [&](size_t i) { return static_cast<uint8_t>(data[i]); };
    const int idLength = u8(0), colorMapType = u8(1), type = u8(2);
    const int w = u8(12) | (u8(13) << 8), h = u8(14) | (u8(15) << 8), bpp = u8(16), descriptor = u8(17);
    if (colorMapType != 0 || (type != 2 && type != 10) || (bpp != 24 && bpp != 32)) return false;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
    const size_t bytes = static_cast<size_t>(bpp / 8);
    size_t at = 18 + static_cast<size_t>(idLength);
    std::vector<uint32_t> pixels;
    pixels.reserve(static_cast<size_t>(w) * h);
    const auto readPixel = [&](uint32_t& p) {
        if (at + bytes > data.size()) return false;
        const uint32_t b = u8(at), g = u8(at + 1), r = u8(at + 2), a = bytes == 4 ? u8(at + 3) : 255u;
        p = (a << 24) | (r << 16) | (g << 8) | b;
        at += bytes;
        return true;
    };
    const size_t total = static_cast<size_t>(w) * h;
    while (pixels.size() < total) {
        if (type == 2) {
            uint32_t p;
            if (!readPixel(p)) return false;
            pixels.push_back(p);
            continue;
        }
        if (at >= data.size()) return false;
        const int header = u8(at++);
        const size_t count = static_cast<size_t>((header & 0x7F) + 1);
        if (pixels.size() + count > total) return false;
        if (header & 0x80) {
            uint32_t p;
            if (!readPixel(p)) return false;
            pixels.insert(pixels.end(), count, p);
        } else {
            for (size_t i = 0; i < count; ++i) {
                uint32_t p;
                if (!readPixel(p)) return false;
                pixels.push_back(p);
            }
        }
    }
    out.w = w; out.h = h; out.bgra.assign(total, 0);
    const bool topFirst = (descriptor & 0x20) != 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out.bgra[static_cast<size_t>(y) * w + x] = pixels[static_cast<size_t>(topFirst ? y : h - 1 - y) * w + x];
    if (premultiply) Premultiply(out);
    return true;
}

#ifdef _WIN32
namespace fs = std::filesystem;

// PNG (or anything else Windows can read) through WIC, converted straight to premultiplied BGRA
// (premultiply = false: plain BGRA, the colour under see-through pixels kept).
inline bool DecodeWic(const std::wstring& path, Image& out, bool premultiply = true)
{
    static const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) || true;
    (void)com;
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapSource* converted = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(WICConvertBitmapSource(premultiply ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppBGRA, frame, &converted))) {
        UINT w = 0, h = 0;
        if (SUCCEEDED(converted->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 1024 && h <= 1024) {
            out.w = static_cast<int>(w); out.h = static_cast<int>(h);
            out.bgra.assign(static_cast<size_t>(w) * h, 0);
            ok = SUCCEEDED(converted->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(out.bgra.data())));
        }
    }
    if (converted) converted->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return ok;
}

// Image bytes in memory (e.g. a PNG resource), scaled to w x h with WIC's high-quality filter, as
// premultiplied BGRA. Used for the client logo, which has no icon size for every place it is drawn.
inline bool DecodeWicMemory(const void* data, size_t size, int w, int h, Image& out)
{
    static const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) || true;
    (void)com;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICBitmapSource* converted = nullptr;
    bool ok = false;
    if (w > 0 && h > 0 && w <= 1024 && h <= 1024 &&
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame, static_cast<UINT>(w), static_cast<UINT>(h), WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, scaler, &converted))) {
        out.w = w; out.h = h;
        out.bgra.assign(static_cast<size_t>(w) * h, 0);
        ok = SUCCEEDED(converted->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(out.bgra.data())));
    }
    if (converted) converted->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return ok;
}

// One loaded icon plus the bitmap used to draw it (created on first use).
struct Icon { Image image; HBITMAP bitmap = nullptr; bool tried = false; };

// Square size x size copy of a premultiplied image, each pixel the average of the source area it covers.
inline Image Shrink(const Image& in, int size)
{
    Image out;
    out.w = out.h = size;
    out.bgra.assign(static_cast<size_t>(size) * size, 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int x0 = x * in.w / size, x1 = std::max(x0 + 1, (x + 1) * in.w / size);
            const int y0 = y * in.h / size, y1 = std::max(y0 + 1, (y + 1) * in.h / size);
            uint64_t sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const uint32_t p = in.bgra[static_cast<size_t>(sy) * in.w + sx];
                    for (int c = 0; c < 4; ++c) sum[c] += (p >> (c * 8)) & 0xFF;
                }
            const uint64_t n = static_cast<uint64_t>(x1 - x0) * (y1 - y0);
            uint32_t p = 0;
            for (int c = 0; c < 4; ++c) p |= static_cast<uint32_t>(sum[c] / n) << (c * 8);
            out.bgra[static_cast<size_t>(y) * size + x] = p;
        }
    return out;
}

// Creates the icon's drawing bitmap on first use. False when there is no picture.
inline bool MakeBitmap(Icon& icon)
{
    if (icon.image.bgra.empty()) return false;
    if (icon.bitmap) return true;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = icon.image.w;
    bi.bmiHeader.biHeight = -icon.image.h;   // top row first
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    icon.bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!icon.bitmap || !bits) return false;
    std::memcpy(bits, icon.image.bgra.data(), icon.image.bgra.size() * 4);
    return true;
}

// Finds <texture>.png / .tga (textures\items, then textures\ui): first in the player's active resource
// packs (highest priority first), then under the game's own data\resource_packs (the "vanilla" pack first).
inline bool LoadTexture(const std::wstring& gameDir, const std::vector<fs::path>& activePacks,
                        const std::string& texture, Image& out)
{
    std::error_code ec;
    const fs::path packs = fs::path(gameDir) / L"data" / L"resource_packs";
    std::vector<fs::path> roots(activePacks.begin(), activePacks.end());
    roots.push_back(packs / L"vanilla");
    for (fs::directory_iterator it(packs, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && it->path().filename() != L"vanilla") roots.push_back(it->path());
    const std::wstring name(texture.begin(), texture.end());
    // Items live in textures/items; the empty armor slot pictures in textures/ui.
    for (const auto& root : roots)
        for (const wchar_t* folder : {L"items", L"ui"}) {
            const fs::path base = root / L"textures" / folder / name;
            if (fs::is_regular_file(base.wstring() + L".png", ec) && DecodeWic(base.wstring() + L".png", out)) return true;
            if (fs::is_regular_file(base.wstring() + L".tga", ec)) {
                std::ifstream in(fs::path(base.wstring() + L".tga"), std::ios::binary);
                const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (DecodeTga(data, out)) return true;
            }
        }
    return false;
}

// Icons by texture name, for one game folder and pack list (cleared when either changes). Misses are remembered too.
class Cache {
    std::wstring gameDir_;
    std::vector<fs::path> packs_;
    std::map<std::string, Icon> icons_;
public:
    ~Cache() { Clear(); }
    void Clear() {
        for (auto& [name, icon] : icons_) if (icon.bitmap) DeleteObject(icon.bitmap);
        icons_.clear();
    }
    // nullptr when the game has no texture for this item (the HUD then shows text instead).
    const Icon* Get(const std::wstring& gameDir, const std::vector<fs::path>& activePacks, const std::string& itemId) {
        if (gameDir.empty() || itemId.empty()) return nullptr;
        if (gameDir != gameDir_ || activePacks != packs_) { Clear(); gameDir_ = gameDir; packs_ = activePacks; }
        auto& icon = icons_[TextureName(itemId)];
        if (!icon.tried) {
            icon.tried = true;
            if (!LoadTexture(gameDir, packs_, TextureName(itemId), icon.image)) icon.image = {};
            else if (TextureName(itemId).rfind("leather_", 0) == 0) TintLeather(icon.image);
        }
        return MakeBitmap(icon) ? &icon : nullptr;
    }
};

// Pictures by file path (pack_icon.png of each resource pack), shrunk once to `size` pixels with an
// area average so large photos stay smooth. Misses are remembered too.
class FileCache {
    std::map<std::wstring, Icon> icons_;
public:
    ~FileCache() { for (auto& [path, icon] : icons_) if (icon.bitmap) DeleteObject(icon.bitmap); }
    // nullptr when the file is missing or unreadable.
    const Icon* Get(const fs::path& file, int size) {
        auto& icon = icons_[file.wstring() + L"|" + std::to_wstring(size)];
        if (!icon.tried) {
            icon.tried = true;
            std::error_code ec;
            if (!fs::is_regular_file(file, ec) || !DecodeWic(file.wstring(), icon.image)) icon.image = {};
            else if (icon.image.w > size || icon.image.h > size) icon.image = Shrink(icon.image, size);
        }
        return MakeBitmap(icon) ? &icon : nullptr;
    }
};

// Draws the icon scaled to size x size with its transparency (pixel art stays sharp).
inline void Draw(HDC hdc, const Icon& icon, int x, int y, int size)
{
    HDC source = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(source, icon.bitmap);
    const int oldMode = SetStretchBltMode(hdc, COLORONCOLOR);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(hdc, x, y, size, size, source, 0, 0, icon.image.w, icon.image.h, blend);
    SetStretchBltMode(hdc, oldMode);
    SelectObject(source, old);
    DeleteDC(source);
}
#endif

}  // namespace ItemIcons

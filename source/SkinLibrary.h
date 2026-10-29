#pragma once
// The skins saved on this PC that the Closet can switch to: Minecraft's own folders under com.mojang
// (custom_skins = classic skins imported in the Dressing Room, skin_packs / development_skin_packs =
// unencrypted skin packs) and the game's built-in data\skin_packs. Marketplace packs (premium_cache)
// are encrypted and persona (Character Creator) skins are built by the game, so neither is listed.
// Pure parts (skins.json / .lang parsing, the front-view doll) are unit-tested: tests/skin_library.cpp.
#include "SkinChannel.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#endif

namespace SkinLibrary {

// One classic skin of a skins.json: its name key, texture file (relative to the pack) and arm model.
struct PackSkin { std::string name, texture; bool slim = false; };

// Minimal JSON reading for skins.json: the string fields of every object inside the "skins" array.
// Only classic skins are kept (geometry.humanoid.custom / customSlim); others need their own model.
// `serialize` / `localization` get the pack's own names (used for the .lang keys).
inline std::vector<PackSkin> ParseSkinsJson(const std::string& text, std::string* serialize = nullptr, std::string* localization = nullptr) {
    std::vector<PackSkin> out;
    size_t i = 0;
    const size_t n = text.size();
    auto skipSpace = [&]() {
        while (i < n) {
            if (text[i] == '/' && i + 1 < n && text[i + 1] == '/') { while (i < n && text[i] != '\n') i++; }
            else if (static_cast<unsigned char>(text[i]) <= ' ') i++;
            else break;
        }
    };
    auto readString = [&](std::string& s) -> bool {   // at '"'
        s.clear();
        if (i >= n || text[i] != '"') return false;
        for (i++; i < n && text[i] != '"'; i++) {
            if (text[i] == '\\' && i + 1 < n) { i++; s += text[i] == 'n' ? '\n' : text[i]; }
            else s += text[i];
        }
        if (i >= n) return false;
        i++;
        return true;
    };
    // Depth-tracking walk: 0 = top object, the "skins" array's objects are at depth 2.
    int depth = 0;
    bool inSkins = false, skinsArrayDepthSet = false;
    int skinsDepth = 0;
    PackSkin current;
    std::string geometry, key, value;
    std::string lastKey;
    while (i < n) {
        skipSpace();
        if (i >= n) break;
        const char c = text[i];
        if (c == '"') {
            if (!readString(key)) break;
            skipSpace();
            if (i < n && text[i] == ':') {
                i++;
                skipSpace();
                lastKey = key;
                if (i < n && text[i] == '"') {
                    if (!readString(value)) break;
                    if (inSkins && depth == skinsDepth + 1) {
                        if (key == "localization_name") current.name = value;
                        else if (key == "texture") current.texture = value;
                        else if (key == "geometry") geometry = value;
                    } else if (depth == 1) {
                        if (key == "serialize_name" && serialize) *serialize = value;
                        else if (key == "localization_name" && localization) *localization = value;
                    }
                }
            }
            continue;
        }
        if (c == '{' || c == '[') {
            depth++;
            if (c == '[' && depth == 2 && lastKey == "skins" && !skinsArrayDepthSet) { inSkins = true; skinsDepth = depth; skinsArrayDepthSet = true; }
            if (c == '{' && inSkins && depth == skinsDepth + 1) { current = PackSkin{}; geometry.clear(); }
            lastKey.clear();
        } else if (c == '}' || c == ']') {
            if (c == '}' && inSkins && depth == skinsDepth + 1) {
                const bool slim = geometry == "geometry.humanoid.customSlim";
                if ((slim || geometry == "geometry.humanoid.custom") && !current.texture.empty()) {
                    current.slim = slim;
                    if (current.name.empty()) current.name = current.texture;
                    out.push_back(current);
                }
            }
            if (c == ']' && inSkins && depth == skinsDepth) inSkins = false;
            depth--;
        }
        i++;
    }
    return out;
}

// key=value lines of a .lang file (# comments, trailing "\t#" comments cut).
inline std::map<std::string, std::string> ParseLang(const std::string& text) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);   // UTF-8 BOM
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string value = line.substr(eq + 1);
        const size_t comment = value.find("\t#");
        if (comment != std::string::npos) value.resize(comment);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
        out[line.substr(0, eq)] = value;
    }
    return out;
}

// The skin's shown name: "skin.<pack>.<name>" from the pack's .lang, else the key itself.
inline std::string SkinName(const std::map<std::string, std::string>& lang, const std::string& pack, const std::string& name) {
    const auto it = lang.find("skin." + pack + "." + name);
    return it != lang.end() && !it->second.empty() ? it->second : name;
}

// The skin seen from the front at one cell per skin pixel of a 64-wide skin (HD skins are sampled):
// 16 x 32 colours 0xAARRGGBB, A = 0 where nothing is drawn. Outer layers (hat, jacket, sleeves,
// trousers) are laid over their part where they are not transparent.
inline std::vector<uint32_t> FrontDoll(const uint8_t* rgba, uint32_t w, uint32_t h) {
    std::vector<uint32_t> out(16 * 32, 0);
    if (!rgba || w < 64 || w % 64 || (h != w && h != w / 2)) return out;
    const uint32_t k = w / 64;
    const bool modern = h == w;
    struct Part { int u, v, pw, ph, ou, ov, dx, dy; bool mirror; };
    const Part parts[] = {
        {8, 8, 8, 8, 40, 8, 4, 0, false},                                                  // head
        {20, 20, 8, 12, modern ? 20 : -1, 36, 4, 8, false},                                // body
        {44, 20, 4, 12, modern ? 44 : -1, 36, 0, 8, false},                                // right arm (viewer's left)
        {modern ? 36 : 44, modern ? 52 : 20, 4, 12, modern ? 52 : -1, 52, 12, 8, !modern}, // left arm
        {4, 20, 4, 12, modern ? 4 : -1, 36, 4, 20, false},                                 // right leg
        {modern ? 20 : 4, modern ? 52 : 20, 4, 12, modern ? 4 : -1, 52, 8, 20, !modern},   // left leg
    };
    for (const Part& p : parts)
        for (int layer = 0; layer < 2; layer++) {
            const int u0 = layer ? p.ou : p.u, v0 = layer ? p.ov : p.v;
            if (u0 < 0) continue;
            for (int j = 0; j < p.ph; j++)
                for (int i = 0; i < p.pw; i++) {
                    const int si = p.mirror ? p.pw - 1 - i : i;
                    const size_t sx = static_cast<size_t>(u0 + si) * k, sy = static_cast<size_t>(v0 + j) * k;
                    if (sx >= w || sy >= h) continue;
                    const uint8_t* px = rgba + (sy * w + sx) * 4;
                    if (px[3] < 128) continue;   // slim arms, empty outer layer
                    out[static_cast<size_t>(p.dy + j) * 16 + (p.dx + i)] =
                        0xFF000000u | (uint32_t(px[0]) << 16) | (uint32_t(px[1]) << 8) | px[2];
                }
        }
    return out;
}

// The same picture saved twice (a copy in custom_skins, the same skin in two packs ...) is listed once: the
// first one found stays. Same = same size, same arms and the same pixels.
template <class Skin>
inline void RemoveSameSkins(std::vector<Skin>& skins) {
    std::vector<Skin> kept;
    std::vector<uint32_t> hashes;
    for (Skin& s : skins) {
        const uint32_t hash = MyClientSkin::Hash(s.rgba, s.w, s.h);
        bool seen = false;
        for (size_t k = 0; k < kept.size() && !seen; k++)
            seen = hashes[k] == hash && kept[k].w == s.w && kept[k].h == s.h && kept[k].slim == s.slim && kept[k].rgba == s.rgba;
        if (seen) continue;
        hashes.push_back(hash);
        kept.push_back(std::move(s));
    }
    skins.swap(kept);
}

#ifdef _WIN32
namespace fs = std::filesystem;

struct Entry {
    std::wstring name;        // shown under the picture
    std::wstring source;      // "Imported", the pack's name ...
    fs::path file;
    bool slim = false;
    std::vector<uint8_t> rgba;   // straight RGBA
    uint32_t w = 0, h = 0;
    std::vector<uint32_t> doll;  // FrontDoll
};

inline std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(len > 0 ? len : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

// A skin PNG as straight (not premultiplied) RGBA. False unless it is a classic skin shape.
inline bool LoadPng(const fs::path& path, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
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
        SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame, &converted))) {
        UINT iw = 0, ih = 0;
        if (SUCCEEDED(converted->GetSize(&iw, &ih)) && iw >= 64 && iw <= MyClientSkin::MaxSide && iw % 64 == 0 && (ih == iw || ih == iw / 2)) {
            rgba.assign(static_cast<size_t>(iw) * ih * 4, 0);
            ok = SUCCEEDED(converted->CopyPixels(nullptr, iw * 4, iw * ih * 4, rgba.data()));
            w = iw; h = ih;
        }
    }
    if (converted) converted->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (!ok) { rgba.clear(); w = h = 0; }
    return ok;
}

inline std::wstring EnvPath(const wchar_t* name) {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD len = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return len && len < std::size(buffer) ? std::wstring(buffer, len) : std::wstring{};
}

// Every com.mojang folder of this PC: the Microsoft Store (UWP) install, and the newer GDK install's
// per-user folders (%APPDATA%\Minecraft Bedrock\Users\<id or Shared>\games\com.mojang), release and Preview.
inline std::vector<fs::path> MojangRoots() {
    std::vector<fs::path> roots;
    std::error_code ec;
    const std::wstring local = EnvPath(L"LOCALAPPDATA"), roaming = EnvPath(L"APPDATA");
    for (const wchar_t* package : {L"Microsoft.MinecraftUWP_8wekyb3d8bbwe", L"Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe"})
        if (!local.empty()) {
            const fs::path p = fs::path(local) / L"Packages" / package / L"LocalState" / L"games" / L"com.mojang";
            if (fs::is_directory(p, ec)) roots.push_back(p);
        }
    for (const wchar_t* app : {L"Minecraft Bedrock", L"Minecraft Bedrock Preview"})
        if (!roaming.empty()) {
            const fs::path users = fs::path(roaming) / app / L"Users";
            if (!fs::is_directory(users, ec)) continue;
            for (fs::directory_iterator it(users, ec), end; !ec && it != end; it.increment(ec)) {
                const fs::path p = it->path() / L"games" / L"com.mojang";
                if (fs::is_directory(p, ec)) roots.push_back(p);
            }
        }
    return roots;
}

// The folder the Closet's "Open folder" button shows: the first com.mojang's custom_skins.
inline fs::path CustomSkinsFolder() {
    const auto roots = MojangRoots();
    return roots.empty() ? fs::path{} : roots.front() / L"custom_skins";
}

constexpr size_t MaxEntries = 300;

inline void AddEntry(std::vector<Entry>& out, const fs::path& file, std::wstring name, std::wstring source, int slim /* -1 = guess */) {
    if (out.size() >= MaxEntries) return;
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(file, ec);
    for (const Entry& e : out) if (e.file == canonical) return;
    Entry entry;
    if (!LoadPng(file, entry.rgba, entry.w, entry.h)) return;
    entry.file = canonical;
    entry.name = std::move(name);
    entry.source = std::move(source);
    entry.slim = slim < 0 ? MyClientSkin::LooksSlim(entry.rgba.data(), entry.w, entry.h) : slim != 0;
    entry.doll = FrontDoll(entry.rgba.data(), entry.w, entry.h);
    out.push_back(std::move(entry));
}

// A skin pack folder (skins.json + PNGs + texts\en_US.lang).
inline void AddPack(std::vector<Entry>& out, const fs::path& pack, std::vector<std::wstring>& log) {
    std::error_code ec;
    const fs::path json = pack / L"skins.json";
    if (!fs::is_regular_file(json, ec)) return;
    std::string serialize, localization;
    const auto skins = ParseSkinsJson(ReadAll(json), &serialize, &localization);
    const auto lang = ParseLang(ReadAll(pack / L"texts" / L"en_US.lang"));
    std::string packName = localization.empty() ? serialize : localization;
    if (const auto it = lang.find("skinpack." + serialize); it != lang.end() && !it->second.empty()) packName = it->second;
    const size_t before = out.size();
    for (const PackSkin& s : skins)
        AddEntry(out, pack / Widen(s.texture), Widen(SkinName(lang, serialize, s.name)), Widen(packName), s.slim ? 1 : 0);
    log.push_back(pack.wstring() + L": " + std::to_wstring(out.size() - before) + L" of " + std::to_wstring(skins.size()));
}

// Every skin found, imported custom skins first. `log` gets one line per folder looked at.
inline std::vector<Entry> Scan(const std::wstring& gameDir, std::vector<std::wstring>& log) {
    std::vector<Entry> out;
    std::error_code ec;
    for (const fs::path& root : MojangRoots()) {
        const fs::path custom = root / L"custom_skins";
        if (fs::is_directory(custom, ec)) {
            const size_t before = out.size();
            for (fs::recursive_directory_iterator it(custom, ec), end; !ec && it != end; it.increment(ec)) {
                if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
                std::wstring ext = it->path().extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                if (ext == L".png" && it->is_regular_file(ec)) AddEntry(out, it->path(), it->path().stem().wstring(), L"Imported", -1);
            }
            log.push_back(custom.wstring() + L": " + std::to_wstring(out.size() - before) + L" skins");
        } else {
            log.push_back(custom.wstring() + L": no folder");
        }
        for (const wchar_t* sub : {L"skin_packs", L"development_skin_packs"}) {
            const fs::path packs = root / sub;
            if (!fs::is_directory(packs, ec)) continue;
            for (fs::directory_iterator it(packs, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_directory(ec)) AddPack(out, it->path(), log);
        }
    }
    if (!gameDir.empty()) {
        const fs::path packs = fs::path(gameDir) / L"data" / L"skin_packs";
        if (fs::is_directory(packs, ec))
            for (fs::directory_iterator it(packs, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_directory(ec)) AddPack(out, it->path(), log);
    }
    const size_t found = out.size();
    RemoveSameSkins(out);
    if (out.size() != found) log.push_back(std::to_wstring(found - out.size()) + L" duplicate skins hidden");
    return out;
}
#endif
}

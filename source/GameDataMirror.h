#pragma once
// A copy of the few game files the HUD's icons need (item / block textures, the texture lists, the .lang files),
// made by the DLL inside the game. Xbox-app (GDK) installs protect their Content folder: the launcher already
// cannot read Minecraft.Windows.exe there (it attaches instead), and the HUD, a normal process, could not read
// data\resource_packs either, so every icon (Armor HUD, Shulker Preview, Block Details and its tool) was a letter.
// The game process itself reads those files, so the DLL copies them to
//   %LOCALAPPDATA%\MyClient\game-data\data\resource_packs\<pack>\...
// (the same layout as the game folder: the HUD passes the mirror's root wherever it passed the game folder).
// stamp.txt holds "<game folder>|<exe size>|<exe write time>"; it is written last, so a copy cut short is redone.
#include <string>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

namespace MyClientGameData {

inline bool EndsWith(const std::string& s, const char* tail)
{
    const std::string t(tail);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
inline bool StartsWith(const std::string& s, const char* head) { return s.rfind(head, 0) == 0; }

struct BrEntry { std::string name; uint32_t offset, size; };

// Offsets are relative to the payload AFTER the entire table, not to the file start.
// Validate every record before exposing any: a bad later entry invalidates the whole archive.
inline bool ParseBrArchive(const uint8_t* data, size_t len, std::vector<BrEntry>& out, size_t& dataStart)
{
    out.clear(); dataStart = 0;
    constexpr uint8_t magic[] = {0x7d, 0x27, 0x25, 0xb1, 0xa0, 0x52, 0x70, 0x26};
    if (!data || len < 16 || std::memcmp(data, magic, sizeof magic) != 0) return false;
    const auto u32 = [](const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    };
    const uint32_t count = u32(data + 8);
    if (u32(data + 12) != 1 || count > (len - 16) / 256) return false;
    const size_t start = 16 + size_t(count) * 256;
    std::vector<BrEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto* record = data + 16 + i * 256;
        const size_t length = record[0];
        if (!length || length > 247) return false;
        for (size_t j = 1; j <= length; ++j)
            if (record[j] == 0 || record[j] > 0x7f) return false; // names are ASCII, not NUL-terminated
        std::string name(reinterpret_cast<const char*>(record + 1), length);
        if (name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos) return false;
        const uint32_t offset = u32(record + 248), size = u32(record + 252);
        // Subtraction avoids wraparound for hostile u32 offsets/sizes and on 32-bit hosts.
        if (offset > len - start || size > len - start - offset) return false;
        entries.push_back({std::move(name), offset, size});
    }
    out.swap(entries); dataStart = start;
    return true;
}

inline std::string BrArchiveDir(const std::string& relInPack)
{
    if (!StartsWith(relInPack, "__brarchive/") || !EndsWith(relInPack, ".brarchive")) return {};
    const std::string dir = relInPack.substr(12, relInPack.size() - 12 - 10);
    if (dir.empty() || dir.find_first_of("\\:") != std::string::npos) return {};
    size_t begin = 0;
    do {
        const size_t end = dir.find('/', begin);
        const std::string part = dir.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == "..") return {};
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
    return dir;
}

// Whether a file of a pack (path inside the pack, '/' separated, lower case) is copied.
inline bool Wanted(const std::string& rel)
{
    if (rel == "blocks.json" || rel == "manifest.json" || rel == "pack_icon.png" ||
        rel == "textures/item_texture.json" || rel == "textures/terrain_texture.json" || rel == "texts/languages.json")
        return true;
    if (StartsWith(rel, "texts/")) return EndsWith(rel, ".lang") && rel.find('/', 6) == std::string::npos;
    const bool picture = EndsWith(rel, ".png") || EndsWith(rel, ".tga");
    if (!picture) return false;
    if (StartsWith(rel, "textures/entity/") || StartsWith(rel, "textures/items/") || StartsWith(rel, "textures/blocks/")) return true;
    return StartsWith(rel, "textures/ui/empty_armor_slot_");
}

}  // namespace MyClientGameData

#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace MyClientGameData {
namespace fs = std::filesystem;

inline fs::path MirrorRoot()
{
    wchar_t base[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return fs::path(base) / L"MyClient" / L"game-data";
}

inline std::string ReadStamp(const fs::path& root)
{
    std::ifstream in(root / L"stamp.txt", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// True when this process can open a file under the game's packs (false on a protected GDK install).
inline bool PacksReadable(const std::wstring& gameDir)
{
    const fs::path probe = fs::path(gameDir) / L"data" / L"resource_packs" / L"vanilla" / L"manifest.json";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// True when the vanilla pack's item pictures sit as loose files there. 26.x keeps them inside __brarchive files,
// so a game folder that can be read (an install registered from a plain folder) still has none (r57-test56: no
// HUD icon at all on such an install, because the HUD read the game folder instead of the DLL's extracted copy).
inline bool HasLooseItemTextures(const std::wstring& root)
{
    std::error_code ec;
    return fs::is_directory(fs::path(root) / L"data" / L"resource_packs" / L"vanilla" / L"textures" / L"items", ec);
}

// The folder the HUD reads game textures from: the DLL's finished copy when there is one, else the game folder.
// The copy comes first: it holds the loose files and the pictures taken out of __brarchive. A game
// folder with a textures\items folder can still miss most pictures (2026-11-19, test59, 26.51.1:
// MyClient\game\26.51.1 was chosen and empty_armor_slot_* and copper_chestplate were all missing).
inline std::wstring TextureFolder(const std::wstring& gameDir)
{
    const fs::path root = MirrorRoot();
    std::error_code ec;
    if (!root.empty() && !ReadStamp(root).empty() && fs::is_directory(root / L"data" / L"resource_packs", ec))
        return root.wstring();
    return gameDir;
}
}  // namespace MyClientGameData
#endif

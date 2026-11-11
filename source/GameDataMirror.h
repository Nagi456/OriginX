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

namespace MyClientGameData {

inline bool EndsWith(const std::string& s, const char* tail)
{
    const std::string t(tail);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
inline bool StartsWith(const std::string& s, const char* head) { return s.rfind(head, 0) == 0; }

// Whether a file of a pack (path inside the pack, '/' separated, lower case) is copied.
inline bool Wanted(const std::string& rel)
{
    if (rel == "blocks.json" || rel == "manifest.json" || rel == "pack_icon.png" ||
        rel == "textures/item_texture.json" || rel == "textures/terrain_texture.json" || rel == "texts/languages.json")
        return true;
    if (StartsWith(rel, "texts/")) return EndsWith(rel, ".lang") && rel.find('/', 6) == std::string::npos;
    const bool picture = EndsWith(rel, ".png") || EndsWith(rel, ".tga");
    if (!picture) return false;
    if (StartsWith(rel, "textures/items/") || StartsWith(rel, "textures/blocks/")) return true;
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

// The folder the HUD reads game textures from: the game folder when it can read it, else the DLL's finished copy
// of it (empty when there is neither).
inline std::wstring TextureFolder(const std::wstring& gameDir)
{
    if (!gameDir.empty() && PacksReadable(gameDir)) return gameDir;
    const fs::path root = MirrorRoot();
    if (root.empty() || ReadStamp(root).empty()) return gameDir;
    std::error_code ec;
    return fs::is_directory(root / L"data" / L"resource_packs", ec) ? root.wstring() : gameDir;
}
}  // namespace MyClientGameData
#endif

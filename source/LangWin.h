#pragma once
// Lang.h's Windows half: where the choice is kept and how the first one is picked. Include after <windows.h>.
#include "Lang.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Lang {

inline std::filesystem::path FilePath()
{
    PWSTR base = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) path = std::filesystem::path(base) / L"MyClient" / L"language.txt";
    CoTaskMemFree(base);
    return path;
}

inline std::optional<Id> LoadSaved()
{
    const auto path = FilePath();
    if (path.empty()) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::stringstream text;
    text << input.rdbuf();
    return Parse(text.str());
}

inline void Save(Id id)
{
    const auto path = FilePath();
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << Key(id);
}

inline std::optional<Id> FromDownload(const std::filesystem::path& exe)
{
    std::ifstream input(std::filesystem::path(exe.wstring() + L":Zone.Identifier"), std::ios::binary);
    if (!input) return std::nullopt;
    std::stringstream text;
    text << input.rdbuf();
    return FromZone(text.str());
}

// The saved choice; else (the launcher, first start) the download page's language, then Windows' display language.
inline void Load(const std::filesystem::path& downloadedExe = {})
{
    if (auto saved = LoadSaved()) { Current() = *saved; return; }
    std::optional<Id> picked;
    if (!downloadedExe.empty()) picked = FromDownload(downloadedExe);
    Current() = picked ? *picked : FromLangId(GetUserDefaultUILanguage());
    if (!downloadedExe.empty()) Save(Current());
}

}  // namespace Lang

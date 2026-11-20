#pragma once
// Logs say %USERPROFILE%, %LOCALAPPDATA% and %APPDATA% instead of the folders themselves, so a log a user shares
// does not carry their Windows user name (release-checklist 2026-11-20, required item 4).
// Used by every log writer: the DLL (client/Log.cpp, GameSigs.cpp), the HUD and the launcher.
// RedactPaths is pure (tests/log_privacy.cpp); UserFolders reads this PC's folders (Windows only).
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LogPrivacy {

using Folders = std::vector<std::pair<std::string, std::string>>;   // {folder as UTF-8, "%NAME%"}

inline char Fold(char c)
{
    if (c == '/') return '\\';
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline bool SameAt(std::string_view text, size_t at, std::string_view folder)
{
    if (at + folder.size() > text.size()) return false;
    for (size_t i = 0; i < folder.size(); ++i)
        if (Fold(text[at + i]) != Fold(folder[i])) return false;
    // a whole folder name only: "C:\Users\ann" must not eat the start of "C:\Users\anna"
    const size_t end = at + folder.size();
    return end == text.size() || text[end] == '\\' || text[end] == '/' || text[end] == '"' || text[end] == '\'' ||
           std::isspace(static_cast<unsigned char>(text[end])) || text[end] == ')' || text[end] == ',' || text[end] == ';';
}

// Every folder (longest first, ignoring case and / versus \), then any "\Users\<name>" left over (another
// spelling of the profile, e.g. a path under a different drive letter), becomes its name.
inline std::string RedactPaths(std::string_view text, Folders folders)
{
    folders.erase(std::remove_if(folders.begin(), folders.end(), [](const auto& f) { return f.first.size() < 4; }), folders.end());
    std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();)
    {
        bool replaced = false;
        for (const auto& [folder, name] : folders)
            if (SameAt(text, i, folder))
            {
                out += name;
                i += folder.size();
                replaced = true;
                break;
            }
        if (replaced) continue;
        // "\Users\<name>" (or "/Users/<name>") not covered above
        static constexpr std::string_view users = "\\users\\";
        if (i + users.size() < text.size() && SameAt(text, i, std::string_view("\\Users")) && Fold(text[i + 6]) == '\\')
        {
            size_t end = i + users.size();
            // a user name may hold spaces: it ends at the next separator, quote or line break
            while (end < text.size() && text[end] != '\\' && text[end] != '/' && text[end] != '"' && text[end] != '\'' &&
                   text[end] != '\r' && text[end] != '\n' && text[end] != '\t')
                ++end;
            const std::string_view name = text.substr(i + users.size(), end - i - users.size());
            if (!name.empty() && name != "Public" && name != "Default" && name != "%USERNAME%")
            {
                out += text.substr(i, users.size());
                out += "%USERNAME%";
                i = end;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

} // namespace LogPrivacy

#ifdef _WIN32
#include <windows.h>
namespace LogPrivacy {
inline std::string Utf8(const std::wstring& wide)
{
    if (wide.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size : 0, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
    return out;
}
// This PC's folders, read once.
inline const Folders& UserFolders()
{
    static const Folders folders = [] {
        Folders list;
        for (const wchar_t* name : {L"LOCALAPPDATA", L"APPDATA", L"USERPROFILE", L"TEMP", L"TMP"})
        {
            wchar_t value[MAX_PATH]{};
            const DWORD size = GetEnvironmentVariableW(name, value, MAX_PATH);
            if (!size || size >= MAX_PATH) continue;
            std::wstring folder(value, size);
            while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/')) folder.pop_back();
            list.push_back({Utf8(folder), "%" + Utf8(name) + "%"});
        }
        return list;
    }();
    return folders;
}
inline std::string Redact(std::string_view text) { return RedactPaths(text, UserFolders()); }
inline std::string Redact(const std::wstring& text) { return RedactPaths(Utf8(text), UserFolders()); }
} // namespace LogPrivacy
#endif

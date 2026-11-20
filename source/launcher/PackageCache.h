#pragma once
// The version-switch engine's msixvc cache and switch-log reading (design:
// docs/superpowers/specs/2026-11-19-version-switch-engine-design.md). Pure functions only: no file system, so
// tests/launcher_package_cache.cpp runs them on Linux. The launcher does the I/O around them.
#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace MyClientLauncher {
namespace PackageCache {

// Provisional limits (design §3, user to confirm): at most 3 versions and 20 GB.
constexpr size_t DefaultMaxVersions = 3;
constexpr uint64_t DefaultMaxBytes = 20ull * 1024 * 1024 * 1024;

inline constexpr const char* Prefix = "Microsoft.MinecraftUWP_";
inline constexpr const char* Extension = ".msixvc";
inline constexpr const char* PartialSuffix = ".partial";

// A version id such as 26.45.1: digits and dots only, no leading/trailing/double dot.
inline bool ValidId(const std::string& id)
{
    if (id.empty() || id.size() > 32 || id.front() == '.' || id.back() == '.') return false;
    for (size_t i = 0; i < id.size(); ++i)
    {
        const char c = id[i];
        if (c == '.') { if (id[i + 1] == '.') return false; }
        else if (c < '0' || c > '9') return false;
    }
    return true;
}
inline std::string FileName(const std::string& id) { return std::string(Prefix) + id + Extension; }
inline std::string PartialName(const std::string& id) { return FileName(id) + PartialSuffix; }

// The version id of a finished cache file, or "" for anything else (a .partial, another file).
inline std::string IdOf(const std::string& name)
{
    const std::string prefix = Prefix, extension = Extension;
    if (name.size() <= prefix.size() + extension.size() || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - extension.size(), extension.size(), extension) != 0)
        return {};
    const std::string id = name.substr(prefix.size(), name.size() - prefix.size() - extension.size());
    return ValidId(id) ? id : std::string{};
}
inline bool IsPartial(const std::string& name)
{
    const std::string suffix = PartialSuffix;
    return name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0 &&
           !IdOf(name.substr(0, name.size() - suffix.size())).empty();
}

struct Entry { std::string id; uint64_t bytes = 0; int64_t lastUsed = 0; };   // lastUsed: any increasing clock

// Which cached versions to delete so that at most maxVersions files and maxBytes remain, least recently used
// first. Versions in `keep` (the one being installed, the one running) are never chosen, even when that leaves the
// cache over the limit.
inline std::vector<std::string> ToEvict(std::vector<Entry> entries, const std::set<std::string>& keep,
                                        size_t maxVersions = DefaultMaxVersions, uint64_t maxBytes = DefaultMaxBytes)
{
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.lastUsed != b.lastUsed ? a.lastUsed < b.lastUsed : a.id < b.id;
    });
    size_t count = entries.size();
    uint64_t total = 0;
    for (const auto& e : entries) total += e.bytes;
    std::vector<std::string> evict;
    for (const auto& e : entries)
    {
        if (count <= maxVersions && total <= maxBytes) break;
        if (keep.count(e.id)) continue;
        evict.push_back(e.id);
        --count;
        total -= e.bytes;
    }
    return evict;
}

// Install failures that mean the package file itself is bad (worth deleting it and downloading once more):
// Windows could not open or read it, or its signature does not match its content. Registration errors are
// kept apart (2026-11-20 report #9/#10): they say nothing about the file, so a good cached file is not thrown
// away for them. 0x80073CF6 is ERROR_INSTALL_REGISTRATION_FAILURE, "The package can't be registered"
// (learn.microsoft.com/windows/win32/appxpkg/troubleshooting): a registration error, not a damaged file.
//   0x80073CF0 ERROR_INSTALL_OPEN_PACKAGE_FAILED   the package couldn't be opened
//   0x8007000D ERROR_INVALID_DATA                  0x80070570 ERROR_FILE_CORRUPT
//   0x80080204 APPX_E_INVALID_MANIFEST             0x800B0100 TRUST_E_NOSIGNATURE
//   0x80096010 TRUST_E_BAD_DIGEST (content does not match its signature)
inline bool PackageFileDamaged(const std::string& log)
{
    static const char* codes[] = {"0x80073CF0", "0x8007000D", "0x80070570", "0x80080204", "0x800B0100", "0x80096010"};
    std::string upper = log;
    for (auto& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const char* code : codes)
    {
        std::string wanted = code;
        for (auto& c : wanted) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (upper.find(wanted) != std::string::npos) return true;
    }
    return false;
}

} // namespace PackageCache

// What the switch script's check mode wrote (switch-check.log).
struct SwitchCheck
{
    std::string installedVersion;   // "1.26.5101.0"
    std::string signatureKind;      // "Store", "None", "Developer", ...
    std::string folderRegistration; // InstallLocation when registered from a folder, else ""
    bool alreadyNormal = false;     // "Normal launch is already <id>"
    bool developmentMode = false;   // "Development mode registration: True" (decision A, 2026-11-19)
    bool loose() const { return signatureKind == "None" || signatureKind == "Developer" || developmentMode; }
};
inline SwitchCheck ParseSwitchCheck(const std::string& text)
{
    SwitchCheck result;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        auto after = [&](const char* key) -> const char* {
            const size_t n = std::char_traits<char>::length(key);
            return line.compare(0, n, key) == 0 ? line.c_str() + n : nullptr;
        };
        if (const char* v = after("Installed: "))
        {
            const std::string rest = v;
            const size_t open = rest.find(" ("), close = rest.rfind(')');
            if (open != std::string::npos && close != std::string::npos && close > open)
            {
                result.installedVersion = rest.substr(0, open);
                result.signatureKind = rest.substr(open + 2, close - open - 2);
            }
            else result.installedVersion = rest;
        }
        else if (const char* v = after("Folder registration: ")) result.folderRegistration = v;
        else if (after("Normal launch is already ")) result.alreadyNormal = true;
        else if (const char* v = after("Development mode registration: ")) result.developmentMode = std::string(v) == "True";
        if (end == text.size()) break;
        start = end + 1;
    }
    return result;
}

} // namespace MyClientLauncher

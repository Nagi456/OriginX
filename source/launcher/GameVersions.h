#pragma once
#include "Install.h"
#include <map>

namespace MyClientLauncher {
// Released Minecraft versions, from the public list LeviLauncher reads (version numbers and
// Microsoft download links; only the numbers are used here).
inline constexpr const char* VersionDbUrl =
    "https://raw.githubusercontent.com/LiteLDev/minecraft-windows-gdk-version-db/refs/heads/main/historical_versions.json";

// The id MyClient uses ("26.51.1", or "1.21.120.4" before 26.x) from any spelling of a version:
// MicrosoftGame.config packs patch and build ("1.26.5101.0"), the database pads the build and
// names the channel ("Release 1.26.51.01"). Empty when the text is not a version.
inline std::string VersionId(std::string text)
{
    const auto space = text.rfind(' ');
    if (space != std::string::npos) text = text.substr(space + 1);
    std::vector<unsigned long> parts;
    size_t start = 0;
    while (start <= text.size())
    {
        const auto dot = std::min(text.find('.', start), text.size());
        const auto part = text.substr(start, dot - start);
        if (part.empty() || part.size() > 9 || part.find_first_not_of("0123456789") != std::string::npos) return {};
        parts.push_back(std::stoul(part));
        start = dot + 1;
    }
    if (parts.size() == 4 && parts[0] >= 26) parts.pop_back();   // "26.51.1.0"
    else if (parts.size() == 4 && parts[3] == 0 && parts[2] >= 100)
        parts = {parts[0], parts[1], parts[2] / 100, parts[2] % 100};   // "1.26.5101.0"
    if (parts.size() == 4 && parts[0] == 1 && parts[1] >= 26) parts.erase(parts.begin());
    if (parts.size() != 3 && parts.size() != 4) return {};
    std::string id;
    for (const auto part : parts) id += (id.empty() ? "" : ".") + std::to_string(part);
    return id;
}

// Negative when a is older than b.
inline int CompareVersionIds(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size())
    {
        unsigned long x = 0, y = 0;
        for (; i < a.size() && a[i] != '.'; ++i) x = x * 10 + static_cast<unsigned long>(a[i] - '0');
        for (; j < b.size() && b[j] != '.'; ++j) y = y * 10 + static_cast<unsigned long>(b[j] - '0');
        if (x != y) return x < y ? -1 : 1;
        ++i;
        ++j;
    }
    return 0;
}

// Release versions in the database, newest first, each once.
inline std::vector<std::string> ParseVersionDb(const Json& db)
{
    if (!db.is_object() || !db.contains("releaseVersions") || !db.at("releaseVersions").is_array())
        throw std::runtime_error("公開版の一覧の形式が不正です。");
    std::vector<std::string> ids;
    for (const auto& item : db.at("releaseVersions"))
    {
        if (!item.is_object() || !item.contains("version") || !item.at("version").is_string()) continue;
        const auto id = VersionId(item.at("version").get<std::string>());
        if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [](const std::string& a, const std::string& b) { return CompareVersionIds(a, b) > 0; });
    return ids;
}

// A Minecraft package on Microsoft's Xbox CDN (the links in the database point there). Windows checks
// the package's Store signature when it installs it, so the plain-HTTP mirrors in the list are kept too.
inline bool GamePackageUrl(const std::string& url)
{
    const auto text = Lower(url);
    size_t start = 0;
    if (text.rfind("https://", 0) == 0) start = 8;
    else if (text.rfind("http://", 0) == 0) start = 7;
    else return false;
    const auto slash = text.find('/', start);
    if (slash == std::string::npos || text.find_first_of("?#@\\ ") != std::string::npos || text.find("..") != std::string::npos) return false;
    const auto host = text.substr(start, slash - start);
    if (host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos) return false;
    const auto endsWith = [](const std::string& value, const std::string& tail) {
        return value.size() > tail.size() && value.compare(value.size() - tail.size(), tail.size(), tail) == 0;
    };
    if (!endsWith(host, ".xboxlive.com") && !endsWith(host, ".xboxlive.cn")) return false;
    const auto file = text.substr(text.rfind('/') + 1);
    return file.rfind("microsoft.minecraftuwp_", 0) == 0 && endsWith(file, ".msixvc");
}

// Download links of every released version (the database's "urls"), only the ones on the Xbox CDN.
inline std::map<std::string, std::vector<std::string>> ParseVersionLinks(const Json& db)
{
    std::map<std::string, std::vector<std::string>> links;
    if (!db.is_object() || !db.contains("releaseVersions") || !db.at("releaseVersions").is_array()) return links;
    for (const auto& item : db.at("releaseVersions"))
    {
        if (!item.is_object() || !item.contains("version") || !item.at("version").is_string() ||
            !item.contains("urls") || !item.at("urls").is_array()) continue;
        const auto id = VersionId(item.at("version").get<std::string>());
        if (id.empty() || links.count(id)) continue;
        std::vector<std::string> urls;
        for (const auto& url : item.at("urls"))
            if (url.is_string() && GamePackageUrl(url.get<std::string>())) urls.push_back(url.get<std::string>());
        if (!urls.empty()) links[id] = urls;
    }
    return links;
}

// The order to try the links in: the .com hosts before the .cn ones, each over HTTPS first.
inline std::vector<std::string> PackageDownloadOrder(const std::vector<std::string>& urls)
{
    std::vector<std::string> sorted = urls;
    std::stable_sort(sorted.begin(), sorted.end(), [](const std::string& a, const std::string& b) {
        return (Lower(a).find(".xboxlive.cn/") == std::string::npos) > (Lower(b).find(".xboxlive.cn/") == std::string::npos);
    });
    std::vector<std::string> order;
    for (const auto& url : sorted)
    {
        const auto secure = Lower(url).rfind("http://", 0) == 0 ? "https://" + url.substr(7) : url;
        for (const auto& candidate : {secure, url})
            if (std::find(order.begin(), order.end(), candidate) == order.end()) order.push_back(candidate);
    }
    return order;
}

// Version id of the Release game in a folder, empty when it has none (Preview, broken, missing).
inline std::string InstalledVersion(const fs::path& directory)
{
    try
    {
        const auto identity = ReadGameIdentity(directory);
        return ReleaseGameName(identity.name) && identity.listsExecutable ? VersionId(identity.version) : std::string{};
    }
    catch (const std::exception&) { return {}; }
}

// Where LeviLauncher puts the versions it installs: its "base_root" setting when set, and the
// default %APPDATA%\levilauncher.exe\versions.
inline std::vector<fs::path> LeviLauncherVersionRoots(const fs::path& appData)
{
    const auto home = appData / "levilauncher.exe";
    std::vector<fs::path> roots;
    try
    {
        const auto config = ReadJson(home / "config.json");
        const auto base = config.value("base_root", std::string{});
        if (!base.empty()) roots.push_back(fs::u8path(base) / "versions");
    }
    catch (const std::exception&) { /* No LeviLauncher, or an unreadable config. */ }
    roots.push_back(home / "versions");
    return roots;
}

struct CatalogEntry { std::string id; bool available = false; };

struct VersionRow {
    std::string id;
    bool published = false;   // in the public list of released versions
    bool inCatalog = false;   // MyClient lists it
    bool supported = false;   // MyClient has a DLL for it
    bool installed = false;   // found on this PC
    bool downloadable = false;   // the database has a link to Microsoft's package
    size_t catalogIndex = 0;
};

// Every version from the three sources once, newest first.
inline std::vector<VersionRow> VersionRows(const std::vector<std::string>& published, const std::vector<CatalogEntry>& catalog,
                                           const std::vector<std::string>& installed,
                                           const std::vector<std::string>& downloadable = {})
{
    std::map<std::string, VersionRow> rows;
    const auto row = [&rows](const std::string& id) -> VersionRow& { auto& r = rows[id]; r.id = id; return r; };
    for (const auto& id : published) row(id).published = true;
    for (size_t i = 0; i < catalog.size(); ++i)
    {
        auto& r = row(catalog[i].id);
        r.inCatalog = true;
        r.supported = catalog[i].available;
        r.catalogIndex = i;
    }
    for (const auto& id : installed) row(id).installed = true;
    for (const auto& id : downloadable) if (rows.count(id)) rows[id].downloadable = true;
    std::vector<VersionRow> result;
    for (auto& [id, r] : rows) result.push_back(r);
    std::sort(result.begin(), result.end(), [](const VersionRow& a, const VersionRow& b) { return CompareVersionIds(a.id, b.id) > 0; });
    return result;
}
} // namespace MyClientLauncher

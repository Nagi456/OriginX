#pragma once
// Bulk pack install, HUD side. Everything that is dropped into the "inbox" folder (.mcpack, .mcaddon,
// .zip, or an unpacked folder) is opened, every manifest.json inside is classified and the pack is
// copied into the matching game folder (resource_packs / behavior_packs / skin_packs) in one go.
// Unpacking an archive is passed in as a function, so this file is plain file handling and unit-tested.
#include "TexturePacks.h"
#include <functional>

namespace PackInstall {

namespace fs = std::filesystem;

enum class Kind { Unknown, Resource, Behavior, Skin, World };

struct Info {
    bool valid = false;
    Kind kind = Kind::Unknown;
    std::string name;
    std::string uuid;
};

// Reads the header (name, uuid) and the first module type of a manifest.json.
inline Info Inspect(const std::string& text)
{
    Info info;
    const size_t header = text.find("\"header\"");
    if (header == std::string::npos) return info;
    const std::string tail = text.substr(header);
    static const std::regex uuidRe("\"uuid\"\\s*:\\s*\"([^\"]+)\"");
    static const std::regex nameRe("\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    static const std::regex typeRe("\"type\"\\s*:\\s*\"([A-Za-z_]+)\"");
    std::smatch match;
    if (!std::regex_search(tail, match, uuidRe)) return info;
    info.uuid = TexturePacks::Lower(match[1].str());
    if (!TexturePacks::ValidUuid(info.uuid)) return info;
    if (std::regex_search(tail, match, nameRe)) info.name = TexturePacks::DecodeJsonString(match[1].str());
    if (std::regex_search(tail, match, typeRe)) {
        const std::string type = TexturePacks::Lower(match[1].str());
        if (type == "resources" || type == "interface") info.kind = Kind::Resource;
        else if (type == "data" || type == "script" || type == "client_data") info.kind = Kind::Behavior;
        else if (type == "skin_pack") info.kind = Kind::Skin;
        else if (type == "world_template") info.kind = Kind::World;
    }
    info.valid = info.kind != Kind::Unknown;
    return info;
}

inline const char* SubFolder(Kind kind)
{
    switch (kind) {
    case Kind::Resource: return "resource_packs";
    case Kind::Behavior: return "behavior_packs";
    case Kind::Skin: return "skin_packs";
    default: return "";
    }
}

// A safe folder name: letters/digits/'-'/'_' only, plus the first 8 characters of the uuid.
inline std::string FolderName(const std::string& name, const std::string& uuid)
{
    std::string out;
    for (unsigned char c : name) {
        if (out.size() >= 40) break;
        if (std::isalnum(c)) out.push_back(static_cast<char>(c));
        else if (c == ' ' || c == '-' || c == '_') out.push_back('_');
    }
    if (out.empty()) out = "pack";
    return out + "_" + uuid.substr(0, 8);
}

inline bool IsArchive(const fs::path& path)
{
    std::string ext = TexturePacks::Lower(path.extension().string());
    return ext == ".mcpack" || ext == ".mcaddon" || ext == ".zip";
}

// Folders under `root` that directly contain a manifest.json (not looking inside a pack once found).
inline void FindPackFolders(const fs::path& root, int depth, std::vector<fs::path>& found)
{
    std::error_code ec;
    if (fs::is_regular_file(root / "manifest.json", ec)) { found.push_back(root); return; }
    if (depth <= 0) return;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        if (it->is_directory(dirEc) && !dirEc) FindPackFolders(it->path(), depth - 1, found);
    }
}

inline bool CopyFileTruncating(const fs::path& from, const fs::path& to)
{
    std::ifstream in(from, std::ios::binary);
    if (!in) return false;
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    char buffer[1 << 16];
    while (in) {
        in.read(buffer, sizeof buffer);
        if (in.gcount() > 0) out.write(buffer, in.gcount());
    }
    return static_cast<bool>(out);
}

inline bool CopyTree(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) return false;
    for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path relative = fs::relative(it->path(), from, ec);
        if (ec) return false;
        const fs::path target = to / relative;
        if (it->is_directory()) { fs::create_directories(target, ec); if (ec) return false; }
        else if (it->is_regular_file()) {
            fs::create_directories(target.parent_path(), ec);
            if (ec || !CopyFileTruncating(it->path(), target)) return false;
        }
    }
    return !ec;
}

struct Summary {
    int resource = 0, behavior = 0, skin = 0;
    int skipped = 0;   // worlds, broken manifests
    int failed = 0;    // copy / unpack errors
    int files = 0;     // inbox entries that were handled
    int Installed() const { return resource + behavior + skin; }
};

enum class Result { Installed, Skipped, Failed };

// Installs one unpacked pack folder below `gameRoot` (...\com.mojang). An older copy of the same pack
// (same uuid, found by folder name) is replaced.
inline Result InstallFolder(const fs::path& source, const fs::path& gameRoot, Summary& summary)
{
    std::error_code ec;
    const std::string text = TexturePacks::ReadFile(source / "manifest.json");
    const Info info = Inspect(text);
    if (!info.valid || info.kind == Kind::World) { ++summary.skipped; return Result::Skipped; }
    const fs::path target = gameRoot / SubFolder(info.kind) / FolderName(info.name, info.uuid);
    fs::remove_all(target, ec);
    if (!CopyTree(source, target)) { ++summary.failed; return Result::Failed; }
    if (info.kind == Kind::Resource) ++summary.resource;
    else if (info.kind == Kind::Behavior) ++summary.behavior;
    else ++summary.skin;
    return Result::Installed;
}

using Extractor = std::function<bool(const fs::path& archive, const fs::path& destination)>;

// Unpacks `archive` into `work`, including archives that sit inside it (.mcaddon holding .mcpack files),
// then installs every pack found. Returns how many packs were installed.
inline void InstallArchive(const fs::path& archive, const fs::path& work, const fs::path& gameRoot,
                           const Extractor& extract, Summary& summary, int level = 0)
{
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    if (!extract(archive, work)) { ++summary.failed; return; }
    std::vector<fs::path> found;
    FindPackFolders(work, 4, found);
    for (const auto& folder : found) InstallFolder(folder, gameRoot, summary);
    if (level >= 2) return;
    std::vector<fs::path> nested;
    for (fs::recursive_directory_iterator it(work, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file() && IsArchive(it->path())) nested.push_back(it->path());
    int index = 0;
    for (const auto& inner : nested)
        InstallArchive(inner, work.parent_path() / (work.filename().string() + "_n" + std::to_string(level) + "_" + std::to_string(index++)),
                       gameRoot, extract, summary, level + 1);
}

// Installs everything in `inbox`. Handled entries are moved to inbox\done (never deleted).
inline Summary InstallInbox(const fs::path& inbox, const fs::path& gameRoot, const Extractor& extract)
{
    Summary summary;
    std::error_code ec;
    if (!fs::is_directory(inbox, ec) || !fs::is_directory(gameRoot, ec)) return summary;
    const fs::path done = inbox / "done";
    const fs::path work = inbox / ".work";
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(inbox, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name == "done" || name == ".work") continue;
        entries.push_back(it->path());
    }
    int index = 0;
    for (const auto& entry : entries) {
        const int failedBefore = summary.failed, skippedBefore = summary.skipped, installedBefore = summary.Installed();
        if (fs::is_directory(entry, ec)) {
            std::vector<fs::path> found;
            FindPackFolders(entry, 4, found);
            for (const auto& folder : found) InstallFolder(folder, gameRoot, summary);
            if (found.empty()) ++summary.skipped;
        } else if (IsArchive(entry)) {
            InstallArchive(entry, work / std::to_string(index), gameRoot, extract, summary);
        } else { ++summary.skipped; continue; }
        ++index; ++summary.files;
        if (summary.failed != failedBefore) continue;       // keep failures in the inbox so they can be retried
        (void)skippedBefore; (void)installedBefore;
        fs::create_directories(done, ec);
        fs::path target = done / entry.filename();
        for (int n = 1; fs::exists(target, ec) && n < 1000; ++n)
            target = done / (entry.stem().string() + "_" + std::to_string(n) + entry.extension().string());
        fs::rename(entry, target, ec);
    }
    fs::remove_all(work, ec);
    return summary;
}

}  // namespace PackInstall

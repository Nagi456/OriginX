#pragma once
#include "LitePack.h"
#include "TexturePacks.h"

namespace HomeBrandPack {
inline constexpr const char* Uuid = "b26973a5-42b0-4b36-9c02-50bada83622f";
inline std::filesystem::path Folder(const std::filesystem::path& local) {
    const auto root = LitePack::GeneratedPackRoot(local);
    return root.empty() ? std::filesystem::path{} : root / "OriginX_HomeBrand";
}
inline std::string Manifest() {
    return std::string("{\"format_version\":2,\"header\":{\"name\":\"OriginX Home\",\"description\":\"OriginX session UI\",\"uuid\":\"") + Uuid +
        "\",\"version\":[1,0,1],\"min_engine_version\":[1,16,0]},\"modules\":[{\"type\":\"resources\",\"uuid\":\"c05df281-b55a-43ae-b85c-c75448996b97\",\"version\":[1,0,1]}]}";
}
inline bool Write(const std::filesystem::path& local, bool wanted) {
    const auto folder = Folder(local);
    if (folder.empty()) return !wanted;
    if (!wanted) {
        const bool ui = LitePack::Remove(folder / "ui/start_screen.json");
        return LitePack::Remove(folder / "manifest.json") && ui;
    }
    return LitePack::WriteFile(folder / "ui/start_screen.json", LitePack::HomeBrandJson()) &&
           LitePack::WriteFile(folder / "manifest.json", Manifest());
}
inline bool CleanLegacy(const std::vector<std::filesystem::path>& folders) {
    bool ok = true;
    for (const auto& folder : folders) {
        std::error_code error;
        const bool exists = std::filesystem::exists(folder / "ui/start_screen.json", error);
        if (error) { ok = false; continue; }
        if (!exists) continue;
        TexturePacks::Pack owner;
        if (!TexturePacks::ParseManifest(TexturePacks::ReadFile(folder / "manifest.json"), owner)) {
            ok = false; // unknown owner or unreadable manifest: leave the file alone and retry
            continue;
        }
        if (owner.uuid == LitePack::kPackUuid)
            ok = LitePack::Remove(folder / "ui/start_screen.json") && ok;
    }
    return ok;
}
inline std::vector<TexturePacks::Entry> WithEntry(const std::vector<TexturePacks::Entry>& stack, bool wanted) {
    std::vector<TexturePacks::Entry> result;
    if (wanted) result.push_back({Uuid, std::string("{\"pack_id\":\"") + Uuid + "\",\"version\":[1,0,1]}"});
    for (const auto& entry : stack) if (entry.uuid != Uuid) result.push_back(entry);
    return result;
}
} // namespace HomeBrandPack

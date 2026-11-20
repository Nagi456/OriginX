#pragma once
#include "LitePack.h"
#include "TexturePacks.h"

namespace JavaChatPack {
inline constexpr const char* Uuid = "dbcaa887-a659-4c43-bcbe-6b817063aaf1";
inline const char* HideJson() { return R"JSON({"namespace":"hud","root_panel/chat_stack/chat_panel":{"visible":false,"bindings":[]}})JSON"; }
inline bool Requested(const std::filesystem::path& local) {
    const auto root = LitePack::GeneratedPackRoot(local);
    return !root.empty() && TexturePacks::ReadFile(root / "OriginX_JavaChat/requested.txt") == "on";
}
inline std::filesystem::path Folder(const std::filesystem::path& local) {
    const auto root = LitePack::GeneratedPackRoot(local);
    return root.empty() ? std::filesystem::path{} : root / "OriginX_JavaChat";
}
inline std::string Manifest() {
    return std::string("{\"format_version\":2,\"header\":{\"name\":\"OriginX Chat\",\"description\":\"OriginX session chat\",\"uuid\":\"") + Uuid +
        "\",\"version\":[1,0,1],\"min_engine_version\":[1,16,0]},\"modules\":[{\"type\":\"resources\",\"uuid\":\"2a1b548d-a299-474a-8724-a14ac51ef8b7\",\"version\":[1,0,1]}]}";
}
inline bool Write(const std::filesystem::path& local, bool wanted) {
    const auto folder = Folder(local);
    if (folder.empty()) return !wanted;
    // Keep the immutable payload for native reads already in flight. OFF removes only its active-stack entry.
    // This private cache is never registered by normal Minecraft/other clients.
    if (!wanted) return true;
    if (TexturePacks::ReadFile(folder / "ui/hud_screen.json") == HideJson() &&
        TexturePacks::ReadFile(folder / "manifest.json") == Manifest()) return true;
    return LitePack::WriteFile(folder / "ui/hud_screen.json", HideJson()) &&
           LitePack::WriteFile(folder / "manifest.json", Manifest());
}
inline bool ProfileExists(const std::filesystem::path& local) {
    const auto folder = Folder(local);
    return !folder.empty() && TexturePacks::ReadFile(folder / "ui/hud_screen.json") == HideJson() &&
           TexturePacks::ReadFile(folder / "manifest.json") == Manifest();
}
inline bool CommitRequest(const std::filesystem::path& local, bool wanted) {
    const auto folder = Folder(local);
    return !folder.empty() && LitePack::WriteFile(folder / "requested.txt", wanted ? "on" : "off");
}
inline std::vector<TexturePacks::Entry> WithEntry(const std::vector<TexturePacks::Entry>& stack, bool wanted) {
    std::vector<TexturePacks::Entry> result;
    if (wanted) result.push_back({Uuid, std::string("{\"pack_id\":\"") + Uuid + "\",\"version\":[1,0,1]}"});
    for (const auto& entry : stack) if (entry.uuid != Uuid) result.push_back(entry);
    return result;
}
} // namespace JavaChatPack

#pragma once
#include "LitePack.h"
#include "MarksGlyph.h"
#include "client/OriginMarks.h"
#include "TexturePacks.h"

namespace MarksPack {
inline constexpr const char* Uuid = "d585862a-67cb-4c36-bfa4-5db45ddcb38e";
inline std::string_view Payload() {
    return {reinterpret_cast<const char*>(MarksGlyph), sizeof MarksGlyph};
}
inline std::string GlyphPath() {
    constexpr char hex[] = "0123456789ABCDEF";
    const auto page = OriginMarks::GlyphPage;
    return std::string("font/glyph_") + hex[page >> 4] + hex[page & 15] + ".png";
}
inline bool Requested(const std::filesystem::path& local) {
    const auto root = LitePack::GeneratedPackRoot(local);
    return !root.empty() && TexturePacks::ReadFile(root / "OriginX_Marks/requested.txt") == "on";
}
inline std::filesystem::path Folder(const std::filesystem::path& local) {
    const auto root = LitePack::GeneratedPackRoot(local);
    return root.empty() ? std::filesystem::path{} : root / "OriginX_Marks";
}
inline std::string Manifest() {
    return std::string("{\"format_version\":2,\"header\":{\"name\":\"OriginX Marks\",\"description\":\"OriginX session name tags\",\"uuid\":\"") + Uuid +
        "\",\"version\":[1,0,1],\"min_engine_version\":[1,16,0]},\"modules\":[{\"type\":\"resources\",\"uuid\":\"7a829fa4-6081-4bf0-8e8a-ffb174f62d0b\",\"version\":[1,0,1]}]}";
}
// Writes the pack into any folder (the generated root, or a copy in the game's own resource_packs folder).
inline bool WriteFolder(const std::filesystem::path& folder) {
    if (folder.empty()) return false;
    if (TexturePacks::ReadFile(folder / GlyphPath()) == Payload() &&
        TexturePacks::ReadFile(folder / "manifest.json") == Manifest()) return true;
    return LitePack::WriteFile(folder / GlyphPath(), std::string(Payload())) &&
           LitePack::WriteFile(folder / "manifest.json", Manifest());
}
inline bool Write(const std::filesystem::path& local, bool wanted) {
    const auto folder = Folder(local);
    if (folder.empty()) return !wanted;
    // Keep the immutable payload for native reads already in flight. OFF removes only its active-stack entry.
    // This private cache is never registered by normal Minecraft/other clients.
    if (!wanted) return true;
    if (TexturePacks::ReadFile(folder / GlyphPath()) == Payload() &&
        TexturePacks::ReadFile(folder / "manifest.json") == Manifest()) return true;
    return LitePack::WriteFile(folder / GlyphPath(), std::string(Payload())) &&
           LitePack::WriteFile(folder / "manifest.json", Manifest());
}
inline bool ProfileExists(const std::filesystem::path& local) {
    const auto folder = Folder(local);
    return !folder.empty() && TexturePacks::ReadFile(folder / GlyphPath()) == Payload() &&
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
} // namespace MarksPack

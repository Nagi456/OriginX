#pragma once
#include <cstdint>
#include <string_view>
namespace MyClientTexturePolicy {
// The title is a positive screen observation, not just the absence of a local player.
inline bool IsTitleForReload(bool hasPlayer, bool hasLevel, std::string_view screen) {
    return !hasPlayer && !hasLevel && screen.find("start_screen") != std::string_view::npos;
}
struct StartupTitleReload {
    uint64_t titleSince = 0;
    bool Due(uint64_t now, bool onTitle, bool queued) {
        if (!onTitle || queued) { titleSince = 0; return false; }
        if (!titleSince || now < titleSince) { titleSince = now; return false; }
        if (now - titleSince < 4000) return false;
        titleSince = now;
        return true;
    }
};
// Absence of a remote address is not evidence of a local world. Loopback servers are servers too.
inline bool AllowInWorld(uint32_t locality, bool realm, bool inServer) {
    return locality == 1 && !realm && !inServer;
}
inline bool ShowTitleNotice(bool inWorld, bool liveAvailable, std::wstring_view status) {
    // Availability can change between Done and its next HUD frame. Override only known success messages.
    return inWorld && !liveAvailable && (status.empty() || status.rfind(L"Applied", 0) == 0 ||
        status.rfind(L"Switched to:", 0) == 0 || status.rfind(L"Saved", 0) == 0 || status == L"タイトル画面で反映されます");
}
}

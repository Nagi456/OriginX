#pragma once
// OriginX capes on other players (docs/superpowers/specs/2026-11-12-capes-design.md), the decisions the DLL makes
// without the game: which players need their cape put on again, and which skins can carry one.
#include "SkinChannel.h"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace MyClientCapes {
// Indexes whose wanted cape differs from what was applied last (missing = nothing applied). A wanted 0 where nothing
// was applied is no change.
inline std::vector<size_t> NeedsApply(const std::vector<uint32_t>& wanted, const std::vector<uint32_t>& applied)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < wanted.size(); ++i) {
        const uint32_t before = i < applied.size() ? applied[i] : 0;
        if (wanted[i] != before) out.push_back(i);
    }
    return out;
}
// A cape put on by OriginX (FillClosetSkin names them "OriginX_trial_cape_<id>"): never sent to the server.
inline bool IsOriginXCape(std::string_view capeId) { return capeId.rfind("OriginX_", 0) == 0; }
// A classic skin picture the Closet's request can hold (64..MaxSide wide, square or half as tall).
inline bool UsableSkinSize(uint32_t w, uint32_t h)
{
    return w >= 64 && w <= MyClientSkin::MaxSide && w % 64 == 0 && (h == w || h == w / 2);
}
} // namespace MyClientCapes

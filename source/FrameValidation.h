#pragma once

#include "BridgeApi.h"
#include <cmath>
#include <cstddef>
#include <type_traits>

static_assert(sizeof(MyClientFrame) == 116, "Frame ABI changed; bump version");
static_assert(offsetof(MyClientFrame, points) == 52, "Frame layout changed");
static_assert(std::is_trivially_copyable<MyClientFrame>::value, "IPC requires POD");

inline bool IsValidFrame(const MyClientFrame& f)
{
    if (f.structSize != sizeof(f) || f.abiVersion != MYCLIENT_ABI_VERSION ||
        (f.flags & ~(MYCLIENT_HAS_ARROWS | MYCLIENT_HAS_TARGET |
                     MYCLIENT_HAS_PROJECTION | MYCLIENT_HAS_BOUNDS)))
        return false;

    if ((f.flags & (MYCLIENT_HAS_PROJECTION | MYCLIENT_HAS_BOUNDS)) &&
        !(f.flags & MYCLIENT_HAS_TARGET))
        return false;

    if ((f.flags & MYCLIENT_HAS_PROJECTION) && !(f.flags & MYCLIENT_HAS_BOUNDS))
        return false;

    if (f.flags & MYCLIENT_HAS_BOUNDS)
    {
        if (!std::isfinite(f.minX) || !std::isfinite(f.minY) ||
            !std::isfinite(f.minZ) || !std::isfinite(f.maxX) ||
            !std::isfinite(f.maxY) || !std::isfinite(f.maxZ) ||
            f.minX >= f.maxX || f.minY >= f.maxY || f.minZ >= f.maxZ)
            return false;
    }
    if (f.flags & MYCLIENT_HAS_PROJECTION)
    {
        for (const auto& p : f.points)
        {
            // Bound off-screen points as well as rejecting NaN/Infinity.
            if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                p.x < -8.f || p.x > 8.f || p.y < -8.f || p.y > 8.f)
                return false;
        }
    }
    return true;
}

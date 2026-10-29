#include "FrameValidation.h"
#include <cassert>
#include <limits>

int main()
{
    MyClientFrame f{};
    assert(!IsValidFrame(f));
    f.structSize = sizeof(f);
    f.abiVersion = MYCLIENT_ABI_VERSION;
    assert(IsValidFrame(f)); // No inventory/world yet is a legitimate frame.
    f.flags = MYCLIENT_HAS_ARROWS;
    f.arrowCount = 0;
    assert(IsValidFrame(f)); // Known zero is different from missing data.
    ++f.abiVersion;
    assert(!IsValidFrame(f));
    --f.abiVersion;
    --f.structSize;
    assert(!IsValidFrame(f));
    ++f.structSize;
    f.flags |= 16u;
    assert(!IsValidFrame(f));
    f.flags = MYCLIENT_HAS_PROJECTION;
    assert(!IsValidFrame(f)); // Cannot project without a target.
    f.flags |= MYCLIENT_HAS_TARGET;
    assert(!IsValidFrame(f)); // Projection requires captured bounds.
    f.flags |= MYCLIENT_HAS_BOUNDS;
    assert(!IsValidFrame(f)); // Degenerate bounds.
    f.maxX = f.maxY = f.maxZ = 1;
    assert(IsValidFrame(f));
    f.minX = 2;
    assert(!IsValidFrame(f));
    f.minX = std::numeric_limits<float>::quiet_NaN();
    assert(!IsValidFrame(f));
    f.minX = 0;
    f.points[7].y = std::numeric_limits<float>::infinity();
    assert(!IsValidFrame(f));
    f.points[7].y = 9;
    assert(!IsValidFrame(f));
    f.points[7].y = -1;
    assert(IsValidFrame(f)); // Bounded off-screen points are allowed.
}

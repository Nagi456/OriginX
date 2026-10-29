#pragma once

#include "BridgeApi.h"
#include <limits>
#include <string_view>

namespace MyClientGame {
inline MyClientFrame EmptyFrame()
{
    MyClientFrame frame{};
    frame.structSize = sizeof(frame);
    frame.abiVersion = MYCLIENT_ABI_VERSION;
    return frame;
}

class ArrowCounter {
public:
    void Add(std::string_view type, uint32_t count)
    {
        if (!count) return; // Empty slots have no item identifier.
        if (type.empty() || count > 255)
        {
            valid_ = false;
            return;
        }
        // Bedrock tipped arrows generally use minecraft:arrow + auxiliary data.
        if (type == "minecraft:arrow" || type == "minecraft:tipped_arrow")
        {
            if (count > std::numeric_limits<uint32_t>::max() - count_)
                valid_ = false;
            else
                count_ += count;
        }
    }

    void Write(MyClientFrame& frame) const
    {
        frame.flags &= ~MYCLIENT_HAS_ARROWS;
        frame.arrowCount = 0;
        if (valid_)
        {
            frame.flags |= MYCLIENT_HAS_ARROWS;
            frame.arrowCount = count_;
        }
    }

private:
    uint32_t count_ = 0;
    bool valid_ = true;
};

inline void SetTarget(MyClientFrame& frame, int32_t x, int32_t y, int32_t z)
{
    frame.flags |= MYCLIENT_HAS_TARGET;
    frame.blockX = x;
    frame.blockY = y;
    frame.blockZ = z;
    // The level's hit result provides exact block coordinates. Do not invent
    // a cube shape or screen projection for blocks such as stairs and fences.
}
} // namespace MyClientGame

#include "client/GameData.h"
#include "FrameValidation.h"
#include <cassert>

int main()
{
    auto frame = MyClientGame::EmptyFrame();
    assert(IsValidFrame(frame) && frame.flags == 0);
    MyClientGame::ArrowCounter counter;
    counter.Add("minecraft:arrow", 64); // Hotbar.
    counter.Add("minecraft:stone", 64);
    counter.Add("", 0); // Empty inventory slot.
    counter.Add("minecraft:arrow", 21); // Another inventory stack.
    counter.Add("minecraft:tipped_arrow", 3); // Offhand.
    counter.Write(frame);
    assert(frame.arrowCount == 88 && (frame.flags & MYCLIENT_HAS_ARROWS));

    MyClientGame::SetTarget(frame, -30000000, -64, 29999999);
    assert(IsValidFrame(frame)); // Exact integer coordinates need no float AABB.
    assert((frame.flags & MYCLIENT_HAS_TARGET) && !(frame.flags & MYCLIENT_HAS_BOUNDS));
    assert(frame.blockX == -30000000 && frame.blockZ == 29999999);

    // A non-empty item that could not be identified must not yield a partial total.
    counter.Add("", 1);
    counter.Write(frame);
    assert(!(frame.flags & MYCLIENT_HAS_ARROWS) && frame.arrowCount == 0);
    assert(frame.flags & MYCLIENT_HAS_TARGET);

    MyClientGame::ArrowCounter none;
    none.Add("minecraft:bow", 1);
    none.Write(frame);
    assert((frame.flags & MYCLIENT_HAS_ARROWS) && frame.arrowCount == 0);

    MyClientGame::ArrowCounter invalid;
    invalid.Add("minecraft:arrow", 256);
    invalid.Write(frame);
    assert(!(frame.flags & MYCLIENT_HAS_ARROWS));

    frame = MyClientGame::EmptyFrame(); // Exit/no player: clear both channels.
    assert(frame.flags == 0 && frame.arrowCount == 0 && frame.blockX == 0);
    assert(IsValidFrame(frame));
}

// HUD <-> DLL video channel (editor preview, export commands and progress): layout and checks (ReplayVideoChannel.h).
#include "../ReplayVideoChannel.h"
#include <cassert>
#include <cstdio>
using namespace MyClientReplayVideo;

int main()
{
    Shared s{};
    Reset(s, 7);
    assert(Valid(s, 7) && !Valid(s, 8) && s.requestFrame == NoFrame && s.doneFrame == NoFrame && s.state == Idle);
    { Shared b = s; b.version = Version + 1; assert(!Valid(b, 7)); }
    { Shared b = s; b.size = 4; assert(!Valid(b, 7)); }

    // The preview box is a part of the game's picture (0..1 each way); anything else means no preview.
    assert(PreviewWanted(0.0f, 0.1f, 0.6f, 0.6f) && !PreviewWanted(0, 0, 0, 0) && !PreviewWanted(0.5f, 0, 0.6f, 0.5f) &&
           !PreviewWanted(-0.1f, 0, 0.5f, 0.5f) && !PreviewWanted(0, 0, 0.01f, 0.5f));

    // Paths go in as UTF-16, cut to fit and always terminated.
    SetPath(s, L"C:\\Videos\\OriginX\\rec_default.mp4");
    assert(std::wstring(s.path) == L"C:\\Videos\\OriginX\\rec_default.mp4");
    SetPath(s, std::wstring(600, L'a'));
    assert(s.path[PathChars - 1] == 0 && std::wstring(s.path).size() == PathChars - 1);
    SetError(s, std::string(500, 'e'));
    assert(s.error[sizeof s.error - 1] == 0);

    // Fresh: the other side wrote within the time (milliseconds, GetTickCount64).
    assert(Fresh(1000, 1500, 1000) && !Fresh(1000, 2501, 1000) && !Fresh(0, 5, 1000));
    std::puts("replay video channel ok");
    return 0;
}

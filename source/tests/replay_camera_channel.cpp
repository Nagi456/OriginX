// HUD -> DLL replay camera channel: layout checks and the replay clock (ReplayCameraChannel.h).
#include "../ReplayCameraChannel.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace MyClientReplayCameraChannel;

int main()
{
    Shared s{};
    s.magic = Magic; s.version = Version; s.size = sizeof(Shared); s.pid = 42; s.count = 2;
    assert(Valid(s, 42));
    assert(!Valid(s, 43));
    { Shared b = s; b.magic = 1; assert(!Valid(b, 42)); }
    { Shared b = s; b.version = Version + 1; assert(!Valid(b, 42)); }
    { Shared b = s; b.size = 8; assert(!Valid(b, 42)); }
    { Shared b = s; b.count = MaxPoints + 1; assert(!Valid(b, 42)); }

    // `at` and the clock are microseconds of a fine, system-wide clock (NowMicros): a 15.6 ms tick made the camera stutter.
    assert(Fresh(1000000, 1000000) && Fresh(1000000, 1500000) && !Fresh(1000000, 1500001) && !Fresh(0, 10) && !Fresh(2000000, 1000000));
    const uint64_t a = NowMicros(), b = NowMicros();
    assert(a > 0 && b >= a);

    s.at = 1000000; s.timeMs = 5000; s.speed = 2.f; s.paused = 1;
    assert(TimeAt(s, 1100000) == 5000.0);
    s.paused = 0;
    assert(std::fabs(TimeAt(s, 1100000) - 5200.0) < 1e-9);
    assert(std::fabs(TimeAt(s, 1000500) - 5001.0) < 1e-9);   // sub-millisecond steps move the camera too
    assert(TimeAt(s, 900000) == 5000.0);   // a clock that went back never runs the path backwards

    // The path time never steps back a little while playing (samples taken at slightly different moments);
    // a real jump back (a seek) is followed at once.
    {
        Steady steady;
        assert(steady.Next(1000.0, false) == 1000.0);
        assert(steady.Next(1016.0, false) == 1016.0);
        assert(steady.Next(1010.0, false) == 1016.0);   // 6 ms back: held
        assert(steady.Next(1020.0, false) == 1020.0);
        assert(steady.Next(400.0, false) == 400.0);     // a seek back
        assert(steady.Next(390.0, true) == 390.0);      // paused: as given
    }

    // The points carry exactly what ReplayCamera.h keeps.
    std::vector<MyClientReplayCamera::Keyframe> path = {{1000, {1, 2, 3, 4, 5}, MyClientReplayCamera::Ease::Cut},
                                                        {3000, {6, 7, 8, 9, 10}, MyClientReplayCamera::Ease::In}};
    Shared w{};
    Fill(w, path);
    assert(w.count == 2 && w.points[1].t == 3000 && w.points[1].yaw == 10.f && w.points[0].ease == static_cast<uint8_t>(MyClientReplayCamera::Ease::Cut));
    std::vector<MyClientReplayCamera::Keyframe> back;
    Read(w, back);
    assert(back.size() == 2 && back[0].ease == MyClientReplayCamera::Ease::Cut && back[1].pose.x == 6.f && back[1].t == 3000);
    std::puts("replay camera channel ok");
    return 0;
}

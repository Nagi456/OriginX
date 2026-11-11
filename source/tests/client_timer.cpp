#include "ClientTimer.h"
#include <cassert>
#include <iostream>
using namespace ClientTimer;
int main()
{
    // Clock: start, pause keeps the time, start again adds to it, reset clears.
    Clock c;
    assert(c.Fresh() && c.Elapsed(500) == 0);
    c.Start(1000);
    assert(c.Elapsed(1500) == 500);
    c.Pause(2000);
    assert(!c.running && c.Elapsed(9000) == 1000);
    c.Toggle(10000);
    assert(c.running && c.Elapsed(10250) == 1250);
    c.Reset();
    assert(c.Fresh());
    // Countdown.
    Clock d;
    d.Start(0);
    assert(Remaining(d, 60000, 1000) == 59000 && Remaining(d, 60000, 61000) == 0);
    // Formats.
    assert(FormatStopwatch(0) == L"0:00.0" && FormatStopwatch(83456) == L"1:23.4" && FormatStopwatch(3723400) == L"1:02:03.4");
    assert(FormatCountdown(300000) == L"5:00" && FormatCountdown(299001) == L"5:00" && FormatCountdown(1) == L"0:01");
    assert(FormatCountdown(0) == L"0:00" && FormatCountdown(3600000) == L"1:00:00");
    // Click window: at most N presses in any second; held-back presses do not count.
    ClickWindow w;
    for (int i = 0; i < 3; i++) assert(w.Allow(i * 10, 3));
    assert(!w.Allow(50, 3) && !w.Allow(999, 3));
    assert(w.Allow(1000, 3));   // the first one (at 0) left the window
    ClickWindow free;
    for (int i = 0; i < 100; i++) assert(free.Allow(i, 0));
    // Alarm WAV: a RIFF header and the data size it promises.
    const auto wav = AlarmWav();
    assert(wav.size() > 44 && wav[0] == 'R' && wav[8] == 'W');
    const uint32_t data = wav[40] | wav[41] << 8 | wav[42] << 16 | (uint32_t)wav[43] << 24;
    assert(data + 44 == wav.size());
    std::cout << "client_timer ok\n";
}

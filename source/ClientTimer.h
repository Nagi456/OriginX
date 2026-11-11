#pragma once
// Timer / Stopwatch module (one clock, two modes) and the CPS Limiter's click window.
// Plain logic with times in milliseconds passed in, so tests/client_timer.cpp can drive it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace ClientTimer
{
// A clock that can be paused: the time it has run is `banked` plus the current run.
struct Clock
{
    bool running = false;
    uint64_t startedAt = 0;
    uint64_t banked = 0;

    uint64_t Elapsed(uint64_t now) const { return banked + (running && now > startedAt ? now - startedAt : 0); }
    void Start(uint64_t now) { if (!running) { running = true; startedAt = now; } }
    void Pause(uint64_t now) { if (running) { banked = Elapsed(now); running = false; } }
    void Toggle(uint64_t now) { if (running) Pause(now); else Start(now); }
    void Reset() { *this = Clock{}; }
    bool Fresh() const { return !running && banked == 0; }
};

// Countdown: what is left of `lengthMs` (0 once it has run out).
inline uint64_t Remaining(const Clock& c, uint64_t lengthMs, uint64_t now)
{
    const uint64_t e = c.Elapsed(now);
    return e >= lengthMs ? 0 : lengthMs - e;
}

// Stopwatch: "m:ss.t", or "h:mm:ss.t" from an hour on (tenths cut, not rounded).
inline std::wstring FormatStopwatch(uint64_t ms)
{
    const uint64_t tenths = ms / 100 % 10, s = ms / 1000 % 60, m = ms / 60000 % 60, h = ms / 3600000;
    wchar_t b[48];
    if (h) swprintf(b, 48, L"%llu:%02llu:%02llu.%llu", (unsigned long long)h, (unsigned long long)m, (unsigned long long)s, (unsigned long long)tenths);
    else swprintf(b, 48, L"%llu:%02llu.%llu", (unsigned long long)m, (unsigned long long)s, (unsigned long long)tenths);
    return b;
}

// Countdown: "m:ss" (or "h:mm:ss"), whole seconds rounded up, so 0:00 shows only once it has run out.
inline std::wstring FormatCountdown(uint64_t ms)
{
    const uint64_t total = (ms + 999) / 1000, s = total % 60, m = total / 60 % 60, h = total / 3600;
    wchar_t b[48];
    if (h) swprintf(b, 48, L"%llu:%02llu:%02llu", (unsigned long long)h, (unsigned long long)m, (unsigned long long)s);
    else swprintf(b, 48, L"%llu:%02llu", (unsigned long long)m, (unsigned long long)s);
    return b;
}

// CPS Limiter: the presses let through in the last second. A press is let through only while fewer than
// `maxCps` were (maxCps <= 0: no limit). Presses that are held back do not count.
struct ClickWindow
{
    std::deque<uint64_t> passed;

    bool Allow(uint64_t now, int maxCps)
    {
        while (!passed.empty() && passed.front() + 1000 <= now)
            passed.pop_front();
        if (maxCps > 0 && (int)passed.size() >= maxCps)
            return false;
        passed.push_back(now);
        return true;
    }
};

// The alarm when a countdown runs out: three short beeps (880 Hz, 22050 Hz mono 16-bit WAV, ~0.75 s).
inline std::vector<unsigned char> AlarmWav()
{
    const int rate = 22050;
    std::vector<int16_t> samples;
    for (int beep = 0; beep < 3; beep++)
    {
        const int on = rate * 15 / 100, off = rate / 10;
        for (int i = 0; i < on; i++)
        {
            const double fade = std::min(1.0, std::min(i, on - i) / (rate * 0.01));   // no clicks at the ends
            samples.push_back((int16_t)(std::sin(2 * 3.14159265358979 * 880.0 * i / rate) * 9000 * fade));
        }
        samples.insert(samples.end(), off, 0);
    }
    std::vector<unsigned char> wav;
    const auto u32 = [&wav](uint32_t v) { for (int i = 0; i < 4; i++) wav.push_back((unsigned char)(v >> (8 * i))); };
    const auto u16 = [&wav](uint16_t v) { wav.push_back((unsigned char)v); wav.push_back((unsigned char)(v >> 8)); };
    const uint32_t data = (uint32_t)(samples.size() * 2);
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'}); u32(36 + data);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); u32(16);
    u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'}); u32(data);
    for (int16_t s : samples) u16((uint16_t)s);
    return wav;
}
}

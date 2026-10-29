#pragma once
// Cost of MyClient's own code inside the game (the game's original functions are not included).
// Each site counts calls and the time spent in our part; the frame hook logs a summary every 10 s, so a
// slow game can be traced to one feature from latest.log. QueryPerformanceCounter costs ~20 ns a call.
// The sites after Chat are the other way round: whole game functions (original included), so the cost of
// the game's own HUD can be compared with the whole frame (gameFrame) before deciding what to make lighter.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <windows.h>

namespace MyClientProfile {
enum Site : int { Frame, Fov, Light, Outline, TessColor, ActorRender, NameTag, Nick, Hit, Replay, Tick, Screen, Chat,
                  GameFrame, UiUpdate, UiRender, HudTick, ItemUpdate, ItemRender, Count };
inline constexpr const char* kNames[Count] = {"frame", "fov", "light", "outline", "tessColor", "actorRender",
                                              "nameTag", "nick", "hit", "replay", "tick", "screen", "chat",
                                              "gameFrame", "uiUpdate", "uiRender", "hudTick", "itemUpdate", "itemRender"};
struct Counter { std::atomic<uint64_t> calls{0}, ticks{0}; };
inline Counter counters[Count];

inline uint64_t Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return static_cast<uint64_t>(t.QuadPart); }

struct Scope {
    Site site; uint64_t start;
    explicit Scope(Site s) : site(s), start(Now()) {}
    ~Scope() {
        counters[site].ticks.fetch_add(Now() - start, std::memory_order_relaxed);
        counters[site].calls.fetch_add(1, std::memory_order_relaxed);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// A game function's whole call. Only the outermost call on a thread counts, so a function that runs
// itself again (a scene drawing the one behind it) is not counted twice.
struct GameScope {
    Site site; uint64_t start = 0;
    static int& Depth(Site s) { thread_local int depth[Count] = {}; return depth[s]; }
    explicit GameScope(Site s) : site(s) { if (Depth(s)++ == 0) start = Now(); }
    ~GameScope() {
        if (--Depth(site) != 0) return;
        counters[site].ticks.fetch_add(Now() - start, std::memory_order_relaxed);
        counters[site].calls.fetch_add(1, std::memory_order_relaxed);
    }
    GameScope(const GameScope&) = delete;
    GameScope& operator=(const GameScope&) = delete;
};

// Called once per rendered frame; returns a summary line every `periodMs` (empty otherwise) and resets.
inline std::string Summary(uint64_t nowMs, uint64_t periodMs = 10000) {
    static uint64_t since = 0, frames = 0;
    frames++;
    if (!since) since = nowMs;
    if (nowMs - since < periodMs) return {};
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    const double seconds = (nowMs - since) / 1000.0;
    std::string out = "Perf per second: fps=" + std::to_string(static_cast<int>(frames / seconds + 0.5));
    for (int i = 0; i < Count; i++) {
        const uint64_t calls = counters[i].calls.exchange(0), ticks = counters[i].ticks.exchange(0);
        if (!calls) continue;
        const double ms = ticks * 1000.0 / static_cast<double>(f.QuadPart) / seconds;
        char part[96];
        std::snprintf(part, sizeof part, " %s=%.0f calls/%.2f ms", kNames[i], calls / seconds, ms);
        out += part;
    }
    since = nowMs; frames = 0;
    return out;
}
}  // namespace MyClientProfile

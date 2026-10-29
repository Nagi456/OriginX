#pragma once
// Frame Generation timing (DLL render thread). Kept free of Windows / D3D so tests/framegen.cpp can run it.
//
// Each game frame N reaches Present once. With Frame Generation on, that one call shows (multiplier - 1)
// made-up frames between N-1 and N and then N itself. For the extra frames to look smooth they must be
// spread over the game's frame interval, so the presenter thread waits between them (the game's own thread
// does not wait, or its frames would get that much longer): the wait is the average interval
// divided by the multiplier. With V-Sync the presents are spaced by the display already, so nothing waits.
//
// The game's next Present waits until the presenter is done, so the made-up frames must fit in the game's own
// frame time: the interval is measured without the time the game was held, and a made-up frame that would not
// finish before the game's next frame is skipped (Fits). Otherwise x3 held the game for its second made-up frame,
// the game ran slower and the presented frame rate hardly went up over x2.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MyClientFrameGen {
constexpr double MaxGapMs = 50;        // a longer gap (loading, a hitch): interpolating would show a wrong picture
constexpr double MaxWaitMs = 1000.0 / 30;

class Pacer {
    double last_ = 0;    // ms, entry of the previous game frame's Present
    double avg_ = 0;     // ms, average interval between game frames
    double gap_ = 0;     // ms, the latest interval
    double held_ = 0;    // ms, how long the previous game frame's Present waited for the presenter
public:
    void Reset() { last_ = avg_ = gap_ = held_ = 0; }
    // Entry of a game frame's Present (milliseconds from any steady clock).
    void Frame(double nowMs) {
        if (last_ <= 0 || nowMs <= last_) { last_ = nowMs; gap_ = 0; held_ = 0; return; }
        gap_ = nowMs - last_;
        last_ = nowMs;
        const double own = std::max(gap_ - held_, gap_ * 0.25);   // the game's own frame time
        held_ = 0;
        if (gap_ > MaxGapMs) return;   // a hitch does not drag the average
        avg_ = avg_ <= 0 ? own : avg_ + (own - avg_) * 0.1;
    }
    // How long this game frame's Present waited for the presenter (left out of the next interval).
    void Held(double ms) { held_ = ms > 0 ? ms : 0; }
    double AverageMs() const { return avg_; }
    // Whether this game frame may get made-up frames before it (a previous frame exists and came in time).
    bool CanGenerate() const { return avg_ > 0 && gap_ > 0 && gap_ <= MaxGapMs; }
    // How long to wait after each made-up frame, before the next present.
    double WaitMs(uint32_t multiplier, bool pacing, uint32_t syncInterval) const {
        if (!pacing || syncInterval > 0 || multiplier < 2 || avg_ <= 0) return 0;
        return std::min(avg_ / multiplier, MaxWaitMs / multiplier);
    }
};

// How long one present of the presenter thread takes (made-up: make + HUD + present; game frame: copy + HUD +
// present), averaged. Presenter thread only.
// A made-up frame is only timed when one is made, so one slow frame (the first after switching on, a hitch) left
// the estimate high, every later made-up frame was skipped and it never came down (2026-10-28 log: made=9.33 ms
// for minutes, "2973 made-up skipped" of 2973). After StaleMs without one, the next is made anyway and its time
// replaces the old estimate.
constexpr double StaleMs = 1000;
struct Cost {
    double made = 0, real = 0;
    double madeAt = 0;   // ms, when `made` was last measured
    static void Note(double& avg, double ms) { if (ms >= 0 && ms < MaxGapMs) avg = avg <= 0 ? ms : avg + (ms - avg) * 0.2; }
    bool Stale(double nowMs) const { return made > 0 && nowMs - madeAt > StaleMs; }
    void Made(double nowMs, double ms) {
        if (Stale(nowMs)) made = 0;   // a new measurement after a long time without one
        Note(made, ms);
        madeAt = nowMs;
    }
};
// Whether a made-up frame started at `nowMs` still leaves room for the game frame before the game's next
// frame is expected (`startMs` + `intervalMs`, the job starts when the game's Present hands it over).
inline bool Fits(double nowMs, double startMs, double intervalMs, const Cost& cost) {
    if (intervalMs <= 0 || cost.Stale(nowMs)) return true;
    return nowMs + cost.made + cost.real <= startMs + intervalMs * 0.9;   // a little slack for the game's jitter
}

// How many made-up frames each game frame gets (presenter thread). Fits alone decided frame by frame: at x3 with
// made + real just under the game's frame time, some game frames got two made-up frames and the next ones one
// (2026-10-28 log: x3, "1787 made-up skipped" of 4376), so the picture came at uneven steps. The count is planned
// up front from the measured cost and spaced evenly over the frame; it goes down after a few frames that do not
// fit and up again only after a run of frames with room, so it does not flip every frame.
class Extras {
    uint32_t extra_ = 0, multiplier_ = 0;
    int down_ = 0, up_ = 0;
public:
    static constexpr int DownAfter = 3, UpAfter = 60;
    uint32_t Plan(uint32_t multiplier, double intervalMs, const Cost& cost, double nowMs) {
        const uint32_t most = multiplier >= 2 ? multiplier - 1 : 0;
        if (multiplier != multiplier_) { multiplier_ = multiplier; extra_ = most; down_ = up_ = 0; }
        uint32_t fit = most;
        if (intervalMs > 0 && cost.made > 0 && !cost.Stale(nowMs)) {
            fit = 0;
            while (fit < most && (fit + 1) * cost.made + cost.real <= intervalMs * 0.9) ++fit;
        }
        if (fit < extra_) {
            up_ = 0;
            if (++down_ >= DownAfter) { extra_ = fit; down_ = 0; }
        } else if (fit > extra_) {
            down_ = 0;
            if (++up_ >= UpAfter) { ++extra_; up_ = 0; }
        } else {
            down_ = up_ = 0;
        }
        return extra_;
    }
};

// Which made-up frame count gives the most presented frames per second (presenter thread). The made-up frames
// share the GPU with the game: when the cost check let x3 make two per game frame, the game itself slowed down
// and x3 showed fewer frames than x2 (2026-10-28: x2 up to ~540, x3 at most ~450, both dipping far down). So
// the count is measured, not assumed: every presented frame is counted over 1 s windows, and now and then one
// window tries one more or one fewer made-up frame (0 included, so it is never worse than no Frame
// Generation); the try is kept only when it showed clearly more frames. A try that lost waits longer next time.
class Tuner {
    double windowStart_ = 0, lastChoose_ = 0, nextProbe_ = 0;
    uint32_t presents_ = 0, multiplier_ = 0, choice_ = 0, base_ = 0;
    double baseRate_ = 0;
    bool probing_ = false;
    int direction_ = 1, losses_ = 0;
public:
    static constexpr double WindowMs = 1000, ProbeEveryMs = 6000, Better = 1.03;
    void Present() { ++presents_; }
    uint32_t Choice() const { return choice_; }
    // `cap`: the most made-up frames the measured cost allows (Extras).
    uint32_t Choose(uint32_t multiplier, uint32_t cap, double nowMs) {
        const uint32_t most = std::min(multiplier >= 2 ? multiplier - 1 : 0u, cap);
        if (multiplier != multiplier_) { multiplier_ = multiplier; choice_ = multiplier >= 2 ? multiplier - 1 : 0; losses_ = 0; lastChoose_ = 0; }
        if (lastChoose_ <= 0 || nowMs - lastChoose_ > 2000) {   // first job, or after a pause / menu
            windowStart_ = nowMs; presents_ = 0; probing_ = false; nextProbe_ = nowMs + ProbeEveryMs;
        }
        lastChoose_ = nowMs;
        if (nowMs - windowStart_ >= WindowMs) {
            const double rate = presents_ * 1000.0 / (nowMs - windowStart_);
            windowStart_ = nowMs; presents_ = 0;
            if (probing_) {
                probing_ = false;
                if (rate > baseRate_ * Better) { baseRate_ = rate; losses_ = 0; }
                else { choice_ = base_; losses_ = std::min(losses_ + 1, 3); }
                direction_ = -direction_;
                nextProbe_ = nowMs + ProbeEveryMs * (1 << losses_);
            } else {
                baseRate_ = rate;
                const uint32_t now = std::min(choice_, most);
                if (nowMs >= nextProbe_) {
                    uint32_t next = now;
                    if (direction_ > 0 && now < most) next = now + 1;
                    else if (direction_ < 0 && now > 0) next = now - 1;
                    else if (now < most) next = now + 1;
                    else if (now > 0) next = now - 1;
                    if (next != now) { base_ = now; choice_ = next; probing_ = true; }
                    else nextProbe_ = nowMs + ProbeEveryMs;
                }
            }
        }
        return std::min(choice_, most);
    }
};

// Frames per second over about one second, for any event (here: every present, made-up frames included).
class Counter {
    double start_ = 0;
    uint32_t count_ = 0;
    float value_ = 0;
public:
    void Event(double nowMs) {
        if (start_ <= 0 || nowMs < start_ || nowMs - start_ > 2000) { start_ = nowMs; count_ = 0; return; }
        ++count_;
        if (nowMs - start_ >= 1000) { value_ = static_cast<float>(count_ * 1000.0 / (nowMs - start_)); start_ = nowMs; count_ = 0; }
    }
    float Value(double nowMs) const { return start_ > 0 && nowMs - start_ <= 2000 ? value_ : 0.f; }
};

// Position in time (0..1, from the previous game frame to this one) of made-up frame k of a multiplier.
inline float FramePhase(uint32_t k, uint32_t multiplier) {
    return multiplier >= 2 && k >= 1 && k < multiplier ? static_cast<float>(k) / static_cast<float>(multiplier) : 1.f;
}
}  // namespace MyClientFrameGen

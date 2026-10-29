#include "FrameGenPacing.h"
#include "FeatureData.h"
#include <cassert>
#include <cstddef>
#include <cmath>
#include <iostream>
using namespace MyClientFrameGen;
using namespace MyClientFeatures;
int main() {
    // Bits 27..31 of command.autoRq, next to the other autoRq fields.
    assert(EncodeFrameGen(1, FrameGenMotion, true) == 0 && FrameGenMultiplierOf(0) == 1 && FrameGenPacingOf(0));
    assert(FrameGenMultiplierOf(EncodeFrameGen(2, FrameGenMotion, true)) == 2);
    assert(FrameGenMultiplierOf(EncodeFrameGen(3, FrameGenBlend, true)) == 3);
    assert(FrameGenMultiplierOf(EncodeFrameGen(9, FrameGenBlend, true)) == 3);
    // x4..x10 come from Shared.frameGenMultiplier; 0 (older HUD) keeps the bits' x2 / x3, and off stays off.
    assert(FrameGenMultiplierOf(EncodeFrameGen(10, FrameGenMotion, true), 10) == 10);
    assert(FrameGenMultiplierOf(EncodeFrameGen(3, FrameGenMotion, true), 0) == 3);
    assert(FrameGenMultiplierOf(EncodeFrameGen(5, FrameGenMotion, true), 11) == 3);
    assert(FrameGenMultiplierOf(0, 7) == 1);
    assert(FrameGenMethodOf(EncodeFrameGen(2, FrameGenCopy, true)) == FrameGenCopy);
    assert(FrameGenMethodOf(EncodeFrameGen(2, 7, true)) == FrameGenMotion);
    assert(!FrameGenPacingOf(EncodeFrameGen(2, FrameGenMotion, false)) && FrameGenPacingOf(EncodeFrameGen(2, FrameGenMotion, true)));
    const uint32_t all = EncodeFrameGen(3, FrameGenCopy, false);
    assert((all & 0x07FFFFFFu) == 0);   // never touches Auto RQ, Render Distance, Left Hand, HUD menu, Shulker
    const uint32_t field = EncodeAutoRqField(6, 12, 3) | LeftHandBit | HudMenuBit | ShulkerHoverBit | all;
    assert(AutoRqModeOf(field) == 6 && RenderChunksOf(field) == 12 && RenderCounterOf(field) == 3);
    assert(LeftHandOf(field) && HudMenuOf(field) && ShulkerHoverOf(field) && FrameGenMultiplierOf(field) == 3);

    // Pacing: average interval of the game's frames, divided by the multiplier.
    Pacer p;
    assert(!p.CanGenerate() && p.WaitMs(2, true, 0) == 0);
    p.Frame(1000);
    assert(!p.CanGenerate());   // no previous frame yet
    for (int i = 1; i <= 50; i++) p.Frame(1000 + i * 5.0);   // 200 FPS
    assert(p.CanGenerate() && std::abs(p.AverageMs() - 5) < 1e-6);
    assert(std::abs(p.WaitMs(2, true, 0) - 2.5) < 1e-6 && std::abs(p.WaitMs(3, true, 0) - 5.0 / 3) < 1e-6);
    assert(p.WaitMs(2, false, 0) == 0 && p.WaitMs(2, true, 1) == 0 && p.WaitMs(1, true, 0) == 0);
    p.Frame(1250 + 200);   // a hitch: no made-up frame, and the average stays
    assert(!p.CanGenerate() && std::abs(p.AverageMs() - 5) < 1e-6);
    p.Frame(1455);
    assert(p.CanGenerate());
    Pacer slow;
    for (int i = 0; i <= 10; i++) slow.Frame(i * 40.0 + 1);   // 25 FPS: the wait is capped
    assert(std::abs(slow.WaitMs(2, true, 0) - MaxWaitMs / 2) < 1e-6);
    p.Reset(); assert(!p.CanGenerate() && p.AverageMs() == 0);

    // The time the game was held for the presenter is not part of its own frame time.
    Pacer held;
    for (int i = 0; i <= 50; i++) { held.Frame(i * 6.0 + 1); held.Held(2); }   // 6 ms apart, 2 ms of it held
    assert(std::abs(held.AverageMs() - 4) < 1e-6);

    // Made-up frames only while the game frame still fits before the game's next frame.
    Cost cost;
    Cost::Note(cost.made, 1.5); Cost::Note(cost.real, 0.5);
    assert(Fits(100, 100, 4, cost) && Fits(101.5, 100, 4, cost) && !Fits(102, 100, 4, cost));
    assert(Fits(500, 100, 0, cost));   // no interval yet: nothing to fit
    Cost::Note(cost.made, 1000);       // a hitch does not count
    assert(std::abs(cost.made - 1.5) < 1e-6);
    // One slow made-up frame does not stop them for good: after a second without one the next is made and
    // its time replaces the old estimate.
    Cost slowCost;
    slowCost.Made(1000, 9.0); Cost::Note(slowCost.real, 0.5);
    assert(!Fits(1000.5, 1000, 4, slowCost) && !Fits(1999, 1000, 4, slowCost));
    assert(Fits(2001, 2001, 4, slowCost));
    slowCost.Made(2002, 1.0);
    assert(std::abs(slowCost.made - 1.0) < 1e-6 && Fits(2003, 2003, 4, slowCost));

    // Planned made-up frames: x3 at a 3.9 ms game frame, 1.3 ms to make one and 1.0 ms for the game frame.
    Extras plan;
    Cost tight;
    tight.Made(5000, 1.3); Cost::Note(tight.real, 1.0);
    assert(plan.Plan(3, 3.9, tight, 5000) == 2);                 // starts at the full multiplier
    assert(plan.Plan(3, 3.9, tight, 5001) == 2);
    assert(plan.Plan(3, 3.9, tight, 5002) == 1);                 // two do not fit: one, after a few frames
    for (int i = 0; i < 100; i++) assert(plan.Plan(3, 3.9, tight, 5004 + i * 0.1) == 1);   // and it stays there
    Cost roomy;
    roomy.Made(6000, 0.5); Cost::Note(roomy.real, 0.5);
    uint32_t got = 1;
    for (int i = 0; i < Extras::UpAfter; i++) got = plan.Plan(3, 3.9, roomy, 6000 + i * 0.1);
    assert(got == 2);                                            // back up after a run of frames with room
    assert(plan.Plan(2, 3.9, roomy, 6010) == 1);                 // a new multiplier starts at its full count

    // Tuner: keeps the made-up frame count that shows the most frames. Simulated GPU: one made-up frame per game
    // frame gives 540 presents/s, two give 450 (the game slows down), none gives 270.
    {
        Tuner t;
        auto rateFor = [](uint32_t extra) { return extra == 0 ? 270.0 : extra == 1 ? 540.0 : 450.0; };
        double now = 1000;
        uint32_t pick = t.Choose(3, 2, now);
        assert(pick == 2);   // starts at the full multiplier
        double carry = 0;
        for (int step = 0; step < 60000; step++) {   // 60 s in 1 ms steps
            now += 1;
            carry += rateFor(pick) / 1000.0;
            while (carry >= 1) { t.Present(); carry -= 1; }
            pick = t.Choose(3, 2, now);
        }
        assert(t.Choice() == 1);   // x3 settles where it shows the most frames
        // x2 where the made-up frame costs the game more than it adds: settles on none.
        Tuner two;
        auto rate2 = [](uint32_t extra) { return extra == 0 ? 300.0 : 200.0; };
        now = 1000; carry = 0;
        pick = two.Choose(2, 1, now);
        for (int step = 0; step < 60000; step++) {
            now += 1;
            carry += rate2(pick) / 1000.0;
            while (carry >= 1) { two.Present(); carry -= 1; }
            pick = two.Choose(2, 1, now);
        }
        assert(two.Choice() == 0);
        assert(two.Choose(2, 1, now + 5000) <= 1);   // after a pause it starts measuring again
    }

    // Present counter: about one second per reading.
    Counter c;
    for (int i = 0; i <= 400; i++) c.Event(100 + i * 2.5);
    assert(std::abs(c.Value(1100) - 400) < 1);
    assert(c.Value(5000) == 0);

    assert(FramePhase(1, 2) == 0.5f && std::abs(FramePhase(2, 3) - 2.f / 3) < 1e-6 && FramePhase(0, 2) == 1.f && FramePhase(1, 1) == 1.f);
    static_assert(offsetof(Shared, presentedFps) == 768 && offsetof(Shared, frameGen) == 772);
    std::cout << "framegen tests passed\n";
}

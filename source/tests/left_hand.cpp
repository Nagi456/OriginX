#include "../LeftHand.h"
#include "../FeatureData.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace MyClientLeftHand;
namespace {
bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
void Apply(const float m[16], const float p[3], float out[3])
{
    for (int r = 0; r < 3; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}
float Det3(const float m[16])
{
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
}
}
int main()
{
    // An item to the right of the screen (view = identity): it moves to the left, same height and depth.
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float c = std::cos(0.5f), s = std::sin(0.5f);
    float world[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0.56f, -0.52f, -0.72f, 1};   // turned about Y, then placed
    const float detBefore = Det3(world);
    assert(MirrorInView(identity, world));
    assert(Near(world[12], -0.56f) && Near(world[13], -0.52f) && Near(world[14], -0.72f));
    assert(Near(Det3(world), detBefore));   // no reflection: triangles keep their winding
    // The whole pose is mirrored, the model itself is not: the item's -X side now sits where the mirror
    // image of its +X side would be (so a sprite still reads the right way round).
    const float tip[3] = {-1, 0, 0};
    float p[3];
    Apply(world, tip, p);
    assert(Near(p[0], -0.56f - c) && Near(p[1], -0.52f) && Near(p[2], -0.72f - s));
    // Mirroring twice gives the original back.
    assert(MirrorInView(identity, world));
    assert(Near(world[0], c) && Near(world[2], -s) && Near(world[12], 0.56f));
    // With a camera view matrix the mirror is about the screen's middle, not the world's X axis.
    const float view[16] = {c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 3, -2, 5, 1};
    float inverse[16];
    assert(Invert(view, inverse));
    float placed[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.5f, -0.4f, -1.f, 1};   // where it should be on screen
    float worldItem[16];
    Multiply(inverse, placed, worldItem);
    assert(MirrorInView(view, worldItem));
    float onScreen[16];
    Multiply(view, worldItem, onScreen);
    assert(Near(onScreen[12], -0.5f) && Near(onScreen[13], -0.4f) && Near(onScreen[14], -1.f));
    // A singular view leaves the matrix alone.
    const float zero[16] = {};
    float keep[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1};
    assert(!MirrorInView(zero, keep) && keep[12] == 2.f);
    // The HUD's bit rides above the Auto RQ / render distance bits and does not change them.
    using namespace MyClientFeatures;
    const uint32_t field = EncodeAutoRqField(6, 12, 3) | LeftHandBit;
    assert(LeftHandOf(field) && !LeftHandOf(EncodeAutoRqField(255, 48, 255)));
    assert(AutoRqModeOf(field) == 6 && RenderChunksOf(field) == 12 && RenderCounterOf(field) == 3);
    // The whole pass through the world matrix: a point drawn with it lands at -x in view space.
    {
        float view[16] = {0.8f, 0, -0.6f, 0,  0, 1, 0, 0,  0.6f, 0, 0.8f, 0,  1, -2, 3, 1};
        float world[16] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0.5f, 0.2f, -1, 1};
        float vw[16], before[4] = {}, after[4] = {};
        const float p[4] = {0.1f, 0.3f, 0.2f, 1};
        Multiply(view, world, vw);
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) before[r] += vw[c * 4 + r] * p[c];
        assert(MirrorWorldInView(view, world));
        Multiply(view, world, vw);
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) after[r] += vw[c * 4 + r] * p[c];
        assert(std::fabs(after[0] + before[0]) < 1e-4f);
        for (int r = 1; r < 4; ++r) assert(std::fabs(after[r] - before[r]) < 1e-4f);
    }
    std::cout << "PASS: left hand mirror\n";
}

#include "../CompositorGeometry.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace MyClientCompositor;

static bool Near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

int main()
{
    // Looking along +Z (yaw 0, pitch 0) from the origin, 90° vertical FOV on 200x100: a point straight
    // ahead lands in the centre; one block up at distance 1 lands at the top edge (focal = 50 px).
    Camera cam; cam.fov = 90;
    Projector p(cam, 200, 100);
    double x0, y0, x1, y1;
    const float ahead[3] = {0, 0, 1}, up[3] = {0, 1, 1}, behind[3] = {0, 0, -1};
    assert(p.Segment(ahead, up, x0, y0, x1, y1));
    assert(Near(x0, 100) && Near(y0, 50) && Near(x1, 100) && Near(y1, 0));
    // +X is to the LEFT when looking along +Z in Bedrock (yaw 0): the HUD's projector agrees.
    const float left[3] = {1, 0, 1};
    assert(p.Segment(ahead, left, x0, y0, x1, y1) && x1 < 100);
    // fully behind: nothing; half behind: clipped to the near plane, still drawn
    assert(!p.Segment(behind, behind, x0, y0, x1, y1));
    assert(p.Segment(ahead, behind, x0, y0, x1, y1));
    // yaw 90 looks along -X
    Camera turned = cam; turned.yaw = 90;
    Projector q(turned, 200, 100);
    const float minusX[3] = {-1, 0, 0}, minusXup[3] = {-1, 1, 0};
    assert(q.Segment(minusX, minusXup, x0, y0, x1, y1) && Near(x0, 100) && Near(y0, 50));

    // The game's matrices for the same camera give the same picture. View: rows = right, up, -forward
    // (right-handed, looking down -Z), camera-relative (origin = eye). Projection: perspective, 90° vertical.
    {
        Camera c2; c2.x = 10; c2.y = 64; c2.z = -5; c2.yaw = 30; c2.pitch = 20; c2.fov = 70;
        const double rad = 3.14159265358979323846 / 180.0, yaw = c2.yaw * rad, pitch = c2.pitch * rad;
        const double f[3] = {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        const double rt[3] = {-std::cos(yaw), 0.0, -std::sin(yaw)};
        const double u[3] = {rt[1] * f[2] - rt[2] * f[1], rt[2] * f[0] - rt[0] * f[2], rt[0] * f[1] - rt[1] * f[0]};
        Matrices m{};
        for (int k = 0; k < 3; k++) { m.view[k * 4 + 0] = (float)rt[k]; m.view[k * 4 + 1] = (float)u[k]; m.view[k * 4 + 2] = (float)-f[k]; }
        m.view[15] = 1;
        const double aspect = 200.0 / 100.0, t = 1.0 / std::tan(c2.fov * rad / 2), n = 0.1, fa = 100;
        m.proj[0] = (float)(t / aspect); m.proj[5] = (float)t; m.proj[10] = (float)(fa / (n - fa)); m.proj[11] = -1; m.proj[14] = (float)(fa * n / (n - fa));
        m.origin[0] = c2.x; m.origin[1] = c2.y; m.origin[2] = c2.z;
        Projector byAngles(c2, 200, 100), byMatrices(m, 200, 100);
        const float p0[3] = {9, 63, -1}, p1[3] = {11.5f, 65, 0};
        double a0, b0, a1, b1, c0, d0, c1, d1;
        assert(byAngles.Segment(p0, p1, a0, b0, a1, b1) && byMatrices.Segment(p0, p1, c0, d0, c1, d1));
        assert(Near(a0, c0, 1e-3) && Near(b0, d0, 1e-3) && Near(a1, c1, 1e-3) && Near(b1, d1, 1e-3));
        const float behind0[3] = {10, 64, -9}, behind1[3] = {10.5f, 64, -9};
        assert(!byMatrices.Segment(behind0, behind1, c0, d0, c1, d1));
    }

    std::vector<Vertex> v;
    Builder b(v, 200, 100);
    b.Picture();
    assert(v.size() == 6 && Near(v[0].x, -1) && Near(v[0].y, 1) && Near(v[4].x, 1) && Near(v[4].y, -1) && v[4].u == 1 && v[4].v == 1);
    v.clear();
    b.Line(0, 50, 200, 50, 2, FromRgb(0xFF0000));
    assert(v.size() == 6 && v[0].u < 0 && Near(v[0].r, 1) && Near(v[0].g, 0));
    // the quad is 2 px thick: 1 px above and below the centre line (y = 0 in clip space)
    assert(Near(std::fabs(v[0].y), 2.0 / 100));
    v.clear();
    AddBlockOutline(b, p, 0, -1, 3, 0x50B4FF, 2);
    assert(v.size() == 12 * 6);
    v.clear();
    MyClientTabList::Box box{};
    box.min[0] = -0.3f; box.min[1] = -1; box.min[2] = 4; box.max[0] = 0.3f; box.max[1] = 0.8f; box.max[2] = 4.6f;
    box.kind = 0;
    for (auto& m : box.visible) m = 0xFF;
    AddHitboxes(b, p, &box, 1, 1u, 2);
    assert(v.size() == 12 * 8 * 6);
    v.clear();
    AddHitboxes(b, p, &box, 1, 2u, 2);   // players filtered out
    assert(v.empty());
    box.visible[0] = 0x0F;               // half of edge 0 hidden
    AddHitboxes(b, p, &box, 1, 1u, 2);
    assert(v.size() == (11 * 8 + 4) * 6);
    v.clear();
    const uint32_t own[4] = {0x00FF00, 0, 0, 0};   // the player colour chosen in the HUD
    AddHitboxes(b, p, &box, 1, 1u, 2, own);
    assert(!v.empty() && Near(v[0].r, 0) && Near(v[0].g, 1));
    std::puts("compositor geometry ok");
}

#pragma once
// Geometry the game DLL draws inside the game's frame (client/Compositor.cpp): the HUD picture as one
// textured rectangle, and 3D lines (Hitbox, Block Outline) projected with the camera of the very frame
// being drawn, so they no longer lag behind the view like the overlay window did.
// The projection is the HUD's CamProjector maths (MyClient.cpp). Pure code: tests/compositor_geometry.cpp.
#include <cmath>
#include <cstdint>
#include <vector>
#include "TabListChannel.h"

namespace MyClientCompositor {

// Vertex: position in clip space (x, y in -1..1), texture coordinate (u < 0 = plain colour),
// premultiplied RGBA colour.
struct Vertex { float x, y, u, v, r, g, b, a; };
static_assert(sizeof(Vertex) == 32);

// Eye position, rotation in degrees (Bedrock: yaw 0 looks along +Z, positive pitch looks down), vertical FOV.
struct Camera { float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, fov = 70; };

struct Rgba { float r, g, b, a; };
inline Rgba FromRgb(uint32_t rgb, float alpha = 1.f) {   // 0xRRGGBB -> premultiplied
    return {((rgb >> 16) & 0xFF) / 255.f * alpha, ((rgb >> 8) & 0xFF) / 255.f * alpha, (rgb & 0xFF) / 255.f * alpha, alpha};
}

// The game's own camera of the frame (LevelRendererPlayer::setupCamera): view and projection matrices,
// column-major like glm (element row r, column c at [c * 4 + r]), and the world position they are
// relative to. With these the lines land exactly where the game draws, view bobbing included.
struct Matrices {
    float view[16], proj[16];
    float origin[3];    // subtracted from world points before the view matrix (camera-relative rendering)
};

class Projector {
    Camera cam_;
    double width_, height_, rx, ry, rz, ux, uy, uz, fx, fy, fz, focal;
    bool matrices_ = false;
    double mvp_[16] = {};   // proj * view
    double origin_[3] = {};
    void Clip(const float p[3], double out[4]) const {
        const double v[4] = {p[0] - origin_[0], p[1] - origin_[1], p[2] - origin_[2], 1.0};
        for (int r = 0; r < 4; r++) out[r] = mvp_[r] * v[0] + mvp_[4 + r] * v[1] + mvp_[8 + r] * v[2] + mvp_[12 + r] * v[3];
    }
public:
    Projector(const Matrices& m, int width, int height) : width_(width), height_(height), matrices_(true) {
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) {
                double sum = 0;
                for (int k = 0; k < 4; k++) sum += double(m.proj[k * 4 + r]) * m.view[c * 4 + k];
                mvp_[c * 4 + r] = sum;
            }
        for (int i = 0; i < 3; i++) origin_[i] = m.origin[i];
        rx = ry = rz = ux = uy = uz = fx = fy = fz = 0; focal = 1;
    }
    Projector(const Camera& c, int width, int height) : cam_(c), width_(width), height_(height) {
        const double rad = 3.14159265358979323846 / 180.0;
        const double yaw = c.yaw * rad, pitch = c.pitch * rad;
        fx = -std::sin(yaw) * std::cos(pitch); fy = -std::sin(pitch); fz = std::cos(yaw) * std::cos(pitch);
        rx = -std::cos(yaw); ry = 0.0; rz = -std::sin(yaw);
        ux = ry * fz - rz * fy; uy = rz * fx - rx * fz; uz = rx * fy - ry * fx;
        focal = (height / 2.0) / std::tan((c.fov > 1 && c.fov < 179 ? c.fov : 70) * rad / 2.0);
    }
    // World point -> camera space (x right, y up, z forward).
    void ToCam(const float p[3], double out[3]) const {
        const double dx = p[0] - cam_.x, dy = p[1] - cam_.y, dz = p[2] - cam_.z;
        out[0] = dx * rx + dy * ry + dz * rz; out[1] = dx * ux + dy * uy + dz * uz; out[2] = dx * fx + dy * fy + dz * fz;
    }
    // Segment a-b in pixels (clipped against the near plane). False when it is fully behind the camera.
    bool Segment(const float a[3], const float b[3], double& x0, double& y0, double& x1, double& y1) const {
        if (matrices_) {
            // Clip against w (in front of the camera), then divide; NDC y points up.
            const double kNearW = 0.05;
            double ca[4], cb[4];
            Clip(a, ca); Clip(b, cb);
            if (ca[3] < kNearW && cb[3] < kNearW) return false;
            if (ca[3] < kNearW) { const double t = (kNearW - ca[3]) / (cb[3] - ca[3]); for (int i = 0; i < 4; i++) ca[i] += (cb[i] - ca[i]) * t; }
            if (cb[3] < kNearW) { const double t = (kNearW - cb[3]) / (ca[3] - cb[3]); for (int i = 0; i < 4; i++) cb[i] += (ca[i] - cb[i]) * t; }
            x0 = (ca[0] / ca[3] * 0.5 + 0.5) * width_; y0 = (0.5 - ca[1] / ca[3] * 0.5) * height_;
            x1 = (cb[0] / cb[3] * 0.5 + 0.5) * width_; y1 = (0.5 - cb[1] / cb[3] * 0.5) * height_;
            const double lim = 1e5;
            return std::fabs(x0) < lim && std::fabs(x1) < lim && std::fabs(y0) < lim && std::fabs(y1) < lim;
        }
        const double kNear = 0.05;
        double ca[3], cb[3];
        ToCam(a, ca); ToCam(b, cb);
        if (ca[2] < kNear && cb[2] < kNear) return false;
        if (ca[2] < kNear) { const double t = (kNear - ca[2]) / (cb[2] - ca[2]); for (int i = 0; i < 2; i++) ca[i] += (cb[i] - ca[i]) * t; ca[2] = kNear; }
        if (cb[2] < kNear) { const double t = (kNear - cb[2]) / (ca[2] - cb[2]); for (int i = 0; i < 2; i++) cb[i] += (ca[i] - cb[i]) * t; cb[2] = kNear; }
        x0 = width_ / 2.0 + ca[0] / ca[2] * focal; y0 = height_ / 2.0 - ca[1] / ca[2] * focal;
        x1 = width_ / 2.0 + cb[0] / cb[2] * focal; y1 = height_ / 2.0 - cb[1] / cb[2] * focal;
        const double lim = 1e5;
        return std::fabs(x0) < lim && std::fabs(x1) < lim && std::fabs(y0) < lim && std::fabs(y1) < lim;
    }
};

class Builder {
    std::vector<Vertex>& out_;
    double width_, height_;
    void Push(double px, double py, float u, float v, const Rgba& c) {
        out_.push_back({static_cast<float>(px / width_ * 2.0 - 1.0), static_cast<float>(1.0 - py / height_ * 2.0), u, v, c.r, c.g, c.b, c.a});
    }
public:
    Builder(std::vector<Vertex>& out, int width, int height) : out_(out), width_(width), height_(height) {}
    // The whole HUD picture over the frame (two triangles).
    void Picture() {
        const Rgba w{1, 1, 1, 1};
        Push(0, 0, 0, 0, w); Push(width_, 0, 1, 0, w); Push(0, height_, 0, 1, w);
        Push(width_, 0, 1, 0, w); Push(width_, height_, 1, 1, w); Push(0, height_, 0, 1, w);
    }
    // A line of `thickness` pixels as a quad (two triangles).
    void Line(double x0, double y0, double x1, double y1, double thickness, const Rgba& c) {
        const double dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) return;
        const double nx = -dy / len * thickness / 2.0, ny = dx / len * thickness / 2.0;
        Push(x0 + nx, y0 + ny, -1, 0, c); Push(x1 + nx, y1 + ny, -1, 0, c); Push(x0 - nx, y0 - ny, -1, 0, c);
        Push(x1 + nx, y1 + ny, -1, 0, c); Push(x1 - nx, y1 - ny, -1, 0, c); Push(x0 - nx, y0 - ny, -1, 0, c);
    }
    void Line3D(const Projector& p, const float a[3], const float b[3], double thickness, const Rgba& c) {
        double x0, y0, x1, y1;
        if (p.Segment(a, b, x0, y0, x1, y1)) Line(x0, y0, x1, y1, thickness, c);
    }
};

// Hitbox colours by kind (player, item, entity, mob) — the same as the HUD draws them by default.
constexpr uint32_t kHitboxColors[4] = {0xFF4646, 0xFFD23C, 0xEBEBEB, 0x5ADC78};

// Hitboxes: only the visible segments of each edge, of the kinds in `filter` (bit = kind), in `colors`
// (0xRRGGBB by kind).
inline void AddHitboxes(Builder& b, const Projector& p, const MyClientTabList::Box* boxes, uint32_t count, uint32_t filter, double thickness,
                        const uint32_t* colors = kHitboxColors) {
    if (!colors) colors = kHitboxColors;
    for (uint32_t i = 0; i < count; i++) {
        const auto& box = boxes[i];
        if (!((filter >> (box.kind & 3)) & 1u)) continue;
        const Rgba c = FromRgb(colors[box.kind & 3]);
        for (int e = 0; e < 12; e++) {
            float a[3], z[3];
            MyClientTabList::Corner(box, MyClientTabList::kEdges[e][0], a);
            MyClientTabList::Corner(box, MyClientTabList::kEdges[e][1], z);
            const int n = static_cast<int>(MyClientTabList::BoxSegments);
            for (int s = 0; s < n; s++) {
                if (!((box.visible[e] >> s) & 1)) continue;
                float p0[3], p1[3];
                for (int k = 0; k < 3; k++) { p0[k] = a[k] + (z[k] - a[k]) * s / n; p1[k] = a[k] + (z[k] - a[k]) * (s + 1) / n; }
                b.Line3D(p, p0, p1, thickness, c);
            }
        }
    }
}

// Block Outline: the 12 edges of the block at (x, y, z), slightly enlarged so it sits on the game's own outline.
inline void AddBlockOutline(Builder& b, const Projector& p, int x, int y, int z, uint32_t rgb, double thickness) {
    const float g = 0.004f;
    MyClientTabList::Box box{};
    box.min[0] = x - g; box.min[1] = y - g; box.min[2] = z - g;
    box.max[0] = x + 1 + g; box.max[1] = y + 1 + g; box.max[2] = z + 1 + g;
    const Rgba c = FromRgb(rgb);
    for (int e = 0; e < 12; e++) {
        float a[3], z2[3];
        MyClientTabList::Corner(box, MyClientTabList::kEdges[e][0], a);
        MyClientTabList::Corner(box, MyClientTabList::kEdges[e][1], z2);
        b.Line3D(p, a, z2, thickness, c);
    }
}
}  // namespace MyClientCompositor

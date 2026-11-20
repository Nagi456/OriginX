#pragma once
// Replay video export, the parts that are plain arithmetic (2026-11-12, docs/superpowers/specs/2026-11-12-replay-camera-
// editor-design.md part 5): which recording times the frames show, how big the video is, its bitrate, shrinking the
// game's picture, and a file name that never overwrites. The DLL does the capture and the encoding (VideoExport.cpp);
// the HUD drives it frame by frame. Plain C++17, tested by tests/replay_export.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MyClientReplayExport {

enum class Resolution : uint8_t { P720, P1080, P1440, Native };
enum class Quality : uint8_t { Low, Medium, High };

// Frames at startMs, startMs + 1/fps, ... before endMs (recording milliseconds).
struct Plan {
    uint64_t startMs = 0, endMs = 0;
    int fps = 60;
    bool Valid() const { return endMs > startMs && (fps == 30 || fps == 60); }
    double StepMs() const { return 1000.0 / fps; }
    uint32_t FrameCount() const {
        if (!Valid()) return 0;
        return static_cast<uint32_t>(std::ceil(static_cast<double>(endMs - startMs) * fps / 1000.0 - 1e-9));
    }
    double TimeOf(uint32_t frame) const { return static_cast<double>(startMs) + frame * StepMs(); }
};

// Seconds left from the average time per frame so far; negative while nothing is done (unknown).
inline double RemainingSeconds(uint32_t done, uint32_t total, double elapsedSeconds) {
    if (done >= total) return 0.0;
    if (done == 0) return -1.0;
    return elapsedSeconds / done * (total - done);
}

// The video's size: the chosen height (or the game's), never above the game's picture, width by its aspect, both even.
inline void OutputSize(Resolution choice, int backWidth, int backHeight, int& width, int& height) {
    width = height = 0;
    if (backWidth <= 0 || backHeight <= 0) return;
    const int wanted = choice == Resolution::P720 ? 720 : choice == Resolution::P1080 ? 1080 : choice == Resolution::P1440 ? 1440 : backHeight;
    height = std::min(wanted, backHeight);
    width = static_cast<int>(std::lround(static_cast<double>(backWidth) * height / backHeight));
    width &= ~1;
    height &= ~1;
}

// Bits per second: about 0.05 / 0.1 / 0.2 bits per pixel per frame (H.264).
inline uint32_t Bitrate(Quality q, int width, int height, int fps) {
    const double bitsPerPixel = q == Quality::Low ? 0.05 : q == Quality::Medium ? 0.1 : 0.2;
    const double rate = static_cast<double>(width) * height * fps * bitsPerPixel;
    return static_cast<uint32_t>(std::clamp(rate, 1000000.0, 80000000.0));
}

// Area average of a BGRA picture (srcPitch in bytes) into out (outWidth x outHeight, tightly packed).
inline void Downscale(const uint32_t* src, int srcWidth, int srcHeight, int srcPitch, uint32_t* out, int outWidth, int outHeight) {
    const auto* base = reinterpret_cast<const uint8_t*>(src);
    for (int y = 0; y < outHeight; ++y) {
        const int y0 = static_cast<int>(static_cast<int64_t>(y) * srcHeight / outHeight);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(y + 1) * srcHeight / outHeight));
        for (int x = 0; x < outWidth; ++x) {
            const int x0 = static_cast<int>(static_cast<int64_t>(x) * srcWidth / outWidth);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<int64_t>(x + 1) * srcWidth / outWidth));
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy) {
                const auto* row = reinterpret_cast<const uint32_t*>(base + static_cast<size_t>(sy) * srcPitch);
                for (int sx = x0; sx < x1; ++sx)
                    for (int c = 0; c < 4; ++c) sum[c] += (row[sx] >> (8 * c)) & 0xFF;
            }
            const uint32_t n = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            uint32_t pixel = 0;
            for (int c = 0; c < 4; ++c) pixel |= ((sum[c] / n) & 0xFF) << (8 * c);
            out[static_cast<size_t>(y) * outWidth + x] = pixel;
        }
    }
}

// A path from UTF-8 text (u8path is deprecated in C++20, which the DLL uses).
inline std::filesystem::path PathFromUtf8(const std::string& text) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
#else
    return std::filesystem::u8path(text);
#endif
}

// <folder>/<name>.mp4, or "<name> (2).mp4" and up when it exists. Characters Windows refuses in a name become '_'.
inline std::filesystem::path UniqueVideoPath(const std::filesystem::path& folder, const std::string& name) {
    std::string clean;
    for (char c : name) clean += (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
                                  static_cast<unsigned char>(c) < 32) ? '_' : c;
    if (clean.empty()) clean = "replay";
    std::error_code ec;
    std::filesystem::path path = folder / PathFromUtf8(clean + ".mp4");
    for (int n = 2; std::filesystem::exists(path, ec) && n < 1000; ++n)
        path = folder / PathFromUtf8(clean + " (" + std::to_string(n) + ").mp4");
    return path;
}

} // namespace MyClientReplayExport

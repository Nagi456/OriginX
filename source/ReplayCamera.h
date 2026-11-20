#pragma once
// Replay camera keyframes (2026-11-12, docs/superpowers/specs/2026-11-12-replay-camera-editor-design.md): a point is a
// time in the recording, a camera pose (eye position, pitch, yaw) and how the camera moves on to the next point.
// The path goes through every point (Catmull-Rom for the position), yaw turns the short way. Saved next to the
// recording as <name>.camera-default.json; the recording itself is never written. Plain C++17: HUD, DLL and tests.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace MyClientReplayCamera {

enum class Ease : uint8_t { Linear, In, Out, InOut, Hold, Cut };
struct Pose { float x, y, z, pitch, yaw; };
struct Keyframe { uint64_t t; Pose pose; Ease ease = Ease::InOut; };

constexpr size_t MaxKeyframes = 64;      // the HUD -> DLL channel holds this many
constexpr uint64_t ReplaceWithinMs = 50; // a point this close in time replaces the old one

// How far along a segment the camera is (0..1) for the time fraction u (0..1).
inline float EaseValue(Ease e, float u)
{
    u = std::clamp(u, 0.f, 1.f);
    switch (e)
    {
    case Ease::Linear: return u;
    case Ease::In: return u * u;
    case Ease::Out: return 1.f - (1.f - u) * (1.f - u);
    case Ease::InOut: return u * u * (3.f - 2.f * u);
    case Ease::Hold: return u >= 1.f ? 1.f : 0.f;
    case Ease::Cut: return u > 0.f ? 1.f : 0.f;
    }
    return u;
}

// Keeps `path` in time order; a point within ReplaceWithinMs of another replaces it. False when full.
inline bool AddOrReplace(std::vector<Keyframe>& path, const Keyframe& k)
{
    for (auto& old : path)
    {
        const uint64_t gap = old.t > k.t ? old.t - k.t : k.t - old.t;
        if (gap <= ReplaceWithinMs) { old = k; std::sort(path.begin(), path.end(), [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; }); return true; }
    }
    if (path.size() >= MaxKeyframes) return false;
    path.insert(std::upper_bound(path.begin(), path.end(), k, [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; }), k);
    return true;
}

namespace detail {
inline float CatmullRom(float p0, float p1, float p2, float p3, float s)
{
    const float s2 = s * s, s3 = s2 * s;
    return 0.5f * (2.f * p1 + (-p0 + p2) * s + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * s2 + (-p0 + 3.f * p1 - 3.f * p2 + p3) * s3);
}
// The difference b - a in (-180, 180].
inline float YawStep(float a, float b)
{
    float d = std::fmod(b - a, 360.f);
    if (d > 180.f) d -= 360.f;
    if (d <= -180.f) d += 360.f;
    return d;
}
} // namespace detail

// The camera at time tMs (recording milliseconds). False with fewer than two points.
inline bool Evaluate(const std::vector<Keyframe>& path, double tMs, Pose& out)
{
    if (path.size() < 2) return false;
    if (tMs <= static_cast<double>(path.front().t)) { out = path.front().pose; return true; }
    if (tMs >= static_cast<double>(path.back().t)) { out = path.back().pose; return true; }
    size_t i = 0;
    while (i + 1 < path.size() && static_cast<double>(path[i + 1].t) <= tMs) ++i;
    const Keyframe& a = path[i];
    const Keyframe& b = path[i + 1];
    const Keyframe& before = i > 0 ? path[i - 1] : a;
    const Keyframe& after = i + 2 < path.size() ? path[i + 2] : b;
    const double span = static_cast<double>(b.t - a.t);
    const float s = EaseValue(a.ease, span > 0 ? static_cast<float>((tMs - static_cast<double>(a.t)) / span) : 1.f);
    out.x = detail::CatmullRom(before.pose.x, a.pose.x, b.pose.x, after.pose.x, s);
    out.y = detail::CatmullRom(before.pose.y, a.pose.y, b.pose.y, after.pose.y, s);
    out.z = detail::CatmullRom(before.pose.z, a.pose.z, b.pose.z, after.pose.z, s);
    out.pitch = a.pose.pitch + (b.pose.pitch - a.pose.pitch) * s;
    out.yaw = a.pose.yaw + detail::YawStep(a.pose.yaw, b.pose.yaw) * s;
    return true;
}

inline const char* EaseName(Ease e)
{
    switch (e)
    {
    case Ease::Linear: return "linear";
    case Ease::In: return "in";
    case Ease::Out: return "out";
    case Ease::InOut: return "inout";
    case Ease::Hold: return "hold";
    case Ease::Cut: return "cut";
    }
    return "inout";
}

inline bool EaseFromName(const std::string& name, Ease& out)
{
    for (Ease e : {Ease::Linear, Ease::In, Ease::Out, Ease::InOut, Ease::Hold, Ease::Cut})
        if (name == EaseName(e)) { out = e; return true; }
    return false;
}

// {"schemaVersion":1,"replay":"X.mcrec","keyframes":[{"t":..,"x":..,"y":..,"z":..,"pitch":..,"yaw":..,"ease":"inout"}]}
inline std::string ToJson(const std::vector<Keyframe>& path, const std::string& replayName)
{
    std::string name;
    for (char c : replayName) if (c != '"' && c != '\\' && static_cast<unsigned char>(c) >= 32) name += c;
    std::string out = "{\"schemaVersion\":1,\"replay\":\"" + name + "\",\"keyframes\":[";
    char line[256];
    for (size_t i = 0; i < path.size(); ++i)
    {
        const Keyframe& k = path[i];
        std::snprintf(line, sizeof line, "%s\n  {\"t\":%llu,\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"pitch\":%.9g,\"yaw\":%.9g,\"ease\":\"%s\"}",
                      i ? "," : "", static_cast<unsigned long long>(k.t), k.pose.x, k.pose.y, k.pose.z, k.pose.pitch, k.pose.yaw, EaseName(k.ease));
        out += line;
    }
    return out + "\n]}\n";
}

// Reads ToJson's format only (known keys; anything else in an object is ignored). False with a reason in `error`.
inline bool FromJson(const std::string& text, std::vector<Keyframe>& path, std::string& error)
{
    static const std::regex versionRe("\"schemaVersion\"\\s*:\\s*(\\d+)");
    std::smatch m;
    if (!std::regex_search(text, m, versionRe)) { error = "not a camera file (no schemaVersion)"; return false; }
    if (m[1].str() != "1") { error = "camera file version " + m[1].str() + " is not supported"; return false; }
    const size_t list = text.find("\"keyframes\"");
    const size_t open = list == std::string::npos ? std::string::npos : text.find('[', list);
    if (open == std::string::npos) { error = "camera file has no keyframes"; return false; }
    const size_t close = text.find(']', open);
    if (close == std::string::npos) { error = "camera file is cut off"; return false; }
    std::vector<Keyframe> read;
    const std::string body = text.substr(open + 1, close - open - 1);
    static const std::regex objectRe("\\{[^{}]*\\}");
    const auto number = [](const std::string& object, const char* key, double& value) {
        const std::regex re(std::string("\"") + key + "\"\\s*:\\s*(-?[0-9.eE+-]+)");
        std::smatch n;
        if (!std::regex_search(object, n, re)) return false;
        char* end = nullptr;
        value = std::strtod(n[1].str().c_str(), &end);
        return end && *end == '\0' && std::isfinite(value);
    };
    static const std::regex easeRe("\"ease\"\\s*:\\s*\"([a-z]+)\"");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), objectRe); it != std::sregex_iterator(); ++it)
    {
        const std::string object = it->str();
        double t, x, y, z, pitch, yaw;
        if (!number(object, "t", t) || t < 0 || t > 1e12 || !number(object, "x", x) || !number(object, "y", y) || !number(object, "z", z) ||
            !number(object, "pitch", pitch) || !number(object, "yaw", yaw))
        { error = "a camera point is incomplete"; return false; }
        Keyframe k{static_cast<uint64_t>(t), Pose{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                                                   static_cast<float>(pitch), static_cast<float>(yaw)}, Ease::InOut};
        std::smatch e;
        if (std::regex_search(object, e, easeRe) && !EaseFromName(e[1].str(), k.ease)) { error = "unknown movement '" + e[1].str() + "'"; return false; }
        if (!AddOrReplace(read, k)) { error = "more than 64 camera points"; return false; }
    }
    path = std::move(read);
    error.clear();
    return true;
}

// A camera name: letters, digits, '-' and '_' only (it becomes part of a file name).
inline bool IsCameraName(const std::string& name)
{
    if (name.empty() || name.size() > 40) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

// X.mcrec -> X.camera-<name>.json beside it.
inline std::filesystem::path CameraFileFor(const std::filesystem::path& replay, const std::string& name = "default")
{
    std::filesystem::path out = replay;
    out.replace_extension();
    out += ".camera-" + (IsCameraName(name) ? name : std::string("default")) + ".json";
    return out;
}

// The camera files of a recording, by name: "default" first, then the others in name order.
inline std::vector<std::string> CameraNames(const std::filesystem::path& replay)
{
    std::vector<std::string> names;
    std::filesystem::path stem = replay;
    stem.replace_extension();
    const std::string prefix = stem.filename().string() + ".camera-";
    std::error_code ec;
    for (std::filesystem::directory_iterator it(replay.parent_path().empty() ? "." : replay.parent_path(), ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string file = it->path().filename().string();
        if (file.size() <= prefix.size() + 5 || file.compare(0, prefix.size(), prefix) != 0 || file.compare(file.size() - 5, 5, ".json") != 0) continue;
        const std::string name = file.substr(prefix.size(), file.size() - prefix.size() - 5);
        if (IsCameraName(name)) names.push_back(name);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        if (a == "default" || b == "default") return a == "default" && b != "default";
        return a < b;
    });
    return names;
}

// A name for a new copy: "1", "2", ... the first that has no file yet.
inline std::string NextCameraName(const std::filesystem::path& replay)
{
    std::error_code ec;
    for (int n = 1; n < 1000; ++n)
        if (!std::filesystem::exists(CameraFileFor(replay, std::to_string(n)), ec)) return std::to_string(n);
    return "999";
}

// The camera file of one recording. A file that exists but cannot be read is never written over (it may come from a
// newer OriginX or be edited by hand): Save refuses until another recording is loaded. Saving writes a temporary file
// and renames it over the old one, so a failed write never leaves half a file.
class CameraFile {
    std::filesystem::path path_, recording_;
    std::string name_ = "default";
    bool locked_ = false;
public:
    bool Locked() const { return locked_; }
    const std::string& Name() const { return name_; }
    const std::filesystem::path& Path() const { return path_; }
    // True with the points (none when there is no file yet); false with the reason when the file cannot be read.
    bool Load(const std::filesystem::path& recording, std::vector<Keyframe>& out, std::string& error)
    {
        return Load(recording, "default", out, error);
    }
    bool Load(const std::filesystem::path& recording, const std::string& name, std::vector<Keyframe>& out, std::string& error)
    {
        name_ = IsCameraName(name) ? name : "default";
        path_ = CameraFileFor(recording, name_);
        recording_ = recording;
        locked_ = false;
        out.clear();
        std::error_code ec;
        if (!std::filesystem::exists(path_, ec)) return true;
        std::FILE* f = nullptr;
#ifdef _WIN32
        f = _wfopen(path_.c_str(), L"rb");
#else
        f = std::fopen(path_.c_str(), "rb");
#endif
        std::string text;
        if (f) { char buffer[4096]; size_t n; while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) text.append(buffer, n); std::fclose(f); }
        if (!f || !FromJson(text, out, error)) {
            if (!f) error = "cannot open the camera file";
            out.clear();
            locked_ = true;
            return false;
        }
        return true;
    }
    bool Save(const std::vector<Keyframe>& path, std::string& error)
    {
        if (locked_) { error = "the camera file could not be read, so it is left as it is (rename or delete it to start a new one)"; return false; }
        if (path_.empty()) { error = "no recording"; return false; }
        const auto u8 = recording_.filename().u8string();   // std::string in C++17, std::u8string in C++20 (the DLL)
        const std::string text = ToJson(path, std::string(u8.begin(), u8.end()));
        const std::filesystem::path temporary = std::filesystem::path(path_.native() + std::filesystem::path(".tmp").native());
        std::FILE* f = nullptr;
#ifdef _WIN32
        f = _wfopen(temporary.c_str(), L"wb");
#else
        f = std::fopen(temporary.c_str(), "wb");
#endif
        if (!f) { error = "cannot write the camera file"; return false; }
        const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        const bool closed = std::fclose(f) == 0;
        std::error_code ec;
        if (!written || !closed) { std::filesystem::remove(temporary, ec); error = "cannot write the camera file"; return false; }
        std::filesystem::rename(temporary, path_, ec);
        if (ec) { std::filesystem::remove(temporary, ec); error = "cannot replace the camera file"; return false; }
        return true;
    }
};

} // namespace MyClientReplayCamera

// Replay camera keyframes: easing, the path through them, the camera file (ReplayCamera.h).
#include "../ReplayCamera.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace MyClientReplayCamera;

namespace {
Keyframe K(uint64_t t, float x, float yaw = 0.f, Ease e = Ease::Linear) { return Keyframe{t, Pose{x, 64.f, 0.f, 0.f, yaw}, e}; }
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
}

int main()
{
    Pose p{};
    std::vector<Keyframe> path;
    assert(!Evaluate(path, 0, p));
    path.push_back(K(0, 0));
    assert(!Evaluate(path, 0, p));   // one point: no path

    // Two points, linear: halfway is halfway; before the first and after the last the camera stays there.
    path.push_back(K(1000, 10));
    assert(Evaluate(path, 500, p) && Near(p.x, 5.f) && Near(p.y, 64.f));
    assert(Evaluate(path, -100, p) && Near(p.x, 0.f));
    assert(Evaluate(path, 2000, p) && Near(p.x, 10.f));

    // Easing shapes.
    assert(Near(EaseValue(Ease::Linear, 0.5f), 0.5f));
    assert(EaseValue(Ease::In, 0.5f) < 0.5f && EaseValue(Ease::Out, 0.5f) > 0.5f);
    assert(Near(EaseValue(Ease::InOut, 0.5f), 0.5f) && EaseValue(Ease::InOut, 0.25f) < 0.25f);
    assert(EaseValue(Ease::Hold, 0.99f) == 0.f && EaseValue(Ease::Cut, 0.01f) == 1.f);
    for (Ease e : {Ease::Linear, Ease::In, Ease::Out, Ease::InOut})
        assert(Near(EaseValue(e, 0.f), 0.f) && Near(EaseValue(e, 1.f), 1.f));

    // Catmull-Rom passes through every point.
    {
        std::vector<Keyframe> four = {K(0, 0), K(1000, 5), K(2000, -3), K(3000, 8)};
        for (const auto& k : four) assert(Evaluate(four, static_cast<double>(k.t), p) && Near(p.x, k.pose.x));
    }

    // Yaw turns the short way across 360.
    {
        std::vector<Keyframe> turn = {K(0, 0, 350.f), K(1000, 0, 10.f)};
        assert(Evaluate(turn, 500, p));
        const float yaw = std::fmod(p.yaw + 720.f, 360.f);
        assert(Near(yaw, 0.f, 1e-3f) || Near(yaw, 360.f, 1e-3f));
    }

    // Adding: kept in time order; within 50 ms replaces; at most 64.
    {
        std::vector<Keyframe> a;
        assert(AddOrReplace(a, K(1000, 1)) && AddOrReplace(a, K(1030, 2)));
        assert(a.size() == 1 && Near(a[0].pose.x, 2.f));
        assert(AddOrReplace(a, K(500, 3)) && a.size() == 2 && a[0].t == 500 && a[1].t == 1030);
        std::vector<Keyframe> full;
        for (uint64_t i = 0; i < MaxKeyframes; ++i) assert(AddOrReplace(full, K(i * 100, 0)));
        assert(!AddOrReplace(full, K(MaxKeyframes * 100, 0)) && full.size() == MaxKeyframes);
        assert(AddOrReplace(full, K(0, 9)) && full.size() == MaxKeyframes);   // replacing is fine when full
    }

    // The camera file: round trip, refusals.
    {
        std::vector<Keyframe> a = {K(0, 1.5f, 350.f, Ease::InOut), K(2500, -2.25f, 12.5f, Ease::Cut), K(4000, 3, 0, Ease::Hold)};
        a[1].pose.pitch = -30.f;
        const std::string json = ToJson(a, "rec.mcrec");
        std::vector<Keyframe> b;
        std::string error;
        assert(FromJson(json, b, error) && error.empty() && b.size() == a.size());
        for (size_t i = 0; i < a.size(); ++i)
            assert(b[i].t == a[i].t && b[i].ease == a[i].ease && Near(b[i].pose.x, a[i].pose.x) && Near(b[i].pose.yaw, a[i].pose.yaw) &&
                   Near(b[i].pose.pitch, a[i].pose.pitch) && Near(b[i].pose.y, a[i].pose.y));
        b.clear();
        assert(!FromJson("{\"schemaVersion\":2,\"keyframes\":[]}", b, error) && !error.empty());
        error.clear();
        assert(!FromJson("not json", b, error) && !error.empty());
        error.clear();
        assert(!FromJson("{\"schemaVersion\":1}", b, error) && !error.empty());
    }

    assert(CameraFileFor("a/b/rec.mcrec") == std::filesystem::path("a/b/rec.camera-default.json"));

    // The camera file next to a recording: a file that cannot be read is never overwritten (it may be newer or
    // hand-edited); a missing one is simply empty; saving replaces the file whole (temporary file, then rename).
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "myclient-camera-test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path recording = dir / "rec.mcrec";
        std::string error;
        std::vector<Keyframe> got;

        CameraFile fresh;
        assert(fresh.Load(recording, got, error) && got.empty() && !fresh.Locked());
        std::vector<Keyframe> two = {K(0, 1), K(1000, 2)};
        assert(fresh.Save(two, error));
        CameraFile again;
        assert(again.Load(recording, got, error) && got.size() == 2 && got[1].pose.x == 2.f);
        {   // the file names the recording it belongs to
            std::FILE* g = std::fopen(CameraFileFor(recording).string().c_str(), "rb");
            char head[80] = {};
            std::fread(head, 1, sizeof head - 1, g);
            std::fclose(g);
            assert(std::string(head).find("\"replay\":\"rec.mcrec\"") != std::string::npos);
        }
        assert(fs::exists(CameraFileFor(recording)) && !fs::exists(fs::path(CameraFileFor(recording).string() + ".tmp")));

        { std::FILE* f = std::fopen(CameraFileFor(recording).string().c_str(), "wb"); std::fputs("{\"schemaVersion\":2}", f); std::fclose(f); }
        CameraFile newer;
        error.clear();
        assert(!newer.Load(recording, got, error) && !error.empty() && newer.Locked() && got.empty());
        error.clear();
        assert(!newer.Save(two, error) && !error.empty());
        std::FILE* f = std::fopen(CameraFileFor(recording).string().c_str(), "rb");
        char text[64] = {};
        std::fread(text, 1, sizeof text - 1, f);
        std::fclose(f);
        assert(std::string(text) == "{\"schemaVersion\":2}");   // untouched
        fs::remove_all(dir);
    }
    // Named camera files: rec.camera-<name>.json, default first in the list, a fresh numbered name for a copy.
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "myclient-camera-names";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path recording = dir / "rec.mcrec";
        assert(CameraFileFor(recording, "take2") == dir / "rec.camera-take2.json");
        assert(CameraNames(recording).empty());
        std::string error;
        CameraFile a;
        std::vector<Keyframe> got;
        assert(a.Load(recording, "default", got, error) && a.Save({K(0, 1), K(500, 2)}, error));
        assert(NextCameraName(recording) == "1");
        CameraFile b;
        assert(b.Load(recording, NextCameraName(recording), got, error) && b.Save({K(0, 3), K(900, 4)}, error) && b.Name() == "1");
        assert(NextCameraName(recording) == "2");
        { std::FILE* f = std::fopen((dir / "other.camera-zzz.json").string().c_str(), "wb"); std::fclose(f); }   // another recording's
        const auto names = CameraNames(recording);
        assert((names == std::vector<std::string>{"default", "1"}));
        CameraFile c;
        assert(c.Load(recording, "1", got, error) && got.size() == 2 && got[0].pose.x == 3.f);
        assert(!IsCameraName("") && !IsCameraName("a/b") && !IsCameraName("..") && IsCameraName("take-2_x"));
        fs::remove_all(dir);
    }
    // Absurd times are refused rather than converted (a double beyond uint64 is undefined behaviour).
    {
        std::vector<Keyframe> b;
        std::string error;
        assert(!FromJson("{\"schemaVersion\":1,\"keyframes\":[{\"t\":1e30,\"x\":0,\"y\":0,\"z\":0,\"pitch\":0,\"yaw\":0}]}", b, error));
    }
    std::puts("replay camera ok");
    return 0;
}

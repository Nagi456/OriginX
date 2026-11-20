// Replay video export: the frame plan, picture downscaling, output sizes, bitrates and file names (ReplayExport.h).
#include "../ReplayExport.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
using namespace MyClientReplayExport;

int main()
{
    // The plan: frames from start to end inclusive of the start, exclusive of the end.
    {
        const Plan p{1000, 3000, 30};
        assert(p.Valid() && p.FrameCount() == 60);
        assert(p.TimeOf(0) == 1000.0 && std::fabs(p.TimeOf(30) - 2000.0) < 1e-9 && std::fabs(p.TimeOf(59) - (3000.0 - 1000.0 / 30)) < 1e-9);
        assert(std::fabs(p.StepMs() - 1000.0 / 30) < 1e-12);
        assert(!(Plan{3000, 3000, 30}.Valid()) && !(Plan{3000, 1000, 30}.Valid()) && !(Plan{0, 1000, 0}.Valid()) && !(Plan{0, 1000, 45}.Valid()));
        assert((Plan{0, 1000, 60}.FrameCount() == 60));
        // remaining time from the average so far
        assert(RemainingSeconds(0, 60, 0.0) < 0);                  // nothing done: unknown
        assert(std::fabs(RemainingSeconds(10, 60, 5.0) - 25.0) < 1e-9);
        assert(RemainingSeconds(60, 60, 30.0) == 0.0);
    }
    // Output sizes: even, aspect kept, never bigger than the game's picture.
    {
        int w = 0, h = 0;
        OutputSize(Resolution::P1080, 2560, 1440, w, h); assert(w == 1920 && h == 1080);
        OutputSize(Resolution::P1440, 1920, 1080, w, h); assert(w == 1920 && h == 1080);   // not upscaled
        OutputSize(Resolution::Native, 1921, 1081, w, h); assert(w == 1920 && h == 1080);  // even
        OutputSize(Resolution::P720, 1920, 1200, w, h); assert(h == 720 && w == 1152);
        OutputSize(Resolution::P720, 0, 0, w, h); assert(w == 0 && h == 0);
    }
    // Bitrates grow with quality and size.
    {
        assert(Bitrate(Quality::Low, 1920, 1080, 60) < Bitrate(Quality::Medium, 1920, 1080, 60));
        assert(Bitrate(Quality::Medium, 1920, 1080, 60) < Bitrate(Quality::High, 1920, 1080, 60));
        assert(Bitrate(Quality::Medium, 1280, 720, 30) < Bitrate(Quality::Medium, 1920, 1080, 30));
        assert(Bitrate(Quality::Medium, 1920, 1080, 60) >= 8000000u && Bitrate(Quality::High, 2560, 1440, 60) <= 80000000u);
    }
    // Downscale: area average; same size is a copy honouring the source pitch.
    {
        const uint32_t src[4 * 2] = {0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF,
                                     0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF};
        std::vector<uint32_t> out(2);
        Downscale(src, 4, 2, 4 * 4, out.data(), 2, 1);
        assert(out[0] == 0xFF000000 && out[1] == 0xFFFFFFFF);
        const uint32_t grey[2] = {0xFF000000, 0xFFFEFEFE};
        std::vector<uint32_t> one(1);
        Downscale(grey, 2, 1, 8, one.data(), 1, 1);
        assert((one[0] & 0xFF) == 0x7F && ((one[0] >> 24) & 0xFF) == 0xFF);
        std::vector<uint32_t> padded = {1, 2, 99, 3, 4, 99};   // pitch 3 pixels
        std::vector<uint32_t> copy(4);
        Downscale(padded.data(), 2, 2, 3 * 4, copy.data(), 2, 2);
        assert((copy == std::vector<uint32_t>{1, 2, 3, 4}));
    }
    // A file that exists is never written over: (2), (3)...
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "myclient-export-test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        assert(UniqueVideoPath(dir, "rec_default") == dir / "rec_default.mp4");
        { std::FILE* f = std::fopen((dir / "rec_default.mp4").string().c_str(), "wb"); std::fclose(f); }
        assert(UniqueVideoPath(dir, "rec_default") == dir / "rec_default (2).mp4");
        { std::FILE* f = std::fopen((dir / "rec_default (2).mp4").string().c_str(), "wb"); std::fclose(f); }
        assert(UniqueVideoPath(dir, "rec_default") == dir / "rec_default (3).mp4");
        assert(UniqueVideoPath(dir, "a/b:c*d") == dir / "a_b_c_d.mp4");   // no path or reserved characters
        fs::remove_all(dir);
    }
    std::puts("replay export ok");
    return 0;
}

#include "launcher/SelfUpdate.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace MyClientLauncher;

static void Reject(const std::function<void()>& action)
{
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}
static void Write(const fs::path& path, const std::string& value) { std::ofstream out(path, std::ios::binary); out << value; }
static LauncherInfo Parse(const Json& value) { return ParseLauncherInfo(Json::parse(value.dump())); }
static std::string Read(const fs::path& path) { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }

int main()
{
    const std::string url = std::string(PublicDownloads) + "launcher/15/MyClientLauncher.exe";
    const std::string sha(64, 'a');
    Json doc = {{"schemaVersion", 1}, {"build", 15}, {"url", url}, {"sha256", sha}, {"bytes", 4000000}};
    const auto info = Parse(doc);
    assert(info.build == 15 && info.url == url && info.sha256 == sha && info.bytes == 4000000);

    // Wrong schema, another build's file, another host, bad digest, too big, zero build.
    for (const auto& [key, value] : std::vector<std::pair<std::string, Json>>{
             {"schemaVersion", 2}, {"url", std::string(PublicDownloads) + "launcher/14/MyClientLauncher.exe"},
             {"url", "https://example.com/launcher/15/MyClientLauncher.exe"}, {"sha256", "ABC"},
             {"bytes", 65ull * 1024 * 1024}, {"build", 0}})
    {
        Json changed = doc;
        changed[key] = value;
        Reject([&] { Parse(changed); });
    }

    // A local build (0) never replaces itself; otherwise only a newer build.
    assert(!LauncherUpdateWanted(0, info));
    assert(LauncherUpdateWanted(14, info));
    assert(!LauncherUpdateWanted(15, info));
    assert(!LauncherUpdateWanted(16, info));

    // Swap: the running exe becomes .old, the new one takes its name; the .old is removed later.
    const auto dir = fs::temp_directory_path() / "myclient-self-update-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto exe = dir / "MyClientLauncher.exe", fresh = dir / "download.exe";
    Write(exe, "old");
    Write(fresh, "new");
    Write(OldLauncherPath(exe), "older");   // left from an earlier update
    SwapLauncher(exe, fresh);
    assert(Read(exe) == "new" && Read(OldLauncherPath(exe)) == "old" && !fs::exists(fresh));
    RemoveOldLauncher(exe);
    assert(!fs::exists(OldLauncherPath(exe)) && fs::exists(exe));

    // The new file missing: the original is put back.
    Reject([&] { SwapLauncher(exe, dir / "missing.exe"); });
    assert(Read(exe) == "new");
    fs::remove_all(dir);
    std::cout << "launcher self update tests passed\n";
}

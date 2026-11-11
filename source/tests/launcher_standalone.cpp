// A release that runs without LeviLamina in the update list (catalog launcherApi 2), launcher/Updates.h.
// g++ -std=c++17 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/launcher_standalone.cpp launcher/vendor/tinyxml2/tinyxml2.cpp -o /tmp/myclient-standalone && /tmp/myclient-standalone
#include "launcher/Updates.h"
#include <cassert>
#include <iostream>
using namespace MyClientLauncher;

namespace {
bool Rejected(const Json& doc)
{
    try { ParseCatalog(doc); } catch (const std::exception&) { return true; }
    return false;
}

Json File(const std::string& name)
{
    return {{"url", std::string(PublicDownloads) + "myclient-26.51.1-r60/" + name}, {"sha256", std::string(64, 'a')}, {"bytes", 1000u}};
}

Json Entry(int api, const std::string& loader, bool withLock)
{
    Json files = {{"mods/MyClientData/MyClientData.dll", File("dll")}, {"mods/MyClientData/manifest.json", File("manifest")}, {HudFile, File("hud")}};
    if (withLock) files["runtime.lock.json"] = File("lock");
    return {{"id", "26.51.1"}, {"available", true}, {"loader", loader}, {"revision", 60u}, {"launcherApi", api}, {"bridgeAbi", 2},
            {"platform", "gdk-release-x64"}, {"files", files}};
}

Json Doc(const Json& entry) { return {{"schemaVersion", 1}, {"sequence", 100u}, {"versions", Json::array({entry})}}; }
}

int main()
{
    // a LeviLamina release, as before
    {
        const auto catalog = ParseCatalog(Doc(Entry(1, "26.51.6", true)));
        assert(catalog.releases.size() == 1 && catalog.releases[0].available && !catalog.releases[0].target.standalone);
        assert(catalog.releases[0].target.loader == "26.51.6");
    }
    // a standalone release: no loader version, no runtime.lock.json
    {
        const auto catalog = ParseCatalog(Doc(Entry(2, "standalone", false)));
        assert(catalog.releases[0].available && catalog.releases[0].target.standalone && catalog.releases[0].revision == 60);
        assert(!catalog.releases[0].files.contains("runtime.lock.json"));
    }
    assert(Rejected(Doc(Entry(2, "26.51.6", false))));      // standalone must say so in "loader"
    assert(Rejected(Doc(Entry(2, "standalone", true))));    // and must not carry a loader lock
    assert(Rejected(Doc(Entry(1, "standalone", true))));    // a LeviLamina release needs a loader version
    assert(Rejected(Doc(Entry(1, "26.51.6", false))));      // ... and its lock
    // an API this launcher does not know: listed, not available, with the reason (what launchers before build 27 do with 2)
    {
        const auto catalog = ParseCatalog(Doc(Entry(3, "something", false)));
        assert(!catalog.releases[0].available && !catalog.releases[0].reason.empty() && !catalog.releases[0].target.standalone);
    }
    // a LeviLamina release and its standalone successor of the same revision are different releases: not accepted as "the same"
    {
        const auto before = ParseCatalog(Doc(Entry(1, "26.51.6", true)));
        Json next = Doc(Entry(2, "standalone", false));
        next["sequence"] = 101u;
        bool refused = false;
        try { CheckAdvance(before, ParseCatalog(next)); } catch (const std::exception&) { refused = true; }
        assert(refused);                                      // same revision, other files/loader: the revision must go up
        Json later = Entry(2, "standalone", false);
        later["revision"] = 61u;
        Json nextDoc = Doc(later);
        nextDoc["sequence"] = 101u;
        CheckAdvance(before, ParseCatalog(nextDoc));          // revision up: accepted
    }
    std::cout << "launcher standalone: ok\n";
    return 0;
}

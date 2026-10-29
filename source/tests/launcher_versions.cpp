#include "launcher/GameVersions.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

using namespace MyClientLauncher;

namespace {
void Write(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
    assert(output.good());
}

std::string Config(const std::string& name, const std::string& version)
{
    return "<Game><Identity Name=\"" + name + "\" Version=\"" + version +
           "\"/><ExecutableList><Executable Name=\"Minecraft.Windows.exe\"/></ExecutableList></Game>";
}
}

int main()
{
    // Version spellings: MicrosoftGame.config packs patch and build, the database pads the build.
    assert(VersionId("1.26.5101.0") == "26.51.1");
    assert(VersionId("1.26.51.01") == "26.51.1");
    assert(VersionId("Release 1.26.60.05") == "26.60.5");
    assert(VersionId("26.51.1.0") == "26.51.1");
    assert(VersionId("1.21.12004.0") == "1.21.120.4");
    assert(VersionId("Release 1.21.120.04") == "1.21.120.4");
    assert(VersionId("1.26.10004.0") == "26.100.4");
    assert(VersionId("") == "" && VersionId("Release") == "" && VersionId("1.26") == "" && VersionId("1.x.3.4") == "");

    assert(CompareVersionIds("26.60.5", "26.51.1") > 0);
    assert(CompareVersionIds("26.51.1", "26.51.1") == 0);
    assert(CompareVersionIds("1.21.132.1", "26.10.4") < 0);
    assert(CompareVersionIds("26.100.1", "26.60.5") > 0);

    // Database: release channel only, newest first, one entry per version.
    const Json db = Json::parse(R"({"file_version":1,
        "releaseVersions":[{"version":"Release 1.26.51.01"},{"version":"Release 1.26.60.05"},{"version":"Release 1.26.51.01"},
                           {"version":"junk"},{"urls":[]},{"version":"Release 1.21.132.01"}],
        "previewVersions":[{"version":"Preview 1.26.70.20"}]})");
    const auto published = ParseVersionDb(db);
    assert((published == std::vector<std::string>{"26.60.5", "26.51.1", "1.21.132.1"}));
    bool threw = false;
    try { ParseVersionDb(Json::parse(R"({"file_version":1})")); } catch (const std::exception&) { threw = true; }
    assert(threw);

    // Installed versions are read from MicrosoftGame.config; Preview and broken folders are skipped.
    const fs::path root = fs::temp_directory_path() / "myclient-versions-test";
    fs::remove_all(root);
    Write(root / "a" / "MicrosoftGame.config", Config("Microsoft.MinecraftUWP", "1.26.5101.0"));
    Write(root / "b" / "MicrosoftGame.config", Config("Microsoft.MinecraftWindowsBeta", "1.26.7020.0"));
    Write(root / "c" / "MicrosoftGame.config", "<Game>");
    assert(InstalledVersion(root / "a") == "26.51.1");
    assert(InstalledVersion(root / "b").empty());
    assert(InstalledVersion(root / "c").empty());
    assert(InstalledVersion(root / "missing").empty());

    // LeviLauncher keeps its versions under %APPDATA%\levilauncher.exe, or under base_root from its config.
    const fs::path appData = root / "appdata";
    assert(LeviLauncherVersionRoots(appData) == std::vector<fs::path>{appData / "levilauncher.exe" / "versions"});
    Write(appData / "levilauncher.exe" / "config.json", R"({"base_root":"D:\\Levi"})");
    const auto roots = LeviLauncherVersionRoots(appData);
    assert(roots.size() == 2 && roots[0] == fs::path("D:\\Levi") / "versions" && roots[1] == appData / "levilauncher.exe" / "versions");
    Write(appData / "levilauncher.exe" / "config.json", "not json");
    assert(LeviLauncherVersionRoots(appData).size() == 1);

    // Rows: every known version once, newest first, with what MyClient and this PC have.
    const auto rows = VersionRows(published, {{"26.51.1", true}, {"26.52.3", false}}, {"26.51.1", "26.40.5"});
    assert(rows.size() == 5);
    assert(rows[0].id == "26.60.5" && rows[0].published && !rows[0].inCatalog && !rows[0].installed);
    assert(rows[1].id == "26.52.3" && !rows[1].published && rows[1].inCatalog && !rows[1].supported && rows[1].catalogIndex == 1);
    assert(rows[2].id == "26.51.1" && rows[2].published && rows[2].supported && rows[2].installed && rows[2].catalogIndex == 0);
    assert(rows[3].id == "26.40.5" && !rows[3].published && rows[3].installed && !rows[3].inCatalog);
    assert(rows[4].id == "1.21.132.1");

    // Download links: only Minecraft packages on the Xbox CDN, HTTPS tried first, .cn mirrors last.
    const std::string cdn = "http://assets1.xboxlive.com/11/a/b/1.26.6005.0.c/Microsoft.MinecraftUWP_1.26.6005.0_x64__8wekyb3d8bbwe.msixvc";
    assert(GamePackageUrl(cdn));
    assert(GamePackageUrl("https://xvcf1.xboxlive.com/x/MICROSOFT.MINECRAFTUWP_1.21.12004.0_x64__8wekyb3d8bbwe.msixvc"));
    assert(GamePackageUrl("http://d1.xboxlive.cn/x/Microsoft.MinecraftUWP_1.26.6005.0_x64__8wekyb3d8bbwe.msixvc"));
    assert(!GamePackageUrl("http://evil.example/x/Microsoft.MinecraftUWP_1.26.6005.0_x64__8wekyb3d8bbwe.msixvc"));
    assert(!GamePackageUrl("http://xboxlive.com.evil.example/Microsoft.MinecraftUWP_1.msixvc"));
    assert(!GamePackageUrl("http://user@assets1.xboxlive.com/Microsoft.MinecraftUWP_1.msixvc"));
    assert(!GamePackageUrl("http://assets1.xboxlive.com/Microsoft.MinecraftUWP_1.msixvc?x=1"));
    assert(!GamePackageUrl("http://assets1.xboxlive.com/Other.Game_1.msixvc"));
    assert(!GamePackageUrl("http://assets1.xboxlive.com/Microsoft.MinecraftUWP_1.exe"));
    assert(!GamePackageUrl("ftp://assets1.xboxlive.com/Microsoft.MinecraftUWP_1.msixvc"));
    const Json linked = Json::parse(R"({"releaseVersions":[
        {"version":"Release 1.26.60.05","urls":["http://d1.xboxlive.cn/x/Microsoft.MinecraftUWP_1.26.6005.0_x64__8wekyb3d8bbwe.msixvc",
            "http://assets1.xboxlive.com/x/Microsoft.MinecraftUWP_1.26.6005.0_x64__8wekyb3d8bbwe.msixvc","http://evil.example/a.msixvc",3]},
        {"version":"Release 1.26.51.01","urls":["http://evil.example/Microsoft.MinecraftUWP_1.msixvc"]},
        {"version":"Release 1.26.52.03"}]})");
    const auto links = ParseVersionLinks(linked);
    assert(links.size() == 1 && links.at("26.60.5").size() == 2);
    const auto order = PackageDownloadOrder(links.at("26.60.5"));
    assert(order.size() == 4);
    assert(order[0] == "https://assets1.xboxlive.com/x/Microsoft.MinecraftUWP_1.26.6005.0_x64__8wekyb3d8bbwe.msixvc");
    assert(order[1] == "http://assets1.xboxlive.com/x/Microsoft.MinecraftUWP_1.26.6005.0_x64__8wekyb3d8bbwe.msixvc");
    assert(order[2].rfind("https://d1.xboxlive.cn/", 0) == 0 && order[3].rfind("http://d1.xboxlive.cn/", 0) == 0);
    assert(ParseVersionLinks(Json::parse("[]")).empty());
    const auto marked = VersionRows(published, {}, {}, {"26.60.5", "9.9.9"});
    assert(marked.size() == 3 && marked[0].downloadable && !marked[1].downloadable);
    // The real database, when the test is given its path.
    if (const char* real = std::getenv("MYCLIENT_VERSION_DB"))
    {
        const auto all = ParseVersionLinks(ReadJson(real));
        assert(!all.empty());
        for (const auto& [id, urls] : all) assert(!urls.empty() && !ParseVersionDb(ReadJson(real)).empty());
        std::cout << "real database: " << all.size() << " versions with links, newest " << ParseVersionDb(ReadJson(real)).front()
                  << (all.count(ParseVersionDb(ReadJson(real)).front()) ? " (has links)" : " (no links)") << "\n";
    }

    fs::remove_all(root);
    std::cout << "PASS: version ids from config and database spellings, ordering, database parsing, installed version reading, "
                 "LeviLauncher folders, version rows and package links.\n";
}

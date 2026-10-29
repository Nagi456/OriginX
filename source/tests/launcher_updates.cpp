#include "launcher/Updates.h"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace MyClientLauncher;

void Reject(const std::function<void()>& action) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}
void Write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path()); std::ofstream out(path, std::ios::binary); out << value; assert(out.good());
}
// A deterministic test double for BCrypt: transaction tests exercise content
// equality/rejection, not a second SHA256 implementation.
std::string Hash(const fs::path& path) {
    std::ifstream in(path, std::ios::binary); uint64_t value = 14695981039346656037ull; char c;
    while (in.get(c)) { value ^= static_cast<unsigned char>(c); value *= 1099511628211ull; }
    std::ostringstream out; out << std::hex << std::setw(64) << std::setfill('0') << value; return out.str();
}
Catalog Parse(const Json& value) { return ParseCatalog(Json::parse(value.dump())); }
int main() {
    const auto catalog = ParseCatalog(ReadJson("launcher/catalog.json"));
    assert(catalog.releases.size() == 2);
    // Only 26.51.1 is supported; 26.52.3 is listed as unavailable (with a reason).
    assert(catalog.releases[0].target.id == "26.51.1" && catalog.releases[0].available);
    assert(catalog.releases[1].target.id == "26.52.3" && !catalog.releases[1].available);
    assert(catalog.releases[1].target.runtimeGame == "1.26.52.03");
    assert(SupportedGameVersion("1.26.5203.0", catalog.releases[1].target));
    assert(!SupportedGameVersion("1.26.5101.0", catalog.releases[1].target));
    auto changed = catalog.document;
    changed["versions"][0]["bridgeAbi"] = 99;
    assert(!Parse(changed).releases[0].available);
    changed = catalog.document; changed["versions"][0]["id"] = "../escape"; Reject([&] { Parse(changed); });
    changed = catalog.document; changed["versions"][1]["id"] = "26.51.1"; Reject([&] { Parse(changed); });
    for (const auto& path : {"../escape.dll", "mods/MyClientData/other.dll", "runtime.lock.json/other", "mods/MyClientData/licenses/a/evil.dll"}) {
        changed = catalog.document;
        changed["versions"][0]["files"][path] = changed["versions"][0]["files"]["runtime.lock.json"];
        Reject([&] { Parse(changed); });
    }
    for (const auto& url : {"http://github.com/Nagi456/MyClient/releases/download/a/a", "https://github.com/other/repo/releases/download/a/a", "https://github.com/Nagi456/MyClient/releases/download/../a"}) {
        changed = catalog.document; changed["versions"][0]["files"]["runtime.lock.json"]["url"] = url;
        Reject([&] { Parse(changed); });
    }
    changed = catalog.document; changed["versions"][0]["files"]["runtime.lock.json"]["bytes"] = -1;
    Reject([&] { Parse(changed); });
    changed = catalog.document; changed["versions"][0]["files"]["runtime.lock.json"]["sha256"] = "bad";
    Reject([&] { Parse(changed); });
    changed = catalog.document; changed["sequence"] = catalog.sequence + 1; changed["versions"][0]["revision"] = catalog.releases[0].revision + 1;
    auto newer = Parse(changed); CheckAdvance(catalog, newer); Reject([&] { CheckAdvance(newer, catalog); });
    changed = catalog.document; changed["versions"][1]["reason"] = "different";
    Reject([&] { CheckAdvance(catalog, Parse(changed)); });
    changed = newer.document; changed["sequence"] = newer.sequence + 1; changed["versions"][0]["revision"] = catalog.releases[0].revision;
    Reject([&] { CheckAdvance(newer, Parse(changed)); });

    const auto root = fs::temp_directory_path() / ("myclient-updates-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    const auto source = root / "source";
    fs::create_directories(source / "mods/MyClientData");
    fs::copy_file("build/client/MyClientData.dll", source / "mods/MyClientData/MyClientData.dll");
    fs::copy_file("build/client/manifest.json", source / "mods/MyClientData/manifest.json");
    fs::copy_file("launcher/runtime.lock.json", source / "runtime.lock.json");
    Release release = catalog.releases[0];
    for (auto it = release.files.begin(); it != release.files.end();) {
        if (!fs::is_regular_file(source / it.key())) it = release.files.erase(it);
        else { (*it)["sha256"] = Hash(source / it.key()); ++it; }
    }
    int downloads = 0;
    const auto copy = [&](const std::string& url, const fs::path& destination, uint64_t) {
        ++downloads;
        for (const auto& file : release.files.items())
            if (file.value()["url"] == url) { fs::copy_file(source / file.key(), destination); return; }
        throw std::runtime_error("fixture URL missing");
    };
    const auto cache = root / "packages";
    const auto installed = AcquirePackage(cache, release, Hash, copy);
    assert(downloads == 3);
    assert(AcquirePackage(cache, release, Hash, copy) == installed && downloads == 3);
    auto next = release; ++next.revision;
    Reject([&] { AcquirePackage(cache, next, Hash, [](const auto&, const auto& path, uint64_t) { Write(path, "corrupt"); }); });
    assert(fs::is_directory(installed) && !fs::exists(cache / release.target.id / std::to_string(next.revision)));
    for (const auto& entry : fs::directory_iterator(cache)) assert(entry.path().filename().string().find(".download-") != 0);
    VerifyPackage(installed, release, Hash);
    const auto updated = AcquirePackage(cache, next, Hash, copy);
    assert(updated != installed && fs::is_directory(installed));
    Write(updated / "mods/MyClientData/extra.dll", "bad");
    Reject([&] { VerifyPackage(updated, next, Hash); });
    fs::remove(updated / "mods/MyClientData/extra.dll");
    fs::remove(updated / "mods/MyClientData/MyClientData.dll");
    fs::create_symlink(source / "mods/MyClientData/MyClientData.dll", updated / "mods/MyClientData/MyClientData.dll");
    Reject([&] { VerifyPackage(updated, next, Hash); });
    Reject([&] { AcquirePackage(cache, catalog.releases[1], Hash, copy); });

    // Bundled files rebuilt by hand: the catalog's (published) hashes no longer match, which the strict
    // path rejects; the local path takes the files as they are, in a cache folder of their own.
    Release stale = catalog.releases[0];
    for (auto it = stale.files.begin(); it != stale.files.end();)
        it = fs::is_regular_file(source / it.key()) ? std::next(it) : stale.files.erase(it);
    const auto fromSource = [&](const std::string& key) { return source / key; };
    const auto copyStale = [&](const std::string& url, const fs::path& destination, uint64_t) {
        for (const auto& file : stale.files.items())
            if (file.value()["url"] == url) { fs::copy_file(source / file.key(), destination); return; }
        throw std::runtime_error("fixture URL missing");
    };
    Reject([&] { AcquirePackage(root / "strict", stale, Hash, copyStale); });
    std::string folder;
    const Release onDisk = LocalRelease(stale, fromSource, Hash, folder);
    assert(folder.rfind("local-", 0) == 0 && folder.size() == 6 + 16);
    for (const auto& file : onDisk.files.items())
        assert(file.value()["sha256"] == Hash(source / file.key()) && file.value()["bytes"] == fs::file_size(source / file.key()));
    const auto copyLocal = [&](const std::string& url, const fs::path& destination, uint64_t) {
        for (const auto& file : onDisk.files.items())
            if (file.value()["url"] == url) { fs::copy_file(source / file.key(), destination); return; }
        throw std::runtime_error("fixture URL missing");
    };
    const auto localCache = root / "local-packages";   // the cache above holds a deliberate symlink
    const auto local = AcquirePackage(localCache, onDisk, Hash, copyLocal, folder);
    assert(local == localCache / onDisk.target.id / folder);
    assert(AcquirePackage(localCache, onDisk, Hash, copyLocal, folder) == local);   // reused
    {   // another rebuild: a different folder, the earlier one untouched
        std::ofstream more(source / "mods/MyClientData/MyClientData.dll", std::ios::binary | std::ios::app);
        more << "rebuilt";
    }
    std::string folder2;
    const Release rebuilt = LocalRelease(stale, fromSource, Hash, folder2);
    assert(folder2 != folder);
    const auto local2 = AcquirePackage(localCache, rebuilt, Hash, [&](const std::string& url, const fs::path& destination, uint64_t) {
        for (const auto& file : rebuilt.files.items())
            if (file.value()["url"] == url) { fs::copy_file(source / file.key(), destination); return; }
        throw std::runtime_error("fixture URL missing");
    }, folder2);
    assert(local2 != local && fs::is_directory(local));
    Reject([&] { std::string f; LocalRelease(catalog.releases[0], [&](const std::string& key) { return root / "missing" / key; }, Hash, f); });
    std::cout << "PASS: catalog versions/compatibility/URL/path/rollback guards, staged download failure, cache reuse, versioned updates, extra-file/link rejection, hand-rebuilt bundled files.\n";
}

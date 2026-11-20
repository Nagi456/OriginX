// g++ -std=c++17 -Wall -Wextra -Werror -I. tests/launcher_package_cache.cpp -o /tmp/pc && /tmp/pc
#include "launcher/PackageCache.h"
#include <cassert>
#include <iostream>

using namespace MyClientLauncher;
using namespace MyClientLauncher::PackageCache;

int main()
{
    // names
    assert(FileName("26.45.1") == "Microsoft.MinecraftUWP_26.45.1.msixvc");
    assert(PartialName("26.45.1") == "Microsoft.MinecraftUWP_26.45.1.msixvc.partial");
    assert(IdOf("Microsoft.MinecraftUWP_26.45.1.msixvc") == "26.45.1");
    assert(IdOf("Microsoft.MinecraftUWP_26.45.1.msixvc.partial").empty());
    assert(IdOf("Microsoft.MinecraftUWP_.msixvc").empty());
    assert(IdOf("Microsoft.MinecraftUWP_26..1.msixvc").empty());
    assert(IdOf("Microsoft.MinecraftUWP_../x.msixvc").empty());
    assert(IdOf("other.msixvc").empty());
    assert(IsPartial("Microsoft.MinecraftUWP_26.51.1.msixvc.partial"));
    assert(!IsPartial("Microsoft.MinecraftUWP_26.51.1.msixvc"));
    assert(!IsPartial("x.partial"));
    assert(ValidId("26.60.5") && !ValidId("") && !ValidId(".1") && !ValidId("1.") && !ValidId("1a"));

    // eviction: count limit, least recently used first
    const uint64_t G = 1024ull * 1024 * 1024;
    std::vector<Entry> e{{"26.45.1", 2 * G, 10}, {"26.51.1", 2 * G, 30}, {"26.60.5", 2 * G, 20}, {"26.62.1", 2 * G, 40}};
    auto out = ToEvict(e, {});
    assert(out.size() == 1 && out[0] == "26.45.1");
    // under both limits: nothing
    assert(ToEvict({e[0], e[1]}, {}).empty());
    // byte limit
    out = ToEvict({{"a1", 12 * G, 1}, {"a2", 12 * G, 2}}, {});
    assert(out.size() == 1 && out[0] == "a1");
    // keep is never evicted, the next oldest goes instead
    out = ToEvict(e, {"26.45.1"});
    assert(out.size() == 1 && out[0] == "26.60.5");
    // keep everything: stays over the limit, nothing chosen
    assert(ToEvict(e, {"26.45.1", "26.51.1", "26.60.5", "26.62.1"}).empty());
    // custom limits
    out = ToEvict(e, {}, 1, ~0ull);
    assert(out.size() == 3 && out[0] == "26.45.1" && out[1] == "26.60.5" && out[2] == "26.51.1");

    // corrupt-package errors
    assert(PackageFileDamaged("HRESULT: 0x80073CF0"));
    assert(PackageFileDamaged("hresult 0x8007000d"));
    assert(!PackageFileDamaged("HRESULT: 0x80073CFB"));
    assert(!PackageFileDamaged(""));
    assert(!PackageFileDamaged("HRESULT: 0x80073CF6"));   // registration failure: the file may be fine
    assert(!PackageFileDamaged("0x80073D02"));
    assert(PackageFileDamaged("0x80096010"));

    // switch-check.log
    auto c = ParseSwitchCheck("Installed: 1.26.5101.0 (None)\r\nNot from the Store: None\r\nFolder registration: C:\\Users\\u\\AppData\\Local\\OderSo\\Versions\\1.26.51\r\n");
    assert(c.installedVersion == "1.26.5101.0" && c.signatureKind == "None" && c.loose());
    assert(c.folderRegistration == "C:\\Users\\u\\AppData\\Local\\OderSo\\Versions\\1.26.51" && !c.alreadyNormal);
    c = ParseSwitchCheck("Installed: 1.26.6005.0 (Store)\nNormal launch is already 26.60.5\n");
    assert(c.signatureKind == "Store" && !c.loose() && c.alreadyNormal && c.folderRegistration.empty());
    c = ParseSwitchCheck("Minecraft is not installed.");
    assert(c.installedVersion.empty() && !c.loose());
    assert(ParseSwitchCheck("Installed: 1.26.4501.0 (Developer)").loose());
    // Decision A: IsDevelopmentMode alone also means hands off, whatever the signature kind.
    c = ParseSwitchCheck("Installed: 1.26.5101.0 (Store)\r\nDevelopment mode registration: True\r\n");
    assert(c.developmentMode && c.loose());
    assert(!ParseSwitchCheck("Installed: 1.26.6005.0 (Store)\nDevelopment mode registration: False\n").loose());

    std::cout << "launcher_package_cache: ok\n";
}

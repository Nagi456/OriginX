#include "../HomeBrandPack.h"
#include <cassert>
#include <iostream>
int main() {
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() / "originx_brand_scope_test";
    fs::remove_all(base);
    const auto local = base / "local";
    const auto legacy = base / "com.mojang/resource_packs/OriginX_Lite";
    assert(LitePack::Write(legacy, LitePack::NothingOn()));
    assert(LitePack::WriteFile(legacy / "ui/start_screen.json", LitePack::HomeBrandJson()));
    assert(LitePack::WriteFile(legacy / "keep.txt", "keep"));
    assert(HomeBrandPack::CleanLegacy({legacy}));
    assert(!fs::exists(legacy / "ui/start_screen.json"));
    assert(fs::exists(legacy / "keep.txt"));
    assert(HomeBrandPack::CleanLegacy({legacy}));
    const auto other = base / "other";
    assert(LitePack::WriteFile(other / "manifest.json", HomeBrandPack::Manifest()));
    assert(LitePack::WriteFile(other / "ui/start_screen.json", "custom"));
    assert(HomeBrandPack::CleanLegacy({other}));
    assert(fs::exists(other / "ui/start_screen.json"));
    assert(LitePack::WriteFile(other / "manifest.json", "broken"));
    assert(!HomeBrandPack::CleanLegacy({other}));
    assert(fs::exists(other / "ui/start_screen.json"));
    assert(HomeBrandPack::Write(local, true));
    const auto folder = HomeBrandPack::Folder(local);
    assert(folder.parent_path() == LitePack::GeneratedPackRoot(local));
    assert(fs::exists(folder / "ui/start_screen.json"));
    assert(!fs::exists(legacy / "ui/start_screen.json"));
    TexturePacks::Pack ref;
    assert(TexturePacks::ParseManifest(TexturePacks::ReadFile(folder / "manifest.json"), ref));
    assert(ref.uuid == HomeBrandPack::Uuid && ref.uuid != LitePack::kPackUuid);
    std::vector<TexturePacks::Entry> users{{"user", "{\"pack_id\":\"user\",\"subpack\":\"six\"}"}};
    const auto stack = HomeBrandPack::WithEntry(users, true);
    assert(stack.size() == 2 && stack[0].uuid == HomeBrandPack::Uuid && stack[1].json == users[0].json);
    assert(HomeBrandPack::WithEntry(stack, true).size() == 2);
    assert(HomeBrandPack::WithEntry(stack, false)[0].json == users[0].json);
    assert(HomeBrandPack::Write(local, false));
    assert(!fs::exists(folder / "ui/start_screen.json"));
    assert(!fs::exists(folder / "manifest.json"));
    // Removal failures must keep the existing retry pending.
    assert(LitePack::WriteFile(legacy / "ui/start_screen.json/busy", "busy"));
    assert(!HomeBrandPack::CleanLegacy({legacy}));
    assert(!HomeBrandPack::Write({}, true));
    fs::remove_all(base);
    std::cout << "home brand isolation ok\n";
}

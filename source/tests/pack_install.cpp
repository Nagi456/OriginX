#include "../PackInstall.h"
#include <cassert>
#include <iostream>
using namespace PackInstall;
static void Write(const fs::path& p, const std::string& t) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << t; }
static std::string Manifest(const std::string& name, const std::string& uuid, const std::string& type) {
    return "{\"format_version\":2,\"header\":{\"name\":\"" + name + "\",\"uuid\":\"" + uuid + "\",\"version\":[1,0,0]},"
           "\"modules\":[{\"type\":\"" + type + "\",\"uuid\":\"11111111-2222-3333-4444-555555555555\",\"version\":[1,0,0]}]}";
}
int main() {
    const std::string u1 = "aaaaaaaa-1111-2222-3333-444444444444", u2 = "bbbbbbbb-1111-2222-3333-444444444444";
    assert(Inspect(Manifest("Pack", u1, "resources")).kind == Kind::Resource);
    assert(Inspect(Manifest("Pack", u1, "data")).kind == Kind::Behavior);
    assert(Inspect(Manifest("Pack", u1, "script")).kind == Kind::Behavior);
    assert(Inspect(Manifest("Pack", u1, "skin_pack")).kind == Kind::Skin);
    assert(Inspect(Manifest("Pack", u1, "world_template")).kind == Kind::World);
    assert(!Inspect("{}").valid && !Inspect(Manifest("Pack", "not-a-uuid", "resources")).valid);
    assert(FolderName("My Cool/Pack: ../x", u1) == "My_Cool_Pack_x_aaaaaaaa" || FolderName("My Cool/Pack: ../x", u1).find("..") == std::string::npos);
    assert(FolderName("日本語", u1) == "pack_aaaaaaaa");

    const fs::path base = fs::temp_directory_path() / "packinstall_test";
    fs::remove_all(base);
    const fs::path game = base / "com.mojang", inbox = base / "inbox", prefab = base / "prefab";
    fs::create_directories(game);
    // prefab "archive" contents: one resource pack and one behavior pack side by side (like a .mcaddon)
    Write(prefab / "RP" / "manifest.json", Manifest("Cool RP", u1, "resources")); Write(prefab / "RP" / "textures" / "a.png", "PNG");
    Write(prefab / "BP" / "manifest.json", Manifest("Cool BP", u2, "data"));
    // inbox: an addon "archive" (fake file), a loose folder pack, a junk file, a broken archive
    Write(inbox / "addon.mcaddon", "x"); Write(inbox / "broken.mcpack", "BAD"); Write(inbox / "notes.txt", "hi");
    Write(inbox / "Loose" / "manifest.json", Manifest("Loose", "cccccccc-1111-2222-3333-444444444444", "resources"));
    Extractor fake = [&](const fs::path& archive, const fs::path& dest) {
        if (archive.filename() == "broken.mcpack") return false;
        return CopyTree(prefab, dest);
    };
    Summary s = InstallInbox(inbox, game, fake);
    assert(s.resource == 2 && s.behavior == 1 && s.failed == 1 && s.skipped == 1);
    assert(fs::exists(game / "resource_packs" / ("Cool_RP_aaaaaaaa") / "textures" / "a.png"));
    assert(fs::exists(game / "behavior_packs" / ("Cool_BP_bbbbbbbb") / "manifest.json"));
    assert(fs::exists(inbox / "done" / "addon.mcaddon") && fs::exists(inbox / "done" / "Loose"));
    assert(fs::exists(inbox / "broken.mcpack"));            // failures stay for a retry
    assert(!fs::exists(inbox / ".work"));
    // installing again replaces instead of failing
    Write(inbox / "addon2.mcaddon", "x");
    s = InstallInbox(inbox, game, fake);
    assert(s.resource == 1 && s.behavior == 1);
    fs::remove_all(base);
    std::cout << "PASS: pack install\n";
    return 0;
}

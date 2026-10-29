#include "TexturePacks.h"
#include <cassert>
#include <iostream>

using namespace TexturePacks;

static void Write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << text;
}

int main()
{
    const std::string manifest = R"({
      "format_version": 2,
      "header": { "name": "Fancy \u30c6\u30af\u30b9\u30c1\u30e3", "description": "x",
                  "uuid": "ABCDEF01-2345-6789-abcd-ef0123456789", "version": [1, 2, 3], "min_engine_version": [1, 21, 0] },
      "modules": [ { "type": "resources", "uuid": "11111111-2222-3333-4444-555555555555", "version": [1, 0, 0] } ]
    })";
    Pack pack;
    assert(ParseManifest(manifest, pack));
    assert(pack.uuid == "abcdef01-2345-6789-abcd-ef0123456789");             // header uuid, lower-cased (not the module's)
    assert(pack.version[0] == 1 && pack.version[1] == 2 && pack.version[2] == 3);
    assert(pack.name == "Fancy \xE3\x83\x86\xE3\x82\xAF\xE3\x82\xB9\xE3\x83\x81\xE3\x83\xA3");   // \u escapes -> UTF-8
    // refused manifests
    Pack other;
    assert(!ParseManifest("not json", other));
    assert(!ParseManifest(R"({"header":{"name":"x","uuid":"abcdef01-2345-6789-abcd-ef0123456789","version":[1,0,0]},"modules":[{"type":"data"}]})", other));  // behaviour pack
    assert(!ParseManifest(R"({"header":{"name":"x","uuid":"short","version":[1,0,0]},"modules":[{"type":"resources"}]})", other));          // bad uuid
    assert(!ParseManifest(R"({"header":{"name":"x","version":[1,0,0]},"modules":[{"type":"resources"}]})", other));                         // no uuid
    // active list
    const auto active = ParseActive(R"([{"pack_id":"ABCDEF01-2345-6789-abcd-ef0123456789","version":[1,2,3]},{"pack_id":"nope"}])");
    assert(active.size() == 1 && active[0] == "abcdef01-2345-6789-abcd-ef0123456789");
    assert(ParseActive("[]").empty() && ParseActive("").empty());
    // written list is read back, and "none" is an empty list
    assert(BuildActive(nullptr) == "[]\n");
    const auto back = ParseActive(BuildActive(&pack));
    assert(back.size() == 1 && back[0] == pack.uuid);
    assert(BuildActive(&pack).find("[1, 2, 3]") != std::string::npos);

    // ---- disk: two data folders, packs in both, newest active file wins ----
    const fs::path base = fs::temp_directory_path() / "texpacks_test";
    fs::remove_all(base);
    const fs::path appData = base / "Roaming", local = base / "Local";
    const fs::path shared = appData / "Minecraft Bedrock" / "Users" / "Shared" / "games" / "com.mojang";
    const fs::path user = appData / "Minecraft Bedrock" / "Users" / "123456" / "games" / "com.mojang";
    Write(shared / "resource_packs" / "Alpha" / "manifest.json", manifest);
    Write(user / "resource_packs" / "pack.beta_folder" / "manifest.json",
          R"({"header":{"name":"pack.name","uuid":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee","version":[2,0,0]},"modules":[{"type":"resources"}]})");
    Write(user / "resource_packs" / "BehaviourOnly" / "manifest.json",
          R"({"header":{"name":"b","uuid":"bbbbbbbb-bbbb-cccc-dddd-eeeeeeeeeeee","version":[1,0,0]},"modules":[{"type":"data"}]})");
    Write(user / "resource_packs" / "Broken" / "manifest.json", "{{{");
    Write(user / "minecraftpe" / "global_resource_packs.json", BuildActive(&pack));
    const auto roots = CandidateRoots(appData, local);
    assert(roots.size() == 3);   // Shared, 123456, and the old UWP location
    State state = Scan(roots);
    assert(state.found && state.root == user);                                  // the one holding the active list
    assert(state.packs.size() == 2);                                            // behaviour-only and broken ones skipped
    assert(state.packs[0].name == "Fancy \xE3\x83\x86\xE3\x82\xAF\xE3\x82\xB9\xE3\x83\x81\xE3\x83\xA3");
    assert(state.packs[1].name == "pack.beta_folder");                          // "pack.name" is a key -> folder name
    assert(IsActive(state, state.packs[0]) && !IsActive(state, state.packs[1]));
    // the generated pack goes next to the active list and into Shared, where the GDK game finds packs
    const auto packRoots = PackRoots(state);
    assert(packRoots.size() == 2 && packRoots[0] == user && packRoots[1] == shared);

    // switch: the file changes, the original is backed up exactly once
    std::string error;
    assert(WriteActive(state, &state.packs[1], error));
    assert(ParseActive(ReadFile(user / "minecraftpe" / "global_resource_packs.json")) == std::vector<std::string>{"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"});
    const std::string backup = ReadFile(user / "minecraftpe" / "global_resource_packs.json.myclient-backup");
    assert(ParseActive(backup).size() == 1 && ParseActive(backup)[0] == pack.uuid);
    assert(WriteActive(state, nullptr, error));                                 // back to the default textures
    assert(ParseActive(ReadFile(user / "minecraftpe" / "global_resource_packs.json")).empty());
    assert(ReadFile(user / "minecraftpe" / "global_resource_packs.json.myclient-backup") == backup);   // backup kept as the original
    // a stack of several packs (set up in the game) survives a rewrite: order, versions and extra fields
    const std::string six = R"([
  { "pack_id": "11111111-0000-0000-0000-000000000001", "subpack": "low", "version": [1, 0, 0] },
  { "pack_id": "11111111-0000-0000-0000-000000000002", "version": [2, 1, 0] },
  { "pack_id": "11111111-0000-0000-0000-000000000003", "version": [1, 0, 0] },
  { "pack_id": "11111111-0000-0000-0000-000000000004", "version": [1, 0, 0] },
  { "pack_id": "11111111-0000-0000-0000-000000000005", "version": [1, 0, 0] },
  { "pack_id": "11111111-0000-0000-0000-000000000006", "version": [3, 0, 0] }
])";
    const auto entries = ParseEntries(six);
    assert(entries.size() == 6 && entries[0].uuid == "11111111-0000-0000-0000-000000000001" && entries[5].uuid == "11111111-0000-0000-0000-000000000006");
    assert(entries[0].json.find("\"subpack\": \"low\"") != std::string::npos);
    assert(ParseEntries(R"([{"pack_id":"x"},{"name":"{not a pack}","pack_id":"11111111-0000-0000-0000-000000000009"}])").size() == 1);
    Pack lite; lite.uuid = "99999999-0000-0000-0000-000000000000";
    const auto withLite = ParseEntries(BuildStack(entries, &lite));
    assert(withLite.size() == 7 && withLite[0].uuid == lite.uuid);
    for (size_t i = 0; i < 6; ++i) assert(withLite[i + 1].uuid == entries[i].uuid && withLite[i + 1].json == entries[i].json);
    assert(ParseEntries(BuildStack(withLite, &lite)).size() == 7);          // Lite is never listed twice
    assert(WriteStack(state, entries, error, &lite));
    assert(ParseActive(ReadFile(user / "minecraftpe" / "global_resource_packs.json")).size() == 7);
    assert(state.stack.size() == 7 && state.active[0] == lite.uuid && state.active[6] == entries[5].uuid);
    State rescanned = Scan(roots);
    assert(rescanned.stack.size() == 7 && rescanned.stack[1].json == entries[0].json);
    // The game's own options.txt decides the folder, not a list MyClient wrote more recently somewhere else.
    {
        const fs::path other = appData / "Minecraft Bedrock" / "Users" / "999" / "games" / "com.mojang";
        Write(user / "minecraftpe" / "options.txt", "game_language:ja_JP\n");
        Write(other / "minecraftpe" / "options.txt", "game_language:en_US\n");
        Write(shared / "minecraftpe" / "global_resource_packs.json", "[]");
        fs::last_write_time(other / "minecraftpe" / "options.txt", fs::last_write_time(user / "minecraftpe" / "options.txt") - std::chrono::hours(1));
        fs::last_write_time(shared / "minecraftpe" / "global_resource_packs.json", fs::last_write_time(user / "minecraftpe" / "options.txt") + std::chrono::hours(1));
        const State byOptions = Scan(CandidateRoots(appData, local));
        assert(byOptions.root == user && byOptions.stack.size() == 7);
        fs::remove_all(appData / "Minecraft Bedrock" / "Users" / "999");
        fs::remove(shared / "minecraftpe" / "global_resource_packs.json");
    }
    // subpacks: listed from the manifest, chosen per active-list entry
    Pack withSubs;
    assert(ParseManifest(R"({"header":{"name":"Sub Pack","uuid":"22222222-0000-0000-0000-000000000001","version":[1,0,0]},
      "modules":[{"type":"resources","uuid":"22222222-0000-0000-0000-000000000002","version":[1,0,0]}],
      "subpacks":[{"folder_name":"x16","name":"16x \u2605","memory_tier":1},{"folder_name":"x32","name":"32x"},{"name":"no folder"}]})", withSubs));
    assert(withSubs.name == "Sub Pack" && withSubs.subpacks.size() == 2);
    assert(withSubs.subpacks[0].folder == "x16" && withSubs.subpacks[0].name == "16x \xE2\x98\x85" && withSubs.subpacks[1].folder == "x32");
    assert(pack.subpacks.empty());
    Entry plain = EntryFor(withSubs);
    assert(SubpackOf(plain).empty());
    Entry chosen = WithSubpack(plain, "x32");
    assert(SubpackOf(chosen) == "x32" && ParseEntries("[" + chosen.json + "]").size() == 1);
    Entry again = WithSubpack(chosen, "x16$");
    assert(SubpackOf(again) == "x16$" && again.json.find("x32") == std::string::npos);
    assert(SubpackOf(entries[0]) == "low" && SubpackOf(WithSubpack(entries[0], "high")) == "high");
    assert(WithSubpack(entries[0], "high").json.find("\"version\": [1, 0, 0]") != std::string::npos);   // other fields kept
    // nothing found
    State none = Scan({base / "does-not-exist"});
    assert(!none.found && !none.note.empty());
    assert(!WriteActive(none, nullptr, error));
    fs::remove_all(base);
    std::cout << "PASS: pack manifests, active list parse/write, stacks of packs, data-folder choice, backup, refusal of odd files\n";
}

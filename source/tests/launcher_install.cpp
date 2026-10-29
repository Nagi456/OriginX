#include "launcher/Install.h"
#include "launcher/PidOption.h"
#include <cassert>
#include <functional>
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

void Pe(const fs::path& path, bool x64 = true, const std::vector<std::string>& imports = {})
{
    std::string bytes(1024, '\0');
    auto u16 = [&](size_t i, uint16_t v) { bytes[i] = char(v); bytes[i+1] = char(v >> 8); };
    auto u32 = [&](size_t i, uint32_t v) { for (int n=0;n<4;++n) bytes[i+n] = char(v >> (8*n)); };
    bytes[0] = 'M'; bytes[1] = 'Z'; u32(60, 64);
    bytes[64] = 'P'; bytes[65] = 'E';
    u16(68, x64 ? 0x8664 : 0x14c); u16(70, 1); u16(84, 240);
    u16(88, 0x20b); u32(148, 512); u32(196, 16);
    u32(340, 4096); u32(344, 512); u32(348, 512);
    if (!imports.empty())
    {
        u32(208, 4096); u32(212, uint32_t(20*(imports.size()+1)));
        size_t position = 640;
        for (size_t i=0;i<imports.size();++i)
        {
            u32(512+20*i+12, uint32_t(4096+position-512));
            bytes.replace(position, imports[i].size(), imports[i]);
            position += imports[i].size()+1;
        }
    }
    Write(path, bytes);
}

void GameConfig(const fs::path& directory, const std::string& version = "1.26.5101.0",
                const std::string& name = "Microsoft.MinecraftWindows")
{
    Write(directory / "MicrosoftGame.config", "<?xml version=\"1.0\"?><Game><Identity Name=\"" + name +
          "\" Version=\"" + version + "\"/><ExecutableList><Executable Name=\"Minecraft.Windows.exe\"/>"
          "</ExecutableList></Game>");
}

void Reject(const std::function<void()>& action)
{
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}

std::string Read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

// Independent CRT argument decoder for an always-quoted single argument.
std::wstring DecodeArgument(const std::wstring& command)
{
    std::wstring result;
    bool quoted = false;
    for (size_t i = 0; i < command.size();)
    {
        size_t count = 0;
        while (i < command.size() && command[i] == L'\\') { ++count; ++i; }
        if (i < command.size() && command[i] == L'"')
        {
            result.append(count / 2, L'\\');
            if (count % 2) result += L'"';
            else quoted = !quoted;
            ++i;
        }
        else
        {
            result.append(count, L'\\');
            if (i < command.size())
            {
                assert(quoted || (command[i] != L' ' && command[i] != L'\t'));
                result += command[i++];
            }
        }
    }
    assert(!quoted);
    return result;
}
}

int main()
{
    const auto root = fs::temp_directory_path() /
        ("myclient-launcher-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(root));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ignored; fs::remove_all(root, ignored); } } cleanup{root};
    const auto directory = root / fs::u8path(u8"公式ゲーム保存先/Content");
    const auto game = directory / "Minecraft.Windows.exe";
    const auto source = root / "distribution/mods/MyClientData";
    Pe(game); GameConfig(directory);
    const auto instance = ResolveGame(game);
    assert(instance.name == "Microsoft.MinecraftWindows" && instance.directory == directory);
    assert(ImportedDlls(game).empty());
    GameConfig(directory, "1.26.5201.0");
    Reject([&] { ResolveGame(game); });
    GameConfig(directory, "1.26.5101.0", "Microsoft.MinecraftWindowsBeta");
    Reject([&] { ResolveGame(game); });
    GameConfig(directory);
    Pe(game, false);
    Reject([&] { ResolveGame(game); });
    Pe(game, true, {"LeviLauncher.dll"});
    Reject([&] { ResolveGame(game); });
    Pe(game, true, {"KERNEL32.dll", "PreLoader.dll"});
    assert(ImportedDlls(game) == std::set<std::string>({"kernel32.dll", "preloader.dll"}));
    Reject([&] { PrepareManagedGame(game, root / "modified-copy"); });
    Pe(game);
    Write(directory / "MicrosoftGame.config", "<Game><Identity");
    Reject([&] { ResolveGame(game); });
    GameConfig(directory);
    Write(directory / "resources.pri", "game resources");
    Write(directory / "mods/OtherMod/manifest.json", "unrelated source mod");
    const auto originalBytes = Read(game);
    const auto managed = root / fs::u8path(u8"自作ソフトの保存先/game/26.51.1");
    const auto copied = PrepareManagedGame(game, managed);
    assert(copied.directory == managed);
    assert(Read(game) == originalBytes);
    assert(Read(managed / "Minecraft.Windows.exe") == originalBytes);
    assert(Read(managed / "MyClient-original.exe") == originalBytes);
    assert(Read(managed / "resources.pri") == "game resources");
    assert(!fs::exists(managed / "mods/OtherMod"));
    assert(PrepareManagedGame({}, managed).directory == managed);
    Reject([&] { PrepareManagedGame(game, directory / "nested-copy"); });
    fs::create_symlink(game, directory / "unsafe-link");
    Reject([&] { PrepareManagedGame(game, root / "linked-source-copy"); });
    assert(!fs::exists(root / "linked-source-copy"));
    fs::remove(directory / "unsafe-link");
    // A LeviLauncher version: its loader is in the import table, the untouched exe is kept as .orig.
    const auto levi = root / "levilauncher.exe/versions/26.51.1";
    Pe(levi / "Minecraft.Windows.exe", true, {"LeviLauncher.dll"}); GameConfig(levi);
    Write(levi / "Minecraft.Windows.exe.orig", originalBytes);
    Write(levi / "LeviLauncher.dll", "loader");
    assert(OriginalExecutable(levi / "Minecraft.Windows.exe") == levi / "Minecraft.Windows.exe.orig");
    assert(OriginalExecutable(game) == game);
    assert(ResolveGame(levi / "Minecraft.Windows.exe").directory == levi);
    const auto fromLevi = root / "managed-from-levi";
    PrepareManagedGame(levi / "Minecraft.Windows.exe", fromLevi);
    assert(Read(fromLevi / "Minecraft.Windows.exe") == originalBytes && Read(fromLevi / "MyClient-original.exe") == originalBytes);
    assert(!fs::exists(fromLevi / "Minecraft.Windows.exe.orig") && !fs::exists(fromLevi / "LeviLauncher.dll"));
    fs::remove(levi / "Minecraft.Windows.exe.orig");
    Reject([&] { ResolveGame(levi / "Minecraft.Windows.exe"); });
    Write(root / "unowned/Minecraft.Windows.exe", originalBytes);
    Reject([&] { PrepareManagedGame(game, root / "unowned"); });
    auto malformed = originalBytes;
    malformed[60] = char(0xff); malformed[61] = char(0xff); malformed[62] = char(0xff); malformed[63] = char(0x7f);
    Write(root / "bad.exe", malformed);
    Reject([&] { ImportedDlls(root / "bad.exe"); });
    Pe(root / "bad.exe", true, {"PreLoader.dll"});
    malformed = Read(root / "bad.exe");
    malformed.resize(530);
    Write(root / "bad.exe", malformed);
    Reject([&] { ImportedDlls(root / "bad.exe"); });
    for (const std::string relative : {"../outside", "/absolute", "C:/drive", "folder/../escape", "a\\..\\b"})
        Reject([&] { SafeChild(managed, relative); });
    assert(SafeChild(managed, "mods/LeviLamina/LeviLamina.dll") == managed / "mods/LeviLamina/LeviLamina.dll");
    const Json mod{{"name", "MyClientData"}, {"entry", "MyClientData.dll"}, {"type", "native"}};
    Write(source / "manifest.json", mod.dump());
    Pe(source / "MyClientData.dll");
    Write(source / "licenses/author/LICENSE", "license");
    Reject([&] { InstallMod(source, directory); }); // Missing loader makes no target.
    assert(!fs::exists(directory / "mods/MyClientData"));
    const auto loaderDir = directory / "mods/LeviLamina";
    Json loader{{"name", "LeviLamina"}, {"version", "26.51.6"}, {"entry", "LeviLamina.dll"}, {"type", "preload-native"}};
    Write(loaderDir / "manifest.json", loader.dump()); Pe(loaderDir / "LeviLamina.dll");
    const auto unrelated = directory / "mods/OtherMod/settings.json";
    Write(unrelated, "untouched");
    assert(InstallMod(source, directory).empty());
    const auto target = directory / "mods/MyClientData";
    assert(Read(target / "MyClientData.dll") == Read(source / "MyClientData.dll"));
    assert(Read(target / "licenses/author/LICENSE") == "license");
    Write(target / "settings.json", "preserve user settings");
    fs::rename(target / "manifest.json", target / "manifest.json.close");
    Write(source / "MyClientData.dll", Read(source / "MyClientData.dll") + "new revision");
    const auto backup = InstallMod(source, directory);
    assert(!backup.empty() && backup.parent_path() == directory / "MyClient-backups");
    assert(Read(backup / "settings.json") == "preserve user settings");
    assert(fs::exists(backup / "manifest.json.close"));
    assert(!fs::exists(target / "manifest.json.close"));
    assert(Read(target / "MyClientData.dll") == Read(source / "MyClientData.dll"));
    assert(Read(target / "settings.json") == "preserve user settings");
    assert(Read(unrelated) == "untouched");
    const auto installed = Read(target / "MyClientData.dll");
    // A genuine filesystem copy failure must leave the installed mod intact
    // and clean its staging directory, even after part of the copy succeeded.
    Write(target / "licenses/conflict", "old file");
    Write(source / "licenses/conflict/new-license", "new directory");
    Reject([&] { InstallMod(source, directory); });
    assert(Read(target / "MyClientData.dll") == installed);
    assert(Read(target / "licenses/conflict") == "old file");
    assert(Read(target / "settings.json") == "preserve user settings");
    fs::remove_all(source / "licenses/conflict");
    loader["version"] = "26.52.1";
    Write(loaderDir / "manifest.json", loader.dump());
    Reject([&] { InstallMod(source, directory); });
    assert(Read(target / "MyClientData.dll") == installed);
    loader["version"] = "26.51.6";
    Write(loaderDir / "manifest.json", loader.dump());
    fs::rename(loaderDir / "manifest.json", loaderDir / "manifest.json.close");
    Reject([&] { InstallMod(source, directory); });
    fs::rename(loaderDir / "manifest.json.close", loaderDir / "manifest.json");
    Write(source / "MyClientData.dll", "incomplete DLL");
    Reject([&] { InstallMod(source, directory); });
    assert(Read(target / "MyClientData.dll") == installed);
    Pe(source / "MyClientData.dll");
    Write(source / "manifest.json", "{invalid JSON");
    Reject([&] { InstallMod(source, directory); });
    assert(Read(target / "MyClientData.dll") == installed);
    Write(source / "manifest.json", mod.dump());
    fs::create_symlink(unrelated, source / "external-settings");
    Reject([&] { InstallMod(source, directory); });
    assert(Read(target / "MyClientData.dll") == installed);
    fs::remove(source / "external-settings");
    // Backup root itself must not be a link.
    fs::remove_all(directory / "MyClient-backups");
    fs::create_directory_symlink(root, directory / "MyClient-backups");
    Reject([&] { InstallMod(source, directory); });
    assert(Read(target / "MyClientData.dll") == installed);
    for (const std::wstring value : {L"", L"normal", L"自作 client 26.51", L"C:\\space dir\\",
                                     L"embedded\"quote", L"\\\"", L"a\\\\\"b", L"&%$`shell text", L"\twhite space"})
        assert(DecodeArgument(QuoteArg(value)) == value);
    uint32_t pid = 99;
    assert(ParsePidOption(L"--minecraft-pid=123", pid) && pid == 123);
    assert(ParsePidOption(L"--minecraft-pid=4294967295", pid) && pid == UINT32_MAX);
    for (const std::wstring value : {L"", L"--minecraft-pid=", L"--minecraft-pid=0", L"--minecraft-pid=-1",
                                     L"--minecraft-pid=+1", L"--minecraft-pid=4294967296", L"--minecraft-pid=1extra",
                                     L"--minecraft-pid= 123", L"--minecraft-pid=１２３", L"--wrong-option=123"})
    {
        pid = 99;
        assert(!ParsePidOption(value, pid) && pid == 0);
    }
    for (const auto& entry : fs::directory_iterator(directory))
        assert(entry.path().filename().u8string().find(".MyClient-install-") != 0);
    // Synthetic alternate target exercises selection plumbing only; this is
    // not a claim of game compatibility for an unreleased adapter.
    Target alternate{"26.52.3", "26.52.1", "1.26.52.03", {"1.26.5203.0", "1.26.52.03"}};
    const auto alternateSource = root / "alternate-source";
    Pe(alternateSource / "Minecraft.Windows.exe"); GameConfig(alternateSource, "1.26.5203.0");
    Reject([&] { ResolveGame(alternateSource / "Minecraft.Windows.exe"); });
    assert(ResolveGame(alternateSource / "Minecraft.Windows.exe", alternate).version == "1.26.5203.0");
    const auto alternateGame = root / "managed-26.52.3";
    PrepareManagedGame(alternateSource / "Minecraft.Windows.exe", alternateGame, alternate);
    Reject([&] { PrepareManagedGame({}, alternateGame); });
    assert(PrepareManagedGame({}, alternateGame, alternate).directory == alternateGame);
    loader["version"] = alternate.loader;
    Write(alternateGame / "mods/LeviLamina/manifest.json", loader.dump());
    Pe(alternateGame / "mods/LeviLamina/LeviLamina.dll");
    InstallMod(source, alternateGame, alternate);
    RequireMod(alternateGame / "mods/MyClientData", alternate);
    // A manifest that depends on LeviLamina is refused by the loader at runtime, so refuse it here too.
    auto dependentMod = mod;
    dependentMod["dependencies"] = Json::array({{{"name", "LeviLamina"}, {"version", "=" + alternate.loader}}});
    Write(source / "manifest.json", dependentMod.dump());
    Reject([&] { RequireMod(source, alternate); });
    std::cout << "PASS: GDK XML/version/x64/PE guards, managed game copy/source preservation, transactional install, backup/settings preservation, disabled mod enable, invalid input isolation, Windows argv quoting and HUD PID parsing.\n";
}

#pragma once
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <nlohmann/json.hpp>
#include "vendor/tinyxml2/tinyxml2.h"
#include "PeImports.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>

namespace MyClientLauncher {
namespace fs = std::filesystem;
using Json = nlohmann::json;

inline Json ReadJson(const fs::path& path)
{
    if (!fs::is_regular_file(path) || fs::file_size(path) > 1024 * 1024)
        throw std::runtime_error("JSONファイルがないか、大きすぎます: " + path.u8string());
    std::ifstream input(path, std::ios::binary);
    auto result = Json::parse(input);
    if (!result.is_object()) throw std::runtime_error("JSONの形式が不正です: " + path.u8string());
    return result;
}

inline std::string Lower(std::string value)
{
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}

// CreateProcessW receives an explicit executable path. Quote each argv item
// using the Windows CRT rules; neither cmd.exe nor PowerShell is involved.
inline std::wstring QuoteArg(const std::wstring& arg)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg)
    {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

inline void RequireX64Pe(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        std::string reason = "原因を特定できません";
#ifdef _WIN32
        HANDLE probe = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (probe != INVALID_HANDLE_VALUE) { CloseHandle(probe); reason = "開けましたが別の理由で失敗しました"; }
        else
        {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
                reason = "ファイルまたはフォルダーが存在しません。ゲームが移動・アンインストールされていないか確認してください (Windowsエラー " + std::to_string(code) + ")";
            else if (code == ERROR_ACCESS_DENIED)
                reason = "Windowsが読み取りを拒否しました (エラー5)。GDK版本体は保護されているため、コピー方式ではなく起動中のゲームへ注入する方式を使います";
            else if (code == ERROR_SHARING_VIOLATION)
                reason = "他のプログラムが使用中です (エラー32)。Minecraftを終了してください";
            else
                reason = "Windowsエラー " + std::to_string(code);
        }
#endif
        throw std::runtime_error("本体を開けません: " + reason + "\n" + path.u8string());
    }
    unsigned char dos[64]{};
    input.read(reinterpret_cast<char*>(dos), sizeof dos);
    const auto got = static_cast<long long>(input.gcount());
    if (got < 64 || dos[0] != 'M' || dos[1] != 'Z')
    {
        char head[32]{};
        std::snprintf(head, sizeof head, "%02X %02X %02X %02X", dos[0], dos[1], dos[2], dos[3]);
        throw std::runtime_error("Windows実行ファイルとして読み取れません（読み取り " + std::to_string(got) +
                                 "バイト、先頭 " + head + "）: " + path.u8string());
    }
    const uint32_t offset = uint32_t(dos[60]) | uint32_t(dos[61]) << 8 |
                            uint32_t(dos[62]) << 16 | uint32_t(dos[63]) << 24;
    if (offset < 64 || offset > 64 * 1024 * 1024)
        throw std::runtime_error("PEヘッダーが不正です: " + path.u8string());
    input.seekg(offset);
    unsigned char pe[6]{};
    if (!input.read(reinterpret_cast<char*>(pe), sizeof pe) ||
        pe[0] != 'P' || pe[1] != 'E' || pe[2] || pe[3] || pe[4] != 0x64 || pe[5] != 0x86)
        throw std::runtime_error("Windows x64版が必要です: " + path.u8string());
}

struct Target {
    std::string id = "26.51.1";
    std::string loader = "26.51.6";
    std::string runtimeGame = "1.26.51.01";
    std::vector<std::string> versions{"1.26.5101.0", "1.26.51.01", "1.26.51.1", "26.51.1.0"};
};

inline bool SupportedGameVersion(const std::string& version, const Target& target = {})
{
    return std::find(target.versions.begin(), target.versions.end(), version) != target.versions.end();
}

struct Instance { fs::path directory; std::string name; std::string version; };

// LeviLauncher adds its loader to the game's import table and keeps the untouched file as
// Minecraft.Windows.exe.orig. That original is what gets copied and checked.
inline fs::path OriginalExecutable(const fs::path& game)
{
    auto original = game;
    original += ".orig";
    std::error_code error;
    if (!fs::is_regular_file(original, error)) return game;
    try
    {
        if (ImportedDlls(game).count("levilauncher.dll") && !ImportedDlls(original).count("levilauncher.dll")) return original;
    }
    catch (const std::exception&) { /* Unreadable: the caller's own checks report it. */ }
    return game;
}

struct GameIdentity { std::string name, version; bool listsExecutable = false; };

inline GameIdentity ReadGameIdentity(const fs::path& directory)
{
    const auto config = directory / "MicrosoftGame.config";
    if (!fs::is_regular_file(config) || fs::file_size(config) > 4 * 1024 * 1024)
        throw std::runtime_error("GDK版のMicrosoftGame.configが見つかりません。Xboxアプリからゲーム本体の場所を確認してください。");
    std::ifstream input(config, std::ios::binary);
    const std::string xml{std::istreambuf_iterator<char>(input), {}};
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS)
        throw std::runtime_error("MicrosoftGame.configのXMLが不正です。");
    const auto* root = document.RootElement();
    const auto* identity = root ? root->FirstChildElement("Identity") : nullptr;
    const auto* executables = root ? root->FirstChildElement("ExecutableList") : nullptr;
    const char* name = identity ? identity->Attribute("Name") : nullptr;
    const char* version = identity ? identity->Attribute("Version") : nullptr;
    if (!root || std::string(root->Name()) != "Game" || !name || !version || !executables)
        throw std::runtime_error("MicrosoftGame.configにゲームの識別情報がありません。");
    GameIdentity result{name, version};
    for (const auto* executable = executables->FirstChildElement("Executable"); executable;
         executable = executable->NextSiblingElement("Executable"))
    {
        const char* filename = executable->Attribute("Name");
        if (filename && Lower(filename) == "minecraft.windows.exe") result.listsExecutable = true;
    }
    return result;
}

inline bool ReleaseGameName(const std::string& name)
{
    const auto normalized = Lower(name);
    return normalized == "microsoft.minecraftuwp" || normalized == "microsoft.minecraftwindows" || normalized == "microsoft.minecraft";
}

inline Instance ResolveGame(const fs::path& game, const Target& target = {}, bool inspectExecutable = true)
{
    if (Lower(game.filename().u8string()) != "minecraft.windows.exe")
        throw std::runtime_error("Minecraft本体のMinecraft.Windows.exeを選択してください。");
    const auto executableFile = OriginalExecutable(game);
    if (inspectExecutable) RequireX64Pe(executableFile);
    const auto directory = game.parent_path();
    const auto identity = ReadGameIdentity(directory);
    const auto& name = identity.name;
    const auto& version = identity.version;
    if (!ReleaseGameName(name))
        throw std::runtime_error("MinecraftのGDK Release版を選択してください。Preview版には対応していません。");
    if (!SupportedGameVersion(version, target))
        throw std::runtime_error("Minecraft " + target.id + "が必要です。選択した本体のバージョン: " + version);
    if (!identity.listsExecutable) throw std::runtime_error("GDK設定にMinecraft.Windows.exeが登録されていません。");
    if (inspectExecutable)
    {
        const auto imports = ImportedDlls(executableFile);
        if (imports.count("levilauncher.dll"))
            throw std::runtime_error("別ランチャー用に変更された本体です。Xboxアプリの公式インストール先の本体を選択してください。");
    }
    return {directory, name, version};
}

inline fs::path SafeChild(const fs::path& root, const std::string& relative)
{
    std::string normalized = relative;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.empty() || normalized.front() == '/' || normalized.find(':') != std::string::npos)
        throw std::runtime_error("相対パスの形式が不正です。");
    const auto path = fs::u8path(normalized);
    for (const auto& part : path)
        if (part == ".." || part == "." || part.empty()) throw std::runtime_error("範囲外のパスは使用できません。");
    return root / path;
}

inline void RequireLoader(const fs::path& gameDirectory, const Target& target = {})
{
    const auto directory = gameDirectory / "mods/LeviLamina";
    const auto meta = ReadJson(directory / "manifest.json");
    if (meta.value("name", "") != "LeviLamina" || meta.value("version", "") != target.loader ||
        meta.value("type", "") != "preload-native" || meta.value("entry", "") != "LeviLamina.dll")
        throw std::runtime_error("必要なLeviLamina Client: " + target.loader);
    RequireX64Pe(directory / "LeviLamina.dll");
}

inline void RequireMod(const fs::path& directory, [[maybe_unused]] const Target& target = {})
{
    const auto meta = ReadJson(directory / "manifest.json");
    // LeviLamina itself is a "preload-native" mod that is not part of the mod registry, so a
    // manifest dependency on it is reported as "dependency LeviLamina is missing" and the mod is
    // rejected. The loader version is verified by MyClientData.dll itself (ll_mod_load).
    const bool hasDependencies = meta.contains("dependencies") && !meta.at("dependencies").empty();
    if (meta.value("name", "") != "MyClientData" || meta.value("entry", "") != "MyClientData.dll" ||
        meta.value("type", "") != "native" || hasDependencies)
        throw std::runtime_error("配布MODの形式が一致しません。ZIPをすべて展開してください。");
    RequireX64Pe(directory / "MyClientData.dll");
}

inline void RequirePlainTree(const fs::path& directory)
{
    if (fs::is_symlink(fs::symlink_status(directory)) || !fs::is_directory(directory))
        throw std::runtime_error("MODフォルダーが通常のフォルダーではありません: " + directory.u8string());
    for (const auto& entry : fs::recursive_directory_iterator(directory))
        if (fs::is_symlink(entry.symlink_status()) ||
            (!entry.is_regular_file() && !entry.is_directory()))
            throw std::runtime_error("MOD内のリンクや特殊ファイルには対応していません: " + entry.path().u8string());
}

// Progress callback: bytes copied so far, total bytes to copy.
using CopyProgress = std::function<void(unsigned long long, unsigned long long)>;

inline unsigned long long TreeBytes(const fs::path& path)
{
    unsigned long long total = 0;
    if (fs::is_regular_file(path)) return static_cast<unsigned long long>(fs::file_size(path));
    for (const auto& entry : fs::recursive_directory_iterator(path))
        if (entry.is_regular_file()) total += static_cast<unsigned long long>(entry.file_size());
    return total;
}

// Copies one file in 4 MiB chunks so large game archives still report progress.
inline void CopyFileChunked(const fs::path& from, const fs::path& to, unsigned long long& done,
                            unsigned long long total, const CopyProgress& progress)
{
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!in || !out) throw std::runtime_error("ゲームのファイルをコピーできません: " + from.u8string());
    std::vector<char> buffer(4u << 20);
    unsigned long long reported = done;
    while (in)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count <= 0) break;
        out.write(buffer.data(), count);
        if (!out) throw std::runtime_error("ゲームのファイルを書き込めません。空き容量を確認してください: " + to.u8string());
        done += static_cast<unsigned long long>(count);
        if (progress && done - reported >= (8ull << 20)) { reported = done; progress(done, total); }
    }
    out.close();
    if (!out) throw std::runtime_error("ゲームのファイルを書き込めません。空き容量を確認してください: " + to.u8string());
    std::error_code ignored;
    fs::last_write_time(to, fs::last_write_time(from), ignored);
}

inline void CopyEntryProgress(const fs::path& from, const fs::path& to, unsigned long long& done,
                              unsigned long long total, const CopyProgress& progress)
{
    if (fs::is_directory(from))
    {
        fs::create_directories(to);
        for (const auto& entry : fs::recursive_directory_iterator(from))
        {
            const auto destination = to / fs::relative(entry.path(), from);
            if (entry.is_directory()) fs::create_directories(destination);
            else CopyFileChunked(entry.path(), destination, done, total, progress);
        }
    }
    else CopyFileChunked(from, to, done, total, progress);
    if (progress) progress(done, total);
}

inline Instance PrepareManagedGame(const fs::path& sourceExecutable, const fs::path& destination, const Target& target = {},
                                   const CopyProgress& progress = {})
{
    if (fs::exists(destination))
    {
        if (fs::is_symlink(fs::symlink_status(destination)) || !fs::is_directory(destination))
            throw std::runtime_error("自作ソフト用のゲーム保存先が通常のフォルダーではありません。");
        const auto marker = ReadJson(destination / "MyClient-game.json");
        if (marker.value("owner", "") != "MyClient" || marker.value("schemaVersion", 0) != 1 ||
            !fs::is_regular_file(destination / "MyClient-original.exe"))
            throw std::runtime_error("準備済みゲームの情報が不正です。保存先とログを確認してください。");
        return ResolveGame(destination / "Minecraft.Windows.exe", target);
    }
    const auto source = ResolveGame(sourceExecutable, target);
    const auto originalExecutable = OriginalExecutable(sourceExecutable);
    if (ImportedDlls(originalExecutable).count("preloader.dll"))
        throw std::runtime_error("変更済みの本体です。Xboxアプリの公式インストール先の本体を選択してください。");
    const auto canonicalSource = fs::canonical(source.directory);
    const auto canonicalTarget = fs::weakly_canonical(destination);
    auto sourcePart = canonicalSource.begin(), targetPart = canonicalTarget.begin();
    while (sourcePart != canonicalSource.end() && targetPart != canonicalTarget.end() && *sourcePart == *targetPart)
    { ++sourcePart; ++targetPart; }
    if (sourcePart == canonicalSource.end())
        throw std::runtime_error("ゲームのコピー先を元のゲームフォルダー内には作成できません。");
    const auto token = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    fs::create_directories(destination.parent_path());
    const auto stage = destination.parent_path() / fs::u8path(".MyClient-game-" + token);
    if (!fs::create_directory(stage)) throw std::runtime_error("ゲーム用の一時フォルダーを作成できません。");
    try
    {
        // Game saves are outside the installed GDK content directory. Do not
        // copy another mod environment, installer state or loader binaries.
        const std::set<std::string> excluded{"mods", "plugins", "myclient-backups", "levilauncher.dll",
            "preloader.dll", "preloaderconfig.json", "peeditor.exe", "version.json", "myclient-game.json",
            "myclient-original.exe", "myclient-runtime.json", "logs", ".lip"};
        std::vector<fs::path> toCopy;
        unsigned long long total = 0, done = 0;
        for (const auto& entry : fs::directory_iterator(source.directory))
        {
            const auto name = Lower(entry.path().filename().u8string());
            const auto endsWith = [&name](const std::string& suffix) {
                return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
            };
            if (excluded.count(name) || endsWith(".bak") || endsWith(".orig")) continue;
            if (fs::is_symlink(entry.symlink_status())) throw std::runtime_error("ゲーム本体内のリンクには対応していません。");
            if (entry.is_directory()) RequirePlainTree(entry.path());
            else if (!entry.is_regular_file()) throw std::runtime_error("ゲーム本体に特殊ファイルがあります。");
            toCopy.push_back(entry.path());
            total += TreeBytes(entry.path());
        }
        if (progress) progress(0, total);
        for (const auto& path : toCopy)
            CopyEntryProgress(path, stage / path.filename(), done, total, progress);
        if (originalExecutable != sourceExecutable)
            CopyFileChunked(originalExecutable, stage / "Minecraft.Windows.exe", done, total, progress);
        ResolveGame(stage / "Minecraft.Windows.exe", target);
        fs::copy_file(stage / "Minecraft.Windows.exe", stage / "MyClient-original.exe");
        std::ofstream marker(stage / "MyClient-game.json", std::ios::binary);
        marker << Json{{"owner", "MyClient"}, {"schemaVersion", 1}, {"source", source.directory.u8string()},
                       {"gameVersion", source.version}}.dump(2);
        marker.close();
        if (!marker) throw std::runtime_error("ゲームの準備情報を保存できません。");
        fs::rename(stage, destination);
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove_all(stage, ignored);
        throw;
    }
    return ResolveGame(destination / "Minecraft.Windows.exe", target);
}

// Caller must ensure Minecraft is stopped. Copy everything before swapping
// directories; retain old contents as a backup and preserve mod settings.
// Copies every file under `from` into `to`, replacing files that already exist.
// std::filesystem's overwrite flags fail with "File exists" on the MinGW toolchain, so the
// destination is simply opened with truncation (CopyFileChunked) instead of relying on them.
inline void CopyTreeOverwrite(const fs::path& from, const fs::path& to)
{
    fs::create_directories(to);
    unsigned long long done = 0;
    for (const auto& entry : fs::recursive_directory_iterator(from))
    {
        const auto destination = to / fs::relative(entry.path(), from);
        if (entry.is_directory()) fs::create_directories(destination);
        else CopyFileChunked(entry.path(), destination, done, 0, CopyProgress{});
    }
}

inline fs::path InstallMod(const fs::path& source, const fs::path& gameDirectory, const Target& profile = {})
{
    RequireMod(source, profile);
    RequirePlainTree(source);
    RequireLoader(gameDirectory, profile);
    const auto mods = gameDirectory / "mods";
    if (fs::is_symlink(fs::symlink_status(mods)))
        throw std::runtime_error("modsフォルダーのリンクには対応していません。");
    const auto target = mods / "MyClientData";
    if (fs::exists(target))
    {
        RequirePlainTree(target);
        if (fs::equivalent(source, target))
            throw std::runtime_error("配布ZIPはゲーム環境とは別のフォルダーに展開してください。");
    }
    else if (fs::is_symlink(fs::symlink_status(target)))
        throw std::runtime_error("導入先にリンクがあります。");
    // Backups stay outside mods, so the loader cannot discover the old DLL.
    const auto backups = gameDirectory / "MyClient-backups";
    if (fs::exists(backups) && (fs::is_symlink(fs::symlink_status(backups)) || !fs::is_directory(backups)))
        throw std::runtime_error("MyClient-backupsが通常のフォルダーではありません。");
    const auto token = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto stage = gameDirectory / fs::u8path(".MyClient-install-" + token);
    const auto backup = backups / fs::u8path("MyClientData-" + token);
    if (fs::exists(stage) || fs::exists(backup)) throw std::runtime_error("導入用の一時フォルダーが既に存在します。");
    fs::create_directory(stage);
    bool movedOld = false;
    try
    {
        // fs::copy(dir, existing dir, recursive | overwrite_existing) throws "File exists" with
        // this toolchain, so copy file by file and overwrite explicitly.
        if (fs::exists(target)) CopyTreeOverwrite(target, stage);
        CopyTreeOverwrite(source, stage);
        fs::remove(stage / "manifest.json.close");
        RequireMod(stage, profile);
        if (fs::exists(target))
        {
            fs::create_directories(backups);
            fs::rename(target, backup);
            movedOld = true;
        }
        fs::rename(stage, target);
    }
    catch (...)
    {
        std::error_code ignored;
        if (movedOld)
        {
            fs::rename(backup, target, ignored);
            if (ignored)
                throw std::runtime_error("復元に失敗しました。以前のMODはここに保管されています: " + backup.u8string());
        }
        fs::remove_all(stage, ignored);
        throw;
    }
    return movedOld ? backup : fs::path{};
}
} // namespace MyClientLauncher

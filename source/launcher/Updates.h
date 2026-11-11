#pragma once
#include "Install.h"
#include <cstdio>
#include <functional>
#include <regex>

namespace MyClientLauncher {
// The update list and the files it names are public downloads from OriginX's R2 (the Worker's /dl/stable/).
inline constexpr const char* PublicDownloads = "https://originx.originx-client.workers.dev/dl/stable/";
inline constexpr const char* CatalogUrl = "https://originx.originx-client.workers.dev/dl/stable/catalog.json";
inline constexpr const char* HudFile = "hud/MyClient.exe";
using HashFile = std::function<std::string(const fs::path&)>;
using DownloadFile = std::function<void(const std::string&, const fs::path&, uint64_t)>;
struct Release {
    Target target;
    bool available = false;
    std::string reason;
    uint64_t revision = 0;
    Json files = Json::object();
};
struct Catalog { uint64_t sequence = 0; std::vector<Release> releases; Json document; };

inline bool IsDigest(const std::string& s)
{
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
inline bool ReleaseUrl(const std::string& url)
{
    for (const std::string prefix : {"https://github.com/Nagi456/MyClient/releases/download/", PublicDownloads})
        if (url.size() > prefix.size() && url.compare(0, prefix.size(), prefix) == 0 &&
            url.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/", prefix.size()) == std::string::npos &&
            url.find("..", prefix.size()) == std::string::npos)
            return true;
    return false;
}
inline bool PackagePath(const std::string& name)
{
    if (name == "runtime.lock.json" || name == "mods/MyClientData/MyClientData.dll" ||
        name == "mods/MyClientData/manifest.json" || name == "mods/MyClientData/THIRD_PARTY_NOTICES.md" || name == HudFile) return true;
    const std::string prefix = "mods/MyClientData/licenses/";
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    // License files only; do not let an update introduce a second loadable DLL.
    return std::regex_match(name.substr(prefix.size()), std::regex("[A-Za-z0-9_-]+/[A-Za-z0-9_-]+(\\.(txt|md|MIT|LESSER|spdx))?"));
}
inline uint64_t PositiveNumber(const Json& value)
{
    if (!value.is_number_unsigned() || value.get<uint64_t>() == 0 || value.get<uint64_t>() > 1000000000)
        throw std::runtime_error("更新情報の番号が不正です。");
    return value.get<uint64_t>();
}
inline Target ParseTarget(const Json& item)
{
    Target target;
    target.id = item.at("id").get<std::string>();
    if (!std::regex_match(target.id, std::regex("[1-9][0-9]?\\.[0-9]{1,2}\\.[0-9]{1,2}")))
        throw std::runtime_error("ゲーム版番号が不正です。");
    const auto first = target.id.find('.'), second = target.id.find('.', first + 1);
    const auto major = target.id.substr(0, first);
    const auto minor = target.id.substr(first + 1, second - first - 1);
    const auto patch = target.id.substr(second + 1);
    const auto normalized = std::to_string(std::stoi(major)) + "." + std::to_string(std::stoi(minor)) + "." + std::to_string(std::stoi(patch));
    if (normalized != target.id) throw std::runtime_error("ゲーム版番号の表記が不正です。");
    const auto prefix = "1." + major + "." + minor + ".";
    target.runtimeGame = prefix + (patch.size() == 1 ? "0" : "") + patch;
    target.versions = {target.runtimeGame, prefix + patch, target.id + ".0",
        "1." + major + "." + std::to_string(std::stoi(minor) * 100 + std::stoi(patch)) + ".0"};
    target.loader = item.value("loader", "");
    return target;
}
// urlOk decides which file URLs a catalog may name: the public release downloads (the default), or, for a release
// channel's catalog the Worker answered after a role check, the signed file links (Channels.h SignedFileUrl).
using UrlCheck = std::function<bool(const std::string&)>;
inline Catalog ParseCatalog(const Json& doc, const UrlCheck& urlOk = [](const std::string& url) { return ReleaseUrl(url); })
{
    if (doc.at("schemaVersion") != 1 || !doc.at("versions").is_array() || doc.at("versions").size() > 128)
        throw std::runtime_error("このランチャーでは更新一覧を読み込めません。");
    Catalog catalog{PositiveNumber(doc.at("sequence")), {}, doc};
    std::set<std::string> ids;
    for (const auto& item : doc.at("versions"))
    {
        Release release;
        release.target = ParseTarget(item);
        if (!ids.insert(release.target.id).second) throw std::runtime_error("ゲーム版が重複しています。");
        release.reason = item.value("reason", "対応DLLは未公開です。");
        if (release.reason.size() > 1024) throw std::runtime_error("更新説明が長すぎます。");
        release.available = item.at("available").get<bool>();
        if (release.available)
        {
            // launcherApi 1 = a LeviLamina mod (loader = its version); 2 = runs without LeviLamina (loader "standalone",
            // no runtime.lock.json). Launchers before build 27 know only 1 and show "a newer launcher is needed" for 2.
            const bool standalone = item.at("launcherApi") == 2;
            if ((item.at("launcherApi") != 1 && !standalone) || item.at("bridgeAbi") != 2 || item.at("platform") != "gdk-release-x64")
            {
                release.available = false;
                release.reason = "新しいランチャーが必要です。使い方をご確認ください。";
            }
            else
            {
                if (standalone ? release.target.loader != "standalone"
                               : !std::regex_match(release.target.loader, std::regex("[0-9]+\\.[0-9]+\\.[0-9]+")))
                    throw std::runtime_error("ローダー版が不正です。");
                release.target.standalone = standalone;
                release.revision = PositiveNumber(item.at("revision"));
                release.files = item.at("files");
                if (!release.files.is_object() || release.files.size() > 128) throw std::runtime_error("更新ファイル一覧が不正です。");
                for (const char* required : {"runtime.lock.json", "mods/MyClientData/MyClientData.dll", "mods/MyClientData/manifest.json"})
                    if (!release.files.contains(required) && !(standalone && std::string(required) == "runtime.lock.json"))
                        throw std::runtime_error("更新ファイルが不足しています。");
                if (standalone && release.files.contains("runtime.lock.json")) throw std::runtime_error("更新ファイル一覧が不正です。");
                uint64_t total = 0;
                std::set<std::string> names;
                for (const auto& file : release.files.items())
                {
                    const auto size = PositiveNumber(file.value().at("bytes"));
                    if (!PackagePath(file.key()) || !names.insert(Lower(file.key())).second ||
                        !IsDigest(file.value().at("sha256")) || !urlOk(file.value().at("url")) || size > 128 * 1024 * 1024)
                        throw std::runtime_error("更新ファイルの配布情報が不正です。");
                    total += size;
                }
                if (total > 256 * 1024 * 1024) throw std::runtime_error("更新サイズが上限を超えています。");
            }
        }
        catalog.releases.push_back(std::move(release));
    }
    if (catalog.releases.empty()) throw std::runtime_error("更新一覧が空です。");
    return catalog;
}
inline void CheckAdvance(const Catalog& old, const Catalog& next)
{
    if (next.sequence < old.sequence || (next.sequence == old.sequence && next.document != old.document))
        throw std::runtime_error("古い更新一覧または同じ番号の変更を拒否しました。");
    for (const auto& previous : old.releases)
        for (const auto& current : next.releases)
            if (previous.target.id == current.target.id && previous.available && current.available &&
                (current.revision < previous.revision || (current.revision == previous.revision &&
                 (current.files != previous.files || current.target.loader != previous.target.loader))))
                throw std::runtime_error("DLLの更新番号が逆行または再利用されています。");
}
inline void VerifyPackage(const fs::path& root, const Release& release, const HashFile& hash)
{
    if (!release.available) throw std::runtime_error(release.reason);
    RequirePlainTree(root);
    for (const auto& file : release.files.items())
    {
        const auto path = SafeChild(root, file.key());
        if (!fs::is_regular_file(path) || fs::file_size(path) != file.value().at("bytes").get<uint64_t>() ||
            hash(path) != file.value().at("sha256").get<std::string>())
            throw std::runtime_error("更新ファイルの検査に失敗しました: " + file.key());
    }
    RequireMod(root / "mods/MyClientData", release.target);
    if (release.files.contains(HudFile)) RequireX64Pe(root / HudFile);
    if (release.target.standalone)
    {
        // No loader, so no lock to compare: the DLL itself must not need LeviLamina (it checks the game build when it starts).
        const auto imports = ImportedDlls(root / "mods/MyClientData/MyClientData.dll");
        if (imports.count("levilamina.dll") || imports.count("preloader.dll"))
            throw std::runtime_error("単独版のはずのDLLがLeviLaminaを必要としています。");
    }
    else
    {
        const auto lock = ReadJson(root / "runtime.lock.json");
        if (lock.at("schemaVersion") != 1 || lock.at("gameVersion") != release.target.runtimeGame || lock.at("loaderVersion") != release.target.loader)
            throw std::runtime_error("DLLとローダーの対応版が一致しません。");
    }
    // Extra code must never hitchhike into the mod installation from a cache.
    for (const auto& entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file() && !release.files.contains(entry.path().lexically_relative(root).generic_u8string()))
            throw std::runtime_error("更新キャッシュに未登録ファイルがあります。");
}
// `folder` names the cache folder (default: the revision number).
// Files shipped next to the launcher (nothing downloaded). The catalog's hashes describe the published
// build; a DLL rebuilt and copied into the MyClient folder by hand is as trusted as the launcher beside it,
// so the release is taken as it really is on disk: each file's own SHA256 and size. `folder` gets a cache
// folder name of its own ("local-<digest of all file hashes>"), so it never meets an older saved copy.
// Downloads keep the strict check against the catalog.
inline Release LocalRelease(Release release, const std::function<fs::path(const std::string&)>& source,
                            const HashFile& hash, std::string& folder)
{
    uint64_t digest = 1469598103934665603ull;   // FNV-1a over "path=sha256;" of every file
    for (auto& file : release.files.items())
    {
        const auto path = source(file.key());
        if (!fs::is_regular_file(path)) throw std::runtime_error("同梱更新ファイルが見つかりません: " + file.key());
        const std::string sha = hash(path);
        file.value()["sha256"] = sha;
        file.value()["bytes"] = static_cast<uint64_t>(fs::file_size(path));
        for (char c : file.key() + "=" + sha + ";") { digest ^= static_cast<unsigned char>(c); digest *= 1099511628211ull; }
    }
    char name[32];
    std::snprintf(name, sizeof name, "local-%016llx", static_cast<unsigned long long>(digest));
    folder = name;
    return release;
}

inline fs::path AcquirePackage(const fs::path& cache, const Release& release,
                              const HashFile& hash, const DownloadFile& download, const std::string& folder = "")
{
    if (!release.available) throw std::runtime_error(release.reason);
    fs::create_directories(cache);
    const auto destination = cache / release.target.id / (folder.empty() ? std::to_string(release.revision) : folder);
    fs::create_directories(destination.parent_path());
    RequirePlainTree(cache);
    if (fs::exists(destination)) { VerifyPackage(destination, release, hash); return destination; }
    const auto stage = cache / (".download-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(stage)) throw std::runtime_error("更新用フォルダーを作成できません。");
    try
    {
        for (const auto& file : release.files.items())
        {
            const auto path = SafeChild(stage, file.key());
            fs::create_directories(path.parent_path());
            download(file.value().at("url"), path, file.value().at("bytes"));
            if (hash(path) != file.value().at("sha256")) throw std::runtime_error("ダウンロードしたファイルのSHA256が一致しません。");
        }
        VerifyPackage(stage, release, hash);
        fs::rename(stage, destination);
    }
    catch (...) { std::error_code ignored; fs::remove_all(stage, ignored); throw; }
    return destination;
}
// Which MyClient.exe (the HUD) to run. A downloaded package brings the HUD built with its DLL: it is copied to
// `managed` (%LOCALAPPDATA%\MyClient\hud), a folder that stays the same across updates because the HUD keeps
// its settings (myclient_config.txt) next to itself; settings of a HUD that sat beside the launcher are copied
// there once. Files shipped beside the launcher (`local`: the bundled list, or no HUD in the package) keep
// using the HUD beside it; with none there, the last downloaded one.
inline fs::path PlaceHud(const fs::path& package, const fs::path& managed, const fs::path& bundle, bool local, const HashFile& hash)
{
    const auto downloaded = package.empty() ? fs::path{} : package / HudFile;
    const auto placed = managed / "MyClient.exe";
    if (!local && !downloaded.empty() && fs::is_regular_file(downloaded))
    {
        fs::create_directories(managed);
        if (!fs::is_regular_file(placed) || hash(placed) != hash(downloaded))
        {
            const auto stage = managed / "MyClient.exe.new";
            std::error_code ec;
            fs::copy_file(downloaded, stage, fs::copy_options::overwrite_existing, ec);
            if (!ec) fs::rename(stage, placed, ec);
            if (ec)
            {
                fs::remove(stage, ec);
                throw std::runtime_error("HUD（MyClient.exe）を更新できません。起動中の MyClient.exe を終了してからもう一度お試しください。");
            }
        }
        if (!fs::exists(managed / "myclient_config.txt") && fs::is_regular_file(bundle / "myclient_config.txt"))
        {
            std::error_code ignored;
            fs::copy_file(bundle / "myclient_config.txt", managed / "myclient_config.txt", ignored);
        }
        return placed;
    }
    if (fs::is_regular_file(bundle / "MyClient.exe")) return bundle / "MyClient.exe";
    if (fs::is_regular_file(placed)) return placed;
    throw std::runtime_error("HUD（MyClient.exe）が見つかりません。Settings の「更新を確認」を押してから起動してください。");
}
}

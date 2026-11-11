#pragma once
// The launcher replacing itself with the one the site hands out (2026-11-03 user request).
// build-launcher (publish=true) uploads stable/launcher/<build>/MyClientLauncher.exe and then stable/launcher.json
// ({"schemaVersion":1,"build":N,"url":...,"sha256":...,"bytes":...}). At start the launcher reads that list; when
// its build is newer, it downloads the exe, checks the SHA256 and that it is an x64 program, renames itself to
// <name>.old (Windows lets a running exe be renamed), puts the new one in its place and starts it.
// A launcher built without a number (MYCLIENT_LAUNCHER_BUILD = 0, a local build) never replaces itself.
#include "Updates.h"

#ifndef MYCLIENT_LAUNCHER_BUILD
#define MYCLIENT_LAUNCHER_BUILD 0
#endif

namespace MyClientLauncher {
inline constexpr uint64_t LauncherBuild = MYCLIENT_LAUNCHER_BUILD;
inline constexpr const char* LauncherListUrl = "https://originx.originx-client.workers.dev/dl/stable/launcher.json";

struct LauncherInfo { uint64_t build = 0; std::string url, sha256; uint64_t bytes = 0; };

inline LauncherInfo ParseLauncherInfo(const Json& doc)
{
    if (!doc.is_object() || doc.value("schemaVersion", 0) != 1) throw std::runtime_error("ランチャーの更新情報を読めません。");
    LauncherInfo info;
    info.build = PositiveNumber(doc.at("build"));
    info.url = doc.at("url").get<std::string>();
    info.sha256 = doc.at("sha256").get<std::string>();
    info.bytes = PositiveNumber(doc.at("bytes"));
    // Only the file published for this very build, under our public downloads.
    const std::string expected = std::string(PublicDownloads) + "launcher/" + std::to_string(info.build) + "/MyClientLauncher.exe";
    if (info.url != expected || !ReleaseUrl(info.url) || !IsDigest(info.sha256) || info.bytes > 64ull * 1024 * 1024)
        throw std::runtime_error("ランチャーの更新情報が不正です。");
    return info;
}

inline bool LauncherUpdateWanted(uint64_t current, const LauncherInfo& info) { return current != 0 && info.build > current; }

inline fs::path OldLauncherPath(const fs::path& exe) { return fs::path(exe.native() + fs::path(".old").native()); }

// Puts `fresh` where `exe` is. The running exe goes to <exe>.old; on any failure the original is put back.
inline void SwapLauncher(const fs::path& exe, const fs::path& fresh)
{
    const auto old = OldLauncherPath(exe);
    std::error_code ec;
    fs::remove(old, ec);
    if (fs::exists(old)) throw std::runtime_error("前回の古いランチャーを消せません: " + old.u8string());
    fs::rename(exe, old, ec);
    if (ec) throw std::runtime_error("ランチャーを入れ替えられません（フォルダーに書き込めない可能性があります）: " + ec.message());
    fs::rename(fresh, exe, ec);
    if (!ec) return;
    std::error_code ignored;
    fs::rename(old, exe, ignored);
    throw std::runtime_error("新しいランチャーを置けませんでした: " + ec.message());
}

// The previous launcher left by SwapLauncher; it may still be closing, so failing is fine (tried again next start).
inline void RemoveOldLauncher(const fs::path& exe)
{
    std::error_code ignored;
    fs::remove(OldLauncherPath(exe), ignored);
}
}

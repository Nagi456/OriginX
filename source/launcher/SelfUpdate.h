#pragma once
// The launcher replacing itself with the one the site hands out (2026-11-03 user request).
// build-launcher (publish=true) uploads stable/launcher/<build>/MyClientLauncher.exe and then stable/launcher.json
// ({"schemaVersion":1,"build":N,"url":...,"sha256":...,"bytes":...}). At start the launcher reads that list; when
// its build is newer, it downloads the exe, checks the SHA256 and that it is an x64 program, renames itself to
// <name>.old (Windows lets a running exe be renamed), puts the new one in its place and starts it.
// A launcher built without a number (MYCLIENT_LAUNCHER_BUILD = 0, a local build) never replaces itself.
#include "../Lang.h"
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
    if (!doc.is_object() || doc.value("schemaVersion", 0) != 1) throw std::runtime_error(Lang::T("ランチャーの更新情報を読めません。", "Cannot read the launcher update information."));
    LauncherInfo info;
    // The stable list is for everyone: a list that says it is another channel's is refused (2026-11-20, author).
    if (doc.contains("channel") && doc.at("channel") != "stable") throw std::runtime_error(Lang::T("ランチャーの更新情報が不正です。", "The launcher update information is invalid."));
    info.build = PositiveNumber(doc.at("build"));
    info.url = doc.at("url").get<std::string>();
    info.sha256 = doc.at("sha256").get<std::string>();
    info.bytes = PositiveNumber(doc.at("bytes"));
    // Only the file published for this very build, under our public downloads.
    const std::string expected = std::string(PublicDownloads) + "launcher/" + std::to_string(info.build) + "/MyClientLauncher.exe";
    if (info.url != expected || !ReleaseUrl(info.url) || !IsDigest(info.sha256) || info.bytes > 64ull * 1024 * 1024)
        throw std::runtime_error(Lang::T("ランチャーの更新情報が不正です。", "The launcher update information is invalid."));
    return info;
}

// The test channel's launcher (signed as launcher/test; author 2026-11-20). Only for accounts on the test channel:
// the Worker hands it out with the test role (status.ts testLauncher), and the list itself must say channel "test"
// and name the exe of its own build under test/, so no other list can stand in for it.
// The exe comes through a 30-minute link of the Worker (`link`), whose file must be that very key.
inline std::string TestLauncherKey(uint64_t build) { return "test/launcher/" + std::to_string(build) + "/MyClientLauncher.exe"; }
inline LauncherInfo ParseTestLauncherInfo(const Json& doc, const std::string& link, const std::string& linkPrefix)
{
    const auto bad = [] { return std::runtime_error(Lang::T("テスト版ランチャーの更新情報が不正です。", "The test launcher update information is invalid.")); };
    if (!doc.is_object() || doc.value("schemaVersion", 0) != 1 || !doc.contains("channel") || doc.at("channel") != "test") throw bad();
    LauncherInfo info;
    info.build = PositiveNumber(doc.at("build"));
    const std::string key = doc.at("key").get<std::string>();
    info.sha256 = doc.at("sha256").get<std::string>();
    info.bytes = PositiveNumber(doc.at("bytes"));
    if (key != TestLauncherKey(info.build) || !IsDigest(info.sha256) || info.bytes > 64ull * 1024 * 1024) throw bad();
    const std::string expected = linkPrefix + key + "?";
    if (link.compare(0, expected.size(), expected) != 0) throw bad();
    info.url = link;
    return info;
}

// `skipped`: a build that was installed once but did not start (2026-11-20, author): not tried again; a later
// build is.
inline bool LauncherUpdateWanted(uint64_t current, const LauncherInfo& info, uint64_t skipped = 0)
{
    return current != 0 && info.build > current && info.build != skipped;
}

inline fs::path OldLauncherPath(const fs::path& exe) { return fs::path(exe.native() + fs::path(".old").native()); }

// Puts `fresh` where `exe` is. The running exe goes to <exe>.old; on any failure the original is put back.
inline void SwapLauncher(const fs::path& exe, const fs::path& fresh)
{
    const auto old = OldLauncherPath(exe);
    std::error_code ec;
    fs::remove(old, ec);
    if (fs::exists(old)) throw std::runtime_error(Lang::T("前回の古いランチャーを消せません: ", "Cannot delete the old launcher from last time: ") + old.u8string());
    fs::rename(exe, old, ec);
    if (ec) throw std::runtime_error(Lang::T("ランチャーを入れ替えられません（フォルダーに書き込めない可能性があります）: ", "Cannot replace the launcher (the folder may not be writable): ") + ec.message());
    fs::rename(fresh, exe, ec);
    if (!ec) return;
    std::error_code ignored;
    fs::rename(old, exe, ignored);
    throw std::runtime_error(Lang::T("新しいランチャーを置けませんでした: ", "Could not place the new launcher: ") + ec.message());
}

// The previous launcher left by SwapLauncher, and a new one that did not start (RollbackLauncher). Only called
// once this launcher holds the single-instance lock, so a launcher still handing over keeps its way back.
// The old one may still be closing, so failing is fine (tried again next start).
inline fs::path FailedLauncherPath(const fs::path& exe) { return fs::path(exe.native() + fs::path(".failed").native()); }
inline void RemoveOldLauncher(const fs::path& exe)
{
    std::error_code ignored;
    fs::remove(OldLauncherPath(exe), ignored);
    fs::remove(FailedLauncherPath(exe), ignored);
}

// Hand-over after a self-update (design 2026-11-20, author's answers). The old launcher keeps the lock and
// starts the new exe with --after-update <its pid>. The new one, once its window is up, leaves this mark;
// the old one then lets go of the lock and closes. No mark within 10 s, or the new one ended: roll back.
inline constexpr uint64_t HandOverLimitMs = 10000;
inline fs::path StartedMarkPath(const fs::path& privateDir, uint32_t oldPid)
{
    return privateDir / ("launcher-started-" + std::to_string(oldPid));
}
enum class HandOver { Waiting, Ready, Failed };
inline HandOver HandOverState(bool marked, bool newEnded, uint64_t elapsedMs)
{
    if (marked) return HandOver::Ready;
    if (newEnded || elapsedMs >= HandOverLimitMs) return HandOver::Failed;
    return HandOver::Waiting;
}

// Puts the previous launcher back after the new one did not start: <exe> -> <exe>.failed, <exe>.old -> <exe>.
// If the second step fails the new one is put back too, nothing is deleted, and the error names both files.
inline void RollbackLauncher(const fs::path& exe)
{
    const auto old = OldLauncherPath(exe), failed = FailedLauncherPath(exe);
    if (!fs::is_regular_file(old)) throw std::runtime_error(Lang::T("前の版のランチャーが見つかりません: ", "The previous launcher was not found: ") + old.u8string());
    std::error_code ec;
    fs::remove(failed, ec);
    fs::rename(exe, failed, ec);
    if (ec) throw std::runtime_error(Lang::T("新しいランチャーをどかせません: ", "Cannot move the new launcher aside: ") + ec.message());
    fs::rename(old, exe, ec);
    if (!ec) return;
    std::error_code ignored;
    fs::rename(failed, exe, ignored);
    throw std::runtime_error(Lang::T("前の版に戻せませんでした。", "Could not put the previous version back: ") + old.u8string() +
                             Lang::T(" を元の名前に戻してください: ", " (rename it back to its original name): ") + ec.message());
}
}

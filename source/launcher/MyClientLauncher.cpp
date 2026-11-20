#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <ctime>
#include <algorithm>
#include <set>
#include "AccountWin.h"   // before <windows.h>: it brings winsock2.h
#include <windows.h>
#include <windowsx.h>
#include "../LangWin.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2   // K32EnumProcessModulesEx from kernel32, no psapi.lib
#endif
#include <psapi.h>
#include "Install.h"
#include "RuntimeWin.h"
#include "DownloadWin.h"
#include "GameVersions.h"
#include "SwitchErrors.h"
#include "PackageCache.h"
#include "StandaloneWin.h"
#include "SelfUpdate.h"
#include "GameImageCheck.h"
#include "ManifestSignature.h"
#include "RemoteStop.h"
#include "../BridgeChannel.h"
#include "../LogPrivacy.h"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cmath>
#include <thread>
#include <vector>

namespace {
using namespace MyClientLauncher;
enum : int { Game = 100, PickGame, Apply, Guide, Logs, StatusText, Version, CheckUpdates, AutoDetect, ImportGame, HomeSettings, Caption = 200, TitleText };
// Black, very dark blue and bright blue (Flarial's black / dark red / bright red, in blue).
// Only colours/shapes; behaviour of every control is unchanged.
constexpr COLORREF ColBg = RGB(5, 6, 10), ColPanel = RGB(14, 17, 25), ColSide = RGB(8, 10, 16), ColTile = RGB(12, 30, 66),
                   ColLine = RGB(24, 30, 44), ColGreen = RGB(110, 220, 150), ColText = RGB(236, 240, 248),
                   ColMuted = RGB(128, 140, 164), ColAccent = RGB(34, 124, 255), ColAccentHi = RGB(112, 178, 255),
                   ColAccentDown = RGB(20, 86, 196), ColSecondary = RGB(24, 29, 42), ColSecondaryDown = RGB(34, 42, 60),
                   ColBorder = RGB(34, 42, 60), ColDisabled = RGB(18, 21, 30), ColDisabledText = RGB(80, 90, 110),
                   ColError = RGB(255, 112, 132);
constexpr UINT PreparedMessage = WM_APP + 1;
constexpr UINT UpdatedMessage = WM_APP + 2, PackageMessage = WM_APP + 3, CheckMessage = WM_APP + 4, VersionDbMessage = WM_APP + 5,
           SwitchedMessage = WM_APP + 6, LauncherUpdateMessage = WM_APP + 7, ApplyLauncherMessage = WM_APP + 8, LoginMessage = WM_APP + 9,
           NormalVersionMessage = WM_APP + 10;
enum class State { Idle, Preparing, Installing, Waiting, Checking, Fetching, Attaching, Switching };
// Attach mode: the protected GDK executable is never copied or edited. The game is
// started normally and PreLoader.dll is injected into the running process.
bool attachMode = false;
fs::path attachDir;
DWORD injectedPid = 0, seenPid = 0;
ULONGLONG seenAt = 0;
int injectFails = 0;
HWND window;
HFONT font, heading, smallFont, boldFont;
HBRUSH background, panelBrush;
bool statusError = false;
int progressPercent = -1;   // -1 = no progress bar
std::atomic<unsigned long long> copyDone{0}, copyTotal{0};   // written by the game-copy thread
std::wstring progressText;
int dpi = 96;
State state = State::Idle;
HANDLE installer = nullptr;
HANDLE gameProcess = nullptr;
HANDLE hudProcess = nullptr;
DWORD hudGamePid = 0;
fs::path bundle, privateDirectory;
std::thread preparation;
struct PreparationResult { Instance instance; std::string error; };
Instance pending;
Catalog catalog, bundledCatalog;
Release selected;
// What the fetched package needs (2026-11-11): a test/early build can differ from the stable `selected` (e.g. a
// standalone test build of a LeviLamina stable release), and how the game is started follows the package.
Target launchTarget;
fs::path packageRoot;
bool packageLocal = false;   // packageRoot holds the files shipped beside the launcher
Json sources = Json::object();
struct UpdateResult { Catalog catalog; std::string error; };
struct PackageResult { fs::path root; std::optional<Target> target; bool local = false; std::string error; std::string note; Channel channel = Channel::Stable; std::optional<AccountStatus> status; unsigned epoch = 0; };
// Version switching: folders the user added, what was found on this PC and the public list of released versions.
Json imports = Json::array();
std::string xboxSeen;   // the Xbox app install's version when the launcher last looked (launcher.json)
std::map<std::string, fs::path> installedGames;   // version id -> Minecraft.Windows.exe
std::vector<std::string> publishedVersions;
std::map<std::string, std::vector<std::string>> versionLinks;   // version id -> Microsoft's package links
std::wstring versionDbNote = Lang::T(L"未取得", L"not fetched");
std::thread versionDbThread;
struct VersionDbResult { std::vector<std::string> versions; std::map<std::string, std::vector<std::string>> links; std::string error; };
// Changing the installed game (Flarial's way): Microsoft's package is downloaded and Windows installs it
// over the Store copy, so Gaming Services decrypts it with the user's own licence.
std::thread switchThread;
// Self-update (SelfUpdate.h): the newer launcher, downloaded and checked, waits here until nothing is running.
HANDLE singleInstance = nullptr;
fs::path launcherExe, readyLauncher;
uint64_t readyLauncherBuild = 0;
uint64_t skippedLauncherBuild = 0;   // a build that was installed but did not start: not tried again (launcher.json)
DWORD afterUpdateFrom = 0;           // --after-update <pid>: the launcher that just installed this one waits for our mark
std::thread selfUpdateThread;
struct LauncherUpdateResult { fs::path file; uint64_t build = 0; std::string error; bool stopAsked = false, stopFetched = false; RemoteStop stop; };
// The remote stop in force (RemoteStop.h): the saved, signature-checked list at start, then the site's.
RemoteStop remoteStop;
std::string switchingId;
std::atomic<int> switchPhase{0};   // 0 = checking, 1 = downloading, 2 = installing
std::atomic<unsigned long long> switchDone{0}, switchTotal{0};
struct SwitchResult { std::string id, error, warning; bool launchAfter = false; };

void ApplyClient();
void StartAccount(bool interactive);
void ReloadAvatar();
void RefreshVersions(const std::string& preferred);
void CheckForUpdates();
void PollSwitchProgress();
void StartSwitch(const std::string& id, bool launchAfter = false);
void SelectUserVersion(const std::string& id);
void SyncVersionCards();
void GoTo(int target);
LRESULT CALLBACK VersionSelectorProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK VersionListProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

ULONGLONG startedAt = 0;
// Launch pressed: the update list is fetched first, then the newest DLL / HUD is taken and the game started.
// (Before, Launch used the list fetched when the launcher opened, so a release made while it stayed open was missed.)
bool launchAfterCheck = false;
// Launch continues when Windows' normal-launch version could not be changed (2026-11-09): the note is shown with the launch result.
std::wstring launchNote;
// The release channel chosen for Launch (launcher.json "channel"; only stable unless the account may use another)
// and what /launcher/status last said about the logged-in account (nullopt = not logged in / not asked yet).
Channel wantedChannel = Channel::Stable;
std::optional<AccountStatus> accountStatus;
AccountEpoch accountEpoch;   // Logout() invalidates it: results that started before are not adopted (Channels.h)
StoredAccount storedAccount;   // account.json as last read / written (the home screen draws from it)
bool accountExpired = false;   // /launcher/status said 401 for the saved key: the login button is shown again
std::vector<HWND> editable;

// ---- Reskinned UI state (sidebar + cards). Behaviour of the controls is unchanged. ----
struct LogLine { std::wstring time, text; bool error = false, live = false; };
std::vector<LogLine> history;
int page = 0;                       // 0 = Home, 1 = Settings (below Home)
int settingsTab = 0;                // 0 = General, 1 = Version, 2 = Logs
constexpr int VersionCardsShown = 1, VersionCardControl = 300;
HWND versionCardControls[VersionCardsShown] = {};
WNDPROC nativeVersionSelectorProc = nullptr, nativeVersionListProc = nullptr;
std::vector<VersionFamilyRows> versionFamilies;
bool syncNormal = true;       // Settings > General: Launch / Versions cards also switch Windows' normal-launch version (user, 2026-11-09)
std::string normalVersionId; // What Windows starts normally: read with switch-game.ps1 -Mode query, or the last successful switch.
// Reading it runs PowerShell on a thread at startup and after every switch (NormalVersionMessage).
enum class NormalRead { Unknown, Reading, Known, Missing };
NormalRead normalRead = NormalRead::Unknown;
std::wstring normalReadNote;          // why it could not be read
bool normalQueryAgain = false;        // asked again while a read was running
std::thread normalQueryThread;
struct NormalQueryResult { std::string id, error; };
double scroll = 0, scrollFrom = 0;  // 0 = Home in view, 1 = Settings in view
ULONGLONG scrollStart = 0;
bool scrolling = false;
HFONT heroFont = nullptr, symFont = nullptr, titleFont = nullptr, greetFont = nullptr;
HFONT versionLabelFont = nullptr;
HICON logoIcon = nullptr;
std::wstring NowText()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t buffer[16];
    swprintf(buffer, 16, L"%02d:%02d", t.wHour, t.wMinute);
    return buffer;
}
void Refresh() { if (window) InvalidateRect(window, nullptr, FALSE); }
void PushLog(const std::wstring& text, bool error, bool live)
{
    if (live && !history.empty() && history.back().live)
    {
        history.back().text = text;
        history.back().time = NowText();
        return;
    }
    history.push_back({NowText(), text, error, live});
    if (history.size() > 60) history.erase(history.begin());
}

std::wstring Wide(const std::string& value)
{
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size || value.empty())
    {
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }
    // Not UTF-8: text written by Windows PowerShell / the system in the ANSI code page (CP932 on Japanese Windows),
    // or a UTF-8 text cut in the middle of a character. Show it anyway (2026-11-09: the install failure's real reason
    // was replaced by "文字列の形式が不正です" and the user could not see it); invalid bytes become U+FFFD.
    const int ansi = MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (ansi > 0)
    {
        std::wstring result(ansi, L'\0');
        MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), result.data(), ansi);
        return result;
    }
    const int lax = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (lax <= 0) return Lang::T(L"（文字を読み取れませんでした）", L"(could not read the text)");
    std::wstring result(lax, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), lax);
    return result;
}

fs::path KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &text)))
        throw std::runtime_error(Lang::T("Windowsのユーザーフォルダーを取得できません。", "Cannot get the Windows user folder."));
    const fs::path result(text);
    CoTaskMemFree(text);
    return result;
}

std::wstring Text(int id)
{
    HWND control = GetDlgItem(window, id);
    std::wstring result(GetWindowTextLengthW(control) + 1, L'\0');
    const int count = GetWindowTextW(control, result.data(), static_cast<int>(result.size()));
    result.resize(count);
    return result;
}

void Status(const std::wstring& text)
{
    statusError = false;
    for (const wchar_t* word : {L"できません", L"失敗", L"不正", L"拒否", L"Cannot", L"Could not", L"could not", L"failed", L"Failed",
                                L"nvalid", L"refused", L"denied"})
        statusError = statusError || text.find(word) != std::wstring::npos;
    SetWindowTextW(GetDlgItem(window, StatusText), text.c_str());
    PushLog(text, statusError, false);
    Refresh();
    std::ofstream log(privateDirectory / "launcher.log", std::ios::app | std::ios::binary);
    log << LogPrivacy::Redact(text) << '\n';   // the user's folders as %USERPROFILE% etc. (../LogPrivacy.h)
}

// launcher.log keeps only the last 3 runs of the launcher (2026-11-03 user request: it piled up). Each run starts with
// a "=== OriginX Launcher started ..." line (before 2026-11-05 "=== MyClient Launcher started ..."); a log from before these lines counts as one run.
void TrimLauncherLog()
{
    const auto path = privateDirectory / "launcher.log";
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::string marker = "=== OriginX Launcher started ";
    std::vector<size_t> starts;
    for (const std::string& head : {marker, std::string("=== MyClient Launcher started ")})
        for (size_t at = text.find(head); at != std::string::npos; at = text.find(head, at + 1))
            if (at == 0 || text[at - 1] == '\n') starts.push_back(at);
    std::sort(starts.begin(), starts.end());
    if (starts.empty() || starts.front() != 0) starts.insert(starts.begin(), 0);
    constexpr size_t keepOld = 2;   // plus this run = 3
    if (starts.size() > keepOld) text.erase(0, starts[starts.size() - keepOld]);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "%04d-%02d-%02d %02d:%02d:%02d ===\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += marker + stamp;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) out << text;
}

int Px(int value);
COLORREF HomeSky(int y, int height);
inline int Sc(int value) { return Px(static_cast<int>(value * 0.82 + 0.5)); }   // layout is authored at 1164x750

RECT ProgressBarArea()
{
    return RECT{0, 0, Sc(1164), Sc(750)};   // the whole window is repainted from one back buffer
}

// Status text without writing to launcher.log (progress updates arrive several times a second).
void StatusQuiet(const std::wstring& text)
{
    statusError = false;
    SetWindowTextW(GetDlgItem(window, StatusText), text.c_str());
    PushLog(text, false, true);
    Refresh();
}

void BeginProgress()
{
    std::error_code ignored;
    fs::remove(privateDirectory / "cache" / "progress.txt", ignored);
    progressPercent = 0;
    progressText.clear();
    const RECT area = ProgressBarArea();
    InvalidateRect(window, &area, TRUE);
}

void EndProgress()
{
    progressPercent = -1;
    const RECT area = ProgressBarArea();
    InvalidateRect(window, &area, TRUE);
}

// Reads "percent|phase|index/count|label|received/total" written by PrepareRuntime.ps1.
void PollProgress()
{
    const auto file = privateDirectory / "cache" / "progress.txt";
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    char buffer[2048];
    DWORD read = 0;
    const BOOL ok = ReadFile(handle, buffer, sizeof buffer - 1, &read, nullptr);
    CloseHandle(handle);
    if (!ok || !read) return;
    buffer[read] = '\0';
    std::vector<std::string> field;
    std::string current;
    for (const char* c = buffer; *c; ++c)
    {
        if (*c == '|') { field.push_back(current); current.clear(); }
        else current += *c;
    }
    field.push_back(current);
    if (field.size() < 2) return;
    const int percent = std::clamp(atoi(field[0].c_str()), 0, 100);
    const std::string& phase = field[1];
    const std::wstring index = field.size() > 2 ? Wide(field[2]) : L"";
    const std::wstring label = field.size() > 3 ? Wide(field[3]) : L"";
    const std::wstring detail = field.size() > 4 ? Wide(field[4]) : L"";
    std::wstring message;
    if (phase == "download")
        message = Lang::T(L"ダウンロード中 (", L"Downloading (") + index + L")\r\n" + label + (detail.empty() ? L"" : L"   " + detail + L" MB");
    else if (phase == "cached") message = Lang::T(L"保存済みのファイルを確認しました (", L"Checked the saved file (") + index + L")\r\n" + label;
    else if (phase == "extract") message = Lang::T(L"展開中\r\n", L"Extracting\r\n") + label;
    else if (phase == "activate") message = Lang::T(L"ゲーム本体にローダーを組み込んでいます。1分ほどかかることがあります。", L"Building the loader into the game. This can take about a minute.");
    else if (phase == "done") message = Lang::T(L"準備が完了しました。Minecraftを起動します…", L"Ready. Starting Minecraft…");
    else message = Lang::T(L"ダウンロードの準備をしています…", L"Preparing the download…");
    const std::wstring text = Lang::T(L"ローダーの準備中  ", L"Preparing the loader  ") + std::to_wstring(percent) + L"%\r\n" + message +
                              Lang::T(L"\r\n初回のみ時間がかかります。このウィンドウは閉じないでください。", L"\r\nOnly the first time takes long. Do not close this window.");
    if (percent == progressPercent && text == progressText) return;
    progressPercent = percent;
    progressText = text;
    StatusQuiet(text);
    const RECT area = ProgressBarArea();
    InvalidateRect(window, &area, TRUE);
}

// Game-copy progress: the copy thread publishes byte counters, the timer shows them.
void PollCopyProgress()
{
    const unsigned long long done = copyDone, total = copyTotal;
    const int percent = total ? static_cast<int>(std::min<unsigned long long>(100, done * 100 / total)) : 0;
    wchar_t size[96];
    if (total >= (1ull << 30))
        swprintf(size, 96, L"%.2f / %.2f GB", done / 1073741824.0, total / 1073741824.0);
    else
        swprintf(size, 96, L"%.0f / %.0f MB", done / 1048576.0, total / 1048576.0);
    const std::wstring text = Lang::T(L"Minecraftをコピー中  ", L"Copying Minecraft  ") + std::to_wstring(percent) + L"%\r\n" + size +
                              Lang::T(L"\r\nゲーム本体を専用フォルダーへコピーしています。初回のみ時間がかかります。\r\nこのウィンドウは閉じないでください。", L"\r\nCopying the game to its own folder. Only the first time takes long.\r\nDo not close this window.");
    if (percent == progressPercent && text == progressText) return;
    progressPercent = percent;
    progressText = text;
    StatusQuiet(text);
    const RECT area = ProgressBarArea();
    InvalidateRect(window, &area, TRUE);
}

void Busy(State next)
{
    state = next;
    for (HWND control : editable) EnableWindow(control, state == State::Idle);
    for (HWND control : versionCardControls) if (control) EnableWindow(control, state == State::Idle);
    SetWindowTextW(GetDlgItem(window, Apply), state == State::Idle ? L"Launch" : L"Working\u2026");
    Refresh();
    if (state == State::Idle && !readyLauncher.empty()) PostMessageW(window, ApplyLauncherMessage, 0, 0);
}

// Signature checks of the lists from the site (ManifestSignature.h). Names and results only, never list contents.
void SignatureLog(const std::string& text)
{
    if (privateDirectory.empty()) return;
    std::ofstream log(privateDirectory / "launcher.log", std::ios::app | std::ios::binary);
    if (log) log << "Signature: " << text << '\n';
}
std::string FileBytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
fs::path SigPath(const fs::path& list) { return fs::path(list.native() + L".sig"); }
void StopForSignature(const std::string& what, const SignatureResult& result)
{
    signatureStopped = true;
    SignatureLog(what + ": " + SignatureCheckName(result.check) + " (key " + std::to_string(result.keyId) + "); updates and switching stopped");
}
// Downloads a list and its .sig into dest and dest.sig. Returns only when the signature is good (or this launcher
// was made without keys, SignaturesRequired); otherwise both files are removed and the user's message is thrown.
void DownloadSignedList(const std::string& url, const std::string& kind, const fs::path& dest, uint64_t limit)
{
    DownloadHttps(url, dest, limit);
    const auto keys = EmbeddedSigningKeys();
    if (!SignaturesRequired(keys)) return;
    std::string sig;
    try { DownloadHttps(url + ".sig", SigPath(dest), 4096); sig = FileBytes(SigPath(dest)); }
    catch (const std::exception&) { /* no .sig (or it could not be read): checked as missing below */ }
    const auto result = VerifyManifest(kind, FileBytes(dest), sig, keys, MinKeyFor(kind));
    if (!result.ok())
    {
        std::error_code ignored;
        fs::remove(dest, ignored);
        fs::remove(SigPath(dest), ignored);
        StopForSignature(kind + " from the site", result);
        throw std::runtime_error(SignatureFailedMessage());
    }
    NoteSigningKey(result.keyId);
}
// A list saved on this PC is checked again each time it is read (a changed file is refused too). One that fails
// (also one saved by build 40 or older, which kept no .sig) is only not used: it does not stop updates, so the
// next list from the site, checked as it is downloaded, replaces it.
bool SavedListVerified(const fs::path& list, const std::string& kind)
{
    const auto keys = EmbeddedSigningKeys();
    if (!SignaturesRequired(keys)) return true;
    std::error_code ignored;
    const std::string sig = fs::is_regular_file(SigPath(list), ignored) ? FileBytes(SigPath(list)) : std::string{};
    const auto result = VerifyManifest(kind, FileBytes(list), sig, keys, MinKeyFor(kind));
    if (result.ok()) { NoteSigningKey(result.keyId); return true; }
    SignatureLog(kind + " saved on this PC: " + SignatureCheckName(result.check) + " (key " + std::to_string(result.keyId) + "); not used");
    return false;
}
// Moves a checked download (and its .sig) over the saved list.
void KeepSignedList(const fs::path& download, const fs::path& saved, const char* failure)
{
    std::error_code ignored;
    if (fs::is_regular_file(SigPath(download), ignored) &&
        !MoveFileExW(SigPath(download).c_str(), SigPath(saved).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error(failure);
    if (!MoveFileExW(download.c_str(), saved.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error(failure);
}
// Package downloads stop while a list failed its check; a package already saved and checked is still used.
void DownloadUnlessStopped(const std::string& url, const fs::path& destination, uint64_t limit)
{
    if (signatureStopped) throw std::runtime_error(SignatureFailedMessage());
    DownloadHttps(url, destination, limit);
}

// Downloads the launcher an update list named and checks its size, SHA256 and that it is an x64 program.
void DownloadNewLauncher(const LauncherInfo& info, const fs::path& folder, LauncherUpdateResult& result)
{
    fs::create_directories(folder);
    const auto file = folder / "MyClientLauncher.exe";
    DownloadHttps(info.url, file, info.bytes);
    if (fs::file_size(file) != info.bytes || Sha256File(file) != info.sha256)
        throw std::runtime_error(Lang::T("ダウンロードした新しいランチャーの SHA256 が一致しません。", "The SHA256 of the downloaded new launcher does not match."));
    RequireX64Pe(file);
    result.file = file;
    result.build = info.build;
}


// Remote stop lines in launcher.log (no list contents beyond what is stopped and why).
void RemoteStopLog(const std::string& text)
{
    if (privateDirectory.empty()) return;
    std::ofstream log(privateDirectory / "launcher.log", std::ios::app | std::ios::binary);
    if (log) log << "INFO Remote stop: " << text << '\n';
}
// The launcher list last fetched and checked (with .sig). Not "launcher.json": that name is the settings file.
fs::path SavedLauncherList() { return privateDirectory / "launcher-list.json"; }
void NoteRemoteStop(const char* source)
{
    if (remoteStop.testKeysFrom)
    {
        NoteSigningKey(remoteStop.testKeysFrom);   // a revoked test key is refused from now on (ManifestSignature.h)
        RemoteStopLog(std::string(source) + ": test keys below " + std::to_string(remoteStop.testKeysFrom) + " revoked");
    }
    const auto cause = StopFor(remoteStop, LauncherBuild);
    RemoteStopLog(std::string(source) + ": " + StopCauseName(cause) + " (build " + std::to_string(LauncherBuild) + ")");
}
// At start: the saved list, only when its signature still checks (a changed or unsigned file is not used).
void LoadSavedRemoteStop()
{
    std::error_code ignored;
    if (!fs::is_regular_file(SavedLauncherList(), ignored)) { RemoteStopLog("no saved stop information yet"); return; }
    try
    {
        if (!SavedListVerified(SavedLauncherList(), "launcher/stable")) { RemoteStopLog("the saved stop information has no valid signature; not used"); return; }
        remoteStop = ParseRemoteStop(ReadJson(SavedLauncherList()));
        NoteRemoteStop("saved list");
    }
    catch (const std::exception& error) { remoteStop = {}; RemoteStopLog(std::string("the saved stop information could not be read; not used: ") + error.what()); }
}
// The user's text for a stop in force, empty when nothing is stopped.
std::wstring RemoteStopText()
{
    const auto cause = StopFor(remoteStop, LauncherBuild);
    return cause == StopCause::None ? std::wstring{} : Wide(StopMessage(remoteStop, cause, LauncherBuild, Lang::English()));
}

// Asks the site for the launcher list at every start (in the background): the remote stop (RemoteStop.h) and,
// when this launcher has a build number, whether a newer launcher is out (then downloads and checks it).
void CheckLauncherUpdate()
{
    if (launcherExe.empty() || signatureStopped)
    {
        if (signatureStopped) RemoteStopLog("could not get the stop information (signature checks stopped); the saved one stays");
        return;
    }
    selfUpdateThread = std::thread([skipped = skippedLauncherBuild] {
        auto result = new LauncherUpdateResult;
        result->stopAsked = true;   // this run fetches the stop information (other launcher downloads do not)
        const auto list = privateDirectory / "launcher.json.download";
        const auto folder = privateDirectory / "launcher-update";
        try
        {
            DownloadSignedList(LauncherListUrl, "launcher/stable", list, 64 * 1024);
            const auto doc = ReadJson(list);
            const auto info = ParseLauncherInfo(doc);
            result->stop = ParseRemoteStop(doc);
            result->stopFetched = true;
            KeepSignedList(list, SavedLauncherList(), Lang::T("ランチャーの一覧を保存できません。", "Cannot save the launcher list."));
            if (LauncherBuild && LauncherUpdateWanted(LauncherBuild, info, skipped)) DownloadNewLauncher(info, folder, *result);
        }
        catch (const std::exception& error) { result->error = error.what(); }
        std::error_code ignored; fs::remove(list, ignored); fs::remove(SigPath(list), ignored);
        if (!PostMessageW(window, LauncherUpdateMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

// The test channel's launcher (SelfUpdate.h ParseTestLauncherInfo): only while the test channel is chosen and the
// account's status carried a list whose signature (launcher/test) and channel ("test") were checked. Never for
// stable or early, so a test build does not reach them. Runs when no other launcher download is going on.
void CheckTestLauncherUpdate()
{
    if (!LauncherBuild || launcherExe.empty() || signatureStopped || wantedChannel != Channel::Test || !accountStatus ||
        !accountStatus->testLauncher || selfUpdateThread.joinable() || !readyLauncher.empty())
        return;
    const LauncherInfo info = *accountStatus->testLauncher;
    if (!LauncherUpdateWanted(LauncherBuild, info, skippedLauncherBuild)) return;
    selfUpdateThread = std::thread([info] {
        auto result = new LauncherUpdateResult;
        try { DownloadNewLauncher(info, privateDirectory / "launcher-update", *result); }
        catch (const std::exception& error) { result->error = error.what(); }
        if (!PostMessageW(window, LauncherUpdateMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

// Puts the downloaded launcher in this one's place and starts it. Only while nothing is being prepared.
void Save();
void ApplyLauncherUpdate()
{
    if (readyLauncher.empty() || state != State::Idle) return;
    const auto file = readyLauncher;
    const auto build = readyLauncherBuild;
    readyLauncher.clear();
    const auto staged = fs::path(launcherExe.native() + L".new");
    try
    {
        std::error_code ec;
        fs::copy_file(file, staged, fs::copy_options::overwrite_existing, ec);   // the cache may be on another drive
        if (ec) throw std::runtime_error(Lang::T("新しいランチャーを置けません（フォルダーに書き込めない可能性があります）: ", "Cannot place the new launcher (the folder may not be writable): ") + ec.message());
        SwapLauncher(launcherExe, staged);
    }
    catch (const std::exception& error)
    {
        std::error_code ignored; fs::remove(staged, ignored);
        Status(Lang::T(L"ランチャーを自動で更新できませんでした。サイトから新しいランチャーを入れ直してください。\r\n", L"Could not update the launcher automatically. Reinstall the new launcher from the site.\r\n") + Wide(error.what()));
        return;
    }
    Status(Lang::T(L"ランチャーを build ", L"Updated the launcher to build ") + std::to_wstring(build) + Lang::T(L" に更新しました。再起動します…", L". Restarting…"));
    // Hand-over (design 2026-11-20): this launcher keeps the single-instance lock until the new one says it is up
    // (StartedMarkPath). If it cannot be started, ends first, or gives no mark within 10 s, the previous
    // launcher is put back and this one goes on.
    const DWORD self = GetCurrentProcessId();
    const auto mark = StartedMarkPath(privateDirectory, self);
    std::error_code ignored;
    fs::remove(mark, ignored);
    std::wstring command = L"\"" + launcherExe.wstring() + L"\" --after-update " + std::to_wstring(self);
    STARTUPINFOW startup{}; startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    HandOver outcome = HandOver::Failed;
    if (CreateProcessW(launcherExe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       launcherExe.parent_path().c_str(), &startup, &process))
    {
        CloseHandle(process.hThread);
        const ULONGLONG started = GetTickCount64();
        for (;;)
        {
            const bool ended = WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0;
            outcome = HandOverState(fs::exists(mark, ignored), ended, GetTickCount64() - started);
            if (outcome != HandOver::Waiting) break;
        }
        if (outcome == HandOver::Failed && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT)
        {
            TerminateProcess(process.hProcess, 1);   // it never took the lock, so nothing is half done
            WaitForSingleObject(process.hProcess, 5000);
        }
        CloseHandle(process.hProcess);
    }
    fs::remove(mark, ignored);
    if (outcome == HandOver::Ready)
    {
        if (singleInstance) { CloseHandle(singleInstance); singleInstance = nullptr; }   // the new one takes it now
        DestroyWindow(window);
        return;
    }
    skippedLauncherBuild = build;   // the next build is tried again
    try
    {
        RollbackLauncher(launcherExe);
        Save();
        Status(Lang::T(L"ランチャーの更新（build ", L"Could not apply the launcher update (build ") + std::to_wstring(build) +
               Lang::T(L"）を適用できなかったため、前の版に戻しました。今の版のまま使えます。次回の更新で再試行します。",
                       L"), so the previous version was put back. You can keep using this version. The next update will be tried again."));
    }
    catch (const std::exception& error)
    {
        try { Save(); } catch (...) {}
        Status(Lang::T(L"ランチャーの更新（build ", L"Could not apply the launcher update (build ") + std::to_wstring(build) +
               Lang::T(L"）を適用できず、前の版にも戻せませんでした: ", L") and could not put the previous version back: ") + Wide(error.what()));
    }
}

void Save()
{
    sources[selected.target.id] = fs::path(Text(Game)).u8string();
    Json config{{"sources", sources}, {"selectedVersion", selected.target.id}, {"imports", imports}, {"xboxSeen", xboxSeen}, {"channel", ChannelKey(wantedChannel)}, {"syncNormal", syncNormal}, {"skippedLauncherBuild", skippedLauncherBuild}, {"minSigningKey", minSigningKeyId.load()}, {"minTestSigningKey", minTestSigningKeyId.load()}, {"schemaVersion", 3}};
    const auto temporary = privateDirectory / "launcher.json.tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << config.dump(2);
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), (privateDirectory / "launcher.json").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error(Lang::T("ランチャー設定を保存できません。", "Cannot save the launcher settings."));
}

// Remember what /launcher/status said about the account: the home screen reads accountStatus, and the renewed
// login key (the Worker renews it when under 30 days are left) and the name / icon link are kept on this PC.
void NoteAccountStatus(const AccountStatus& status)
{
    accountStatus = status;
    accountExpired = false;
    if (!status.signatureProblem.empty())
    {
        StopForSignature("channel list (" + status.signatureProblem + ")", SignatureResult{});
        PushLog(Wide(SignatureFailedMessage()), true, false);
    }
    StoredAccount stored = LoadAccount(privateDirectory);
    if (!status.token.empty()) stored.token = status.token;
    if (status.verified) stored.verifiedAt = static_cast<long long>(std::time(nullptr));
    stored.name = status.name;
    stored.avatarUrl = status.avatarUrl;
    storedAccount = stored;
    try { if (!stored.token.empty()) SaveAccount(privateDirectory, stored); } catch (const std::exception&) { /* the old key still works */ }
}

// Versions only the chosen test/early channel has (e.g. 26.45 before it reaches stable, 2026-11-14) are added to the
// version list while that channel is chosen and the account may use it, so they can be picked; Launch takes them
// through PlanLaunch like any channel build. Choosing stable again takes them out.
std::set<std::string> channelOnlyIds;
std::wstring channelVersionsNote;
void VersionDiagnostic(const std::string& text)
{
    if (privateDirectory.empty()) return;
    std::ofstream log(privateDirectory / "launcher.log", std::ios::app | std::ios::binary);
    if (log) log << "Versions: " << LogPrivacy::Redact(text) << '\n'; // public ids / booleans only, never signed URLs or account payloads
}
std::vector<Release> replacedStable;   // stable rows a channel release replaced (MergeChannelVersions)
void MergeChannelVersions()
{
    channelVersionsNote.clear();
    auto& releases = catalog.releases;
    releases.erase(std::remove_if(releases.begin(), releases.end(),
                                  [](const Release& r) { return channelOnlyIds.count(r.target.id) != 0; }), releases.end());
    channelOnlyIds.clear();
    // Stable entries that were replaced by a channel's available release come back first (channel change/reload).
    // Launcher 32 bug: after a catalogue reload (fresh stable list) the old originals were added again as
    // duplicates, and the switch found the grey duplicate first. Put back only ids that are not listed.
    for (auto& original : replacedStable)
        if (std::none_of(releases.begin(), releases.end(), [&](const Release& r) { return r.target.id == original.target.id; }))
            releases.push_back(std::move(original));
    replacedStable.clear();
    VersionDiagnostic(std::string("merge channel=") + ChannelName(wantedChannel) + " account=" + (accountStatus ? "1" : "0") +
        " test=" + (accountStatus && !accountStatus->test.is_null() ? "1" : "0") +
        " early=" + (accountStatus && !accountStatus->early.is_null() ? "1" : "0"));
    if (wantedChannel == Channel::Stable || !accountStatus) {
        if (wantedChannel != Channel::Stable) channelVersionsNote = Lang::T(L"test/early一覧をまだ取得できていません。ログイン状態を確認してください。", L"The test/early list has not been fetched yet. Check your login.");
        VersionDiagnostic(wantedChannel == Channel::Stable ? "skip: stable channel" : "skip: account status not ready");
        return;
    }
    const Json& doc = wantedChannel == Channel::Early ? accountStatus->early : accountStatus->test;
    if (doc.is_null()) { channelVersionsNote = Lang::T(L"選択中のtest/early一覧がありません。接続と利用権限を確認してください。", L"There is no list for the chosen test/early. Check your connection and permissions."); VersionDiagnostic("skip: selected channel catalogue missing"); return; }
    Catalog extra;
    try { extra = ParseCatalog(doc, SignedFileUrl); }
    catch (const std::exception&) { channelVersionsNote = Lang::T(L"選択中のtest/early一覧を読み取れません。launcher.logを確認してください。", L"Cannot read the chosen test/early list. Check launcher.log."); VersionDiagnostic("skip: selected channel catalogue parse failed"); return; }
    for (const auto& release : extra.releases) {
        if (!release.available) { VersionDiagnostic("skip id=" + release.target.id + " reason=release unavailable"); continue; }
        const auto listed = std::find_if(releases.begin(), releases.end(), [&](const Release& r) { return r.target.id == release.target.id; });
        if (listed != releases.end()) {
            // launcher 31 log: 26.45.1 is in the stable catalogue as "not supported", so the test channel's
            // supported release was skipped and the row stayed grey. An unavailable stable entry is replaced.
            if (listed->available) { VersionDiagnostic("skip id=" + release.target.id + " reason=already listed"); continue; }
            replacedStable.push_back(*listed);
            releases.erase(listed);
            VersionDiagnostic("replaced unavailable stable id=" + release.target.id);
        }
        releases.push_back(release);
        channelOnlyIds.insert(release.target.id);
        VersionDiagnostic("added id=" + release.target.id);
    }
}

void Pick(int id)
{
    std::vector<wchar_t> path(32768);
    const std::wstring existing = Text(id);
    if (existing.size() < path.size()) std::copy(existing.begin(), existing.end(), path.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = window;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrFilter = Lang::T(L"Windowsプログラム (*.exe)\0*.exe\0\0", L"Windows programs (*.exe)\0*.exe\0\0");
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const auto title = L"Minecraft " + Wide(selected.target.id) + Lang::T(L"のMinecraft.Windows.exe", L" Minecraft.Windows.exe");
    dialog.lpstrTitle = title.c_str();
    if (GetOpenFileNameW(&dialog))
    {
        SetWindowTextW(GetDlgItem(window, id), path.data());
        Save();
        Status(Lang::T(L"選択を保存しました。「適用して起動」で導入先を確認して起動します。", L"Saved the selection. \"Apply & Launch\" checks the install location and launches."));
    }
}

HANDLE Start(const fs::path& executable, const std::vector<std::wstring>& arguments,
             const fs::path& directory, const fs::path& logPath = {})
{
    std::wstring command = QuoteArg(executable.wstring());
    for (const auto& argument : arguments) command += L" " + QuoteArg(argument);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    HANDLE log = INVALID_HANDLE_VALUE, nullInput = INVALID_HANDLE_VALUE;
    struct LogGuard {
        HANDLE& log; HANDLE& input;
        ~LogGuard() { if (log != INVALID_HANDLE_VALUE) CloseHandle(log); if (input != INVALID_HANDLE_VALUE) CloseHandle(input); }
    } logGuard{log, nullInput};
    if (!logPath.empty())
    {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        if (log == INVALID_HANDLE_VALUE || nullInput == INVALID_HANDLE_VALUE)
            throw std::runtime_error(Lang::T("準備処理のログを作成できません。", "Cannot create the preparation log."));
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = startup.hStdError = log;
        startup.hStdInput = nullInput;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, !logPath.empty(),
                        logPath.empty() ? 0 : CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
        throw std::runtime_error(Lang::T("プログラムを起動できません。Windowsエラー: ", "Cannot start the program. Windows error: ") + std::to_string(GetLastError()));
    CloseHandle(process.hThread);
    return process.hProcess;
}

// Match the executable's directory, including console builds.
// Denied access to a Minecraft process blocks replacement rather than assuming
// the game is stopped. Other accessible Minecraft instances are ignored.
DWORD GamePid(const fs::path& directory)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error(Lang::T("起動中のゲームを確認できません。", "Cannot check the running game."));
    struct SnapshotGuard { HANDLE handle; ~SnapshotGuard() { CloseHandle(handle); } } guard{snapshot};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    DWORD found = 0;
    if (!Process32FirstW(snapshot, &entry)) throw std::runtime_error(Lang::T("起動中のゲーム一覧を取得できません。", "Cannot get the list of running games."));
    do
    {
        const auto name = Lower(fs::path(entry.szExeFile).u8string());
        if (name != "minecraft.windows.exe" && name != "minecraft.windows.console.exe") continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process)
        {
            if (GetLastError() == ERROR_INVALID_PARAMETER) continue; // Already exited.
            throw std::runtime_error(Lang::T("Minecraftの確認に失敗しました。ゲームを終了して再試行してください。", "Checking Minecraft failed. Close the game and try again."));
        }
        std::vector<wchar_t> path(32768);
        DWORD length = static_cast<DWORD>(path.size());
        const bool read = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess(process, &exitCode);
        CloseHandle(process);
        if (!read)
        {
            if (exitCode != STILL_ACTIVE) continue;
            throw std::runtime_error(Lang::T("Minecraftの実行パスを取得できません。ゲームを終了して再試行してください。", "Cannot get Minecraft's program path. Close the game and try again."));
        }
        std::error_code error;
        if (fs::equivalent(fs::path(std::wstring(path.data(), length)).parent_path(), directory, error))
        {
            if (found) throw std::runtime_error(Lang::T("同じ環境のMinecraftが複数起動しています。1つにしてください。", "Several Minecraft instances of the same install are running. Keep only one."));
            found = entry.th32ProcessID;
        }
        else if (error) throw std::runtime_error(Lang::T("Minecraftの導入先を比較できません。", "Cannot compare Minecraft install locations."));
    } while (Process32NextW(snapshot, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        throw std::runtime_error(Lang::T("ゲーム一覧の確認が途中で失敗しました。再試行してください。", "Checking the game list failed partway. Try again."));
    return found;
}

struct WindowSearch { DWORD pid; HWND found; };
BOOL CALLBACK FindGameWindow(HWND handle, LPARAM parameter)
{
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(handle, &pid);
    wchar_t title[256]{};
    GetWindowTextW(handle, title, 256);
    if (pid == search.pid && IsWindowVisible(handle) && std::wstring(title) == L"Minecraft")
    {
        search.found = handle;
        return FALSE;
    }
    return TRUE;
}

bool Ready(DWORD pid)
{
    if (!pid) return false;
    WindowSearch search{pid, nullptr};
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.found) return false;
    MyClientIpc::Channel channel;
    if (!channel.Open(pid, false)) return false;
    const auto locked = channel.Lock(0);
    if (locked == WAIT_ABANDONED) { channel.Unlock(); return false; }
    if (locked != WAIT_OBJECT_0) return false;
    const bool valid = MyClientIpc::HasValidHeader(*channel.Data(), pid);
    channel.Unlock();
    return valid;
}

fs::path HudExecutable()
{
    return PlaceHud(packageRoot, privateDirectory / "hud", bundle, packageLocal, Sha256File);
}

// PrepareRuntime.ps1 (fetches the loader): beside the launcher when shipped in a folder, otherwise the copy
// built into the launcher, written to the private folder.
fs::path RuntimeScript()
{
    if (fs::is_regular_file(bundle / "PrepareRuntime.ps1")) return bundle / "PrepareRuntime.ps1";
    HRSRC found = FindResourceW(nullptr, L"PREPARERUNTIME", MAKEINTRESOURCEW(10));
    HGLOBAL loaded = found ? LoadResource(nullptr, found) : nullptr;
    const char* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    const DWORD size = found ? SizeofResource(nullptr, found) : 0;
    if (!bytes || !size) throw std::runtime_error(Lang::T("ローダーの準備スクリプト（PrepareRuntime.ps1）が見つかりません。", "The loader preparation script (PrepareRuntime.ps1) was not found."));
    const std::string wanted(bytes, size);
    const auto path = privateDirectory / "PrepareRuntime.ps1";
    std::string current;
    if (fs::is_regular_file(path))
    {
        std::ifstream in(path, std::ios::binary);
        current.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (current != wanted)
    {
        fs::create_directories(privateDirectory);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(wanted.data(), static_cast<std::streamsize>(wanted.size()));
        if (!out) throw std::runtime_error(Lang::T("ローダーの準備スクリプトを書き込めません。", "Cannot write the loader preparation script."));
    }
    return path;
}

void StartHud(DWORD pid)
{
    if (hudProcess)
    {
        if (WaitForSingleObject(hudProcess, 0) == WAIT_TIMEOUT && hudGamePid == pid)
        {
            Status(Lang::T(L"HUDは既に起動しています。ゲームでGを押して設定を開いてください。", L"The HUD is already running. Press G in the game to open settings."));
            Busy(State::Idle);
            return;
        }
        CloseHandle(hudProcess);
        hudProcess = nullptr;
    }
    const auto hud = HudExecutable();
    hudProcess = Start(hud, {L"--minecraft-pid=" + std::to_wstring(pid)}, hud.parent_path());
    hudGamePid = pid;
    Busy(State::Idle);
    Status(Lang::T(L"DLLへの接続を確認し、HUDを起動しました。\r\nワールドに入り、G → Arrow Counter / Block OutlineをONにしてください。", L"Connected to the DLL and started the HUD.\r\nJoin a world, then press G and turn on Arrow Counter / Block Outline.") +
           (launchNote.empty() ? std::wstring{} : L"\r\n" + launchNote));
}

// Developer diagnostics of the DLL (client/GameImageDump.cpp, SignatureDump.cpp: the game image and the real
// addresses of the functions it looks up) run only when ORIGINX_DIAGNOSTICS=1 was set before the game started and
// the folder %LOCALAPPDATA%\\MyClient\\dumps exists. Making that folder is the user's whole part: the launcher
// then sets the variable for the game it starts (a child inherits it). Without the folder nothing changes.
void DiagnosticsEnvironment()
{
    std::error_code error;
    const bool wanted = fs::is_directory(privateDirectory / "dumps", error);
    SetEnvironmentVariableW(L"ORIGINX_DIAGNOSTICS", wanted ? L"1" : nullptr);
    if (wanted) PushLog(Lang::T(L"診断モードで起動します（dumps フォルダーがあるため）。結果は ", L"Starting in diagnostic mode (there is a dumps folder). Results are written to ") + (privateDirectory / "dumps").wstring() + Lang::T(L" に書かれます。", L"."), false, false);
}

// Remote stop in force (RemoteStop.h): OriginX is not injected or loaded, Minecraft itself is still started
// (the untouched exe of the chosen standalone version, else Windows' normal install), and the reason is shown.
// Returns false when nothing is stopped.
DWORD GamePidAny();
bool LaunchWithoutOriginX(bool standalone, const fs::path& directory)
{
    const auto text = RemoteStopText();
    if (text.empty()) return false;
    RemoteStopLog(std::string("launch without OriginX: ") + StopCauseName(StopFor(remoteStop, LauncherBuild)));
    std::wstring started;
    if (!GamePidAny())
    {
        if (standalone && !directory.empty())
        {
            UseOriginalExe(directory);
            if (gameProcess) { CloseHandle(gameProcess); gameProcess = nullptr; }
            CloseHandle(Start(directory / "Minecraft.Windows.exe", {}, directory));
        }
        else
        {
            const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open",
                L"shell:AppsFolder\\Microsoft.MinecraftUWP_8wekyb3d8bbwe!Game", nullptr, nullptr, SW_SHOWNORMAL));
            if (result <= 32) started = Lang::T(L"\r\nMinecraftを起動できませんでした。Xboxアプリから起動してください。", L"\r\nCould not start Minecraft. Start it from the Xbox app.");
        }
    }
    Busy(State::Idle);
    std::wstring shown = text;
    for (size_t at = 0; (at = shown.find(L'\n', at)) != std::wstring::npos; at += 2)
        if (at == 0 || shown[at - 1] != L'\r') shown.insert(at, 1, L'\r');
    Status(shown + started);
    return true;
}

void LaunchGame()
{
    DiagnosticsEnvironment();
    if (LaunchWithoutOriginX(false, {})) return;
    RequireExecutableVersion(pending.directory / "Minecraft.Windows.exe", launchTarget);
    RequireRuntime(pending.directory, packageRoot / "runtime.lock.json", launchTarget);
    const DWORD pid = GamePid(pending.directory);
    if (pid)
    {
        if (!Ready(pid))
            throw std::runtime_error(Lang::T("Minecraftが起動中です。DLLを導入するには一度ゲームを終了してください。", "Minecraft is running. Close the game once to install the DLL."));
        StartHud(pid);
        return;
    }
    const auto backup = InstallMod(packageRoot / "mods/MyClientData", pending.directory, launchTarget);
    if (!backup.empty()) Status(Lang::T(L"以前のMODをバックアップしました:\r\n", L"Backed up your previous mods:\r\n") + backup.wstring());
    if (gameProcess) { CloseHandle(gameProcess); gameProcess = nullptr; }
    // The official PeEditor/PreLoader prepare the managed executable; no
    // external launcher, protocol activation or registration lookup is used.
    gameProcess = Start(pending.directory / "Minecraft.Windows.exe", {}, pending.directory);
    startedAt = GetTickCount64();
    Busy(State::Waiting);
    Status(Lang::T(L"Minecraftを直接起動しました。DLLへの接続を待っています…\r\n接続を確認するとHUDを自動で起動します。", L"Started Minecraft directly. Waiting for the DLL to connect…\r\nThe HUD starts automatically once connected.") +
           (launchNote.empty() ? std::wstring{} : L"\r\n" + launchNote));
}

// Any running Minecraft.Windows.exe, regardless of its install folder.
DWORD GamePidAny()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error(Lang::T("起動中のゲームを確認できません。", "Cannot check the running game."));
    struct SnapshotGuard { HANDLE handle; ~SnapshotGuard() { CloseHandle(handle); } } guard{snapshot};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    DWORD found = 0;
    if (!Process32FirstW(snapshot, &entry)) throw std::runtime_error(Lang::T("起動中のゲーム一覧を取得できません。", "Cannot get the list of running games."));
    do
    {
        if (_wcsicmp(entry.szExeFile, L"Minecraft.Windows.exe") != 0) continue;
        if (found) throw std::runtime_error(Lang::T("Minecraftが複数起動しています。1つにしてください。", "Several Minecraft instances are running. Keep only one."));
        found = entry.th32ProcessID;
    } while (Process32NextW(snapshot, &entry));
    return found;
}

struct InjectResult { bool ok = false; std::string message; };

// Classic LoadLibraryW remote thread. Returns ok only if LoadLibraryW returned a module.
InjectResult InjectDll(DWORD pid, const fs::path& dll)
{
    InjectResult result;
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                 PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) { result.message = Lang::T("プロセスを開けません (Windowsエラー ", "Cannot open the process (Windows error ") + std::to_string(GetLastError()) + ")"; return result; }
    struct Guard { HANDLE h; ~Guard() { CloseHandle(h); } } guard{process};
    const std::wstring path = dll.wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { result.message = Lang::T("メモリ確保に失敗 (Windowsエラー ", "Memory allocation failed (Windows error ") + std::to_string(GetLastError()) + ")"; return result; }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, path.c_str(), bytes, &written) || written != bytes)
    {
        result.message = Lang::T("パスの書き込みに失敗 (Windowsエラー ", "Writing the path failed (Windows error ") + std::to_string(GetLastError()) + ")";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return result;
    }
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
    HANDLE thread = loadLibrary ? CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr) : nullptr;
    if (!thread)
    {
        result.message = Lang::T("スレッド作成に失敗 (Windowsエラー ", "Creating the thread failed (Windows error ") + std::to_string(GetLastError()) + ")";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return result;
    }
    const DWORD wait = WaitForSingleObject(thread, 30000);
    DWORD module = 0;
    GetExitCodeThread(thread, &module);
    CloseHandle(thread);
    if (wait == WAIT_OBJECT_0) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    if (wait != WAIT_OBJECT_0) result.message = Lang::T("LoadLibraryが30秒で終わりませんでした", "LoadLibrary did not finish in 30 seconds");
    else if (!module) result.message = Lang::T("LoadLibraryWが失敗しました。依存DLLの不足、またはファイルのアクセス権を確認してください", "LoadLibraryW failed. Check for missing dependent DLLs or file permissions");
    else result.ok = true;
    return result;
}

// A release that runs without LeviLamina (StandaloneWin.h): the untouched exe is started suspended, the DLL of
// the package is loaded into it, then the game goes on. Nothing is installed into the game's folder.
fs::path StandaloneDll() { return packageRoot / "mods/MyClientData/MyClientData.dll"; }

void LaunchStandalone()
{
    DiagnosticsEnvironment();
    if (LaunchWithoutOriginX(true, pending.directory)) return;
    const DWORD pid = GamePid(pending.directory);
    if (pid)
    {
        if (!Ready(pid))
            throw std::runtime_error(Lang::T("Minecraftが起動中です。DLLを読み込むには一度ゲームを終了してください。", "Minecraft is running. Close the game once to load the DLL."));
        StartHud(pid);
        return;
    }
    RequireStandaloneDll(StandaloneDll());
    UseOriginalExe(pending.directory);
    RequireExecutableVersion(pending.directory / "Minecraft.Windows.exe", launchTarget);
    if (gameProcess) { CloseHandle(gameProcess); gameProcess = nullptr; }
    const auto started = StartSuspendedWithDll(pending.directory / "Minecraft.Windows.exe", pending.directory, StandaloneDll(),
        [](DWORD target, const fs::path& dll) { const auto result = InjectDll(target, dll); return result.ok ? std::string{} : result.message; });
    gameProcess = started.process;
    startedAt = GetTickCount64();
    Busy(State::Waiting);
    Status(std::wstring(Lang::T(L"Minecraftを起動し、OriginXのDLLを読み込みました（LeviLaminaなし）。DLLへの接続を待っています…", L"Started Minecraft and loaded the OriginX DLL (without LeviLamina). Waiting for the DLL to connect…")) +
           (started.loadedSignal ? L"" : Lang::T(L"\r\nDLLからの開始の合図がまだありません。接続できない場合はログを確認してください。", L"\r\nNo start signal from the DLL yet. If it does not connect, check the log.")) +
           (launchNote.empty() ? std::wstring{} : L"\r\n" + launchNote));
}

// UWP/GDK processes cannot always load DLLs from a user folder; allow app packages to read it.
void GrantAppPackageAccess(const fs::path& directory)
{
    std::vector<wchar_t> systemPath(32768);
    const UINT length = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
    if (!length || length >= systemPath.size()) return;
    const auto icacls = fs::path(std::wstring(systemPath.data(), length)) / "icacls.exe";
    HANDLE process = Start(icacls, {directory.wstring(), L"/grant", L"*S-1-15-2-1:(OI)(CI)(RX)",
                                    L"/grant", L"*S-1-15-2-2:(OI)(CI)(RX)", L"/T", L"/C", L"/Q"},
                           directory, privateDirectory / "icacls.log");
    WaitForSingleObject(process, 60000);
    CloseHandle(process);
}

// Checked just before injecting into a running Minecraft (GameImageCheck.h): its image in memory against the
// package's supported.json. Ok: inject. Retry: the image could not be read yet (a process that has just started).
enum class GameCheck { Ok, Retry, Unsupported };
GameCheck CheckRunningGame(DWORD pid, std::string& message)
{
    const auto tablePath = packageRoot / SupportedFile;
    SupportedImages table;
    try { table = ParseSupportedImages(ReadJson(tablePath)); }
    catch (const std::exception& error)
    {
        std::error_code ignored;
        message = UnsupportedGameMessage(nullptr, launchTarget.id, fs::exists(tablePath, ignored) ? error.what() :
            Lang::T("このパッケージには対応版の表（supported.json）がありません。「更新を確認」してからもう一度起動してください",
                    "this package has no supported version table (supported.json). Check for updates, then launch again"));
        return GameCheck::Unsupported;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) { message = Lang::T("起動中のMinecraftを読めません (", "Cannot read the running Minecraft (") + std::to_string(GetLastError()) + ")"; return GameCheck::Retry; }
    struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{process};
    HMODULE first = nullptr;
    DWORD needed = 0;
    if (!K32EnumProcessModulesEx(process, &first, sizeof first, &needed, LIST_MODULES_64BIT) || !first)
    {
        message = Lang::T("起動中のMinecraftの本体を読めません (", "Cannot read the running Minecraft's image (") + std::to_string(GetLastError()) + ")";
        return GameCheck::Retry;
    }
    const auto base = reinterpret_cast<uintptr_t>(first);
    const ImageReader read = [process, base](uint32_t rva, void* out, uint32_t size) {
        SIZE_T got = 0;
        return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(base + rva), out, size, &got) && got == size;
    };
    const auto image = ReadGameImage(read);
    if (!image) { message = Lang::T("起動中のMinecraftの本体の形式を読めません", "Cannot read the format of the running Minecraft's image"); return GameCheck::Retry; }
    std::string why;
    const bool supported = ImageSupported(*image, table, why);
    if (!privateDirectory.empty())
    {
        std::ofstream log(privateDirectory / "launcher.log", std::ios::app | std::ios::binary);
        if (log) log << "Attach check: package " << table.gameVersion << ", game " << (image->fileVersion.empty() ? "?" : image->fileVersion)
                     << " " << Hex32(image->timestamp) << " " << Hex32(image->sizeOfImage) << ": " << (supported ? "supported" : "not supported (" + why + ")") << '\n';
    }
    if (supported) return GameCheck::Ok;
    message = UnsupportedGameMessage(&*image, table.gameVersion, why);
    return GameCheck::Unsupported;
}

void LaunchAttach()
{
    DiagnosticsEnvironment();
    if (LaunchWithoutOriginX(false, {})) return;
    const bool standalone = launchTarget.standalone;
    if (standalone) RequireStandaloneDll(StandaloneDll());
    else RequireAttachRuntime(attachDir, packageRoot / "runtime.lock.json", launchTarget);
    const DWORD pid = GamePidAny();
    if (pid && Ready(pid)) { StartHud(pid); return; }
    if (!standalone)
    {
        const auto backup = InstallMod(packageRoot / "mods/MyClientData", attachDir, launchTarget);
        if (!backup.empty()) Status(Lang::T(L"以前のMODをバックアップしました:\r\n", L"Backed up your previous mods:\r\n") + backup.wstring());
    }
    GrantAppPackageAccess(standalone ? StandaloneDll().parent_path() : attachDir);
    injectedPid = seenPid = 0;
    seenAt = 0;
    injectFails = 0;
    startedAt = GetTickCount64();
    if (!pid)
    {
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open",
            L"shell:AppsFolder\\Microsoft.MinecraftUWP_8wekyb3d8bbwe!Game", nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32)
            throw std::runtime_error(Lang::T("Minecraftを起動できません。Xboxアプリから一度起動できるか確認してください。(ShellExecute ", "Cannot start Minecraft. Check that it starts once from the Xbox app. (ShellExecute ") + std::to_string(result) + ")");
        Status(Lang::T(L"Minecraftを通常起動しました。起動後にローダーを注入します…\r\n接続を確認するとHUDを自動で起動します。", L"Started Minecraft normally. The loader is injected after it starts…\r\nThe HUD starts automatically once connected.") +
               (launchNote.empty() ? std::wstring{} : L"\r\n" + launchNote));
    }
    else Status(Lang::T(L"起動中のMinecraftへローダーを注入します…", L"Injecting the loader into the running Minecraft…"));
    Busy(State::Attaching);
}

void ApplyAttach(const fs::path& source)
{
    // Only MicrosoftGame.config is read; the protected executable is never opened.
    pending = ResolveGame(source, launchTarget, false);
    attachMode = true;
    attachDir = privateDirectory / "attach" / fs::u8path(selected.target.id);
    fs::create_directories(attachDir);
    const auto marker = attachDir / "MyClient-game.json";
    if (!fs::exists(marker))
    {
        std::ofstream output(marker, std::ios::binary);
        output << Json{{"owner", "MyClient"}, {"schemaVersion", 1}, {"mode", "attach"}}.dump(2);
        output.close();
        if (!output) throw std::runtime_error(Lang::T("注入用フォルダーの情報を保存できません。", "Cannot save the injection folder information."));
    }
    if (launchTarget.standalone) { LaunchAttach(); return; }   // no loader to prepare: the DLL itself is loaded
    bool ready = false;
    try { RequireAttachRuntime(attachDir, packageRoot / "runtime.lock.json", launchTarget); ready = true; }
    catch (const std::exception&) { /* First use or interrupted preparation. */ }
    if (ready) { LaunchAttach(); return; }
    std::vector<wchar_t> systemPath(32768);
    const UINT length = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
    if (!length || length >= systemPath.size()) throw std::runtime_error(Lang::T("Windowsのシステムフォルダーを取得できません。", "Cannot get the Windows system folder."));
    const auto powershell = fs::path(std::wstring(systemPath.data(), length)) / "WindowsPowerShell/v1.0/powershell.exe";
    installer = Start(powershell,
        {L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
         RuntimeScript().wstring(), L"-ManifestPath", (packageRoot / "runtime.lock.json").wstring(),
         L"-ExpectedGameVersion", Wide(launchTarget.runtimeGame), L"-ExpectedLoaderVersion", Wide(launchTarget.loader),
         L"-GameDirectory", attachDir.wstring(), L"-CacheDirectory", (privateDirectory / "cache").wstring(), L"-Attach"},
         attachDir, privateDirectory / "runtime-setup.log");
    Busy(State::Installing);
    BeginProgress();
    Status(Lang::T(L"ローダーを公式配布元から準備しています（本体は変更しません）。初回はインターネット接続が必要です。\r\n進行状況は「ログを開く」で確認できます。", L"Preparing the loader from the official source (the game is not modified). The first time needs an internet connection.\r\nSee progress with \"Open log\"."));
}

void PrepareRuntimeOrLaunch()
{
    if (launchTarget.standalone) { LaunchStandalone(); return; }   // no loader to prepare, the exe stays as it is
    RequireExecutableVersion(pending.directory / "Minecraft.Windows.exe", launchTarget);
    if (GamePid(pending.directory)) throw std::runtime_error(Lang::T("ローダーの導入前にMinecraftを終了してください。", "Close Minecraft before installing the loader."));
    bool ready = false;
    try { RequireRuntime(pending.directory, packageRoot / "runtime.lock.json", launchTarget); ready = true; }
    catch (const std::exception&) { /* First use or interrupted preparation. */ }
    if (ready) { LaunchGame(); return; }
    const auto originalImports = ImportedDlls(pending.directory / "MyClient-original.exe");
    if (originalImports.count("preloader.dll") || originalImports.count("levilauncher.dll"))
        throw std::runtime_error(Lang::T("保存した元のゲーム本体が変更されています。公式インストール元から準備し直してください。", "The saved original game has been modified. Prepare it again from the official install."));
    std::vector<wchar_t> systemPath(32768);
    const UINT length = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
    if (!length || length >= systemPath.size()) throw std::runtime_error(Lang::T("Windowsのシステムフォルダーを取得できません。", "Cannot get the Windows system folder."));
    const auto powershell = fs::path(std::wstring(systemPath.data(), length)) / "WindowsPowerShell/v1.0/powershell.exe";
    installer = Start(powershell,
        {L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
         RuntimeScript().wstring(), L"-ManifestPath", (packageRoot / "runtime.lock.json").wstring(),
         L"-ExpectedGameVersion", Wide(launchTarget.runtimeGame), L"-ExpectedLoaderVersion", Wide(launchTarget.loader),
         L"-GameDirectory", pending.directory.wstring(), L"-CacheDirectory", (privateDirectory / "cache").wstring()},
         pending.directory, privateDirectory / "runtime-setup.log");
    Busy(State::Installing);
    BeginProgress();
    Status(Lang::T(L"必要なローダーを公式配布元から準備しています。初回はインターネット接続が必要です。\r\n準備が終わるとMinecraftを起動します。進行状況は「ログを開く」で確認できます。", L"Preparing the required loader from the official source. The first time needs an internet connection.\r\nMinecraft starts when it is ready. See progress with \"Open log\"."));
}

bool FindInstalledGame(bool quiet);

void ApplyClient()
{
    Save();
    attachMode = false;
    // GDK saves are still shared; prevent concurrent managed instances.
    const auto games = privateDirectory / "game";
    if (fs::is_directory(games))
        for (const auto& entry : fs::directory_iterator(games))
            if (entry.is_directory() && entry.path().filename() != selected.target.id && GamePid(entry.path()))
                throw std::runtime_error(Lang::T("別バージョンのMinecraftを終了してから切り替えてください。", "Close the other version of Minecraft before switching."));
    RequireMod(packageRoot / "mods/MyClientData", launchTarget);
    RequireX64Pe(HudExecutable());
    const auto destination = privateDirectory / "game" / selected.target.id;
    if (fs::exists(destination))
    {
        pending = PrepareManagedGame({}, destination, launchTarget);
        SetWindowTextW(GetDlgItem(window, Game), (destination / "Minecraft.Windows.exe").c_str());
        Save();
        const DWORD pid = GamePid(pending.directory);
        if (pid)
        {
            if (!Ready(pid)) throw std::runtime_error(Lang::T("Minecraftが起動中です。準備や更新の前にゲームを終了してください。", "Minecraft is running. Close the game before preparing or updating."));
            StartHud(pid);
            return;
        }
        PrepareRuntimeOrLaunch();
        return;
    }
    fs::path source(Text(Game));
    if (source.empty() || Lower(source.filename().u8string()) != "minecraft.windows.exe")
    {
        if (FindInstalledGame(true)) source = fs::path(Text(Game));
    }
    // Xbox-app (GDK) installs protect the executable: copying it is impossible, so attach instead.
    std::error_code existsError;
    if (!source.empty() && fs::exists(source, existsError) && ProbeRead(source) == ERROR_ACCESS_DENIED)
    {
        ApplyAttach(source);
        return;
    }
    const auto instance = ResolveGame(source, launchTarget);
    RequireExecutableVersion(source, launchTarget);
    if (GamePid(instance.directory)) throw std::runtime_error(Lang::T("ゲーム本体をコピーする前にMinecraftを終了してください。", "Close Minecraft before copying the game."));
    Busy(State::Preparing);
    copyDone = 0;
    copyTotal = 0;
    BeginProgress();
    Status(Lang::T(L"Minecraftを自作ソフト用のフォルダーへコピーしています…\r\n初回は時間とゲーム本体分の空き容量が必要です。", L"Copying Minecraft to the folder for third-party software…\r\nThe first time takes a while and needs free space for a copy of the game."));
    const auto target = launchTarget;
    preparation = std::thread([source, destination, target] {
        auto result = new PreparationResult;
        try
        {
            result->instance = PrepareManagedGame(source, destination, target,
                [](unsigned long long done, unsigned long long total) { copyDone = done; copyTotal = total; });
        }
        catch (const std::exception& error) { result->error = error.what(); }
        if (!PostMessageW(window, PreparedMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

void Tick()
{
    if (state == State::Installing)
    {
        const DWORD wait = WaitForSingleObject(installer, 0);
        if (wait == WAIT_TIMEOUT) { PollProgress(); return; }
        PollProgress();
        EndProgress();
        DWORD exitCode = 1;
        GetExitCodeProcess(installer, &exitCode);
        CloseHandle(installer);
        installer = nullptr;
        Busy(State::Idle);
        if (wait != WAIT_OBJECT_0 || exitCode)
            throw std::runtime_error(Lang::T("ローダーの準備に失敗しました。ログを開いて確認してください。終了コード: ", "Preparing the loader failed. Open the log to check. Exit code: ") + std::to_string(exitCode));
        if (attachMode) LaunchAttach(); else LaunchGame();
    }
    else if (state == State::Preparing)
    {
        PollCopyProgress();
    }
    else if (state == State::Switching)
    {
        PollSwitchProgress();
    }
    else if (state == State::Attaching)
    {
        const DWORD pid = GamePidAny();
        if (Ready(pid)) { StartHud(pid); return; }
        const ULONGLONG now = GetTickCount64();
        if (pid && injectedPid != pid)
        {
            if (seenPid != pid) { seenPid = pid; seenAt = now; }
            std::string checkMessage;
            const GameCheck check = now - seenAt >= 1500 ? CheckRunningGame(pid, checkMessage) : GameCheck::Retry;
            if (check == GameCheck::Unsupported)
            {
                // Nothing is injected (release-checklist 2026-11-20, item 3): the game keeps running without OriginX.
                Busy(State::Idle);
                throw std::runtime_error(checkMessage);
            }
            if (now - seenAt >= 1500 && check == GameCheck::Retry)
            {
                if (++injectFails >= 20)
                {
                    Busy(State::Idle);
                    throw std::runtime_error(Lang::T("起動中のMinecraftの版を確かめられないため、読み込みませんでした: ", "Could not check the running Minecraft's version, so nothing was loaded: ") + checkMessage);
                }
            }
            else if (now - seenAt >= 1500 && !RemoteStopText().empty())
            {
                LaunchWithoutOriginX(false, {});   // the game is running already: nothing injected, the reason shown
                return;
            }
            else if (now - seenAt >= 1500)
            {
                const auto result = InjectDll(pid, launchTarget.standalone ? StandaloneDll() : attachDir / "PreLoader.dll");
                if (result.ok)
                {
                    injectedPid = pid;
                    Status(launchTarget.standalone ? Lang::T(L"OriginXのDLLを読み込みました。DLLへの接続を待っています…", L"Loaded the OriginX DLL. Waiting for the DLL to connect…")
                                                      : Lang::T(L"PreLoader.dllを注入しました。DLLへの接続を待っています…", L"Injected PreLoader.dll. Waiting for the DLL to connect…"));
                }
                else if (++injectFails >= 20)
                {
                    Busy(State::Idle);
                    throw std::runtime_error(Lang::T("ローダーの注入に失敗しました: ", "Injecting the loader failed: ") + result.message);
                }
                else if (injectFails == 1 || injectFails % 5 == 0)
                    Status(Lang::T(L"注入を再試行しています (", L"Retrying the injection (attempt") + std::to_wstring(injectFails) + Lang::T(L"回目): ", L"): ") + Wide(result.message));
            }
        }
        if (now - startedAt > 180000)
        {
            Busy(State::Idle);
            Status(injectedPid ?
                Lang::T(L"注入はしましたが、DLLへの接続を確認できませんでした。\r\nPreLoaderが後読みに対応していない可能性があります。ログを開いて確認してください。", L"Injected, but could not confirm the connection to the DLL.\r\nPreLoader may not support late loading. Open the log to check.") :
                Lang::T(L"Minecraftのプロセスを確認できませんでした。Xboxアプリから起動できるか確認してください。", L"Could not find the Minecraft process. Check that it starts from the Xbox app."));
        }
    }
    else if (state == State::Waiting)
    {
        const DWORD pid = GamePid(pending.directory);
        if (Ready(pid)) { StartHud(pid); return; }
        if (!pid && gameProcess && WaitForSingleObject(gameProcess, 0) == WAIT_OBJECT_0)
        {
            DWORD exitCode = 0;
            GetExitCodeProcess(gameProcess, &exitCode);
            Busy(State::Idle);
            throw std::runtime_error(Lang::T("Minecraftが接続前に終了しました。Windowsエラー/終了コード: ", "Minecraft exited before connecting. Windows error / exit code: ") + std::to_string(exitCode) +
                Lang::T("。Xboxアプリでの起動、Gaming Services・GameInput・VC++ランタイムを確認してください。", ". Check launching from the Xbox app, Gaming Services, GameInput and the VC++ runtime."));
        }
        if (GetTickCount64() - startedAt > 120000)
        {
            Busy(State::Idle);
            Status(Lang::T(L"接続を確認できませんでした。ゲームのログとWindowsの必要な部品を確認してください。\r\nゲーム起動後に「適用して起動」を押すと、読み込み済みのDLLへの接続を再試行できます。", L"Could not confirm the connection. Check the game log and the Windows components it needs.\r\nAfter the game starts, press \"Apply & Launch\" to retry connecting to the loaded DLL."));
        }
    }
}

int Px(int value) { return MulDiv(value, dpi, 96); }

// One owner-drawn button. Also used to draw a picture of a button while the pages slide
// (the real controls are hidden then). `under` is the colour behind its rounded corners.
void DrawButtonFace(HDC dc, RECT area, const wchar_t* label, UINT id, bool disabled, bool pressed, bool focus, COLORREF under)
{
    const bool primary = id == Apply;
    const COLORREF fill = disabled ? ColDisabled : primary ? (pressed ? ColAccentDown : ColAccent)
                                                          : (pressed ? ColSecondaryDown : ColSecondary);
    const COLORREF edge = disabled ? ColDisabled : primary ? fill : ColBorder;
    HBRUSH underBrush = CreateSolidBrush(under);
    FillRect(dc, &area, underBrush);
    DeleteObject(underBrush);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, brush);
    RoundRect(dc, area.left, area.top, area.right - 1, area.bottom - 1, Px(12), Px(12));
    if (focus && !disabled)
    {
        HPEN ring = CreatePen(PS_SOLID, 1, ColAccentHi);
        SelectObject(dc, ring);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, area.left + Px(3), area.top + Px(3), area.right - 1 - Px(3), area.bottom - 1 - Px(3), Px(8), Px(8));
        SelectObject(dc, pen);
        DeleteObject(ring);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? ColDisabledText : primary ? RGB(255, 255, 255) : ColText);
    HGDIOBJ oldFont = SelectObject(dc, primary ? boldFont : font);
    if (pressed) OffsetRect(&area, 0, Px(1));
    if (id == HomeSettings)
    {
        // Gear glyph before the label, like the play triangle on Launch.
        RECT measure = area;
        DrawTextW(dc, label, -1, &measure, DT_CALCRECT | DT_SINGLELINE);
        const int textWidth = measure.right - measure.left, glyph = Px(18), gap = Px(10);
        const int left = area.left + (area.right - area.left - (glyph + gap + textWidth)) / 2;
        RECT icon{left, area.top, left + glyph, area.bottom};
        HGDIOBJ previous = SelectObject(dc, symFont);
        DrawTextW(dc, L"\u2699", -1, &icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
        RECT text{left + glyph + gap, area.top, area.right, area.bottom};
        DrawTextW(dc, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    else if (primary && wcscmp(label, L"Launch") == 0)
    {
        // Play triangle drawn by hand so it does not depend on the font having the glyph.
        RECT measure = area;
        DrawTextW(dc, label, -1, &measure, DT_CALCRECT | DT_SINGLELINE);
        const int textWidth = measure.right - measure.left, tri = Px(13), gap = Px(12);
        const int left = area.left + (area.right - area.left - (tri + gap + textWidth)) / 2;
        const int mid = (area.top + area.bottom) / 2;
        POINT points[3] = {{left, mid - tri / 2 - 1}, {left, mid + tri / 2 + 1}, {left + tri, mid}};
        HBRUSH white = CreateSolidBrush(disabled ? ColDisabledText : RGB(255, 255, 255));
        HGDIOBJ ob = SelectObject(dc, white), op = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, points, 3);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(white);
        RECT text{left + tri + gap, area.top, area.right, area.bottom};
        DrawTextW(dc, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    else
        DrawTextW(dc, label, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
}

// Home buttons sit on the sky, the others on a panel.
COLORREF ButtonUnder(UINT id, int middleY)
{
    if (id != Apply && id != HomeSettings) return ColPanel;
    RECT client;
    GetClientRect(window, &client);
    return HomeSky(middleY, client.bottom);
}

void DrawButton(const DRAWITEMSTRUCT& item)
{
    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, 127);
    POINT middle{0, (item.rcItem.top + item.rcItem.bottom) / 2};
    MapWindowPoints(item.hwndItem, window, &middle, 1);
    DrawButtonFace(item.hDC, item.rcItem, label, item.CtlID, (item.itemState & ODS_DISABLED) != 0,
                   (item.itemState & ODS_SELECTED) != 0, (item.itemState & ODS_FOCUS) != 0, ButtonUnder(item.CtlID, middle.y));
}
HWND Control(const wchar_t* type, const wchar_t* text, int x, int y, int w, int h,
             int id = 0, DWORD style = 0, bool large = false, bool visible = true)
{
    HWND handle = CreateWindowExW(0, type, text, WS_CHILD | (visible ? WS_VISIBLE : 0) | style,
        Sc(x), Sc(y), Sc(w), Sc(h), window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!handle) throw std::runtime_error(Lang::T("画面を作成できません。", "Cannot create the window."));
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(large ? heading : font), TRUE);
    return handle;
}

// Candidate Minecraft.Windows.exe files: the running game first, then every XboxGames
// folder on local drives. Nothing is modified; each candidate is validated by ResolveGame.
std::wstring searchReport;   // where GameCandidates() looked (shown when nothing is found)

std::vector<fs::path> GameCandidates()
{
    std::vector<fs::path> list;
    searchReport.clear();
    const auto managedRoot = Lower(privateDirectory.u8string());
    const auto add = [&](const fs::path& path) {
        // Protected (Xbox app) installs: the file exists but cannot be opened.
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_ACCESS_DENIED) return;
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return;
        if (Lower(path.u8string()).rfind(managedRoot, 0) == 0) return; // our own private copy
        std::error_code error;
        for (const auto& known : list)
            if (Lower(known.u8string()) == Lower(path.u8string())) return;
        list.push_back(path);
    };
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof entry;
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        {
            if (_wcsicmp(entry.szExeFile, L"Minecraft.Windows.exe") != 0) continue;
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;
            std::vector<wchar_t> buffer(32768);
            DWORD size = static_cast<DWORD>(buffer.size());
            if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size))
                add(fs::path(std::wstring(buffer.data(), size)));
            CloseHandle(process);
        }
        CloseHandle(snapshot);
    }
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
    {
        if (!(drives & (1u << i))) continue;
        const std::wstring root = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":\\";
        const UINT type = GetDriveTypeW(root.c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
        std::error_code error;
        const fs::path xbox = fs::path(root) / L"XboxGames";
        const bool exists = fs::is_directory(xbox, error);
        searchReport += root + L"XboxGames: " + (exists ? Lang::T(L"あり", L"yes") : Lang::T(L"なし", L"no")) + L"\r\n";
        error.clear();
        for (fs::directory_iterator it(xbox, error), end; !error && it != end; it.increment(error))
        {
            searchReport += L"  - " + it->path().filename().wstring() + L"\r\n";
            add(it->path() / L"Content" / L"Minecraft.Windows.exe");
        }
    }
    // Installed-package registry (current user): covers installs outside XboxGames.
    HKEY packages = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages",
            0, KEY_READ, &packages) == ERROR_SUCCESS)
    {
        wchar_t name[512];
        for (DWORD index = 0;; ++index)
        {
            DWORD length = 512;
            if (RegEnumKeyExW(packages, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            if (_wcsnicmp(name, L"Microsoft.MinecraftUWP_", 23) != 0) continue;
            wchar_t folder[2048]{};
            DWORD size = sizeof folder;
            if (RegGetValueW(packages, name, L"PackageRootFolder", RRF_RT_REG_SZ, nullptr, folder, &size) != ERROR_SUCCESS) continue;
            searchReport += std::wstring(Lang::T(L"パッケージ: ", L"Package: ")) + folder + L"\r\n";
            add(fs::path(folder) / L"Content" / L"Minecraft.Windows.exe");
            add(fs::path(folder) / L"Minecraft.Windows.exe");
        }
        RegCloseKey(packages);
    }
    else searchReport += Lang::T(L"パッケージ情報: 読み取れません\r\n", L"Package info: unreadable\r\n");
    // Versions installed by LeviLauncher (unprotected copies, one folder per version).
    try
    {
        for (const auto& root : LeviLauncherVersionRoots(KnownFolder(FOLDERID_RoamingAppData)))
        {
            std::error_code error;
            if (!fs::is_directory(root, error)) continue;
            searchReport += L"LeviLauncher: " + root.wstring() + L"\r\n";
            for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error))
                add(it->path() / L"Minecraft.Windows.exe");
        }
    }
    catch (const std::exception&) { searchReport += Lang::T(L"LeviLauncher: 読み取れません\r\n", L"LeviLauncher: unreadable\r\n"); }
    // Folders added with "フォルダーを追加…".
    for (const auto& folder : imports)
    {
        if (!folder.is_string()) continue;
        const auto path = fs::u8path(folder.get<std::string>());
        searchReport += Lang::T(L"追加したフォルダー: ", L"Added folder: ") + path.wstring() + L"\r\n";
        add(path / L"Minecraft.Windows.exe");
        add(path / L"Content" / L"Minecraft.Windows.exe");
    }
    return list;
}

// Which versions are on this PC: every candidate install plus MyClient's own copies.
void ScanInstalled()
{
    installedGames.clear();
    for (const auto& candidate : GameCandidates())
    {
        const auto id = InstalledVersion(candidate.parent_path());
        if (!id.empty() && !installedGames.count(id)) installedGames[id] = candidate;
    }
    std::error_code error;
    for (fs::directory_iterator it(privateDirectory / "game", error), end; !error && it != end; it.increment(error))
    {
        const auto id = InstalledVersion(it->path());
        if (!id.empty()) installedGames[id] = it->path() / "Minecraft.Windows.exe";
    }
}

bool FindInstalledGame(bool quiet)
{
    std::string lastError;
    size_t checked = 0;
    for (const auto& candidate : GameCandidates())
    {
        ++checked;
        try
        {
            ResolveGame(candidate, selected.target);
            SetWindowTextW(GetDlgItem(window, Game), candidate.c_str());
            Status(Lang::T(L"本体を自動検出しました:\r\n", L"Found the game automatically:\r\n") + candidate.wstring() +
                   Lang::T(L"\r\n「適用して起動」で導入先を確認して起動します。", L"\r\n\"Apply & Launch\" checks the install location and launches."));
            return true;
        }
        catch (const std::exception& error)
        {
            lastError = error.what();
            // Xbox-app (GDK) installs are protected: the exe cannot be opened, but the
            // folder's MicrosoftGame.config can. Accept it; "適用して起動" then uses attach mode.
            if (ProbeRead(candidate) == ERROR_ACCESS_DENIED)
            {
                try
                {
                    ResolveGame(candidate, selected.target, false);
                    SetWindowTextW(GetDlgItem(window, Game), candidate.c_str());
                    Status(Lang::T(L"保護されたXbox版の本体を検出しました:\r\n", L"Found the protected Xbox game:\r\n") + candidate.wstring() +
                           Lang::T(L"\r\n本体は変更せず、起動後にローダーを注入する方式で起動します。「適用して起動」を押してください。", L"\r\nThe game is not modified; the loader is injected after it starts. Press \"Apply & Launch\"."));
                    return true;
                }
                catch (const std::exception& inner) { lastError = inner.what(); }
            }
        }
    }
    if (!quiet)
    {
        std::wstring message = L"Minecraft " + Wide(selected.target.id) + Lang::T(L"の本体を自動検出できませんでした。", L": could not find the game automatically.");
        if (checked) message += Lang::T(L"\r\n見つかった本体の確認結果: ", L"\r\nResults for the games found: ") + Wide(lastError);
        else message += Lang::T(L"\r\nXboxGamesフォルダーや起動中のMinecraftが見つかりません。「選択…」で指定してください。\r\n確認した場所:\r\n", L"\r\nNo XboxGames folder or running Minecraft was found. Set it with \"Select…\".\r\nPlaces checked:\r\n") +
                        (searchReport.empty() ? std::wstring(Lang::T(L"(なし)", L"(none)")) : searchReport);
        Status(message);
    }
    return false;
}

void SelectVersion()
{
    const auto index = SendDlgItemMessageW(window, Version, CB_GETCURSEL, 0, 0);
    if (index < 0 || static_cast<size_t>(index) >= catalog.releases.size()) return;
    selected = catalog.releases[static_cast<size_t>(index)];
    launchTarget = selected.target;   // until a fetched package says otherwise (PackageMessage)
    fs::path path = fs::u8path(sources.value(selected.target.id, std::string{}));
    const auto managed = privateDirectory / "game" / selected.target.id / "Minecraft.Windows.exe";
    if (fs::is_regular_file(managed)) path = managed;
    if (!path.empty() && Lower(path.filename().u8string()) != "minecraft.windows.exe") path.clear();
    SetWindowTextW(GetDlgItem(window, Game), path.c_str());
    EnableWindow(GetDlgItem(window, Apply), selected.available);
    EnableWindow(GetDlgItem(window, PickGame), selected.available);
    EnableWindow(GetDlgItem(window, AutoDetect), selected.available);
    // Always look for an installed game quietly, so a stale saved path (for example the
    // normal install after the version was switched with another tool) is replaced by
    // the install that matches the selected version. A found install is validated for it.
    const bool detected = selected.available && !fs::is_regular_file(managed) && FindInstalledGame(true);
    if (!detected)
        Status(selected.available ? Lang::T(L"選択した版の本体を準備して起動できます。DLLは自動取得・検査します。", L"You can prepare and launch the chosen version. The DLL is fetched and checked automatically.") : Wide(selected.reason));
}

void RefreshVersions(const std::string& preferred)
{
    SendDlgItemMessageW(window, Version, CB_RESETCONTENT, 0, 0);
    size_t chosen = 0;
    for (size_t i = 0; i < catalog.releases.size(); ++i)
    {
        const auto& release = catalog.releases[i];
        const auto label = Wide(release.target.id) + (release.available ? Lang::T(L"  — DLLあり", L"  — DLL available") : Lang::T(L"  — 対応待ち", L"  — waiting for support"));
        SendDlgItemMessageW(window, Version, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (release.target.id == preferred) chosen = i;
    }
    SendDlgItemMessageW(window, Version, CB_SETCURSEL, chosen, 0);
    SelectVersion();
    SyncVersionCards();
}

// A folder holding a game the user installed some other way (LeviLauncher, a backup copy...).
// It is remembered and searched like the XboxGames folders; nothing in it is changed.
void ImportFolder()
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) || !dialog)
        throw std::runtime_error(Lang::T("フォルダー選択画面を開けません。", "Cannot open the folder picker."));
    struct ComRelease { IUnknown* p; ~ComRelease() { if (p) p->Release(); } } releaseDialog{dialog};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(Lang::T(L"Minecraftの版が入っているフォルダー（Minecraft.Windows.exeがある場所）", L"The folder that holds a Minecraft version (where Minecraft.Windows.exe is)"));
    if (dialog->Show(window) != S_OK) return;   // cancelled
    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item)) || !item) throw std::runtime_error(Lang::T("選択したフォルダーを読み取れません。", "Cannot read the chosen folder."));
    ComRelease releaseItem{item};
    PWSTR text = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) || !text) throw std::runtime_error(Lang::T("ローカルのフォルダーを選択してください。", "Choose a local folder."));
    const fs::path folder(text);
    CoTaskMemFree(text);
    fs::path game;
    for (const auto& candidate : {folder / L"Minecraft.Windows.exe", folder / L"Content" / L"Minecraft.Windows.exe"})
    {
        std::error_code error;
        if (fs::exists(candidate, error) || ProbeRead(candidate) == ERROR_ACCESS_DENIED) { game = candidate; break; }
    }
    if (game.empty()) throw std::runtime_error(Lang::T("このフォルダーにMinecraft.Windows.exeがありません。ゲーム本体のフォルダーを選択してください。", "This folder has no Minecraft.Windows.exe. Choose the game's folder."));
    const auto id = InstalledVersion(game.parent_path());
    if (id.empty())
        throw std::runtime_error(Lang::T("このフォルダーのMinecraftの版を読み取れません。GDKのRelease版（MicrosoftGame.configがあるもの）が必要です。Preview版には対応していません。", "Cannot read this folder's Minecraft version. A GDK Release build (with MicrosoftGame.config) is required. Preview is not supported."));
    const auto saved = folder.u8string();
    bool known = false;
    for (const auto& entry : imports) known = known || (entry.is_string() && Lower(entry.get<std::string>()) == Lower(saved));
    if (!known) imports.push_back(saved);
    Save();
    ScanInstalled();
    for (size_t i = 0; i < catalog.releases.size(); ++i)
        if (catalog.releases[i].target.id == id)
        {
            SelectUserVersion(id);
            Refresh();
            return;
        }
    Status(L"Minecraft " + Wide(id) + Lang::T(L"を追加しました:\r\n", L" added:\r\n") + game.wstring() +
           Lang::T(L"\r\nOriginXはこの版にまだ対応していません。対応版が出ると一覧に追加されます。", L"\r\nOriginX does not support this version yet. It is added to the list once supported."));
    Refresh();
}

// The public list of released versions (the one LeviLauncher reads). The last copy is kept for offline use.
void LoadVersionDb(const fs::path& file)
{
    if (!SavedListVerified(file, "versions/stable")) throw std::runtime_error("the saved version list has no valid signature");
    const auto db = ReadJson(file);
    publishedVersions = ParseVersionDb(db);
    versionLinks = ParseVersionLinks(db);
    std::error_code error;
    const auto written = fs::last_write_time(file, error);
    if (error) { versionDbNote = Lang::T(L"保存済み", L"saved"); return; }
    const auto age = std::chrono::duration_cast<std::chrono::hours>(fs::file_time_type::clock::now() - written).count();
    versionDbNote = age < 24 ? Lang::T(L"最新", L"up to date") : Lang::T(L"保存済み（", L"saved (") + std::to_wstring(age / 24) + Lang::T(L"日前）", L" days ago)");
}

void FetchVersionDb()
{
    if (versionDbThread.joinable()) return;
    versionDbThread = std::thread([] {
        auto result = new VersionDbResult;
        const auto temporary = privateDirectory / "versions_db.download";
        try
        {
            if (signatureStopped) throw std::runtime_error(SignatureFailedMessage());
            DownloadSignedList(VersionDbUrl, "versions/stable", temporary, 1024 * 1024);
            const auto db = ReadJson(temporary);
            result->versions = ParseVersionDb(db);
            result->links = ParseVersionLinks(db);
            KeepSignedList(temporary, privateDirectory / "versions_db.json", Lang::T("公開版の一覧を保存できません。", "Cannot save the published version list."));
        }
        catch (const std::exception& error) { result->error = error.what(); }
        std::error_code ignored;
        fs::remove(temporary, ignored);
        fs::remove(SigPath(temporary), ignored);
        if (!PostMessageW(window, VersionDbMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}


// ---- Changing the installed Minecraft to another released version ----
// Checks run before the download; the install is Windows' own Add-AppxPackage.
constexpr const char* SwitchScript = R"ps(param([string]$Mode, [string]$Package, [string]$Helper, [string]$ExpectedId)
function Get-MinecraftVersionId([version]$Version) {
    $parts = @($Version.Major, $Version.Minor, $Version.Build, $Version.Revision)
    if ($parts[0] -ge 26) { $parts = $parts[0..2] }
    elseif ($parts[3] -eq 0 -and $parts[2] -ge 100) {
        $parts = @($parts[0], $parts[1], [int][math]::Floor($parts[2] / 100), ($parts[2] % 100))
    }
    if ($parts.Count -eq 4 -and $parts[0] -eq 1 -and $parts[1] -ge 26) { $parts = $parts[1..3] }
    return ($parts -join '.')
}
$ErrorActionPreference = 'Stop'
try {
    if (-not (Get-AppxPackage -Name Microsoft.GamingServices)) { Write-Output 'Gaming Services is not installed.'; exit 2 }
    $game = Get-AppxPackage -Name Microsoft.MinecraftUWP | Select-Object -First 1
    if (-not $game) { Write-Output 'Minecraft is not installed.'; exit 3 }
    Write-Output ('Installed: ' + $game.Version + ' (' + $game.SignatureKind + ')')
    if ($Mode -eq 'query') { Write-Output ('NormalId: ' + (Get-MinecraftVersionId $game.Version)); exit 0 }
    # Not only the Xbox app / Store: Minecraft bought elsewhere (e.g. a code from Amazon) can report another kind.
    # Windows itself refuses the install below when the installed package cannot be replaced.
    if ("$($game.SignatureKind)" -ne 'Store') { Write-Output ('Not from the Store: ' + $game.SignatureKind) }
    # A game registered from a plain folder (OderSo and similar launchers: SignatureKind None or Developer) blocks
    # every packaged install with 0x80073CFB (2026-11-18). Build 35 unregistered it without asking; from build 37 the
    # launcher does not touch it (user, 2026-11-19): it says so before any download (exit 11). Asking first and
    # unregistering with consent is the new engine's job (docs/superpowers/specs/2026-11-19-version-switch-engine-design.md).
    # Decision A (user, 2026-11-19): a developer-mode registration (IsDevelopmentMode True or SignatureKind None,
    # e.g. OderSo's 1.26.5101.0) is never downloaded over, unregistered or written to; only guidance is shown,
    # until a way to restore it after unregistering has been confirmed.
    Write-Output ('Development mode registration: ' + $game.IsDevelopmentMode)
    try {
        $unlock = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction Stop
        Write-Output ('Developer mode: ' + $unlock.AllowDevelopmentWithoutDevLicense)
    } catch { Write-Output 'Developer mode: unknown' }
    $loose = ("$($game.SignatureKind)" -eq 'None' -or "$($game.SignatureKind)" -eq 'Developer' -or "$($game.IsDevelopmentMode)" -eq 'True')
    if ($loose) { Write-Output ('Folder registration: ' + $game.InstallLocation) }
    if ($Mode -ne 'install') {
        if ($Mode -eq 'check' -and $ExpectedId -and (Get-MinecraftVersionId $game.Version) -eq $ExpectedId) {
            Write-Output ('Normal launch is already ' + $ExpectedId)
            exit 10
        }
        if ($Mode -eq 'check' -and $loose) { exit 11 }
        exit 0
    }
    if ($loose) { exit 11 }
    if (-not $ExpectedId) { throw 'Expected Minecraft version is missing.' }
    if (Get-Process -Name Minecraft.Windows -ErrorAction SilentlyContinue) { throw 'Close Minecraft before changing the normal-launch version.' }
    try { Add-AppxPackage -Path $Package -ForceUpdateFromAnyVersion }
    catch {
        # Windows' own reason: the error code and, through the ActivityId in the message, its install log.
        $message = $_.Exception.Message
        Write-Output $message
        Write-Output ('HRESULT: ' + ('0x{0:X8}' -f $_.Exception.HResult))
        if ($message -match '([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})') {
            $activity = $Matches[1]
            Write-Output ('ActivityId: ' + $activity)
            try {
                Get-AppPackageLog -ActivityID $activity -ErrorAction Stop | Select-Object -Last 25 |
                    ForEach-Object { Write-Output ('  [' + $_.Id + '] ' + ("$($_.Message)" -replace '\s+', ' ')) }
            } catch { Write-Output ('Could not read the Windows install log: ' + $_.Exception.Message) }
        }
        exit 4
    }
    $game = Get-AppxPackage -Name Microsoft.MinecraftUWP | Select-Object -First 1
    if (-not $game -or (Get-MinecraftVersionId $game.Version) -ne $ExpectedId) {
        throw ('Windows did not register the requested normal-launch version ' + $ExpectedId)
    }
    Write-Output ('Now: ' + $game.Version + ' at ' + $game.InstallLocation)
    if ($Helper) {
        try { Copy-Item -LiteralPath $Helper -Destination (Join-Path $game.InstallLocation 'gamelaunchhelper.dll') -Force }
        catch { Write-Output ('Could not replace gamelaunchhelper.dll: ' + $_.Exception.Message); exit 5 }
        Write-Output 'Replaced gamelaunchhelper.dll'
    }
    exit 0
} catch {
    Write-Output $_.Exception.Message
    exit 1
}
)ps";

// Text Windows PowerShell wrote to a redirected file is in the console code page (CP932 on Japanese Windows), not
// UTF-8. Everything the launcher shows and logs is UTF-8: a message that joined both was read as one or the other
// as a whole and half of it came out garbled (2026-11-11, the normal-launch switch's error). Valid UTF-8 is kept.
std::string ConsoleTextToUtf8(const std::string& text)
{
    if (text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0)
        return text;
    for (const UINT page : {GetConsoleOutputCP(), GetOEMCP(), GetACP()})
    {
        if (!page) continue;
        const int wide = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (wide <= 0) continue;
        std::wstring buffer(wide, L'\0');
        MultiByteToWideChar(page, 0, text.data(), static_cast<int>(text.size()), buffer.data(), wide);
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), wide, nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) continue;
        std::string utf8(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer.data(), wide, utf8.data(), bytes, nullptr, nullptr);
        return utf8;
    }
    return text;
}

// The last lines PowerShell wrote, for error messages (always UTF-8, see ConsoleTextToUtf8).
std::string LogTail(const fs::path& log, size_t limit = 600)
{
    std::ifstream input(log, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    text = ConsoleTextToUtf8(text);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    if (text.size() > limit)
    {
        size_t from = text.size() - limit;
        while (from < text.size() && (static_cast<unsigned char>(text[from]) & 0xC0) == 0x80) ++from;   // not inside a UTF-8 character
        text = "…" + text.substr(from);
    }
    return text + Lang::T("\n（ログ: ", "\n(log: ") + log.u8string() + "）";
}

DWORD RunSwitchScript(const fs::path& script, const std::wstring& mode, const fs::path& package, const fs::path& helper,
                      const fs::path& log, const std::string& expectedId)
{
    std::vector<wchar_t> systemPath(32768);
    const UINT length = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
    if (!length || length >= systemPath.size()) throw std::runtime_error(Lang::T("Windowsのシステムフォルダーを取得できません。", "Cannot get the Windows system folder."));
    const auto powershell = fs::path(std::wstring(systemPath.data(), length)) / "WindowsPowerShell/v1.0/powershell.exe";
    HANDLE process = Start(powershell, {L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
                                        script.wstring(), L"-Mode", mode, L"-Package", package.wstring(), L"-Helper", helper.wstring(),
                                        L"-ExpectedId", Wide(expectedId)},
                           script.parent_path(), log);
    if (!process) throw std::runtime_error(Lang::T("PowerShellを起動できません。", "Cannot start PowerShell."));
    WaitForSingleObject(process, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process, &exitCode);
    CloseHandle(process);
    return exitCode;
}

fs::path WriteSwitchScript(const fs::path& folder, const char* name)
{
    fs::create_directories(folder);
    const auto path = folder / name;
    std::ofstream script(path, std::ios::binary | std::ios::trunc);
    script << SwitchScript;
    if (!script) throw std::runtime_error(Lang::T("版の変更の準備ファイルを書き込めません。", "Cannot write the version change file."));
    return path;
}

// Which version Windows starts normally (Start menu / Xbox app). Only reads; ends in NormalVersionMessage.
void QueryNormalVersion()
{
    if (normalQueryThread.joinable()) { normalQueryAgain = true; return; }
    const auto folder = privateDirectory / "cache" / "game-switch";
    fs::path script;
    try { script = WriteSwitchScript(folder, "query-game.ps1"); }
    catch (const std::exception& error) { normalRead = NormalRead::Missing; normalReadNote = Wide(error.what()); Refresh(); return; }
    normalRead = NormalRead::Reading;
    normalQueryAgain = false;
    Refresh();
    normalQueryThread = std::thread([folder, script] {
        auto* result = new NormalQueryResult;
        try
        {
            const auto log = folder / "switch-query.log";
            const DWORD code = RunSwitchScript(script, L"query", {}, {}, log, {});
            std::ifstream input(log, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            result->id = code == 0 ? NormalIdFromLog(text) : std::string{};
            if (code == 2) result->error = Lang::T("Gaming Servicesが入っていません", "Gaming Services is not installed");
            else if (code == 3) result->error = Lang::T("Minecraftが入っていません", "Minecraft is not installed");
            else if (result->id.empty()) result->error = Lang::T("読み取れません（", "unreadable (") + std::to_string(code) + "）: " + LogTail(log, 300);
        }
        catch (const std::exception& error) { result->error = error.what(); }
        if (!PostMessageW(window, NormalVersionMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

// ---- msixvc cache of the version-switch engine (design 2026-11-19 §3) ----
// Runs on the switch thread, so not launcher.log (the UI thread writes that): cache/game-switch/switch-cache.log.
void CacheLog(const std::string& line)
{
    std::ofstream log(privateDirectory / "cache" / "game-switch" / "switch-cache.log", std::ios::app | std::ios::binary);
    SYSTEMTIME now{}; GetLocalTime(&now);
    char stamp[32]; snprintf(stamp, sizeof stamp, "%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    log << stamp << LogPrivacy::Redact(line) << '\n';
}
fs::path PackageCacheFolder() { return privateDirectory / "cache" / "game-packages"; }

// Deletes least recently used versions so that `room` more fit under the limits; `id` is never deleted. Leftover
// .partial files (an interrupted download) are removed too.
void TrimPackageCache(const std::string& id, size_t room)
{
    std::vector<PackageCache::Entry> entries;
    std::error_code error;
    for (fs::directory_iterator it(PackageCacheFolder(), error), end; !error && it != end; it.increment(error))
    {
        const std::string name = it->path().filename().string();
        std::error_code ignored;
        if (PackageCache::IsPartial(name)) { fs::remove(it->path(), ignored); continue; }
        const std::string cachedId = PackageCache::IdOf(name);
        if (cachedId.empty() || !it->is_regular_file(ignored)) continue;
        const auto size = it->file_size(ignored);
        const auto time = it->last_write_time(ignored);
        entries.push_back({cachedId, ignored ? 0 : static_cast<uint64_t>(size), static_cast<int64_t>(time.time_since_epoch().count())});
    }
    const size_t versions = PackageCache::DefaultMaxVersions > room ? PackageCache::DefaultMaxVersions - room : 0;
    for (const auto& old : PackageCache::ToEvict(entries, {id}, versions, PackageCache::DefaultMaxBytes))
    {
        std::error_code ignored;
        fs::remove(PackageCacheFolder() / PackageCache::FileName(old), ignored);
        CacheLog("removed Minecraft " + old + " from the package cache");
    }
}

// Downloads into <name>.partial and renames it only when complete, so a cut download is never used.
void DownloadToCache(const std::string& id, const std::vector<std::string>& urls, const fs::path& package)
{
    const auto partial = PackageCacheFolder() / PackageCache::PartialName(id);
    struct Remove { fs::path path; ~Remove() { std::error_code ignored; fs::remove(path, ignored); } } removeLater{partial};
    std::string errors;
    for (const auto& url : PackageDownloadOrder(urls))
    {
        try
        {
            DownloadGamePackage(url, partial, switchDone, switchTotal);
            fs::rename(partial, package);
            return;
        }
        catch (const std::exception& error)
        {
            if (!errors.empty()) errors += " / ";
            errors += error.what();
            if (std::string(error.what()).find(Lang::T("空き容量", "free space")) != std::string::npos) break;
        }
    }
    throw std::runtime_error("Minecraft " + id + Lang::T(" をダウンロードできませんでした: ", " could not be downloaded: ") + errors);
}

// The installed Minecraft is registered from a plain folder by another launcher (exit 11 of the switch script).
// Nothing was downloaded, unregistered or written (build 37, user 2026-11-19).
std::string FolderRegistrationMessage(const std::string& log)
{
    return Lang::T("今の Minecraft は別のランチャーがフォルダーから登録したものなので、OriginX からは通常起動の版を切り替えられません。"
                   "その登録とフォルダーには何も変更していません。この版はそのランチャーで入れてください（入っている版は OriginX からも選べます）。",
                   "The installed Minecraft was registered from a folder by another launcher, so OriginX cannot switch the normal-launch "
                   "version. Nothing was changed in that registration or folder. Install this version with that launcher (versions it "
                   "installed can also be chosen in OriginX).") + std::string("\n") + log;
}

void SwitchGame(const std::string& id, const std::vector<std::string>& urls, const fs::path& folder, const fs::path& helper,
                std::string& warning)
{
    if (signatureStopped) throw std::runtime_error(SignatureFailedMessage());
    const auto script = folder / "switch-game.ps1";
    switchPhase = 0;
    switch (const DWORD code = RunSwitchScript(script, L"check", {}, {}, folder / "switch-check.log", id))
    {
    case 0: break;
    case 10: return; // already registered: no download/install, also safe when that game is running
    case 11: throw std::runtime_error(FolderRegistrationMessage(LogTail(folder / "switch-check.log")));
    case 2: throw std::runtime_error(Lang::T("Gaming Servicesが入っていません。Microsoft Storeで「Gaming Services」を入れてから、もう一度押してください。", "Gaming Services is not installed. Install \"Gaming Services\" from the Microsoft Store, then press it again."));
    case 3: throw std::runtime_error(Lang::T("Minecraftが入っていません。先にXboxアプリかMicrosoft StoreからMinecraftを入れてください（購入済みのMinecraftを別の版に置き換える仕組みです）。", "Minecraft is not installed. Install Minecraft from the Xbox app or Microsoft Store first (this replaces your purchased Minecraft with another version)."));
    default: throw std::runtime_error(Lang::T("Minecraftの状態を確認できませんでした（", "Could not check Minecraft's state (") + std::to_string(code) + "）: " + LogTail(folder / "switch-check.log"));
    }
    if (GamePidAny()) throw std::runtime_error(Lang::T("Minecraftを終了してから版を変更してください。起動中のゲームは終了しません。", "Close Minecraft before changing the version. A running game is not closed for you."));
    if (urls.empty()) throw std::runtime_error(Lang::T("通常起動の版を変更するためのダウンロード先を取得できません。公開版の一覧を更新してから選び直してください。", "Cannot get the download for changing the normal-launch version. Refresh the published version list and choose again."));
    switchPhase = 1;
    // The msixvc stays in the cache (design 2026-11-19 §3): the next switch to this version needs no download.
    const auto cache = PackageCacheFolder();
    const auto package = cache / PackageCache::FileName(id);
    std::error_code ignored;
    fs::create_directories(cache, ignored);
    const bool cached = fs::is_regular_file(package, ignored);
    if (cached) fs::last_write_time(package, fs::file_time_type::clock::now(), ignored);   // most recently used
    else
    {
        TrimPackageCache(id, 1);   // leave room for this version
        DownloadToCache(id, urls, package);
    }
    if (!cached) CacheLog("downloaded Minecraft " + id + " into the cache");
    else CacheLog("Minecraft " + id + " from the cache, no download");
    switchPhase = 2;
    // A damaged file is never kept under its finished name, whether it came from the cache or was just
    // downloaded again (2026-11-20 report #9). A cached file is downloaded once more; a fresh one is not.
    DWORD code = 0;
    for (bool again = cached;;)
    {
        code = RunSwitchScript(script, L"install", package, helper, folder / "switch-install.log", id);
        if (!code || code == 5 || code == 11 || !PackageCache::PackageFileDamaged(LogTail(folder / "switch-install.log", 4000))) break;
        fs::remove(package, ignored);
        CacheLog("the package file of " + id + " was unusable and was deleted");
        if (!again) break;
        again = false;
        switchPhase = 1;
        DownloadToCache(id, urls, package);
        switchPhase = 2;
    }
    if (code == 11) throw std::runtime_error(FolderRegistrationMessage(LogTail(folder / "switch-install.log")));
    if (code == 0 || code == 5) TrimPackageCache(id, 0);
    if (code == 5)
        warning = Lang::T("起動役（gamelaunchhelper.dll）を差し替えられませんでした。Xboxアプリから起動すると最新版への更新を求められることがあります: ", "Could not replace the launch helper (gamelaunchhelper.dll). The Xbox app may ask you to update to the latest version when launching: ") +
                  LogTail(folder / "switch-install.log");
    else if (code)
    {
        // Installed some other way than the Xbox app / Store: say so, the reason is usually there.
        const bool notStore = LogTail(folder / "switch-check.log").find("Not from the Store") != std::string::npos;
        const std::string log = LogTail(folder / "switch-install.log", 2400);
        const std::string hint = InstallErrorHint(log);
        throw std::runtime_error(Lang::T("Windowsがインストールできませんでした（", "Windows could not install it (") + std::to_string(code) + "）: " + (hint.empty() ? "" : hint + "\n") + log +
                                 (notStore ? Lang::T("\n今のMinecraftはXboxアプリ・Microsoft Store以外の方法で入っているため、Windowsが置き換えを断った可能性があります。", "\nThe current Minecraft was installed some way other than the Xbox app or Microsoft Store, so Windows may have refused to replace it.") : ""));
    }
}

void StartSwitch(const std::string& id, bool launchAfter)
{
    if (state != State::Idle) return;
    const auto release = std::find_if(catalog.releases.begin(), catalog.releases.end(), [&](const Release& item) {
        return item.target.id == id;
    });
    if (release == catalog.releases.end() || !ClientVersionSelectable(id, release->available))
        throw std::runtime_error(Lang::T("この版はOriginX未対応のため、インストール・切替できません。対応済みの26.45以降を選んでください。", "OriginX does not support this version, so it cannot be installed or switched to. Choose a supported version, 26.45 or later."));
    const auto links = versionLinks.find(id);
    Save(); // keep the previous selection until Windows confirms the requested version
    const auto folder = privateDirectory / "cache" / "game-switch";
    WriteSwitchScript(folder, "switch-game.ps1");
    // The replacement bootstrapper built from launcher/vendor/gamelaunchhelper (absent in local builds).
    fs::path helper;
    if (HRSRC found = FindResourceW(nullptr, L"GAMELAUNCHHELPER", MAKEINTRESOURCEW(10)))
    {
        HGLOBAL loaded = LoadResource(nullptr, found);
        const void* bytes = loaded ? LockResource(loaded) : nullptr;
        const DWORD size = SizeofResource(nullptr, found);
        if (bytes && size)
        {
            helper = folder / "gamelaunchhelper.dll";
            std::ofstream out(helper, std::ios::binary | std::ios::trunc);
            out.write(static_cast<const char*>(bytes), size);
            if (!out) throw std::runtime_error(Lang::T("版の変更の準備ファイルを書き込めません。", "Cannot write the version change file."));
        }
    }
    if (switchThread.joinable()) switchThread.join();
    switchingId = id;
    switchPhase = 0;
    switchDone = 0;
    switchTotal = 0;
    Busy(State::Switching);
    BeginProgress();
    Status(L"Minecraft " + Wide(id) + Lang::T(L" の通常起動を確認・変更しています…", L": checking / changing the normal launch…"));
    const auto urls = links != versionLinks.end() ? links->second : std::vector<std::string>{};
    switchThread = std::thread([id, urls, folder, helper, launchAfter] {
        auto result = new SwitchResult{id, {}, {}, launchAfter};
        try { SwitchGame(id, urls, folder, helper, result->warning); }
        catch (const std::exception& error) { result->error = error.what(); }
        if (!PostMessageW(window, SwitchedMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

// A Versions card picks the version OriginX plays. With the Settings switch on, Windows' normal-launch version
// follows (StartSwitch: no download when Windows already has it); off, Windows is not touched (user, 2026-11-09).
void SelectUserVersion(const std::string& id)
{
    if (syncNormal) { StartSwitch(id); return; }   // failures keep the OriginX choice: see SwitchedMessage
    const auto release = std::find_if(catalog.releases.begin(), catalog.releases.end(), [&](const Release& item) {
        return item.target.id == id;
    });
    if (release == catalog.releases.end() || !ClientVersionSelectable(id, release->available))
        throw std::runtime_error(Lang::T("この版はOriginX未対応のため、選べません。対応済みの26.45以降を選んでください。", "OriginX does not support this version, so it cannot be chosen. Choose a supported version, 26.45 or later."));
    RefreshVersions(id);
    Save();
    const bool here = installedGames.count(id) || !Text(Game).empty();
    Status(Lang::T(L"OriginX の版を ", L"OriginX version set to ") + Wide(id) + Lang::T(L" にしました（Windowsの通常起動の版はそのまま）。", L" (Windows' normal-launch version is unchanged).") +
           (here ? std::wstring{} : Lang::T(L"\r\nこの版はまだこの PC にありません。Launch を押すと、確認のあとダウンロードして Windows に入れます。", L"\r\nThis version is not on this PC yet. Pressing Launch downloads it and installs it in Windows after asking.")));
    GoTo(0);
}

void PollSwitchProgress()
{
    const unsigned long long done = switchDone, total = switchTotal;
    int percent = 0;
    std::wstring detail;
    if (switchPhase == 0) detail = Lang::T(L"Gaming Services と今の Minecraft を確認しています…", L"Checking Gaming Services and the current Minecraft…");
    else if (switchPhase == 1)
    {
        percent = total ? static_cast<int>(std::min<unsigned long long>(100, done * 100 / total)) : 0;
        wchar_t size[96];
        swprintf(size, 96, L"%.2f / %.2f GB", done / 1073741824.0, total / 1073741824.0);
        detail = std::wstring(Lang::T(L"Microsoft の配信サーバーからダウンロード中  ", L"Downloading from Microsoft's servers  ")) + size;
    }
    else
    {
        percent = 100;
        detail = Lang::T(L"Windows がインストールしています。数分かかることがあります…", L"Windows is installing. This can take a few minutes…");
    }
    const std::wstring text = Lang::T(L"Minecraft を ", L"Changing Minecraft to ") + Wide(switchingId) + Lang::T(L" に変更中  ", L"  ") + std::to_wstring(percent) + L"%\r\n" + detail +
                              Lang::T(L"\r\nこのウィンドウは閉じないでください。", L"\r\nDo not close this window.");
    if (percent == progressPercent && text == progressText) return;
    progressPercent = percent;
    progressText = text;
    StatusQuiet(text);
    const RECT area = ProgressBarArea();
    InvalidateRect(window, &area, TRUE);
}

void CheckForUpdates()
{
    Busy(State::Checking);
    Status(Lang::T(L"対応バージョンとDLLの更新を確認しています…", L"Checking supported versions and DLL updates…"));
    const auto previous = catalog;
    preparation = std::thread([previous] {
        auto result = new UpdateResult;
        const auto temporary = privateDirectory / "catalog.download";
        try {
            if (signatureStopped) throw std::runtime_error(SignatureFailedMessage());
            DownloadSignedList(CatalogUrl, "catalog/stable", temporary, 1024 * 1024);
            result->catalog = ParseCatalog(ReadJson(temporary));
            CheckAdvance(previous, result->catalog);
            KeepSignedList(temporary, privateDirectory / "catalog.json", Lang::T("更新一覧を保存できません。", "Cannot save the update list."));
        }
        catch (const std::exception& error) { result->error = error.what(); }
        std::error_code ignored; fs::remove(temporary, ignored); fs::remove(SigPath(temporary), ignored);
        if (!PostMessageW(window, UpdatedMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

void FetchAndApply()
{
    if (!selected.available) throw std::runtime_error(selected.reason);
    Save();
    Busy(State::Fetching);
    Status(Lang::T(L"選択した版のDLLを取得・検査しています…\r\n保存済みの同じ版は再ダウンロードしません。", L"Fetching and checking the DLL for the chosen version…\r\nA saved copy of the same version is not downloaded again."));
    const auto release = selected;
    const Channel wanted = wantedChannel;
    const StoredAccount account = LoadAccount(privateDirectory);
    const std::string token = account.token;
    const long long verifiedAt = account.verifiedAt;
    const unsigned epoch = accountEpoch.Now();
    preparation = std::thread([release, wanted, token, verifiedAt, epoch] {
        auto result = new PackageResult;
        result->epoch = epoch;
        try {
            // The chosen channel first (docs/superpowers/specs/2026-11-09-release-channels-design.md): any problem is a
            // reason shown to the user and the stable release is used, so Launch is never stopped by a channel.
            AccountStatus status;   // alive until the end: the plan points into it
            // Members only (Channels.h MemberGate): asked on every launch, whatever the channel.
            const AccountStatus* known = nullptr;
            std::string error;
            if (!token.empty())
            {
                try { status = FetchStatus(token); known = &status; result->status = status; }
                catch (const std::exception& e) { error = e.what(); }
            }
            const std::string refused = MemberGate(!token.empty(), known, error, verifiedAt, static_cast<long long>(std::time(nullptr)));
            if (!refused.empty()) throw std::runtime_error(refused);
            if (wanted != Channel::Stable && known && !known->signatureProblem.empty())
                result->note = std::string(ChannelName(wanted)) + Lang::T("を使えないため、安定版で起動します。理由: ", " cannot be used, so launching on Stable. Reason: ") + SignatureFailedMessage();
            else if (wanted != Channel::Stable)
            {
                const LaunchPlan plan = PlanLaunch(wanted, known, error, release.target.id, &release);
                result->note = plan.reason;
                if (plan.release)
                {
                    try
                    {
                        result->root = AcquirePackage(privateDirectory / "packages", *plan.release, Sha256File, DownloadUnlessStopped,
                                                      PackageFolder(plan.channel, *plan.release));
                        result->local = false;
                        result->channel = plan.channel;
                        result->target = plan.release->target;
                    }
                    catch (const std::exception& e)
                    {
                        // e.g. the signed link expired during a long download: nothing half-written is used
                        result->note = std::string(ChannelName(wanted)) + Lang::T("を取得できなかったため、安定版で起動します。理由: ", " could not be fetched, so launching on Stable. Reason: ") + e.what();
                        result->root.clear();
                    }
                }
            }
            if (result->root.empty())
            {
                bool local = false;
                for (const auto& candidate : bundledCatalog.releases)
                    if (candidate.available && candidate.target.id == release.target.id && candidate.files == release.files &&
                        candidate.revision == release.revision && candidate.target.loader == release.target.loader) local = true;
                result->local = local;
                if (!local)
                    result->root = AcquirePackage(privateDirectory / "packages", release, Sha256File, DownloadUnlessStopped);
                else
                {
                    // Bundled files: taken as they are in the MyClient folder (a DLL rebuilt and copied in by hand
                    // works without editing catalog.json); see LocalRelease.
                    const auto bundled = [release](const std::string& key) {
                        // Extra game versions would ship under versions/<id>/ (none now: only 26.51.1, at the bundle root).
                        // The HUD of a bundle sits beside the launcher (MyClient.exe), not under hud/.
                        const std::string name = key == HudFile ? std::string("MyClient.exe") : key;
                        auto source = SafeChild(bundle / "versions" / fs::u8path(release.target.id), name);
                        if (!fs::is_regular_file(source)) source = SafeChild(bundle, name);
                        return source;
                    };
                    std::string folder;
                    const Release onDisk = LocalRelease(release, bundled, Sha256File, folder);
                    result->root = AcquirePackage(privateDirectory / "packages", onDisk, Sha256File,
                        [onDisk, bundled](const std::string& url, const fs::path& destination, uint64_t) {
                            for (const auto& file : onDisk.files.items())
                                if (file.value().at("url") == url) { fs::copy_file(bundled(file.key()), destination); return; }
                            throw std::runtime_error(Lang::T("同梱更新ファイルが見つかりません。", "Bundled update file not found."));
                        }, folder);
                }
            }
        }
        catch (const std::exception& error) { result->error = error.what(); }
        if (!PostMessageW(window, PackageMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

// Real controls are shown only when their screen is at rest (pictures of them are drawn while sliding).
void ShowPage()
{
    const int settings[] = {Game, PickGame, AutoDetect, ImportGame, CheckUpdates, Guide};
    for (int id : settings) ShowWindow(GetDlgItem(window, id), !scrolling && page == 1 && settingsTab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(window, Apply), !scrolling && page == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(window, HomeSettings), !scrolling && page == 0 ? SW_SHOW : SW_HIDE);
    SyncVersionCards();
    Refresh();
}

constexpr UINT_PTR SlideTimer = 2;
constexpr double SlideMs = 320.0;

// Slide between Home (0) and Settings (1). Settings is below Home: Home moves up out of view.
void GoTo(int target)
{
    if (scrolling || target == page) return;
    scrollFrom = scroll;
    page = target;
    scrolling = true;
    scrollStart = GetTickCount64();
    ShowPage();
    if (!SetTimer(window, SlideTimer, 15, nullptr)) { scrolling = false; scroll = page; ShowPage(); }
}

void AdvanceSlide()
{
    const double t = std::min(1.0, (GetTickCount64() - scrollStart) / SlideMs);
    const double eased = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    scroll = scrollFrom + (page - scrollFrom) * eased;
    if (t >= 1.0)
    {
        KillTimer(window, SlideTimer);
        scroll = page;
        scrolling = false;
        ShowPage();
    }
    Refresh();
}

void OpenLogs()
{
    ShellExecuteW(window, L"open", (fs::exists(privateDirectory / "runtime-setup.log") ?
        privateDirectory / "runtime-setup.log" : privateDirectory / "launcher.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Started by the launcher that just installed this one (--after-update): our window is up, so leave the mark
// it waits for, then take the single-instance lock it lets go of, and only then clear its .old copy.
void TakeOverFromUpdater()
{
    { std::ofstream(StartedMarkPath(privateDirectory, afterUpdateFrom)) << "started\n"; }
    const DWORD wait = singleInstance ? WaitForSingleObject(singleInstance, 15000) : WAIT_FAILED;
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) RemoveOldLauncher(launcherExe);
}

void BuildUi()
{
    // Hidden data holders: the version list and the status text keep their old roles.
    ShowWindow(Control(L"STATIC", L"", 0, 0, 10, 10, StatusText), SW_HIDE);
    editable.push_back(Control(L"COMBOBOX", L"", 0, 0, 10, 200, Version, CBS_DROPDOWNLIST | WS_VSCROLL));
    ShowWindow(GetDlgItem(window, Version), SW_HIDE);
    // Home: the launch and settings buttons in the middle of the window.
    editable.push_back(Control(L"BUTTON", L"Launch", 452, 318, 260, 62, Apply, WS_TABSTOP | BS_OWNERDRAW));
    Control(L"BUTTON", L"Settings", 452, 456, 260, 50, HomeSettings, WS_TABSTOP | BS_OWNERDRAW);   // (under Launch: the version at 384, the normal-launch line at 422)
    // Settings page.
    editable.push_back(Control(L"EDIT", L"", 328, 116, 516, 42, Game, WS_BORDER | ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP));
    editable.push_back(Control(L"BUTTON", Lang::T(L"自動検出", L"Auto-detect"), 856, 116, 126, 42, AutoDetect, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", Lang::T(L"選択…", L"Select…"), 990, 116, 126, 42, PickGame, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", Lang::T(L"フォルダーを追加…", L"Add folder…"), 896, 210, 220, 42, ImportGame, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", Lang::T(L"更新を確認", L"Check for updates"), 896, 318, 220, 42, CheckUpdates, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", Lang::T(L"使い方・必要な部品", L"Guide & required components"), 896, 426, 220, 42, Guide, WS_TABSTOP | BS_OWNERDRAW));
    ShowPage();
    // Downloaded on its own (no folder around it), the launcher has no bundled list: the list comes from the
    // update check right below, and everything it names is downloaded.
    // The retired-key mark is needed before the saved lists below are checked.
    try
    {
        if (fs::exists(privateDirectory / "launcher.json"))
        {
            const auto early = ReadJson(privateDirectory / "launcher.json");
            for (const char* mark : {"minSigningKey", "minTestSigningKey"})
                if (early.contains(mark) && early.at(mark).is_number_unsigned())
                    NoteSigningKey(static_cast<uint32_t>(std::min<uint64_t>(early.at(mark).get<uint64_t>(), 0xFFFF)));
        }
    }
    catch (const std::exception&) { /* read again, with its errors shown, below */ }
    if (fs::is_regular_file(bundle / "catalog.json")) bundledCatalog = ParseCatalog(ReadJson(bundle / "catalog.json"));
    catalog = bundledCatalog;
    if (fs::exists(privateDirectory / "catalog.json"))
    {
        try {
            if (!SavedListVerified(privateDirectory / "catalog.json", "catalog/stable"))
                throw std::runtime_error(Lang::T("署名がないか合わないため使いません。更新の確認で取り直します。", "it has no valid signature; the update check fetches it again."));
            auto cached = ParseCatalog(ReadJson(privateDirectory / "catalog.json"));
            // A list saved by an older bundle (e.g. one still naming 26.32 / 26.40) is superseded by the bundled one.
            if (cached.sequence < catalog.sequence) { std::error_code ignored; fs::remove(privateDirectory / "catalog.json", ignored); }
            else { CheckAdvance(catalog, cached); catalog = std::move(cached); }
        }
        catch (const std::exception& error) { Status(Lang::T(L"保存済み一覧を使用できません: ", L"Cannot use the saved list: ") + Wide(error.what())); }
    }
    std::string preferred = "26.51.1";
    if (fs::exists(privateDirectory / "launcher.json"))
    {
        const auto config = ReadJson(privateDirectory / "launcher.json");
        if (config.value("schemaVersion", 0) == 3)
        {
            sources = config.at("sources");
            if (!sources.is_object()) throw std::runtime_error(Lang::T("保存した本体一覧が不正です。", "The saved game list is invalid."));
            preferred = config.value("selectedVersion", preferred);
            if (config.contains("imports") && config.at("imports").is_array()) imports = config.at("imports");
            if (config.contains("xboxSeen") && config.at("xboxSeen").is_string()) xboxSeen = config.at("xboxSeen").get<std::string>();
            if (config.contains("channel") && config.at("channel").is_string()) wantedChannel = ParseChannel(config.at("channel").get<std::string>());
            if (config.contains("syncNormal") && config.at("syncNormal").is_boolean()) syncNormal = config.at("syncNormal").get<bool>();
            if (config.contains("skippedLauncherBuild") && config.at("skippedLauncherBuild").is_number_unsigned())
                skippedLauncherBuild = config.at("skippedLauncherBuild").get<uint64_t>();
            for (const char* mark : {"minSigningKey", "minTestSigningKey"})
                if (config.contains(mark) && config.at(mark).is_number_unsigned())
                    NoteSigningKey(static_cast<uint32_t>(std::min<uint64_t>(config.at(mark).get<uint64_t>(), 0xFFFF)));
        }
        else sources["26.51.1"] = config.value("game", std::string{});
    }
    if (fs::exists(privateDirectory / "versions_db.json"))
    {
        try { LoadVersionDb(privateDirectory / "versions_db.json"); }
        catch (const std::exception&) { /* Replaced by the download below. */ }
    }
    ScanInstalled();
    // The Xbox app updates the game by itself (e.g. 26.51.1 -> 26.60.5). When its install changed to another
    // version MyClient supports, that version is chosen; a version picked by hand stays until the install changes.
    for (const auto& [id, path] : installedGames)
    {
        if (Lower(path.u8string()).find("xboxgames") == std::string::npos || id == xboxSeen) continue;
        for (const auto& release : catalog.releases)
            if (release.available && release.target.id == id) preferred = id;
        xboxSeen = id;
    }
    RefreshVersions(preferred);
    FetchVersionDb();
    QueryNormalVersion();
    LoadSavedRemoteStop();
    CheckLauncherUpdate();
    if (!SetTimer(window, 1, 500, nullptr)) throw std::runtime_error(Lang::T("接続確認タイマーを開始できません。", "Cannot start the connection check timer."));
    PostMessageW(window, CheckMessage, 0, 0);
    // The saved Discord login: the name and icon are drawn at once, the roles are asked in the background.
    storedAccount = LoadAccount(privateDirectory);
    ReloadAvatar();
    StartAccount(false);
    if (afterUpdateFrom) TakeOverFromUpdater();
}


// ----------------------------- Reskinned window painting -----------------------------
RECT MR(int x, int y, int w, int h) { return RECT{Sc(x), Sc(y), Sc(x + w), Sc(y + h)}; }
// Settings: tabs and Return on the left panel, the log file link on the Logs tab.
RECT TabRect(int i) { return MR(44, 120 + i * 64, 220, 54); }
RECT ReturnRect() { return MR(44, 650, 220, 56); }
RECT LogFileLink() { return MR(960, 48, 156, 30); }
// Home: links in the corners.
RECT HomeVersionsLink() { return MR(904, 28, 104, 30); }
RECT HomeLogsLink() { return MR(1020, 28, 104, 30); }
RECT VersionCardRect(int i) { return MR(328, 136 + i * 154, 788, 140); }

std::vector<VersionRow> CurrentVersionRows()
{
    std::vector<CatalogEntry> entries;
    for (const auto& release : catalog.releases) entries.push_back({release.target.id, release.available});
    std::vector<std::string> installed;
    for (const auto& [id, path] : installedGames) installed.push_back(id);
    std::vector<std::string> downloadable;
    for (const auto& [id, urls] : versionLinks) downloadable.push_back(id);
    const auto rows = ClientVersionRows(VersionRows(publishedVersions, entries, installed, downloadable));
    std::string description;
    for (const auto& row : rows) description += "id=" + row.id + " supported=" + (row.supported ? "1" : "0") +
        " installed=" + (row.installed ? "1" : "0") + " downloadable=" + (row.downloadable ? "1" : "0") +
        " selectable=" + (ClientVersionSelectable(row.id, row.supported) ? "1" : "0") +
        " reason=" + (!ClientVersionSelectable(row.id, row.supported) ? "unsupported/outside supported version range" :
            row.installed ? "selectable/installed" : row.downloadable ? "selectable/downloadable" : "selectable/no distribution link") + "\n";
    static std::string previous;
    if (description != previous) { previous = description; VersionDiagnostic("rows changed\n" + description); }
    return rows;
}

// The card shows the OriginX choice (what Launch starts), not Windows' normal-launch version:
// after a stopped or refused switch the two differ, and Launch says the chosen one (2026-11-19).
size_t VersionCardChoice(const VersionFamilyRows& family)
{
    for (const auto& id : {switchingId, selected.target.id, normalVersionId})
        if (!id.empty())
            for (size_t i = 0; i < family.rows.size(); ++i) if (family.rows[i].id == id) return i;
    for (size_t i = 0; i < family.rows.size(); ++i) if (family.rows[i].supported) return i;
    return 0;
}

std::wstring DisplayGameVersion(const std::string& id)
{
    return Wide(id.rfind("1.", 0) == 0 ? id : "1." + id);
}

void SyncVersionCards()
{
    versionFamilies = GroupVersionRows(CurrentVersionRows());
    for (int slot = 0; slot < VersionCardsShown; ++slot)
    {
        HWND& control = versionCardControls[slot];
        const size_t index = static_cast<size_t>(slot);
        const bool visible = !scrolling && page == 1 && settingsTab == 1 && index < versionFamilies.size();
        if (!visible) { if (control) ShowWindow(control, SW_HIDE); continue; }
        if (!control)
        {
            control = Control(L"COMBOBOX", L"", 700, 174 + slot * 154, 392, 270,
                              VersionCardControl + slot, CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                              false, false); // Keep native white chrome hidden until custom painting and selection are ready.
            if (!control) throw std::runtime_error(Lang::T("バージョン選択欄を作成できません。", "Cannot create the version selector."));
            COMBOBOXINFO info{};
            info.cbSize = sizeof(info);
            if (!GetComboBoxInfo(control, &info) || !info.hwndList)
                throw std::runtime_error(Lang::T("バージョン一覧の表示を準備できません。", "Cannot prepare the version list display."));
            nativeVersionSelectorProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(control, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(VersionSelectorProc)));
            if (!nativeVersionSelectorProc) throw std::runtime_error(Lang::T("バージョン選択欄の配色を設定できません。", "Cannot set the version selector colors."));
            nativeVersionListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(info.hwndList, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(VersionListProc)));
            if (!nativeVersionListProc) throw std::runtime_error(Lang::T("バージョン一覧の配色を設定できません。", "Cannot set the version list colors."));
            SendMessageW(control, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Sc(32));
        }
        SendMessageW(control, CB_RESETCONTENT, 0, 0);
        const auto& family = versionFamilies[index];
        for (const auto& row : family.rows)
        {
            const auto label = DisplayGameVersion(row.id) + (!row.supported ? Lang::T(L"　未対応・選択不可", L"  unsupported, cannot choose") : row.installed ? Lang::T(L"　インストール済み", L"  installed") :
                row.downloadable ? Lang::T(L"　ダウンロード", L"  download") : Lang::T(L"　配信リンクなし", L"  no download link"));
            if (SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())) < 0)
                throw std::runtime_error(Lang::T("バージョン一覧を表示できません。", "Cannot show the version list."));
        }
        SendMessageW(control, CB_SETCURSEL, VersionCardChoice(family), 0);
        EnableWindow(control, state == State::Idle);
        ShowWindow(control, SW_SHOW);
        RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
    }
    Refresh();
}

void Box(HDC dc, RECT r, int radius, COLORREF fill, COLORREF edge, bool border = true)
{
    HPEN pen = border ? CreatePen(PS_SOLID, 1, edge) : nullptr;
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldPen = SelectObject(dc, border ? static_cast<HGDIOBJ>(pen) : GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, Sc(radius) * 2, Sc(radius) * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    if (pen) DeleteObject(pen);
}

void Txt(HDC dc, const std::wstring& text, RECT r, HFONT f, COLORREF colour,
         UINT flags = DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colour);
    HGDIOBJ old = SelectObject(dc, f);
    DrawTextW(dc, text.c_str(), -1, &r, flags);
    SelectObject(dc, old);
}

// A Yes/No question in the launcher's own colours (user, 2026-11-19: the white MessageBox looked out of place).
// Modal like MessageBoxW: the owner is disabled while it is open. Enter = yes, Esc or closing = no.
struct ConfirmState { std::wstring title, body, yes, no; int hot = -1; bool done = false, answer = false; };
RECT ConfirmButton(const RECT& client, int index)
{
    const int w = Sc(150), h = Sc(44), gap = Sc(12), pad = Sc(24);
    const int right = client.right - pad - (index == 0 ? 0 : w + gap);
    return RECT{right - w, client.bottom - pad - h, right, client.bottom - pad};
}
LRESULT CALLBACK ConfirmProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<ConfirmState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC target = BeginPaint(window, &ps);
        RECT client; GetClientRect(window, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        HBRUSH bg = CreateSolidBrush(ColBg); FillRect(dc, &client, bg); DeleteObject(bg);
        Box(dc, RECT{0, 0, client.right, client.bottom}, 0, ColPanel, ColAccent);
        HBRUSH bar = CreateSolidBrush(ColAccent); RECT top{0, 0, client.right, Sc(4)}; FillRect(dc, &top, bar); DeleteObject(bar);
        const int pad = Sc(24);
        Txt(dc, state->title, RECT{pad, Sc(20), client.right - pad, Sc(56)}, boldFont, RGB(255, 255, 255),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        Txt(dc, state->body, RECT{pad, Sc(64), client.right - pad, client.bottom - Sc(84)}, font, RGB(196, 208, 228),
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        for (int i = 0; i < 2; ++i)
        {
            const RECT b = ConfirmButton(client, i);
            const bool hot = state->hot == i;
            if (i == 0) Box(dc, b, 10, hot ? ColAccentHi : ColAccent, ColAccent, false);
            else Box(dc, b, 10, hot ? ColSecondaryDown : ColSecondary, ColBorder);
            Txt(dc, i == 0 ? state->yes : state->no, b, boldFont, i == 0 ? RGB(255, 255, 255) : ColText,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        RECT client; GetClientRect(window, &client);
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int hot = -1;
        for (int i = 0; i < 2; ++i) { const RECT b = ConfirmButton(client, i); if (PtInRect(&b, pt)) hot = i; }
        if (hot != state->hot) { state->hot = hot; InvalidateRect(window, nullptr, FALSE); }
        TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, window, 0}; TrackMouseEvent(&track);
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(hot >= 0 ? 32649 : 32512)));
        return 0;
    }
    case WM_MOUSELEAVE:
        if (state->hot != -1) { state->hot = -1; InvalidateRect(window, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONUP:
    {
        RECT client; GetClientRect(window, &client);
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        for (int i = 0; i < 2; ++i)
        {
            const RECT b = ConfirmButton(client, i);
            if (PtInRect(&b, pt)) { state->answer = i == 0; state->done = true; DestroyWindow(window); return 0; }
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) { state->answer = wParam == VK_RETURN; state->done = true; DestroyWindow(window); }
        return 0;
    case WM_CLOSE:
        state->done = true; DestroyWindow(window); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
bool ConfirmDialog(HWND owner, const std::wstring& title, const std::wstring& body, const std::wstring& yes, const std::wstring& no)
{
    static bool registered = false;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registered)
    {
        WNDCLASSW type{};
        type.hInstance = instance; type.lpfnWndProc = ConfirmProc; type.lpszClassName = L"OriginXConfirm";
        type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        registered = RegisterClassW(&type) != 0;
    }
    ConfirmState state{title, body, yes, no};
    const int w = Sc(560), h = Sc(280);
    RECT o{}; GetWindowRect(owner, &o);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"OriginXConfirm", title.c_str(), WS_POPUP,
        o.left + (o.right - o.left - w) / 2, o.top + (o.bottom - o.top - h) / 2, w, h, owner, nullptr, instance, &state);
    if (!window) return MessageBoxW(owner, body.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW); SetForegroundWindow(window); SetFocus(window);
    MSG message{};
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (message.message == WM_QUIT) PostQuitMessage(static_cast<int>(message.wParam));
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return state.answer;
}

void DrawVersionItem(const DRAWITEMSTRUCT& item)
{
    if (!item.hDC || item.CtlID < VersionCardControl || item.CtlID >= VersionCardControl + VersionCardsShown) return;
    const size_t slot = item.CtlID - VersionCardControl;
    const VersionRow* row = nullptr;
    if (slot < versionFamilies.size() && item.itemID < versionFamilies[slot].rows.size())
        row = &versionFamilies[slot].rows[item.itemID];
    const bool enabled = row && ClientVersionSelectable(row->id, row->supported) && !(item.itemState & ODS_DISABLED);
    const bool highlighted = enabled && (item.itemState & ODS_SELECTED);
    Box(item.hDC, item.rcItem, 0, highlighted ? ColTile : ColPanel, 0, false);
    if (!row) return;
    const auto label = DisplayGameVersion(row->id) + (!row->supported ? Lang::T(L"　未対応・選択不可", L"  unsupported, cannot choose") :
        row->installed ? Lang::T(L"　インストール済み", L"  installed") : row->downloadable ? Lang::T(L"　ダウンロード", L"  download") : Lang::T(L"　配信リンクなし", L"  no download link"));
    RECT text = item.rcItem;
    text.left += Sc(10);
    text.right -= Sc(8);
    Txt(item.hDC, label, text, font, enabled ? ColText : RGB(77, 84, 100),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (enabled && (item.itemState & ODS_FOCUS)) DrawFocusRect(item.hDC, &item.rcItem);
}

void ClearVersionSelector(HDC dc, const RECT& r)
{
    if (!dc) return;
    const auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    if (!brush) return;
    const COLORREF previous = SetDCBrushColor(dc, ColSide);
    FillRect(dc, &r, brush);
    SetDCBrushColor(dc, previous);
}

void DrawVersionSelector(HWND handle, HDC dc)
{
    if (!handle || !dc) return;
    RECT r{};
    if (!GetClientRect(handle, &r)) return;
    // RoundRect does not touch its four outside corners. Clear the entire client area first.
    ClearVersionSelector(dc, r);
    const bool enabled = IsWindowEnabled(handle) != FALSE;
    const bool focused = GetFocus() == handle;
    Box(dc, r, 6, ColPanel, enabled && focused ? ColAccentHi : ColLine);
    const int arrowWidth = Sc(34);
    const int centreX = r.right - arrowWidth / 2, centreY = (r.top + r.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, std::max(1, Sc(2)), enabled ? ColMuted : RGB(77, 84, 100));
    if (pen)
    {
        const HGDIOBJ previous = SelectObject(dc, pen);
        MoveToEx(dc, centreX - Sc(5), centreY - Sc(2), nullptr);
        LineTo(dc, centreX, centreY + Sc(3));
        LineTo(dc, centreX + Sc(5), centreY - Sc(2));
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    const LRESULT choice = SendMessageW(handle, CB_GETCURSEL, 0, 0);
    const int id = GetDlgCtrlID(handle) - VersionCardControl;
    if (id < 0 || static_cast<size_t>(id) >= versionFamilies.size() || choice == CB_ERR ||
        static_cast<size_t>(choice) >= versionFamilies[static_cast<size_t>(id)].rows.size()) return;
    DRAWITEMSTRUCT item{};
    item.CtlID = GetDlgCtrlID(handle);
    item.itemID = static_cast<UINT>(choice);
    item.hDC = dc;
    item.rcItem = r;
    InflateRect(&item.rcItem, -Sc(2), -Sc(2));
    item.rcItem.right -= arrowWidth;
    item.itemState = enabled ? 0 : ODS_DISABLED;
    DrawVersionItem(item);
}

void DrawVersionBorder(HWND handle)
{
    if (!handle) return;
    RECT r{};
    if (!GetWindowRect(handle, &r)) return;
    r = RECT{0, 0, r.right - r.left, r.bottom - r.top};
    HDC dc = GetWindowDC(handle);
    if (!dc) return;
    HBRUSH brush = CreateSolidBrush(ColLine);
    if (brush)
    {
        // Cover both pixels of the standard raised white popup frame.
        FrameRect(dc, &r, brush);
        InflateRect(&r, -1, -1);
        FrameRect(dc, &r, brush);
        DeleteObject(brush);
    }
    ReleaseDC(handle, dc);
}

LRESULT CALLBACK VersionSelectorProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(handle, &paint);
        if (dc) DrawVersionSelector(handle, dc);
        EndPaint(handle, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT)
    {
        if (wParam) DrawVersionSelector(handle, reinterpret_cast<HDC>(wParam));
        return 0;
    }
    if (message == WM_ERASEBKGND)
    {
        RECT r{};
        if (wParam && GetClientRect(handle, &r)) ClearVersionSelector(reinterpret_cast<HDC>(wParam), r);
        return 1;
    }
    const LRESULT result = nativeVersionSelectorProc ? CallWindowProcW(nativeVersionSelectorProc, handle, message, wParam, lParam) :
        DefWindowProcW(handle, message, wParam, lParam);
    if (message == WM_NCPAINT) DrawVersionBorder(handle);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
        message == CB_SETCURSEL || message == CB_SHOWDROPDOWN || message == WM_LBUTTONDOWN ||
        message == WM_LBUTTONUP || message == WM_KEYDOWN || message == WM_KEYUP)
        InvalidateRect(handle, nullptr, FALSE);
    return result;
}

LRESULT CALLBACK VersionListProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    const LRESULT result = nativeVersionListProc ? CallWindowProcW(nativeVersionListProc, handle, message, wParam, lParam) :
        DefWindowProcW(handle, message, wParam, lParam);
    if (message == WM_NCPAINT) DrawVersionBorder(handle);
    return result;
}

std::wstring SecondLine(const std::wstring& text)
{
    const size_t a = text.find(L"\r\n");
    if (a == std::wstring::npos) return {};
    const size_t b = text.find(L"\r\n", a + 2);
    return text.substr(a + 2, b == std::wstring::npos ? std::wstring::npos : b - a - 2);
}

// ----------------------------- Home: one full-window scene -----------------------------
COLORREF Mix(COLORREF a, COLORREF b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const auto c = [t](int x, int y) { return static_cast<BYTE>(x + (y - x) * t + 0.5); };
    return RGB(c(GetRValue(a), GetRValue(b)), c(GetGValue(a), GetGValue(b)), c(GetBValue(a), GetBValue(b)));
}

// Night sky brightening towards the horizon, so the buttons can paint their corners with the same colour.
COLORREF HomeSky(int y, int height)
{
    const double t = height > 0 ? static_cast<double>(y) / height : 0.0;
    // Black at the top, very dark blue, a bright blue glow on the horizon, black below.
    constexpr COLORREF top = RGB(3, 4, 8), navy = RGB(9, 20, 48), glow = RGB(32, 92, 200), low = RGB(4, 6, 12);
    if (t < 0.52) return Mix(top, navy, t / 0.52);
    if (t < 0.70) return Mix(navy, glow, (t - 0.52) / 0.18);
    return Mix(glow, low, (t - 0.70) / 0.30);
}

// A ridge of hills across the bottom: a few sine waves, filled down to the window edge.
void DrawHills(HDC dc, int width, int height, double base, double amplitude, double phase, COLORREF colour)
{
    std::vector<POINT> points;
    const int step = std::max(4, width / 120);
    for (int x = 0; x <= width + step; x += step)
    {
        const double u = static_cast<double>(x) / std::max(1, width);
        const double wave = std::sin(u * 7.1 + phase) * 0.55 + std::sin(u * 15.3 + phase * 2.3) * 0.3 + std::sin(u * 31.0 + phase) * 0.15;
        points.push_back({x, static_cast<LONG>(height * (base - amplitude * wave))});
    }
    points.push_back({width + step, height});
    points.push_back({0, height});
    HBRUSH brush = CreateSolidBrush(colour);
    HGDIOBJ ob = SelectObject(dc, brush), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, points.data(), static_cast<int>(points.size()));
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(brush);
}

std::wstring Greeting()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    if (t.wHour >= 5 && t.wHour < 12) return L"Good Morning!";
    if (t.wHour >= 12 && t.wHour < 18) return L"Good Afternoon!";
    return L"Good Evening!";
}

// ---- Home: the Discord account and the release channel (bottom left), the version under Launch ----
// (docs/superpowers/specs/2026-11-09-release-channels-design.md)
struct LoginResult { std::string error, token; std::optional<AccountStatus> status; bool interactive = true; unsigned epoch = 0; };
std::thread accountThread;
std::atomic<bool> loginCancel{false};
bool accountBusy = false, channelMenuOpen = false;
HBITMAP avatarBitmap = nullptr;

RECT HomeVersionUnderLaunch() { return MR(452, 384, 260, 36); }
RECT HomeNormalLine() { return MR(452, 422, 460, 26); }
RECT HomeAvatarRect() { return MR(28, 684, 34, 34); }
RECT HomeLoginButton() { return MR(28, 684, 190, 34); }
RECT HomeNameRect() { return MR(70, 684, 150, 34); }
RECT HomeChannelButton() { return MR(228, 684, 132, 34); }
RECT HomeChannelMenuRow(int i, int count) { return MR(228, 684 - 6 - (count - i) * 36, 230, 34); }

bool LoggedIn() { return !storedAccount.token.empty() && !accountExpired; }

void ReloadAvatar()
{
    if (avatarBitmap) { DeleteObject(avatarBitmap); avatarBitmap = nullptr; }
    if (!storedAccount.token.empty()) avatarBitmap = LoadAvatarBitmap(AvatarFile(privateDirectory), Sc(34));
}

// The browser login (interactive) or a refresh of the roles with the saved key, on a thread: both end in LoginMessage.
void StartAccount(bool interactive)
{
    if (accountBusy) return;
    const std::string token = interactive ? std::string{} : storedAccount.token;
    if (!interactive && token.empty()) return;
    if (accountThread.joinable()) accountThread.join();
    accountBusy = true;
    loginCancel = false;
    if (interactive) { Status(Lang::T(L"ブラウザーで Discord にログインしてください。終わるとここへ自動で戻ります。", L"Log in to Discord in your browser. You come back here automatically when done.")); Refresh(); }
    const unsigned epoch = accountEpoch.Now();
    accountThread = std::thread([interactive, token, epoch] {
        auto* result = new LoginResult;
        result->interactive = interactive;
        result->epoch = epoch;
        try
        {
            result->token = interactive ? LoginInBrowser(loginCancel) : token;
            AccountStatus status = FetchStatus(result->token);
            if (status.token.empty()) status.token = result->token;
            const fs::path icon = AvatarFile(privateDirectory);
            std::error_code ignored;
            if (status.avatarUrl.empty()) fs::remove(icon, ignored);
            else if (status.avatarUrl != LoadAccount(privateDirectory).avatarUrl || !fs::exists(icon, ignored)) FetchAvatar(status.avatarUrl, icon);
            result->status = status;
        }
        catch (const std::exception& error) { result->error = error.what(); }
        if (!PostMessageW(window, LoginMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

void Logout()
{
    loginCancel = true;
    accountEpoch.Invalidate();   // a login / refresh still running must not bring the account back
    DeleteAccount(privateDirectory);
    storedAccount = {};
    accountStatus.reset();
    accountExpired = false;
    wantedChannel = Channel::Stable;
    channelMenuOpen = false;
    ReloadAvatar();
    Save();
    Status(Lang::T(L"ログアウトしました。", L"Logged out."));
    Refresh();
}

struct MenuEntry { std::wstring label; bool logout; Channel channel; bool current; };
std::vector<MenuEntry> ChannelMenu()
{
    std::vector<MenuEntry> entries;
    const auto allowed = accountStatus ? AllowedChannels(*accountStatus) : std::vector<Channel>{Channel::Stable};
    for (Channel channel : allowed)
        entries.push_back({Wide(ChannelName(channel)) + (channel == Channel::Early ? Lang::T(L"（creator・partner）", L" (creator / partner)") : channel == Channel::Test ? L"（moderator）" : L""),
                           false, channel, channel == wantedChannel});
    entries.push_back({Lang::T(L"ログアウト", L"Log out"), true, Channel::Stable, false});
    return entries;
}

void DrawAvatar(HDC dc, RECT r)
{
    const int size = r.right - r.left;
    if (avatarBitmap)
    {
        HDC source = CreateCompatibleDC(dc);
        HGDIOBJ previous = SelectObject(source, avatarBitmap);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(dc, r.left, r.top, size, size, source, 0, 0, size, size, blend);
        SelectObject(source, previous);
        DeleteDC(source);
        return;
    }
    // no icon (none set, not downloaded, or it could not be read): a round badge with the first letter of the name
    Box(dc, r, size / 2, RGB(88, 101, 242), RGB(88, 101, 242), false);
    std::wstring name = Wide(storedAccount.name);
    const std::wstring letter = name.empty() ? std::wstring(L"D") : std::wstring(1, static_cast<wchar_t>(towupper(name[0])));
    Txt(dc, letter, r, boldFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void ChannelColours(Channel channel, COLORREF& fill, COLORREF& edge, COLORREF& text)
{
    if (channel == Channel::Test) { fill = RGB(58, 31, 8); edge = RGB(163, 90, 20); text = RGB(255, 200, 138); }
    else if (channel == Channel::Early) { fill = RGB(14, 52, 40); edge = RGB(30, 120, 90); text = RGB(150, 230, 190); }
    else { fill = RGB(15, 42, 87); edge = RGB(29, 77, 156); text = RGB(214, 228, 255); }
}

void DrawAccountArea(HDC dc)
{
    if (!LoggedIn())
    {
        const RECT button = HomeLoginButton();
        Box(dc, button, 17, RGB(15, 42, 87), RGB(29, 77, 156));
        Txt(dc, accountBusy ? Lang::T(L"ブラウザーで確認しています…", L"Waiting for the browser…") : (accountExpired ? Lang::T(L"Discord でログインし直す", L"Log in with Discord again") : Lang::T(L"Discord でログイン", L"Log in with Discord")), button, smallFont,
            RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    DrawAvatar(dc, HomeAvatarRect());
    Txt(dc, storedAccount.name.empty() ? std::wstring(L"Discord") : Wide(storedAccount.name), HomeNameRect(), smallFont, ColText,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    COLORREF fill, edge, text;
    ChannelColours(wantedChannel, fill, edge, text);
    const RECT button = HomeChannelButton();
    Box(dc, button, 17, fill, edge);
    Txt(dc, Wide(ChannelName(wantedChannel)) + L" \u25BC", button, smallFont, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (!channelMenuOpen) return;
    const auto entries = ChannelMenu();
    const int count = static_cast<int>(entries.size());
    for (int i = 0; i < count; i++)
    {
        const RECT row = HomeChannelMenuRow(i, count);
        Box(dc, row, 10, entries[i].current ? RGB(15, 42, 87) : ColPanel, ColBorder);
        Txt(dc, (entries[i].current ? L"\u25CF " : L"   ") + entries[i].label, RECT{row.left + Sc(12), row.top, row.right, row.bottom}, smallFont,
            entries[i].logout ? ColMuted : ColText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// Under Launch: the chosen game version (opens Versions); with the release channel named when it is not stable.
void DrawVersionUnderLaunch(HDC dc, const std::wstring& id)
{
    const RECT area = HomeVersionUnderLaunch();
    const std::wstring where = !selected.available ? L"waiting for DLL" : installedGames.count(selected.target.id) ? L"on this PC" : L"not on this PC";
    const std::wstring pillText = wantedChannel == Channel::Stable ? id : id + L" \u00B7 " + Wide(ChannelName(wantedChannel));
    SIZE pillSize{};
    HGDIOBJ previous = SelectObject(dc, smallFont);
    GetTextExtentPoint32W(dc, pillText.c_str(), static_cast<int>(pillText.size()), &pillSize);
    SelectObject(dc, previous);
    const int pillWidth = std::max<int>(Sc(96), pillSize.cx + Sc(32)), height = Sc(34);
    RECT pill{area.left, area.top + (area.bottom - area.top - height) / 2, area.left + pillWidth, area.top + (area.bottom - area.top - height) / 2 + height};
    COLORREF fill = ColSide, edge = ColBorder, text = ColText;
    if (wantedChannel == Channel::Test) ChannelColours(wantedChannel, fill, edge, text);
    Box(dc, pill, 17, fill, edge);
    Txt(dc, pillText, pill, smallFont, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    Txt(dc, where + L"  \u203A", RECT{pill.right + Sc(12), area.top, area.right, area.bottom}, smallFont, ColMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// The second line under Launch: what Windows starts normally, and a switch to the chosen version when they differ.
bool NormalSwitchOffered()
{
    return state == State::Idle && normalRead == NormalRead::Known && !normalVersionId.empty() && selected.available &&
           normalVersionId != selected.target.id && ClientVersionSelectable(selected.target.id, selected.available);
}

void DrawNormalLine(HDC dc)
{
    const RECT area = HomeNormalLine();
    std::wstring label = Lang::T(L"通常起動: ", L"Normal launch: "), value, link;
    if (state == State::Switching && !switchingId.empty())
        value = (normalVersionId.empty() ? std::wstring{} : Wide(normalVersionId) + L" → ") + Wide(switchingId) + Lang::T(L" に切り替え中…", L" switching…");
    else if (normalRead == NormalRead::Unknown || normalRead == NormalRead::Reading) value = Lang::T(L"確認中…", L"Checking…");
    else if (normalRead == NormalRead::Missing) value = normalReadNote.empty() ? std::wstring(Lang::T(L"読み取れません", L"unreadable")) : normalReadNote;
    else
    {
        value = Wide(normalVersionId);
        if (NormalSwitchOffered()) link = Wide(selected.target.id) + Lang::T(L" に切り替える  \u203A", L" — switch  \u203A");
        else if (normalVersionId == selected.target.id) value += Lang::T(L"  \u2713 同じ版", L"  ✓ same version");
    }
    SIZE labelSize{}, valueSize{};
    HGDIOBJ previous = SelectObject(dc, smallFont);
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &labelSize);
    GetTextExtentPoint32W(dc, value.c_str(), static_cast<int>(value.size()), &valueSize);
    SelectObject(dc, previous);
    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    Txt(dc, label, area, smallFont, ColMuted, flags);
    RECT valueArea{area.left + labelSize.cx, area.top, area.right, area.bottom};
    Txt(dc, value, valueArea, smallFont, ColText, flags);
    if (!link.empty())
        Txt(dc, link, RECT{valueArea.left + valueSize.cx + Sc(14), area.top, area.right, area.bottom}, smallFont, ColAccentHi, flags);
}

void DrawHome(HDC dc, int width, int height)
{
    for (int y = 0; y < height; y += 2)
    {
        RECT band{0, y, width, std::min(height, y + 2)};
        HBRUSH brush = CreateSolidBrush(HomeSky(y, height));
        FillRect(dc, &band, brush);
        DeleteObject(brush);
    }
    DrawHills(dc, width, height, 0.80, 0.07, 0.4, RGB(9, 18, 40));
    DrawHills(dc, width, height, 0.88, 0.05, 2.1, RGB(6, 10, 24));
    DrawHills(dc, width, height, 0.95, 0.035, 4.0, RGB(3, 4, 9));

    // Top bar: name on the left, links on the right.
    if (logoIcon) DrawIconEx(dc, Sc(28), Sc(24), logoIcon, Sc(40), Sc(40), 0, nullptr, DI_NORMAL);
    Txt(dc, L"OriginX Client", MR(80, 24, 300, 40), boldFont, RGB(255, 255, 255), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    Txt(dc, L"Versions", HomeVersionsLink(), font, ColText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    Txt(dc, L"Logs", HomeLogsLink(), font, ColText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Greeting, or what is happening right now.
    const std::wstring id = Wide(selected.target.id);
    const bool found = !Text(Game).empty();
    std::wstring title = Greeting(), sub = L"Ready to play?";
    if (state == State::Checking) { title = L"Checking for updates"; sub = L"Fetching the list of supported versions..."; }
    else if (state == State::Fetching) { title = L"Getting the DLL"; sub = L"Downloading and verifying files for " + id + L"..."; }
    else if (state == State::Preparing || state == State::Installing)
    {
        title = L"Preparing Minecraft " + id;
        sub = SecondLine(progressText);
        if (sub.empty()) sub = L"Please keep this window open.";
    }
    else if (state == State::Waiting || state == State::Attaching) { title = L"Starting Minecraft"; sub = L"Waiting for the game to connect..."; }
    else if (!selected.available) sub = L"Minecraft " + id + L" is waiting for OriginX support.";
    else if (!found) sub = L"Minecraft " + id + L" was not found. Start the game once, or set it in Settings.";
    Txt(dc, title, MR(0, 196, 1164, 56), greetFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    Txt(dc, sub, MR(160, 254, 844, 30), font, RGB(196, 208, 228), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Under the buttons: the latest message (errors in red) and the progress bar.
    const std::wstring status = Text(StatusText);
    if (!status.empty())
    {
        size_t cut = status.find(L"\r\n");   // at most two lines
        if (cut != std::wstring::npos) cut = status.find(L"\r\n", cut + 2);
        Txt(dc, status.substr(0, cut), MR(232, 520, 700, 44), smallFont, statusError ? ColError : RGB(168, 182, 206),
            DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (progressPercent >= 0)
    {
        const RECT track = MR(402, 572, 300, 10);
        Box(dc, track, 5, ColSide, ColBorder);
        RECT fill = track;
        fill.right = fill.left + std::max<LONG>(Sc(10), (track.right - track.left) * progressPercent / 100);
        if (progressPercent > 0) Box(dc, fill, 5, ColAccent, ColAccent, false);
        Txt(dc, std::to_wstring(progressPercent) + L"%", MR(712, 564, 60, 26), smallFont, RGB(255, 255, 255));
    }

    // Under Launch: the chosen version (opens Versions). Bottom left: the Discord account and the release channel.
    // Bottom right: launcher version and readiness.
    DrawVersionUnderLaunch(dc, id);
    DrawNormalLine(dc);
    DrawAccountArea(dc);
    const std::wstring versionText = LauncherBuild ? L"v1.0.0 · build " + std::to_wstring(LauncherBuild) : std::wstring(L"v1.0.0");
    const wchar_t* launcherVersion = versionText.c_str();
    SIZE versionSize{};
    HGDIOBJ previous = SelectObject(dc, smallFont);
    GetTextExtentPoint32W(dc, launcherVersion, static_cast<int>(wcslen(launcherVersion)), &versionSize);
    SelectObject(dc, previous);
    const RECT versionLine = MR(820, 674, 316, 24);
    Txt(dc, launcherVersion, versionLine, smallFont, ColAccentHi, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    Txt(dc, L"Launcher", RECT{versionLine.left, versionLine.top, versionLine.right - versionSize.cx - Sc(6), versionLine.bottom}, smallFont,
        ColText, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    std::wstring ready = L"Ready to launch";
    if (state != State::Idle) ready = L"Working...";
    else if (!selected.available) ready = L"Waiting for support";
    else if (!found) ready = L"Minecraft not found";
    Txt(dc, ready, MR(820, 698, 316, 24), smallFont, ColMuted, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

// ----------------------------- Settings: a second screen below Home -----------------------------
void DrawVersions(HDC dc)
{
    Txt(dc, Lang::T(L"Minecraft のバージョン", L"Minecraft version"), MR(328, 48, 600, 30), boldFont, ColText);
    Txt(dc, channelVersionsNote.empty() ? Lang::T(L"1.26 系列の26.45以降を表示。対応済みの版を選ぶと、通常起動の Minecraft も自動で切り替わります。", L"Shows 26.45 and later in the 1.26 series. Choosing a supported version also switches the normal-launch Minecraft.") : channelVersionsNote,
        MR(328, 86, 788, 36), smallFont, ColMuted, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    if (versionFamilies.empty()) Txt(dc, Lang::T(L"バージョン一覧を取得しています…", L"Fetching the version list…"), MR(348, 174, 700, 40), font, ColMuted);
    for (int slot = 0; slot < VersionCardsShown; ++slot)
    {
        const size_t index = static_cast<size_t>(slot);
        if (index >= versionFamilies.size()) break;
        const auto& family = versionFamilies[index];
        if (family.rows.empty()) continue;
        const auto& row = family.rows[VersionCardChoice(family)];
        const int y = 136 + slot * 154;
        Box(dc, VersionCardRect(slot), 14, ColSide, row.id == selected.target.id ? ColAccentHi : ColLine);
        Txt(dc, Wide(family.label), MR(360, y + 18, 306, 76), versionLabelFont ? versionLabelFont : greetFont, ColText,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Txt(dc, L"Minecraft Bedrock", MR(364, y + 100, 300, 22), smallFont, ColMuted);
        Txt(dc, Lang::T(L"バージョンを選ぶ", L"Choose a version"), MR(700, y + 12, 370, 20), smallFont, ColText);
        if (scrolling)
        {
            Box(dc, MR(700, y + 38, 392, 34), 6, ColPanel, ColLine);
            Txt(dc, DisplayGameVersion(row.id) + L"  ▾", MR(712, y + 44, 370, 26), font, ColText);
        }
        std::wstring status = row.supported ? Lang::T(L"OriginX 対応", L"Supported by OriginX") : Lang::T(L"OriginX 未対応・選択不可", L"Not supported by OriginX, cannot choose");
        if (row.supported)
            status += row.installed ? Lang::T(L" / インストール済み", L" / installed") : row.downloadable ? Lang::T(L" / 選ぶとダウンロード", L" / downloads when chosen") : Lang::T(L" / 配信リンクなし", L" / no download link");
        Txt(dc, status, MR(700, y + 80, 400, 22), smallFont, row.supported ? ColGreen : ColMuted,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        std::wstring current = row.id == normalVersionId ? Lang::T(L"通常起動に設定済み", L"Set as normal launch") : L"";
        if (row.id == switchingId)
            current = switchPhase.load() == 0 ? Lang::T(L"通常起動の版を確認中…", L"Checking the normal-launch version…") : switchPhase.load() == 1 ?
                Lang::T(L"ダウンロード中 ", L"Downloading ") + std::to_wstring(std::max(progressPercent, 0)) + L"%" : Lang::T(L"Windows にインストール中…", L"Installing in Windows…");
        Txt(dc, current, MR(700, y + 108, 400, 22), smallFont, ColAccentHi);
    }
    Txt(dc, Lang::T(L"暗い項目はOriginX未対応のため、インストール・切替できません。", L"Dimmed versions are not supported by OriginX and cannot be installed or switched to."),
        MR(328, 302, 788, 30), smallFont, ColMuted);
    Txt(dc, Lang::T(L"公開一覧: ", L"Published list: ") + versionDbNote, MR(328, 340, 788, 24), smallFont, ColMuted);
    Txt(dc, Lang::T(L"別の版に切り替えるときは Minecraft を終了してください。通常起動が別の版なら本体を取得します（PC内に別コピーがあっても対象）。同じ版なら再取得しません。", L"Close Minecraft when switching to another version. If the normal launch is another version, the game is downloaded (even if another copy is on this PC). The same version is not downloaded again."),
        MR(328, 390, 788, 90), smallFont, ColMuted, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

void Divider(HDC dc, int y)
{
    RECT line = MR(328, y, 788, 1);
    line.bottom = line.top + 1;
    HBRUSH brush = CreateSolidBrush(ColLine);
    FillRect(dc, &line, brush);
    DeleteObject(brush);
}

RECT SyncSwitchRect() { return MR(1040, 530, 76, 36); }

void DrawSyncSwitch(HDC dc)
{
    const RECT track = SyncSwitchRect();
    const int height = track.bottom - track.top, inset = Sc(4);
    Box(dc, track, height / 2, syncNormal ? ColAccent : ColSide, syncNormal ? ColAccent : ColBorder);
    const int knob = height - 2 * inset;
    const int left = syncNormal ? track.right - inset - knob : track.left + inset;
    Box(dc, RECT{left, track.top + inset, left + knob, track.top + inset + knob}, knob / 2, RGB(255, 255, 255), RGB(255, 255, 255), false);
    Txt(dc, syncNormal ? L"ON" : L"OFF", RECT{track.left - Sc(60), track.top, track.left - Sc(10), track.bottom}, smallFont,
        syncNormal ? ColAccentHi : ColMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// Settings > General, top right: 日本語 | English (user, 2026-11-17). The HUD reads the same choice (LangWin.h).
RECT LanguageRect(int half) { return MR(936 + half * 90, 48, 90, 32); }

void DrawLanguageSwitch(HDC dc)
{
    for (int half = 0; half < 2; half++)
    {
        const bool on = (half == 1) == Lang::English();
        const RECT r = LanguageRect(half);
        Box(dc, r, Sc(8), on ? ColAccent : ColSide, on ? ColAccent : ColBorder);
        Txt(dc, half ? L"English" : L"日本語", r, smallFont, on ? RGB(255, 255, 255) : ColMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// The texts set when the window was made; the drawn ones follow on the next paint.
void Relabel()
{
    const std::pair<int, const wchar_t*> labels[] = {
        {AutoDetect, Lang::T(L"自動検出", L"Auto-detect")}, {PickGame, Lang::T(L"選択…", L"Select…")},
        {ImportGame, Lang::T(L"フォルダーを追加…", L"Add folder…")}, {CheckUpdates, Lang::T(L"更新を確認", L"Check for updates")},
        {Guide, Lang::T(L"使い方・必要な部品", L"Guide & required components")}};
    for (const auto& [id, text] : labels) SetWindowTextW(GetDlgItem(window, id), text);
    SetWindowTextW(window, Lang::T(L"OriginX Client — 適用・起動", L"OriginX Client — Apply & Launch"));
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void DrawGeneral(HDC dc)
{
    const UINT wrap = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX;
    Txt(dc, L"Game location", MR(328, 52, 500, 28), boldFont, ColText);
    DrawLanguageSwitch(dc);
    Txt(dc, Lang::T(L"選択した版（", L"The Minecraft game for the chosen version (") + Wide(selected.target.id) + Lang::T(L"）のMinecraft本体。自動検出、または「選択…」で指定します。", L"). Auto-detect, or set it with \"Select…\"."),
        MR(328, 82, 790, 24), smallFont, ColMuted);
    Divider(dc, 178);
    Txt(dc, L"Version folders", MR(328, 198, 500, 28), boldFont, ColText);
    Txt(dc, Lang::T(L"LeviLauncherなどで入れた版のフォルダーを追加すると、Version の一覧に出ます。", L"Add a version folder installed with LeviLauncher or similar to show it in the Version list."), MR(328, 228, 540, 44), smallFont, ColMuted, wrap);
    Divider(dc, 286);
    Txt(dc, L"Updates", MR(328, 306, 500, 28), boldFont, ColText);
    Txt(dc, Lang::T(L"公開された対応版とDLLの更新を確認します（起動時にも自動で確認）。", L"Checks published supported versions and DLL updates (also checked at startup)."), MR(328, 336, 540, 44), smallFont, ColMuted, wrap);
    Divider(dc, 394);
    Txt(dc, L"Help", MR(328, 414, 500, 28), boldFont, ColText);
    Txt(dc, Lang::T(L"使い方と、ゲームに必要なWindowsの部品。", L"How to use it, and the Windows components the game needs."), MR(328, 444, 540, 44), smallFont, ColMuted, wrap);
    Divider(dc, 502);
    Txt(dc, L"Normal launch", MR(328, 522, 500, 28), boldFont, ColText);
    Txt(dc, syncNormal ? Lang::T(L"ON：Launch と版の選択で、Windows の通常起動（スタートメニューなど）の版も選んだ版に変えます。すでにその版ならダウンロードしません。", L"ON: Launch and choosing a version also change Windows' normal-launch version (Start menu etc.). Nothing is downloaded if it is already that version.")
                       : Lang::T(L"OFF：Windows の通常起動の版は変えません。この PC に無い版だけ、確認のあとダウンロードして入れます（このとき通常起動の版も変わります）。", L"OFF: Windows' normal-launch version is not changed. Only versions missing on this PC are downloaded after asking (this also changes the normal-launch version)."),
        MR(328, 552, 640, 44), smallFont, ColMuted, wrap);
    DrawSyncSwitch(dc);
    Divider(dc, 610);
    Txt(dc, Lang::T(L"DLLは版ごとに保存し、公開された更新を起動時に確認します。通常起動の版は Home の Launch の下からも変えられます。ワールドの版ごとの分離は未対応です。"
            L"対応待ちの版は起動できません。配布先が未公開・オフラインの場合は保存済み一覧を使います。", L"DLLs are kept per version, and published updates are checked at startup. The normal-launch version can also be changed under Launch on Home. Separating worlds per version is not supported. "
            L"Versions waiting for support cannot be launched. If the source is unpublished or offline, the saved list is used."),
        MR(328, 630, 788, 64), smallFont, ColMuted, wrap);
}

void DrawLogs(HDC dc)
{
    Txt(dc, L"Activity", MR(328, 48, 300, 30), boldFont, ColText);
    Txt(dc, L"Open log file", LogFileLink(), smallFont, ColAccentHi, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const RECT inner = MR(324, 96, 792, 606);
    Box(dc, inner, 12, ColSide, ColLine);
    const int timeWidth = Sc(52), pad = Sc(14);
    std::vector<int> heights(history.size());
    int total = 0;
    for (size_t n = 0; n < history.size(); n++)
    {
        RECT measure{inner.left + pad + timeWidth, 0, inner.right - pad, 0};
        HGDIOBJ old = SelectObject(dc, smallFont);
        DrawTextW(dc, history[n].text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        heights[n] = measure.bottom - measure.top + Sc(8);
        total += heights[n];
    }
    const int room = inner.bottom - inner.top - 2 * pad;
    size_t first = 0;
    while (first < history.size() && total > room) total -= heights[first++];   // oldest lines scroll away
    int top = inner.top + pad;
    for (size_t n = first; n < history.size(); n++)
    {
        const LogLine& line = history[n];
        const COLORREF colour = line.error ? ColError : line.live ? ColAccentHi : ColText;
        Txt(dc, line.time, RECT{inner.left + pad, top, inner.left + pad + timeWidth, top + heights[n]}, smallFont, ColMuted);
        Txt(dc, line.text, RECT{inner.left + pad + timeWidth, top, inner.right - pad, top + heights[n]}, smallFont, colour,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        top += heights[n];
    }
}

// A small rounded square with a symbol, in front of each tab and Return.
void TabIcon(HDC dc, RECT row, const wchar_t* glyph, COLORREF fill)
{
    const int size = Sc(36);
    const RECT icon{row.left + Sc(10), (row.top + row.bottom - size) / 2, row.left + Sc(10) + size, (row.top + row.bottom + size) / 2};
    Box(dc, icon, 10, fill, fill, false);
    Txt(dc, glyph, icon, symFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawSettingsScene(HDC dc, int width, int height)
{
    RECT all{0, 0, width, height};
    FillRect(dc, &all, background);
    // Left panel: title, tabs, Return.
    Box(dc, MR(24, 24, 260, 702), 22, ColPanel, ColLine);
    if (logoIcon) DrawIconEx(dc, Sc(48), Sc(46), logoIcon, Sc(40), Sc(40), 0, nullptr, DI_NORMAL);
    Txt(dc, L"Settings", MR(100, 44, 170, 44), heroFont, RGB(255, 255, 255), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const wchar_t* labels[3] = {L"General", L"Version", L"Logs"};
    const wchar_t* glyphs[3] = {L"⚙", L"▦", L"▤"};
    for (int i = 0; i < 3; i++)
    {
        const RECT row = TabRect(i);
        const bool on = i == settingsTab;
        if (on) Box(dc, row, 16, ColAccent, ColAccent);
        TabIcon(dc, row, glyphs[i], on ? ColAccentDown : ColSecondary);
        Txt(dc, labels[i], RECT{row.left + Sc(60), row.top, row.right, row.bottom}, on ? boldFont : font,
            on ? RGB(255, 255, 255) : ColText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    const RECT back = ReturnRect();
    Box(dc, back, 16, ColSecondary, ColBorder);
    TabIcon(dc, back, L"←", ColAccent);
    Txt(dc, L"Return", RECT{back.left + Sc(60), back.top, back.right, back.bottom}, font, ColText,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    // Right panel: the chosen tab.
    Box(dc, MR(300, 24, 840, 702), 22, ColPanel, ColLine);
    if (settingsTab == 1) DrawVersions(dc);
    else if (settingsTab == 2) DrawLogs(dc);
    else DrawGeneral(dc);
}

// While the screens slide the real controls are hidden; draw pictures of them in their places.
void DrawControlFaces(HDC dc, const std::vector<int>& ids)
{
    for (int id : ids)
    {
        HWND control = GetDlgItem(window, id);
        if (!control) continue;
        RECT r;
        GetWindowRect(control, &r);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&r), 2);
        wchar_t label[512]{};
        GetWindowTextW(control, label, 511);
        if (id == Game)
        {
            Box(dc, r, 4, ColPanel, ColBorder);
            RECT text{r.left + Px(6), r.top, r.right - Px(6), r.bottom};
            Txt(dc, label, text, font, ColText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            continue;
        }
        DrawButtonFace(dc, r, label, static_cast<UINT>(id), !IsWindowEnabled(control), false, false,
                       ButtonUnder(static_cast<UINT>(id), (r.top + r.bottom) / 2));
    }
}

// Home is on top, Settings right below it. `scroll` is 0 on Home, 1 on Settings, in between while sliding.
void DrawUi(HDC dc, int width, int height)
{
    const int offset = static_cast<int>(std::lround(scroll * height));
    if (offset < height)
    {
        SetViewportOrgEx(dc, 0, -offset, nullptr);
        DrawHome(dc, width, height);
        if (scrolling) DrawControlFaces(dc, {Apply, HomeSettings});
    }
    if (offset > 0)
    {
        SetViewportOrgEx(dc, 0, height - offset, nullptr);
        DrawSettingsScene(dc, width, height);
        if (scrolling && settingsTab == 0) DrawControlFaces(dc, {Game, AutoDetect, PickGame, ImportGame, CheckUpdates, Guide});
    }
    SetViewportOrgEx(dc, 0, 0, nullptr);
}

bool InRect(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

LRESULT CALLBACK WindowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    try
    {
        switch (message)
        {
        case WM_CREATE: window = handle; BuildUi(); return 0;
        case CheckMessage: if (state == State::Idle) CheckForUpdates(); return 0;
        case LauncherUpdateMessage:
        {
            std::unique_ptr<LauncherUpdateResult> result(reinterpret_cast<LauncherUpdateResult*>(lParam));
            if (selfUpdateThread.joinable()) selfUpdateThread.join();
            if (result->stopFetched)
            {
                remoteStop = result->stop;
                NoteRemoteStop("site");
                const auto text = RemoteStopText();
                if (!text.empty()) Status(text);
            }
            else if (result->stopAsked)
            {
                std::error_code ignored;
                RemoteStopLog(fs::is_regular_file(SavedLauncherList(), ignored) ? "could not get the stop information; the last checked one stays"
                                                                             : "could not get the stop information and none is saved; nothing stopped");
            }
            if (!result->error.empty()) Status(Lang::T(L"ランチャーの更新は確認できませんでした（このまま使えます）: ", L"Could not check for launcher updates (you can keep using it): ") + Wide(result->error));
            else if (!result->file.empty())
            {
                readyLauncher = result->file;
                readyLauncherBuild = result->build;
                ApplyLauncherUpdate();   // now if idle, else when the current step ends (Busy)
            }
            else CheckTestLauncherUpdate();   // nothing newer on stable: a test account may have a test build
            return 0;
        }
        case ApplyLauncherMessage: ApplyLauncherUpdate(); return 0;
        case UpdatedMessage:
        {
            std::unique_ptr<UpdateResult> result(reinterpret_cast<UpdateResult*>(lParam));
            if (preparation.joinable()) preparation.join();
            Busy(State::Idle);
            if (result->error.empty()) { catalog = std::move(result->catalog); channelOnlyIds.clear(); replacedStable.clear(); MergeChannelVersions(); }
            RefreshVersions(selected.target.id);
            Status(result->error.empty() ? Lang::T(L"更新一覧を確認しました。版を選んで起動してください。", L"Checked the update list. Choose a version and launch.") :
                Lang::T(L"保存済み一覧を使用します。", L"Using the saved list.") + Wide(result->error));
            if (launchAfterCheck)
            {
                launchAfterCheck = false;
                if (!selected.available) throw std::runtime_error(selected.reason);
                launchNote.clear();
                // (user, 2026-11-09) Switch on: Windows' normal-launch version follows the chosen one (no download when
                // Windows already has it). Off: OriginX's copy / the found install starts, Windows is not touched; a version
                // not on this PC at all can only come from Windows, so that is asked first.
                const std::string id = selected.target.id;
                if (syncNormal) StartSwitch(id, true);
                else if (installedGames.count(id) || !Text(Game).empty()) FetchAndApply();
                else if (ConfirmDialog(window, Lang::T(L"Minecraft ", L"Minecraft ") + Wide(id) + Lang::T(L" をダウンロードしますか？", L" — download?"),
                                       L"Minecraft " + Wide(id) + Lang::T(L" はまだこの PC にありません。\r\n\r\nMicrosoft からダウンロード（数GB）して Windows に入れてから起動します。"
                                              L"Windows の通常起動の版も ", L" is not on this PC yet.\r\n\r\nIt will be downloaded from Microsoft (several GB) and installed in Windows before launching. "
                                              L"Windows' normal-launch version also changes to ") + Wide(id) + Lang::T(L" に変わります。", L"."),
                                       Lang::T(L"ダウンロード", L"Download"), Lang::T(L"やめる", L"Cancel")))
                    StartSwitch(id, true);
                else Status(Lang::T(L"起動を取り消しました（Minecraft ", L"Launch cancelled (Minecraft ") + Wide(id) + Lang::T(L" はこの PC にありません）。", L" is not on this PC)."));
            }
            return 0;
        }
        case VersionDbMessage:
        {
            std::unique_ptr<VersionDbResult> result(reinterpret_cast<VersionDbResult*>(lParam));
            if (versionDbThread.joinable()) versionDbThread.join();
            if (result->error.empty())
            {
                publishedVersions = std::move(result->versions);
                versionLinks = std::move(result->links);
                versionDbNote = Lang::T(L"最新", L"up to date");
            }
            else if (publishedVersions.empty()) versionDbNote = Lang::T(L"取得できません: ", L"Cannot get: ") + Wide(result->error);
            SyncVersionCards();
            return 0;
        }
        case SwitchedMessage:
        {
            std::unique_ptr<SwitchResult> result(reinterpret_cast<SwitchResult*>(lParam));
            if (!result) throw std::runtime_error(Lang::T("Minecraftの版変更の結果を受け取れませんでした。", "Did not receive the result of changing the Minecraft version."));
            if (switchThread.joinable()) switchThread.join();
            Busy(State::Idle);
            EndProgress();
            switchingId.clear();
            ScanInstalled();
            QueryNormalVersion();   // what Windows starts normally now, whatever the result
            if (!result->error.empty() && result->launchAfter)
            {
                // Launch does not need Windows' registered version: OriginX starts its own copy of the chosen version.
                // The registered (Start menu) version stays as it was; say why, then go on.
                if (!installedGames.count(result->id) && Text(Game).empty())
                    throw std::runtime_error("Minecraft " + result->id + Lang::T(" を Windows に入れられなかったため、起動できません（この PC にこの版がありません）。\n理由: ", " could not be installed in Windows, so it cannot launch (this version is not on this PC).\nReason: ") + result->error);
                launchNote = Lang::T(L"Windowsの通常起動の版は変更できませんでした（Start menuなどのMinecraftは今のまま）。OriginXは選んだ版 ", L"Could not change Windows' normal-launch version (Minecraft in the Start menu etc. stays as it is). OriginX launches the chosen version ") +
                             Wide(result->id) + Lang::T(L" で起動します。\r\n理由: ", L".\r\nReason: ") + Wide(result->error);
                PushLog(launchNote, true, false);
                FetchAndApply();
                return 0;
            }
            bool listed = false;
            for (const auto& release : catalog.releases) if (release.target.id == result->id) listed = true;
            if (!result->error.empty())
            {
                // A Versions card or the switch under Launch: OriginX takes the chosen version anyway (user, 2026-11-09);
                // only Windows' registered (Start menu) version stays as it was. Say why.
                if (listed) { RefreshVersions(result->id); Save(); }
                else SyncVersionCards();
                Status(Lang::T(L"OriginX の版は ", L"OriginX version set to ") + Wide(result->id) + Lang::T(L" にしました。Windowsの通常起動の版は変更できませんでした（Start menuなどのMinecraftは今のまま）。\r\n理由: ", L". Could not change Windows' normal-launch version (Minecraft in the Start menu etc. stays as it is).\r\nReason: ") +
                       Wide(result->error));
                if (listed) GoTo(0);
                return 0;
            }
            normalVersionId = result->id;
            normalRead = NormalRead::Known;
            if (listed) { RefreshVersions(result->id); Save(); }
            else SyncVersionCards();
            Status(Lang::T(L"通常起動の Minecraft を ", L"Set the normal-launch Minecraft to ") + Wide(result->id) + Lang::T(L" に設定し、Windowsの登録を確認しました。", L" and confirmed the Windows registration.") +
                   (result->warning.empty() ? L"" : L"\r\n" + Wide(result->warning)) +
                   (installedGames.count(result->id) ? L"" : Lang::T(L"\r\n（この PC の一覧にまだ出ない場合は、Settings の自動検出を押してください。）", L"\r\n(If it is not in this PC's list yet, press Auto-detect in Settings.)")));
            if (result->launchAfter) FetchAndApply();
            else if (listed) GoTo(0);
            return 0;
        }
        case NormalVersionMessage:
        {
            std::unique_ptr<NormalQueryResult> result(reinterpret_cast<NormalQueryResult*>(lParam));
            if (normalQueryThread.joinable()) normalQueryThread.join();
            if (result && !result->id.empty()) { normalVersionId = result->id; normalRead = NormalRead::Known; normalReadNote.clear(); }
            else
            {
                normalVersionId.clear();
                normalRead = NormalRead::Missing;
                normalReadNote = result ? Wide(result->error) : std::wstring(Lang::T(L"読み取れません", L"unreadable"));
                if (result) PushLog(Lang::T(L"Windowsの通常起動の版: ", L"Windows' normal-launch version: ") + normalReadNote, false, false);
            }
            SyncVersionCards();
            Refresh();
            if (normalQueryAgain) QueryNormalVersion();
            return 0;
        }
        case LoginMessage:
        {
            std::unique_ptr<LoginResult> result(reinterpret_cast<LoginResult*>(lParam));
            if (accountThread.joinable()) accountThread.join();
            accountBusy = false;
            if (!accountEpoch.Current(result->epoch))
            {
                // the user logged out while this ran: drop it, and the icon file it may have written meanwhile
                if (storedAccount.token.empty()) DeleteAccount(privateDirectory);
                Refresh();
                return 0;
            }
            if (result->status)
            {
                NoteAccountStatus(*result->status);
                MergeChannelVersions();
                RefreshVersions(selected.target.id);
                ReloadAvatar();
                CheckTestLauncherUpdate();
                if (result->interactive) Status(Lang::T(L"Discord にログインしました: ", L"Logged in to Discord: ") + Wide(result->status->name));
            }
            else if (result->interactive) { Refresh(); throw std::runtime_error(result->error); }
            else
            {
                // the saved key could not be checked: the account stays as drawn, except when the key has expired
                accountStatus.reset();
                if (result->error.find(Lang::T("期限が切れました", "expired")) != std::string::npos) accountExpired = true;
            }
            Refresh();
            return 0;
        }
        case PackageMessage:
        {
            std::unique_ptr<PackageResult> result(reinterpret_cast<PackageResult*>(lParam));
            if (preparation.joinable()) preparation.join();
            Busy(State::Idle);
            if (result->status && accountEpoch.Current(result->epoch)) NoteAccountStatus(*result->status);   // not after a logout
            if (!result->note.empty())
            {
                // the chosen channel could not be used: the reason is shown with the launch result and kept in the log
                const std::wstring note = Wide(result->note);
                launchNote += (launchNote.empty() ? std::wstring{} : std::wstring(L"\r\n")) + note;
                PushLog(note, true, false);
            }
            if (signatureStopped)
            {
                const std::wstring note = Wide(SignatureStoppedNote());
                launchNote += (launchNote.empty() ? std::wstring{} : std::wstring(L"\r\n")) + note;
            }
            if (!result->error.empty()) throw std::runtime_error(result->error);
            packageRoot = result->root;
            packageLocal = result->local;
            launchTarget = result->target ? *result->target : selected.target;
            ApplyClient();
            return 0;
        }
        case PreparedMessage:
        {
            std::unique_ptr<PreparationResult> result(reinterpret_cast<PreparationResult*>(lParam));
            if (preparation.joinable()) preparation.join();
            Busy(State::Idle);
            EndProgress();
            if (!result->error.empty()) throw std::runtime_error(result->error);
            pending = result->instance;
            SetWindowTextW(GetDlgItem(window, Game), (pending.directory / "Minecraft.Windows.exe").c_str());
            Save();
            PrepareRuntimeOrLaunch();
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) >= VersionCardControl && LOWORD(wParam) < VersionCardControl + VersionCardsShown)
            {
                // Commit only when the dropdown closes with a selection; arrow browsing/Escape never installs.
                if (HIWORD(wParam) == CBN_SELENDOK && state == State::Idle)
                {
                    const int slot = LOWORD(wParam) - VersionCardControl;
                    const size_t familyIndex = static_cast<size_t>(slot);
                    const HWND control = versionCardControls[slot];
                    const LRESULT choice = control ? SendMessageW(control, CB_GETCURSEL, 0, 0) : CB_ERR;
                    if (familyIndex < versionFamilies.size() && choice != CB_ERR &&
                        static_cast<size_t>(choice) < versionFamilies[familyIndex].rows.size())
                    {
                        const auto row = versionFamilies[familyIndex].rows[static_cast<size_t>(choice)];
                        if (!ClientVersionSelectable(row.id, row.supported))
                        {
                            Status(Lang::T(L"この版はOriginX未対応のため、インストール・切替できません。", L"OriginX does not support this version, so it cannot be installed or switched to."));
                            SendMessageW(control, CB_SETCURSEL, VersionCardChoice(versionFamilies[familyIndex]), 0);
                            Refresh();
                        }
                        else
                        {
                            try { SelectUserVersion(row.id); }
                            catch (...) { SyncVersionCards(); throw; }
                            Refresh();
                        }
                    }
                }
                else if (HIWORD(wParam) == CBN_SELENDCANCEL)
                {
                    const int slot = LOWORD(wParam) - VersionCardControl;
                    const size_t index = static_cast<size_t>(slot);
                    if (versionCardControls[slot] && index < versionFamilies.size())
                        SendMessageW(versionCardControls[slot], CB_SETCURSEL, VersionCardChoice(versionFamilies[index]), 0);
                }
                return 0;
            }
            if (LOWORD(wParam) == Version && HIWORD(wParam) == CBN_SELCHANGE && state == State::Idle)
            {
                const auto index = SendDlgItemMessageW(window, Version, CB_GETCURSEL, 0, 0);
                if (index != CB_ERR && static_cast<size_t>(index) < catalog.releases.size())
                    SelectUserVersion(catalog.releases[static_cast<size_t>(index)].target.id);
                return 0;
            }
            if (HIWORD(wParam) != BN_CLICKED) return 0;
            switch (LOWORD(wParam))
            {
            case PickGame: if (state == State::Idle) Pick(Game); break;
            case AutoDetect: if (state == State::Idle && FindInstalledGame(false)) Save(); break;
            case ImportGame: if (state == State::Idle) ImportFolder(); break;
            case Apply:
                if (state == State::Idle) { launchAfterCheck = true; CheckForUpdates(); }
                else if (state == State::Checking) { launchAfterCheck = true; Status(Lang::T(L"更新を確認してから起動します…", L"Checking for updates before launching…")); }
                break;
            case CheckUpdates: if (state == State::Idle) CheckForUpdates(); break;
            case Guide:
                ShellExecuteW(handle, L"open", fs::is_regular_file(bundle / "README-ja.md") ? (bundle / "README-ja.md").c_str()
                              : L"https://originx.originx-client.workers.dev/", nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case Logs: OpenLogs(); break;
            case HomeSettings: settingsTab = 0; GoTo(1); break;
            }
            return 0;
        case WM_TIMER:
            if (wParam == SlideTimer) AdvanceSlide();
            else Tick();
            return 0;
        case WM_DRAWITEM:
        {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item && item->CtlType == ODT_BUTTON) { DrawButton(*item); return TRUE; }
            if (item && item->CtlType == ODT_COMBOBOX && item->CtlID >= VersionCardControl &&
                item->CtlID < VersionCardControl + VersionCardsShown) { DrawVersionItem(*item); return TRUE; }
            break;
        }
        case WM_MEASUREITEM:
        {
            auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (item && item->CtlType == ODT_COMBOBOX && item->CtlID >= VersionCardControl &&
                item->CtlID < VersionCardControl + VersionCardsShown)
            {
                item->itemHeight = static_cast<UINT>(Sc(32));
                return TRUE;
            }
            break;
        }
        case WM_ERASEBKGND: return 1;   // everything is drawn from the back buffer
        case WM_LBUTTONDOWN:
        {
            const int x = static_cast<short>(LOWORD(lParam)), y = static_cast<short>(HIWORD(lParam));
            if (scrolling) return 0;
            if (page == 0)
            {
                if (channelMenuOpen)
                {
                    // a click inside the menu picks an entry, a click anywhere else only closes it
                    const auto entries = ChannelMenu();
                    const int count = static_cast<int>(entries.size());
                    channelMenuOpen = false;
                    for (int i = 0; i < count; i++)
                        if (InRect(HomeChannelMenuRow(i, count), x, y))
                        {
                            if (entries[i].logout) Logout();
                            else { wantedChannel = entries[i].channel; Save(); MergeChannelVersions(); RefreshVersions(selected.target.id); CheckTestLauncherUpdate(); }
                            break;
                        }
                    Refresh();
                    return 0;
                }
                if (InRect(HomeLogsLink(), x, y)) { settingsTab = 2; GoTo(1); }
                else if (InRect(HomeVersionsLink(), x, y) || InRect(HomeVersionUnderLaunch(), x, y)) { settingsTab = 1; GoTo(1); }
                else if (InRect(HomeNormalLine(), x, y) && NormalSwitchOffered()) StartSwitch(selected.target.id, false);
                else if (!LoggedIn() && InRect(HomeLoginButton(), x, y)) StartAccount(true);
                else if (LoggedIn() && InRect(HomeChannelButton(), x, y)) { channelMenuOpen = true; Refresh(); }
                return 0;
            }
            if (InRect(ReturnRect(), x, y)) { GoTo(0); return 0; }
            for (int i = 0; i < 3; i++)
                if (InRect(TabRect(i), x, y)) { settingsTab = i; ShowPage(); return 0; }
            if (settingsTab == 2 && InRect(LogFileLink(), x, y)) OpenLogs();
            for (int half = 0; settingsTab == 0 && half < 2; half++)
                if (InRect(LanguageRect(half), x, y) && (half == 1) != Lang::English())
                {
                    Lang::Current() = half ? Lang::Id::En : Lang::Id::Ja;
                    Lang::Save(Lang::Current());
                    Relabel();
                    Status(Lang::T(L"表示の言語を日本語にしました。HUD は次に起動したときに切り替わります。",
                                   L"Switched the language to English. The HUD switches the next time it starts."));
                    return 0;
                }
            if (settingsTab == 0 && state == State::Idle && InRect(SyncSwitchRect(), x, y))
            {
                syncNormal = !syncNormal;
                Save();
                Status(syncNormal ? Lang::T(L"通常起動の版も切り替えます（Launch・版の選択で Windows の版も合わせます）。", L"Also switches the normal-launch version (Launch and choosing a version also set Windows' version).")
                                  : Lang::T(L"通常起動の版は切り替えません（OriginX だけ選んだ版で起動します）。", L"Does not switch the normal-launch version (only OriginX launches the chosen version)."));
            }
            return 0;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(handle, &paint);
            RECT client;
            GetClientRect(handle, &client);
            const int width = client.right - client.left, height = client.bottom - client.top;
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
            HGDIOBJ previous = SelectObject(memory, bitmap);
            DrawUi(memory, width, height);
            BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(handle, &paint);
            return 0;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor(reinterpret_cast<HDC>(wParam), ColText);
            SetBkColor(reinterpret_cast<HDC>(wParam), ColPanel);
            return reinterpret_cast<LRESULT>(panelBrush);
        case WM_CTLCOLORSTATIC:
        {
            const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
            HDC dc = reinterpret_cast<HDC>(wParam);
            if (id == StatusText || id == Game)  // read-only edit boxes are reported as "static"
            {
                SetTextColor(dc, id == StatusText && statusError ? ColError : ColText);
                SetBkColor(dc, ColPanel);
                return reinterpret_cast<LRESULT>(panelBrush);
            }
            SetTextColor(dc, id == TitleText ? ColAccentHi : id == Caption ? ColMuted : ColText);
            SetBkColor(dc, ColBg);
            return reinterpret_cast<LRESULT>(background);
        }
        case WM_CLOSE:
            if (state == State::Installing || state == State::Preparing || state == State::Checking || state == State::Fetching ||
                state == State::Switching)
            {
                Status(Lang::T(L"ゲームとローダーの準備が終わるまで、この画面を開いたままにしてください。", L"Keep this window open until the game and loader are ready."));
                return 0;
            }
            DestroyWindow(handle);
            return 0;
        case WM_DESTROY:
            loginCancel = true;
            if (accountThread.joinable()) accountThread.detach();
            if (avatarBitmap) DeleteObject(avatarBitmap);
            KillTimer(handle, 1);
            KillTimer(handle, SlideTimer);
            if (versionDbThread.joinable()) versionDbThread.detach();
            if (switchThread.joinable()) switchThread.detach();
            if (normalQueryThread.joinable()) normalQueryThread.detach();
            if (selfUpdateThread.joinable()) selfUpdateThread.detach();
            if (hudProcess) CloseHandle(hudProcess);
            if (gameProcess) CloseHandle(gameProcess);
            PostQuitMessage(0);
            return 0;
        }
    }
    catch (const std::exception& error)
    {
        if (state != State::Installing && state != State::Switching) Busy(State::Idle);
        Status(Lang::T(L"処理できませんでした:\r\n", L"Could not process it:\r\n") + Wide(error.what()) + Lang::T(L"\r\n初回の準備と選択したファイルを確認してください。", L"\r\nCheck the first-time setup and the chosen files."));
    }
    return DefWindowProcW(handle, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try
    {
        std::vector<wchar_t> path(32768);
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) throw std::runtime_error(Lang::T("配布フォルダーを取得できません。", "Cannot get the distribution folder."));
        launcherExe = fs::path(std::wstring(path.data(), length));
        bundle = launcherExe.parent_path();
        {   // --after-update <pid>: started by the launcher that installed this one (design 2026-11-20)
            int count = 0;
            if (LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count))
            {
                for (int i = 1; i + 1 < count; ++i)
                    if (std::wstring(args[i]) == L"--after-update") afterUpdateFrom = static_cast<DWORD>(std::wcstoul(args[i + 1], nullptr, 10));
                LocalFree(args);
            }
        }
        privateDirectory = KnownFolder(FOLDERID_LocalAppData) / "MyClient";
        fs::create_directories(privateDirectory);
        TrimLauncherLog();
        Lang::Load(launcherExe);   // the saved choice, else the site page it was downloaded from, else Windows
        versionDbNote = Lang::T(L"未取得", L"not fetched");
        HANDLE single = singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\MyClient.Launcher");
        const DWORD mutexError = GetLastError();
        if (!single) throw std::runtime_error(Lang::T("ランチャーの起動状態を確認できません。", "Cannot check the launcher's running state."));
        // After a self-update the old launcher still holds the lock until our mark: wait for it in TakeOverFromUpdater.
        if (mutexError == ERROR_ALREADY_EXISTS && !afterUpdateFrom)
        {
            HWND previous = FindWindowW(L"MyClientLauncherWindow", nullptr);
            if (previous) { ShowWindow(previous, SW_RESTORE); SetForegroundWindow(previous); }
            CloseHandle(single);
            singleInstance = nullptr;
            CoUninitialize();
            return 0;
        }
        if (mutexError != ERROR_ALREADY_EXISTS) RemoveOldLauncher(launcherExe);   // we hold the lock: no hand-over runs
        HDC screen = GetDC(nullptr);
        dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
        font = CreateFontW(-Px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        heading = CreateFontW(-Px(27), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        smallFont = CreateFontW(-Px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        boldFont = CreateFontW(-Px(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        heroFont = CreateFontW(-Px(23), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        titleFont = CreateFontW(-Px(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        greetFont = CreateFontW(-Px(34), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        versionLabelFont = CreateFontW(-Px(48), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
        symFont = CreateFontW(-Px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
        logoIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(2), IMAGE_ICON, Sc(44), Sc(44), LR_DEFAULTCOLOR));
        background = CreateSolidBrush(ColBg);
        panelBrush = CreateSolidBrush(ColPanel);
        WNDCLASSW type{};
        type.hInstance = instance;
        type.lpfnWndProc = WindowProc;
        type.lpszClassName = L"MyClientLauncherWindow";
        type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        type.hbrBackground = background;
        RegisterClassW(&type);
        RECT rect{0, 0, Sc(1164), Sc(750)};
        constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        AdjustWindowRect(&rect, style, FALSE);
        HWND handle = CreateWindowW(type.lpszClassName, Lang::T(L"OriginX Client — 適用・起動", L"OriginX Client — Apply & Launch"), style,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, instance, nullptr);
        if (!handle) throw std::runtime_error(Lang::T("ランチャー画面を作成できません。", "Cannot create the launcher window."));
        ShowWindow(handle, show);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(handle, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        DeleteObject(font);
        DeleteObject(heading);
        DeleteObject(background);
        DeleteObject(panelBrush);
        DeleteObject(smallFont);
        DeleteObject(boldFont);
        DeleteObject(heroFont);
        DeleteObject(greetFont);
        if (versionLabelFont) DeleteObject(versionLabelFont);
        DeleteObject(symFont);
        DeleteObject(titleFont);
        if (logoIcon) DestroyIcon(logoIcon);
        if (singleInstance) CloseHandle(singleInstance);
    }
    catch (const std::exception& error)
    {
        MessageBoxW(nullptr, Wide(error.what()).c_str(), L"OriginX Client Launcher", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    CoUninitialize();
    return 0;
}

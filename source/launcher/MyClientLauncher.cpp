#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include "AccountWin.h"   // before <windows.h>: it brings winsock2.h
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include "Install.h"
#include "RuntimeWin.h"
#include "DownloadWin.h"
#include "GameVersions.h"
#include "SwitchErrors.h"
#include "StandaloneWin.h"
#include "SelfUpdate.h"
#include "../BridgeChannel.h"
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
fs::path packageRoot;
bool packageLocal = false;   // packageRoot holds the files shipped beside the launcher
Json sources = Json::object();
struct UpdateResult { Catalog catalog; std::string error; };
struct PackageResult { fs::path root; bool local = false; std::string error; std::string note; Channel channel = Channel::Stable; std::optional<AccountStatus> status; unsigned epoch = 0; };
// Version switching: folders the user added, what was found on this PC and the public list of released versions.
Json imports = Json::array();
std::string xboxSeen;   // the Xbox app install's version when the launcher last looked (launcher.json)
std::map<std::string, fs::path> installedGames;   // version id -> Minecraft.Windows.exe
std::vector<std::string> publishedVersions;
std::map<std::string, std::vector<std::string>> versionLinks;   // version id -> Microsoft's package links
std::wstring versionDbNote = L"未取得";
std::thread versionDbThread;
struct VersionDbResult { std::vector<std::string> versions; std::map<std::string, std::vector<std::string>> links; std::string error; };
// Changing the installed game (Flarial's way): Microsoft's package is downloaded and Windows installs it
// over the Store copy, so Gaming Services decrypts it with the user's own licence.
std::thread switchThread;
// Self-update (SelfUpdate.h): the newer launcher, downloaded and checked, waits here until nothing is running.
HANDLE singleInstance = nullptr;
fs::path launcherExe, readyLauncher;
uint64_t readyLauncherBuild = 0;
std::thread selfUpdateThread;
struct LauncherUpdateResult { fs::path file; uint64_t build = 0; std::string error; };
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
    if (lax <= 0) return L"（文字を読み取れませんでした）";
    std::wstring result(lax, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), lax);
    return result;
}

fs::path KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &text)))
        throw std::runtime_error("Windowsのユーザーフォルダーを取得できません。");
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
    statusError = text.find(L"できません") != std::wstring::npos || text.find(L"失敗") != std::wstring::npos ||
                  text.find(L"不正") != std::wstring::npos || text.find(L"拒否") != std::wstring::npos;
    SetWindowTextW(GetDlgItem(window, StatusText), text.c_str());
    PushLog(text, statusError, false);
    Refresh();
    std::ofstream log(privateDirectory / "launcher.log", std::ios::app | std::ios::binary);
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    log << utf8 << '\n';
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
        message = L"ダウンロード中 (" + index + L")\r\n" + label + (detail.empty() ? L"" : L"   " + detail + L" MB");
    else if (phase == "cached") message = L"保存済みのファイルを確認しました (" + index + L")\r\n" + label;
    else if (phase == "extract") message = L"展開中\r\n" + label;
    else if (phase == "activate") message = L"ゲーム本体にローダーを組み込んでいます。1分ほどかかることがあります。";
    else if (phase == "done") message = L"準備が完了しました。Minecraftを起動します…";
    else message = L"ダウンロードの準備をしています…";
    const std::wstring text = L"ローダーの準備中  " + std::to_wstring(percent) + L"%\r\n" + message +
                              L"\r\n初回のみ時間がかかります。このウィンドウは閉じないでください。";
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
    const std::wstring text = L"Minecraftをコピー中  " + std::to_wstring(percent) + L"%\r\n" + size +
                              L"\r\nゲーム本体を専用フォルダーへコピーしています。初回のみ時間がかかります。\r\nこのウィンドウは閉じないでください。";
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

// Asks the site whether a newer launcher is out and, if so, downloads and checks it (in the background).
void CheckLauncherUpdate()
{
    if (!LauncherBuild || launcherExe.empty()) return;
    selfUpdateThread = std::thread([] {
        auto result = new LauncherUpdateResult;
        const auto list = privateDirectory / "launcher.json.download";
        const auto folder = privateDirectory / "launcher-update";
        try
        {
            DownloadHttps(LauncherListUrl, list, 64 * 1024);
            const auto info = ParseLauncherInfo(ReadJson(list));
            if (LauncherUpdateWanted(LauncherBuild, info))
            {
                fs::create_directories(folder);
                const auto file = folder / "MyClientLauncher.exe";
                DownloadHttps(info.url, file, info.bytes);
                if (fs::file_size(file) != info.bytes || Sha256File(file) != info.sha256)
                    throw std::runtime_error("ダウンロードした新しいランチャーの SHA256 が一致しません。");
                RequireX64Pe(file);
                result->file = file;
                result->build = info.build;
            }
        }
        catch (const std::exception& error) { result->error = error.what(); }
        std::error_code ignored; fs::remove(list, ignored);
        if (!PostMessageW(window, LauncherUpdateMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

// Puts the downloaded launcher in this one's place and starts it. Only while nothing is being prepared.
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
        if (ec) throw std::runtime_error("新しいランチャーを置けません（フォルダーに書き込めない可能性があります）: " + ec.message());
        SwapLauncher(launcherExe, staged);
    }
    catch (const std::exception& error)
    {
        std::error_code ignored; fs::remove(staged, ignored);
        Status(L"ランチャーを自動で更新できませんでした。サイトから新しいランチャーを入れ直してください。\r\n" + Wide(error.what()));
        return;
    }
    Status(L"ランチャーを build " + std::to_wstring(build) + L" に更新しました。再起動します…");
    // The new launcher must be able to take the single-instance lock.
    if (singleInstance) { CloseHandle(singleInstance); singleInstance = nullptr; }
    std::wstring command = L"\"" + launcherExe.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(launcherExe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        launcherExe.parent_path().c_str(), &startup, &process))
    {
        Status(L"新しいランチャーは置けましたが、起動できませんでした。もう一度ランチャーを開いてください。");
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    DestroyWindow(window);
}

void Save()
{
    sources[selected.target.id] = fs::path(Text(Game)).u8string();
    Json config{{"sources", sources}, {"selectedVersion", selected.target.id}, {"imports", imports}, {"xboxSeen", xboxSeen}, {"channel", ChannelKey(wantedChannel)}, {"syncNormal", syncNormal}, {"schemaVersion", 3}};
    const auto temporary = privateDirectory / "launcher.json.tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << config.dump(2);
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), (privateDirectory / "launcher.json").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("ランチャー設定を保存できません。");
}

// Remember what /launcher/status said about the account: the home screen reads accountStatus, and the renewed
// login key (the Worker renews it when under 30 days are left) and the name / icon link are kept on this PC.
void NoteAccountStatus(const AccountStatus& status)
{
    accountStatus = status;
    accountExpired = false;
    StoredAccount stored = LoadAccount(privateDirectory);
    if (!status.token.empty()) stored.token = status.token;
    stored.name = status.name;
    stored.avatarUrl = status.avatarUrl;
    storedAccount = stored;
    try { if (!stored.token.empty()) SaveAccount(privateDirectory, stored); } catch (const std::exception&) { /* the old key still works */ }
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
    dialog.lpstrFilter = L"Windowsプログラム (*.exe)\0*.exe\0\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const auto title = L"Minecraft " + Wide(selected.target.id) + L"のMinecraft.Windows.exe";
    dialog.lpstrTitle = title.c_str();
    if (GetOpenFileNameW(&dialog))
    {
        SetWindowTextW(GetDlgItem(window, id), path.data());
        Save();
        Status(L"選択を保存しました。「適用して起動」で導入先を確認して起動します。");
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
            throw std::runtime_error("準備処理のログを作成できません。");
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = startup.hStdError = log;
        startup.hStdInput = nullInput;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, !logPath.empty(),
                        logPath.empty() ? 0 : CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
        throw std::runtime_error("プログラムを起動できません。Windowsエラー: " + std::to_string(GetLastError()));
    CloseHandle(process.hThread);
    return process.hProcess;
}

// Match the executable's directory, including console builds.
// Denied access to a Minecraft process blocks replacement rather than assuming
// the game is stopped. Other accessible Minecraft instances are ignored.
DWORD GamePid(const fs::path& directory)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("起動中のゲームを確認できません。");
    struct SnapshotGuard { HANDLE handle; ~SnapshotGuard() { CloseHandle(handle); } } guard{snapshot};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    DWORD found = 0;
    if (!Process32FirstW(snapshot, &entry)) throw std::runtime_error("起動中のゲーム一覧を取得できません。");
    do
    {
        const auto name = Lower(fs::path(entry.szExeFile).u8string());
        if (name != "minecraft.windows.exe" && name != "minecraft.windows.console.exe") continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process)
        {
            if (GetLastError() == ERROR_INVALID_PARAMETER) continue; // Already exited.
            throw std::runtime_error("Minecraftの確認に失敗しました。ゲームを終了して再試行してください。");
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
            throw std::runtime_error("Minecraftの実行パスを取得できません。ゲームを終了して再試行してください。");
        }
        std::error_code error;
        if (fs::equivalent(fs::path(std::wstring(path.data(), length)).parent_path(), directory, error))
        {
            if (found) throw std::runtime_error("同じ環境のMinecraftが複数起動しています。1つにしてください。");
            found = entry.th32ProcessID;
        }
        else if (error) throw std::runtime_error("Minecraftの導入先を比較できません。");
    } while (Process32NextW(snapshot, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        throw std::runtime_error("ゲーム一覧の確認が途中で失敗しました。再試行してください。");
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
    if (!bytes || !size) throw std::runtime_error("ローダーの準備スクリプト（PrepareRuntime.ps1）が見つかりません。");
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
        if (!out) throw std::runtime_error("ローダーの準備スクリプトを書き込めません。");
    }
    return path;
}

void StartHud(DWORD pid)
{
    if (hudProcess)
    {
        if (WaitForSingleObject(hudProcess, 0) == WAIT_TIMEOUT && hudGamePid == pid)
        {
            Status(L"HUDは既に起動しています。ゲームでGを押して設定を開いてください。");
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
    Status(L"DLLへの接続を確認し、HUDを起動しました。\r\nワールドに入り、G → Arrow Counter / Block OutlineをONにしてください。" +
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
    if (wanted) PushLog(L"診断モードで起動します（dumps フォルダーがあるため）。結果は " + (privateDirectory / "dumps").wstring() + L" に書かれます。", false, false);
}

void LaunchGame()
{
    DiagnosticsEnvironment();
    RequireExecutableVersion(pending.directory / "Minecraft.Windows.exe", selected.target);
    RequireRuntime(pending.directory, packageRoot / "runtime.lock.json", selected.target);
    const DWORD pid = GamePid(pending.directory);
    if (pid)
    {
        if (!Ready(pid))
            throw std::runtime_error("Minecraftが起動中です。DLLを導入するには一度ゲームを終了してください。");
        StartHud(pid);
        return;
    }
    const auto backup = InstallMod(packageRoot / "mods/MyClientData", pending.directory, selected.target);
    if (!backup.empty()) Status(L"以前のMODをバックアップしました:\r\n" + backup.wstring());
    if (gameProcess) { CloseHandle(gameProcess); gameProcess = nullptr; }
    // The official PeEditor/PreLoader prepare the managed executable; no
    // external launcher, protocol activation or registration lookup is used.
    gameProcess = Start(pending.directory / "Minecraft.Windows.exe", {}, pending.directory);
    startedAt = GetTickCount64();
    Busy(State::Waiting);
    Status(L"Minecraftを直接起動しました。DLLへの接続を待っています…\r\n接続を確認するとHUDを自動で起動します。" +
           (launchNote.empty() ? std::wstring{} : L"\r\n" + launchNote));
}

// Any running Minecraft.Windows.exe, regardless of its install folder.
DWORD GamePidAny()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("起動中のゲームを確認できません。");
    struct SnapshotGuard { HANDLE handle; ~SnapshotGuard() { CloseHandle(handle); } } guard{snapshot};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    DWORD found = 0;
    if (!Process32FirstW(snapshot, &entry)) throw std::runtime_error("起動中のゲーム一覧を取得できません。");
    do
    {
        if (_wcsicmp(entry.szExeFile, L"Minecraft.Windows.exe") != 0) continue;
        if (found) throw std::runtime_error("Minecraftが複数起動しています。1つにしてください。");
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
    if (!process) { result.message = "プロセスを開けません (Windowsエラー " + std::to_string(GetLastError()) + ")"; return result; }
    struct Guard { HANDLE h; ~Guard() { CloseHandle(h); } } guard{process};
    const std::wstring path = dll.wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { result.message = "メモリ確保に失敗 (Windowsエラー " + std::to_string(GetLastError()) + ")"; return result; }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, path.c_str(), bytes, &written) || written != bytes)
    {
        result.message = "パスの書き込みに失敗 (Windowsエラー " + std::to_string(GetLastError()) + ")";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return result;
    }
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
    HANDLE thread = loadLibrary ? CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr) : nullptr;
    if (!thread)
    {
        result.message = "スレッド作成に失敗 (Windowsエラー " + std::to_string(GetLastError()) + ")";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return result;
    }
    const DWORD wait = WaitForSingleObject(thread, 30000);
    DWORD module = 0;
    GetExitCodeThread(thread, &module);
    CloseHandle(thread);
    if (wait == WAIT_OBJECT_0) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    if (wait != WAIT_OBJECT_0) result.message = "LoadLibraryが30秒で終わりませんでした";
    else if (!module) result.message = "LoadLibraryWが失敗しました。依存DLLの不足、またはファイルのアクセス権を確認してください";
    else result.ok = true;
    return result;
}

// A release that runs without LeviLamina (StandaloneWin.h): the untouched exe is started suspended, the DLL of
// the package is loaded into it, then the game goes on. Nothing is installed into the game's folder.
fs::path StandaloneDll() { return packageRoot / "mods/MyClientData/MyClientData.dll"; }

void LaunchStandalone()
{
    DiagnosticsEnvironment();
    const DWORD pid = GamePid(pending.directory);
    if (pid)
    {
        if (!Ready(pid))
            throw std::runtime_error("Minecraftが起動中です。DLLを読み込むには一度ゲームを終了してください。");
        StartHud(pid);
        return;
    }
    RequireStandaloneDll(StandaloneDll());
    UseOriginalExe(pending.directory);
    RequireExecutableVersion(pending.directory / "Minecraft.Windows.exe", selected.target);
    if (gameProcess) { CloseHandle(gameProcess); gameProcess = nullptr; }
    const auto started = StartSuspendedWithDll(pending.directory / "Minecraft.Windows.exe", pending.directory, StandaloneDll(),
        [](DWORD target, const fs::path& dll) { const auto result = InjectDll(target, dll); return result.ok ? std::string{} : result.message; });
    gameProcess = started.process;
    startedAt = GetTickCount64();
    Busy(State::Waiting);
    Status(std::wstring(L"Minecraftを起動し、OriginXのDLLを読み込みました（LeviLaminaなし）。DLLへの接続を待っています…") +
           (started.loadedSignal ? L"" : L"\r\nDLLからの開始の合図がまだありません。接続できない場合はログを確認してください。") +
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

void LaunchAttach()
{
    DiagnosticsEnvironment();
    const bool standalone = selected.target.standalone;
    if (standalone) RequireStandaloneDll(StandaloneDll());
    else RequireAttachRuntime(attachDir, packageRoot / "runtime.lock.json", selected.target);
    const DWORD pid = GamePidAny();
    if (pid && Ready(pid)) { StartHud(pid); return; }
    if (!standalone)
    {
        const auto backup = InstallMod(packageRoot / "mods/MyClientData", attachDir, selected.target);
        if (!backup.empty()) Status(L"以前のMODをバックアップしました:\r\n" + backup.wstring());
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
            throw std::runtime_error("Minecraftを起動できません。Xboxアプリから一度起動できるか確認してください。(ShellExecute " + std::to_string(result) + ")");
        Status(L"Minecraftを通常起動しました。起動後にローダーを注入します…\r\n接続を確認するとHUDを自動で起動します。" +
               (launchNote.empty() ? std::wstring{} : L"\r\n" + launchNote));
    }
    else Status(L"起動中のMinecraftへローダーを注入します…");
    Busy(State::Attaching);
}

void ApplyAttach(const fs::path& source)
{
    // Only MicrosoftGame.config is read; the protected executable is never opened.
    pending = ResolveGame(source, selected.target, false);
    attachMode = true;
    attachDir = privateDirectory / "attach" / fs::u8path(selected.target.id);
    fs::create_directories(attachDir);
    const auto marker = attachDir / "MyClient-game.json";
    if (!fs::exists(marker))
    {
        std::ofstream output(marker, std::ios::binary);
        output << Json{{"owner", "MyClient"}, {"schemaVersion", 1}, {"mode", "attach"}}.dump(2);
        output.close();
        if (!output) throw std::runtime_error("注入用フォルダーの情報を保存できません。");
    }
    if (selected.target.standalone) { LaunchAttach(); return; }   // no loader to prepare: the DLL itself is loaded
    bool ready = false;
    try { RequireAttachRuntime(attachDir, packageRoot / "runtime.lock.json", selected.target); ready = true; }
    catch (const std::exception&) { /* First use or interrupted preparation. */ }
    if (ready) { LaunchAttach(); return; }
    std::vector<wchar_t> systemPath(32768);
    const UINT length = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
    if (!length || length >= systemPath.size()) throw std::runtime_error("Windowsのシステムフォルダーを取得できません。");
    const auto powershell = fs::path(std::wstring(systemPath.data(), length)) / "WindowsPowerShell/v1.0/powershell.exe";
    installer = Start(powershell,
        {L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
         RuntimeScript().wstring(), L"-ManifestPath", (packageRoot / "runtime.lock.json").wstring(),
         L"-ExpectedGameVersion", Wide(selected.target.runtimeGame), L"-ExpectedLoaderVersion", Wide(selected.target.loader),
         L"-GameDirectory", attachDir.wstring(), L"-CacheDirectory", (privateDirectory / "cache").wstring(), L"-Attach"},
         attachDir, privateDirectory / "runtime-setup.log");
    Busy(State::Installing);
    BeginProgress();
    Status(L"ローダーを公式配布元から準備しています（本体は変更しません）。初回はインターネット接続が必要です。\r\n進行状況は「ログを開く」で確認できます。");
}

void PrepareRuntimeOrLaunch()
{
    if (selected.target.standalone) { LaunchStandalone(); return; }   // no loader to prepare, the exe stays as it is
    RequireExecutableVersion(pending.directory / "Minecraft.Windows.exe", selected.target);
    if (GamePid(pending.directory)) throw std::runtime_error("ローダーの導入前にMinecraftを終了してください。");
    bool ready = false;
    try { RequireRuntime(pending.directory, packageRoot / "runtime.lock.json", selected.target); ready = true; }
    catch (const std::exception&) { /* First use or interrupted preparation. */ }
    if (ready) { LaunchGame(); return; }
    const auto originalImports = ImportedDlls(pending.directory / "MyClient-original.exe");
    if (originalImports.count("preloader.dll") || originalImports.count("levilauncher.dll"))
        throw std::runtime_error("保存した元のゲーム本体が変更されています。公式インストール元から準備し直してください。");
    std::vector<wchar_t> systemPath(32768);
    const UINT length = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
    if (!length || length >= systemPath.size()) throw std::runtime_error("Windowsのシステムフォルダーを取得できません。");
    const auto powershell = fs::path(std::wstring(systemPath.data(), length)) / "WindowsPowerShell/v1.0/powershell.exe";
    installer = Start(powershell,
        {L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
         RuntimeScript().wstring(), L"-ManifestPath", (packageRoot / "runtime.lock.json").wstring(),
         L"-ExpectedGameVersion", Wide(selected.target.runtimeGame), L"-ExpectedLoaderVersion", Wide(selected.target.loader),
         L"-GameDirectory", pending.directory.wstring(), L"-CacheDirectory", (privateDirectory / "cache").wstring()},
         pending.directory, privateDirectory / "runtime-setup.log");
    Busy(State::Installing);
    BeginProgress();
    Status(L"必要なローダーを公式配布元から準備しています。初回はインターネット接続が必要です。\r\n準備が終わるとMinecraftを起動します。進行状況は「ログを開く」で確認できます。");
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
                throw std::runtime_error("別バージョンのMinecraftを終了してから切り替えてください。");
    RequireMod(packageRoot / "mods/MyClientData", selected.target);
    RequireX64Pe(HudExecutable());
    const auto destination = privateDirectory / "game" / selected.target.id;
    if (fs::exists(destination))
    {
        pending = PrepareManagedGame({}, destination, selected.target);
        SetWindowTextW(GetDlgItem(window, Game), (destination / "Minecraft.Windows.exe").c_str());
        Save();
        const DWORD pid = GamePid(pending.directory);
        if (pid)
        {
            if (!Ready(pid)) throw std::runtime_error("Minecraftが起動中です。準備や更新の前にゲームを終了してください。");
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
    const auto instance = ResolveGame(source, selected.target);
    RequireExecutableVersion(source, selected.target);
    if (GamePid(instance.directory)) throw std::runtime_error("ゲーム本体をコピーする前にMinecraftを終了してください。");
    Busy(State::Preparing);
    copyDone = 0;
    copyTotal = 0;
    BeginProgress();
    Status(L"Minecraftを自作ソフト用のフォルダーへコピーしています…\r\n初回は時間とゲーム本体分の空き容量が必要です。");
    const auto target = selected.target;
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
            throw std::runtime_error("ローダーの準備に失敗しました。ログを開いて確認してください。終了コード: " + std::to_string(exitCode));
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
            if (now - seenAt >= 1500)
            {
                const auto result = InjectDll(pid, selected.target.standalone ? StandaloneDll() : attachDir / "PreLoader.dll");
                if (result.ok)
                {
                    injectedPid = pid;
                    Status(selected.target.standalone ? L"OriginXのDLLを読み込みました。DLLへの接続を待っています…"
                                                      : L"PreLoader.dllを注入しました。DLLへの接続を待っています…");
                }
                else if (++injectFails >= 20)
                {
                    Busy(State::Idle);
                    throw std::runtime_error("ローダーの注入に失敗しました: " + result.message);
                }
                else if (injectFails == 1 || injectFails % 5 == 0)
                    Status(L"注入を再試行しています (" + std::to_wstring(injectFails) + L"回目): " + Wide(result.message));
            }
        }
        if (now - startedAt > 180000)
        {
            Busy(State::Idle);
            Status(injectedPid ?
                L"注入はしましたが、DLLへの接続を確認できませんでした。\r\nPreLoaderが後読みに対応していない可能性があります。ログを開いて確認してください。" :
                L"Minecraftのプロセスを確認できませんでした。Xboxアプリから起動できるか確認してください。");
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
            throw std::runtime_error("Minecraftが接続前に終了しました。Windowsエラー/終了コード: " + std::to_string(exitCode) +
                "。Xboxアプリでの起動、Gaming Services・GameInput・VC++ランタイムを確認してください。");
        }
        if (GetTickCount64() - startedAt > 120000)
        {
            Busy(State::Idle);
            Status(L"接続を確認できませんでした。ゲームのログとWindowsの必要な部品を確認してください。\r\nゲーム起動後に「適用して起動」を押すと、読み込み済みのDLLへの接続を再試行できます。");
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
    if (!handle) throw std::runtime_error("画面を作成できません。");
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
        searchReport += root + L"XboxGames: " + (exists ? L"あり" : L"なし") + L"\r\n";
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
            searchReport += std::wstring(L"パッケージ: ") + folder + L"\r\n";
            add(fs::path(folder) / L"Content" / L"Minecraft.Windows.exe");
            add(fs::path(folder) / L"Minecraft.Windows.exe");
        }
        RegCloseKey(packages);
    }
    else searchReport += L"パッケージ情報: 読み取れません\r\n";
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
    catch (const std::exception&) { searchReport += L"LeviLauncher: 読み取れません\r\n"; }
    // Folders added with "フォルダーを追加…".
    for (const auto& folder : imports)
    {
        if (!folder.is_string()) continue;
        const auto path = fs::u8path(folder.get<std::string>());
        searchReport += L"追加したフォルダー: " + path.wstring() + L"\r\n";
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
            Status(L"本体を自動検出しました:\r\n" + candidate.wstring() +
                   L"\r\n「適用して起動」で導入先を確認して起動します。");
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
                    Status(L"保護されたXbox版の本体を検出しました:\r\n" + candidate.wstring() +
                           L"\r\n本体は変更せず、起動後にローダーを注入する方式で起動します。「適用して起動」を押してください。");
                    return true;
                }
                catch (const std::exception& inner) { lastError = inner.what(); }
            }
        }
    }
    if (!quiet)
    {
        std::wstring message = L"Minecraft " + Wide(selected.target.id) + L"の本体を自動検出できませんでした。";
        if (checked) message += L"\r\n見つかった本体の確認結果: " + Wide(lastError);
        else message += L"\r\nXboxGamesフォルダーや起動中のMinecraftが見つかりません。「選択…」で指定してください。\r\n確認した場所:\r\n" +
                        (searchReport.empty() ? std::wstring(L"(なし)") : searchReport);
        Status(message);
    }
    return false;
}

void SelectVersion()
{
    const auto index = SendDlgItemMessageW(window, Version, CB_GETCURSEL, 0, 0);
    if (index < 0 || static_cast<size_t>(index) >= catalog.releases.size()) return;
    selected = catalog.releases[static_cast<size_t>(index)];
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
        Status(selected.available ? L"選択した版の本体を準備して起動できます。DLLは自動取得・検査します。" : Wide(selected.reason));
}

void RefreshVersions(const std::string& preferred)
{
    SendDlgItemMessageW(window, Version, CB_RESETCONTENT, 0, 0);
    size_t chosen = 0;
    for (size_t i = 0; i < catalog.releases.size(); ++i)
    {
        const auto& release = catalog.releases[i];
        const auto label = Wide(release.target.id) + (release.available ? L"  — DLLあり" : L"  — 対応待ち");
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
        throw std::runtime_error("フォルダー選択画面を開けません。");
    struct ComRelease { IUnknown* p; ~ComRelease() { if (p) p->Release(); } } releaseDialog{dialog};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Minecraftの版が入っているフォルダー（Minecraft.Windows.exeがある場所）");
    if (dialog->Show(window) != S_OK) return;   // cancelled
    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item)) || !item) throw std::runtime_error("選択したフォルダーを読み取れません。");
    ComRelease releaseItem{item};
    PWSTR text = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) || !text) throw std::runtime_error("ローカルのフォルダーを選択してください。");
    const fs::path folder(text);
    CoTaskMemFree(text);
    fs::path game;
    for (const auto& candidate : {folder / L"Minecraft.Windows.exe", folder / L"Content" / L"Minecraft.Windows.exe"})
    {
        std::error_code error;
        if (fs::exists(candidate, error) || ProbeRead(candidate) == ERROR_ACCESS_DENIED) { game = candidate; break; }
    }
    if (game.empty()) throw std::runtime_error("このフォルダーにMinecraft.Windows.exeがありません。ゲーム本体のフォルダーを選択してください。");
    const auto id = InstalledVersion(game.parent_path());
    if (id.empty())
        throw std::runtime_error("このフォルダーのMinecraftの版を読み取れません。GDKのRelease版（MicrosoftGame.configがあるもの）が必要です。Preview版には対応していません。");
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
    Status(L"Minecraft " + Wide(id) + L"を追加しました:\r\n" + game.wstring() +
           L"\r\nOriginXはこの版にまだ対応していません。対応版が出ると一覧に追加されます。");
    Refresh();
}

// The public list of released versions (the one LeviLauncher reads). The last copy is kept for offline use.
void LoadVersionDb(const fs::path& file)
{
    const auto db = ReadJson(file);
    publishedVersions = ParseVersionDb(db);
    versionLinks = ParseVersionLinks(db);
    std::error_code error;
    const auto written = fs::last_write_time(file, error);
    if (error) { versionDbNote = L"保存済み"; return; }
    const auto age = std::chrono::duration_cast<std::chrono::hours>(fs::file_time_type::clock::now() - written).count();
    versionDbNote = age < 24 ? L"最新" : L"保存済み（" + std::to_wstring(age / 24) + L"日前）";
}

void FetchVersionDb()
{
    if (versionDbThread.joinable()) return;
    versionDbThread = std::thread([] {
        auto result = new VersionDbResult;
        const auto temporary = privateDirectory / "versions_db.download";
        try
        {
            DownloadHttps(VersionDbUrl, temporary, 1024 * 1024);
            const auto db = ReadJson(temporary);
            result->versions = ParseVersionDb(db);
            result->links = ParseVersionLinks(db);
            if (!MoveFileExW(temporary.c_str(), (privateDirectory / "versions_db.json").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("公開版の一覧を保存できません。");
        }
        catch (const std::exception& error) { result->error = error.what(); }
        std::error_code ignored;
        fs::remove(temporary, ignored);
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
    if ($Mode -ne 'install') {
        if ($Mode -eq 'check' -and $ExpectedId -and (Get-MinecraftVersionId $game.Version) -eq $ExpectedId) {
            Write-Output ('Normal launch is already ' + $ExpectedId)
            exit 10
        }
        exit 0
    }
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

// The last lines PowerShell wrote, for error messages.
std::string LogTail(const fs::path& log, size_t limit = 600)
{
    std::ifstream input(log, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    if (text.size() > limit)
    {
        size_t from = text.size() - limit;
        while (from < text.size() && (static_cast<unsigned char>(text[from]) & 0xC0) == 0x80) ++from;   // not inside a UTF-8 character
        text = "…" + text.substr(from);
    }
    return text + "\n（ログ: " + log.u8string() + "）";
}

DWORD RunSwitchScript(const fs::path& script, const std::wstring& mode, const fs::path& package, const fs::path& helper,
                      const fs::path& log, const std::string& expectedId)
{
    std::vector<wchar_t> systemPath(32768);
    const UINT length = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
    if (!length || length >= systemPath.size()) throw std::runtime_error("Windowsのシステムフォルダーを取得できません。");
    const auto powershell = fs::path(std::wstring(systemPath.data(), length)) / "WindowsPowerShell/v1.0/powershell.exe";
    HANDLE process = Start(powershell, {L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
                                        script.wstring(), L"-Mode", mode, L"-Package", package.wstring(), L"-Helper", helper.wstring(),
                                        L"-ExpectedId", Wide(expectedId)},
                           script.parent_path(), log);
    if (!process) throw std::runtime_error("PowerShellを起動できません。");
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
    if (!script) throw std::runtime_error("版の変更の準備ファイルを書き込めません。");
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
            if (code == 2) result->error = "Gaming Servicesが入っていません";
            else if (code == 3) result->error = "Minecraftが入っていません";
            else if (result->id.empty()) result->error = "読み取れません（" + std::to_string(code) + "）: " + LogTail(log, 300);
        }
        catch (const std::exception& error) { result->error = error.what(); }
        if (!PostMessageW(window, NormalVersionMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

void SwitchGame(const std::string& id, const std::vector<std::string>& urls, const fs::path& folder, const fs::path& helper,
                std::string& warning)
{
    const auto script = folder / "switch-game.ps1";
    switchPhase = 0;
    switch (const DWORD code = RunSwitchScript(script, L"check", {}, {}, folder / "switch-check.log", id))
    {
    case 0: break;
    case 10: return; // already registered: no download/install, also safe when that game is running
    case 2: throw std::runtime_error("Gaming Servicesが入っていません。Microsoft Storeで「Gaming Services」を入れてから、もう一度押してください。");
    case 3: throw std::runtime_error("Minecraftが入っていません。先にXboxアプリかMicrosoft StoreからMinecraftを入れてください（購入済みのMinecraftを別の版に置き換える仕組みです）。");
    default: throw std::runtime_error("Minecraftの状態を確認できませんでした（" + std::to_string(code) + "）: " + LogTail(folder / "switch-check.log"));
    }
    if (GamePidAny()) throw std::runtime_error("Minecraftを終了してから版を変更してください。起動中のゲームは終了しません。");
    if (urls.empty()) throw std::runtime_error("通常起動の版を変更するためのダウンロード先を取得できません。公開版の一覧を更新してから選び直してください。");
    switchPhase = 1;
    const auto package = folder / ("Microsoft.MinecraftUWP_" + id + ".msixvc");
    struct Remove { fs::path path; ~Remove() { std::error_code ignored; fs::remove(path, ignored); } } removeLater{package};
    std::string errors;
    bool downloaded = false;
    for (const auto& url : PackageDownloadOrder(urls))
    {
        try { DownloadGamePackage(url, package, switchDone, switchTotal); downloaded = true; break; }
        catch (const std::exception& error)
        {
            if (!errors.empty()) errors += " / ";
            errors += error.what();
            if (std::string(error.what()).find("空き容量") != std::string::npos) break;
        }
    }
    if (!downloaded) throw std::runtime_error("Minecraft " + id + " をダウンロードできませんでした: " + errors);
    switchPhase = 2;
    const DWORD code = RunSwitchScript(script, L"install", package, helper, folder / "switch-install.log", id);
    if (code == 5)
        warning = "起動役（gamelaunchhelper.dll）を差し替えられませんでした。Xboxアプリから起動すると最新版への更新を求められることがあります: " +
                  LogTail(folder / "switch-install.log");
    else if (code)
    {
        // Installed some other way than the Xbox app / Store: say so, the reason is usually there.
        const bool notStore = LogTail(folder / "switch-check.log").find("Not from the Store") != std::string::npos;
        const std::string log = LogTail(folder / "switch-install.log", 2400);
        const std::string hint = InstallErrorHint(log);
        throw std::runtime_error("Windowsがインストールできませんでした（" + std::to_string(code) + "）: " + (hint.empty() ? "" : hint + "\n") + log +
                                 (notStore ? "\n今のMinecraftはXboxアプリ・Microsoft Store以外の方法で入っているため、Windowsが置き換えを断った可能性があります。" : ""));
    }
}

void StartSwitch(const std::string& id, bool launchAfter)
{
    if (state != State::Idle) return;
    const auto release = std::find_if(catalog.releases.begin(), catalog.releases.end(), [&](const Release& item) {
        return item.target.id == id;
    });
    if (release == catalog.releases.end() || !ClientVersionSelectable(id, release->available))
        throw std::runtime_error("この版はOriginX未対応のため、インストール・切替できません。対応済みの26.45以降を選んでください。");
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
            if (!out) throw std::runtime_error("版の変更の準備ファイルを書き込めません。");
        }
    }
    if (switchThread.joinable()) switchThread.join();
    switchingId = id;
    switchPhase = 0;
    switchDone = 0;
    switchTotal = 0;
    Busy(State::Switching);
    BeginProgress();
    Status(L"Minecraft " + Wide(id) + L" の通常起動を確認・変更しています…");
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
        throw std::runtime_error("この版はOriginX未対応のため、選べません。対応済みの26.45以降を選んでください。");
    RefreshVersions(id);
    Save();
    const bool here = installedGames.count(id) || !Text(Game).empty();
    Status(L"OriginX の版を " + Wide(id) + L" にしました（Windowsの通常起動の版はそのまま）。" +
           (here ? std::wstring{} : L"\r\nこの版はまだこの PC にありません。Launch を押すと、確認のあとダウンロードして Windows に入れます。"));
    GoTo(0);
}

void PollSwitchProgress()
{
    const unsigned long long done = switchDone, total = switchTotal;
    int percent = 0;
    std::wstring detail;
    if (switchPhase == 0) detail = L"Gaming Services と今の Minecraft を確認しています…";
    else if (switchPhase == 1)
    {
        percent = total ? static_cast<int>(std::min<unsigned long long>(100, done * 100 / total)) : 0;
        wchar_t size[96];
        swprintf(size, 96, L"%.2f / %.2f GB", done / 1073741824.0, total / 1073741824.0);
        detail = std::wstring(L"Microsoft の配信サーバーからダウンロード中  ") + size;
    }
    else
    {
        percent = 100;
        detail = L"Windows がインストールしています。数分かかることがあります…";
    }
    const std::wstring text = L"Minecraft を " + Wide(switchingId) + L" に変更中  " + std::to_wstring(percent) + L"%\r\n" + detail +
                              L"\r\nこのウィンドウは閉じないでください。";
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
    Status(L"対応バージョンとDLLの更新を確認しています…");
    const auto previous = catalog;
    preparation = std::thread([previous] {
        auto result = new UpdateResult;
        const auto temporary = privateDirectory / "catalog.download";
        try {
            DownloadHttps(CatalogUrl, temporary, 1024 * 1024);
            result->catalog = ParseCatalog(ReadJson(temporary));
            CheckAdvance(previous, result->catalog);
            if (!MoveFileExW(temporary.c_str(), (privateDirectory / "catalog.json").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("更新一覧を保存できません。");
        }
        catch (const std::exception& error) { result->error = error.what(); }
        std::error_code ignored; fs::remove(temporary, ignored);
        if (!PostMessageW(window, UpdatedMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
}

void FetchAndApply()
{
    if (!selected.available) throw std::runtime_error(selected.reason);
    Save();
    Busy(State::Fetching);
    Status(L"選択した版のDLLを取得・検査しています…\r\n保存済みの同じ版は再ダウンロードしません。");
    const auto release = selected;
    const Channel wanted = wantedChannel;
    const std::string token = wanted == Channel::Stable ? std::string{} : LoadAccount(privateDirectory).token;
    const unsigned epoch = accountEpoch.Now();
    preparation = std::thread([release, wanted, token, epoch] {
        auto result = new PackageResult;
        result->epoch = epoch;
        try {
            // The chosen channel first (docs/superpowers/specs/2026-11-09-release-channels-design.md): any problem is a
            // reason shown to the user and the stable release is used, so Launch is never stopped by a channel.
            AccountStatus status;   // alive until the end: the plan points into it
            if (wanted != Channel::Stable)
            {
                const AccountStatus* known = nullptr;
                std::string error;
                try { status = FetchStatus(token); known = &status; result->status = status; }
                catch (const std::exception& e) { error = e.what(); }
                const LaunchPlan plan = PlanLaunch(wanted, known, error, release.target.id, &release);
                result->note = plan.reason;
                if (plan.release)
                {
                    try
                    {
                        result->root = AcquirePackage(privateDirectory / "packages", *plan.release, Sha256File, DownloadHttps,
                                                      PackageFolder(plan.channel, *plan.release));
                        result->local = false;
                        result->channel = plan.channel;
                    }
                    catch (const std::exception& e)
                    {
                        // e.g. the signed link expired during a long download: nothing half-written is used
                        result->note = std::string(ChannelName(wanted)) + "を取得できなかったため、安定版で起動します。理由: " + e.what();
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
                    result->root = AcquirePackage(privateDirectory / "packages", release, Sha256File, DownloadHttps);
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
                            throw std::runtime_error("同梱更新ファイルが見つかりません。");
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
    editable.push_back(Control(L"BUTTON", L"自動検出", 856, 116, 126, 42, AutoDetect, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", L"選択…", 990, 116, 126, 42, PickGame, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", L"フォルダーを追加…", 896, 210, 220, 42, ImportGame, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", L"更新を確認", 896, 318, 220, 42, CheckUpdates, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", L"使い方・必要な部品", 896, 426, 220, 42, Guide, WS_TABSTOP | BS_OWNERDRAW));
    ShowPage();
    // Downloaded on its own (no folder around it), the launcher has no bundled list: the list comes from the
    // update check right below, and everything it names is downloaded.
    if (fs::is_regular_file(bundle / "catalog.json")) bundledCatalog = ParseCatalog(ReadJson(bundle / "catalog.json"));
    catalog = bundledCatalog;
    if (fs::exists(privateDirectory / "catalog.json"))
    {
        try {
            auto cached = ParseCatalog(ReadJson(privateDirectory / "catalog.json"));
            // A list saved by an older bundle (e.g. one still naming 26.32 / 26.40) is superseded by the bundled one.
            if (cached.sequence < catalog.sequence) { std::error_code ignored; fs::remove(privateDirectory / "catalog.json", ignored); }
            else { CheckAdvance(catalog, cached); catalog = std::move(cached); }
        }
        catch (const std::exception& error) { Status(L"保存済み一覧を使用できません: " + Wide(error.what())); }
    }
    std::string preferred = "26.51.1";
    if (fs::exists(privateDirectory / "launcher.json"))
    {
        const auto config = ReadJson(privateDirectory / "launcher.json");
        if (config.value("schemaVersion", 0) == 3)
        {
            sources = config.at("sources");
            if (!sources.is_object()) throw std::runtime_error("保存した本体一覧が不正です。");
            preferred = config.value("selectedVersion", preferred);
            if (config.contains("imports") && config.at("imports").is_array()) imports = config.at("imports");
            if (config.contains("xboxSeen") && config.at("xboxSeen").is_string()) xboxSeen = config.at("xboxSeen").get<std::string>();
            if (config.contains("channel") && config.at("channel").is_string()) wantedChannel = ParseChannel(config.at("channel").get<std::string>());
            if (config.contains("syncNormal") && config.at("syncNormal").is_boolean()) syncNormal = config.at("syncNormal").get<bool>();
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
    CheckLauncherUpdate();
    if (!SetTimer(window, 1, 500, nullptr)) throw std::runtime_error("接続確認タイマーを開始できません。");
    PostMessageW(window, CheckMessage, 0, 0);
    // The saved Discord login: the name and icon are drawn at once, the roles are asked in the background.
    storedAccount = LoadAccount(privateDirectory);
    ReloadAvatar();
    StartAccount(false);
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
    return ClientVersionRows(VersionRows(publishedVersions, entries, installed, downloadable));
}

size_t VersionCardChoice(const VersionFamilyRows& family)
{
    for (const auto& id : {switchingId, normalVersionId, selected.target.id})
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
            if (!control) throw std::runtime_error("バージョン選択欄を作成できません。");
            COMBOBOXINFO info{};
            info.cbSize = sizeof(info);
            if (!GetComboBoxInfo(control, &info) || !info.hwndList)
                throw std::runtime_error("バージョン一覧の表示を準備できません。");
            nativeVersionSelectorProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(control, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(VersionSelectorProc)));
            if (!nativeVersionSelectorProc) throw std::runtime_error("バージョン選択欄の配色を設定できません。");
            nativeVersionListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(info.hwndList, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(VersionListProc)));
            if (!nativeVersionListProc) throw std::runtime_error("バージョン一覧の配色を設定できません。");
            SendMessageW(control, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Sc(32));
        }
        SendMessageW(control, CB_RESETCONTENT, 0, 0);
        const auto& family = versionFamilies[index];
        for (const auto& row : family.rows)
        {
            const auto label = DisplayGameVersion(row.id) + (!row.supported ? L"　未対応・選択不可" : row.installed ? L"　インストール済み" :
                row.downloadable ? L"　ダウンロード" : L"　配信リンクなし");
            if (SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())) < 0)
                throw std::runtime_error("バージョン一覧を表示できません。");
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
    const auto label = DisplayGameVersion(row->id) + (!row->supported ? L"　未対応・選択不可" :
        row->installed ? L"　インストール済み" : row->downloadable ? L"　ダウンロード" : L"　配信リンクなし");
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
    if (interactive) { Status(L"ブラウザーで Discord にログインしてください。終わるとここへ自動で戻ります。"); Refresh(); }
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
    Status(L"ログアウトしました。");
    Refresh();
}

struct MenuEntry { std::wstring label; bool logout; Channel channel; bool current; };
std::vector<MenuEntry> ChannelMenu()
{
    std::vector<MenuEntry> entries;
    const auto allowed = accountStatus ? AllowedChannels(*accountStatus) : std::vector<Channel>{Channel::Stable};
    for (Channel channel : allowed)
        entries.push_back({Wide(ChannelName(channel)) + (channel == Channel::Early ? L"（supporter）" : channel == Channel::Test ? L"（moderator）" : L""),
                           false, channel, channel == wantedChannel});
    entries.push_back({L"ログアウト", true, Channel::Stable, false});
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
        Txt(dc, accountBusy ? L"ブラウザーで確認しています…" : (accountExpired ? L"Discord でログインし直す" : L"Discord でログイン"), button, smallFont,
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
    std::wstring label = L"通常起動: ", value, link;
    if (state == State::Switching && !switchingId.empty())
        value = (normalVersionId.empty() ? std::wstring{} : Wide(normalVersionId) + L" → ") + Wide(switchingId) + L" に切り替え中…";
    else if (normalRead == NormalRead::Unknown || normalRead == NormalRead::Reading) value = L"確認中…";
    else if (normalRead == NormalRead::Missing) value = normalReadNote.empty() ? std::wstring(L"読み取れません") : normalReadNote;
    else
    {
        value = Wide(normalVersionId);
        if (NormalSwitchOffered()) link = Wide(selected.target.id) + L" に切り替える  \u203A";
        else if (normalVersionId == selected.target.id) value += L"  \u2713 同じ版";
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
    Txt(dc, L"Minecraft のバージョン", MR(328, 48, 600, 30), boldFont, ColText);
    Txt(dc, L"1.26 系列の26.45以降を表示。対応済みの版を選ぶと、通常起動の Minecraft も自動で切り替わります。",
        MR(328, 86, 788, 36), smallFont, ColMuted, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    if (versionFamilies.empty()) Txt(dc, L"バージョン一覧を取得しています…", MR(348, 174, 700, 40), font, ColMuted);
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
        Txt(dc, L"バージョンを選ぶ", MR(700, y + 12, 370, 20), smallFont, ColText);
        if (scrolling)
        {
            Box(dc, MR(700, y + 38, 392, 34), 6, ColPanel, ColLine);
            Txt(dc, DisplayGameVersion(row.id) + L"  ▾", MR(712, y + 44, 370, 26), font, ColText);
        }
        std::wstring status = row.supported ? L"OriginX 対応" : L"OriginX 未対応・選択不可";
        if (row.supported)
            status += row.installed ? L" / インストール済み" : row.downloadable ? L" / 選ぶとダウンロード" : L" / 配信リンクなし";
        Txt(dc, status, MR(700, y + 80, 400, 22), smallFont, row.supported ? ColGreen : ColMuted,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        std::wstring current = row.id == normalVersionId ? L"通常起動に設定済み" : L"";
        if (row.id == switchingId)
            current = switchPhase.load() == 0 ? L"通常起動の版を確認中…" : switchPhase.load() == 1 ?
                L"ダウンロード中 " + std::to_wstring(std::max(progressPercent, 0)) + L"%" : L"Windows にインストール中…";
        Txt(dc, current, MR(700, y + 108, 400, 22), smallFont, ColAccentHi);
    }
    Txt(dc, L"暗い項目はOriginX未対応のため、インストール・切替できません。",
        MR(328, 302, 788, 30), smallFont, ColMuted);
    Txt(dc, L"公開一覧: " + versionDbNote, MR(328, 340, 788, 24), smallFont, ColMuted);
    Txt(dc, L"別の版に切り替えるときは Minecraft を終了してください。通常起動が別の版なら本体を取得します（PC内に別コピーがあっても対象）。同じ版なら再取得しません。",
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

void DrawGeneral(HDC dc)
{
    const UINT wrap = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX;
    Txt(dc, L"Game location", MR(328, 52, 500, 28), boldFont, ColText);
    Txt(dc, L"選択した版（" + Wide(selected.target.id) + L"）のMinecraft本体。自動検出、または「選択…」で指定します。",
        MR(328, 82, 790, 24), smallFont, ColMuted);
    Divider(dc, 178);
    Txt(dc, L"Version folders", MR(328, 198, 500, 28), boldFont, ColText);
    Txt(dc, L"LeviLauncherなどで入れた版のフォルダーを追加すると、Version の一覧に出ます。", MR(328, 228, 540, 44), smallFont, ColMuted, wrap);
    Divider(dc, 286);
    Txt(dc, L"Updates", MR(328, 306, 500, 28), boldFont, ColText);
    Txt(dc, L"公開された対応版とDLLの更新を確認します（起動時にも自動で確認）。", MR(328, 336, 540, 44), smallFont, ColMuted, wrap);
    Divider(dc, 394);
    Txt(dc, L"Help", MR(328, 414, 500, 28), boldFont, ColText);
    Txt(dc, L"使い方と、ゲームに必要なWindowsの部品。", MR(328, 444, 540, 44), smallFont, ColMuted, wrap);
    Divider(dc, 502);
    Txt(dc, L"Normal launch", MR(328, 522, 500, 28), boldFont, ColText);
    Txt(dc, syncNormal ? L"ON：Launch と版の選択で、Windows の通常起動（スタートメニューなど）の版も選んだ版に変えます。すでにその版ならダウンロードしません。"
                       : L"OFF：Windows の通常起動の版は変えません。この PC に無い版だけ、確認のあとダウンロードして入れます（このとき通常起動の版も変わります）。",
        MR(328, 552, 640, 44), smallFont, ColMuted, wrap);
    DrawSyncSwitch(dc);
    Divider(dc, 610);
    Txt(dc, L"DLLは版ごとに保存し、公開された更新を起動時に確認します。通常起動の版は Home の Launch の下からも変えられます。ワールドの版ごとの分離は未対応です。"
            L"対応待ちの版は起動できません。配布先が未公開・オフラインの場合は保存済み一覧を使います。",
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
            if (!result->error.empty()) Status(L"ランチャーの更新は確認できませんでした（このまま使えます）: " + Wide(result->error));
            else if (!result->file.empty())
            {
                readyLauncher = result->file;
                readyLauncherBuild = result->build;
                ApplyLauncherUpdate();   // now if idle, else when the current step ends (Busy)
            }
            return 0;
        }
        case ApplyLauncherMessage: ApplyLauncherUpdate(); return 0;
        case UpdatedMessage:
        {
            std::unique_ptr<UpdateResult> result(reinterpret_cast<UpdateResult*>(lParam));
            if (preparation.joinable()) preparation.join();
            Busy(State::Idle);
            if (result->error.empty()) catalog = std::move(result->catalog);
            RefreshVersions(selected.target.id);
            Status(result->error.empty() ? L"更新一覧を確認しました。版を選んで起動してください。" :
                L"保存済み一覧を使用します。" + Wide(result->error));
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
                else if (MessageBoxW(window, (L"Minecraft " + Wide(id) + L" はまだこの PC にありません。\r\n\r\nMicrosoft からダウンロード（数GB）して Windows に入れてから起動します。"
                                              L"Windows の通常起動の版も " + Wide(id) + L" に変わります。\r\n\r\n続けますか？").c_str(),
                                     L"OriginX", MB_YESNO | MB_ICONQUESTION) == IDYES)
                    StartSwitch(id, true);
                else Status(L"起動を取り消しました（Minecraft " + Wide(id) + L" はこの PC にありません）。");
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
                versionDbNote = L"最新";
            }
            else if (publishedVersions.empty()) versionDbNote = L"取得できません: " + Wide(result->error);
            SyncVersionCards();
            return 0;
        }
        case SwitchedMessage:
        {
            std::unique_ptr<SwitchResult> result(reinterpret_cast<SwitchResult*>(lParam));
            if (!result) throw std::runtime_error("Minecraftの版変更の結果を受け取れませんでした。");
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
                    throw std::runtime_error("Minecraft " + result->id + " を Windows に入れられなかったため、起動できません（この PC にこの版がありません）。\n理由: " + result->error);
                launchNote = L"Windowsの通常起動の版は変更できませんでした（Start menuなどのMinecraftは今のまま）。OriginXは選んだ版 " +
                             Wide(result->id) + L" で起動します。\r\n理由: " + Wide(result->error);
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
                Status(L"OriginX の版は " + Wide(result->id) + L" にしました。Windowsの通常起動の版は変更できませんでした（Start menuなどのMinecraftは今のまま）。\r\n理由: " +
                       Wide(result->error));
                if (listed) GoTo(0);
                return 0;
            }
            normalVersionId = result->id;
            normalRead = NormalRead::Known;
            if (listed) { RefreshVersions(result->id); Save(); }
            else SyncVersionCards();
            Status(L"通常起動の Minecraft を " + Wide(result->id) + L" に設定し、Windowsの登録を確認しました。" +
                   (result->warning.empty() ? L"" : L"\r\n" + Wide(result->warning)) +
                   (installedGames.count(result->id) ? L"" : L"\r\n（この PC の一覧にまだ出ない場合は、Settings の自動検出を押してください。）"));
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
                normalReadNote = result ? Wide(result->error) : std::wstring(L"読み取れません");
                if (result) PushLog(L"Windowsの通常起動の版: " + normalReadNote, false, false);
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
                ReloadAvatar();
                if (result->interactive) Status(L"Discord にログインしました: " + Wide(result->status->name));
            }
            else if (result->interactive) { Refresh(); throw std::runtime_error(result->error); }
            else
            {
                // the saved key could not be checked: the account stays as drawn, except when the key has expired
                accountStatus.reset();
                if (result->error.find("期限が切れました") != std::string::npos) accountExpired = true;
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
            if (!result->error.empty()) throw std::runtime_error(result->error);
            packageRoot = result->root;
            packageLocal = result->local;
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
                            Status(L"この版はOriginX未対応のため、インストール・切替できません。");
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
                else if (state == State::Checking) { launchAfterCheck = true; Status(L"更新を確認してから起動します…"); }
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
                            else { wantedChannel = entries[i].channel; Save(); }
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
            if (settingsTab == 0 && state == State::Idle && InRect(SyncSwitchRect(), x, y))
            {
                syncNormal = !syncNormal;
                Save();
                Status(syncNormal ? L"通常起動の版も切り替えます（Launch・版の選択で Windows の版も合わせます）。"
                                  : L"通常起動の版は切り替えません（OriginX だけ選んだ版で起動します）。");
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
                Status(L"ゲームとローダーの準備が終わるまで、この画面を開いたままにしてください。");
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
        Status(L"処理できませんでした:\r\n" + Wide(error.what()) + L"\r\n初回の準備と選択したファイルを確認してください。");
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
        if (!length || length >= path.size()) throw std::runtime_error("配布フォルダーを取得できません。");
        launcherExe = fs::path(std::wstring(path.data(), length));
        bundle = launcherExe.parent_path();
        RemoveOldLauncher(launcherExe);   // left by the last self-update
        privateDirectory = KnownFolder(FOLDERID_LocalAppData) / "MyClient";
        fs::create_directories(privateDirectory);
        TrimLauncherLog();
        HANDLE single = singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\MyClient.Launcher");
        const DWORD mutexError = GetLastError();
        if (!single) throw std::runtime_error("ランチャーの起動状態を確認できません。");
        if (mutexError == ERROR_ALREADY_EXISTS)
        {
            HWND previous = FindWindowW(L"MyClientLauncherWindow", nullptr);
            if (previous) { ShowWindow(previous, SW_RESTORE); SetForegroundWindow(previous); }
            CloseHandle(single);
            singleInstance = nullptr;
            CoUninitialize();
            return 0;
        }
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
        HWND handle = CreateWindowW(type.lpszClassName, L"OriginX Client — 適用・起動", style,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, instance, nullptr);
        if (!handle) throw std::runtime_error("ランチャー画面を作成できません。");
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

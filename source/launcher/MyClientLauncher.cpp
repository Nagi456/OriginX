#ifndef NOMINMAX
#define NOMINMAX
#endif
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
#include "../BridgeChannel.h"
#include <atomic>
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
           SwitchedMessage = WM_APP + 6;
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
Json sources = Json::object();
struct UpdateResult { Catalog catalog; std::string error; };
struct PackageResult { fs::path root; std::string error; };
// Version switching: folders the user added, what was found on this PC and the public list of released versions.
Json imports = Json::array();
std::map<std::string, fs::path> installedGames;   // version id -> Minecraft.Windows.exe
std::vector<std::string> publishedVersions;
std::map<std::string, std::vector<std::string>> versionLinks;   // version id -> Microsoft's package links
std::wstring versionDbNote = L"未取得";
std::thread versionDbThread;
struct VersionDbResult { std::vector<std::string> versions; std::map<std::string, std::vector<std::string>> links; std::string error; };
// Changing the installed game (Flarial's way): Microsoft's package is downloaded and Windows installs it
// over the Store copy, so Gaming Services decrypts it with the user's own licence.
std::thread switchThread;
std::string switchingId;
std::atomic<int> switchPhase{0};   // 0 = checking, 1 = downloading, 2 = installing
std::atomic<unsigned long long> switchDone{0}, switchTotal{0};
struct SwitchResult { std::string id, error, warning; };

void ApplyClient();
void RefreshVersions(const std::string& preferred);
void CheckForUpdates();
void PollSwitchProgress();

ULONGLONG startedAt = 0;
std::vector<HWND> editable;

// ---- Reskinned UI state (sidebar + cards). Behaviour of the controls is unchanged. ----
struct LogLine { std::wstring time, text; bool error = false, live = false; };
std::vector<LogLine> history;
int page = 0;                       // 0 = Home, 1 = Settings (below Home)
int settingsTab = 0;                // 0 = General, 1 = Version, 2 = Logs
double scroll = 0, scrollFrom = 0;  // 0 = Home in view, 1 = Settings in view
ULONGLONG scrollStart = 0;
bool scrolling = false;
HFONT heroFont = nullptr, symFont = nullptr, titleFont = nullptr, greetFont = nullptr;
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
    if (!size && !value.empty()) return L"文字列の形式が不正です。";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
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
    SetWindowTextW(GetDlgItem(window, Apply), state == State::Idle ? L"Launch" : L"Working\u2026");
    Refresh();
}

void Save()
{
    sources[selected.target.id] = fs::path(Text(Game)).u8string();
    Json config{{"sources", sources}, {"selectedVersion", selected.target.id}, {"imports", imports}, {"schemaVersion", 3}};
    const auto temporary = privateDirectory / "launcher.json.tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << config.dump(2);
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), (privateDirectory / "launcher.json").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("ランチャー設定を保存できません。");
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
    hudProcess = Start(bundle / "MyClient.exe", {L"--minecraft-pid=" + std::to_wstring(pid)}, bundle);
    hudGamePid = pid;
    Busy(State::Idle);
    Status(L"DLLへの接続を確認し、HUDを起動しました。\r\nワールドに入り、G → Arrow Counter / Block OutlineをONにしてください。");
}

void LaunchGame()
{
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
    Status(L"Minecraftを直接起動しました。DLLへの接続を待っています…\r\n接続を確認するとHUDを自動で起動します。");
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
    RequireAttachRuntime(attachDir, packageRoot / "runtime.lock.json", selected.target);
    const DWORD pid = GamePidAny();
    if (pid && Ready(pid)) { StartHud(pid); return; }
    const auto backup = InstallMod(packageRoot / "mods/MyClientData", attachDir, selected.target);
    if (!backup.empty()) Status(L"以前のMODをバックアップしました:\r\n" + backup.wstring());
    GrantAppPackageAccess(attachDir);
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
        Status(L"Minecraftを通常起動しました。起動後にローダーを注入します…\r\n接続を確認するとHUDを自動で起動します。");
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
         (bundle / "PrepareRuntime.ps1").wstring(), L"-ManifestPath", (packageRoot / "runtime.lock.json").wstring(),
         L"-ExpectedGameVersion", Wide(selected.target.runtimeGame), L"-ExpectedLoaderVersion", Wide(selected.target.loader),
         L"-GameDirectory", attachDir.wstring(), L"-CacheDirectory", (privateDirectory / "cache").wstring(), L"-Attach"},
         attachDir, privateDirectory / "runtime-setup.log");
    Busy(State::Installing);
    BeginProgress();
    Status(L"ローダーを公式配布元から準備しています（本体は変更しません）。初回はインターネット接続が必要です。\r\n進行状況は「ログを開く」で確認できます。");
}

void PrepareRuntimeOrLaunch()
{
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
         (bundle / "PrepareRuntime.ps1").wstring(), L"-ManifestPath", (packageRoot / "runtime.lock.json").wstring(),
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
    RequireX64Pe(bundle / "MyClient.exe");
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
                const auto result = InjectDll(pid, attachDir / "PreLoader.dll");
                if (result.ok)
                {
                    injectedPid = pid;
                    Status(L"PreLoader.dllを注入しました。DLLへの接続を待っています…");
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
             int id = 0, DWORD style = 0, bool large = false)
{
    HWND handle = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
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
            SendDlgItemMessageW(window, Version, CB_SETCURSEL, i, 0);
            SelectVersion();
            Save();
            Status(L"Minecraft " + Wide(id) + L"を追加しました:\r\n" + game.wstring() +
                   (catalog.releases[i].available ? L"\r\nホームでこの版を選んで起動できます。" : L"\r\nこの版のMyClient DLLはまだありません（対応待ち）。"));
            Refresh();
            return;
        }
    Status(L"Minecraft " + Wide(id) + L"を追加しました:\r\n" + game.wstring() +
           L"\r\nMyClientはこの版にまだ対応していません。対応版が出ると一覧に追加されます。");
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
constexpr const char* SwitchScript = R"ps(param([string]$Mode, [string]$Package, [string]$Helper)
$ErrorActionPreference = 'Stop'
try {
    if (-not (Get-AppxPackage -Name Microsoft.GamingServices)) { Write-Output 'Gaming Services is not installed.'; exit 2 }
    $game = Get-AppxPackage -Name Microsoft.MinecraftUWP | Select-Object -First 1
    if (-not $game) { Write-Output 'Minecraft is not installed.'; exit 3 }
    Write-Output ('Installed: ' + $game.Version + ' (' + $game.SignatureKind + ')')
    if ("$($game.SignatureKind)" -ne 'Store') { exit 4 }
    if ($Mode -ne 'install') { exit 0 }
    Add-AppxPackage -Path $Package -ForceUpdateFromAnyVersion -ForceApplicationShutdown
    $game = Get-AppxPackage -Name Microsoft.MinecraftUWP | Select-Object -First 1
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
std::string LogTail(const fs::path& log)
{
    std::ifstream input(log, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    if (text.size() > 600) text = "…" + text.substr(text.size() - 600);
    return text;
}

DWORD RunSwitchScript(const fs::path& script, const std::wstring& mode, const fs::path& package, const fs::path& helper, const fs::path& log)
{
    std::vector<wchar_t> systemPath(32768);
    const UINT length = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
    if (!length || length >= systemPath.size()) throw std::runtime_error("Windowsのシステムフォルダーを取得できません。");
    const auto powershell = fs::path(std::wstring(systemPath.data(), length)) / "WindowsPowerShell/v1.0/powershell.exe";
    HANDLE process = Start(powershell, {L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
                                        script.wstring(), L"-Mode", mode, L"-Package", package.wstring(), L"-Helper", helper.wstring()},
                           script.parent_path(), log);
    if (!process) throw std::runtime_error("PowerShellを起動できません。");
    WaitForSingleObject(process, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process, &exitCode);
    CloseHandle(process);
    return exitCode;
}

void SwitchGame(const std::string& id, const std::vector<std::string>& urls, const fs::path& folder, const fs::path& helper,
                std::string& warning)
{
    const auto script = folder / "switch-game.ps1";
    switchPhase = 0;
    switch (const DWORD code = RunSwitchScript(script, L"check", {}, {}, folder / "switch-check.log"))
    {
    case 0: break;
    case 2: throw std::runtime_error("Gaming Servicesが入っていません。Microsoft Storeで「Gaming Services」を入れてから、もう一度押してください。");
    case 3: throw std::runtime_error("Minecraftが入っていません。先にXboxアプリかMicrosoft StoreからMinecraftを入れてください（購入済みのMinecraftを別の版に置き換える仕組みです）。");
    case 4: throw std::runtime_error("今のMinecraftはストア以外（LeviLauncherなど）で入れたものなので置き換えられません。XboxアプリからMinecraftを入れ直してください。");
    default: throw std::runtime_error("Minecraftの状態を確認できませんでした（" + std::to_string(code) + "）: " + LogTail(folder / "switch-check.log"));
    }
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
    const DWORD code = RunSwitchScript(script, L"install", package, helper, folder / "switch-install.log");
    if (code == 5)
        warning = "起動役（gamelaunchhelper.dll）を差し替えられませんでした。Xboxアプリから起動すると最新版への更新を求められることがあります: " +
                  LogTail(folder / "switch-install.log");
    else if (code)
        throw std::runtime_error("Windowsがインストールできませんでした（" + std::to_string(code) + "）: " + LogTail(folder / "switch-install.log"));
}

void StartSwitch(const std::string& id)
{
    if (state != State::Idle) return;
    const auto links = versionLinks.find(id);
    if (links == versionLinks.end()) throw std::runtime_error("この版のダウンロード先が公開一覧にありません。");
    if (GamePidAny()) throw std::runtime_error("Minecraftを終了してから版を変更してください。");
    bool supported = false;
    for (const auto& release : catalog.releases) if (release.target.id == id && release.available) supported = true;
    std::wstring current;
    for (const auto& [installedId, path] : installedGames)
        if (Lower(path.u8string()).find("xboxgames") != std::string::npos) current = Wide(installedId);
    const std::wstring message =
        L"Minecraft を " + Wide(id) + L" に変更します。\n\n"
        L"・Microsoft の配信サーバーから数 GB をダウンロードし、Windows（Gaming Services）がインストールします。\n"
        L"・今 Xbox アプリで入っている Minecraft" + (current.empty() ? L"" : L"（" + current + L"）") + L"は置き換えられます。版を並べて持つことはできません。\n"
        L"・ワールドは消えませんが、新しい版で開いたワールドを古い版で開くと壊れることがあります。大事なワールドはバックアップしてください。\n"
        L"・Xbox アプリや Microsoft Store の自動更新で最新版に戻ることがあります。\n" +
        (supported ? std::wstring{} : L"・MyClient はこの版にまだ対応していないので、変更後は MyClient で起動できません。\n") +
        L"\n続けますか？";
    if (MessageBoxW(window, message.c_str(), L"Minecraft の版を変更", MB_OKCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON2) != IDOK) return;
    const auto folder = privateDirectory / "cache" / "game-switch";
    fs::create_directories(folder);
    {
        std::ofstream script(folder / "switch-game.ps1", std::ios::binary | std::ios::trunc);
        script << SwitchScript;
        if (!script) throw std::runtime_error("版の変更の準備ファイルを書き込めません。");
    }
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
    Status(L"Minecraft を " + Wide(id) + L" に変更しています。終わるまでこのウィンドウを開いたままにしてください。");
    const auto urls = links->second;
    switchThread = std::thread([id, urls, folder, helper] {
        auto result = new SwitchResult{id, {}, {}};
        try { SwitchGame(id, urls, folder, helper, result->warning); }
        catch (const std::exception& error) { result->error = error.what(); }
        if (!PostMessageW(window, SwitchedMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
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
    preparation = std::thread([release] {
        auto result = new PackageResult;
        try {
            bool local = false;
            for (const auto& candidate : bundledCatalog.releases)
                if (candidate.available && candidate.target.id == release.target.id && candidate.files == release.files &&
                    candidate.revision == release.revision && candidate.target.loader == release.target.loader) local = true;
            if (!local)
                result->root = AcquirePackage(privateDirectory / "packages", release, Sha256File, DownloadHttps);
            else
            {
                // Bundled files: taken as they are in the MyClient folder (a DLL rebuilt and copied in by hand
                // works without editing catalog.json); see LocalRelease.
                const auto bundled = [release](const std::string& key) {
                    // Extra game versions would ship under versions/<id>/ (none now: only 26.51.1, at the bundle root).
                    auto source = SafeChild(bundle / "versions" / fs::u8path(release.target.id), key);
                    if (!fs::is_regular_file(source)) source = SafeChild(bundle, key);
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
    Control(L"BUTTON", L"Settings", 452, 394, 260, 50, HomeSettings, WS_TABSTOP | BS_OWNERDRAW);
    // Settings page.
    editable.push_back(Control(L"EDIT", L"", 328, 116, 516, 42, Game, WS_BORDER | ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP));
    editable.push_back(Control(L"BUTTON", L"自動検出", 856, 116, 126, 42, AutoDetect, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", L"選択…", 990, 116, 126, 42, PickGame, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", L"フォルダーを追加…", 896, 210, 220, 42, ImportGame, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", L"更新を確認", 896, 318, 220, 42, CheckUpdates, WS_TABSTOP | BS_OWNERDRAW));
    editable.push_back(Control(L"BUTTON", L"使い方・必要な部品", 896, 426, 220, 42, Guide, WS_TABSTOP | BS_OWNERDRAW));
    ShowPage();
    bundledCatalog = ParseCatalog(ReadJson(bundle / "catalog.json"));
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
        }
        else sources["26.51.1"] = config.value("game", std::string{});
    }
    if (fs::exists(privateDirectory / "versions_db.json"))
    {
        try { LoadVersionDb(privateDirectory / "versions_db.json"); }
        catch (const std::exception&) { /* Replaced by the download below. */ }
    }
    ScanInstalled();
    RefreshVersions(preferred);
    FetchVersionDb();
    if (!SetTimer(window, 1, 500, nullptr)) throw std::runtime_error("接続確認タイマーを開始できません。");
    PostMessageW(window, CheckMessage, 0, 0);
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
RECT HomeVersionChip() { return MR(28, 676, 300, 50); }
constexpr int VersionRowsShown = 12;
RECT VersionListRow(int i) { return MR(324, 156 + i * 38, 792, 34); }
RECT VersionInstallButton(int i) { return MR(1004, 161 + i * 38, 100, 24); }
bool ShowsInstall(const VersionRow& row) { return row.published && row.downloadable && !row.installed; }

std::vector<VersionRow> CurrentVersionRows()
{
    std::vector<CatalogEntry> entries;
    for (const auto& release : catalog.releases) entries.push_back({release.target.id, release.available});
    std::vector<std::string> installed;
    for (const auto& [id, path] : installedGames) installed.push_back(id);
    std::vector<std::string> downloadable;
    for (const auto& [id, urls] : versionLinks) downloadable.push_back(id);
    auto rows = VersionRows(publishedVersions, entries, installed, downloadable);
    if (rows.size() > VersionRowsShown) rows.resize(VersionRowsShown);
    return rows;
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
    else if (!selected.available) sub = L"Minecraft " + id + L" is waiting for MyClient support.";
    else if (!found) sub = L"Minecraft " + id + L" was not found. Start the game once, or set it in Settings.";
    Txt(dc, title, MR(0, 196, 1164, 56), greetFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    Txt(dc, sub, MR(160, 254, 844, 30), font, RGB(196, 208, 228), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Under the buttons: the latest message (errors in red) and the progress bar.
    const std::wstring status = Text(StatusText);
    if (!status.empty())
    {
        size_t cut = status.find(L"\r\n");   // at most two lines
        if (cut != std::wstring::npos) cut = status.find(L"\r\n", cut + 2);
        Txt(dc, status.substr(0, cut), MR(232, 460, 700, 44), smallFont, statusError ? ColError : RGB(168, 182, 206),
            DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (progressPercent >= 0)
    {
        const RECT track = MR(402, 512, 300, 10);
        Box(dc, track, 5, ColSide, ColBorder);
        RECT fill = track;
        fill.right = fill.left + std::max<LONG>(Sc(10), (track.right - track.left) * progressPercent / 100);
        if (progressPercent > 0) Box(dc, fill, 5, ColAccent, ColAccent, false);
        Txt(dc, std::to_wstring(progressPercent) + L"%", MR(712, 504, 60, 26), smallFont, RGB(255, 255, 255));
    }

    // Bottom left: the chosen version (opens Versions). Bottom right: launcher version and readiness.
    const RECT chip = HomeVersionChip();
    Box(dc, RECT{chip.left, chip.top + Sc(8), chip.left + Sc(110), chip.bottom - Sc(8)}, 17, ColSide, ColBorder);
    Txt(dc, id, RECT{chip.left, chip.top, chip.left + Sc(110), chip.bottom}, smallFont, ColText,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const std::wstring where = !selected.available ? L"waiting for DLL" : installedGames.count(selected.target.id) ? L"on this PC" : L"not on this PC";
    Txt(dc, where + L"  ›", RECT{chip.left + Sc(122), chip.top, chip.right, chip.bottom}, smallFont, ColMuted,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const wchar_t* launcherVersion = L"v0.5.0";
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
    Txt(dc, L"Minecraft versions", MR(328, 48, 500, 30), boldFont, ColText);
    Txt(dc, L"公開版の一覧（LeviLauncherと同じ公開データ・" + versionDbNote + L"）と、MyClientの対応・このPCにある版。"
            L"遊べるのはDLLがあり、このPCにある版です。対応版の行を押すと選んでホームに戻ります。",
        MR(328, 80, 790, 40), smallFont, ColMuted, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    const int columns[4] = {344, 500, 660, 880};
    const wchar_t* heads[4] = {L"Version", L"Released", L"MyClient", L"This PC"};
    for (int c = 0; c < 4; c++) Txt(dc, heads[c], MR(columns[c], 130, 200, 22), smallFont, ColMuted);
    const auto rows = CurrentVersionRows();
    if (rows.empty()) Txt(dc, L"一覧を取得しています…", VersionListRow(0), font, ColMuted);
    for (size_t n = 0; n < rows.size(); n++)
    {
        const auto& row = rows[n];
        const RECT r = VersionListRow(static_cast<int>(n));
        const bool chosen = row.inCatalog && row.id == selected.target.id;
        Box(dc, r, 10, chosen ? ColTile : ColSide, chosen ? ColAccentHi : ColLine);
        const auto cell = [&](int c, int width) { return RECT{Sc(columns[c]), r.top, Sc(columns[c] + width), r.bottom}; };
        const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
        Txt(dc, Wide(row.id), cell(0, 150), titleFont, row.supported ? ColText : ColMuted, flags);
        Txt(dc, row.published ? L"Released" : L"—", cell(1, 150), smallFont, row.published ? ColText : ColMuted, flags);
        Txt(dc, row.supported ? L"DLL ready" : row.inCatalog ? L"Waiting for DLL" : L"Not supported yet", cell(2, 210), smallFont,
            row.supported ? ColGreen : ColMuted, flags);
        if (row.id == switchingId)
            Txt(dc, L"Installing\u2026 " + std::to_wstring(std::max(progressPercent, 0)) + L"%", cell(3, 220), smallFont, ColAccentHi, flags);
        else
        {
            Txt(dc, row.installed ? L"Installed" : L"—", cell(3, 110), smallFont, row.installed ? ColGreen : ColMuted, flags);
            if (ShowsInstall(row))
            {
                const RECT button = VersionInstallButton(static_cast<int>(n));
                const bool idle = state == State::Idle;
                Box(dc, button, 8, idle ? ColAccent : ColDisabled, idle ? ColAccent : ColDisabled, false);
                Txt(dc, L"Install", button, smallFont, idle ? RGB(255, 255, 255) : ColDisabledText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
    }
    Txt(dc, L"Install を押すと、Microsoft の配信からその版を入れて今の Minecraft と置き換えます（Gaming Services とストア版の Minecraft が必要）。"
            L"LeviLauncherなどで入れた版も自動で探します。MyClientが対応していない版はDLLが出るまで起動できません。",
        MR(328, 628, 790, 60), smallFont, ColMuted, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
}

void Divider(HDC dc, int y)
{
    RECT line = MR(328, y, 788, 1);
    line.bottom = line.top + 1;
    HBRUSH brush = CreateSolidBrush(ColLine);
    FillRect(dc, &line, brush);
    DeleteObject(brush);
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
    Txt(dc, L"DLLは版ごとに保存し、公開された更新を起動時に確認します。本体の版は Version タブの Install で変えられます。ワールドの版ごとの分離は未対応です。"
            L"対応待ちの版は起動できません。配布先が未公開・オフラインの場合は保存済み一覧を使います。",
        MR(328, 522, 788, 64), smallFont, ColMuted, wrap);
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
        case UpdatedMessage:
        {
            std::unique_ptr<UpdateResult> result(reinterpret_cast<UpdateResult*>(lParam));
            if (preparation.joinable()) preparation.join();
            Busy(State::Idle);
            if (result->error.empty()) catalog = std::move(result->catalog);
            RefreshVersions(selected.target.id);
            Status(result->error.empty() ? L"更新一覧を確認しました。版を選んで起動してください。" :
                L"保存済み一覧を使用します。" + Wide(result->error));
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
            Refresh();
            return 0;
        }
        case SwitchedMessage:
        {
            std::unique_ptr<SwitchResult> result(reinterpret_cast<SwitchResult*>(lParam));
            if (switchThread.joinable()) switchThread.join();
            Busy(State::Idle);
            EndProgress();
            switchingId.clear();
            ScanInstalled();
            if (!result->error.empty()) throw std::runtime_error(result->error);
            bool listed = false;
            for (const auto& release : catalog.releases) if (release.target.id == result->id) listed = true;
            if (listed) { RefreshVersions(result->id); Save(); }
            Status(L"Minecraft を " + Wide(result->id) + L" に変更しました。" +
                   (result->warning.empty() ? L"" : L"\r\n" + Wide(result->warning)) +
                   (installedGames.count(result->id) ? L"" : L"\r\n（この PC の一覧にまだ出ない場合は、Settings の自動検出を押してください。）"));
            return 0;
        }
        case PackageMessage:
        {
            std::unique_ptr<PackageResult> result(reinterpret_cast<PackageResult*>(lParam));
            if (preparation.joinable()) preparation.join();
            Busy(State::Idle);
            if (!result->error.empty()) throw std::runtime_error(result->error);
            packageRoot = result->root;
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
            if (LOWORD(wParam) == Version && HIWORD(wParam) == CBN_SELCHANGE && state == State::Idle)
            { Save(); SelectVersion(); Save(); return 0; }
            if (HIWORD(wParam) != BN_CLICKED) return 0;
            switch (LOWORD(wParam))
            {
            case PickGame: if (state == State::Idle) Pick(Game); break;
            case AutoDetect: if (state == State::Idle && FindInstalledGame(false)) Save(); break;
            case ImportGame: if (state == State::Idle) ImportFolder(); break;
            case Apply: if (state == State::Idle) FetchAndApply(); break;
            case CheckUpdates: if (state == State::Idle) CheckForUpdates(); break;
            case Guide: ShellExecuteW(handle, L"open", (bundle / "README-ja.md").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
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
            break;
        }
        case WM_ERASEBKGND: return 1;   // everything is drawn from the back buffer
        case WM_LBUTTONDOWN:
        {
            const int x = static_cast<short>(LOWORD(lParam)), y = static_cast<short>(HIWORD(lParam));
            if (scrolling) return 0;
            if (page == 0)
            {
                if (InRect(HomeLogsLink(), x, y)) { settingsTab = 2; GoTo(1); }
                else if (InRect(HomeVersionsLink(), x, y) || InRect(HomeVersionChip(), x, y)) { settingsTab = 1; GoTo(1); }
                return 0;
            }
            if (InRect(ReturnRect(), x, y)) { GoTo(0); return 0; }
            for (int i = 0; i < 3; i++)
                if (InRect(TabRect(i), x, y)) { settingsTab = i; ShowPage(); return 0; }
            if (settingsTab == 2 && InRect(LogFileLink(), x, y)) OpenLogs();
            else if (settingsTab == 1 && state == State::Idle)
            {
                // A version MyClient lists is chosen, then Home slides back.
                const auto rows = CurrentVersionRows();
                for (size_t n = 0; n < rows.size(); n++)
                    if (ShowsInstall(rows[n]) && InRect(VersionInstallButton(static_cast<int>(n)), x, y)) { StartSwitch(rows[n].id); return 0; }
                for (size_t n = 0; n < rows.size(); n++)
                    if (rows[n].inCatalog && InRect(VersionListRow(static_cast<int>(n)), x, y))
                    {
                        SendDlgItemMessageW(window, Version, CB_SETCURSEL, rows[n].catalogIndex, 0);
                        Save(); SelectVersion(); Save();
                        GoTo(0);
                        break;
                    }
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
            KillTimer(handle, 1);
            KillTimer(handle, SlideTimer);
            if (versionDbThread.joinable()) versionDbThread.detach();
            if (switchThread.joinable()) switchThread.detach();
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
        bundle = fs::path(std::wstring(path.data(), length)).parent_path();
        privateDirectory = KnownFolder(FOLDERID_LocalAppData) / "MyClient";
        fs::create_directories(privateDirectory);
        HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\MyClient.Launcher");
        const DWORD mutexError = GetLastError();
        if (!single) throw std::runtime_error("ランチャーの起動状態を確認できません。");
        if (mutexError == ERROR_ALREADY_EXISTS)
        {
            HWND previous = FindWindowW(L"MyClientLauncherWindow", nullptr);
            if (previous) { ShowWindow(previous, SW_RESTORE); SetForegroundWindow(previous); }
            CloseHandle(single);
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
        DeleteObject(symFont);
        DeleteObject(titleFont);
        if (logoIcon) DestroyIcon(logoIcon);
        CloseHandle(single);
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

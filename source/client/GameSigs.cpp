// The start-up self check and its logs (2026-11-11). See GameSigs.h for the tables, docs/version-update.md for what
// to do with the logs.
//
//   %LOCALAPPDATA%\MyClient\logs\startup.log   one line the moment the DLL is in the game (DllMain), then the game's
//                                              version, then whether MyClient starts. Written and flushed line by
//                                              line: a crash right after still leaves everything before it.
//   %LOCALAPPDATA%\MyClient\logs\sigcheck.log  every signature: OK / NG, fatal or not, address, pattern, purpose.
//
// A signature that is NG on a supported build is taken out of the address table (GameRva.cpp), so only the feature
// that needs it is missing. A fatal one NG, or a game build that is not in sigs.json: nothing is hooked at all, the
// reason goes to the log and a message box, and the game goes on without MyClient.
#include "SelfCheck.h"
#include "GameSigs.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

namespace MyClientGame {
void RefuseRvaLabel(const char* label);   // GameRva.cpp
}

namespace MyClientSigs {
namespace {
std::mutex mutex;
bool startupOpened = false;

std::wstring LogsFolder()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    std::wstring folder = std::wstring(buffer) + L"\\MyClient";
    CreateDirectoryW(folder.c_str(), nullptr);
    folder += L"\\logs";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

// Appends (or, the first time in this process, starts) a log file; every write reaches the disk before returning.
void WriteFileLine(const wchar_t* name, const std::string& text, bool fresh)
{
    const std::wstring folder = LogsFolder();
    if (folder.empty()) return;
    const std::wstring path = folder + L"\\" + name;
    if (fresh)
    {
        const std::wstring previous = folder + L"\\" + std::wstring(name).replace(std::wstring(name).find(L".log"), 4, L".previous.log");
        MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", now.wYear, now.wMonth, now.wDay, now.wHour,
                  now.wMinute, now.wSecond, now.wMilliseconds);
    const std::string line = stamp + text + "\r\n";
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}

std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

struct Exe { const uint8_t* base = nullptr; uint32_t timestamp = 0, imageSize = 0; const uint8_t* code = nullptr; size_t codeSize = 0; };

Exe RunningExe()
{
    Exe e;
    const auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return e;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return e;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return e;
    e.base = base;
    e.timestamp = nt->FileHeader.TimeDateStamp;
    e.imageSize = nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) && std::memcmp(section->Name, ".text", 5) == 0)
        {
            e.code = base + section->VirtualAddress;
            e.codeSize = section->Misc.VirtualSize;
            break;
        }
    return e;
}

// The game's own version text: MicrosoftGame.config beside the exe (Identity Version="1.26.5101.0").
std::string GameVersionText()
{
    wchar_t exe[MAX_PATH * 2]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    if (!n || n >= MAX_PATH * 2) return "unknown (no exe path)";
    std::wstring folder(exe, n);
    folder = folder.substr(0, folder.find_last_of(L"\\/"));
    for (const wchar_t* name : {L"\\MicrosoftGame.config", L"\\..\\MicrosoftGame.config"})
    {
        HANDLE file = CreateFileW((folder + name).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        char buffer[16384]{};
        DWORD got = 0;
        ReadFile(file, buffer, sizeof buffer - 1, &got, nullptr);
        CloseHandle(file);
        const std::string text(buffer, got);
        const size_t identity = text.find("<Identity");
        const size_t version = identity == std::string::npos ? std::string::npos : text.find("Version=\"", identity);
        if (version != std::string::npos)
        {
            const size_t end = text.find('"', version + 9);
            if (end != std::string::npos) return text.substr(version + 9, end - version - 9);
        }
    }
    return "unknown (no MicrosoftGame.config beside " + Narrow(std::wstring(exe, n)) + ")";
}

bool Readable(const uint8_t* at, size_t size)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!at || !VirtualQuery(at, &info, sizeof info) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return at + size <= static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize;
}

std::string Hex(uint64_t v)
{
    char text[24];
    std::snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(v));
    return text;
}

std::atomic<int> runningBuild{-2};   // -2 = not checked yet
} // namespace

void StartupLine(const std::string& text)
{
    std::lock_guard lock(mutex);
    WriteFileLine(L"startup.log", text, !startupOpened);
    startupOpened = true;
}

void SigcheckLine(const std::string& text, bool fresh) { WriteFileLine(L"sigcheck.log", text, fresh); }

// The whole check, before any hook. `scanUnknown`: on a build that is not supported, also search the newest
// patterns in the game's code (seconds) so the log says which signatures still match.
Verdict Check(const char* dllFor, bool scanUnknown)
{
    Verdict v;
    const Exe exe = RunningExe();
    v.version = GameVersionText();
    v.build = GameSigs::FindBuild(exe.timestamp, exe.imageSize);
    // A DLL is built against one game version's SDK (class layouts): another supported build is still the wrong one.
    const bool sameVersion = v.build >= 0 && dllFor && std::strncmp(GameSigs::kBuilds[v.build].version, dllFor, std::strlen(dllFor)) == 0;
    runningBuild.store(v.build);
    StartupLine("game version " + v.version + ", exe timestamp " + Hex(exe.timestamp) + ", image size " + Hex(exe.imageSize) +
                "; this DLL is for Minecraft " + (dllFor ? dllFor : "?"));
    SigcheckLine("sigcheck: game " + v.version + " (exe " + Hex(exe.timestamp) + "/" + Hex(exe.imageSize) + "), DLL for " +
                 (dllFor ? dllFor : "?") + ", build in sigs.json: " +
                 (v.build >= 0 ? std::string(GameSigs::kBuilds[v.build].version) : std::string("none (not supported)")), true);
    SigcheckLine("columns: result <tab> name <tab> FATAL/- <tab> address <tab> pattern <tab> purpose / NG: what stops working", false);
    std::vector<int> pattern;
    if (v.build >= 0)
    {
        for (const GameSigs::Sig& s : GameSigs::kSigs)
        {
            const GameSigs::At* at = GameSigs::AtFor(s.index, v.build);
            if (!at) { ++v.notUsed; SigcheckLine(GameSigs::ReportLine("N/A", s, "not used on this build", ""), false); continue; }
            const uint8_t* address = exe.base + at->rva;
            bool hooked = false;
            const bool ok = GameSigs::ParsePattern(at->pattern, pattern) && Readable(address, pattern.size()) &&
                            GameSigs::MatchAtMaybeHooked(address, pattern.size(), pattern, hooked);
            (ok ? v.ok : v.ng)++;
            if (!ok && s.fatal) ++v.fatalNg;
            if (!ok) MyClientGame::RefuseRvaLabel(s.name);   // its feature stays off; nothing else changes
            SigcheckLine(GameSigs::ReportLine(ok ? "OK" : "NG", s, "rva " + Hex(at->rva) + (hooked ? " (already hooked by another module)" : ""),
                                              at->pattern), false);
        }
        v.start = v.fatalNg == 0 && sameVersion;
        v.why = !sameVersion ? std::string("this DLL is built for Minecraft ") + (dllFor ? dllFor : "?") + ", the game is " + GameSigs::kBuilds[v.build].version
              : v.start ? "" : "a fatal signature does not match (see sigcheck.log)";
    }
    else
    {
        v.why = "this Minecraft build is not in tools/sigs.json (supported_versions)";
        if (scanUnknown && exe.code)
            for (const GameSigs::Sig& s : GameSigs::kSigs)
            {
                const GameSigs::At* at = GameSigs::Newest(s.index);
                if (!at || !GameSigs::ParsePattern(at->pattern, pattern)) continue;
                const auto hits = GameSigs::Scan(exe.code, exe.codeSize, pattern, 2);
                const std::string where = hits.empty() ? "not found" : hits.size() == 1
                    ? "rva " + Hex(static_cast<uint64_t>(exe.code - exe.base) + hits[0]) : "found more than once";
                (hits.size() == 1 ? v.ok : v.ng)++;
                if (hits.size() != 1 && s.fatal) ++v.fatalNg;
                SigcheckLine(GameSigs::ReportLine(hits.size() == 1 ? "OK" : hits.empty() ? "NG" : "MULTI", s, where, at->pattern) +
                             " (pattern of " + GameSigs::kBuilds[at->build].version + ")", false);
            }
    }
    SigcheckLine("summary: " + std::to_string(v.ok) + " OK, " + std::to_string(v.ng) + " NG (" + std::to_string(v.fatalNg) +
                 " fatal), " + std::to_string(v.notUsed) + " not used on this build; MyClient " +
                 (v.start ? "starts" : "does NOT start: " + v.why), false);
    StartupLine(v.start ? "self check passed (" + std::to_string(v.ng) + " non-fatal NG: those features stay off); starting"
                        : "NOT starting, nothing hooked: " + v.why);
    return v;
}

int RunningBuild() { return runningBuild.load(); }

// A game-version-dependent number from sigs.json's "offsets" for the running build; `fallback` when it has none.
uint32_t OffsetOr(const char* name, uint32_t fallback)
{
    uint32_t value = 0;
    const int build = runningBuild.load();
    return build >= 0 && GameSigs::OffsetFor(name, build, value) ? value : fallback;
}

// The reason MyClient did not start, on screen too (its own thread: the game does not wait for the button).
void ShowNotStarted(const std::string& version, const std::string& why)
{
    auto* text = new std::wstring(L"OriginX はこの Minecraft の版では動きません（何も変更していません）。\n\n版: ");
    const int n = MultiByteToWideChar(CP_UTF8, 0, (version + "\n理由: " + why).c_str(), -1, nullptr, 0);
    std::wstring tail(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, (version + "\n理由: " + why).c_str(), -1, tail.data(), n);
    *text += tail + L"\n\n詳しくは %LOCALAPPDATA%\\MyClient\\logs\\startup.log と sigcheck.log。ランチャーで対応している版を選んでください。";
    HANDLE thread = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        std::unique_ptr<std::wstring> message(static_cast<std::wstring*>(p));
        MessageBoxW(nullptr, message->c_str(), L"OriginX", MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
        return 0;
    }, text, 0, nullptr);
    if (thread) CloseHandle(thread); else delete text;
}
} // namespace MyClientSigs

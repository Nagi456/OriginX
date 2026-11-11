#pragma once
// Starting a release that runs without LeviLamina (catalog launcherApi 2; client/Standalone.cpp is the DLL's side).
//
// LeviLamina needed the game's exe edited (PeEditor adds PreLoader.dll to its imports) and its own files beside
// it. Here nothing is added to the game: the launcher starts Minecraft.Windows.exe suspended, loads
// MyClientData.dll into it with a LoadLibraryW thread, waits until the DLL says its load step is done, and only
// then lets the game's own start-up run. The DLL checks the game build itself and does nothing on another one.
#include <windows.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "PeImports.h"

namespace MyClientLauncher {
namespace fs = std::filesystem;

// The DLL of a standalone release: an x64 PE that imports neither LeviLamina nor PreLoader.
inline void RequireStandaloneDll(const fs::path& dll)
{
    const auto imports = ImportedDlls(dll);
    if (imports.count("levilamina.dll") || imports.count("preloader.dll"))
        throw std::runtime_error("このDLLはLeviLaminaが必要な版です（単独版ではありません）。");
}

// The managed copy's exe as Microsoft shipped it. A copy prepared for LeviLamina has PreLoader.dll in the exe's
// imports and the untouched file beside it (MyClient-original.exe, Install.h): that file is put back, and the
// LeviLamina receipt is removed so a LeviLamina release prepares the copy again when it is chosen.
inline void UseOriginalExe(const fs::path& directory)
{
    const auto game = directory / "Minecraft.Windows.exe";
    const auto imports = ImportedDlls(game);
    if (!imports.count("preloader.dll") && !imports.count("levilauncher.dll")) return;
    const auto original = directory / "MyClient-original.exe";
    if (!fs::is_regular_file(original)) throw std::runtime_error("元のゲーム本体（MyClient-original.exe）が見つかりません。版を準備し直してください。");
    const auto originalImports = ImportedDlls(original);
    if (originalImports.count("preloader.dll") || originalImports.count("levilauncher.dll"))
        throw std::runtime_error("保存した元のゲーム本体が変更されています。公式インストール元から準備し直してください。");
    std::error_code error;
    fs::remove(directory / "MyClient-runtime.json", error);   // the LeviLamina preparation is no longer complete
    const auto temporary = directory / "Minecraft.Windows.exe.originx-new";
    fs::copy_file(original, temporary, fs::copy_options::overwrite_existing, error);
    if (error) throw std::runtime_error("元のゲーム本体を戻せません: " + error.message());
    if (!MoveFileExW(temporary.c_str(), game.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD code = GetLastError();
        fs::remove(temporary, error);
        throw std::runtime_error("元のゲーム本体を戻せません（Windowsエラー " + std::to_string(code) + "）。Minecraftを終了してからもう一度試してください。");
    }
}

struct StandaloneStart { HANDLE process = nullptr; DWORD pid = 0; bool loadedSignal = false; };

// inject(pid, dll) is the launcher's LoadLibraryW thread (InjectDll); it returns an empty text when the DLL loaded.
template <class Inject>
StandaloneStart StartSuspendedWithDll(const fs::path& exe, const fs::path& directory, const fs::path& dll, Inject inject)
{
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &process))
        throw std::runtime_error("Minecraftを起動できません。Windowsエラー: " + std::to_string(GetLastError()));
    struct Guard {
        PROCESS_INFORMATION& p; bool keep = false;
        ~Guard() { if (!keep) { TerminateProcess(p.hProcess, 1); CloseHandle(p.hProcess); } CloseHandle(p.hThread); }
    } guard{process};
    // Still suspended: the DLL's start thread hooks what it must before any of the game's own code has run.
    const std::string problem = inject(process.dwProcessId, dll);
    if (!problem.empty()) throw std::runtime_error("OriginXのDLLをゲームに読み込めませんでした: " + problem);
    StandaloneStart result{process.hProcess, process.dwProcessId, false};
    const std::wstring name = L"Local\\MyClientStandaloneLoaded-" + std::to_wstring(process.dwProcessId);
    for (int n = 0; n < 150 && !result.loadedSignal; ++n)   // up to 15 s; the DLL makes the event when its load step ends
    {
        if (HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name.c_str()))
        {
            result.loadedSignal = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
            CloseHandle(event);
        }
        if (!result.loadedSignal)
        {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) throw std::runtime_error("Minecraftが起動の途中で終了しました。");
            Sleep(100);
        }
    }
    // Without the signal the game is started all the same (the DLL refused this game build and wrote why to its
    // log, or it is slow): the caller reports it, and the connection wait that follows decides.
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1))
        throw std::runtime_error("Minecraftの起動を再開できません。Windowsエラー: " + std::to_string(GetLastError()));
    guard.keep = true;
    return result;
}
} // namespace MyClientLauncher

#pragma once
// Best-effort unhandled-exception dump. No game objects, logger locks or symbol lookup in the filter.
// This DLL is process-lifetime (no unload entry point). Other handlers may replace this filter;
// fail-fast / process termination and a damaged process can prevent a dump.
#include <windows.h>
#include <dbghelp.h>
#include <cwchar>
#include <cstdio>
#include <atomic>

namespace MyClientCrash {
using DumpFn = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                             PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                             PMINIDUMP_CALLBACK_INFORMATION);
inline DumpFn writeDump = nullptr;
inline LPTOP_LEVEL_EXCEPTION_FILTER previous = nullptr;
inline wchar_t folder[MAX_PATH]{};
inline volatile LONG writing = 0, attempts = 0, fatalAttempts = 0;
inline std::atomic<bool> reloadActive{false};
inline PVOID vectoredHandler = nullptr;
// Labels must be string literals (valid for the process lifetime).
static_assert(std::atomic<const char*>::is_always_lock_free);
inline std::atomic<const char*> reloadStage{"idle"};
inline void Stage(const char* label) { reloadStage.store(label, std::memory_order_relaxed); }

inline void Capture(EXCEPTION_POINTERS* exception, const char* kind)
{
    if (kind && exception && exception->ExceptionRecord && writeDump &&
        InterlockedCompareExchange(&writing, 1, 0) == 0) {
        const bool fatal = kind[0] == 'u';
        const LONG slot = InterlockedIncrement(fatal ? &fatalAttempts : &attempts);
        const LONG attempt = fatal ? slot + 4 : slot;
        if (slot > (fatal ? 1 : 4)) { InterlockedExchange(&writing, 0); return; }
        SYSTEMTIME time{};
        GetSystemTime(&time);
        wchar_t path[MAX_PATH + 100]{};
        std::swprintf(path, sizeof(path) / sizeof(*path),
                      L"%ls\\crash-%04u%02u%02u-%02u%02u%02u-%03u-p%lu-t%lu-a%ld.dmp", folder,
                      time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                      time.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), attempt);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
            // Normal dump: exception, module list and thread stacks; no full memory/heap dump.
            const BOOL ok = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal,
                                      &info, nullptr, nullptr);
            const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            FlushFileBuffers(file);
            CloseHandle(file);
            // Keep a small independent result file even when the normal logger is the crashing thread.
            wchar_t resultPath[MAX_PATH + 110]{};
            std::swprintf(resultPath, sizeof(resultPath) / sizeof(*resultPath), L"%ls.txt", path);
            HANDLE result = CreateFileW(resultPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
            if (result != INVALID_HANDLE_VALUE) {
                char line[380]{};
                const int size = std::snprintf(line, sizeof(line),
                    "kind=%s minidump_ok=%d win32_error=%lu exception=0x%08lx address=%p thread=%lu last_reload_stage=%s\r\n",
                    kind, int(ok != FALSE), error, exception->ExceptionRecord->ExceptionCode,
                    exception->ExceptionRecord->ExceptionAddress, GetCurrentThreadId(),
                    reloadStage.load(std::memory_order_relaxed));
                DWORD written = 0;
                if (size > 0 && size < int(sizeof(line))) WriteFile(result, line, DWORD(size), &written, nullptr);
                FlushFileBuffers(result);
                CloseHandle(result);
            }
        }
        InterlockedExchange(&writing, 0);
    }
}
inline LONG WINAPI ObserveReload(EXCEPTION_POINTERS* exception)
{
    // VEH is FIRST chance even with First=0. These dumps are candidates, not proof of a fatal exception.
    if (reloadActive.load(std::memory_order_relaxed) && exception && exception->ExceptionRecord &&
        exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        Capture(exception, "first-chance-reload-av");
    return EXCEPTION_CONTINUE_SEARCH;
}
inline LONG WINAPI Filter(EXCEPTION_POINTERS* exception)
{
    Capture(exception, "unhandled");
    // Observe only. Do not recover, swallow the exception or bypass an existing crash handler.
    return previous && previous != &Filter ? previous(exception) : EXCEPTION_CONTINUE_SEARCH;
}

inline bool Install()
{
    if (writeDump) return true;
    wchar_t local[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!length || length >= MAX_PATH || length + 20 >= MAX_PATH) return false;
    wchar_t base[MAX_PATH]{};
    std::swprintf(base, MAX_PATH, L"%ls\\MyClient", local);
    if (!CreateDirectoryW(base, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    std::swprintf(folder, MAX_PATH, L"%ls\\logs", base);
    if (!CreateDirectoryW(folder, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    // Preload outside the exception handler. Keep this module for the process lifetime.
    HMODULE library = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return false;
    writeDump = reinterpret_cast<DumpFn>(GetProcAddress(library, "MiniDumpWriteDump"));
    if (!writeDump) { FreeLibrary(library); return false; }
    previous = SetUnhandledExceptionFilter(&Filter);
    vectoredHandler = AddVectoredExceptionHandler(0, &ObserveReload);
    return true;
}
} // namespace MyClientCrash

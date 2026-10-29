#define _MINAPPMODEL_H_
#include <windows.h>
#include <appmodel.h>
#include <winternl.h>
#include <wtsapi32.h>

/*
    - When using -O3, GCC really wants to insert memset() calls.
    - We will just provide a wrapped implementation of memset instead.
*/

PVOID  __wrap_memset(PVOID dst, BYTE data, SIZE_T count)
{
    __stosb(dst, data, count);
    return dst;
}


/*
    - MinGW doesn't declare this symbol in its headers.
    - So declare it manually for use in our dynamic link library.
*/

BOOL WTSEnumerateProcessesExW(HANDLE hServer, DWORD *pLevel, DWORD SessionId, LPWSTR *ppProcessInfo, DWORD *pCount);

/*
    - This symbol is exported and called by Game Launch Helper to launch the game.
    - We don't know about the symbol declaration so leave it as a stub.
*/

__declspec(dllexport) VOID GameLaunch(VOID)
{
    Sleep(INFINITE);
}

/*
    - To retain compatibility, emulate the PC Bootstrapper.
    - We utilize enumeration to avoid "marking" anything.
*/

DWORD ThreadProc(PVOID parameter)
{
    WCHAR pfn[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
    if (GetCurrentPackageFamilyName(&(UINT){ARRAYSIZE(pfn)}, pfn))
        goto _;

    WCHAR path[MAX_PATH] = {};
    if (GetCurrentPackagePath(&(UINT){MAX_PATH}, path) || !SetCurrentDirectoryW(path))
        goto _;

    HANDLE mutex = CreateMutexW(NULL, FALSE, pfn);
    if (!mutex || GetLastError())
    {
        CloseHandle(mutex);
        goto _;
    }

    DWORD count = {};
    HANDLE process = {};
    WTS_PROCESS_INFOW *items = {};

    /*
        - Try and look for an existing instance of the game.
        - We only enumerate processes launched by the current user.
    */

    if (WTSEnumerateProcessesExW(WTS_CURRENT_SERVER, &(DWORD){}, WTS_CURRENT_SESSION, (PWSTR *)&items, &count))
    {
        for (DWORD index = {}; index < count; index++)
        {
            WTS_PROCESS_INFOW item = items[index];

            if (CompareStringOrdinal(L"Minecraft.Windows.exe", -1, item.pProcessName, -1, TRUE) != CSTR_EQUAL)
                continue;

            WCHAR buffer[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
            process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.ProcessId);

            if (!GetPackageFamilyName(process, &(UINT){ARRAYSIZE(buffer)}, buffer) &&
                CompareStringOrdinal(pfn, -1, buffer, -1, TRUE) == CSTR_EQUAL)
                break;

            CloseHandle(process);
        }
        WTSFreeMemoryExW(WTSTypeProcessInfoLevel0, items, count);
    }

    /*
        - If we found an existing instance of the game, wait for it to initialize.
        - If initialization fails, assume the game didn't start & launch it.
    */

    if (WaitForInputIdle(process, INFINITE))
    {
        PRTL_USER_PROCESS_PARAMETERS params = NtCurrentTeb()->ProcessEnvironmentBlock->ProcessParameters;
        PWSTR args = params->CommandLine.Buffer + lstrlenW(params->ImagePathName.Buffer) + 2;

        PROCESS_INFORMATION info = {};
        CreateProcessW(L"Minecraft.Windows.exe", args, NULL, NULL, FALSE, 0, 0, NULL, &(STARTUPINFOW){}, &info);
        CloseHandle(info.hThread);

        WaitForInputIdle(info.hProcess, INFINITE);
        CloseHandle(info.hProcess);
    }
    CloseHandle(process);

    /*
        - Push the game's window into the foreground if possible.
        - Using "FindWindowEx" to enumerate windows is objectively faster than "EnumWindows".
    */

    HWND wnd = {};
    while ((wnd = FindWindowExW(NULL, wnd, L"Bedrock", NULL)))
    {
        DWORD processId = {};
        GetWindowThreadProcessId(wnd, &processId);

        WCHAR buffer[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

        if (!GetPackageFamilyName(process, &(UINT){ARRAYSIZE(buffer)}, buffer) &&
            CompareStringOrdinal(pfn, -1, buffer, -1, TRUE) == CSTR_EQUAL)
            SwitchToThisWindow(wnd, TRUE);

        CloseHandle(process);
    }

    CloseHandle(mutex);
_:
    return TerminateProcess(GetCurrentProcess(), 0);
}

BOOL DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        QueueUserWorkItem(ThreadProc, NULL, WT_EXECUTEDEFAULT);
    }
    return TRUE;
}
#include "BridgeChannel.h"

namespace {
SRWLOCK apiLock = SRWLOCK_INIT;
MyClientIpc::Channel channel;

struct ApiGuard {
    ApiGuard() { AcquireSRWLockExclusive(&apiLock); }
    ~ApiGuard() { ReleaseSRWLockExclusive(&apiLock); }
};

void ResetSharedData()
{
    *channel.Data() = {};
    channel.Data()->magic = MyClientIpc::Magic;
    channel.Data()->abiVersion = MYCLIENT_ABI_VERSION;
    channel.Data()->size = sizeof(MyClientIpc::SharedData);
    channel.Data()->processId = GetCurrentProcessId();
}
}

BOOL WINAPI MyClientBridgeInitialize()
{
    ApiGuard guard;
    if (channel.Data()) return TRUE;
    if (!channel.Open(GetCurrentProcessId(), true)) return FALSE;

    const DWORD result = channel.Lock(20);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
    {
        const DWORD error = result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        channel.Close();
        SetLastError(error);
        return FALSE;
    }
    ResetSharedData();
    channel.Unlock();
    return TRUE;
}

BOOL WINAPI MyClientBridgePublish(const MyClientFrame* frame)
{
    if (!frame || !IsValidFrame(*frame))
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    ApiGuard guard;
    if (!channel.Data())
    {
        SetLastError(ERROR_NOT_READY);
        return FALSE;
    }
    const DWORD result = channel.Lock(20);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
    {
        if (result == WAIT_TIMEOUT) SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    if (result == WAIT_ABANDONED) ResetSharedData();
    // Commit marker prevents partial frames being read after writer failure
    // and an abandoned mutex. Interlocked stores cannot be optimized away.
    InterlockedExchange(&channel.Data()->hasFrame, 0);
    channel.Data()->frame = *frame;
    channel.Data()->publishedAt = GetTickCount64();
    InterlockedExchange(&channel.Data()->hasFrame, 1);
    channel.Unlock();
    return TRUE;
}

void WINAPI MyClientBridgeShutdown()
{
    ApiGuard guard;
    if (!channel.Data()) return;
    const DWORD result = channel.Lock(20);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED)
    {
        ResetSharedData();
        channel.Unlock();
    }
    // If another process holds the lock, readers expire the last frame in 2s.
    channel.Close();
}

#if MYCLIENT_STANDALONE
namespace MyClientStandalone { void Attach(); }   // client/Standalone.cpp
#endif
#ifdef MYCLIENT_GAME_VERSION   // MyClientData.dll (the game side): logs/startup.log starts the moment it is loaded
#include "client/SelfCheck.h"
#endif

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
#ifdef MYCLIENT_GAME_VERSION
    if (reason == DLL_PROCESS_ATTACH)
    {
        // Before anything else (docs/version-update.md: no line here = the DLL was never loaded).
        wchar_t exe[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        char path[MAX_PATH * 3]{};
        WideCharToMultiByte(CP_UTF8, 0, exe, static_cast<int>(n), path, sizeof path - 1, nullptr, nullptr);
        MyClientSigs::StartupLine(std::string("injected: MyClientData.dll (built for Minecraft " MYCLIENT_GAME_VERSION
                                              ") loaded into ") + path + " pid " + std::to_string(GetCurrentProcessId()));
    }
#endif
    // The game adapter must initialize outside the Windows loader lock.
    // Loading this bridge alone does NOT hook or read Minecraft.
#if MYCLIENT_STANDALONE
    // Without LeviLamina nobody calls into the DLL after it is loaded: start MyClient's own thread (it does the
    // work, after DllMain has returned).
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(module); MyClientStandalone::Attach(); }
#else
    (void)module; (void)reason;
#endif
    return TRUE;
}

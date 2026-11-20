// See HangWatch.h. Each thread is paused only while its registers are read (GetThreadContext takes no user-mode lock);
// its stack is walked after it runs again: walking a paused thread can wait for a loader lock it holds (review,
// 2026-11-11). A hung thread does not move, so its stack still says where it waits. The walk is guarded (SEH) and
// stops on any stack pointer that is not readable or does not grow, so a broken stack cannot take the game down.
#include "HangWatch.h"
#include "Log.h"
#include "SelfCheck.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>

namespace MyClientDiag {
namespace {
constexpr int MaxFrames = 24;
constexpr int MaxThreads = 128;

struct Captured { DWORD id = 0; int frames = 0; DWORD64 pc[MaxFrames]{}; };

bool ReadableStack(DWORD64 at)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!at || (at & 7) || !VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof info) || info.State != MEM_COMMIT) return false;
    if (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    return at + 8 <= reinterpret_cast<DWORD64>(info.BaseAddress) + info.RegionSize;
}

// Return addresses from a thread's registers, by the x64 unwind data (the same walk a debugger does).
int WalkUnguarded(CONTEXT& context, DWORD64* out)
{
    int n = 0;
    while (n < MaxFrames && context.Rip)
    {
        out[n++] = context.Rip;
        const DWORD64 rsp = context.Rsp;
        if (!ReadableStack(rsp)) break;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (function)
        {
            PVOID handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
        }
        else
        {
            // a leaf function: the return address is on top of the stack
            MEMORY_BASIC_INFORMATION info{};
            if (!context.Rsp || !VirtualQuery(reinterpret_cast<void*>(context.Rsp), &info, sizeof info) || info.State != MEM_COMMIT) break;
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
        }
        if (context.Rsp <= rsp) break;   // a stack only grows towards the caller
    }
    return n;
}

int Walk(CONTEXT context, DWORD64* out)
{
    int n = 0;
    __try { n = WalkUnguarded(context, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { n = 0; }
    return n;
}

std::string Where(DWORD64 pc)
{
    HMODULE module = nullptr;
    char text[MAX_PATH + 48];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(pc), &module) && module)
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\');
        char narrow[MAX_PATH]{};
        WideCharToMultiByte(CP_UTF8, 0, name ? name + 1 : path, -1, narrow, sizeof narrow - 1, nullptr, nullptr);
        std::snprintf(text, sizeof text, "%s+0x%llX", narrow, static_cast<unsigned long long>(pc - reinterpret_cast<DWORD64>(module)));
    }
    else
        std::snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(pc));
    return text;
}

void Dump(unsigned seconds)
{
    std::vector<Captured> threads;
    threads.reserve(MaxThreads);
    const DWORD self = GetCurrentThreadId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof entry;
    for (BOOL more = Thread32First(snapshot, &entry); more && threads.size() < MaxThreads; more = Thread32Next(snapshot, &entry))
    {
        if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == self) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        threads.emplace_back();
        Captured& c = threads.back();
        c.id = entry.th32ThreadID;
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        bool got = false;
        if (SuspendThread(thread) != static_cast<DWORD>(-1))
        {
            got = GetThreadContext(thread, &context) != 0;
            ResumeThread(thread);
        }
        CloseHandle(thread);
        if (got) c.frames = Walk(context, c.pc);
    }
    CloseHandle(snapshot);

    const std::string head = "start-up has not finished " + std::to_string(seconds) + " s after load (the enable step has not run): "
                             "call stacks of " + std::to_string(threads.size()) + " threads follow (docs/testing.md)";
    MyClientSigs::StartupLine(head);
    try { MyClientLog::Get().warn("{}", head); } catch (...) {}
    for (const Captured& c : threads)
    {
        std::string line = "thread " + std::to_string(c.id) + ":";
        for (int i = 0; i < c.frames; ++i) line += (i ? " < " : " ") + Where(c.pc[i]);
        if (!c.frames) line += " (no stack read)";
        MyClientSigs::StartupLine(line);
        try { MyClientLog::Get().warn("{}", line); } catch (...) {}
    }
}
} // namespace

void StartHangWatch(const std::atomic<bool>& done, unsigned limitSeconds)
{
    const auto* flag = &done;
    std::thread([flag, limitSeconds] {
        const auto start = std::chrono::steady_clock::now();
        for (unsigned waited = 0; waited < limitSeconds; ++waited)
        {
            if (flag->load())
            {
                const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                MyClientSigs::StartupLine("enable step finished " + std::to_string(took / 1000.0).substr(0, 5) + " s after load");
                return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!flag->load()) Dump(limitSeconds);
    }).detach();
}
} // namespace MyClientDiag

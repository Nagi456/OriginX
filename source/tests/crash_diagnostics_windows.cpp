// Windows-only smoke test. --crash deliberately terminates THIS test process, never Minecraft.
#include "../client/CrashDiagnostics.h"
#include <cstring>
constexpr DWORD TestException = 0xE0424242;
LONG WINAPI HandleTest(EXCEPTION_POINTERS* info)
{
    return info && info->ExceptionRecord && (info->ExceptionRecord->ExceptionCode == TestException || info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc, char** argv)
{
    if (!MyClientCrash::Install()) return 2;
    MyClientCrash::Stage("standalone smoke test");
    if (argc == 2 && std::strcmp(argv[1], "--handled") == 0) {
        void* handler = AddVectoredExceptionHandler(1, HandleTest);
        if (!handler) return 3;
        RaiseException(TestException, 0, 0, nullptr);
        RemoveVectoredExceptionHandler(handler);
        return 0; // No dump is expected for a handled exception.
    }
    if (argc == 2 && (std::strcmp(argv[1], "--handled-reload-av") == 0 ||
                      std::strcmp(argv[1], "--budget-crash") == 0)) {
        void* handler = AddVectoredExceptionHandler(0, HandleTest); // after the observer; allow it to see the candidate
        if (!handler || !MyClientCrash::vectoredHandler) return 4;
        MyClientCrash::reloadActive.store(true);
        for (int i = 0; i < 6; ++i) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        MyClientCrash::reloadActive.store(false);
        RemoveVectoredExceptionHandler(handler);
        // Four candidate files max; the unhandled exception still owns its reserved fifth slot.
        if (MyClientCrash::fatalAttempts != 0) return 5;
        if (std::strcmp(argv[1], "--budget-crash") == 0) RaiseException(TestException, 0, 0, nullptr);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--replaced-filter-av") == 0) {
        SetUnhandledExceptionFilter(nullptr); // simulate a later game/third-party replacement
        MyClientCrash::reloadActive.store(true);
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); // candidate expected even without our UEF
    }
    if (argc == 2 && std::strcmp(argv[1], "--crash") == 0)
        RaiseException(TestException, 0, 0, nullptr);
    return 0;
}

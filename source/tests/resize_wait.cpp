#include "client/ResizeWait.h"
#include <cassert>
#include <cstdio>

int main() {
    using MyClientResizeWait::UntilReady;
    // A resize must survive multiple timeout windows, not return after the first one.
    int attempts = 0, warnings = 0;
    assert(UntilReady([&] { return ++attempts == 4; }, [] { return false; },
                      [&] { ++warnings; }));
    assert(attempts == 4 && warnings == 3);

    // An idle resize has no retry or warning.
    attempts = warnings = 0;
    assert(UntilReady([&] { ++attempts; return true; }, [] { return false; },
                      [&] { ++warnings; }));
    assert(attempts == 1 && warnings == 0);

    // Removed before waiting: do not enter a wait that can never finish.
    attempts = warnings = 0;
    assert(!UntilReady([&] { ++attempts; return false; }, [] { return true; },
                       [&] { ++warnings; }));
    assert(attempts == 0 && warnings == 0);

    // Removal during a failed wait must break the retry loop.
    bool removed = false;
    attempts = warnings = 0;
    assert(!UntilReady([&] { ++attempts; removed = attempts == 2; return false; },
                       [&] { return removed; }, [&] { ++warnings; }));
    assert(attempts == 2 && warnings == 1);

    // D3D12 can report UINT64_MAX as fence completion on device removal.
    // Even a successful-looking wait must not authorize normal resource cleanup.
    removed = false;
    attempts = warnings = 0;
    assert(!UntilReady([&] { ++attempts; removed = true; return true; },
                       [&] { return removed; }, [&] { ++warnings; }));
    assert(attempts == 1 && warnings == 0);
    std::puts("resize wait: 5 cases passed");
}

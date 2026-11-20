#pragma once
// A start-up that never finishes (2026-11-11, r57-test1 on 26.51.1: one start in two stopped right after the mod was
// loaded, no window, no crash log): when OriginX is not enabled `limitSeconds` after load, the call stack of every
// thread of the game is written to logs/startup.log and the OriginX log, so the next report shows where it waits.
#include <atomic>

namespace MyClientDiag {
// `done`: true once the enable step has finished, whether it worked or not.
void StartHangWatch(const std::atomic<bool>& done, unsigned limitSeconds = 75);
}

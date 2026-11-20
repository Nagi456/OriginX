#pragma once
#include <cstdint>
namespace MyClientResources {
struct Completion {
    bool pending = false, wantedChat = false, appliedChat = false;
    uint64_t began = 0, sequence = 0;
    void Begin(bool chat, uint64_t now) { pending = true; wantedChat = chat; began = now; }
    bool Finish(bool loadsIdle, bool restackPending) {
        if (!pending || !loadsIdle || restackPending) return false;
        pending = false; appliedChat = wantedChat; ++sequence; return true;
    }
};
}

#pragma once

namespace MyClientResizeWait {
// Resize cannot skip a frame like Present: retire its users before native resize.
// The caller supplies the existing bounded wait and reports each retry. Device loss
// ends the loop even if a fence reports apparent completion (UINT64_MAX on D3D12).
template <class Wait, class Removed, class Retry>
bool UntilReady(Wait wait, Removed removed, Retry retry) {
    for (;;) {
        if (removed()) return false;
        const bool ready = wait();
        if (removed()) return false;
        if (ready) return true;
        retry();
    }
}
} // namespace MyClientResizeWait

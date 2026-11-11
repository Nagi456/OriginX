#pragma once
#include <cstdint>

namespace MyClientPresentation {
// Sync interval zero is the game's V-Sync OFF request. An optimization toggle must not
// turn it into a compositor-paced present. The caller validates windowed mode and the
// swap chain's ALLOW_TEARING capability; V-Sync ON and unsupported chains stay native.
constexpr bool AllowNativeTearing(uint32_t syncInterval, bool swapAllowsTearing) {
    return syncInterval == 0 && swapAllowsTearing;
}
}

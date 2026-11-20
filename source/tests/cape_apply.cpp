#include "../CapeApply.h"
#include <cassert>
#include <cstdio>

int main()
{
    assert(MyClientCapes::NeedsApply({0, 2, 4}, {0, 2, 4}).empty());
    assert((MyClientCapes::NeedsApply({0, 3, 4}, {0, 2, 4}) == std::vector<size_t>{1}));
    assert((MyClientCapes::NeedsApply({1, 0}, {}) == std::vector<size_t>{0}));   // nothing applied yet: only real capes
    assert(MyClientCapes::UsableSkinSize(64, 64) && MyClientSkin::MaxSide >= 128 && MyClientCapes::UsableSkinSize(128, 64));
    assert(!MyClientCapes::UsableSkinSize(100, 100) && !MyClientCapes::UsableSkinSize(64, 48) && !MyClientCapes::UsableSkinSize(0, 0));
    // Review C1: a skin carrying an OriginX cape is never sent to the server as it is.
    assert(MyClientCapes::IsOriginXCape("OriginX_trial_cape_1") && !MyClientCapes::IsOriginXCape("") && !MyClientCapes::IsOriginXCape("Minecon2016"));
    std::puts("cape apply ok");
    return 0;
}

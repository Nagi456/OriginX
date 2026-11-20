#include "../TextureReloadPolicy.h"
#include <cassert>
int main() {
    using MyClientTexturePolicy::AllowInWorld;
    using MyClientTexturePolicy::IsTitleForReload;
    assert(IsTitleForReload(false, false, "start_screen"));
    assert(!IsTitleForReload(false, false, "settings_screen"));
    assert(!IsTitleForReload(false, false, "progress_screen"));
    assert(!IsTitleForReload(true, false, "start_screen"));
    assert(!IsTitleForReload(false, true, "start_screen"));
    MyClientTexturePolicy::StartupTitleReload startup;
    // Login/settings/loading must not enqueue one startup attempt every four seconds.
    for (uint64_t now : {1u, 4001u, 8001u, 12001u}) assert(!startup.Due(now, false, false));
    assert(!startup.Due(13000, true, false));
    assert(!startup.Due(16999, true, false));
    assert(startup.Due(17000, true, false)); // title path has no dependency on liveReady
    assert(!startup.Due(21000, true, true)); // existing deferred/pending request owns retry
    assert(!startup.Due(22000, false, false));
    assert(!startup.Due(23000, true, false));
    assert(startup.Due(27000, true, false));

    using MyClientTexturePolicy::ShowTitleNotice;
    assert(ShowTitleNotice(true, false, L"Applied 2 packs (reload requested)"));
    assert(ShowTitleNotice(true, false, L"Switched to: Default textures"));
    assert(!ShowTitleNotice(true, false, L"Could not switch: write denied"));
    assert(!ShowTitleNotice(true, false, L"Not applied yet - click Done"));
    assert(!ShowTitleNotice(false, false, L"Applied"));
    assert(!ShowTitleNotice(true, true, L"Applied"));
    assert(AllowInWorld(1, false, false));
    assert(!AllowInWorld(0, false, false));
    assert(!AllowInWorld(2, false, false));
    assert(!AllowInWorld(1, true, false));
    assert(!AllowInWorld(1, false, true));
    assert(!AllowInWorld(2, false, true));
}

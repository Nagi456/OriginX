#include "../CapeChannel.h"
#include "../SkinChannel.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace MyClientCapes;

int main()
{
    static_assert(Version == 2 && sizeof(Shared) == 1744);
    static_assert(offsetof(Shared, flags) == 1664 && offsetof(Shared, ownHideCape) == 1728);
    static_assert(offsetof(Shared, ownHideMark) == 1732 && offsetof(Shared, showMarks) == 1736);
    static_assert(offsetof(Shared, ownTier) == 1740);
    static Shared s{};
    s.magic = Magic; s.version = Version; s.size = sizeof(Shared); s.pid = 7;
    assert(Valid(s, 7) && !Valid(s, 8));
    s.version = Version + 1; assert(!Valid(s, 7)); s.version = Version;
    s.ownTier = 15; assert(Valid(s, 7));
    s.ownTier = 16; assert(!Valid(s, 7)); s.ownTier = 0;
    s.count = MaxPlayers + 1; assert(!Valid(s, 7)); s.count = 0;
    s.flags[0] = 15; s.ownHideCape = 1; s.ownHideMark = 1; s.showMarks = 1;
    Shared copy{}; std::memcpy(&copy, &s, sizeof s);
    assert(Valid(copy, 7) && copy.flags[0] == 15 && copy.showMarks == 1);
    copy.version = 1; assert(!Valid(copy, 7));
    copy = s; copy.flags[0] = 0x10; assert(!Valid(copy, 7));
    copy = s; copy.ownHideCape = 2; assert(!Valid(copy, 7));
    assert(Fresh(1000, 3000) && !Fresh(1000, 5000) && !Fresh(0, 10));

    // Review I6: the HUD's last answers are kept for AnswerKeepMs, not 3 s (its loop can take longer).
    assert(AnswerFresh(1000, 1000 + AnswerKeepMs) && !AnswerFresh(1000, 1001 + AnswerKeepMs) && !AnswerFresh(0, 5));
    char xuid[XuidSize];
    CopyXuid(xuid, "2535400000000001"); assert(std::strcmp(xuid, "2535400000000001") == 0);
    CopyXuid(xuid, "12a"); assert(xuid[0] == 0);
    CopyXuid(xuid, ""); assert(xuid[0] == 0);
    CopyXuid(xuid, "123456789012345678901234"); assert(xuid[0] == 0);   // 24 digits: too long for 1..20

    MyClientSkin::Request r{};
    r.width = 64; r.height = 64; r.capeId = 4; assert(MyClientSkin::UsableRequest(r));
    r.capeId = 6; assert(MyClientSkin::UsableRequest(r));
    r.capeId = 7; assert(!MyClientSkin::UsableRequest(r));
    std::puts("cape channel ok");
    return 0;
}

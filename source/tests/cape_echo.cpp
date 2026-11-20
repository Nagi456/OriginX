#include "../SkinChannel.h"
#include <cassert>
int main() {
    static MyClientSkin::Request desired{};
    desired.width = desired.height = 64; desired.capeId = 6;
    std::vector<uint8_t> pixels(64*64*4, 0);
    assert(MyClientSkin::MatchesCapeEcho(desired, 64, 64, false, pixels));
    pixels[100] = 1;
    assert(!MyClientSkin::MatchesCapeEcho(desired, 64, 64, false, pixels));
    pixels[100] = 0;
    assert(!MyClientSkin::MatchesCapeEcho(desired, 64, 64, true, pixels));
    assert(!MyClientSkin::MatchesCapeEcho(desired, 64, 32, false, pixels));
    pixels.pop_back();
    assert(!MyClientSkin::MatchesCapeEcho(desired, 64, 64, false, pixels));
    desired.capeId = 0;
    assert(!MyClientSkin::MatchesCapeEcho(desired, 64, 64, false, pixels));
}

#include "../SkinChannel.h"
#include <cassert>
#include <cstdio>
using namespace MyClientSkin;

int main()
{
    // The independent cape request ABI must not reinterpret a skin-only reset or overwrite the
    // last confirmed cape while a new request is still pending.
    static Request appearance{};
    appearance.width = appearance.height = 64;
    appearance.capeId = 2;
    assert(UsableRequest(appearance));
    appearance.capeId = 3;
    assert(!UsableRequest(appearance));
    assert(offsetof(Request, rgba) == 104); // legacy saved skin header remains readable
#ifdef _WIN32
    {
        RequestChannel producer, consumer;
        const DWORD pid = GetCurrentProcessId();
        assert(producer.Open(pid, true) && consumer.Open(pid, false));
        std::vector<uint8_t> cape(CapeBytes, 255), invalidCape(7, 255);
        uint32_t serial = 0;
        assert(!consumer.SendCape(pid, 3, cape, serial));
        assert(!consumer.SendCape(pid, 1, invalidCape, serial));
        assert(consumer.SendCape(pid, 1, cape, serial));
        assert(!consumer.SendCape(pid, 2, cape, serial)); // Never lose a pending skin/cape edit.
        assert(producer.TakePending(pid, appearance));
        assert(appearance.serial == serial && appearance.operation == uint32_t(Operation::Cape) && appearance.capeId == 1);
        assert(appearance.capeRgba[0] == 255 && appearance.capeRgba[CapeBytes - 1] == 255);
        assert(consumer.CapeChoice(pid) == 0);
        producer.PublishCapeChoice(pid, 1);
        producer.Finish(pid, serial, Result::Saved);
        assert(consumer.CapeChoice(pid) == 1 && consumer.Answer(pid, serial) == Result::Saved);
        assert(consumer.Send(pid, {}, 0, 0, false, "", serial));
        assert(producer.TakePending(pid, appearance) && appearance.operation == uint32_t(Operation::Skin));
        assert(appearance.width == 0 && appearance.skinOverride == 0 && consumer.CapeChoice(pid) == 1);
    }
#endif
    // 64x64 RGBA skin: every pixel encodes its own position; the hat area (40..47, 8..15) is see-through
    // except one red pixel at (41, 9), half transparent.
    std::vector<uint8_t> skin(64 * 64 * 4);
    for (uint32_t y = 0; y < 64; y++)
        for (uint32_t x = 0; x < 64; x++) {
            uint8_t* p = &skin[(y * 64 + x) * 4];
            p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[2] = 7; p[3] = 255;
            if (x >= 40 && x < 48 && y >= 8 && y < 16) p[3] = 0;
        }
    uint8_t* hat = &skin[(9 * 64 + 41) * 4];
    hat[0] = 255; hat[1] = 0; hat[2] = 0; hat[3] = 128;

    std::vector<uint8_t> rgba;
    assert(ToRgba(4, skin.data(), skin.size(), 64, 64, rgba) && rgba == skin);
    assert(!ToRgba(4, skin.data(), skin.size() - 1, 64, 64, rgba) && rgba.empty());   // too few bytes
    assert(!ToRgba(9, skin.data(), skin.size(), 64, 64, rgba));                        // unknown format
    assert(!ToRgba(4, skin.data(), skin.size(), 64, 48, rgba));                        // odd shape
    assert(!ToRgba(4, skin.data(), skin.size(), 512, 512, rgba));                      // too big
    assert(ToRgba(4, skin.data(), skin.size(), 64, 32, rgba) && rgba.size() == 64 * 32 * 4);   // legacy

    std::vector<uint8_t> rgb(64 * 64 * 3);
    for (size_t i = 0; i < 64 * 64; i++) { rgb[i * 3] = 1; rgb[i * 3 + 1] = 2; rgb[i * 3 + 2] = 3; }
    assert(ToRgba(3, rgb.data(), rgb.size(), 64, 64, rgba) && rgba[0] == 1 && rgba[2] == 3 && rgba[3] == 255);

    std::vector<uint8_t> head;
    assert(Head(skin.data(), 64, 64, head) && head.size() == 8 * 8 * 4);
    assert(head[0] == 8 && head[1] == 8 && head[3] == 255);                          // face (8, 8)
    const uint8_t* h7 = &head[(7 * 8 + 7) * 4];
    assert(h7[0] == 15 && h7[1] == 15);                                              // face (15, 15)
    const uint8_t* mixed = &head[(1 * 8 + 1) * 4];                                   // face (9, 9) + hat
    assert(mixed[0] == (255 * 128 + 9 * 127) / 255 && mixed[1] == (9 * 127) / 255 && mixed[3] == 255);

    // HD skin (128 wide): the face is 16x16 at (16, 16).
    std::vector<uint8_t> hd(128 * 128 * 4, 0);
    hd[(16 * 128 + 16) * 4] = 99;
    assert(Head(hd.data(), 128, 128, head) && head.size() == 16 * 16 * 4 && head[0] == 99);
    assert(!Head(hd.data(), 100, 100, head));

    assert(Hash(skin, 64, 64) != Hash(hd, 128, 128));

    // A stale menu/world removal must never share a resource name with another application of the same choice.
    const std::string choice = "MyClient_12345678_slim";
    const auto title = InstanceId(choice, 1), world = InstanceId(choice, 2), rejoin = InstanceId(choice, 3);
    assert(title != world && world != rejoin && title != rejoin);
    assert(MatchesSelection(title, choice) && MatchesSelection(world, choice) && MatchesSelection(rejoin, choice));
    assert(MatchesSelection(choice, choice)); // migration from old saved client instances
    assert(!MatchesSelection(title, "MyClient_12345678") && !MatchesSelection(title, "MyClient_12345679_slim"));
    assert(!MatchesSelection(choice + "_OX", choice) && !MatchesSelection(choice + "_OX1bad", choice));
    assert(!MatchesSelection("", "") && InstanceId(choice, UINT64_MAX).size() < 48);

    // Startup must let the real selected avatar finish first; a saved request is not proof of model readiness.
    AvatarLifecycle avatar;
    AvatarResources resources;
    assert(!resources.Changed(7)); // first observation must allow the initial native load
    assert(!resources.Changed(7));
    assert(resources.Changed(8)); // resource replacement without a new skin choice still invalidates readiness
    assert(!resources.Changed(8));
    AvatarLoadRetry nativeWait;
    assert(!nativeWait.Retry(100, false));
    assert(nativeWait.Retry(3100, false)); // applies even before an override can be built
    assert(!nativeWait.Retry(3200, true));
    AvatarLoadRetry load;
    assert(!load.Retry(100, false));
    assert(!load.Retry(3099, false));
    assert(load.Retry(3100, false));
    assert(!load.Retry(3101, false));
    assert(load.Retry(6100, false));
    assert(!load.Retry(9100, false)); // cap retries instead of constantly replacing a pending key
    assert(!load.Retry(9200, true));
    AvatarLoadRetry normalLoad;
    assert(!normalLoad.Retry(100, false));
    assert(!normalLoad.Retry(150, true));
    assert(normalLoad.attempts == 0);
    auto change = avatar.Select(1, true);
    assert(change.changed && !change.rebuild && !avatar.overriding);
    avatar.ObserveNative(false);
    assert(!avatar.Select(1, true).changed && !avatar.overriding);
    avatar.ObserveNative(true);
    change = avatar.Select(1, true);
    assert(change.changed && change.rebuild && avatar.overriding);
    assert(!avatar.Select(1, true).changed); // reuse this ref/key while the async texture becomes ready
    change = avatar.Select(2, true);
    assert(change.changed && change.rebuild && avatar.overriding); // A -> B
    change = avatar.Select(3, false);
    assert(change.changed && change.rebuild && !avatar.overriding); // My own skin restores the native model
    assert(!avatar.Select(3, false).changed);

    // Reload after a successful title build: the account pack can temporarily return
    // no selected skin. Only the retained ref AND the same initialized animation may
    // resume the override; first startup / other renderers still wait for native init.
    AvatarLifecycle reloaded;
    assert(!reloaded.Select(1, true, true).rebuild && !reloaded.overriding);
    reloaded.ObserveNative(true);
    assert(reloaded.initialized && reloaded.Select(1, true).rebuild);
    reloaded.nativeReady = false;
    reloaded.overriding = false;
    assert(!reloaded.Select(1, true, false).rebuild && !reloaded.overriding);
    assert(reloaded.Select(1, true, true).rebuild && reloaded.overriding);
    assert(!reloaded.nativeReady); // reusing initialized animation never fabricates texture readiness
    assert(!reloaded.Select(1, true, true).changed); // no rebuild per frame while asynchronous load runs
    assert(reloaded.Select(2, true, true).rebuild); // a new choice still receives a new native key
    assert(reloaded.Select(3, false, true).rebuild && !reloaded.overriding); // My own skin never uses retained override
    reloaded = {}; // reused renderer/context must not inherit the old initialization prerequisite
    assert(!reloaded.initialized && !reloaded.Select(4, true, true).rebuild && !reloaded.overriding);

    // Menu models bypass Player::updateSkin: supply its normalized cache key for both arm types.
    assert(std::string(NativeGeometryName(false)) == "geometry.humanoid.custom");
    assert(std::string(NativeGeometryName(true)) == "geometry.humanoid.customslim");
    assert(std::string(ResourcePatch(true)).find("customSlim") != std::string::npos);

    // Whose head can be read from the picture.
    assert(ClassicLayout(false, "geometry.humanoid.custom", ""));
    assert(ClassicLayout(false, "geometry.humanoid.customSlim", ""));
    assert(ClassicLayout(false, "", "{\"geometry\":{\"default\":\"geometry.humanoid.custom\"}}"));
    assert(ClassicLayout(false, "", ""));
    assert(!ClassicLayout(true, "geometry.humanoid.custom", ""));                 // persona
    assert(!ClassicLayout(false, "geometry.persona_1a2b", ""));
    assert(!ClassicLayout(false, "geometry.n3_frog.custom", ""));                 // a pack's own model
    assert(!ClassicLayout(false, "", "{\"geometry\":{\"default\":\"geometry.n3_frog\"}}"));
    std::puts("skin ok");
}

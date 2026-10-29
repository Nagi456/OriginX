#include "MinecraftDataProvider.h"
#include "FeatureChannel.h"
#include "PartyChannel.h"
#include <cassert>
#include <iostream>

// Synthetic data for IPC verification ONLY. Never used by the shipped HUD/DLL.
int main()
{
    HWND window = CreateWindowExW(0, L"STATIC", L"Bridge smoke test", WS_OVERLAPPED,
                                  0, 0, 800, 600, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    assert(window);
    assert(MyClientBridgeInitialize());
    assert(MyClientBridgeInitialize()); // Idempotent.
    MyClientFeatures::Channel features;
    assert(features.Open(GetCurrentProcessId(), true));
    MyClientParty::Channel party;
    assert(party.Open(GetCurrentProcessId(), true));
    assert(MinecraftDataProvider::Initialize(window));
    MinecraftDataProvider::Tick(window);
    assert(MinecraftDataProvider::GetStatus() == MinecraftDataProvider::Status::WaitingForData);
    uint32_t count = 100;
    assert(!MinecraftDataProvider::GetArrowCount(count));

    MyClientFrame f{};
    f.structSize = sizeof(f);
    f.abiVersion = MYCLIENT_ABI_VERSION;
    f.flags = MYCLIENT_HAS_ARROWS | MYCLIENT_HAS_TARGET | MYCLIENT_HAS_PROJECTION | MYCLIENT_HAS_BOUNDS;
    f.arrowCount = 96;
    f.blockX = -12;
    f.blockY = 64;
    f.blockZ = 9;
    f.minX = -12; f.maxX = -11;
    f.minY = 64; f.maxY = 65;
    f.minZ = 9; f.maxZ = 10;
    f.points[0] = {0.5f, 0.5f};
    assert(MyClientBridgePublish(&f));
    MinecraftDataProvider::Tick(window);
    assert(MinecraftDataProvider::GetStatus() == MinecraftDataProvider::Status::Live);
    assert(MinecraftDataProvider::GetArrowCount(count) && count == 96);
    assert(MinecraftDataProvider::GetTargetBlockData().x == -12);
    assert(MinecraftDataProvider::GetBlockOutlineScreenData().valid);
    RECT client{}, outer{};
    POINT origin{};
    assert(GetClientRect(window, &client) && GetWindowRect(window, &outer));
    assert(ClientToScreen(window, &origin));
    assert(MinecraftDataProvider::GetBlockOutlineScreenData().points[0].x ==
           origin.x - outer.left + (client.right + 1) / 2);

    f.flags = MYCLIENT_HAS_ARROWS;
    f.arrowCount = 0;
    assert(MyClientBridgePublish(&f));
    MinecraftDataProvider::Tick(window);
    assert(MinecraftDataProvider::GetArrowCount(count) && count == 0);
    assert(!MinecraftDataProvider::GetTargetBlockData().valid);
    assert(!MinecraftDataProvider::GetBlockOutlineScreenData().valid);
    Sleep(2100);
    MinecraftDataProvider::Tick(window);
    assert(MinecraftDataProvider::GetStatus() == MinecraftDataProvider::Status::Stale);
    assert(!MinecraftDataProvider::GetArrowCount(count));

    assert(features.Lock());
    features.Data()->telemetry.fps = 144;
    features.Data()->telemetry.fpsAt = GetTickCount64();
    features.Data()->telemetry.armorAt = GetTickCount64();
    features.Data()->telemetry.armor[0] = {MyClientFeatures::Durable, 10, 100};
    features.Data()->telemetry.zoomAt = GetTickCount64();
    features.Data()->telemetry.zoomActive = 1;
    features.Unlock();
    MinecraftDataProvider::Tick(window);
    float fps = 0;
    assert(MinecraftDataProvider::GetFps(fps) && fps == 144);
    assert(MinecraftDataProvider::GetArmor(0).remaining == 10);
    assert(MinecraftDataProvider::IsZoomApplied());
    MinecraftDataProvider::SetZoomRequested(true, 'C');
    assert(features.Lock());
    assert(features.Data()->command.zoom == 1 && features.Data()->command.key == 'C');
    features.Data()->telemetry.fpsAt = 0;
    features.Data()->telemetry.armorAt = 0;
    features.Data()->telemetry.zoomAt = 0;
    features.Unlock();
    MinecraftDataProvider::Tick(window);
    assert(!MinecraftDataProvider::GetFps(fps));
    assert(MinecraftDataProvider::GetArmor(0).state == MyClientFeatures::Unknown);
    assert(!MinecraftDataProvider::IsZoomApplied());
    MinecraftDataProvider::SetPartyRequested(true, {L"Test Player", L"Alex"});
    assert(party.Lock());
    assert(MyClientParty::Registered(party.Data()->config, "test player", GetTickCount64()));
    party.Data()->at = GetTickCount64(); party.Data()->status = MyClientParty::Status::Sent;
    party.Unlock();
    MinecraftDataProvider::Tick(window);
    assert(std::wstring(MinecraftDataProvider::GetPartyStatusText()).find(L"sent") != std::wstring::npos);
    MinecraftDataProvider::SetPartyRequested(true, {L"Alex\n"});
    assert(party.Lock()); assert(!MyClientParty::Active(party.Data()->config, GetTickCount64())); party.Unlock();
    MinecraftDataProvider::SetPartyRequested(true, {L"日本語"});
    assert(party.Lock()); assert(!MyClientParty::Active(party.Data()->config, GetTickCount64())); party.Unlock();
    MinecraftDataProvider::SetPartyRequested(true, std::vector<std::wstring>(65, L"Alex"));
    assert(party.Lock()); assert(!MyClientParty::Active(party.Data()->config, GetTickCount64()));
    party.Data()->at = 0; party.Unlock();
    MinecraftDataProvider::Tick(window);
    assert(std::wstring(MinecraftDataProvider::GetPartyStatusText()).find(L"waiting") != std::wstring::npos);
    MinecraftDataProvider::Shutdown();
    assert(party.Lock()); assert(party.Data()->config.enabled == 0); party.Unlock();
    assert(features.Lock());
    assert(features.Data()->command.zoom == 0);
    features.Unlock();
    assert(MinecraftDataProvider::Initialize(window));
    ++f.abiVersion;
    assert(!MyClientBridgePublish(&f));
    assert(GetLastError() == ERROR_INVALID_DATA);
    MyClientBridgeShutdown();
    MinecraftDataProvider::Tick(window);
    assert(MinecraftDataProvider::GetStatus() == MinecraftDataProvider::Status::WaitingForData);
    assert(MyClientBridgeInitialize());
    --f.abiVersion;
    assert(MyClientBridgePublish(&f));
    MinecraftDataProvider::Tick(window);
    assert(MinecraftDataProvider::GetArrowCount(count) && count == 0);
    MyClientBridgeShutdown();
    MyClientBridgeShutdown();
    assert(!MyClientBridgePublish(&f));
    assert(GetLastError() == ERROR_NOT_READY);
    MinecraftDataProvider::Shutdown();
    DestroyWindow(window);
    std::cout << "Bridge smoke test passed (synthetic data; not Minecraft validation).\n";
}

#include "MinecraftDataProvider.h"
#include "BridgeChannel.h"
#include "FeatureChannel.h"
#include "PartyChannel.h"
#include "ResourceChannel.h"
#include "ShulkerChannel.h"
#include "TabListChannel.h"
#include "SkinChannel.h"

namespace MinecraftDataProvider {
namespace {
MyClientIpc::Channel channel;
MyClientFeatures::Channel features;
MyClientParty::Channel party;
MyClientResources::Channel resources;
MyClientResources::Shared resourceSnapshot{};
ULONGLONG resourceOpenAttempt = 0;
MyClientShulker::Channel shulkers;
MyClientShulker::Shared shulkerSnapshot{};
ULONGLONG shulkerOpenAttempt = 0;
MyClientShulker::HoverChannel shulkerHover;
MyClientShulker::Hover shulkerHoverSnapshot{};
ULONGLONG shulkerHoverOpenAttempt = 0;
MyClientTabList::Channel tabLists;
MyClientTabList::Shared tabListSnapshot{};
ULONGLONG tabListOpenAttempt = 0;
MyClientSkin::Channel skins;
ULONGLONG skinOpenAttempt = 0;
uint32_t skinSerial = 0;          // of the channel (per game process)
uint32_t skinChanges = 0;         // goes up whenever skinRgba changed: the HUD redraws its head then
std::vector<uint8_t> skinRgba;    // kept after the game closes: the last skin seen
uint32_t skinW = 0, skinH = 0;
bool skinNoPicture = false;       // the game said: a persona / own-model skin (the default head is shown)
MyClientSkin::RequestChannel skinRequests;   // the Closet: "wear this skin" (opened when needed)
uint32_t skinRequestSerial = 0;              // of the last request written (0 = none)
MyClientParty::Shared partySnapshot{};
ULONGLONG partyOpenAttempt = 0;
MyClientFeatures::Telemetry featureSnapshot{};
MyClientFeatures::Server serverSnapshot{};
MyClientFeatures::Stats statsSnapshot{}, statsPrevious{}, statsRate{};
ULONGLONG statsPreviousAt = 0;
ULONGLONG featureOpenAttempt = 0;
DWORD processId = 0;
char armorItems[4][32] = {};   // Armor HUD icons: item id per slot from the DLL
ULONGLONG clockAt = 0, clockMovedAt = 0;   // Day Timer: last clock from the DLL, last time its value changed
int32_t clockTime = 0;
uint32_t healthField = 0;   // Health Warning, fresh with clockAt (MyClientFeatures::DecodeHealth)
MyClientFeatures::Debug debugSnapshot{};   // Debug Menu, fresh with clockAt
MyClientFeatures::Look lookSnapshot{};   // Block Details, fresh with clockAt
MinecraftDataProvider::Gpu gpuSnapshot{};   // Debug Menu: the game's GPU (stays while the DLL channel is open)
ULONGLONG lastOpenAttempt = 0;
Status status = Status::Disconnected;
bool arrowsKnown = false;
ULONGLONG inWorldAt = 0; // last time the DLL reported a local player (frame had inventory data)
uint32_t arrows = 0;
bool zoomRequested = false;
bool fullbrightRequested = false;
bool nametagRequested = false;
bool hitboxRequested = false;
bool nickRequested = false;
bool replayRequested = false;
bool replayBuffer = false;   // keep the session from joining so recording can start mid-game
bool hitEffectCrit = false, hitEffectMagic = false;   // Hit Effect module (DLL spawns the particles)
bool leftHandRequested = false;   // Left Hand module (DLL mirrors the first-person held item)
uint32_t frameGenBits = 0;   // Frame Generation request (bits 27..31 of the autoRq field)
float framePresentedFps = 0; uint32_t frameGenState = 0;   // from the DLL, with featureSnapshot
bool shulkerHoverRequested = false;   // Shulker Preview (DLL hides the game's tooltip under the HUD's popup)
bool hudMenuOpen = false;         // the HUD menu has the focus (the DLL closes the pause screen that causes)
uint32_t entityDistanceBits = 0;   // Optimization: entity render distance, already encoded for command.reserved
uint32_t frameGenWide = 0;          // Frame Generation multiplier x2..x10 for Shared.frameGenMultiplier, 0 = off
uint32_t optimizeBits = 0;         // Optimization extras, already encoded for Shared.optimize
char replayPath[264] = {};
char nickText[32] = {};
uint32_t hitboxFilter = 1;
uint32_t hitboxRgb[4] = {};   // Shared.hitboxRgb: HitboxCustom | 0xRRGGBB, or 0 for the kind's own colour
bool outlineRequested = false;
bool texturesRequested = false;
uint32_t autoRqMode = 0;
uint32_t renderChunks = 0, renderCounter = 0;   // Render Distance request (0 chunks = none)
uint32_t texturesSeq = 0;
uint32_t outlineRgb = 0x50B4FF;
uint32_t zoomKey = 'C';
uint32_t zoomFactorX100 = 300;
TargetBlockData target;
BlockOutlineData block;
BlockOutlineScreenData screen;

void ClearSnapshot()
{
    arrowsKnown = false;
    arrows = 0;
    target = {};
    block = {};
    screen = {};
}
}

bool Initialize(HWND window)
{
    Shutdown();
    if (!IsWindow(window)) return false;
    GetWindowThreadProcessId(window, &processId);
    return processId != 0;
}

void Shutdown()
{
    if (features.Data() && features.Lock()) { features.Data()->command = {}; features.Unlock(); }
    features.Close(); featureSnapshot = {}; serverSnapshot = {}; statsSnapshot = {}; statsPrevious = {}; statsRate = {}; statsPreviousAt = 0; featureOpenAttempt = 0;
    resources.Close(); resourceSnapshot = {}; resourceOpenAttempt = 0;
    shulkers.Close(); shulkerSnapshot = {}; shulkerOpenAttempt = 0;
    shulkerHover.Close(); shulkerHoverSnapshot = {}; shulkerHoverOpenAttempt = 0;
    tabLists.Close(); tabListSnapshot = {}; tabListOpenAttempt = 0;
    skins.Close(); skinOpenAttempt = 0; skinSerial = 0;
    skinRequests.Close(); skinRequestSerial = 0;
    if (party.Data() && party.Lock()) { party.Data()->config = {}; party.Unlock(); }
    party.Close(); partySnapshot = {}; partyOpenAttempt = 0;
    channel.Close();
    processId = 0;
    lastOpenAttempt = 0;
    status = Status::Disconnected;
    inWorldAt = 0;
    zoomRequested = false;
    fullbrightRequested = false;
    nametagRequested = false;
    hitboxRequested = false;
    nickRequested = false;
    replayRequested = false;
    entityDistanceBits = 0;
    optimizeBits = 0;
    outlineRequested = false;
    texturesRequested = false; autoRqMode = 0; renderChunks = 0;
    ClearSnapshot();
}

void Tick(HWND window)
{
    DWORD currentPid = 0;
    if (!IsWindow(window) || !GetWindowThreadProcessId(window, &currentPid))
    {
        Shutdown();
        return;
    }
    if (currentPid != processId && !Initialize(window)) return;

    ClearSnapshot();
    const ULONGLONG now = GetTickCount64();
    // The feature data is kept from the last good read: a lock that is busy for a moment must not blank
    // it (the settings pages flashed "UNAVAILABLE"). It is cleared when the channel is closed or invalid.
    partySnapshot = {};
    resourceSnapshot = {};
    shulkerSnapshot = {};
    shulkerHoverSnapshot = {};
    tabListSnapshot = {};
    if (!resources.Data() && (!resourceOpenAttempt || now - resourceOpenAttempt >= 500)) {
        resourceOpenAttempt = now; resources.Open(processId, false);
    }
    if (resources.Data() && resources.Lock()) {
        const auto snapshot = *resources.Data();
        resources.Unlock();
        if (MyClientResources::Valid(snapshot, processId)) resourceSnapshot = snapshot;
        else resources.Close();
    }
    if (!skins.Data() && (!skinOpenAttempt || now - skinOpenAttempt >= 500)) {
        skinOpenAttempt = now;
        if (skins.Open(processId, false)) skinSerial = 0;
    }
    if (skins.Data()) {
        std::vector<uint8_t> rgba;
        uint32_t w = 0, h = 0, flags = 0;
        if (skins.TakeIfNew(processId, skinSerial, rgba, w, h, flags)) {
            if (w && h) { skinRgba.swap(rgba); skinW = w; skinH = h; skinChanges++; skinNoPicture = false; }
            else if (flags & MyClientSkin::NoClassicPicture) {   // a persona / own-model skin: the default head
                skinRgba.clear(); skinW = skinH = 0; skinChanges++; skinNoPicture = true;
            }   // plain "none" (no player) keeps the last skin
        }
    }
    if (!shulkers.Data() && (!shulkerOpenAttempt || now - shulkerOpenAttempt >= 500)) {
        shulkerOpenAttempt = now; shulkers.Open(processId, false);
    }
    if (shulkers.Data() && shulkers.Lock()) {
        const auto snapshot = *shulkers.Data();
        shulkers.Unlock();
        if (MyClientShulker::Valid(snapshot, processId)) shulkerSnapshot = snapshot;
        else shulkers.Close();
    }
    if (!shulkerHover.Data() && (!shulkerHoverOpenAttempt || now - shulkerHoverOpenAttempt >= 500)) {
        shulkerHoverOpenAttempt = now; shulkerHover.Open(processId, false);
    }
    if (shulkerHover.Data() && shulkerHover.Lock()) {
        const auto snapshot = *shulkerHover.Data();
        shulkerHover.Unlock();
        if (MyClientShulker::Valid(snapshot, processId)) shulkerHoverSnapshot = snapshot;
        else shulkerHover.Close();
    }
    if (!tabLists.Data() && (!tabListOpenAttempt || now - tabListOpenAttempt >= 500)) {
        tabListOpenAttempt = now; tabLists.Open(processId, false);
    }
    if (tabLists.Data() && tabLists.Lock()) {
        const auto snapshot = *tabLists.Data();
        tabLists.Unlock();
        if (MyClientTabList::Valid(snapshot, processId)) tabListSnapshot = snapshot;
        else tabLists.Close();
    }
    if (!party.Data() && (!partyOpenAttempt || now - partyOpenAttempt >= 500)) {
        partyOpenAttempt = now; party.Open(processId, false);
    }
    if (party.Data() && party.Lock()) {
        partySnapshot = *party.Data(); party.Unlock();
        if (!MyClientParty::Valid(partySnapshot, processId)) { party.Close(); partySnapshot = {}; }
    }
    if (!features.Data() && (!featureOpenAttempt || now - featureOpenAttempt >= 500)) {
        featureOpenAttempt = now;
        features.Open(processId, false);
    }
    if (!features.Data()) { featureSnapshot = {}; serverSnapshot = {}; statsSnapshot = {}; statsRate = {}; statsPreviousAt = 0; }
    if (features.Data() && features.Lock(2)) {
        const auto snapshot = *features.Data();
        features.Unlock();
        if (MyClientFeatures::Valid(snapshot, processId)) {
            std::memcpy(replayPath, snapshot.recPath, sizeof(replayPath)); replayPath[sizeof(replayPath) - 1] = 0;
            featureSnapshot = snapshot.telemetry; serverSnapshot = snapshot.server; statsSnapshot = snapshot.stats;
            framePresentedFps = snapshot.presentedFps; frameGenState = snapshot.frameGen;
            std::memcpy(armorItems, snapshot.armorItems, sizeof(armorItems));
            for (auto& item : armorItems) item[sizeof(item) - 1] = 0;
            if (snapshot.clockAt) {
                // The first value after joining a world counts as moving until it proves otherwise.
                if (!clockAt || snapshot.clockTime != clockTime) clockMovedAt = now;
                clockTime = snapshot.clockTime;
            }
            clockAt = snapshot.clockAt;
            healthField = snapshot.clockReserved;
            debugSnapshot = snapshot.debug;
            lookSnapshot = snapshot.look;
            {
                char name[sizeof(snapshot.gpuName)];
                std::memcpy(name, snapshot.gpuName, sizeof(name)); name[sizeof(name) - 1] = 0;
                gpuSnapshot = {name, snapshot.gpuDirectX, snapshot.gpuFeatureLevel, snapshot.gpuVideoMb, snapshot.gpuVendor, snapshot.gpuDriver};
            }
            debugSnapshot.lookBlock[sizeof(debugSnapshot.lookBlock) - 1] = 0;
            debugSnapshot.biome[sizeof(debugSnapshot.biome) - 1] = 0;
            // Per-second call counts of the DLL's game hooks (shown in the detail screens for diagnosis).
            if (!statsPreviousAt) { statsPrevious = statsSnapshot; statsPreviousAt = now; }
            else if (now - statsPreviousAt >= 1000) {
                const double seconds = static_cast<double>(now - statsPreviousAt) / 1000.0;
                const auto rate = [&](uint32_t current, uint32_t previous) {
                    return static_cast<uint32_t>(static_cast<double>(static_cast<uint32_t>(current - previous)) / seconds + 0.5);
                };
                statsRate = {statsSnapshot.at,
                             rate(statsSnapshot.lightBuild, statsPrevious.lightBuild), rate(statsSnapshot.lightSplit, statsPrevious.lightSplit),
                             rate(statsSnapshot.lightCreate, statsPrevious.lightCreate), rate(statsSnapshot.nameTag, statsPrevious.nameTag),
                             rate(statsSnapshot.nameTagLocal, statsPrevious.nameTagLocal), rate(statsSnapshot.outline, statsPrevious.outline),
                             rate(statsSnapshot.tessColor, statsPrevious.tessColor), 0, 0, 0};
                statsPrevious = statsSnapshot; statsPreviousAt = now;
            }
        }
        else { features.Close(); featureSnapshot = {}; serverSnapshot = {}; statsSnapshot = {}; statsRate = {}; statsPreviousAt = 0; }
    }
    if (!channel.Data())
    {
        if (lastOpenAttempt && now - lastOpenAttempt < 500) return;
        lastOpenAttempt = now;
        if (!channel.Open(processId, false))
        {
            status = Status::Disconnected;
            return;
        }
    }

    const DWORD result = channel.Lock(0); // Never block the HUD render loop.
    if (result != WAIT_OBJECT_0)
    {
        if (result == WAIT_ABANDONED) channel.Unlock();
        status = Status::Stale;
        return;
    }
    const MyClientIpc::SharedData snapshot = *channel.Data();
    channel.Unlock();

    if (!MyClientIpc::HasValidHeader(snapshot, processId))
    {
        status = Status::InvalidData;
        channel.Close();
        return;
    }
    if (!snapshot.hasFrame)
    {
        status = Status::WaitingForData;
        return;
    }
    const ULONGLONG snapshotTime = GetTickCount64();
    if (snapshot.publishedAt > snapshotTime ||
        snapshotTime - snapshot.publishedAt > MyClientIpc::StaleAfterMs)
    {
        status = Status::Stale;
        return;
    }
    if (!IsValidFrame(snapshot.frame))
    {
        status = Status::InvalidData;
        return;
    }

    const auto& f = snapshot.frame;
    status = Status::Live;
    arrowsKnown = (f.flags & MYCLIENT_HAS_ARROWS) != 0;
    if (arrowsKnown) inWorldAt = GetTickCount64();
    arrows = f.arrowCount;
    if (f.flags & MYCLIENT_HAS_TARGET)
    {
        target = {true, f.blockX, f.blockY, f.blockZ};
    }
    if (f.flags & MYCLIENT_HAS_BOUNDS)
    {
        block = {true, f.minX, f.minY, f.minZ, f.maxX, f.maxY, f.maxZ};
    }
    if (f.flags & MYCLIENT_HAS_PROJECTION)
    {
        RECT clientRect{}, windowRect{};
        POINT origin{};
        if (!GetClientRect(window, &clientRect) || !GetWindowRect(window, &windowRect) ||
            !ClientToScreen(window, &origin) || clientRect.right <= 0 ||
            clientRect.bottom <= 0 || clientRect.right > 32768 || clientRect.bottom > 32768)
            return;
        screen.valid = true;
        for (int i = 0; i < 8; ++i)
        {
            screen.points[i].x = origin.x - windowRect.left +
                static_cast<LONG>(std::lround(f.points[i].x * clientRect.right));
            screen.points[i].y = origin.y - windowRect.top +
                static_cast<LONG>(std::lround(f.points[i].y * clientRect.bottom));
        }
    }
}

// True only while the game is inside a world/server. The DLL's per-frame screen check (Stats.reserved,
// refreshed every frame on every screen) answers at once on leaving. Older DLLs fall back to the level
// tick's frame, which simply stops when the world is left, so it only ends once that frame goes stale.
bool IsInWorld()
{
    if (MyClientFeatures::Fresh(statsSnapshot.at, GetTickCount64(), 500) &&
        MyClientFeatures::HasFlag(statsSnapshot.reserved, MyClientFeatures::GameWorldKnown))
        return MyClientFeatures::HasFlag(statsSnapshot.reserved, MyClientFeatures::GameInWorld);
    return status == Status::Live && inWorldAt &&
           GetTickCount64() - inWorldAt <= 1500;
}

// The DLL refreshes Stats.at from its per-frame render hook, which runs on every screen.
bool IsGameRunning()
{
    return status == Status::Live || MyClientFeatures::Fresh(statsSnapshot.at, GetTickCount64(), 3000);
}

int IsOnTitleScreen()
{
    if (!MyClientFeatures::Fresh(statsSnapshot.at, GetTickCount64(), 1000) ||
        !MyClientFeatures::HasFlag(statsSnapshot.reserved, MyClientFeatures::GameTitleKnown))
        return -1;
    return MyClientFeatures::HasFlag(statsSnapshot.reserved, MyClientFeatures::GameOnTitle) ? 1 : 0;
}

bool GetArrowCount(uint32_t& count)
{
    count = arrows;
    return arrowsKnown;
}
Status GetStatus() { return status; }
const wchar_t* GetStatusText()
{
    switch (status)
    {
    case Status::Disconnected: return L"DLL: not connected";
    case Status::WaitingForData: return L"DLL: waiting for game data";
    case Status::Live: return L"Game data: receiving";
    case Status::Stale: return L"Game data: stale";
    case Status::InvalidData: return L"Game data: incompatible / invalid";
    }
    return L"Game data: unknown";
}
const TargetBlockData& GetTargetBlockData() { return target; }
const BlockOutlineData& GetBlockOutlineData() { return block; }
const BlockOutlineScreenData& GetBlockOutlineScreenData() { return screen; }
void UpdateBlockOutlineData(const BlockOutlineData& data) { block = data; }
void UpdateBlockOutlineScreenData(const BlockOutlineScreenData& data) { screen = data; }
void ClearBlockOutlineData() { block = {}; screen = {}; target = {}; }
const std::vector<uint8_t>& GetSkin(uint32_t& width, uint32_t& height, uint32_t& changes) {
    width = skinW; height = skinH; changes = skinChanges; return skinRgba;
}
SkinRequestState RequestSkin(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, bool slim, const std::string& name) {
    if (!processId) return SkinRequestState::NoGame;
    if (!skinRequests.Data() && !skinRequests.Open(processId, false)) return SkinRequestState::OldDll;   // a DLL without the Closet request
    uint32_t serial = 0;
    if (!skinRequests.Send(processId, rgba, width, height, slim, name, serial)) return SkinRequestState::Failed;
    skinRequestSerial = serial;
    return SkinRequestState::Waiting;
}
SkinRequestState SkinRequestAnswer() {
    if (!processId || !skinRequestSerial || !skinRequests.Data()) return SkinRequestState::None;
    switch (skinRequests.Answer(processId, skinRequestSerial)) {
    case MyClientSkin::Result::Sent: return SkinRequestState::Sent;
    case MyClientSkin::Result::NoPlayer: return SkinRequestState::NoPlayer;
    case MyClientSkin::Result::Failed: return SkinRequestState::Failed;
    default: return SkinRequestState::Waiting;
    }
}
bool SkinHasNoPicture() { return skinNoPicture; }
void SetSkin(std::vector<uint8_t> rgba, uint32_t width, uint32_t height) {
    if (rgba.size() < static_cast<size_t>(width) * height * 4) return;
    skinRgba = std::move(rgba); skinW = width; skinH = height; skinChanges++;
}
bool GetResources(MyClientResources::Counts& counts) {
    counts = {};
    if (!MyClientResources::Fresh(resourceSnapshot.at, GetTickCount64())) return false;
    counts = resourceSnapshot.counts; return true;
}
bool GetShulkers(MyClientShulker::Shared& out) {
    out = {};
    if (!MyClientShulker::Fresh(shulkerSnapshot.at, GetTickCount64())) return false;
    out = shulkerSnapshot; return true;
}
bool GetShulkerHover(MyClientShulker::HoverBox& out) {
    out = {};
    if (!shulkerHoverSnapshot.active || !MyClientShulker::HoverFresh(shulkerHoverSnapshot.at, GetTickCount64())) return false;
    out = shulkerHoverSnapshot.box; return true;
}
bool GetTabList(MyClientTabList::Shared& out) {
    out = {};
    if (!MyClientTabList::Fresh(tabListSnapshot.at, GetTickCount64())) return false;
    out = tabListSnapshot; return true;
}
bool GetFps(float& fps) {
    fps = 0;
    if (!MyClientFeatures::Fresh(featureSnapshot.fpsAt, GetTickCount64()) ||
        !std::isfinite(featureSnapshot.fps) || featureSnapshot.fps <= 0 || featureSnapshot.fps > 10000) return false;
    fps = featureSnapshot.fps; return true;
}
bool GetWorldClock(int32_t& time, bool& moving) {
    const ULONGLONG now = GetTickCount64();
    if (!MyClientFeatures::Fresh(clockAt, now, 1500)) return false;
    time = clockTime;
    moving = clockMovedAt && now - clockMovedAt < 3000;
    return true;
}
bool GetPlayerHealth(int& points) {
    if (!MyClientFeatures::Fresh(clockAt, GetTickCount64(), 1500)) return false;
    return MyClientFeatures::DecodeHealth(healthField, points);
}
bool GetGpu(Gpu& out) {
    if (!features.Data() || gpuSnapshot.name.empty()) return false;
    out = gpuSnapshot;
    return true;
}
bool GetDebug(MyClientFeatures::Debug& out) {
    if (!MyClientFeatures::Fresh(clockAt, GetTickCount64(), 1500)) return false;
    out = debugSnapshot;
    return true;
}
bool GetLook(MyClientFeatures::Look& out) {
    if (!MyClientFeatures::Fresh(clockAt, GetTickCount64(), 1500) || !lookSnapshot.known) return false;
    out = lookSnapshot;
    out.name[sizeof(out.name) - 1] = out.tags[sizeof(out.tags) - 1] = out.held[sizeof(out.held) - 1] = 0;
    return true;
}
std::string GetArmorItem(unsigned slot) {
    if (slot >= 4 || !MyClientFeatures::Fresh(featureSnapshot.armorAt, GetTickCount64())) return {};
    return armorItems[slot];
}
std::wstring GameDirectoryUncached();
std::wstring GameDirectory() {
    // Asked several times per HUD frame: the answer only changes with the game process, so it is kept.
    static DWORD cachedFor = 0;
    static std::wstring cached;
    if (processId && processId == cachedFor && !cached.empty()) return cached;
    cachedFor = processId;
    cached = GameDirectoryUncached();
    return cached;
}
std::wstring GameDirectoryUncached() {
    if (!processId) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return {};
    wchar_t path[MAX_PATH * 2] = {};
    DWORD size = MAX_PATH * 2;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != 0;
    CloseHandle(process);
    if (!ok) return {};
    std::wstring dir(path, size);
    const auto slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash);
}
MyClientFeatures::Armor GetArmor(unsigned slot) {
    if (slot >= 4 || !MyClientFeatures::Fresh(featureSnapshot.armorAt, GetTickCount64()) ||
        !MyClientFeatures::ValidArmor(featureSnapshot.armor[slot])) return {};
    return featureSnapshot.armor[slot];
}
bool GetCamera(Camera& out) {
    const auto& t = featureSnapshot;
    if (!MyClientFeatures::Fresh(t.camAt, GetTickCount64(), MyClientFeatures::CommandLeaseMs)) return false;
    const float v[] = {t.camX, t.camY, t.camZ, t.camYaw, t.camPitch, t.camFov};
    for (float f : v) if (!std::isfinite(f)) return false;
    if (t.camFov < 5.f || t.camFov > 170.f) return false;
    out = {t.camX, t.camY, t.camZ, t.camYaw, t.camPitch, t.camFov};
    return true;
}
bool IsZoomApplied() {
    return MyClientFeatures::Fresh(featureSnapshot.zoomAt, GetTickCount64(), MyClientFeatures::CommandLeaseMs) && featureSnapshot.zoomActive == 1;
}
namespace {
// Zoom, Full Bright and Nametag share one command record, so always write them together.
void WriteCommand() {
    if (!features.Data() || !features.Lock(2)) return;
    if (MyClientFeatures::Valid(*features.Data(), processId)) {
        std::memcpy(features.Data()->nick, nickText, sizeof(nickText));
        features.Data()->optimize = optimizeBits;
        features.Data()->frameGenMultiplier = frameGenWide;
        std::memcpy(features.Data()->hitboxRgb, hitboxRgb, sizeof(hitboxRgb));
        features.Data()->command = {GetTickCount64(), zoomRequested ? 1u : 0u, zoomKey,
                                    fullbrightRequested ? 1u : 0u, nametagRequested ? 1u : 0u,
                                    outlineRequested ? 1u : 0u, outlineRgb,
                                    texturesSeq, texturesRequested ? 1u : 0u, MyClientFeatures::EncodeAutoRqField(autoRqMode, renderChunks, renderCounter) | (leftHandRequested ? MyClientFeatures::LeftHandBit : 0u) | (hudMenuOpen ? MyClientFeatures::HudMenuBit : 0u) | (shulkerHoverRequested ? MyClientFeatures::ShulkerHoverBit : 0u) | frameGenBits, zoomFactorX100 | (hitboxRequested ? 0x10000u : 0u) | ((hitboxFilter & 0xFu) << 17) | (nickRequested ? 0x200000u : 0u) | (replayRequested ? 0x400000u : 0u) | (replayBuffer ? 0x20000000u : 0u) | (hitEffectCrit ? 0x40000000u : 0u) | (hitEffectMagic ? 0x80000000u : 0u) | entityDistanceBits};
    }
    features.Unlock();
}
}
void SetZoomRequested(bool requested, uint32_t key, uint32_t factorX100) {
    zoomRequested = requested; zoomKey = key; zoomFactorX100 = factorX100;
    WriteCommand();
}
bool IsFullbrightApplied() {
    return MyClientFeatures::Fresh(featureSnapshot.fullbrightAt, GetTickCount64(), MyClientFeatures::CommandLeaseMs) &&
           MyClientFeatures::HasFlag(featureSnapshot.fullbrightActive, MyClientFeatures::FlagFullbright);
}
bool IsFullbrightAvailable() { return MyClientFeatures::HasFlag(featureSnapshot.fullbrightAvailable, MyClientFeatures::FlagFullbright); }
// Name tag status rides on the FPS heartbeat (the DLL refreshes it every frame).
bool IsNametagApplied() {
    return MyClientFeatures::Fresh(featureSnapshot.fpsAt, GetTickCount64(), MyClientFeatures::CommandLeaseMs * 4) &&
           MyClientFeatures::HasFlag(featureSnapshot.fullbrightActive, MyClientFeatures::FlagNametag);
}
// Where Ping can be measured: the server the game is connected to (RakNet only).
bool GetPingTarget(std::string& host, int& port) {
    char text[64]; uint32_t number = 0;
    if (!MyClientFeatures::PingTarget(serverSnapshot, GetTickCount64(), text, number)) return false;
    host = text; port = static_cast<int>(number);
    return true;
}
// Calls per second of each hooked game function; false while the DLL's counters are not arriving.
bool GetHookRates(MyClientFeatures::Stats& rates) {
    if (!MyClientFeatures::Fresh(statsSnapshot.at, GetTickCount64(), 3000) || !statsPreviousAt || !statsRate.at) return false;
    rates = statsRate; return true;
}
// What the DLL says it holds for Block Outline (lease state and colour), for the detail screen.
bool GetOutlineEcho(bool& leaseFresh, uint32_t& rgb) {
    if (!MyClientFeatures::Fresh(statsSnapshot.at, GetTickCount64(), 3000)) return false;
    leaseFresh = (statsSnapshot.outlineEcho & 0x01000000u) != 0;
    rgb = statsSnapshot.outlineEcho & 0xFFFFFFu;
    return true;
}
void SetAutoRq(uint32_t mode) { autoRqMode = mode; WriteCommand(); }
void SetRenderDistance(uint32_t chunks, uint32_t counter) { renderChunks = chunks; renderCounter = counter; WriteCommand(); }
// Texture-pack switching: the HUD only reports "on" and a request counter; the DLL does the reload.
void SetTextures(bool on, uint32_t sequence) {
    texturesRequested = on;
    texturesSeq = sequence;
    WriteCommand();
}
bool IsTexturesAvailable() { return MyClientFeatures::HasFlag(featureSnapshot.fullbrightAvailable, MyClientFeatures::FlagTextures); }
bool GetTextureReloads(uint32_t& count) {
    if (!MyClientFeatures::Fresh(statsSnapshot.at, GetTickCount64(), 3000)) return false;
    count = statsSnapshot.texReloads;
    return true;
}
bool IsNametagAvailable() { return MyClientFeatures::HasFlag(featureSnapshot.fullbrightAvailable, MyClientFeatures::FlagNametag); }
bool IsOutlineAvailable() { return MyClientFeatures::HasFlag(featureSnapshot.fullbrightAvailable, MyClientFeatures::FlagOutline); }
bool IsOutlineRunning() {
    return MyClientFeatures::Fresh(featureSnapshot.fpsAt, GetTickCount64(), MyClientFeatures::CommandLeaseMs * 4) &&
           MyClientFeatures::HasFlag(featureSnapshot.fullbrightActive, MyClientFeatures::FlagOutline);
}
bool IsOutlineColored() {
    return MyClientFeatures::Fresh(featureSnapshot.fpsAt, GetTickCount64(), MyClientFeatures::CommandLeaseMs * 4) &&
           MyClientFeatures::HasFlag(featureSnapshot.fullbrightActive, MyClientFeatures::FlagOutlineColored);
}
void SetOutlineRequested(bool requested, uint32_t rgb) {
    outlineRequested = requested;
    outlineRgb = rgb & 0xFFFFFFu;
    WriteCommand();
}
void SetEntityDistance(uint32_t blocks, bool players) {
    entityDistanceBits = MyClientFeatures::EncodeEntityDistance(blocks, players);
}
void SetOptimize(bool cullMobs, bool cullItems, bool cullMisc, bool hideNameTags, uint32_t playerBlocks) {
    optimizeBits = MyClientFeatures::EncodeOptimize(cullMobs, cullItems, cullMisc, hideNameTags, playerBlocks);
}
void SetReplayRequested(bool requested) {
    replayRequested = requested;
    WriteCommand();
}
void SetReplayBuffer(bool keep) {
    replayBuffer = keep;
}
DWORD GamePid() { return processId; }
void SetLeftHand(bool on) {
    leftHandRequested = on;
}
void SetFrameGen(bool on, int multiplier, int method, bool pacing) {
    frameGenBits = on ? MyClientFeatures::EncodeFrameGen(static_cast<uint32_t>(multiplier), static_cast<uint32_t>(method), pacing) : 0u;
    frameGenWide = on ? static_cast<uint32_t>(std::clamp(multiplier, 2, static_cast<int>(MyClientFeatures::FrameGenMaxMultiplier))) : 0u;
}
bool GetFrameGen(float& presentedFps, uint32_t& state) {
    presentedFps = 0; state = MyClientFeatures::FrameGenOff;
    if (!MyClientFeatures::Fresh(featureSnapshot.fpsAt, GetTickCount64()) || !std::isfinite(framePresentedFps) ||
        framePresentedFps < 0 || framePresentedFps > 10000 || frameGenState > MyClientFeatures::FrameGenUnavailable) return false;
    presentedFps = framePresentedFps; state = frameGenState; return true;
}
void SetShulkerHover(bool on) {
    shulkerHoverRequested = on;
}
void SetHudMenuOpen(bool open) {
    if (hudMenuOpen == open) return;
    hudMenuOpen = open;
    WriteCommand();   // at once: the game opens its pause screen as soon as the menu takes the focus
}
bool IsGameMenuOpen() {
    if (!MyClientFeatures::Fresh(statsSnapshot.at, GetTickCount64(), 1000)) return false;
    return MyClientFeatures::HasFlag(statsSnapshot.reserved, MyClientFeatures::GameScreenKnown) &&
           MyClientFeatures::HasFlag(statsSnapshot.reserved, MyClientFeatures::GameMenuOpen);
}
void SetHitEffect(bool on, bool crit, bool magic) {
    hitEffectCrit = on && crit;
    hitEffectMagic = on && magic;
}
bool GetReplayStatus(ReplayStatus& out) {
    const auto& t = featureSnapshot;
    if (!MyClientFeatures::Fresh(t.recAt, GetTickCount64(), 4000)) return false;
    out.bytes = t.recBytes; out.packets = t.recPackets; out.seconds = t.recSeconds;
    std::memcpy(out.path, replayPath, sizeof(out.path));
    return true;
}
void SetNickRequested(bool requested, const char* utf8Name) {
    nickRequested = requested;
    std::memset(nickText, 0, sizeof(nickText));
    if (utf8Name) {
        size_t n = std::strlen(utf8Name);
        if (n > sizeof(nickText) - 1) { n = sizeof(nickText) - 1; while (n > 0 && (static_cast<unsigned char>(utf8Name[n]) & 0xC0) == 0x80) n--; }
        std::memcpy(nickText, utf8Name, n);
    }
    WriteCommand();
}
void SetHitboxRequested(bool requested, uint32_t filter, const uint32_t* colors) {
    hitboxRequested = requested; hitboxFilter = filter;
    for (int k = 0; k < 4; k++) hitboxRgb[k] = colors ? colors[k] : 0u;
    WriteCommand();
}
void SetNametagRequested(bool requested) {
    nametagRequested = requested;
    WriteCommand();
}
void SetFullbrightRequested(bool requested) {
    fullbrightRequested = requested;
    WriteCommand();
}
bool IsZoomRequested() { return zoomRequested; }
void SetPartyRequested(bool enabled, const std::vector<std::wstring>& tags) {
    MyClientParty::Config config{};
    config.at = GetTickCount64(); config.enabled = enabled ? 1u : 0u;
    // Reject the whole config on invalid entries or overflow, never silently
    // accept only a prefix of the user's registered list.
    if (tags.size() > MyClientParty::MaxPlayers) config.enabled = 0;
    else for (const auto& wide : tags) {
        std::string tag;
        for (wchar_t c : wide) { if (static_cast<uint32_t>(c) > 127) { config.enabled = 0; break; } tag += static_cast<char>(c); }
        if (!MyClientParty::ValidTag(tag)) { config.enabled = 0; break; }
        std::memcpy(config.tags[config.count++], tag.c_str(), tag.size()+1);
    }
    if (!party.Data() || !party.Lock()) return;
    if (MyClientParty::Valid(*party.Data(), processId)) party.Data()->config = config;
    party.Unlock();
}
const wchar_t* GetPartyStatusText() {
    if (!party.Data() || !MyClientParty::Fresh(partySnapshot.at, GetTickCount64(), 2000)) return L"Hive: waiting for DLL / world";
    switch (partySnapshot.status) {
    case MyClientParty::Status::Waiting: return L"Hive: waiting for world";
    case MyClientParty::Status::Disabled: return L"Hive: off / check registered tags";
    case MyClientParty::Status::OtherServer: return L"Hive: server address not confirmed";
    case MyClientParty::Status::Ready: return L"Hive: waiting for registered invitation";
    case MyClientParty::Status::Sent: return L"Hive: accept command sent (unconfirmed)";
    case MyClientParty::Status::UnknownFormat: return L"Hive: unknown invite format (ignored)";
    case MyClientParty::Status::Failed: return L"Hive: command send failed";
    }
    return L"Hive: invalid status";
}
}

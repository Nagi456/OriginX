// g++ -std=c++17 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/launcher_remote_stop.cpp -o /tmp/remote-stop
#include "../launcher/RemoteStop.h"
#include <cstdio>

using namespace MyClientLauncher;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static bool Throws(const char* text)
{
    try { ParseRemoteStop(Json::parse(text)); return false; } catch (const std::exception&) { return true; }
}

int main()
{
    // no block: nothing stopped
    const auto none = ParseRemoteStop(Json::parse(R"({"schemaVersion":1,"build":41})"));
    CHECK(StopFor(none, 41) == StopCause::None);
    CHECK(StopMessage(none, StopCause::None, 41, false).empty());

    const auto stop = ParseRemoteStop(Json::parse(R"({"stop":{"minLauncherBuild":42,"disabledBuilds":[44]}})"));
    CHECK(StopFor(stop, 41) == StopCause::TooOld);
    CHECK(StopFor(stop, 42) == StopCause::None);
    CHECK(StopFor(stop, 44) == StopCause::Disabled);
    CHECK(StopFor(stop, 45) == StopCause::None);
    CHECK(StopFor(stop, 0) == StopCause::None);   // a local build: only the kill switch
    CHECK(StopMessage(stop, StopCause::TooOld, 41, false).find("build 41") != std::string::npos);
    CHECK(StopMessage(stop, StopCause::Disabled, 44, true).find("Minecraft starts without it") != std::string::npos);

    const auto kill = ParseRemoteStop(Json::parse(R"({"stop":{"killSwitch":{"enabled":true,"reason":{"ja":"不具合のため停止中","en":"Stopped for a bug"}}}})"));
    CHECK(StopFor(kill, 0) == StopCause::KillSwitch);
    CHECK(StopFor(kill, 50) == StopCause::KillSwitch);
    CHECK(StopMessage(kill, StopCause::KillSwitch, 50, false).rfind("不具合のため停止中", 0) == 0);
    CHECK(StopMessage(kill, StopCause::KillSwitch, 50, true).rfind("Stopped for a bug", 0) == 0);

    const auto off = ParseRemoteStop(Json::parse(R"({"stop":{"killSwitch":{"enabled":false}}})"));
    CHECK(StopFor(off, 41) == StopCause::None);
    const auto custom = ParseRemoteStop(Json::parse(R"({"stop":{"minLauncherBuild":50,"reason":{"ja":"更新して","en":"Update"}}})"));
    CHECK(StopMessage(custom, StopCause::TooOld, 41, false).rfind("更新して", 0) == 0);

    // the kill switch never goes on without a reason in both languages; malformed blocks are refused
    CHECK(Throws(R"({"stop":{"killSwitch":{"enabled":true}}})"));
    CHECK(Throws(R"({"stop":{"killSwitch":{"enabled":true,"reason":{"ja":"x","en":""}}}})"));
    CHECK(Throws(R"({"stop":{"killSwitch":{"enabled":"yes","reason":{"ja":"x","en":"y"}}}})"));
    CHECK(Throws(R"({"stop":{"minLauncherBuild":-1}})"));
    CHECK(Throws(R"({"stop":{"disabledBuilds":"41"}})"));
    CHECK(Throws(R"({"stop":[]})"));
    CHECK(Throws(R"({"stop":{"reason":"text"}})"));

    // revokedTestKeys: test keys up to the highest listed are refused; no stop block needed
    CHECK(ParseRemoteStop(Json::parse(R"({"revokedTestKeys":[101]})")).testKeysFrom == 102);
    CHECK(ParseRemoteStop(Json::parse(R"({"revokedTestKeys":[101,103,102]})")).testKeysFrom == 104);
    CHECK(ParseRemoteStop(Json::parse(R"({"build":41})")).testKeysFrom == 0);
    CHECK(Throws(R"({"revokedTestKeys":[1]})"));       // a stable key is never retired this way
    CHECK(Throws(R"({"revokedTestKeys":101})"));
    CHECK(Throws(R"({"revokedTestKeys":["101"]})"));

    if (failures) return 1;
    std::printf("PASS\n");
    return 0;
}

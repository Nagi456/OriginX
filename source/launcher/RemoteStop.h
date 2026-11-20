#pragma once
// Remote stop (release-checklist 2026-11-20, required item 2; author's answers the same day).
// The signed launcher list (stable/launcher.json, kind launcher/stable, ManifestSignature.h) may carry
//   "stop": { "minLauncherBuild": 41, "disabledBuilds": [42, 43],
//             "reason": { "ja": "...", "en": "..." },                       // optional, for the two above
//             "killSwitch": { "enabled": true, "reason": { "ja": "...", "en": "..." } } }
// It is read at every start. A list that could not be fetched leaves the last one whose signature was checked
// (saved beside the settings); with none saved nothing is stopped and the log says so.
// What a stop does: OriginX is not injected / loaded, the game itself still starts, and the reason is always shown.
// The launcher's own update keeps working, so a newer build can arrive and lift a stop on an old one.
// Next to "stop", "revokedTestKeys": [101] retires leaked test signing keys (2026-11-20-test-signing-key-design.md
// §4): every test key up to the highest listed is refused from then on (the mark only goes up, ManifestSignature.h).
// Older launchers ignore "stop" (ParseLauncherInfo only reads its own fields).
#include "../Lang.h"
#include "Updates.h"

#include <algorithm>
#include <string>
#include <vector>

namespace MyClientLauncher {

struct StopText { std::string ja, en; };

struct RemoteStop
{
    uint64_t minLauncherBuild = 0;
    std::vector<uint64_t> disabledBuilds;
    StopText reason;            // for minLauncherBuild / disabledBuilds; empty: the default text
    bool killSwitch = false;
    StopText killReason;        // required (both languages) when killSwitch is on
    uint32_t testKeysFrom = 0;  // from revokedTestKeys: the lowest test key still accepted (0: none revoked)
};

inline StopText ParseStopText(const Json& value, bool required)
{
    StopText text;
    if (value.is_null() && !required) return text;
    if (!value.is_object() || !value.contains("ja") || !value.contains("en") || !value.at("ja").is_string() || !value.at("en").is_string())
        throw std::runtime_error("stop: a reason needs \"ja\" and \"en\" strings");
    text.ja = value.at("ja").get<std::string>();
    text.en = value.at("en").get<std::string>();
    if (required && (text.ja.empty() || text.en.empty())) throw std::runtime_error("stop: the kill switch needs a reason in both languages");
    if (text.ja.size() > 1000 || text.en.size() > 1000) throw std::runtime_error("stop: a reason is too long");
    return text;
}

// The "stop" block of a launcher list (already signature-checked). No block: nothing stopped.
// A block that is malformed throws: the list is then treated as unreadable, like any other broken list.
inline RemoteStop ParseRemoteStop(const Json& doc)
{
    RemoteStop stop;
    if (doc.is_object() && doc.contains("revokedTestKeys"))
    {
        const Json& revoked = doc.at("revokedTestKeys");
        if (!revoked.is_array() || revoked.size() > 100) throw std::runtime_error("revokedTestKeys is not a list");
        for (const auto& id : revoked)
        {
            const uint64_t key = PositiveNumber(id);
            if (key < 101 || key >= 0xFFFF) throw std::runtime_error("revokedTestKeys: only test keys (101 and up)");
            stop.testKeysFrom = std::max<uint32_t>(stop.testKeysFrom, static_cast<uint32_t>(key + 1));
        }
    }
    if (!doc.is_object() || !doc.contains("stop")) return stop;
    const Json& block = doc.at("stop");
    if (!block.is_object()) throw std::runtime_error("stop: not an object");
    if (block.contains("minLauncherBuild")) stop.minLauncherBuild = PositiveNumber(block.at("minLauncherBuild"));
    if (block.contains("disabledBuilds"))
    {
        const Json& builds = block.at("disabledBuilds");
        if (!builds.is_array() || builds.size() > 1000) throw std::runtime_error("stop: disabledBuilds is not a list");
        for (const auto& build : builds) stop.disabledBuilds.push_back(PositiveNumber(build));
    }
    if (block.contains("reason")) stop.reason = ParseStopText(block.at("reason"), false);
    if (block.contains("killSwitch"))
    {
        const Json& kill = block.at("killSwitch");
        if (!kill.is_object() || !kill.contains("enabled") || !kill.at("enabled").is_boolean())
            throw std::runtime_error("stop: killSwitch needs \"enabled\" true or false");
        stop.killSwitch = kill.at("enabled").get<bool>();
        if (stop.killSwitch || kill.contains("reason")) stop.killReason = ParseStopText(kill.contains("reason") ? kill.at("reason") : Json(), stop.killSwitch);
    }
    return stop;
}

enum class StopCause { None, KillSwitch, TooOld, Disabled };

// build 0 (a local build, never self-updated) is only stopped by the kill switch.
inline StopCause StopFor(const RemoteStop& stop, uint64_t build)
{
    if (stop.killSwitch) return StopCause::KillSwitch;
    if (build == 0) return StopCause::None;
    if (std::find(stop.disabledBuilds.begin(), stop.disabledBuilds.end(), build) != stop.disabledBuilds.end()) return StopCause::Disabled;
    if (stop.minLauncherBuild && build < stop.minLauncherBuild) return StopCause::TooOld;
    return StopCause::None;
}

inline const char* StopCauseName(StopCause cause)
{
    switch (cause)
    {
    case StopCause::KillSwitch: return "kill switch";
    case StopCause::TooOld: return "below minLauncherBuild";
    case StopCause::Disabled: return "in disabledBuilds";
    default: return "none";
    }
}

// The text shown to the user (UTF-8): the list's reason in the UI language, then what still works.
inline std::string StopMessage(const RemoteStop& stop, StopCause cause, uint64_t build, bool english)
{
    const auto pick = [&](const StopText& text) { return english ? text.en : text.ja; };
    std::string head;
    if (cause == StopCause::KillSwitch) head = pick(stop.killReason);
    else if (!pick(stop.reason).empty()) head = pick(stop.reason);
    else if (cause == StopCause::TooOld || cause == StopCause::Disabled)
        head = english ? "This launcher (build " + std::to_string(build) + ") has been stopped. Install the new launcher from the site."
                       : "このランチャー（build " + std::to_string(build) + "）は停止されています。サイトから新しいランチャーを入れてください。";
    if (cause == StopCause::None) return {};
    return head + (english ? "\nOriginX is not loaded; Minecraft starts without it." : "\nOriginX は読み込まず、Minecraft だけを起動します。");
}

} // namespace MyClientLauncher

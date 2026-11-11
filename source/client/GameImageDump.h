#pragma once
// Game image dump: see client/GameImageDump.cpp.
#include <functional>
#include <string>

namespace MyClientGame {
// Developer opt-in only (ORIGINX_DIAGNOSTICS=1 before process start), keeping scans out of normal gameplay.
bool DiagnosticsEnabled();
// Writes the game's executable as it sits in memory once per game build (call from ll_mod_load, before
// MyClient's hooks). log gets one line.
void DumpGameImage(const std::string& gameVersion, const std::function<void(const std::string&)>& log);
// 60 s later on a background thread: where the game's code differs from that image (the hook patches), once
// per game build.
void StartPatchScan(std::string gameVersion, std::function<void(const std::string&)> log);
} // namespace MyClientGame

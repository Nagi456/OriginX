#pragma once
// The start-up self check (client/GameSigs.cpp): logs/startup.log, logs/sigcheck.log, and the go / no-go decision.
// Called by MyClientStart::Load (MyClientData.cpp) before anything is hooked, whoever loaded the DLL.
#include <cstdint>
#include <string>

namespace MyClientSigs {
struct Verdict
{
    bool start = false;     // a supported build and every fatal signature OK: MyClient may hook
    int build = -1;         // index into GameSigs::kBuilds, -1 = not supported
    int ok = 0, ng = 0, fatalNg = 0, notUsed = 0;
    std::string version, why;
};
// One line of logs/startup.log (the first call of the process starts a new file, the old one is kept as
// startup.previous.log). Safe in DllMain: kernel32 only.
void StartupLine(const std::string& text);
void SigcheckLine(const std::string& text, bool fresh);
// Checks every signature of sigs.json against the running game and writes sigcheck.log. NG ones are taken out of the
// address table (their features stay off). `scanUnknown`: on an unsupported build, search the newest patterns too.
Verdict Check(const char* dllFor, bool scanUnknown);
int RunningBuild();   // index into GameSigs::kBuilds after Check, -1 = not supported, -2 = not checked yet
// A version-dependent number from sigs.json's "offsets" for the running build, `fallback` when it has none.
uint32_t OffsetOr(const char* name, uint32_t fallback);
// The reason MyClient did not start, in a message box on its own thread.
void ShowNotStarted(const std::string& version, const std::string& why);
}

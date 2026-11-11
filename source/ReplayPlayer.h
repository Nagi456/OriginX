#pragma once
// Replay playback in the game: runs ReplayServer.h's Session on a UDP socket on 127.0.0.1 (its own thread) and
// asks the game to join it. Called from the HUD's UI thread only.
#include <cstdint>
#include <string>

namespace ReplayPlayer {

enum class Phase { Idle, Waiting, Joining, Playing, Finished };

struct Status {
    Phase phase = Phase::Idle;
    uint64_t positionMs = 0, lengthMs = 0;
    bool paused = false;
    float speed = 1.f;
    bool packsRequested = false;   // the game asked for resource packs the recording cannot give
    bool freeCamera = false;       // the viewer flies freely and sees the recorded player (ReplayServer.h FreeCam)
    bool rewinding = false;        // a jump back is under way (the game connects again)
    std::wstring file, error;   // error: why the last playback ended early (empty = it did not)
};

// Opens the recording and starts the local server (Port()). The game is taken there by the DLL
// (MinecraftDataProvider::RequestReplayJoin) or, with an older DLL, by OpenJoinLink.
// False (with the reason in `error`) when the file cannot be played or no port is free.
bool Start(const std::wstring& path, std::wstring& error);
uint16_t Port();   // 0 = no server
// Opens minecraft://connect for the server. False (server stopped, reason in `error`) when Windows cannot open it.
bool OpenJoinLink(std::wstring& error);
// Ends the playback with a reason (kept in Get().error).
void Fail(const std::wstring& error);
// Ends the playback (the game shows its disconnected screen) and closes the server.
void Stop();
Status Get();
bool Active();   // a playback is running (the HUD keeps the recorder off meanwhile)
void SetPaused(bool paused);
void SetSpeed(float speed);
// Jumps to a time of the recording (ms from where recording was started). Back in time, the game connects again.
void SeekTo(uint64_t positionMs);
void SetFreeCamera(bool on);   // on by default; while playing, the game connects again and goes on from the same time

}  // namespace ReplayPlayer

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
    std::wstring file, error;   // error: why the last playback ended early (empty = it did not)
};

// Opens the recording, starts the local server and opens minecraft://connect for it.
// False (with the reason in `error`) when the file cannot be played or no port is free.
bool Start(const std::wstring& path, std::wstring& error);
// Ends the playback (the game shows its disconnected screen) and closes the server.
void Stop();
Status Get();
bool Active();   // a playback is running (the HUD keeps the recorder off meanwhile)
void SetPaused(bool paused);
void SetSpeed(float speed);

}  // namespace ReplayPlayer

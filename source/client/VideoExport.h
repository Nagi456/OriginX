#pragma once
// Replay video export, the encoder (2026-11-12, docs/superpowers/specs/2026-11-12-replay-camera-editor-design.md
// part 5): BGRA frames in, an H.264 .mp4 (picture only) out, through Windows' Media Foundation. Media Foundation is
// loaded only when an export starts (Windows N editions lack it: the DLL must still load there).
#include <cstdint>
#include <string>

namespace MyClientVideo {
// Starts a file. width/height even; fps 30 or 60; bitrate in bits per second. False with the reason in `error`.
bool Begin(const std::wstring& path, int width, int height, int fps, uint32_t bitrate, std::string& error);
// Adds frame `index` (0, 1, ...): a BGRA picture of any size (srcPitch in bytes), shrunk to the video's size.
bool Write(const uint32_t* bgra, int srcWidth, int srcHeight, int srcPitch, uint32_t index, std::string& error);
// Ends the file (true when it is complete).
bool Finish(std::string& error);
// Stops and deletes the file.
void Cancel();
bool Running();
}

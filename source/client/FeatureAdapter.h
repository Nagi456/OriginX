#pragma once
#include <cstdint>
#include "FeatureData.h"
namespace MyClientGame {
bool EnableFeatures();
// In-game drawing hooks (MyClientCompositor), from ll_mod_load: before the game makes its swap chain, so it is made
// with ALLOW_TEARING and the game draws into MyClient's copies of its back buffers. EnableFeatures calls it again.
bool StartDrawing(bool probe);
void DisableFeatures();
uint32_t HitboxFilter();  // which hitboxes: 1 players, 2 items, 4 entities, 8 mobs
void HitboxColors(uint32_t out[4]);   // their colours by kind (player, item, entity, mob), 0xRRGGBB
bool ShulkerHoverWanted();   // Shulker Preview is on: hide the game's tooltip over shulker boxes (bit 26 of command.autoRq)
bool HitboxRequested();   // the HUD wants Hitbox data (bit 16 of the command record)
// Frame Generation request: bits 27..31 of command.autoRq (MyClientFeatures::FrameGenMultiplierOf...), 0 = off or
// lease expired. `paused`: not playing right now (menu, loading, title screen) - no made-up frames then.
uint32_t FrameGenField(bool& paused);
// Frames shown per game frame for that request (x2..x10 from Shared.frameGenMultiplier; 1 = off).
uint32_t FrameGenMultiplier(uint32_t field);
// Game tick: the actor of each hitbox just handed to the Compositor and its position (x, y, z each) at that
// moment. Every frame the boxes are then moved to where the game draws those actors (between two ticks).
void NoteHitboxActors(void* const* actors, const float* positions, uint32_t count);
bool OutlineWanted();     // Block Outline is on (fresh HUD lease)
uint32_t OutlineRgb();    // its colour, 0xRRGGBB
uint32_t AutoRqMode();  // Auto RQ mode requested by the HUD (0 = off or lease expired)
char LocalTeamColor();
void ApplyRenderDistance();  // applies a Render Distance request from the HUD; game thread  // colour code of your own name tag (Hive team colour), 0 = unknown; game thread
void SampleArmor();
void SampleClock(int time, const MyClientFeatures::Debug& debug, const MyClientFeatures::Look& look, uint32_t health);
void ClearArmor();
}

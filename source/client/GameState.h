#pragma once
// The game's client and world, found by MyClient's own hooks (2026-11-05, step 3 of leaving LeviLamina on 26.51).
//
// LeviLamina's ll::service::getClientInstance() keeps the ClientInstance its own constructor hook saw, and
// getMultiPlayerLevel() asks that client's Minecraft for its level (both are LeviLamina.dll exports). Instead:
//   Client()     = GameRenderer::mPrimaryClient, read in the per-frame hook (FeatureAdapter.cpp FrameHook);
//                  ClientInstance's first base is IClientInstance, so the pointer is the same object.
//   WorldLevel() = the ClientLevel whose _subTick last ran (MyClientData.cpp LevelTickHook), cleared when the
//                  client leaves the world (ClientInstance::onLevelExit, LevelExitHook). ClientLevel's first base
//                  is Level. Sample() already checked that the ticked level is getMultiPlayerLevel()'s.
// Both return optional_ref (header only, as LeviLamina's did) so the callers did not change.
#include <atomic>

#include "mc/deps/core/utility/optional_ref.h"

class ClientInstance;
class Level;

namespace MyClientGame {

inline std::atomic<ClientInstance*> ownClient{nullptr};
inline std::atomic<Level*> ownLevel{nullptr};

inline optional_ref<ClientInstance> Client() { return ownClient.load(std::memory_order_acquire); }
inline optional_ref<Level> WorldLevel() { return ownLevel.load(std::memory_order_acquire); }

inline void NoteClient(ClientInstance* client)
{
    if (client && ownClient.load(std::memory_order_relaxed) != client) ownClient.store(client, std::memory_order_release);
}
inline void NoteWorldTick(Level* level)
{
    if (level && ownLevel.load(std::memory_order_relaxed) != level) ownLevel.store(level, std::memory_order_release);
}
inline void NoteWorldExit() { ownLevel.store(nullptr, std::memory_order_release); }

}  // namespace MyClientGame

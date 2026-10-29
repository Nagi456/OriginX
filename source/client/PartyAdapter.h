#pragma once
#include <cstdint>
#include <string>
namespace MyClientGame {
// The server the game is connected to (resolved IP when known, else the typed address), its port and
// the transport layer (0 = RakNet). False when not connected or when the connection lookup is not ready.
bool CurrentServer(std::string& host, uint32_t& port, uint32_t& transport);
bool EnableParty();
void DisableParty();
void TickParty();
void TickAutoRq();
void ClearParty();
}

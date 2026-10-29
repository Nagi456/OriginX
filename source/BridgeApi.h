#pragma once

#include <stdint.h>

// Stable C ABI between the game adapter and the external HUD.
// No STL objects, pointers, HWNDs or game objects cross this boundary.
#define MYCLIENT_ABI_VERSION 2u
#define MYCLIENT_HAS_ARROWS 1u
#define MYCLIENT_HAS_TARGET 2u
#define MYCLIENT_HAS_PROJECTION 4u
#define MYCLIENT_HAS_BOUNDS 8u

typedef struct MyClientPoint {
    float x;
    float y;
} MyClientPoint;

typedef struct MyClientFrame {
    uint32_t structSize;
    uint32_t abiVersion;
    uint32_t flags;
    uint32_t arrowCount;
    int32_t blockX;
    int32_t blockY;
    int32_t blockZ;
    // Optional world bounds; valid only with HAS_BOUNDS. Target coordinates
    // remain available even when bounds or projection have not been captured.
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    // Normalized Minecraft client-area coordinates, NOT desktop pixels.
    // Corners: bottom 0-1-2-3, top 4-5-6-7, vertical 0-4 etc.
    // Only set HAS_PROJECTION if all corners can be projected safely.
    MyClientPoint points[8];
} MyClientFrame;

#ifdef _WIN32
#include <windows.h>
#ifdef MYCLIENT_BRIDGE_BUILD
#define MYCLIENT_API __declspec(dllexport)
#else
#define MYCLIENT_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Call outside DllMain, from a verified game adapter. FALSE => GetLastError().
MYCLIENT_API BOOL WINAPI MyClientBridgeInitialize(void);
MYCLIENT_API BOOL WINAPI MyClientBridgePublish(const MyClientFrame* frame);
// Stop/join all adapter callbacks before Shutdown and FreeLibrary.
MYCLIENT_API void WINAPI MyClientBridgeShutdown(void);
#ifdef __cplusplus
}
#endif
#endif

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
compiler="${CXX:-x86_64-w64-mingw32-g++}"
if [[ "$("$compiler" -dumpmachine)" != x86_64-w64-mingw32* ]]; then
    echo 'Windows x64 MinGW-w64 compiler required.' >&2
    exit 1
fi
mkdir -p build
x86_64-w64-mingw32-windres -O coff -o build/myclient_res.o myclient.rc
flags=(-std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -DNOMINMAX -D_WIN32_WINNT=0x0601 -static -static-libgcc -static-libstdc++)
"$compiler" "${flags[@]}" -DMYCLIENT_BRIDGE_BUILD -shared MyClientBridge.cpp \
    -o build/MyClientBridge.dll -Wl,--exclude-all-symbols \
    -Wl,--out-implib,build/libMyClientBridge.dll.a
"$compiler" "${flags[@]}" MyClient.cpp MinecraftDataProvider.cpp PingMonitor.cpp ReplayPlayer.cpp \
    build/myclient_res.o -o build/MyClient.exe -mwindows -luser32 -lgdi32 -lshell32 -lws2_32 -lwinhttp -lwinmm -lole32 -luuid -lwindowscodecs -lmsimg32
"$compiler" "${flags[@]}" -Ilauncher/vendor -I. launcher/MyClientLauncher.cpp launcher/vendor/tinyxml2/tinyxml2.cpp build/myclient_res.o \
    -o build/MyClientLauncher.exe -municode -mwindows -luser32 -lgdi32 -lcomdlg32 -lshell32 -lole32 -luuid -lbcrypt -lversion -lwinhttp
"$compiler" "${flags[@]}" -I. tests/bridge_smoke.cpp MinecraftDataProvider.cpp \
    -o build/bridge_smoke.exe build/libMyClientBridge.dll.a -luser32
echo 'Built Windows x64 launcher, bridge DLL, overlay, and bridge smoke test in build/.'
echo 'For the Minecraft 26.51 data mod, also run tools/build_client.py; see README.md.'

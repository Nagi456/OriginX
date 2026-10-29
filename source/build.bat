@echo off
setlocal
cd /d "%~dp0"
where g++ >nul 2>nul
if errorlevel 1 (
    echo Install MinGW-w64 x64 and add its bin directory to PATH.
    exit /b 1
)
g++ -dumpmachine | findstr /b /c:"x86_64-w64-mingw32" >nul
if errorlevel 1 (
    echo Windows x64 MinGW-w64 compiler required.
    exit /b 1
)
if not exist build mkdir build
windres -O coff -o build\myclient_res.o myclient.rc
set "FLAGS=-std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -DNOMINMAX -D_WIN32_WINNT=0x0601 -static -static-libgcc -static-libstdc++"
g++ %FLAGS% -DMYCLIENT_BRIDGE_BUILD -shared MyClientBridge.cpp -o build\MyClientBridge.dll -Wl,--exclude-all-symbols -Wl,--out-implib,build\libMyClientBridge.dll.a
if errorlevel 1 exit /b 1
g++ %FLAGS% MyClient.cpp MinecraftDataProvider.cpp PingMonitor.cpp ReplayPlayer.cpp build\myclient_res.o -o build\MyClient.exe -mwindows -luser32 -lgdi32 -lshell32 -lws2_32 -lwinhttp -lwinmm -lole32 -luuid -lwindowscodecs -lmsimg32
if errorlevel 1 exit /b 1
g++ %FLAGS% -Ilauncher\vendor -I. launcher\MyClientLauncher.cpp launcher\vendor\tinyxml2\tinyxml2.cpp build\myclient_res.o -o build\MyClientLauncher.exe -municode -mwindows -luser32 -lgdi32 -lcomdlg32 -lshell32 -lole32 -luuid -lbcrypt -lversion -lwinhttp
if errorlevel 1 exit /b 1
g++ %FLAGS% -I. tests\bridge_smoke.cpp MinecraftDataProvider.cpp -o build\bridge_smoke.exe build\libMyClientBridge.dll.a -luser32
if errorlevel 1 exit /b 1
echo Built Windows x64 launcher, bridge DLL, overlay, and bridge smoke test in build\.
echo For the Minecraft 26.51 data mod, also run tools\build_client.py. See README.md.
exit /b 0

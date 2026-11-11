// MyClient's own log: see Log.h.
#include "Log.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

#ifndef MYCLIENT_LOG_TO_LEVILAMINA
#define MYCLIENT_LOG_TO_LEVILAMINA (!MYCLIENT_STANDALONE)   // without LeviLamina there is only the own file
#endif
#if MYCLIENT_LOG_TO_LEVILAMINA
#include "ll/api/mod/NativeMod.h"
#endif
#ifndef MYCLIENT_GAME_VERSION
#define MYCLIENT_GAME_VERSION "unknown"
#endif

namespace {
std::mutex mutex;
FILE* file = nullptr;
bool opened = false;
std::wstring path;

void Open()
{
    opened = true;
    wchar_t local[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!size || size >= MAX_PATH) return;
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::path(local) / L"MyClient" / L"logs";
    std::filesystem::create_directories(folder, error);
    const std::wstring name = L"originx-" + std::filesystem::path(MYCLIENT_GAME_VERSION).wstring();
    const std::filesystem::path current = folder / (name + L".log");
    std::filesystem::rename(current, folder / (name + L".previous.log"), error);   // the last run's, if any
    file = _wfopen(current.c_str(), L"wb");
    if (file) path = current.wstring();
}

const char* Name(MyClientLog::Level level)
{
    switch (level)
    {
    case MyClientLog::Level::Warn: return "WARN";
    case MyClientLog::Level::Error: return "ERROR";
    default: return "INFO";
    }
}
} // namespace

namespace MyClientLog {
void Write(Level level, std::string_view text)
{
    try
    {
        {
            std::lock_guard lock(mutex);
            if (!opened) Open();
            if (file)
            {
                SYSTEMTIME now{};
                GetLocalTime(&now);
                std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s %.*s\r\n", now.wYear, now.wMonth, now.wDay, now.wHour,
                             now.wMinute, now.wSecond, now.wMilliseconds, Name(level), static_cast<int>(text.size()), text.data());
                std::fflush(file);   // a crash or a closed game keeps every line written so far
            }
        }
#if MYCLIENT_LOG_TO_LEVILAMINA
        if (auto mod = ll::mod::NativeMod::current())
        {
            auto& logger = mod->getLogger();
            if (level == Level::Error) logger.error("{}", text);
            else if (level == Level::Warn) logger.warn("{}", text);
            else logger.info("{}", text);
        }
#endif
    }
    catch (...) {}
}

const wchar_t* FilePath()
{
    std::lock_guard lock(mutex);
    return path.c_str();
}
} // namespace MyClientLog

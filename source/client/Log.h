#pragma once
// MyClient's own log (2026-11-05, step 1 of leaving LeviLamina on 26.51).
// Written like LeviLamina's logger (log.info("... {}", value), fmt format strings, so every call switched by
// renaming), each line goes to MyClient's own file %LOCALAPPDATA%\MyClient\logs\originx-<game>.log (written at
// once, the previous run kept as originx-<game>.previous.log) and, while LeviLamina loads the DLL, also to the
// game's log (latest.log) as before. Log.cpp.
#include <string_view>
#include <utility>

#if __has_include(<expected>)
#include <expected>   // fmt/std.h formats std::expected when the header exists (as LeviLamina's Logger.h includes it)
#endif
#include "fmt/format.h"
// the same formatters LeviLamina's logger brings in, so every value it could write still can be
#include "fmt/chrono.h"
#include "fmt/ranges.h"
#include "fmt/std.h"

namespace MyClientLog {
enum class Level { Info, Warn, Error };
void Write(Level level, std::string_view text);   // thread-safe; never throws
const wchar_t* FilePath();                         // the own file, "" until the first line

struct Logger
{
    template <class... Args>
    void info(fmt::format_string<Args...> format, Args&&... args) { Emit(Level::Info, format, std::forward<Args>(args)...); }
    template <class... Args>
    void warn(fmt::format_string<Args...> format, Args&&... args) { Emit(Level::Warn, format, std::forward<Args>(args)...); }
    template <class... Args>
    void error(fmt::format_string<Args...> format, Args&&... args) { Emit(Level::Error, format, std::forward<Args>(args)...); }

private:
    template <class... Args>
    static void Emit(Level level, fmt::format_string<Args...> format, Args&&... args)
    {
        try { Write(level, fmt::format(format, std::forward<Args>(args)...)); }
        catch (...) {}
    }
};

inline Logger& Get()
{
    static Logger logger;
    return logger;
}
} // namespace MyClientLog

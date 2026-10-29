#pragma once
#include <charconv>
#include <cstdint>
#include <string>

namespace MyClientLauncher {
inline bool ParsePidOption(const std::wstring& argument, uint32_t& pid)
{
    pid = 0;
    std::string ascii;
    for (const wchar_t c : argument)
    {
        if (static_cast<uint32_t>(c) > 127) return false;
        ascii += static_cast<char>(c);
    }
    constexpr char prefix[] = "--minecraft-pid=";
    const size_t length = sizeof(prefix) - 1;
    if (ascii.size() <= length || ascii.compare(0, length, prefix) != 0) return false;
    uint32_t value = 0;
    const auto end = ascii.data() + ascii.size();
    const auto parsed = std::from_chars(ascii.data() + length, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !value) return false;
    pid = value;
    return true;
}
}

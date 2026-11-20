#pragma once
// Your own tag in the name tag list (2026-11-20 report #6). The list carries no player ID, only the text, so
// your name must stand on its own there: "Ann" is not found inside "Anna", "xAnn" or "Ann Smith". A colour code (§x) right
// before or after the name is a boundary, so "§aAnn§r" still matches. The actor hook (NickHook) compares the
// actor itself and does not use this.
#include <string_view>

namespace MyClientWholeName {

// Letters, digits, '_' and any non-ASCII byte (part of a UTF-8 name) continue a name.
inline bool NameByte(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}

// Position of `name` in `text` where the bytes before and after it end a name, or npos.
inline size_t Find(std::string_view text, std::string_view name)
{
    if (name.empty()) return std::string_view::npos;
    for (size_t at = text.find(name); at != std::string_view::npos; at = text.find(name, at + 1)) {
        const size_t end = at + name.size();
        // Before: start, a non-name byte, or the character of a colour code ("\xC2\xA7" then one byte).
        const bool startOk = at == 0 || !NameByte(static_cast<unsigned char>(text[at - 1])) ||
                             (at >= 3 && text.substr(at - 3, 2) == "\xC2\xA7");
        // After: only the end, a line break or a colour code (author 2026-11-20). Gamertags may contain
        // spaces, so "Ann" must not be found in "Ann Smith".
        const bool endOk = end == text.size() || text[end] == '\n' || text.substr(end, 2) == "\xC2\xA7";
        if (startOk && endOk) return at;
    }
    return std::string_view::npos;
}

} // namespace MyClientWholeName

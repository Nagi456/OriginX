#pragma once
// Settings file text (2026-11-20). Saved as UTF-8; files written before that by a default-locale
// wofstream hold one byte per character (Latin-1, e.g. 0xA7 for the colour sign), and still load.
#include <string>

namespace ConfigText {

// Strict UTF-8 decode. Fails on any malformed, overlong, surrogate or out-of-range sequence.
template <class C>
bool DecodeUtf8(const std::string& bytes, std::basic_string<C>& out)
{
    out.clear();
    size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        char32_t cp;
        size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if (c >= 0xC2 && c <= 0xDF) { cp = c & 0x1F; n = 2; }
        else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07; n = 4; }
        else return false;
        if (i + n > bytes.size()) return false;
        for (size_t k = 1; k < n; ++k) {
            const unsigned char d = static_cast<unsigned char>(bytes[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
            (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (sizeof(C) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<C>(0xD800 + (cp >> 10));
            out += static_cast<C>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<C>(cp);
        }
        i += n;
    }
    return true;
}

// UTF-8 if the whole file is valid UTF-8, otherwise the old one-byte-per-character form.
template <class C = wchar_t>
std::basic_string<C> DecodeFile(const std::string& bytes)
{
    std::basic_string<C> text;
    if (DecodeUtf8(bytes, text)) return text;
    text.clear();
    for (unsigned char c : bytes) text += static_cast<C>(c);
    return text;
}

// A lone surrogate (cannot be UTF-8) becomes U+FFFD so the rest of the file is still written.
template <class C>
std::string EncodeUtf8(const std::basic_string<C>& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        char32_t cp = static_cast<char32_t>(text[i]);
        if (sizeof(C) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() &&
            text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<char32_t>(text[i + 1]) - 0xDC00);
            ++i;
        } else if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

} // namespace ConfigText

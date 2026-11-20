// ConfigText: UTF-8 round trip (Windows-style 16-bit and Linux 32-bit wchar_t) and old Latin-1 files.
#include "ConfigText.h"
#include <cassert>
#include <cstdio>
using namespace ConfigText;
int main()
{
    const std::wstring nick = L"§aあなた\U0001F600 Ann";   // colour sign, Japanese, emoji
    const std::string bytes = EncodeUtf8(L"NICK " + nick + L"\nHITBOX 1\n");
    std::wstring back;
    assert(DecodeUtf8(bytes, back));
    assert(back == L"NICK " + nick + L"\nHITBOX 1\n");          // the lines after the Nick are kept
    assert(DecodeFile(bytes) == back);
    // An old file: wofstream in the "C" locale wrote one byte per character (0xA7 for the colour sign).
    const std::string old = std::string("NICK \xA7" "bName\nHITBOX 1\n");
    assert(DecodeFile(old) == L"NICK §bName\nHITBOX 1\n");
    assert(DecodeFile("VERSION 4\n") == L"VERSION 4\n");
    // Invalid UTF-8 is rejected: overlong, surrogate, truncated.
    assert(!DecodeUtf8("\xC0\x80", back));
    assert(!DecodeUtf8("\xED\xA0\x80", back));
    assert(!DecodeUtf8("\xE3\x81", back));
    // A lone surrogate is written as U+FFFD and the rest still goes out.
    std::wstring lone = L"a"; lone += static_cast<wchar_t>(0xD800); lone += L"b";
    assert(EncodeUtf8(lone) == "a\xEF\xBF\xBD" "b");
    // 16-bit characters (wchar_t on Windows): the emoji is a surrogate pair.
    const std::u16string n16 = u"\u00A7a\u3042\U0001F600 Ann";
    const std::string b16 = EncodeUtf8(n16);
    assert(b16 == EncodeUtf8(std::wstring(L"\u00A7a\u3042\U0001F600 Ann")));
    std::u16string r16;
    assert(DecodeUtf8(b16, r16) && r16 == n16);
    std::u16string lone16 = u"a"; lone16 += char16_t(0xDC00); lone16 += u"b";
    assert(EncodeUtf8(lone16) == "a\xEF\xBF\xBD" "b");
    assert(DecodeFile<char16_t>(old) == u"NICK \u00A7bName\nHITBOX 1\n");
    std::puts("OK");
}

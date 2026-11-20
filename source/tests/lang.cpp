// The display language (Lang.h): the saved file's text, and the first start's guess from the download link.
// g++ -std=c++17 -Wall -Wextra -Werror -I. tests/lang.cpp -o /tmp/myclient-lang && /tmp/myclient-lang
#include "Lang.h"
#include <cassert>
#include <iostream>
using Lang::Id;

int main()
{
    assert(Lang::Parse("en") == Id::En);
    assert(Lang::Parse("ja\r\n") == Id::Ja);
    assert(Lang::Parse("\xEF\xBB\xBF" "en\n") == Id::En);
    assert(!Lang::Parse("fr"));
    assert(!Lang::Parse(""));

    // The Worker's link from the English page, as Chrome / Edge write it.
    assert(Lang::FromZone("[ZoneTransfer]\r\nZoneId=3\r\nReferrerUrl=https://originx.example/\r\n"
                          "HostUrl=https://originx.example/file/launcher/MyClientLauncher.exe?exp=1&sig=a&lang=en\r\n") == Id::En);
    // The Japanese page's link has no lang; the referrer decides.
    assert(Lang::FromZone("[ZoneTransfer]\r\nZoneId=3\r\nReferrerUrl=https://originx.example/download/\r\n"
                          "HostUrl=https://originx.example/file/launcher/MyClientLauncher.exe?exp=1&sig=a\r\n") == Id::Ja);
    assert(Lang::FromZone("ReferrerUrl=https://originx.example/en/download/\nHostUrl=https://x/f.exe\n") == Id::En);
    // HostUrl wins over the referrer; "lang=english" or "xlang=en" is not lang=en.
    assert(Lang::FromZone("ReferrerUrl=https://x/en/\nHostUrl=https://x/f?lang=ja\n") == Id::Ja);
    assert(!Lang::FromZone("HostUrl=https://x/f?xlang=en&lang=english\n"));
    assert(!Lang::FromZone("[ZoneTransfer]\nZoneId=3\n"));
    assert(!Lang::FromZone(""));

    assert(Lang::FromLangId(0x0411) == Id::Ja);
    assert(Lang::FromLangId(0x0409) == Id::En);
    assert(Lang::FromLangId(0x0809) == Id::En);

    assert(Lang::T("ja", "en") == std::string("ja"));
    Lang::Current() = Id::En;
    assert(Lang::T(L"ja", L"en") == std::wstring(L"en"));
    std::cout << "lang ok\n";
}

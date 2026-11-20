// Name tag list: your own name only as a whole name (report #6).
#include "client/WholeName.h"
#include <cassert>
#include <cstdio>
using MyClientWholeName::Find;
constexpr auto npos = std::string_view::npos;
int main()
{
    assert(Find("Ann", "Ann") == 0);
    assert(Find("Anna", "Ann") == npos);                 // someone else whose name contains yours
    assert(Find("xAnn", "Ann") == npos);
    assert(Find("Ann_2", "Ann") == npos);
    assert(Find("\xC2\xA7" "aAnn\xC2\xA7r", "Ann") == 3);  // colour codes around it
    assert(Find("\xC2\xA7" "c[VIP] \xC2\xA7" "fAnn\n\xC2\xA7" "7Lv 5", "Ann") == 12);
    assert(Find("[VIP] Ann", "Ann") == 6);               // server rank prefix
    assert(Find("Anna Ann", "Ann") == 5);                // first hit fails, second is yours
    assert(Find("Ann Smith", "Ann") == npos);            // gamertags may contain spaces
    assert(Find("Ann Smith", "Ann Smith") == 0);
    assert(Find("\xC2\xA7" "aAnn Smith\n\xC2\xA7" "7Lv 5", "Ann Smith") == 3);
    assert(Find("Ann Smith\nAnn", "Ann") == 10);
    assert(Find("\xE3\x81\x82" "Ann", "Ann") == npos);   // a UTF-8 letter right before it
    assert(Find("\xC2\xA7" "a\xE3\x81\x82\xE3\x81\x84", "\xE3\x81\x82") == npos);
    assert(Find("\xC2\xA7" "a\xE3\x81\x82", "\xE3\x81\x82") == 3);
    assert(Find("Ann", "") == npos);
    std::puts("OK");
}

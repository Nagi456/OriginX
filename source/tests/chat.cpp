// Command Hotkeys: the line handed to the DLL (ChatChannel.h).
#include "ChatChannel.h"
#include <cassert>
#include <cstdio>
using namespace MyClientChat;

int main()
{
    assert(Clean("  /hub  ") == "/hub");
    assert(Clean("gg\nwp\r") == "gg wp");
    assert(Clean("   ").empty() && Clean("").empty());
    assert(IsCommand("/q solo") && !IsCommand("gg") && !IsCommand(""));
    // Cut to 255 bytes, never inside a UTF-8 character ("あ" is 3 bytes: 85 of them = 255).
    std::string longJp;
    for (int i = 0; i < 100; i++) longJp += "\xE3\x81\x82";
    const std::string cut = Clean(longJp);
    assert(cut.size() == 255 && cut.size() % 3 == 0);
    std::string longAscii(400, 'a');
    assert(Clean(longAscii).size() == MaxText - 1);
    // Replay Mod: the join request's "host:port".
    std::string host;
    uint16_t port = 0;
    assert(ParseHostPort("127.0.0.1:19150", host, port) && host == "127.0.0.1" && port == 19150);
    assert(!ParseHostPort("127.0.0.1", host, port) && !ParseHostPort(":19150", host, port));
    assert(!ParseHostPort("127.0.0.1:", host, port) && !ParseHostPort("127.0.0.1:0", host, port));
    assert(!ParseHostPort("127.0.0.1:70000", host, port) && !ParseHostPort("127.0.0.1:19a50", host, port));
    assert(!ParseHostPort("127.0.0.1:1234567", host, port));
    std::puts("PASS: chat line cleaning, replay join address");
}

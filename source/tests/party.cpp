#include "PartyData.h"
#include <cassert>
#include <iostream>
using namespace MyClientParty;
int main() {
    Config c{}; c.at = 100; c.enabled = 1; c.count = 2;
    std::strcpy(c.tags[0], "Test Player"); std::strcpy(c.tags[1], "Alex");
    assert(Registered(c, "test player", 101));
    assert(!Registered(c, "Test", 101)); assert(!Registered(c, "Other", 101));
    assert(!Registered(c, "Alex", 1101)); assert(!Active(c, 99));
    c.enabled = 2; assert(!Active(c, 100)); c.enabled = 1;
    c.count = 65; assert(!Active(c, 100)); c.count = 2;
    std::memset(c.tags[1], 'A', TagBytes); assert(!Active(c, 100)); std::strcpy(c.tags[1], "Alex");
    assert(HiveHost("GEO.HIVEBEDROCK.NETWORK.")); assert(HiveHost("as.hivebedrock.network"));
    assert(!HiveHost("geo.hivebedrock.network.evil.com")); assert(!HiveHost("evilhivebedrock.network"));
    assert(!HiveHost("evil.hivebedrock.network")); assert(!HiveHost("127.0.0.1"));
    assert(Invite("Test Playerからパーティへの招待が来ています",0,false,false) == "Test Player");
    assert(Invite("§aAlex§r has invited you to their party!",6,false,false) == "Alex");
    // The real English wording (screenshot): name, then "wants you to join their party!"
    assert(Invite("RulingWeb9802 wants you to join their party!",0,false,false) == "RulingWeb9802");
    assert(Invite("§fRulingWeb9802 §bwants you to join their party!§r",6,false,false) == "RulingWeb9802");
    assert(Invite("RulingWeb9802 wants you to join their party!",1,true,false).empty());      // a player chat line must never count
    assert(Invite("<Other> RulingWeb9802 wants you to join their party!",0,false,false).empty());
    assert(Invite("RulingWeb9802 wants you to join their party! now",0,false,false).empty());
    assert(Invite("Alexからパーティーへの招待が来ています。",0,false,false) == "Alex");
    assert(Invite("Alex has invited you to their party!",1,true,false).empty());
    assert(Invite("Alex has invited you to their party!",0,true,false).empty());
    assert(Invite("Alex has invited you to their party!",0,false,true).empty());
    assert(Invite("Alex has invited you to their party!",7,false,false).empty());
    assert(Invite("<Other> Alex has invited you to their party!",0,false,false).empty());
    assert(Invite("Alex has invited you to their party! extra",0,false,false).empty());
    assert(Invite("Alex\nhas invited you to their party!",0,false,false).empty());
    assert(Invite(std::string(1025,'A'),0,false,false).empty());
    assert(Clean("§xAlex").empty());
    assert(AcceptCommand("Test Player") == "/party accept \"Test Player\"");
    for (auto invalid : {"", " Alex", "Alex ", "Alex\n", "Alex\"", "Alex;op", "@a", "こんにちは"})
        assert(AcceptCommand(invalid).empty());
    Gate gate;
    assert(gate.Allow("Alex",100)); gate.Sent("Alex",100);
    assert(!gate.Allow("Bob",101)); assert(!gate.Allow("alex",2100));
    assert(gate.Allow("Bob",2100)); gate.Sent("Bob",2100);
    assert(!gate.Allow("Alex",4100)); assert(gate.Allow("Alex",30100));
    assert(!gate.Allow("Bob",50)); gate.Reset(); assert(gate.Allow("Alex",100));
    std::cout << "PASS: party whitelist, lease, host, invite parser, command safety and deduplication\n";
}

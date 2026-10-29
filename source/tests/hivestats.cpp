#include "../HiveStats.h"
#include <cassert>
#include <cstdio>
using namespace HiveStats;
int main()
{
    Stats s;
    assert(ParseStats(R"({"UUID":"abc","xp":12345,"played": 100, "victories":40,"kills":321,"deaths":100,"final_kills":9})", s));
    assert(s.xp == 12345 && s.played == 100 && s.victories == 40 && s.kills == 321 && s.deaths == 100);
    assert(s.prestige == 0);
    Stats p;
    assert(ParseStats(R"({"xp":900,"prestige":2,"kills":1})", p) && p.prestige == 2);
    assert(Higher(2, Level(kGames[0], 900), 0, 99.5));   // prestige 2 lvl ~3 beats prestige 0 lvl 99
    assert(!Higher(0, 99.5, 1, 1));
    assert(Higher(1, 20, 1, 10));
    assert(!ParseStats(R"({"message":"not found"})", s));
    assert(Kdr(s) > 3.20 && Kdr(s) < 3.22);
    assert(WinPercent(s) == 40.0);
    Stats z; z.kills = 5; assert(Kdr(z) == 5.0 && WinPercent(z) == 0.0);
    // level maths: xp 0 -> level 1; Bed Wars clamps at 100
    assert(Level(kGames[0], 0) == 1);
    assert(Level(kGames[0], 1e12) == 100);
    const double l = Level(kGames[0], 150000);
    assert(l > 1 && l < 100);
    assert(Level(kGames[3], 0) == 1);
    assert(Level(kGames[3], 1e12) == 20);
    assert(UrlEncode("suzuki pvp6100") == "suzuki%20pvp6100");
    assert(Lower("AbC") == "abc");
    // fixed-point check of the formula: level 2 needs xp = increment (150) in Bed Wars
    assert(Level(kGames[0], 150) >= 1.99 && Level(kGames[0], 150) <= 2.01);
    assert(DetectGame("\xC2\xA7" "l\xC2\xA7" "eBED WARS") == 0);
    assert(DetectGame("Sky Wars") == 1 && DetectGame("THE BRIDGE") == 3);
    assert(DetectGame("The Hive Bedrock") == -1 && DetectGame("") == -1);
    std::printf("hivestats ok (bed 150000xp -> %.2f)\n", l);
}

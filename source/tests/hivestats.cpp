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
    // /all/all: one object per game; hide and murder add their kill fields; sky-classic folds into Sky Wars.
    {
        Profile pr;
        const char* all = R"({"main":{"UUID":"x","first_played":1},"bed":{"xp":150,"played":10,"victories":4,"kills":20,"deaths":10},
            "hide":{"played":5,"victories":1,"deaths":2,"hider_kills":3,"seeker_kills":4},
            "murder":{"played":2,"victories":1,"murders":5,"murderer_eliminations":1,"deaths":3},
            "sky":{"played":1,"victories":1,"kills":1,"deaths":0},"sky-classic":{"played":3,"victories":0,"kills":2,"deaths":2},
            "build":{"played":7,"victories":2,"name":"a \"quoted\" {brace}"},"unknown":[1,{"a":2}]})";
        assert(ParseAll(all, pr));
        assert(pr.played[0] && pr.game[0].kills == 20 && Level(kGames[0], pr.game[0].xp) >= 1.99);
        assert(pr.played[8] && pr.game[8].kills == 7 && pr.game[8].deaths == 2);
        assert(pr.played[4] && pr.game[4].kills == 6);
        // Sky Wars is today's mode only; sky-classic is an ended mode that counts in All Games alone (2026-11-19).
        assert(pr.played[1] && pr.game[1].played == 1 && pr.game[1].kills == 1 && pr.game[1].deaths == 0);
        assert(pr.played[15] && pr.game[15].played == 3 && !kGames[15].listed && !Pickable(15));
        assert(pr.played[10] && pr.game[10].played == 7 && !pr.game[10].hasKd && !kGames[13].kdr && !kGames[14].kdr);
        assert(!pr.played[2]);
        const Stats t = Total(pr);
        assert(t.played == 10 + 5 + 2 + 4 + 7 && t.victories == 4 + 1 + 1 + 1 + 2);
        assert(t.kills == 20 + 7 + 6 + 3 && t.deaths == 10 + 2 + 3 + 2);
        assert(Level(kGames[10], 1e9) == 0 && !kGames[10].level && !kGames[10].kdr);
        // Indices stay (the config stores them); ended modes leave the list but keep their place.
        assert(std::string(kGames[2].id) == "wars" && !Pickable(2) && !Pickable(14) && Pickable(1) && Pickable(13));
        assert(Pickable(kAllGames) && Pickable(kHidden) && !Pickable(kGameCount));
        assert(PickRow(kHidden) == 0 && PickRow(kAllGames) == 1 && PickRow(0) == 2 && PickRow(1) == 3 && PickRow(3) == 4);
        assert(DetectGame("TREASURE WARS") == -1);
        Profile none;
        assert(ParseAll("{}", none) && !none.played[0]);
        assert(!ParseAll("{\"bed\":", none) && !ParseAll("[]", none));
    }
    // Limiter: first lookup now; again only after a game change; 30 s after a failure; 3 per hour; 429 blocks all.
    {
        using C = Limiter::Clock;
        const auto t0 = C::now();
        Limiter::Player p;
        assert(Limiter::Due(p, 0, t0, {}));
        assert(!Limiter::Due(p, 0, t0, t0 + std::chrono::seconds(5)));   // blocked by Retry-After
        Limiter::Sent(p, 0, t0);
        assert(!Limiter::Due(p, 1, t0, {}));   // in flight
        p.answered = true; p.answeredAt = t0;
        assert(!Limiter::Due(p, 0, t0 + std::chrono::minutes(5), {}));   // same game: kept
        assert(Limiter::Due(p, 1, t0 + std::chrono::minutes(1), {}));   // game changed
        Limiter::Sent(p, 1, t0 + std::chrono::minutes(1)); p.answered = true; p.failed = true; p.answeredAt = t0 + std::chrono::minutes(1);
        assert(!Limiter::Due(p, 1, t0 + std::chrono::minutes(1) + std::chrono::seconds(10), {}));
        assert(Limiter::Due(p, 1, t0 + std::chrono::minutes(1) + std::chrono::seconds(30), {}));
        Limiter::Sent(p, 1, t0 + std::chrono::minutes(2)); p.answered = true; p.failed = false;
        assert(!Limiter::Due(p, 2, t0 + std::chrono::minutes(3), {}));   // 3 in the last hour
        assert(Limiter::Due(p, 2, t0 + std::chrono::minutes(61), {}));   // the first one aged out
    }
    {   // The worker is joined: Stop ends it (here every lookup fails at once, there is no HTTP off Windows),
        // and after Stop nothing starts again.
        Fetcher f;
        for (int i = 0; i < 20; i++) f.Get("player" + std::to_string(i));
        f.Stop();
        f.Get("late");
        f.Stop();
        Fetcher g;
        g.Get("a");   // the destructor stops it
    }
    std::printf("hivestats ok (bed 150000xp -> %.2f)\n", l);
}

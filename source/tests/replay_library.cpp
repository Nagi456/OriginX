#include "../ReplayLibrary.h"
#include <cassert>
#include <cstdio>
using namespace ReplayLibrary;

static Entry Make(const char* file, uint64_t size, uint32_t seconds)
{
    Entry e; e.file = file; e.size = size; e.seconds = seconds;
    ParseName(e);
    return e;
}

int main()
{
    Entry a = Make("replay-20261021-084012.mcrec", 12 * 1048576 + 300000, 461);
    assert(a.year == 2026 && a.month == 10 && a.day == 21 && a.hour == 8 && a.minute == 40);
    assert(DefaultTitle(a) == "Replay 2026-10-21 08:40" && Title(a) == DefaultTitle(a));
    assert(Details(a) == "7m 41s, 12.3 MB");
    Entry odd = Make("my clip.mcrec", 1048576, 0);
    assert(!odd.year && Title(odd) == "my clip.mcrec" && Details(odd) == "1.0 MB");
    a.meta.title = "Bed Wars win";
    assert(Title(a) == "Bed Wars win");

    // date groups (today = 2026-10-21)
    const long today = DayNumber(2026, 10, 21);
    assert(DayNumber(1970, 1, 1) == 0 && DayNumber(2026, 10, 21) - DayNumber(2026, 10, 20) == 1);
    assert(DayNumber(2026, 3, 1) - DayNumber(2026, 2, 28) == 1);   // across February
    Entry y = Make("replay-20261020-120000.mcrec", 1, 30);
    Entry w = Make("replay-20261017-120000.mcrec", 5, 90);
    Entry o = Make("replay-20260901-120000.mcrec", 9, 10);
    Entry f = Make("replay-20261008-204100.mcrec", 2, 5);
    f.meta.favorite = true;
    assert(GroupOf(a, today) == Group::Today && GroupOf(y, today) == Group::Yesterday);
    assert(GroupOf(w, today) == Group::ThisWeek && GroupOf(o, today) == Group::Older && GroupOf(f, today) == Group::Favorites);

    // layout: Favorites first, then Today, Yesterday, This week, Older; collapsed keeps the header
    std::vector<Entry> list{o, a, y, w, f};
    bool collapsed[kGroupCount] = {};
    auto rows = Layout(list, Sort::Date, "", today, collapsed);
    assert(rows.size() == 10);
    assert(rows[0].header && rows[0].group == Group::Favorites && list[rows[1].entry].file == f.file);
    assert(rows[2].header && rows[2].group == Group::Today && list[rows[3].entry].meta.title == "Bed Wars win");
    collapsed[static_cast<int>(Group::Older)] = true;
    rows = Layout(list, Sort::Date, "", today, collapsed);
    assert(rows.size() == 9 && rows.back().header && rows.back().group == Group::Older);
    // search (case-insensitive, on the title)
    collapsed[static_cast<int>(Group::Older)] = false;
    rows = Layout(list, Sort::Date, "bed", today, collapsed);
    assert(rows.size() == 2 && list[rows[1].entry].meta.title == "Bed Wars win");
    rows = Layout(list, Sort::Date, "2026-10-20", today, collapsed);
    assert(rows.size() == 2 && list[rows[1].entry].file == y.file);
    // sort inside a group
    std::vector<Entry> two{Make("replay-20261021-010000.mcrec", 50, 10), Make("replay-20261021-020000.mcrec", 10, 99)};
    rows = Layout(two, Sort::Size, "", today, collapsed);
    assert(two[rows[1].entry].size == 50);
    rows = Layout(two, Sort::Length, "", today, collapsed);
    assert(two[rows[1].entry].seconds == 99);
    rows = Layout(two, Sort::Date, "", today, collapsed);
    assert(two[rows[1].entry].file == "replay-20261021-020000.mcrec");

    // replays.txt round trip (UTF-8 title, tabs cleaned, empty entries dropped)
    MetaMap map;
    map["replay-20261008-204100.mcrec"] = {true, ""};
    map["replay-20261021-084012.mcrec"] = {false, "\xE3\x83\x99\xE3\x83\x83\xE3\x83\x89\twin"};
    map["replay-20261020-120000.mcrec"] = {false, ""};
    const std::string text = SerializeMeta(map);
    const MetaMap back = ParseMeta(text);
    assert(back.size() == 2);
    assert(back.at("replay-20261008-204100.mcrec").favorite && back.at("replay-20261008-204100.mcrec").title.empty());
    assert(back.at("replay-20261021-084012.mcrec").title == "\xE3\x83\x99\xE3\x83\x83\xE3\x83\x89 win");
    assert(ParseMeta("bad line\n\tx\t1\r\n").empty());
    std::puts("replay library ok");
}

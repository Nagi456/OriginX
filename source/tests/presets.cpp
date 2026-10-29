#include "../Presets.h"
#include <cassert>
#include <iostream>
// Presets file format: name TAB key=0|1,...  (UTF-8 names kept intact).
int main()
{
    using namespace Presets;
    Preset p{"PvP", {{"f3", true}, {"f12", false}, {"hive", true}}};
    assert(Serialize(p) == "PvP\tf3=1,f12=0,hive=1");
    Preset q;
    assert(Parse(Serialize(p), q) && q.name == "PvP" && q.states.size() == 3 && q.states[0].second && !q.states[1].second);
    assert(CountOn(q) == 2);
    assert(Parse("\xE3\x82\xB5\xE3\x83\x90\xE3\x82\xA4\xE3\x83\x90\xE3\x83\xAB\tf1=1\r", q) && q.name == "\xE3\x82\xB5\xE3\x83\x90\xE3\x82\xA4\xE3\x83\x90\xE3\x83\xAB" && q.states.size() == 1);
    assert(!Parse("no tab here", q) && !Parse("\tf1=1", q) && !Parse("  \tf1=1", q));
    assert(Parse("Empty\t", q) && q.states.empty());
    assert(Parse("Bad\tf1,=1,f2=,f3=1", q) && q.states.size() == 1 && q.states[0].first == "f3");
    assert(CleanName(" a\tb\nc ") == "a b c");
    std::string jp; for (int i = 0; i < 30; i++) jp += "\xE3\x81\x82";   // 90 bytes of "あ"
    const std::string cut = CleanName(jp);
    assert(cut.size() <= MaxNameBytes && cut.size() % 3 == 0);
    std::cout << "presets ok\n";
    return 0;
}

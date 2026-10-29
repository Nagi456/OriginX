#include "../TabListChannel.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main()
{
    MyClientTabList::Collector c;
    c.Add("Alice"); c.Add(""); c.Add("Bob");
    assert(c.Result().count == 2 && c.Result().total == 2);
    assert(std::string(c.Result().names[1]) == "Bob");
    for (int i = 0; i < 100; i++) c.Add("P" + std::to_string(i));
    assert(c.Result().count == MyClientTabList::MaxPlayers && c.Result().total == 102);
    // long UTF-8 name is cut on a character boundary
    MyClientTabList::Collector d;
    d.Add(std::string(30, 'a') + "\xE3\x81\x82");
    assert(std::string(d.Result().names[0]) == std::string(30, 'a'));
    MyClientTabList::Collector e;
    e.SetTitle("Bed Wars x");
    assert(std::string(e.Result().title) == "Bed Wars x");
    e.SetTitle(std::string(500, 'z'));
    assert(std::strlen(e.Result().title) == MyClientTabList::TitleSize - 1);
    MyClientTabList::Collector f;
    MyClientTabList::Box box{};
    box.max[0] = 1; box.max[1] = 2; box.max[2] = 3;
    for (int i = 0; i < 40; i++) f.AddBox(box);
    assert(f.Result().boxCount == MyClientTabList::MaxBoxes);
    float cc[3];
    MyClientTabList::Corner(box, 7, cc);
    assert(cc[0] == 1 && cc[1] == 2 && cc[2] == 3);
    for (auto& e : MyClientTabList::kEdges) assert(__builtin_popcount(e[0] ^ e[1]) == 1);
    std::puts("tablist ok");
}

#include "ItemCountChannel.h"
#include <cassert>
#include <cstdio>
using namespace MyClientItemCount;

int main()
{
    // The screenshot's example: 64 + 64 + 512 = 640 = 10 stacks of 64.
    Counts c;
    c.inventory = 64; c.shulkers = 64; c.container = 512;
    assert(c.Total() == 640 && Stacks(c.Total(), 64) == 10);
    assert(Stacks(65, 64) == 2 && Stacks(0, 64) == 0 && Stacks(16, 16) == 1 && Stacks(17, 16) == 2);
    assert(Stacks(3, 1) == 3 && Stacks(3, 0) == 3);   // unstackable: one stack each
    char out[8];
    CopyText(out, sizeof out, "abcdefghij");
    assert(std::string(out) == "abcdefg");
    CopyText(out, sizeof out, "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86");   // 3 characters of 3 bytes: only 2 fit in 7
    assert(std::string(out) == "\xE3\x81\x82\xE3\x81\x84");
    assert(Fresh(100, 200, 300) && !Fresh(0, 200, 300) && !Fresh(100, 500, 300) && !Fresh(300, 200, 300));
    assert(IsOpenContainerModel("container_items") && IsOpenContainerModel("shulker_box_items") && IsOpenContainerModel("hopper_items"));
    assert(!IsOpenContainerModel("inventory_items") && !IsOpenContainerModel("hotbar_items") &&
           !IsOpenContainerModel("combined_hotbar_and_inventory_items") && !IsOpenContainerModel("armor_items") &&
           !IsOpenContainerModel("offhand_items") && !IsOpenContainerModel("cursor_items") &&
           !IsOpenContainerModel("crafting_input_items") && !IsOpenContainerModel("") && !IsOpenContainerModel("foo"));
    std::puts("PASS: item counter");
}

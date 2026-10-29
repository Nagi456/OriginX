#include "../ShulkerChannel.h"
#include <cassert>
#include <cstring>
using namespace MyClientShulker;
int main() {
    assert(IsShulkerBox("minecraft:undyed_shulker_box") && IsShulkerBox("minecraft:red_shulker_box"));
    assert(!IsShulkerBox("minecraft:chest") && !IsShulkerBox("minecraft:shulker_shell"));
    Collector c;
    assert(c.Begin("minecraft:blue_shulker_box"));
    c.Add("minecraft:diamond", 10); c.Add("minecraft:diamond", 5); c.Add("minecraft:stick", 64);
    const auto& r = c.Result();
    assert(r.boxCount == 1 && std::strcmp(r.boxes[0].name, "blue_shulker_box") == 0);
    assert(r.boxes[0].used == 3 && r.boxes[0].typeCount == 2);
    assert(std::strcmp(r.boxes[0].items[0].name, "diamond") == 0 && r.boxes[0].items[0].count == 15);
    // More distinct types than fit: extras are dropped but still counted as used slots.
    Collector d; d.Begin("minecraft:shulker_box");
    char n[16];
    for (int i = 0; i < 20; i++) { std::snprintf(n, sizeof n, "minecraft:i%d", i); d.Add(n, 1); }
    assert(d.Result().boxes[0].typeCount == MaxTypes && d.Result().boxes[0].used == 20 && d.Valid());
    // Box limit and invalid counts.
    Collector e; for (uint32_t i = 0; i < MaxBoxes; i++) assert(e.Begin("minecraft:shulker_box"));
    assert(!e.Begin("minecraft:shulker_box") && e.Result().boxCount == MaxBoxes);
    e.Add("minecraft:stick", 1);   // ignored: no current box
    Collector f; f.Begin("minecraft:shulker_box"); f.Add("", 3); assert(!f.Valid());
    Collector g; g.Begin("minecraft:shulker_box"); g.Add("minecraft:stick", 300); assert(!g.Valid());
    Shared s{}; s.magic = Magic; s.version = Version; s.size = sizeof(Shared); s.pid = 7;
    assert(Valid(s, 7) && !Valid(s, 8));
    assert(Fresh(1000, 1500) && !Fresh(0, 1500) && !Fresh(1000, 4000));
    // Hover popup header: own magic, so a list snapshot never passes as a hover one.
    Hover h{}; h.magic = HoverMagic; h.version = HoverVersion; h.size = sizeof(Hover); h.pid = 7;
    assert(Valid(h, 7) && !Valid(h, 8));
    h.magic = Magic; assert(!Valid(h, 7));
    assert(HoverFresh(1000, 1200) && !HoverFresh(0, 1200) && !HoverFresh(1000, 1400) && !HoverFresh(1500, 1200));
    // v2 hover: one entry per slot, a taken slot keeps its first stack, bad entries are refused.
    HoverBox b{};
    assert(PutSlot(b, 0, "minecraft:deepslate", 64) && PutSlot(b, 26, "minecraft:light_gray_stained_glass_pane", 3));
    assert(PutSlot(b, 0, "minecraft:stone", 1) && std::strcmp(b.slots[0].name, "deepslate") == 0 && b.slots[0].count == 64);
    assert(std::strcmp(b.slots[26].name, "light_gray_stained_glass_pane") == 0 && b.used == 2);
    assert(PutSlot(b, 5, "minecraft:stick", 0) && b.used == 2);   // empty entry: nothing to place
    assert(!PutSlot(b, 27, "minecraft:stick", 1) && !PutSlot(b, -1, "minecraft:stick", 1));
    assert(!PutSlot(b, 3, "", 1) && !PutSlot(b, 3, "minecraft:stick", 256));
    return 0;
}

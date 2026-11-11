#include "GameDataMirror.h"
#include <cassert>
#include <cstdio>
using MyClientGameData::Wanted;
int main(){
 assert(Wanted("blocks.json")); assert(Wanted("textures/item_texture.json")); assert(Wanted("textures/terrain_texture.json"));
 assert(Wanted("textures/items/diamond_pickaxe.png")); assert(Wanted("textures/blocks/stone.png")); assert(Wanted("textures/blocks/deepslate/deepslate_top.png"));
 assert(Wanted("textures/items/leather_helmet.tga")); assert(Wanted("textures/ui/empty_armor_slot_helmet.png"));
 assert(Wanted("texts/ja_jp.lang")); assert(Wanted("pack_icon.png"));
 assert(!Wanted("textures/ui/hotbar.png")); assert(!Wanted("textures/entity/zombie.png")); assert(!Wanted("sounds/x.ogg"));
 assert(!Wanted("textures/blocks/stone.json")); assert(!Wanted("texts/sub/x.lang")); assert(!Wanted("texts/x.txt"));
 std::puts("game data mirror ok");}

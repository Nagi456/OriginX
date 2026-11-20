#include "GameDataMirror.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include <cstdint>
using namespace MyClientGameData;

// Synthetic bytes only: no game files or image assets are used by this test.
static void U32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
 for (unsigned i=0;i<4;++i) bytes[at+i]=uint8_t(value>>(8*i));
}
static std::vector<uint8_t> Archive() {
 std::vector<uint8_t> bytes(16+2*256+5,0);
 const uint8_t magic[]={0x7d,0x27,0x25,0xb1,0xa0,0x52,0x70,0x26};
 std::memcpy(bytes.data(),magic,8);U32(bytes,8,2);U32(bytes,12,1);
 bytes[16]=5;std::memcpy(bytes.data()+17,"a.png",5);U32(bytes,16+248,0);U32(bytes,16+252,3);
 bytes[272]=6;std::memcpy(bytes.data()+273,"b.json",6);U32(bytes,272+248,3);U32(bytes,272+252,2);
 std::memcpy(bytes.data()+528,"abc{}",5);return bytes;
}
static void ParserTests() {
 std::vector<BrEntry> entries;size_t start=0;
 auto bytes=Archive();
 assert(ParseBrArchive(bytes.data(),bytes.size(),entries,start));
 assert(start==528 && entries.size()==2);
 assert(entries[0].name=="a.png" && entries[0].offset==0 && entries[0].size==3);
 assert(entries[1].name=="b.json" && entries[1].offset==3 && entries[1].size==2);
 assert(std::memcmp(bytes.data()+start+entries[0].offset,"abc",3)==0);
 auto empty=bytes;empty.resize(16);U32(empty,8,0);
 assert(ParseBrArchive(empty.data(),empty.size(),entries,start) && entries.empty() && start==16);
 U32(bytes,272+252,0); // zero-byte entries are valid
 assert(ParseBrArchive(bytes.data(),bytes.size(),entries,start) && entries[1].size==0);
 const auto bad=[&](std::vector<uint8_t> corrupt){
  entries={{"stale",0,1}};start=123;
  assert(!ParseBrArchive(corrupt.data(),corrupt.size(),entries,start));
  assert(entries.empty() && start==0); // never expose a partial archive
 };
 for(size_t n=0;n<16;++n) bad(std::vector<uint8_t>(n,0));
 auto b=Archive();b[0]^=1;bad(b);
 b=Archive();U32(b,12,2);bad(b);
 b=Archive();U32(b,8,3);bad(b);
 b=Archive();U32(b,8,UINT32_MAX);bad(b);
 b=Archive();U32(b,16+248,6);bad(b);
 b=Archive();U32(b,16+252,6);bad(b);
 b=Archive();U32(b,16+248,UINT32_MAX);U32(b,16+252,UINT32_MAX);bad(b);
 b=Archive();b[16]=0;bad(b);
 for(unsigned length:{248u,255u}){b=Archive();b[16]=uint8_t(length);bad(b);}
 for(const char* name:{"a/b.png","a\\b.png","..png","a..png","a:b.png"}){
  b=Archive();b[272]=uint8_t(std::strlen(name));std::memcpy(b.data()+273,name,std::strlen(name));bad(b);
 }
 b=Archive();b[273]=0;bad(b);
 b=Archive();b[273]=0x80;bad(b);
 assert(!ParseBrArchive(nullptr,16,entries,start));
 // Boundary: the last record can end exactly at EOF; one missing payload byte invalidates the whole input.
 b=Archive();b.pop_back();bad(b);
 // Maximum legal name length fits before the offset field.
 b=Archive();b[16]=247;std::memset(b.data()+17,'x',247);
 assert(ParseBrArchive(b.data(),b.size(),entries,start) && entries[0].name.size()==247);
}
int main(){
 assert(Wanted("blocks.json")); assert(Wanted("textures/item_texture.json")); assert(Wanted("textures/terrain_texture.json"));
 assert(Wanted("textures/items/diamond_pickaxe.png")); assert(Wanted("textures/blocks/stone.png")); assert(Wanted("textures/blocks/deepslate/deepslate_top.png"));
 assert(Wanted("textures/items/leather_helmet.tga")); assert(Wanted("textures/ui/empty_armor_slot_helmet.png"));
 assert(Wanted("texts/ja_jp.lang")); assert(Wanted("pack_icon.png"));
 assert(!Wanted("textures/ui/hotbar.png")); assert(Wanted("textures/entity/zombie.png")); assert(!Wanted("sounds/x.ogg"));
 assert(!Wanted("textures/blocks/stone.json")); assert(!Wanted("texts/sub/x.lang")); assert(!Wanted("texts/x.txt"));
 assert(BrArchiveDir("__brarchive/textures/items.brarchive")=="textures/items");
 assert(BrArchiveDir("__brarchive/textures.brarchive")=="textures");
 assert(BrArchiveDir("__brarchive/textures/entity/chest.brarchive")=="textures/entity/chest");
 for(const char* path:{"textures/items.brarchive","__brarchive/textures.zip","x/__brarchive/textures.brarchive","__brarchive/.brarchive","__brarchive/../escape.brarchive","__brarchive/textures/../escape.brarchive","__brarchive/C:/items.brarchive","__brarchive/textures\\items.brarchive"})
  assert(BrArchiveDir(path).empty());
 ParserTests();
 std::puts("game data mirror ok");}

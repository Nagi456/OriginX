#include "MapData.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace MyClientMap;
int main() {
 assert(Shade(Rgb(127,178,56),70,70,0)==Rgb(109,153,48));
 assert(Shade(Rgb(127,178,56),71,70,0)==Rgb(127,178,56));
 assert(Shade(Rgb(127,178,56),69,70,0)==Rgb(89,125,39));
 assert(Shade(Rgb(100,150,250),70,70,12)<Shade(Rgb(100,150,250),70,70,1));
 assert(TileOf(-1)==-1 && TileOf(-256)==-1 && TileOf(-257)==-2 && InTile(-1)==255);
 assert(WorldKey("My World","")=="My_World"); assert(WorldKey("a/b:c","")=="a_b_c");
 assert(WorldKey("Realm","1.2.3.4")=="Realm@1.2.3.4");
 assert(WorldKey("CON","")=="_CON" && WorldKey("con.txt","")=="_con.txt" && WorldKey("Com1","")=="_Com1" && WorldKey("LPT0","")=="LPT0" && WorldKey("CONSOLE","")=="CONSOLE" && WorldKey("nul","")=="_nul");
 assert(FallbackColor("minecraft:grass_block")==Rgb(127,178,56) && FallbackColor("minecraft:oak_leaves")==Rgb(0,124,0) && FallbackColor("minecraft:stone")==0 && FallbackColor("minecraft:grass_path")==Rgb(151,109,77));
 assert(FallbackColor("minecraft:oak_log")==Rgb(143,119,72) && FallbackColor("minecraft:cactus")==Rgb(0,124,0));
 assert(ValidLevelId("Ab3+dE=") && !ValidLevelId("") && !ValidLevelId("..") && !ValidLevelId("a/b"));
 assert(WorldIdentity("My World","Ab3=",96)=="My World#Ab3=" && WorldIdentity("My World","../x",96)=="My World");
 assert(WorldIdentity(std::string(200,'a'),"Ab3=",96)==std::string(90,'a')+"#Ab3=");
 assert(WorldKey(WorldIdentity("My World","Ab3=",96),"")!=WorldKey(WorldIdentity("My World","Zz9=",96),""));
 assert(WorldKey("","")=="unnamed" && WorldKey("..","")=="__" && WorldKey("v1.2","")=="v1.2");
 assert(DimensionName(0)=="overworld" && DimensionName(1)=="nether" && DimensionName(2)=="end" && DimensionName(8)=="dim8");
 auto face=FaceOf(199456); assert(face && std::string(face->path)=="textures/entity/zombie/zombie" && face->x==8 && face->enemy);
 for(auto const& f:kMobFaces) assert(f.x>=0 && f.y>=0 && f.x+f.w<=f.imageW && f.y+f.h<=f.imageH);
 auto dir=std::filesystem::temp_directory_path()/"originx-map-data-test";std::filesystem::create_directories(dir);
 Tile tile{};tile.cells[123]=0xff123456;assert(SaveTile(dir/"tile.bin",tile));Tile loaded{}; assert(LoadTile(dir/"tile.bin",loaded) && loaded.cells==tile.cells);
 {std::ofstream bad(dir/"bad.bin");bad<<"bad";} assert(!LoadTile(dir/"bad.bin",loaded));
 {std::fstream wrong(dir/"tile.bin",std::ios::in|std::ios::out|std::ios::binary);wrong.put('x');}assert(!LoadTile(dir/"tile.bin",loaded));assert(SaveTile(dir/"tile.bin",tile));
 {std::ofstream extra(dir/"tile.bin",std::ios::binary|std::ios::app);extra<<'x';} assert(!LoadTile(dir/"tile.bin",loaded));
 std::filesystem::remove_all(dir);std::cout<<"map data ok\n";
}

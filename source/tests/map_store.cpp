#include "MapStore.h"
#include <cassert>
#include <iostream>
using namespace MyClientMap;
int main(){
 auto root=std::filesystem::temp_directory_path()/"originx-map-store-test";
 std::filesystem::remove_all(root);{Store s(root);Shared data{};data.at=1;CopyText(data.world,96,"World");data.dimension=0;
 data.cells[128]=Cell(0x7fb238,0,true);data.cells[0]=Cell(0x7fb238,0,true);data.heights[128]=70;data.heights[0]=70;
 assert(s.Update(data));assert(s.Color(0,1)==0x80000000u+Rgb(109,153,48));assert(s.Color(-1,-1)==0);
 assert(s.Flush());Store loaded(root);loaded.Update(data);assert(loaded.Color(0,1)==s.Color(0,1));
 data.dimension=1;s.Update(data);assert(s.Folder().filename()=="nether");
 data.dimension=2;data.cells[128]=0;s.Update(data);assert(!s.Color(0,1));
 // Failed persistence must leave the old identity intact so the HUD can hide the map.
 auto blocker=root/"blocked";{std::ofstream f(blocker);f<<"file";}
 Store failure(blocker);data.dimension=0;data.cells[128]=Cell(0xff0000,0,true);assert(failure.Update(data));
 auto oldFolder=failure.Folder();data.dimension=1;assert(!failure.Update(data));assert(failure.Folder()==oldFolder);
 assert(s.ClearWorld());assert(s.Flush());assert(!std::filesystem::exists(s.WorldFolder()));}std::filesystem::remove_all(root);
 std::cout<<"map store ok\n";
}

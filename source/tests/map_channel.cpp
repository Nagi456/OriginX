#include "MapChannel.h"
#include <cassert>
#include <iostream>
using namespace MyClientMap;
int main(){
 Shared s{};s.magic=Magic;s.version=Version;s.size=sizeof(s);s.pid=42;assert(Valid(s,42));assert(!Valid(s,43));
 auto valid=s;s.magic=0;assert(!Valid(s,42));s=valid;s.version=9;assert(!Valid(s,42));s=valid;s.size=1;assert(!Valid(s,42));s=valid;
 s.mobCount=129;assert(!Valid(s,42));s.mobCount=0;
 assert(Cell(0x123456,9,true)==0x89123456);assert(Cell(0xffffff,999,false)==0x7fffffff);
 s.cells[20*Window+10]=Cell(0x123456,0,true);s.heights[20*Window+10]=70;
 Shift(s,3,-2);assert(s.cells[22*Window+7]==Cell(0x123456,0,true) && s.heights[22*Window+7]==70);assert(!s.cells[20*Window+10]);
 Shift(s,128,0);for(auto c:s.cells)assert(!c);
 char text[4];CopyText(text,sizeof(text),"abcdef");assert(std::string(text)=="abc");CopyText(text,0,"x");
 assert(AutoMobs(1,false) && AutoMobs(2,true) && !AutoMobs(2,false) && !AutoMobs(0,false));
 assert(ConnectionLocality(false,false,-1,"")==0);
 assert(ConnectionLocality(true,false,-1,"")==0);
 assert(ConnectionLocality(true,true,0,"")==1);
 assert(ConnectionLocality(true,true,1,"127.0.0.1")==1);
 assert(ConnectionLocality(true,true,1,"example.org")==2);
 assert(ConnectionLocality(true,true,0,"example.org")!=1);
 std::cout<<"map channel ok\n";
}

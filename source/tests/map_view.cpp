#include "MapView.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace MyClientMap;
int main(){
 {// Wheel zoom keeps the block under the cursor where it is; drag moves the view with the mouse.
  View v{100,200,2,0};
  auto under=v.Unproject(40,-30);
  View z=ZoomAt(v,40,-30,2.0,0.25,16);
  assert(std::abs(z.zoom-4)<1e-9);
  auto after=z.Unproject(40,-30);
  assert(std::abs(after.first-under.first)<1e-9 && std::abs(after.second-under.second)<1e-9);
  assert(std::abs(ZoomAt(v,0,0,1000,0.25,16).zoom-16)<1e-9 && std::abs(ZoomAt(v,0,0,0.001,0.25,16).zoom-0.25)<1e-9);
  View p=PanBy(v,20,-10);   // mouse moved 20 right, 10 up: the map follows, the view centre goes left / down
  assert(std::abs(p.x-90)<1e-9 && std::abs(p.z-205)<1e-9);
 }

 View v{100,200,2,0};auto p=v.Project(105,203);assert(p.first==10 && p.second==6);
 auto w=v.Unproject(10,6);assert(w.first==105 && w.second==203);
 v.angle=3.141592653589793/2;p=v.Project(105,200);assert(std::abs(p.first)<0.001 && std::abs(p.second+10)<0.001);
 w=v.Unproject(p.first,p.second);assert(std::abs(w.first-105)<0.001 && std::abs(w.second-200)<0.001);
 assert(InCircle(0,0,10)&&!InCircle(10,10,10));
 std::cout<<"map view ok\n";
}

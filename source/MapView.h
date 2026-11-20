#pragma once
#include <cmath>
#include <utility>
namespace MyClientMap {
struct View {
 double x=0,z=0,zoom=2,angle=0;
 std::pair<double,double> Project(double wx,double wz)const {double dx=wx-x,dz=wz-z,c=std::cos(angle),s=std::sin(angle);return {(dx*c+dz*s)*zoom,(-dx*s+dz*c)*zoom};}
 std::pair<double,double> Unproject(double sx,double sy)const {double c=std::cos(angle),s=std::sin(angle);sx/=zoom;sy/=zoom;return {x+sx*c-sy*s,z+sx*s+sy*c};}
};
// The world map's wheel: zoom by `factor` (clamped to minZoom..maxZoom) keeping the block under the cursor (sx, sy:
// pixels from the map's centre) under it.
inline View ZoomAt(View v,double sx,double sy,double factor,double minZoom,double maxZoom){
 const auto under=v.Unproject(sx,sy);
 double zoom=v.zoom*factor;zoom=zoom<minZoom?minZoom:zoom>maxZoom?maxZoom:zoom;
 v.zoom=zoom;const auto now=v.Unproject(sx,sy);v.x+=under.first-now.first;v.z+=under.second-now.second;return v;
}
// The world map's drag: the mouse moved (dx, dy) pixels and the map moves with it.
inline View PanBy(View v,double dx,double dy){
 const double c=std::cos(v.angle),s=std::sin(v.angle);dx/=v.zoom;dy/=v.zoom;
 v.x-=dx*c-dy*s;v.z-=dx*s+dy*c;return v;
}
inline bool InCircle(double x,double y,double radius){return x*x+y*y<=radius*radius;}
}

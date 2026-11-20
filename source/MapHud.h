#pragma once
// Included by MyClient.cpp after the HUD drawing helpers. Game data stays in MapChannel;
// this file owns only the HUD's cache, image crops and pixels.
#include "GameDataMirror.h"
#include "MapStore.h"
#include "MapView.h"
namespace MinimapHud {
inline MyClientMap::Store& Maps(){static MyClientMap::Store store(MyClientGameData::MirrorRoot().parent_path()/L"maps");return store;}
inline MyClientMap::Shared snapshot{};
inline bool ready=false,open=false,dragging=false;
inline MyClientMap::View worldView{};
inline POINT drag{},cursor{};
inline uint64_t lastAt=0,lastFlush=0;
inline std::filesystem::path lastFolder;
inline std::map<unsigned,ItemIcons::Image> faces;
inline std::string imageStamp;
inline uint64_t stampChecked=0;
inline bool saveFailed=false;
inline void Tick(bool enabled,bool mobs){
 if(enabled && GetTickCount64()-stampChecked>=1000){
  stampChecked=GetTickCount64();auto stamp=MyClientGameData::ReadStamp(MyClientGameData::MirrorRoot());
  if(stamp!=imageStamp){imageStamp=stamp;faces.clear();}
 }
 MinecraftDataProvider::SetMapWanted(enabled,mobs);
 ready=enabled && MinecraftDataProvider::GetMap(snapshot);
 if(ready && snapshot.at!=lastAt){
  saveFailed=!Maps().Update(snapshot);
  if(saveFailed){ready=false;return;}
  lastAt=snapshot.at;
  if(lastFolder!=Maps().Folder()){lastFolder=Maps().Folder();worldView.x=snapshot.x;worldView.z=snapshot.z;open=false;}
 }
 if(GetTickCount64()-lastFlush>=3000){saveFailed=!Maps().Flush();Maps().Trim();lastFlush=GetTickCount64();}
}
inline const ItemIcons::Image& Face(unsigned type){
 auto [it,inserted]=faces.try_emplace(type&255);if(!inserted)return it->second;
 auto f=MyClientMap::FaceOf(type);if(!f)return it->second;
 auto packs=MyClientGameData::MirrorRoot()/L"data"/L"resource_packs";
 std::vector<std::filesystem::path> roots{packs/L"vanilla"};std::error_code ec;
 for(std::filesystem::directory_iterator i(packs,ec),end;!ec && i!=end;i.increment(ec))if(i->is_directory(ec) && i->path().filename()!=L"vanilla")roots.push_back(i->path());
 ItemIcons::Image image;
 for(auto const& root:roots){
  auto base=root/std::filesystem::path(f->path);bool ok=ItemIcons::DecodeWic(base.wstring()+L".png",image);
  if(!ok){std::ifstream input(std::filesystem::path(base.wstring()+L".tga"),std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});ok=ItemIcons::DecodeTga(bytes,image);}
  if(!ok || f->x+f->w>image.w || f->y+f->h>image.h)continue;
  auto& crop=it->second;crop.w=f->w;crop.h=f->h;crop.bgra.resize(size_t(crop.w)*crop.h);
  for(int y=0;y<crop.h;++y)for(int x=0;x<crop.w;++x)crop.bgra[y*crop.w+x]=image.bgra[(y+f->y)*image.w+x+f->x];
  break;
 }
 return it->second;
}
inline void Blit(HDC dc,int x,int y,int w,int h,const std::vector<uint32_t>& pixels){
 BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 SetDIBitsToDevice(dc,x,y,w,h,0,0,0,h,pixels.data(),&bi,DIB_RGB_COLORS);
}
inline void Arrow(HDC dc,int x,int y,double angle,int size=12){
 POINT points[4];double c=std::cos(angle),s=std::sin(angle);int px[4]={0,-7,0,7},py[4]={-12,8,4,8};
 for(int i=0;i<4;++i)points[i]={LONG(x+(px[i]*c-py[i]*s)*size/12),LONG(y+(px[i]*s+py[i]*c)*size/12)};
 auto pen=CreatePen(PS_SOLID,2,RGB(12,12,16));auto brush=CreateSolidBrush(RGB(255,255,255));auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,brush);Polygon(dc,points,4);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);DeleteObject(brush);
}
inline void Draw(HDC dc,int x,int y,int width,int height,MyClientMap::View view,bool circle,bool mobs,bool border,COLORREF borderColor){
 width=std::clamp(width,1,8192);height=std::clamp(height,1,8192);
 const int cx=width/2,cy=height/2,radius=std::min(cx,cy)-3;
 const double cosAngle=std::cos(view.angle),sinAngle=std::sin(view.angle);
 std::vector<uint32_t> pixels(size_t(width)*height,0xff101016);
 for(int py=0;py<height;++py)for(int px=0;px<width;++px){
  double dx=px-cx,dy=py-cy;auto& pixel=pixels[size_t(py)*width+px];
  if(circle && !MyClientMap::InCircle(dx,dy,radius)){pixel=MyClientMap::InCircle(dx,dy,radius+3)?0xff565662:0xffff00ff;continue;}
  const auto at=std::make_pair(view.x+(dx*cosAngle-dy*sinAngle)/view.zoom,view.z+(dx*sinAngle+dy*cosAngle)/view.zoom);
  if(at.first < -30000000 || at.first>30000000 || at.second < -30000000 || at.second>30000000)continue;
  auto cell=Maps().Color(int(std::floor(at.first)),int(std::floor(at.second)));if(cell&0x80000000u)pixel=0xff000000u|(cell&0xffffff);
 }
 if(circle && mobs && MyClientMap::AutoMobs(snapshot.locality,snapshot.realm!=0))for(unsigned i=0;i<snapshot.mobCount && i<MyClientMap::MaxMobs;++i){
  auto const& mob=snapshot.mobs[i];auto at=view.Project(mob.x,mob.z);if(!std::isfinite(at.first)||!std::isfinite(at.second))continue;
  if(!MyClientMap::InCircle(at.first,at.second,radius-9))continue;
  int mx=cx+int(at.first),my=cy+int(at.second);auto const& face=mob.player?ItemIcons::Image{}:Face(mob.type);
  const uint32_t dot=mob.player?0xffffffffu:mob.enemy?0xffff4040u:0xff55dd77u;
  for(int sy=-7;sy<=7;++sy)for(int sx=-7;sx<=7;++sx){
   int px=mx+sx,py=my+sy;if(px<0||py<0||px>=width||py>=height)continue;
   bool edge=std::abs(sx)==7||std::abs(sy)==7;uint32_t value=dot;
   if(face.w){if(edge){if(!mob.enemy||!border)continue;value=0xff000000u|MyClientMap::Rgb(GetRValue(borderColor),GetGValue(borderColor),GetBValue(borderColor));}
    else {value=face.bgra[((sy+6)*face.h/13)*face.w+(sx+6)*face.w/13];if((value>>24)<128)continue;}}
   else if(sx*sx+sy*sy>9)continue;
   pixels[size_t(py)*width+px]=value;
  }
 }
 Blit(dc,x,y,width,height,pixels);
 auto player=view.Project(snapshot.x,snapshot.z);
 if(ready && std::abs(player.first)<cx-14 && std::abs(player.second)<cy-14)Arrow(dc,x+cx+int(player.first),y+cy+int(player.second),snapshot.yaw*3.141592653589793/180+3.141592653589793-view.angle);
 if(circle){auto north=view.Project(view.x,view.z-100);double angle=std::atan2(north.second,north.first);int nx=x+cx+int(std::cos(angle)*(radius-12)),ny=y+cy+int(std::sin(angle)*(radius-12));DrawTextCentered(dc,nx-8,ny-8,16,16,L"N",RGB(255,255,255),14);}
 Maps().Trim();
}
}

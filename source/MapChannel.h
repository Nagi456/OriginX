#pragma once
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <algorithm>
namespace MyClientMap {
constexpr uint32_t Magic=0x4d434d50,Version=1;
constexpr int Window=128,MaxMobs=128;
struct Mob {float x,z;uint32_t type;uint8_t enemy,player;uint16_t pad;};
struct Shared {
 uint32_t magic,version,size,pid;uint64_t at,hudAt;
 int32_t originX,originZ,dimension;uint32_t locality;
 float x,y,z,yaw;uint32_t wantMap,wantMobs,mobCount,realm;
 char world[96],server[64];Mob mobs[MaxMobs];
 uint32_t cells[Window*Window];int16_t heights[Window*Window];
};
static_assert(sizeof(Mob)==16 && sizeof(Shared)==100592);
static_assert(std::is_trivially_copyable<Shared>::value);
static_assert(offsetof(Shared,mobs)==240 && offsetof(Shared,cells)==2288 && offsetof(Shared,heights)==67824);
inline bool Valid(const Shared& s,uint32_t pid) {return s.magic==Magic && s.version==Version && s.size==sizeof(s) && s.pid==pid && s.mobCount<=MaxMobs && s.locality<=2 && s.realm<=1;}
inline bool Fresh(uint64_t at,uint64_t now) {return at && at<=now && now-at<=3000;}
inline uint32_t ConnectionLocality(bool canRead,bool hasConnection,int type,std::string_view host) {
 if(!canRead || !hasConnection)return 0;
 const bool loopback=host=="127.0.0.1" || host=="localhost" || host=="::1";
 if(loopback || (type==0 && host.empty()))return 1;
 return host.empty()?0u:2u;
}
inline bool AutoMobs(uint32_t locality,bool realm) {return locality==1 || realm;}
inline uint32_t Cell(uint32_t rgb,unsigned depth,bool read) {return (rgb&0xffffff)|(std::min(depth,127u)<<24)|(read?0x80000000u:0);}
inline void CopyText(char* out,size_t size,std::string_view text) {if(!size)return;size_t n=std::min(size-1,text.size());while(n<text.size() && n && (static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;std::memcpy(out,text.data(),n);out[n]=0;}
inline void Shift(Shared& s,int dx,int dz) {
 std::array<uint32_t,Window*Window> cells{};std::array<int16_t,Window*Window> heights{};
 if(dx>-Window && dx<Window && dz>-Window && dz<Window)
  for(int z=0;z<Window;++z)for(int x=0;x<Window;++x){int sx=x+dx,sz=z+dz;if(sx>=0 && sx<Window && sz>=0 && sz<Window){cells[z*Window+x]=s.cells[sz*Window+sx];heights[z*Window+x]=s.heights[sz*Window+sx];}}
 std::copy(cells.begin(),cells.end(),s.cells);std::copy(heights.begin(),heights.end(),s.heights);
}
#ifdef _WIN32
class Channel {
 HANDLE mapping_=nullptr,mutex_=nullptr;Shared* data_=nullptr;
public:
 Channel()=default;Channel(const Channel&)=delete;Channel& operator=(const Channel&)=delete;
 ~Channel(){Close();}Shared* Data(){return data_;}
 void Close(){if(data_)UnmapViewOfFile(data_);if(mapping_)CloseHandle(mapping_);if(mutex_)CloseHandle(mutex_);data_=nullptr;mapping_=mutex_=nullptr;}
 bool Lock(){if(!mutex_)return false;DWORD r=WaitForSingleObject(mutex_,0);if(r==WAIT_ABANDONED){if(data_)data_->at=0;ReleaseMutex(mutex_);return false;}return r==WAIT_OBJECT_0;}
 void Unlock(){ReleaseMutex(mutex_);}
 bool Open(DWORD pid,bool producer){Close();auto name=std::wstring(L"Local\\MyClient.Map.v1.")+std::to_wstring(pid);
 mutex_=producer?CreateMutexW(nullptr,FALSE,(name+L".Lock").c_str()):OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(name+L".Lock").c_str());
 if(!mutex_)return false;
 mapping_=producer?CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str()):OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
 if(mapping_)data_=static_cast<Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
 if(!data_){Close();return false;}if(producer){if(!Lock()){Close();return false;}*data_={};data_->magic=Magic;data_->version=Version;data_->size=sizeof(Shared);data_->pid=pid;Unlock();}return true;}
};
#endif
}

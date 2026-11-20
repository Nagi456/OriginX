#pragma once
#include "MapData.h"
#include "MapChannel.h"
#include <map>
#include <set>
namespace MyClientMap {
// HUD-owned cache. Only visible/read tiles are loaded; clean tiles are evicted.
class Store {
 struct Cached {Tile tile;bool dirty=false;uint64_t used=0;};
 std::filesystem::path root_,folder_,worldFolder_;
 std::map<std::pair<int,int>,Cached> tiles_;
 uint64_t serial_=0;
 const Tile* colorTile_=nullptr;int colorTx_=0,colorTz_=0;
 std::filesystem::path Path(int x,int z)const {return folder_/(std::to_string(x)+"_"+std::to_string(z)+".bin");}
 Cached& Get(int x,int z){auto key=std::make_pair(x,z);auto [it,inserted]=tiles_.try_emplace(key);if(inserted)LoadTile(Path(x,z),it->second.tile);it->second.used=++serial_;return it->second;}
public:
 explicit Store(std::filesystem::path root):root_(std::move(root)){}
 ~Store(){Flush();}
 const std::filesystem::path& Folder()const{return folder_;}
 const std::filesystem::path& WorldFolder()const{return worldFolder_;}
 bool Flush(){if(folder_.empty() || std::none_of(tiles_.begin(),tiles_.end(),[](auto const& entry){return entry.second.dirty;}))return true;std::error_code ec;std::filesystem::create_directories(folder_,ec);if(ec)return false;bool ok=true;for(auto& [key,v]:tiles_)if(v.dirty){if(SaveTile(Path(key.first,key.second),v.tile))v.dirty=false;else ok=false;}return ok;}
 void Trim(){colorTile_=nullptr;while(tiles_.size()>512){auto oldest=tiles_.end();for(auto it=tiles_.begin();it!=tiles_.end();++it)if(!it->second.dirty && (oldest==tiles_.end()||it->second.used<oldest->second.used))oldest=it;if(oldest==tiles_.end())break;tiles_.erase(oldest);}}
 bool Update(const Shared& s){
  if(!s.at)return true;
  auto world=root_/WorldKey(std::string(s.world,strnlen(s.world,96)),std::string(s.server,strnlen(s.server,64)));
  auto folder=world/DimensionName(s.dimension);
  if(folder!=folder_){if(!Flush())return false;tiles_.clear();colorTile_=nullptr;folder_=folder;worldFolder_=world;}
  // North edge has no neighbour: wait until a later window covers both cells.
  for(int z=1;z<Window;++z)for(int x=0;x<Window;++x){int i=z*Window+x;auto c=s.cells[i];if(!(c&0x80000000u))continue;
   int north=(s.cells[i-Window]&0x80000000u)?s.heights[i-Window]:s.heights[i];
   uint32_t shaded=Cell(Shade(c&0xffffff,s.heights[i],north,(c>>24)&127),0,true);
   int wx=s.originX+x,wz=s.originZ+z;auto& v=Get(TileOf(wx),TileOf(wz));auto& dest=v.tile.cells[InTile(wz)*TileSize+InTile(wx)];if(dest!=shaded){dest=shaded;v.dirty=true;}}
  Trim();return true;
 }
 uint32_t Color(int x,int z){if(folder_.empty())return 0;int tx=TileOf(x),tz=TileOf(z);if(!colorTile_||tx!=colorTx_||tz!=colorTz_){colorTile_=&Get(tx,tz).tile;colorTx_=tx;colorTz_=tz;}return colorTile_->cells[InTile(z)*TileSize+InTile(x)];}
 bool ClearWorld(){if(worldFolder_.empty()||worldFolder_.parent_path()!=root_)return false;std::error_code ec;std::filesystem::remove_all(worldFolder_,ec);if(ec)return false;tiles_.clear();colorTile_=nullptr;return true;}
};
}

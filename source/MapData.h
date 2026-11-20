#pragma once
#ifdef _WIN32
#include <windows.h>
#endif
#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
namespace MyClientMap {
constexpr int TileSize=256;
constexpr uint32_t TileMagic=0x504d584f, TileVersion=1;
constexpr uint32_t Rgb(unsigned r,unsigned g,unsigned b) {return (r<<16)|(g<<8)|b;}
inline uint32_t Shade(uint32_t rgb,int height,int north,unsigned water) {
 unsigned factor=height>north?255:height<north?180:220;
 if(water) factor=factor*(255-std::min(water,16u)*8)/255;
 return Rgb(((rgb>>16)&255)*factor/255,((rgb>>8)&255)*factor/255,(rgb&255)*factor/255);
}
inline int TileOf(int v) {return v/TileSize-(v%TileSize<0);}
inline int InTile(int v) {int r=v%TileSize;return r<0?r+TileSize:r;}
inline std::string WorldKey(std::string name,std::string_view server) {
 if(!server.empty()) name+="@"+std::string(server);
 for(char& c:name) if(static_cast<unsigned char>(c)<=32 || std::string_view("<>:\"/\\|?*").find(c)!=std::string_view::npos)c='_';
 for(size_t i=0;i<name.size() && name[i]=='.';++i)name[i]='_';
 if(name.size()>120) {size_t n=120;while(n && (static_cast<unsigned char>(name[n])&0xc0)==0x80)--n;name.resize(n);}
 while(!name.empty() && name.back()=='.')name.back()='_' ;
 if(name.empty())return "unnamed";
 // Windows refuses CON, PRN, AUX, NUL, COM1-9 and LPT1-9 as a name stem, in any case and with any extension.
 std::string stem=name.substr(0,name.find('.'));for(char& c:stem)if(c>='a'&&c<='z')c=char(c-32);
 const bool device=stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||
  (stem.size()==4&&(stem.compare(0,3,"COM")==0||stem.compare(0,3,"LPT")==0)&&stem[3]>='1'&&stem[3]<='9');
 return device?"_"+name:name;
}
// The game's save-folder id (minecraftWorlds\<id>): letters, digits, + = - _ only, so it is safe as a path part.
inline bool ValidLevelId(std::string_view id) {
 if(id.empty()||id.size()>64)return false;
 for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='+'||c=='='||c=='-'||c=='_'))return false;
 return true;
}
// Same-named worlds on this PC keep separate maps: the save-folder id follows the name (the id is kept whole).
inline std::string WorldIdentity(std::string_view name,std::string_view id,size_t capacity) {
 std::string tail=ValidLevelId(id)?"#"+std::string(id):std::string();
 if(capacity<=tail.size()+1)return std::string(name.substr(0,capacity?capacity-1:0));
 size_t n=std::min(name.size(),capacity-1-tail.size());
 while(n && n<name.size() && (static_cast<unsigned char>(name[n])&0xc0)==0x80)--n;
 return std::string(name.substr(0,n))+tail;
}
// Map colours (Java's map palette) for blocks whose game map colour is empty because the biome tints them.
inline uint32_t FallbackColor(std::string_view name) {
 auto has=[&](std::string_view part){return name.find(part)!=std::string_view::npos;};
 if(has("grass_block")||name=="minecraft:grass"||has("grass_path")) return has("path")?Rgb(151,109,77):Rgb(127,178,56);
 if(has("water")) return Rgb(64,64,255);
 if(has("leaves")||has("vine")||has("waterlily")||has("lily_pad")||has("tallgrass")||has("short_grass")||has("fern")) return Rgb(0,124,0);
 if(has("snow")) return Rgb(255,255,255);
 if(has("_log")||has("_wood")||has("_stem")||has("hyphae")||has("planks")) return Rgb(143,119,72);
 if(has("cactus")||has("sugar_cane")||has("reeds")||has("bamboo")||has("azalea")||has("moss")) return Rgb(0,124,0);
 return 0;
}
inline std::string DimensionName(int n) {return n==0?"overworld":n==1?"nether":n==2?"end":"dim"+std::to_string(n);}
struct Face {unsigned type;const char* path;int x,y,w,h,imageW,imageH;bool enemy;};
// Provisional vanilla face crops; unknown types are rendered as dots.
inline constexpr Face kMobFaces[]={
 {32,"textures/entity/zombie/zombie",8,8,8,8,64,64,true},
 {33,"textures/entity/creeper/creeper",8,8,8,8,64,32,true},
 {34,"textures/entity/skeleton/skeleton",8,8,8,8,64,32,true},
 {35,"textures/entity/spider/spider",40,12,8,8,64,32,true},
 {36,"textures/entity/zombie_pigman",8,8,8,8,64,64,false},
 {37,"textures/entity/slime/slime",8,8,8,8,64,32,true},
 {38,"textures/entity/enderman/enderman",8,8,8,8,64,32,true},
 {40,"textures/entity/spider/cave_spider",40,12,8,8,64,32,true},
 {42,"textures/entity/magma_cube",8,8,8,8,64,32,true},
 {43,"textures/entity/blaze",8,8,8,8,64,32,true},
 {45,"textures/entity/witch",26,2,8,10,64,128,true},
 {47,"textures/entity/skeleton/wither_skeleton",8,8,8,8,64,32,true},
 {48,"textures/entity/guardian",16,16,12,12,64,64,true},
 {49,"textures/entity/guardian_elder",16,16,12,12,64,64,true},
 {10,"textures/entity/chicken",2,3,4,6,64,32,false},
 {11,"textures/entity/cow/cow",6,6,8,8,64,32,false},
 {12,"textures/entity/pig/pig",8,8,8,8,64,32,false},
 {13,"textures/entity/sheep/sheep",6,6,6,6,64,32,false},
 {14,"textures/entity/wolf/wolf",4,4,6,6,64,32,false},
 {15,"textures/entity/villager/villager",8,8,8,10,64,64,false},
 {16,"textures/entity/cow/mooshroom",6,6,8,8,64,32,false},
 {17,"textures/entity/squid",16,16,12,12,64,32,false},
 {18,"textures/entity/rabbit/brown",3,3,5,4,64,32,false},
 {19,"textures/entity/bat",6,6,6,6,64,64,false},
 {20,"textures/entity/iron_golem",8,8,8,10,128,128,false},
 {21,"textures/entity/snow_golem",8,8,8,8,64,64,false},
 {22,"textures/entity/ocelot",5,4,5,4,64,32,false},
 {28,"textures/entity/polarbear",7,7,7,7,128,64,false},
 {46,"textures/entity/horse/horse_white",0,13,5,5,64,64,false},
 {39,"textures/entity/silverfish",0,2,3,3,64,32,true}
};
inline const Face* FaceOf(uint32_t type) {for(auto const& f:kMobFaces)if(f.type==(type&255))return &f;return nullptr;}
struct Tile {std::array<uint32_t,TileSize*TileSize> cells{};};
inline void Write32(std::ostream& s,uint32_t v){for(int i=0;i<4;++i)s.put(static_cast<char>(v>>(8*i)));}
inline uint32_t Read32(std::istream& s){uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(static_cast<unsigned char>(s.get()))<<(8*i);return v;}
inline bool LoadTile(const std::filesystem::path& path,Tile& out) {
 std::error_code ec;if(std::filesystem::file_size(path,ec)!=8+4*TileSize*TileSize || ec)return false;
 std::ifstream f(path,std::ios::binary);if(!f || Read32(f)!=TileMagic || Read32(f)!=TileVersion)return false;
 Tile tmp;for(auto& c:tmp.cells)c=Read32(f);if(!f)return false;out=tmp;return true;
}
inline bool SaveTile(const std::filesystem::path& path,const Tile& tile) {
 auto tmp=path;tmp+=".tmp";{std::ofstream f(tmp,std::ios::binary|std::ios::trunc);if(!f)return false;Write32(f,TileMagic);Write32(f,TileVersion);for(auto c:tile.cells)Write32(f,c);f.flush();if(!f)return false;}
#ifdef _WIN32
 // MoveFileExW atomically replaces an existing tile without deleting the old one first.
 return MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 std::error_code ec;std::filesystem::rename(tmp,path,ec);return !ec;
#endif
}
}

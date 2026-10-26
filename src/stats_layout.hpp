#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include "resident_stats_sites.hpp"
#include "motion_pool_sites.hpp"
#include <vector>
namespace stats_layout {
using u32=std::uint32_t;
struct payload {u32 base,live,capacity;bool operator==(const payload&)const=default;};
struct descriptor {bool installed;u32 capacity,pool_live,pool_mask,trailing_delta,manager_size,entries,heroes,hero_count,definition_count;payload hero,npc;bool operator==(const descriptor&)const=default;};
inline constexpr descriptor stock{false,31,0x9aa0,0x9b20,0,0x12334,0x9b28,0x120dc,0x12330,0xbba8,{0x11218,0x11860,22},{0x11864,0x11e80,11}};
inline constexpr descriptor raised{true,320,0x63b64,0x64068,0x5a548,0x777e0,0x64070,0x6c624,0x6c878,0x660f0,{0x6b760,0x6bda8,22},{0x6c87c,0x777dc,320}};
inline descriptor active=stock;
inline bool motion_installed=false;
enum class consumer:unsigned {discord=1,forced_teams=2,revival=4,xp=8};
inline unsigned consumers=0;
inline void register_consumer(consumer c){consumers|=static_cast<unsigned>(c);}
inline bool registered(){return (consumers&15)==15;}
inline const descriptor& current(){return active;}
inline void publish(const descriptor& d){active=d;}
inline void publish_motion(bool value){motion_installed=value;}
inline std::optional<std::string> expected(u32 va,std::string_view hex,const descriptor& d,bool motion){
 if(hex.size()%2)return std::nullopt;
 std::vector<unsigned char> bytes;bytes.reserve(hex.size()/2);
 auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
 for(std::size_t i=0;i<hex.size();i+=2){auto a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0)return std::nullopt;bytes.push_back(static_cast<unsigned char>((a<<4)|b));}
 const auto end=std::uint64_t(va)+bytes.size();
 auto overlaps=[&](u32 a,u32 n){return std::uint64_t(a)<end&&std::uint64_t(va)<std::uint64_t(a)+n;};
 auto edit=[&](u32 a,unsigned size,u32 value){for(unsigned i=0;i<size;++i)if(std::uint64_t(a)+i>=va&&std::uint64_t(a)+i<end)bytes[a+i-va]=static_cast<unsigned char>(value>>(8*i));};
 if(d.installed)for(const auto& s:resident_stats_sites::sites){
  if(s.type==resident_stats_sites::kind::operand)edit(s.va+s.offset,s.size,s.new_value);
  else if(s.type!=resident_stats_sites::kind::incave&&overlaps(s.va,s.span))return std::nullopt;
 }
 if(motion)for(const auto& s:motion_pool_sites::sites){
  if(s.type==motion_pool_sites::kind::operand)edit(s.va+s.offset,s.size,s.new_value);
  else if(overlaps(s.va,static_cast<u32>(s.hex.size()/2)))return std::nullopt;
 }
 constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(hex.size());
 for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;
}
inline std::optional<std::string> expected(u32 va,std::string_view hex){return expected(va,hex,active,motion_installed);}
}

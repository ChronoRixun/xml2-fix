#pragma once
// Pure XML1 drop selection. Spawn placement and descriptor construction are
// intentionally separate operations, with their own random-draw accounting.
#include "xml1_economy_rules.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace xml1_roll_rules {
struct input {
 int category=3;
 bool leader=false;
 int highest_level=1;
 std::optional<int> actor_level;
};
struct drop {
 const xml1_economy_rules::record* row=nullptr;
 std::string name;
 unsigned definition=0;
 int amount=1;
};
struct result {
 std::array<drop,4> drops{};
 unsigned count=0;
 int G=1,bucket=0;
 unsigned draws=0,eligible=0;
};

// Services: double unit(), int integer(low, high) inclusive, bool
// obtained(record), bool xtreme_full(). Random calls use only the injected
// lockstep stream. rows must be in resolved definition-index order and outlive
// the result. Runtime resolves generated names; definition=0 with row=nullptr
// is not an actual definition-zero selection.
template<class Services>
result x1_roll(const std::vector<xml1_economy_rules::record>& rows,
               const input& in,Services& services){
 result out;out.G=std::min(5,in.highest_level/10+1);
 auto unit=[&](){++out.draws;return static_cast<double>(services.unit());};
 auto integer=[&](int low,int high){++out.draws;return services.integer(low,high);};
 constexpr std::array<float,6> chances{0,.15f,.30f,.45f,.65f,1};
 // XBE 0x710d4-0x710e0 uses test ah,0x41: equality passes, including
 // category zero when the random stream returns exactly zero.
 if(!in.leader && unit()>chances[std::clamp(in.category,0,5)])return out;
 constexpr std::array<float,4> thresholds{.005f,.02f,.07f,.65f};
 // XBE 0x71134 explicitly stores only this draw as binary32. Other
 // comparisons consume the unrounded random result directly from ST0.
 const float bucket_roll=static_cast<float>(unit());
 for(unsigned i=0;i<thresholds.size();++i)if(bucket_roll<thresholds[i]){out.bucket=static_cast<int>(i)+1;break;}
 unsigned count=in.leader?4u:1u;
 if(in.leader && out.bucket!=1 && out.bucket!=2){
  const double reroll=unit();out.bucket=reroll<.1f?2:reroll<.2f?3:4;
 }
 if(in.highest_level<5 && out.bucket>0 && out.bucket<3)out.bucket=3;
 const xml1_economy_rules::record* selected=nullptr;
 for(const auto& row:rows){
  if(!row.group || (*row.group!=1 && (*row.group<=0 || *row.group<out.G-2 || *row.group>out.G)))continue;
  if(!row.frequency || *row.frequency!=out.bucket)continue;
  if((row.flags&1u) && services.obtained(row))continue;
  ++out.eligible;
  // XBE 0x71249-0x71267 stores 1/n to float before comparison and
  // accepts equality (test ah,0x41/jp). Do not change this to strict <.
  const float reciprocal=static_cast<float>(1.0/static_cast<double>(out.eligible));
  if(unit()<=reciprocal)selected=&row;
 }
 std::string name=selected?selected->name:"money1";
 bool money=selected==nullptr;
 if(money && unit()<.4f){name="XTREME_PIP";money=false;}
 if(name=="XTREME_PIP" && services.xtreme_full())money=true;
 int amount=1;
 if(money){
  selected=nullptr;
  if(in.actor_level){
   const int level=*in.actor_level;
   // XBE keeps these products in x87 registers until ftol, rather than
   // rounding each multiplication to binary32. Double exactly represents
   // 31-bit U * signed-short level * -10 at XML1's CRT-initialized
   // 53-bit x87 precision. Runtime normalizes selection to 53-bit/RN
   // and restores incoming PC control before construction/spawn.
   amount=level-static_cast<int>(unit()*level*-10.0);
  }else{
   const int squared=out.G*out.G;
   amount=squared+static_cast<int>(unit()*squared*25.0);
  }
  name="MONEY"+std::to_string(integer(1,3));
  const int splits=std::min(integer(2,4),amount);
  if(splits<=0)return out;
  count=static_cast<unsigned>(splits);amount/=splits;
 }
 for(unsigned i=0;i<count;++i){
  if(in.leader && i>0){
   const int pick=integer(0,services.xtreme_full()?1:3);
   name=pick==0?"HEALTH_ITEM":pick==1?"ENERGY_ITEM":"XTREME_PIP";
   selected=nullptr;amount=1;
  }
  out.drops[out.count++]={selected,name,selected?selected->definition:0,amount};
 }
 return out;
}
}

#pragma once
#include "lcsave/layout.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>
namespace native_save_layout_rules {
inline constexpr unsigned rule_id=47,rule_revision=1,structural_revision=1;
inline std::string normal(std::string_view value){
 const auto begin=value.find_first_not_of(" \t\r\n"),end=value.find_last_not_of(" \t\r\n");
 std::string text=begin==value.npos?"":std::string(value.substr(begin,end-begin+1));
 for(auto& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');return text;
}
struct selection {bool requested=false;std::string refusal;};
inline selection choose(std::string_view format,bool entity_capacity,const std::optional<std::string>& layout,const std::optional<std::string>& enhancements){
 if(normal(format)!="lc"||!layout||normal(*layout).empty())return {};
 if(normal(*layout)!="w2")return {true,"[Limits] SaveLayout must be w2 or absent; no native layout was changed"};
 if(!entity_capacity)return {true,"SaveLayout=w2 needs EntityCapacity=1"};
 if(enhancements&&normal(*enhancements)!="4096")return {true,"SaveLayout=w2 needs ItemEnhancements absent or 4096"};
 return {true,{}};
}
inline constexpr std::array<std::uint32_t,4> agreement_values(){
 const auto& n=lcsave::w2_layout;
 return {2u|(n.record_count<<8)|(structural_revision<<16),n.definitions|(n.enhancement_records<<16),n.instances|(n.world_slots<<16),n.objectives|(n.talent_multiplier<<16)};
}
struct usage {unsigned defs=0,enh=0,att=0,inst=0,world=0,obj=0,talents=0,zones=0;};
inline std::string status(const usage& u){
 const auto& n=lcsave::w2_layout;
 return "layout W2; defs "+std::to_string(u.defs)+"/"+std::to_string(n.definitions)+"; enh "+std::to_string(u.enh)+"/"+std::to_string(n.enhancement_records)+
  "; att "+std::to_string(u.att)+"/"+std::to_string(n.attached_enhancements)+"; inst "+std::to_string(u.inst)+"/"+std::to_string(n.instances)+
  "; world "+std::to_string(u.world)+"/"+std::to_string(n.world_slots)+"; obj "+std::to_string(u.obj)+"/"+std::to_string(n.objectives)+
  "; talents "+std::to_string(u.talents)+"/"+std::to_string(n.talent_pool)+"; zones "+std::to_string(u.zones)+"/"+std::to_string(n.record_count);
}
}

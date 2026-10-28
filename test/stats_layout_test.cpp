#include "stats_layout.hpp"
#include "resident_stats_sites.hpp"
#include "discord_rules.hpp"
#include "forced_teams_rules.hpp"
#include "revival_cost_rules.hpp"
#include "xp_curve_rules.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main(){
 using namespace stats_layout;
 for(const auto& s:resident_stats_sites::sites){auto off=expected(s.va,s.hex,stock,false);assert(off&&*off==s.hex);}
 const auto moved=expected(0x44b6b0,"8b8130230100c3",raised,false);
 assert(moved&&*moved=="8b8178c80600c3"); // moved hero count
 assert(!expected(0x448ff0,"56578b7c240c",raised,false)); // replaced function
 assert(!expected(0x448c76,"83fa1f7d41",raised,false));
 assert(expected(0x56a3e3,"81bfe0460000f4010000",stock,true)=="81bf10760c0000580000");
 unsigned guards=0;
 auto check=[&](const auto& table){for(const auto& g:table){
  for(const auto& d:{stock,raised}){auto r=expected(g.va,g.hex,d,true);assert(r);std::vector<unsigned char> b(r->size()/2);for(unsigned i=0;i<b.size();++i)b[i]=limits_rules::hex_byte(*r,i);
   assert(limits_rules::matches(b.data(),r->c_str()));for(unsigned i=0;i<b.size();++i){b[i]^=1;assert(!limits_rules::matches(b.data(),r->c_str()));b[i]^=1;}
  }++guards;
 }};
 check(discord_rules::guards);check(forced_teams_rules::guards);check(revival_cost_rules::guards);check(xp_curve_rules::guards);
 assert(raised.manager_size==raised.npc.base+0xaf64&&raised.npc.capacity>=raised.capacity);
 std::printf("PASS TR7 shared layout: %u consumer guards, stock/raised spans, protected replacements, shared R3 signature\n",guards);
}

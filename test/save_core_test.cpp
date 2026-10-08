#include "save_core_rules.hpp"
#include <cstdio>
using namespace save_core_rules;
int main()
{
 int failures=0;
 auto check=[&](bool value,const char* name){ if(!value){++failures;std::printf("FAIL %s\n",name);} };
 check(capture_incomplete==1,"approved capture bit mask");
 check(capture_unobserved==2,"legacy completeness is unobserved");
 check(ordinal_fits(1)&&ordinal_fits(256)&&!ordinal_fits(0)&&!ordinal_fits(257),"ordinal boundary independent of live handles");
 check(placed_match(3,"prop_a","twin",3,"PROP_A","twin"),"names ignore ASCII case");
 check(!placed_match(3,"prop_a","twin",4,"prop_a","twin"),"same-name twins need actual placement");
 check(!placed_match(3,"prop_a","twin",3,"prop_b","twin"),"definition-only numbering mismatch");
 check(!full_campaign_load(1)&&full_campaign_load(0)&&full_campaign_load(2),"saved-stats mode is not full campaign load");
 struct M { unsigned ordinal,placement_index;std::string definition_name,instance_name; };
 std::vector<M> mapping{{2,1,"prop_a","twin"}};
 auto* selected=placement_source(mapping,1,"prop_a","twin");
 std::array<std::uint16_t,256> frozen{};frozen[1]=7;
 check(selected && frozen[selected->ordinal-1]==7 && frozen[0]==0,"matched placement bypasses modeled empty death slot");
 check(selected && selected->ordinal==2,"same-name skipped factory resolves actual placement before map-slot death");
 check(!placement_source(mapping,0,"prop_a","twin"),"previously skipped placement cannot inherit a captured twin");
 lifetime s;
 s.stage_load();
 check(!s.may_save(),"pending native load blocks all slots");
 s.finish_load(true);
 check(s.may_save(),"native completion allows supported saves");
 s.commit_save(17,2);
 check(s.serial==17 && s.sequence==2,"sequence committed only after write");
 s.new_game();
 check(s.serial==0&&s.sequence==0&&s.may_save(),"actual reset clears campaign identity");
 s.stage_load();s.finish_load(false);
 check(!s.may_save(),"native failure blocks propagation");
 s.new_game();check(s.may_save(),"reset clears failed session");
 s.commit_save(17,2);
 check(!s.begin_native(1,false),"unstaged stats load preserves campaign identity");s.finish_native(1,true);
 check(s.serial==17&&s.sequence==2&&s.may_save(),"stats-only completion cannot reset campaign");
 s.stage_load();check(!s.begin_native(1,true),"staged stats load is not campaign commit");s.finish_native(1,true);
 check(s.load_pending&&s.serial==17&&!s.may_save(),"stats-only load cannot complete staged campaign");
 check(s.begin_native(0,true),"full disk load owns staged campaign");s.finish_native(0,true);
 check(s.may_save(),"full native completion finishes staged load");
 check(s.begin_native(2,false)&&!s.may_save()&&s.serial==0,"fresh transfer blocks reentrant writes");
 s.write_blocked=true;s.finish_native(2,true);
 check(!s.may_save(),"load return cannot clear failure observed during load");
 check(capacity_fits(10,365,375)&&!capacity_fits(11,365,375)&&!capacity_fits(1,376,375),"preflight against free capacity");
 const capture_bound* arena=nullptr;
 for(const auto& site:capture_bounds)if(site.va==0x41a427)arena=&site;
 check(arena&&arena->skip==0x41a44c&&arena->length==8&&std::string(arena->compare)=="81f900380000",
       "native string-arena exhaustion branch is observed before an unwritten name handle escapes");
 const auto bridge=numbering_bridge(0x100000,0x200000);
 check(bridge.size()>20 && bridge[5]==0x44,"numbering bridge uses original loop frame");
 for(const auto& site:capture_bounds){
  const auto b=bound_bridge(site,0x100000,0x200000);
  const auto n=limits_rules::hex_size(site.compare);
  check(b[n]==0x0f&&b[n+1]==0x82,"capacity guard preserves whole compare and checks unsigned below");
 }
 std::printf("core runtime rules: %d failures\n",failures);
 return failures?1:0;
}

#include "save_core_rules.hpp"
#include "save_guard_rules.hpp"
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
 std::vector<std::uint8_t> strings(0x4008+0x7800);
 auto put=[&](std::size_t offset,std::uint32_t value){std::memcpy(strings.data()+offset,&value,4);};
 put(0x4004,20);put(4+0x123*4,2);std::memcpy(strings.data()+0x4008+2,"Prop_A",7);
 auto read=[&](std::uint32_t offset,void* out,std::size_t n){if(offset+n>strings.size())return false;std::memcpy(out,strings.data()+offset,n);return true;};
 std::string resolved;
 check(general_name(0x0b000123,read,resolved)&&resolved=="Prop_A","entity name is a general interned handle, not a pointer");
 check(general_name(0,read,resolved)&&resolved.empty(),"empty instance handle is valid");
 check(!general_name(4096,read,resolved),"general string slot bound");
 put(0x4004,0x7801);check(!general_name(0x123,read,resolved),"general arena bound");
 put(0x4004,4);put(4+0x123*4,4);check(!general_name(0x123,read,resolved),"name offset must be inside used arena");
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
 {
  std::vector<std::string> logged; std::string notice;
  const std::string ledger="zone zone/x/y: legacy capture provenance inferred (unknown completeness)\n"
    "zone zone/x/y: conversation digest unknown or changed: bits reset\n"
    "zone zone/x/z: entity placement/name changed: record not applied\n";
  forward_load_ledger(ledger,notice,[&](const std::string& line){logged.push_back(line);});
  check(logged.size()==3,"load ledger logs every explanation separately");
  check(notice.find("zone state or conversation bits reset")!=std::string::npos &&
        notice.find("conversation digest")!=std::string::npos && notice.find("record not applied")!=std::string::npos,
        "load reset notice includes conversation and zone discard reasons");
  notice="pending notice";logged.clear();
  forward_load_ledger("zone zone/x/y: legacy capture provenance inferred (unknown completeness)",notice,[&](const std::string& line){logged.push_back(line);});
  check(logged.size()==1 && notice=="pending notice","inferred provenance logs without a reset notice");
 }
 {
  const auto good=native_completion(0,true,3,true,false,false,false);
  check(good.native_ok&&good.confirm_campaign&&!good.refuse_campaign,"completed disk application confirms only its campaign");
  const auto registry=native_completion(0,true,3,true,false,false,true);
  check(!registry.native_ok&&!registry.confirm_campaign&&registry.refuse_campaign,"registry refusal during native application is not success");
  const auto failed=native_completion(0,true,3,false,false,false,false);
  save_guard_rules::refusal_latches paths;
  if(failed.refuse_campaign)paths.refuse("campaign3","native failure");
  lifetime reset;reset.new_game();
  check(!paths.reason("campaign3").empty(),"new campaign reset cannot unlock a failed disk-load destination");
  paths.refuse("profile","profile still awaiting proven native application");
  if(good.confirm_campaign)paths.successful_load("campaign3");
  check(paths.reason("campaign3").empty()&&!paths.reason("profile").empty(),"campaign completion leaves profile refusal independent");
  const auto stats=native_completion(1,true,3,true,false,false,false);
  check(stats.native_ok&&!stats.confirm_campaign&&!stats.refuse_campaign,"stats-only application cannot confirm campaign or profile");
 }
 {
  // The frozen restore map takes every installed layout's whole ordinal map, and never writes past itself.
  using namespace save_format_rules;
  check(restore_map_entries>=retail_profile.ordinals && restore_map_entries>=e10a2_profile.ordinals &&
        restore_map_entries>=w2_profile.ordinals,"restore map holds the largest layout's ordinals (W2 4096)");
  struct guarded { std::array<std::uint16_t,restore_map_entries> map; std::array<std::uint8_t,0x2000> after; };
  static guarded g;
  std::vector<std::uint16_t> record(w2_profile.ordinals*2+16);
  for(std::size_t i=0;i<record.size();++i) record[i]=static_cast<std::uint16_t>((0x5a00+(i&0xff))|1);
  const auto plain=[](void* to,const void* from,std::size_t n){std::memcpy(to,from,n);return true;};
  for(const auto* p:{&retail_profile,&e10a2_profile,&w2_profile})
  {
   g.map.fill(0);g.after.fill(0xcc);
   const bool copied=freeze_restore_map(g.map.data(),g.map.size(),record.data(),p->ordinals,plain);
   bool intact=true;for(auto b:g.after)intact=intact&&b==0xcc;
   check(copied && std::memcmp(g.map.data(),record.data(),p->ordinals*2)==0,
         p==&w2_profile?"W2 ordinal map frozen whole":p==&e10a2_profile?"E10A2 ordinal map frozen whole":"retail ordinal map frozen whole");
   check(intact,p==&w2_profile?"W2 freeze leaves the statics after the map untouched":"freeze leaves the statics after the map untouched");
  }
  g.after.fill(0xcc);
  const bool refused=!freeze_restore_map(g.map.data(),1024,record.data(),w2_profile.ordinals,plain);
  bool intact=true;for(auto b:g.after)intact=intact&&b==0xcc;
  check(refused && intact,"a layout larger than the map is refused, nothing copied");
 }
 std::printf("core runtime rules: %d failures\n",failures);
 return failures?1:0;
}

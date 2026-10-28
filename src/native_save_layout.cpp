#include "native_save_layout.hpp"
#include "native_save_layout_rules.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "save_format.hpp"
#include "save_limits.hpp"
#include "save_layout_view.hpp"
#include "item_capacity_rules.hpp"
#include "gameplay_agreement.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <algorithm>
#include <bit>
#ifdef XML2_SAVE_STARTUP_FAULT_TEST
#include "../test/w2_startup_faults.hpp"
#endif
namespace native_save_layout {
namespace {
bool requested=false,published=false,settled=false;
std::string failure;
void refuse(patch_transaction::transaction& transaction,const std::string& why){
 failure=why.empty()?"the W2 owners could not be prepared":why;
 logger::write("native save layout: W2 refused: %s; startup refused with stock layout bytes",failure.c_str());
 patch_transaction::contribution veto;veto.owner="NativeSaveLayout W2 prerequisite";
 veto.requires_written.push_back({0xffffffff,"refused W2 must not commit any part of the startup union"});transaction.add(std::move(veto));
}
template<class T>T read(unsigned address){T value{};patch_transaction::read_memory(address,&value,sizeof value);return value;}
}
void prepare(patch_transaction::transaction& transaction,HMODULE game){
 const auto selected=native_save_layout_rules::choose(ini::text(L"Save",L"Format").value_or(""),ini::flag(L"Limits",L"EntityCapacity",false),ini::text(L"Limits",L"SaveLayout"),ini::text(L"Limits",L"ItemEnhancements"));
 requested=selected.requested;
 if(!requested){entity_capacity::prepare(transaction,game);return;}
#ifdef XML2_SAVE_STARTUP_FAULT_TEST
 w2_startup_faults::snapshot("before-union");
#endif
 if(!selected.refusal.empty()){refuse(transaction,selected.refusal);return;}
 entity_capacity::prepare(transaction,game,save_format_rules::w2_profile);
 if(!entity_capacity::prepared_for_w2())refuse(transaction,"SaveLayout=w2 could not be installed: "+entity_capacity::startup_error());
}
void after_commit(patch_transaction::outcome outcome){
 if(!requested||settled)return;settled=true;
 published=outcome==patch_transaction::outcome::committed&&entity_capacity::installed().raised&&save_limits::installed()&&
  std::string_view(save_format::installed_profile().layout_id)=="W2";
 if(!published&&failure.empty())failure="SaveLayout=w2 startup transaction "+std::string(patch_transaction::name(outcome))+"; the W2 layout was not published";
 const bool agreed=gameplay_agreement::register_rule(native_save_layout_rules::rule_id,native_save_layout_rules::rule_revision,native_save_layout_rules::agreement_values(),published);
 if(!agreed&&published){published=false;failure="SaveLayout=w2 could not register verified rule 47 NativeSaveLayout";}
 logger::write("native save layout: rule 47 NativeSaveLayout %s%s%s",published?"verified":"unverified; online closed; startup refused",failure.empty()?"":"; ",failure.c_str());
 if(published)logger::write("native save layout: %s",status().c_str());
#ifdef XML2_SAVE_STARTUP_FAULT_TEST
 w2_startup_faults::snapshot("after-union");
#endif
}
bool startup_allowed(){return !requested||published;}
const std::string& startup_error(){return failure;}
bool installed(){return published;}
std::string status(){
 if(!published)return {};
 native_save_layout_rules::usage u;
 const auto manager=read<unsigned>(0x72a514),instances=read<unsigned>(0x72a524),attached=read<unsigned>(0x717b30),talents=read<unsigned>(0x77339c);
 if(manager){u.defs=read<std::uint16_t>(manager+0x2588);u.enh=read<unsigned>(manager+0x605c)+read<unsigned>(manager+0x6060);for(unsigned i=0;i<32;++i)u.world+=std::popcount(read<unsigned>(manager+item_capacity_rules::world_bitmap+i*4));}
 if(instances)u.inst=read<unsigned>(instances+item_capacity_rules::instance_count_offset(true));
 if(attached)u.att=read<unsigned>(attached+item_capacity_rules::attached_count_offset(true));
 u.obj=save_limits::objective_registered_count();
 if(talents)for(unsigned i=0;i<3;++i)u.talents=std::max(u.talents,read<unsigned>(talents+save_limits::talent_count_offset(i)));
 u.zones=entity_capacity::zone_record_count();
 return native_save_layout_rules::status(u);
}
}

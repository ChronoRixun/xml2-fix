#include "xml1_economy.hpp"
#include "xml1_economy_digest.hpp"
#include "xml1_roll.hpp"
#include "xml1_economy_prices.hpp"
#include "xml1_economy_shop.hpp"
#include "xml1_grab_bag.hpp"
#include "native_save_layout.hpp"
#include "entity_capacity.hpp"
#include "item_capacity.hpp"
#include "item_capacity_rules.hpp"
#include "lcsave/layout.hpp"
#include "lcsave/lcsave.hpp"
#include "gameplay_agreement.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <atomic>
#include <fstream>
#include <iterator>
#include <memory>

namespace xml1_economy {
namespace {
using namespace xml1_economy_rules;
using make_t=descriptor*(__fastcall*)(void*,void*,descriptor*,void*,int,int,int,int,int);
template<class T>T native(unsigned va){return reinterpret_cast<T>(static_cast<std::uintptr_t>(va));}
unsigned address(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T read(const void* p,unsigned offset=0){T v{};std::memcpy(&v,static_cast<const std::uint8_t*>(p)+offset,sizeof v);return v;}
template<class T>T method(void* p,unsigned slot){return reinterpret_cast<T>((*static_cast<void***>(p))[slot/4]);}
bool wanted=false,hooks=false,settled=false,registered=false,held=false;
std::atomic<bool> active=false;
std::atomic<unsigned> refusals=0;
std::array<bool,1024> refused_once{};
std::array<int,1024> rows{};
sidecar data;
plan mode=default_plan;
std::string failure;
void* table=nullptr;
make_t original=nullptr;
void register_result(bool verified){
 if(registered)return;registered=true;
 auto values=agreement_values(mode,data);if(roller::installed())values[0]|=1;
 if(prices::installed())values[0]|=2;
 if(shop::installed())values[0]|=4|16;
 if(grab_bag::installed())values[0]|=8;
 values[0]|=64;
 const bool agreed=gameplay_agreement::register_rule(rule_id,rule_revision,values,verified);
 active.store(verified&&agreed,std::memory_order_release);
 lcsave::set_xml1_economy_verifier(&installed);
 logger::write("economy: rule 52 Xml1Economy %s%s%s",active.load()?"verified":"unverified; online closed",failure.empty()?"":"; ",failure.c_str());
 if(held){held=false;gameplay_agreement::resume_finalization();}
}
void refused(unsigned index,const char* why){
 ++refusals;
 if(!refused_once[index]){refused_once[index]=true;logger::write("economy: construction refused for definition %u (%s): %s",index,data.rows[static_cast<unsigned>(rows[index])].name.c_str(),why);}
}
std::optional<unsigned> definition_index(const void* definition){
 if(!table||!definition)return {};
 const auto base=address(table)+item_capacity_rules::definitions,at=address(definition);
 if(at<base||(at-base)%0x3c)return {};
 const auto index=(at-base)/0x3c;
 if(index>=1024||index>=read<std::uint16_t>(table,0x2588))return {};
 return index;
}
void __fastcall loader_end(void* manager,void*){
 native<void(__fastcall*)(void*,void*)>(0x47ce80)(manager,nullptr);
 table_loaded(manager);
}
struct code_storage {
 void* p=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 ~code_storage(){if(p)VirtualFree(p,0,MEM_RELEASE);}
};
// Storage lives as long as a published entry can reach it, including uncertain
// rollback. No global executable bytes are changed until the startup union.
std::vector<std::shared_ptr<code_storage>> retained;
void prepare_impl(patch_transaction::transaction& transaction,HMODULE game,bool requested,std::string_view text,plan selected){
 wanted=requested;if(!wanted)return;
 mode=selected;data=parse(text);rows.fill(-1);
 if(!data.ok()){failure="Data/x1economy.txt line "+std::to_string(data.error.line)+": "+data.error.message;logger::write("economy: %s",failure.c_str());return;}
 if(address(game)!=0x400000){failure="XMen2.exe is not at its retail base";return;}
 patch_transaction::contribution c;c.owner="Xml1Economy constructor and table resolution";
 c.guards.push_back({0x47bb50,patch_transaction::bytes_of("83ec08538b5c2414"),"vt+0xe0 plain item constructor entry"});
 c.guards.push_back({0x4806ad,patch_transaction::bytes_of("e8cec7ffff"),"item-loader final record walk"});
 std::string why;if(!patch_transaction::verify_expectations(c,why)){failure=why;return;}
 auto storage=std::make_shared<code_storage>();
 if(!storage->p){failure="constructor trampoline allocation failed";return;}
 const auto displaced=c.guards[0].bytes;
 std::memcpy(storage->p,displaced.data(),displaced.size());
 const auto tail=patch_transaction::relocation(address(storage->p)+8,"e9",true,0x47bb58,0,"constructor native continuation");
 std::memcpy(static_cast<std::uint8_t*>(storage->p)+8,tail.bytes.data(),tail.bytes.size());
 DWORD old=0;
 if(!VirtualProtect(storage->p,4096,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),storage->p,13)){failure="constructor trampoline protection/flush failed";return;}
 c.writes.push_back(patch_transaction::relocation(0x47bb50,"e9",true,address(&make_instance),3,"all direct and virtual item-constructor calls"));
 c.writes.push_back(patch_transaction::relocation(0x4806ad,"e8",true,address(&loader_end),0,"resolve economy names after native record walk"));
 c.publish=[storage]{retained.push_back(storage);original=reinterpret_cast<make_t>(storage->p);hooks=true;};
 c.abandon=[]{hooks=false;};
 c.retain=[storage]{retained.push_back(storage);original=reinterpret_cast<make_t>(storage->p);hooks=false;};
 if(!transaction.add(std::move(c)))failure="startup transaction is sealed";
 else if(!roller::contribute(transaction,failure)||!prices::contribute(transaction,failure)||!shop::contribute(transaction,failure)||!grab_bag::contribute(transaction,failure)||!digest::contribute(transaction,failure)){
  // Any failed required contribution must refuse the entire union, including
  // stale legacy leader ownership and every prices/sales retail guard.
  patch_transaction::contribution veto;veto.owner="Xml1Economy required contribution: "+failure;
  veto.requires_written.push_back({0xffffffff,"economy ownership/guards refused"});
  transaction.add(std::move(veto));
 }
}
}
void prepare(patch_transaction::transaction& transaction,HMODULE game){
 const bool requested=ini::flag(L"Game",L"Xml1Economy",false);
 if(!requested)return; // OFF performs no file reads, allocation, guards or writes.
 if(!entity_capacity::prepared_for_w2()){
  wanted=true;failure="Xml1Economy requires SaveLayout=w2 prepared in the startup union";return;
 }
 std::ifstream stream(logger::module_dir()/L"Data"/L"x1economy.txt",std::ios::binary);
 if(!stream){wanted=true;failure="Data/x1economy.txt line 1: cannot open sidecar";return;}
 const std::string text{std::istreambuf_iterator<char>(stream),{}};
 if(stream.bad()){wanted=true;failure="Data/x1economy.txt line 1: sidecar read failed";return;}
 prepare_impl(transaction,game,true,text,default_plan);
}
void prepare_for_testing(patch_transaction::transaction& transaction,HMODULE game,bool requested,std::string_view text,plan selected){prepare_impl(transaction,game,requested,text,selected);}
void after_commit(patch_transaction::outcome outcome){
 if(!wanted||settled)return;settled=true;
 if(!native_save_layout::installed()||item_capacity::installed_capacity()!=lcsave::w2_layout.attached_enhancements){
  failure="Xml1Economy requires verified rule 47 NativeSaveLayout (SaveLayout=w2)";register_result(false);return;
 }
 if(!hooks||!roller::installed()||!prices::installed()||!shop::installed()||!grab_bag::installed()||!digest::installed()||outcome!=patch_transaction::outcome::committed||!data.ok()){
  if(failure.empty())failure="constructor/table hooks not committed: "+std::string(patch_transaction::name(outcome));
  register_result(false);return;
 }
 held=true;gameplay_agreement::defer_finalization();
 logger::write("economy: constructor plan %s installed at 0x47bb50; awaiting Data/items name resolution at 0x4806ad; online closed",mode==plan::zero_draw?"A":"1roll");
}
void table_loaded(void* manager){
 if(!wanted||!hooks||registered)return;
 table=manager;
 const auto lookup=[&](std::string_view name)->std::optional<unsigned>{
  // W2 widens this native by-name lookup's return to AX; 0xffff is not found.
  const auto index=method<std::uint16_t(__fastcall*)(void*,void*,const char*)>(manager,0x78)(manager,nullptr,name.data());
  if(index==0xffff||index>=read<std::uint16_t>(manager,0x2588))return {};return index;
 };
 if(!data.resolve(lookup)){
  failure="Data/x1economy.txt line "+std::to_string(data.error.line)+": "+data.error.message;register_result(false);return;
 }
 for(unsigned i=0;i<data.rows.size();++i)rows[data.rows[i].definition]=static_cast<int>(i);
 register_result(true);
 logger::write("economy: %s",status().c_str());
}
bool installed(){return active.load(std::memory_order_acquire);}
const record* record_for(unsigned index){return installed()&&index<rows.size()&&rows[index]>=0?&data.rows[static_cast<unsigned>(rows[index])]:nullptr;}
const std::vector<record>& records(){return data.rows;}
bool is_economy(const void* definition){if(!installed())return false;const auto i=definition_index(definition);return i&&rows[*i]>=0;}
const record* record_for_definition(const void* definition){const auto i=definition_index(definition);return i?record_for(*i):nullptr;}
descriptor* __fastcall make_instance(void* manager,void*,descriptor* out,void* definition,int level,int prefixes,int suffixes,int special_a,int special_b){
 if(!installed()||!plain(level,prefixes,suffixes,special_a,special_b)||!is_economy(definition))return original(manager,nullptr,out,definition,level,prefixes,suffixes,special_a,special_b);
 const auto index=*definition_index(definition);
 std::array<int,8> own{};unsigned count=0;
 for(int i=0;i<8;++i){const auto record=method<std::int16_t(__fastcall*)(void*,void*,int)>(definition,0x10)(definition,nullptr,i);if(record>=0)own[count++]=record;}
 // Getters initialize W2 backing if needed, without entering the unsafe native
 // instance allocator. Capacities/offsets are exactly the verified W2 layout.
 const auto instances=native<void*(__cdecl*)()>(0x483610)(),attached=native<void*(__cdecl*)()>(0x45fb00)();
 const auto& layout=lcsave::w2_layout;
 if(!instances||!attached||!preflight(read<unsigned>(instances,item_capacity_rules::instance_count_offset(true)),read<unsigned>(attached,item_capacity_rules::attached_count_offset(true)),count,layout.instances,layout.attached_enhancements)){
  *out={0,nullptr};refused(index,"W2 instance or attached-record capacity exhausted");return out;
 }
 if(mode==plan::one_roll)return original(manager,nullptr,out,definition,level,prefixes,suffixes,special_a,special_b);
 method<descriptor*(__fastcall*)(void*,void*,descriptor*)>(manager,0xe4)(manager,nullptr,out);
 auto* instance=native<void*(__fastcall*)(void*,void*)>(0x480f30)(out,nullptr);
 std::memcpy(static_cast<std::uint8_t*>(instance)+4,&definition,4);
 for(unsigned i=0;i<count;++i){
  const auto result=native<bool(__fastcall*)(void*,void*,descriptor*,int,float)>(0x481910)(instance,nullptr,reinterpret_cast<descriptor*>(static_cast<std::uint8_t*>(instance)+0x10),own[i],0.0f);
  // Preflight on this single game thread guarantees success. A failure means
  // corrupt state; close agreement rather than publish a verified installation.
  if(!result){gameplay_agreement::reject_unregistered_rule("Xml1Economy attach failed after capacity preflight");active=false;logger::write("economy: unexpected attach failure for definition %u",index);}
 }
 native<void(__fastcall*)(void*,void*)>(0x481170)(instance,nullptr);
 return out;
}
std::string status(){
 if(!wanted)return {};
 return "econ on; plan "+std::string(mode==plan::zero_draw?"A":"1roll")+"; defs "+std::to_string(installed()?data.rows.size():0)+"; digest "+digest_hex(data.digest).substr(0,8)+shop::status()+roller::status()+"; refused "+std::to_string(refusals.load());
}
}

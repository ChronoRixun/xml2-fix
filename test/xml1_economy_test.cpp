// T-LOOT-CTOR. Maps owner-supplied retail instructions into this test's image;
// executes native W2 pools/reset/constructor/attach/link/cache/bonus consumers.
// No game process, data package, desktop input or registry access.
#include "xml1_economy.hpp"
#include "xml1_economy_digest.hpp"
#include "lcsave/lcsave.hpp"
#include "item_capacity.hpp"
#include "item_capacity_rules.hpp"
#include "gameplay_capabilities.hpp"
#include "log.hpp"
#include "xml1_grab_bag_rules.hpp"
#include <xmmintrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

extern "C" std::uint8_t fixture_space[0x700000];
namespace native_save_layout {bool verified=true;bool installed(){return verified;}}
namespace entity_capacity {bool prepared_for_w2(){return native_save_layout::verified;}}
namespace save_format {void latch_reconstruction_failure(const char*){}}
namespace ini {bool wanted=false;bool flag(const wchar_t*,const wchar_t*,bool){return wanted;}}
namespace logger {
 std::string last;unsigned refusal_logs=0;
 const std::filesystem::path& module_dir(){static const std::filesystem::path p="economy-test-missing-sidecar-root";return p;}
 void write(const char* format,...){char b[1024];va_list a;va_start(a,format);std::vsnprintf(b,sizeof b,format,a);va_end(a);last=b;if(last.find("construction refused")!=std::string::npos)++refusal_logs;}
}
namespace gameplay_agreement {
 gameplay_capabilities::record rules;unsigned holds=0,registrations=0;bool good=false;
 bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> values,bool verified){++registrations;good=rules.add(id,revision,values,verified);return good;}
 void reject_unregistered_rule(const char*){rules.invalidate();good=false;}
 void defer_finalization(){++holds;}
 void resume_finalization(){--holds;}
}
namespace {
using namespace xml1_economy_rules;
using xml1_economy::descriptor;
using make_t=descriptor*(__fastcall*)(void*,void*,descriptor*,void*,int,int,int,int,int);
constexpr unsigned first_economy=160,equipment_count=116,economy_count=132,legacy_definition=first_economy+economy_count;
unsigned own_count(unsigned ordinal){return ordinal<equipment_count||ordinal==economy_count?ordinal%5+1:0;}
int checks=0,failures=0;
void check(bool value,const std::string& what){++checks;if(!value)++failures;std::printf("%s %s\n",value?"PASS":"FAIL",what.c_str());}
void* at(unsigned va){return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va));}
unsigned addr(const void* p){return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T native(unsigned va){return reinterpret_cast<T>(at(va));}
template<class T>T read(const void* p,unsigned off=0){T v;std::memcpy(&v,static_cast<const std::uint8_t*>(p)+off,sizeof v);return v;}
template<class T>void put(void* p,unsigned off,T v){std::memcpy(static_cast<std::uint8_t*>(p)+off,&v,sizeof v);}
void jump(unsigned va,const void* target){const auto w=patch_transaction::relocation(va,"e9",true,addr(target),0,"test environment edge");std::memcpy(at(va),w.bytes.data(),w.bytes.size());FlushInstructionCache(GetCurrentProcess(),at(va),w.bytes.size());}
bool load(const char* path){
 std::ifstream f(path,std::ios::binary);std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),{}};
 if(b.size()<0x1000)return false;
 const auto pe=read<unsigned>(b.data(),0x3c),sections=pe+24+read<std::uint16_t>(b.data(),pe+20);
 for(unsigned i=0;i<read<std::uint16_t>(b.data(),pe+6);++i){const auto row=sections+i*40,va=0x400000+read<unsigned>(b.data(),row+12),size=read<unsigned>(b.data(),row+16),off=read<unsigned>(b.data(),row+20);
  if(off+std::uint64_t(size)>b.size()||va<addr(fixture_space)||va+std::uint64_t(size)>addr(fixture_space)+sizeof fixture_space)return false;
  std::memcpy(at(va),b.data()+off,size);
 }return true;
}
std::string sidecar_file(const std::string& body,unsigned n){return "x1economy 1\ncount "+std::to_string(n)+"\nsha256 "+digest_hex(sha256_fips::digest(reinterpret_cast<const std::uint8_t*>(body.data()),body.size()))+"\n"+body;}
std::vector<std::uint8_t> manager;
std::array<std::uint8_t,0x18000> names{};
void* __cdecl get_manager(){return manager.data();}
void* __cdecl get_names(){return names.data();}
int __cdecl compare(const char* a,const char* b){return std::strcmp(a,b);}
int __cdecl as_number(const char* text){return std::atoi(text);}
// Native x87 consumers call the CRT ftol veneer. Values here are integral and
// exactly representable; both constructors use the same authored fixture edge.
__declspec(naked) void ftol(){__asm {
 sub esp,16
 fnstcw word ptr[esp+8]
 mov ax,word ptr[esp+8]
 or ax,0c00h
 mov word ptr[esp+10],ax
 fldcw word ptr[esp+10]
 fistp qword ptr[esp]
 fldcw word ptr[esp+8]
 mov eax,[esp]
 mov edx,[esp+4]
 add esp,16
 ret
}}
const char* __fastcall property_text(void*,void*,int){return "137";}
unsigned __fastcall property_key(void*,void*,void*,const char*){return 1;}
void __fastcall empty_walk(void*,void*){} // test the completion bridge independently of XML parsing
// Synthetic enhancement definitions model the builder's single-valued record
// graph. The retail consumers still do the interpolation, list iteration, cache
// arithmetic, rounding and valid-bit publication.
struct pool {void** vt;void* object;};
void* pool_vt[3]{};void* record_vt[16]{};void* node_vt[32]{};void* affecter_vt[32]{};
std::array<std::uint8_t,0x40> node{},affecter{};
pool node_pool{pool_vt,node.data()},affecter_pool{pool_vt,affecter.data()};
bool __fastcall valid(void*,void*,unsigned h){return h==1;}
void* __fastcall get(void* p,void*,unsigned){return static_cast<pool*>(p)->object;}
descriptor* __fastcall record_head(void*,void*,descriptor* out){*out={1,&node_pool};return out;}
descriptor* __fastcall affecter_head(void*,void*,descriptor* out){*out={1,&affecter_pool};return out;}
bool __fastcall yes(void*,void*){return true;}
void __fastcall noop(void*,void*){}
int __fastcall type(void*,void*){return 0;}
float __fastcall fixed_bonus(void*,void*,int,int,float* maximum){*maximum=11.0f;return 11.0f;}
bool __fastcall apply_bonus(void*,void*,int amount,int* sum,float*){*sum+=amount;return true;}
void __cdecl construct_instance(void* p){native<void*(__fastcall*)(void*,void*)>(0x4834d0)(p,nullptr);}
void __cdecl construct_attached(void* p){native<void*(__fastcall*)(void*,void*)>(0x45fa60)(p,nullptr);}
void setup_table(unsigned count){
 using namespace item_capacity_rules;
 manager.resize(manager_size);std::memcpy(manager.data(),at(0x688314),4);put(manager.data(),0,0x688314u);
 put(manager.data(),0x2588,static_cast<std::uint16_t>(count));put(at(0x72a514),0,addr(manager.data()));
 // The native by-name walker uses interned name IDs. Only its external name
 // table and strcmp are supplied; W2's real lookup is called by the loader.
 put(names.data(),0x8004,2+count*40);
 jump(0x602140,&get_names);jump(0x672562,&compare);jump(0x480a00,&get_manager);
 jump(0x67233c,&as_number);jump(0x67217c,&ftol);
 put(at(0x688314),0x7c,addr(&property_key));put(at(0x688314),0xac,addr(&property_text));
 jump(0x483480,&construct_instance);jump(0x45fa10,&construct_attached);
 pool_vt[0]=reinterpret_cast<void*>(&valid);pool_vt[2]=reinterpret_cast<void*>(&get);
 record_vt[0x18/4]=reinterpret_cast<void*>(&record_head);record_vt[0x28/4]=reinterpret_cast<void*>(&yes);
 record_vt[0x34/4]=reinterpret_cast<void*>(&apply_bonus);record_vt[0x38/4]=reinterpret_cast<void*>(&noop);record_vt[0x3c/4]=reinterpret_cast<void*>(&noop);
 node_vt[0x78/4]=reinterpret_cast<void*>(&affecter_head);put(node.data(),0,addr(node_vt));
 affecter_vt[0x2c/4]=reinterpret_cast<void*>(&type);affecter_vt[0x34/4]=reinterpret_cast<void*>(&fixed_bonus);affecter_vt[0x38/4]=reinterpret_cast<void*>(&type);put(affecter.data(),0,addr(affecter_vt));
 for(unsigned i=0;i<count;++i){
  auto* d=manager.data()+definitions+i*0x3c;native<void*(__fastcall*)(void*,void*)>(0x47b8f0)(d,nullptr);
  const auto ordinal=i>=first_economy?i-first_economy:economy_count+1;
  put(d,8,i+1);put(d,0x10,ordinal<equipment_count||i==legacy_definition?2u:1u);
  if(ordinal<equipment_count&&ordinal%3==0)put(d,0x3b,std::uint8_t(4)); // unique, not obtained
  const unsigned offset=2+i*40;put(names.data(),4+(i+1)*4,offset);std::snprintf(reinterpret_cast<char*>(names.data())+0x8008+offset,40,"item%04u",i);
  for(unsigned slot=0;slot<8;++slot)put(d,0x24+2*slot,std::uint16_t(0xffff));
  const unsigned n=own_count(ordinal);
  for(unsigned j=0;j<n;++j){const unsigned slot=(i+j)%8,record=i*5+j;put(d,0x24+2*slot,static_cast<std::uint16_t>(record));auto* r=manager.data()+records+record*0x28;put(r,0,addr(record_vt));put(manager.data(),enhancement_bitmap+(record/32)*4,read<unsigned>(manager.data(),enhancement_bitmap+(record/32)*4)|(1u<<(record%32)));}
  if(n<8)put(d,0x24+2*((i+n)%8),std::uint16_t(0x8000)); // a negative other than the sentinel
 }
}
struct snapshot {std::array<std::uint8_t,0x34> instance;std::vector<std::array<std::uint8_t,0x14>> attached;int bonus=0;unsigned draws=0,rng_state=0;};
snapshot capture(const descriptor& result){
 snapshot s;auto* instance=native<void*(__fastcall*)(void*,void*)>(0x480f30)(const_cast<descriptor*>(&result),nullptr);std::memcpy(s.instance.data(),instance,0x34);
 auto link=read<descriptor>(instance,0x10);unsigned iterations=0;
 while(link.pool&&iterations++<8){auto* p=native<void*(__fastcall*)(void*,void*)>(0x480f30)(&link,nullptr);std::array<std::uint8_t,0x14> bytes;std::memcpy(bytes.data(),p,bytes.size());
  int sum=0;float multiplier=1.0f;
  const bool evaluated=native<bool(__fastcall*)(void*,void*,int*,float*)>(0x45ef50)(p,nullptr,&sum,&multiplier);
  if(!evaluated||sum!=11||multiplier!=1.0f)s.bonus=-10000;else s.bonus+=sum;
  put(bytes.data(),8,0.0f);s.attached.push_back(bytes);link=read<descriptor>(p,0xc);
 }
 s.draws=read<unsigned>(at(0x7ad8b4));s.rng_state=read<unsigned>(at(0x6e0874));return s;
}
bool same(const snapshot& a,const snapshot& b){return a.instance==b.instance&&a.attached==b.attached&&a.bonus==b.bonus;}
unsigned allocation_calls=0;
descriptor* __fastcall allocation_spy(void* self,void*,descriptor* out){++allocation_calls;return native<descriptor*(__fastcall*)(void*,void*,descriptor*)>(0x47c990)(self,nullptr,out);}
#include "xml1_economy_prices_checks.hpp"
#include "xml1_economy_shop_checks.hpp"
#include "xml1_grab_bag_checks.hpp"
#include "xml1_economy_digest_checks.hpp"
}
LONG WINAPI fault(EXCEPTION_POINTERS* e){std::printf("FAULT %08lx EIP=%08lx ESP=%08lx EAX=%08lx ECX=%08lx EDX=%08lx\n",e->ExceptionRecord->ExceptionCode,e->ContextRecord->Eip,e->ContextRecord->Esp,e->ContextRecord->Eax,e->ContextRecord->Ecx,e->ContextRecord->Edx);return EXCEPTION_CONTINUE_SEARCH;}
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,&fault);
 const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");if(!path)return 2;
 const std::string scenario=argc>2?argv[2]:"A";
 DWORD old=0;VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old);
 check(addr(fixture_space)==0x401000&&load(path),"retail code mapped read-only from owner exe; no game launched");
 if(scenario=="off"||scenario=="zero"){
  ini::wanted=false;const auto before=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);patch_transaction::transaction tx;
  xml1_economy::prepare(tx,static_cast<HMODULE>(at(0x400000)));xml1_economy::after_commit(tx.commit());
  check(tx.contributions()==0&&gameplay_agreement::registrations==0&&xml1_economy::status().empty()&&std::equal(before.begin(),before.end(),fixture_space),"absent/zero key: no entry, no writes, no status, every mapped byte unchanged");
  std::printf("Economy execution: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;
 }
 if(scenario=="prerequisite"){
  native_save_layout::verified=false;ini::wanted=true;patch_transaction::transaction tx;xml1_economy::prepare(tx,static_cast<HMODULE>(at(0x400000)));xml1_economy::after_commit(tx.commit());
  check(!tx.contributions()&&!gameplay_agreement::good&&gameplay_agreement::registrations==1&&(!xml1_economy::installed()&&!lcsave::item_own_rebuilt_enabled()),"missing rule 47: no hook contribution, rule 52 unverified, online closed");return failures?1:0;
 }
 std::string body;for(unsigned i=0;i<economy_count;++i){char row[100];std::snprintf(row,sizeof row,"item%04u 1 2 %x %s %s - %u\n",i+first_economy,i<equipment_count?(i%3==0?0x51:0x50):(i%2?0x10:0),i==1?"0":i==2?"-":"137",i<equipment_count?"forge":"none",i);body+=row;}
 if(scenario=="shop"){
  body.clear();for(unsigned i=0;i<economy_count;++i){
   const int group=i<18?1:i<37?2:i<52?3:i<69?4:i<95?5:-1;
   const unsigned flags=i<equipment_count?(0x50|(i%3==0?1:0)|(i>=85&&i<95?2:0)):0;
   char row[120];std::snprintf(row,sizeof row,"item%04u %d %d %x 137 %s - %u\n",i+first_economy,group,i==5?3:2,flags,i<equipment_count?"forge":"none",i);body+=row;
  }
 }
 if(scenario=="bag"){
  body.clear();for(unsigned i=0;i<economy_count;++i){char row[120];const unsigned flags=i<equipment_count?(0x51|(i<20&&i%4<2?2:0)):0;
   std::snprintf(row,sizeof row,"item%04u %d 2 %x 137 %s - %u\n",i+first_economy,i<20?int(i/4+1):-1,flags,i<equipment_count?"forge":"none",i);body+=row;}
 }
 const auto text=sidecar_file(body,economy_count);const auto chosen=scenario=="1roll"?plan::one_roll:plan::zero_draw;
 patch_transaction::contribution w2;std::string why;check(item_capacity::contribute_w2(w2,why),"W2 complete pool and definition guards: "+why);
 patch_transaction::transaction tx;tx.add(std::move(w2));
 if(scenario=="legacy-count"||scenario=="legacy-pick")put(at(scenario=="legacy-count"?0x46e735:0x46e774),0,std::uint8_t(0xe9));
 if(scenario=="bag-pre-guard")put(at(0x5d3720),0,std::uint8_t(0xcc));
 if(scenario=="prices-pre-guard")put(at(0x5d2639),0,std::uint8_t(0xcc));
 if(scenario=="shop-pre-guard")put(at(0x5d3ff0),0,std::uint8_t(0xcc));
 if(scenario=="digest-pre-guard")put(at(0x5d3ba0),0,std::uint8_t(0xcc));
 if(scenario=="missingfile"){ini::wanted=true;xml1_economy::prepare(tx,static_cast<HMODULE>(at(0x400000)));}
 else xml1_economy::prepare_for_testing(tx,static_cast<HMODULE>(at(0x400000)),true,scenario=="sidecar"?"":text,chosen);
 if(scenario=="legacy-count"||scenario=="legacy-pick"||scenario=="prices-pre-guard"||scenario=="shop-pre-guard"||scenario=="bag-pre-guard"||scenario=="digest-pre-guard"){
  const auto before=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);
  check(tx.commit()==patch_transaction::outcome::refused_unresolved&&std::equal(before.begin(),before.end(),fixture_space),"failed required contribution vetoes entire W2/economy union without writes");
  xml1_economy::after_commit(tx.result());
  check(!gameplay_agreement::good&&(!xml1_economy::installed()&&!lcsave::item_own_rebuilt_enabled())&&gameplay_agreement::registrations==1,"refused required contribution leaves rule52 unverified, migration disabled and online closed");return failures?1:0;
 }
 if(scenario=="sidecar"||scenario=="missingfile"){
  check(tx.commit()==patch_transaction::outcome::committed,"W2 alone commits with missing economy sidecar");xml1_economy::after_commit(tx.result());
  check((!xml1_economy::installed()&&!lcsave::item_own_rebuilt_enabled())&&!gameplay_agreement::good&&gameplay_agreement::registrations==1,"missing sidecar unverified");return failures?1:0;
 }
 const auto stock=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);
 if(scenario.starts_with("guard")){
  const std::array<unsigned,34> guards{0x47bb50,0x4806ad,0x421f3f,0x5a9cd8,0x5a9d2c,0x481170,0x480dd8,0x5a9b8d,0x5d3ee9,0x5d404b,0x5d2639,
   0x49e64b,0x46a1a0,0x5d2a55,0x5d2b18,0x5d35d0,0x5d36c8,0x5d3a34,0x5d3b2a,0x5d2b70,0x5d3ff0,0x5d4104,0x5d4119,0x5d412e,0x480e20,0x5d3720,
   0x5d39c0,0x5d3ba0,0x5d3c31,0x5d3c81,0x5d3c97,0x5d3dd7,0x5d4135,0x5d3fda};
  const auto site=guards.at(static_cast<unsigned>(std::stoi(scenario.substr(5))));*static_cast<std::uint8_t*>(at(site))^=1;
  const auto before=std::vector<std::uint8_t>(fixture_space,fixture_space+sizeof fixture_space);check(tx.commit()!=patch_transaction::outcome::committed&&std::equal(before.begin(),before.end(),fixture_space),"guard mutation: entire W2/economy union refuses, zero writes");xml1_economy::after_commit(tx.result());check(!gameplay_agreement::good&&(!xml1_economy::installed()&&!lcsave::item_own_rebuilt_enabled()),"guard failure leaves rule 52 unverified");return failures?1:0;
 }
 if(scenario=="rollback"){
  patch_transaction::hooks h;h.after_apply=[]{*static_cast<std::uint8_t*>(at(0x47bb50))^=1;};
  check(tx.commit(&h)==patch_transaction::outcome::rolled_back&&std::equal(stock.begin(),stock.end(),fixture_space),"forced readback failure: union rollback restores every retail byte");xml1_economy::after_commit(tx.result());check(!gameplay_agreement::good&&(!xml1_economy::installed()&&!lcsave::item_own_rebuilt_enabled()),"rolled back rule unverified");return failures?1:0;
 }
 if(scenario=="rollback-failed"){
  std::array<std::uint8_t,8> installed_jump{};std::array<std::uint8_t,6> cache_jump{};patch_transaction::hooks h;
  h.after_apply=[&]{std::memcpy(installed_jump.data(),at(0x47bb50),8);std::memcpy(cache_jump.data(),at(0x481170),6);*static_cast<std::uint8_t*>(at(0x47bb50))^=1;};
  h.after_rollback=[&]{std::memcpy(at(0x47bb50),installed_jump.data(),8);std::memcpy(at(0x481170),cache_jump.data(),6);};
  check(tx.commit(&h)==patch_transaction::outcome::rollback_failed,"uncertified rollback retains entry JMP and executable storage");
  xml1_economy::after_commit(tx.result());check(!gameplay_agreement::good&&(!xml1_economy::installed()&&!lcsave::item_own_rebuilt_enabled()),"uncertified rollback never publishes rule52");
  // Production refuses DLL startup here. Even if a retained entry is probed,
  // its trampoline must remain valid. Null definition never touches any pool.
  descriptor out{1,at(1)};native<make_t>(0x47bb50)(nullptr,nullptr,&out,nullptr,1,0,0,0,0);
  check(!out.handle&&!out.pool,"retained trampoline safely preserves native null-definition ABI");
  std::array<void*,16> definition_vt{};definition_vt[0x24/4]=reinterpret_cast<void*>(&type);
  std::array<unsigned char,0x3c> definition{};put(definition.data(),0,definition_vt.data());
  std::array<unsigned char,0x34> legacy{};put(legacy.data(),4,definition.data());put(legacy.data(),0x28,1234);
  native<void(__fastcall*)(void*,void*)>(0x481170)(legacy.data(),nullptr);
  check(read<int>(legacy.data(),0x28)==1234,"retained price trampoline safely forwards native non-equipment cache early-out while economy inactive");
  return failures?1:0;
 }
 const auto committed=tx.commit();
 check(committed==patch_transaction::outcome::committed,"W2 plus constructor/table hooks commit in one union: "+tx.report());
 if(committed!=patch_transaction::outcome::committed)return 1;
 xml1_economy::after_commit(tx.result());check(gameplay_agreement::holds==1&&(!xml1_economy::installed()&&!lcsave::item_own_rebuilt_enabled())&&gameplay_agreement::registrations==0,"agreement held before table resolution");
 auto* picker_copy=item_capacity::prepared_definition_picker();
 check(picker_copy&&read<unsigned char>(picker_copy)==0xe9&&0x47b595+read<unsigned>(at(0x47b591))==addr(picker_copy),"picker tripwire composes at W2 copied entry; original width redirect retained");
 setup_table(legacy_definition+1); // high indices exercise widened AX lookup; one extra legacy definition
 auto* ip=native<void*(__cdecl*)()>(0x483610)();auto* ap=native<void*(__cdecl*)()>(0x45fb00)();
 const std::vector<std::uint8_t> initial_i(static_cast<std::uint8_t*>(ip),static_cast<std::uint8_t*>(ip)+item_capacity_rules::instances.size),initial_a(static_cast<std::uint8_t*>(ap),static_cast<std::uint8_t*>(ap)+item_capacity_rules::attached.size);
 auto restore=[&]{std::memcpy(ip,initial_i.data(),initial_i.size());std::memcpy(ap,initial_a.data(),initial_a.size());put(at(0x6e0874),0,12345u);put(at(0x7ad8b0),0,67890u);put(at(0x7ad8b4),0,0u);};
 std::vector<snapshot> native_results;bool native_good=true;
 for(unsigned i=0;i<economy_count;++i){restore();descriptor out{};native<make_t>(0x47bb50)(manager.data(),nullptr,&out,manager.data()+item_capacity_rules::definitions+(i+first_economy)*0x3c,1,0,0,0,0);auto s=capture(out);native_good&=s.draws==own_count(i)&&s.attached.size()==own_count(i)&&s.bonus==11*int(own_count(i));if(i<equipment_count)native_good&=read<unsigned>(s.instance.data(),0x28)>0&&(s.instance[0x30]&2);else native_good&=read<unsigned>(s.instance.data(),0x28)==0&&!(s.instance[0x30]&2);native_results.push_back(s);}
 check(native_good,"132 native constructors (116 equipment + 16 plain): 1..5/0 own records, all eight slots/negative slots, native n draws, bonuses/cache valid");
 check(read<int>(native_results[4].instance.data(),0x28)!=137,"native affecter-cost sum differs from the economy definition base");
 if(scenario=="unresolved")put(manager.data(),0x2588,static_cast<std::uint16_t>(legacy_definition-1));
 // Exercise the actual installed completion call (its native record walk is an
 // environment edge here; the production bridge invokes it before resolution).
 jump(0x47ce80,&empty_walk);
 const auto target=0x4806b2+read<unsigned>(at(0x4806ae));native<void(__fastcall*)(void*,void*)>(target)(manager.data(),nullptr);
 if(scenario=="unresolved"){
  check((!xml1_economy::installed()&&!lcsave::item_own_rebuilt_enabled())&&!gameplay_agreement::good&&gameplay_agreement::holds==0,"unresolved native name refuses rule and releases hold closed");return failures?1:0;
 }
 check(xml1_economy::installed()&&lcsave::item_own_rebuilt_enabled()&&gameplay_agreement::holds==0&&gameplay_agreement::registrations==1,"all 132 names resolved with real W2 by-name lookup, incl indices255/256; rule verified once");
 if(!xml1_economy::installed()){std::printf("resolution failure: %s\n",logger::last.c_str());return 1;}
 auto expected_values=agreement_values(chosen,parse(text));expected_values[0]|=95;
 check(gameplay_agreement::rules.rule(1).values==expected_values,"rule 52 exact parts/revision/digest/count values, all seven parts 1|2|4|8|16|64 plus the plan bit");
 gameplay_capabilities::record local,old_peer;local.add(52,1,expected_values,true);local.finalize();
 auto previous_values=expected_values;previous_values[0]&=~64u;old_peer.add(52,1,previous_values,true);old_peer.finalize();
 gameplay_capabilities::difference difference;char description[128]{};
 const auto mismatch=local.compare(old_peer.bytes(),&difference);local.describe(difference.buckets,0,true,description,sizeof description);
 check(mismatch!=gameplay_capabilities::result::match&&std::strstr(description,"52 Xml1Economy"),"peer without migration part64 is refused naming rule52");
 if(scenario=="shop"){restore();shop_checks();std::printf("Shop execution: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;}
 if(scenario=="bag"){restore();bag_checks();std::printf("T-BAG native: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;}
 if(scenario=="digest"){restore();digest_checks();std::printf("Digest execution: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;}
 if(scenario=="prices"){
  restore();price_checks();
  std::printf("Prices/transactions/lists: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;
 }
 // Step 4 intentionally replaces the native affecter-derived cache. All other
 // constructor fields retain step 3a's normalized structural/behavioral parity.
 for(unsigned i=0;i<economy_count;++i){put(native_results[i].instance.data(),0x28,i==1||i==2?0:137);native_results[i].instance[0x30]|=2;}
 bool parity=true,draws=true,other_stream_unchanged=true;
 for(unsigned i=0;i<economy_count;++i){restore();descriptor out{};auto* def=manager.data()+item_capacity_rules::definitions+(i+first_economy)*0x3c;native<make_t>(0x47bb50)(manager.data(),nullptr,&out,def,1,0,0,0,0);const auto s=capture(out);const bool equal=same(s,native_results[i]);parity&=equal;draws&=s.draws==(chosen==plan::zero_draw?0u:own_count(i))&&s.rng_state==(chosen==plan::zero_draw?12345u:native_results[i].rng_state);
  if(!equal){std::printf("parity definition %u: bonus %d/%d, records %zu/%zu\n",i,s.bonus,native_results[i].bonus,s.attached.size(),native_results[i].attached.size());for(unsigned n=0;n<0x34;++n)if(s.instance[n]!=native_results[i].instance[n])std::printf(" instance +%02x %02x/%02x\n",n,s.instance[n],native_results[i].instance[n]);}
  other_stream_unchanged&=read<unsigned>(at(0x7ad8b0))==67890;
 }
 check(parity,"T-LOOT-CTOR 132 definitions: structural/bonus parity except +8; cache/validity equal the step-4 definition-cost contract");
 check(draws,chosen==plan::zero_draw?"plan A consumes zero RNG draws":"fallback consumes exactly n native RNG draws");
 check(other_stream_unchanged,"unsynchronized RNG stream unchanged for every construction");
 restore();descriptor legacy{};native<make_t>(0x47bb50)(manager.data(),nullptr,&legacy,manager.data()+item_capacity_rules::definitions+legacy_definition*0x3c,1,0,0,0,0);check(capture(legacy).draws==own_count(economy_count),"legacy definition uses native body");
 restore();descriptor other{};native<make_t>(0x47bb50)(manager.data(),nullptr,&other,manager.data()+item_capacity_rules::definitions+first_economy*0x3c,2,0,0,0,0);check(capture(other).draws==1,"nonplain argument form uses native body");
 restore();descriptor null_def{1,at(1)};native<make_t>(0x47bb50)(manager.data(),nullptr,&null_def,nullptr,1,0,0,0,0);check(!null_def.handle&&!null_def.pool&&read<unsigned>(at(0x7ad8b4))==0&&std::memcmp(ip,initial_i.data(),initial_i.size())==0,"null definition retains native null-descriptor path without allocation");
 // Preflight must not call vt+e4 at all; replace only this manager's slot with
 // a spy, then compare the complete pool bytes after each refusal.
 std::array<void*,80> manager_vt;std::memcpy(manager_vt.data(),at(0x688314),sizeof manager_vt);put(manager.data(),0,addr(manager_vt.data()));
 manager_vt[0xe4/4]=reinterpret_cast<void*>(&allocation_spy);
 // Count-based boundary tests intentionally set only occupancy; no allocator
 // can run because every demand in these cases is insufficient.
 for(const auto& counts:std::array<std::array<unsigned,2>,3>{{{1024,0},{1023,4096},{1023,4092}}}){
  restore();put(ip,item_capacity_rules::instance_count_offset(true),counts[0]);put(ap,item_capacity_rules::attached_count_offset(true),counts[1]);
  const std::vector<std::uint8_t> before_i(static_cast<std::uint8_t*>(ip),static_cast<std::uint8_t*>(ip)+initial_i.size()),before_a(static_cast<std::uint8_t*>(ap),static_cast<std::uint8_t*>(ap)+initial_a.size());
  descriptor out{123,at(123)};native<make_t>(0x47bb50)(manager.data(),nullptr,&out,manager.data()+item_capacity_rules::definitions+(first_economy+4)*0x3c,1,0,0,0,0);
  check(!out.handle&&!out.pool&&allocation_calls==0&&std::memcmp(ip,before_i.data(),before_i.size())==0&&std::memcmp(ap,before_a.data(),before_a.size())==0&&read<unsigned>(at(0x7ad8b4))==0,"capacity preflight null descriptor, complete pools unchanged, 0x47c990 never entered, no RNG");
 }
 const auto expected_status="econ on; plan "+std::string(chosen==plan::zero_draw?"A":"1roll")+"; defs 132; digest "+digest_hex(parse(text).digest).substr(0,8)+"; forge 0/G0; gen 0; refused 3";
 check(logger::refusal_logs==1&&xml1_economy::status()==expected_status,"refusal counted per attempt, logged once per definition; exact status");
 restore();
 for(unsigned i=0;i<1023;++i){descriptor used{};native<descriptor*(__fastcall*)(void*,void*,descriptor*)>(0x47c990)(manager.data(),nullptr,&used);}
 for(unsigned i=0;i<4091;++i){descriptor used{};native<descriptor*(__fastcall*)(void*,void*,descriptor*)>(0x47c8f0)(manager.data(),nullptr,&used);}
 descriptor last{};native<make_t>(0x47bb50)(manager.data(),nullptr,&last,manager.data()+item_capacity_rules::definitions+(first_economy+4)*0x3c,1,0,0,0,0);
 check(last.pool&&allocation_calls==1&&(last.handle&1023)==1023&&read<unsigned>(ip,item_capacity_rules::instance_count_offset(true))==1024&&read<unsigned>(ap,item_capacity_rules::attached_count_offset(true))==4096&&capture(last).attached.size()==5,"real W2 capacity-1: final instance and final five attached slots succeed, one allocator entry");
 const std::vector<std::uint8_t> full_i(static_cast<std::uint8_t*>(ip),static_cast<std::uint8_t*>(ip)+initial_i.size()),full_a(static_cast<std::uint8_t*>(ap),static_cast<std::uint8_t*>(ap)+initial_a.size());
 descriptor full{1,at(1)};native<make_t>(0x47bb50)(manager.data(),nullptr,&full,manager.data()+item_capacity_rules::definitions+(first_economy+4)*0x3c,1,0,0,0,0);
 check(!full.handle&&!full.pool&&allocation_calls==1&&std::memcmp(ip,full_i.data(),full_i.size())==0&&std::memcmp(ap,full_a.data(),full_a.size())==0,"real W2 capacity: refusal changes no pool byte and never re-enters 0x47c990");
 std::memcpy(ip,initial_i.data(),initial_i.size());put(at(0x7ad8b4),0,0u);
 descriptor plain_item{};native<make_t>(0x47bb50)(manager.data(),nullptr,&plain_item,manager.data()+item_capacity_rules::definitions+(first_economy+equipment_count)*0x3c,1,0,0,0,0);
 check(plain_item.pool&&allocation_calls==2&&capture(plain_item).draws==0&&read<unsigned>(ap,item_capacity_rules::attached_count_offset(true))==4096,"zero-own plain economy definition needs no attached space, even when attached pool is full");
 check(xml1_economy::record_for(first_economy+4)&&xml1_economy::is_economy(manager.data()+item_capacity_rules::definitions+(first_economy+4)*0x3c)&&!xml1_economy::record_for(legacy_definition),"resolved metadata and economy membership exposed");
 std::printf("%s\nEconomy execution: %d checks, %d failure(s)\n",xml1_economy::status().c_str(),checks,failures);return failures?1:0;
}

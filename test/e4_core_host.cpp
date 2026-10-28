// Private retail-fragment fixture. Never enters the game or loads an engine.
// Native actor/controller rings, token resolvers, acceptance, leave and timeout
// run from the owned image. Payload initialization and engine services are fakes.
#include "e4_core.hpp"
#include "e4_core_state.hpp"
#include "e4_core_rules.hpp"
#include "e4_observe_rules.hpp"
#include "e4_core_sites.hpp"
#include "e4_core_plan.hpp"
#include "e4_observe.hpp"
#include "actor_ai_layout.hpp"
#include "actor_pools.hpp"
#include "entity_capacity_layout.hpp"
#include "entity_capacity_rules.hpp"
#include "entity_capacity_sites.hpp"
#include "gameplay_capabilities.hpp"
#include "patch_transaction.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <map>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" unsigned char fixture_space[];
using u32=std::uint32_t;using bytes=std::vector<unsigned char>;
namespace {
unsigned checks=0,failures=0;std::string mode;bool refused=false,save_enabled=false;
std::map<std::pair<std::wstring,std::wstring>,std::string> settings;
gameplay_capabilities::record agreement;
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
void* ptr(u32 p){return reinterpret_cast<void*>(p);}
u32& word(u32 p,unsigned off=0){return *reinterpret_cast<u32*>(p+off);}
float& real(u32 p,unsigned off){return *reinterpret_cast<float*>(p+off);}
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
LONG CALLBACK crash(EXCEPTION_POINTERS* p){if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){std::printf("FAULT EIP=%08lx ECX=%08lx EAX=%08lx EDX=%08lx ESI=%08lx EDI=%08lx ESP=%08lx target=%08lx\n",p->ContextRecord->Eip,p->ContextRecord->Ecx,p->ContextRecord->Eax,p->ContextRecord->Edx,p->ContextRecord->Esi,p->ContextRecord->Edi,p->ContextRecord->Esp,static_cast<unsigned long>(p->ExceptionRecord->ExceptionInformation[1]));for(unsigned i=0;i<10;++i)std::printf(" %08x",word(p->ContextRecord->Esp+i*4));std::puts("");}return EXCEPTION_CONTINUE_SEARCH;}
bytes read(const char* path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
bool load(const bytes& raw){
 if(raw.size()<0x400)return false;const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
 const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);DWORD prior=0;
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.ImageBase!=0x400000||!VirtualProtect(fixture_space,0x700000,PAGE_EXECUTE_READWRITE,&prior))return false;
 std::memset(fixture_space,0,0x700000);const auto* section=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=section[i];if(s.VirtualAddress+s.SizeOfRawData>0x701000||s.PointerToRawData+s.SizeOfRawData>raw.size())return false;std::memcpy(ptr(0x400000+s.VirtualAddress),raw.data()+s.PointerToRawData,s.SizeOfRawData);}
 return true;
}
bytes snapshot(){return {fixture_space,fixture_space+0x700000};}
void jump(u32 va,const void* target){auto b=patch_transaction::relocation(va,"e9",true,address(target),0,"fixture service").bytes;std::memcpy(ptr(va),b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),ptr(va),b.size());}
}
namespace ini {
std::optional<std::string> text(const wchar_t* section,const wchar_t* key){const auto it=settings.find({section,key});return it==settings.end()?std::nullopt:std::optional(it->second);}
bool flag(const wchar_t* section,const wchar_t* key,bool fallback){const auto t=text(section,key);return t?*t=="1":fallback;}
}
namespace logger {void write(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::puts("");va_end(args);}}
namespace game_version {void refuse_startup(){refused=true;}bool require_gameplay_agreement(HMODULE){return true;}}
namespace gameplay_agreement {bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<u32,4> values,bool verified){return agreement.add(id,revision,values,verified);}}
namespace entity_capacity {
const entity_capacity_layout::descriptor& installed(){static auto d=entity_capacity_layout::raised_descriptor_for(0,0,0,0);d.actors=e4_core_state::installed?96:32;d.layout_revision=e4_core_state::installed?4:3;return d;}
[[noreturn]] void fail_stop_lifecycle(const char*){ExitProcess(77);}
}
std::string save_refusal;bool save_refused=false;
namespace save_format {void enable_actor_capacity96(){save_enabled=true;}void actor_capacity96_refused(const std::string& why){save_refused=true;save_refusal=why;}}
namespace {
using method=void(__fastcall*)(void*,void*);
u32 allocator=0,cai=0;float clock_value=0;bool throw_brain=false,nested=false,resolve_in_dtor=false,bad_handler=false;void* nested_result=nullptr;int injected_phase=-1;
u32 expected_resolve_caller=0;std::array<u32,2> wrong_handler{};
unsigned controller_dtor_hits=0,team_dtor_hits=0;
std::array<std::array<u32,96>,4> reset_tables{};unsigned reset_hits=0,reset_last=~0u;bool check_reset=false;
void reset_probe(u32 slot){++reset_hits;reset_last=slot;check(slot<96,"W11 reset slot within96");if(slot<96)for(auto& table:reset_tables)table[slot]=0;}
__declspec(naked) void* __stdcall observed_resolve(u32,u32){__asm {
 mov ecx,[esp+4]
 push dword ptr [esp+8]
 mov eax,offset returned
 mov expected_resolve_caller,eax
 mov eax,525160h
 call eax
returned:
 ret 8
}}
void* spawn();
std::array<u32,2> factory_vt{};u32 factory_object=0;
std::array<u32,0x61c/4> controller_vt{};std::array<u32,0x164/4> game_vt{};u32 game_object=0;
void __fastcall generic_clear(void* p,void*){entity_capacity_rules::generic_ring_rebuild(static_cast<unsigned char*>(p),768);}
int __cdecl at_exit(void*){return 0;}
float __fastcall game_time(void*,void*){return clock_value;}
void* __cdecl game_get(){return &game_object;}
void* __fastcall vector_init(void* p,void*){std::memset(p,0,12);return p;}
float __fastcall distance2(const float* a,void*,const float* b){float sum=0;for(unsigned i=0;i<3;++i){const auto d=a[i]-b[i];sum+=d*d;}return sum;}
unsigned __fastcall faction(void* actor,void*){return word(address(actor),0x800);}
u32 __fastcall factory_size(void*,void*){return 0x844;}
void* __cdecl factory_allocate(void*,unsigned size){auto* p=reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4615a0)(ptr(allocator),nullptr,size);if(p&&injected_phase==0)throw std::runtime_error("admitted phase fault");return p;}
void* __fastcall delete_controller(void* p,void*,unsigned){
 ++controller_dtor_hits;
 const auto c=address(p),team=word(c,0x538);if(resolve_in_dtor){resolve_in_dtor=false;check(observed_resolve(cai,word(word(c,0x10),0x7dc))==p,"T19 native resolver still hits during destructor");}if(team)reinterpret_cast<void(__fastcall*)(void*,void*,u32,u32)>(0x522b30)(ptr(cai),nullptr,team,word(word(c,0x10),0x7dc));return p;
}
void* __fastcall delete_team(void* p,void*,unsigned){++team_dtor_hits;return p;}
u32 __fastcall controller_body(void* p,void*,void* actor,void*){
 auto* record=reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x522e90)(static_cast<unsigned char*>(p)+4,nullptr);
 const auto c=address(record),i=(c-address(p)-4)/0x61c;
 if(check_reset)for(auto& table:reset_tables){check(table[i]==0,"W11 all four slot tables reset before each brain constructor");table[i]=0xdead0000u+i;}
 std::memset(record,0,0x61c);word(c)=address(controller_vt.data());word(c,0x10)=address(actor);
 if(bad_handler){bad_handler=false;wrong_handler[1]=c;word(c,0x4c4)=address(wrong_handler.data());}
 if(throw_brain)throw std::runtime_error("test brain constructor throws after native pop/constructed bit");
 const auto generation=actor_ai::generations(actor_ai::installed());return word(address(p)+4,generation+i*4);
}
__declspec(naked) void __stdcall publish_fragment(void*,u32){__asm {
 push esi
 mov esi,[esp+8]
 mov eax,[esp+0ch]
 mov ecx,42394dh
 call ecx
 pop esi
 ret 8
}}
void* __fastcall actor_ctor(void* actor,void*,u32,u32,u32,u32,u32){
 if(injected_phase==1)throw std::runtime_error("actor-init phase fault");
 if(nested){nested=false;nested_result=spawn();}
 const auto token=reinterpret_cast<u32(__fastcall*)(void*,void*,void*,void*)>(0x523060)(ptr(cai),nullptr,actor,nullptr);
 publish_fragment(actor,token);return actor;
}
void __fastcall actor_post(void*,void*){if(injected_phase==3)throw std::runtime_error("init-returned phase fault");}
__declspec(naked) void frame_tail(){__asm {
 add esp,8
 ret
}}
void dependencies(){
 jump(0x401d50,reinterpret_cast<void*>(&at_exit));jump(0x67211e,reinterpret_cast<void*>(&at_exit));jump(0x46dce0,reinterpret_cast<void*>(&game_get));jump(0x41a220,reinterpret_cast<void*>(&faction));
 word(0x67fa60)=address(reinterpret_cast<void*>(&vector_init));word(0x67fa24)=address(reinterpret_cast<void*>(&distance2));
 jump(0x45fed0,reinterpret_cast<void*>(&factory_allocate));jump(0x423330,reinterpret_cast<void*>(&actor_ctor));jump(0x423953,reinterpret_cast<void*>(&at_exit));*reinterpret_cast<unsigned char*>(0x423953)=0xc3;
 jump(0x523066,reinterpret_cast<void*>(&controller_body));jump(0x4603b0,reinterpret_cast<void*>(&actor_post));jump(0x401d77,reinterpret_cast<void*>(&frame_tail));
 factory_vt[1]=address(reinterpret_cast<void*>(&factory_size));factory_object=address(factory_vt.data());
 controller_vt[0]=controller_vt[2]=address(reinterpret_cast<void*>(&delete_controller));game_vt[0x160/4]=address(reinterpret_cast<void*>(&game_time));game_object=address(game_vt.data());
 word(0x695cac)=address(reinterpret_cast<void*>(&delete_team));
}
void phase_a(patch_transaction::transaction& tx){
 patch_transaction::contribution c;c.owner="entity_capacity";
 for(const auto& s:entity_capacity_sites::literal_sites)if(s.va>=0x461400&&s.va<0x461900){
  auto after=patch_transaction::bytes_of(s.new_hex);if(s.va==0x461588||s.va==0x4615db||s.va==0x46176d){u32 live=0x1aa51c;std::memcpy(after.data()+2,&live,4);}
  c.guards.push_back({s.va,patch_transaction::bytes_of(s.old_hex),"Phase A allocator fixture"});c.writes.push_back({s.va,after,"Phase A allocator fixture"});
 }
 for(const auto& s:entity_capacity_sites::relocation_sites)if(s.va==0x46155c||s.va==0x4618d6){c.guards.push_back({s.va,patch_transaction::bytes_of(s.old_hex),"Phase A clear fixture"});c.writes.push_back(patch_transaction::relocation(s.va,s.prefix_hex,true,address(reinterpret_cast<void*>(&generic_clear)),s.pad,"Phase A clear fixture"));}
 tx.add(std::move(c));
}
void* spawn(){return reinterpret_cast<void*(__fastcall*)(void*,void*,u32,u32,u32,u32)>(0x423a90)(&factory_object,nullptr,0,0,0,0);}
void destroy(void* actor){const auto token=word(address(actor),0x7dc);if(token)reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x523960)(ptr(cai),nullptr,token);reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x461700)(ptr(allocator),nullptr,actor);}
void* resolve(u32 token){return reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x525160)(ptr(cai),nullptr,token);}
u32 join(void* actor){const auto token=word(address(actor),0x7dc);const auto team=reinterpret_cast<u32(__fastcall*)(void*,void*,u32)>(0x523320)(ptr(cai),nullptr,token);word(address(resolve(token)),0x538)=team;return team;}
void boundary(){reinterpret_cast<method>(0x401d70)(nullptr,nullptr);}
__declspec(naked) void __stdcall reregister_fragment(void*){__asm {
 push esi
 push ebx
 mov esi,[esp+0ch]
 xor ebx,ebx
 mov eax,512847h
 call eax
 pop ebx
 pop esi
 ret 4
}}
std::vector<unsigned> accept_calls;void* accept_body=nullptr;
unsigned char __fastcall accept_observer(void* team,void*,u32 token){
 accept_calls.push_back((address(team)-actor_ai::installed().team_base)/0x268);
 return reinterpret_cast<unsigned char(__fastcall*)(void*,void*,u32)>(accept_body)(team,nullptr,token);
}
void trace_dependencies(){
 accept_body=VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
 std::memcpy(accept_body,ptr(0x525df0),5);auto tail=patch_transaction::relocation(address(accept_body)+5,"e9",true,0x525df5,0,"fixture accept continuation").bytes;std::memcpy(static_cast<unsigned char*>(accept_body)+5,tail.data(),tail.size());
 jump(0x525df0,reinterpret_cast<void*>(&accept_observer));*reinterpret_cast<unsigned char*>(0x51286e)=0xc3;
}
u32 brain_entry=0,brain_controller=0,brain_sp=0;std::array<u32,9> brain_registers{};
__declspec(align(16)) std::array<unsigned char,512> brain_fx_before{},brain_fx_after{};
__declspec(align(16)) std::array<u32,4> brain_pattern{0x12345678,0x87654321,0x11223344,0x55667788};
unsigned brain_lookups=0,brain_activations=0,brain_cleanups=0,brain_actions=0;bool brain_free_team=false,brain_free_controller=false;
std::array<u32,0xa0/4> brain_target_vt{};u32 brain_target=0,brain_token=0;void* brain_actor=nullptr;void* brain_lookup_body=nullptr;
void* __fastcall brain_lookup(void* ai,void*,u32 token){++brain_lookups;return reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(brain_lookup_body)(ai,nullptr,token);}
void __fastcall brain_activation(void*,void*,void*,u32){++brain_activations;
 if(brain_free_team){brain_free_team=false;reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x5251c0)(ptr(cai),nullptr,word(brain_controller,0x538));}
 if(brain_free_controller){brain_free_controller=false;reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x523960)(ptr(cai),nullptr,brain_token);}
}
void __fastcall brain_cleanup(void*,void*){++brain_cleanups;}
void __fastcall brain_action(void*,void*,void*){++brain_actions;}
__declspec(naked) void brain_exit(){__asm {
 fxsave brain_fx_after
 pushfd
 pushad
 cld
 mov esi,esp
 mov edi,offset brain_registers
 mov ecx,9
 rep movsd
 mov esp,brain_sp
 popad
 popfd
 ret
}}
__declspec(naked) void brain_enter(){__asm {
 pushfd
 pushad
 mov brain_sp,esp
 sub esp,200h
 fninit
 movaps xmm0,brain_pattern
 movaps xmm1,xmm0
 movaps xmm2,xmm0
 movaps xmm3,xmm0
 movaps xmm4,xmm0
 movaps xmm5,xmm0
 movaps xmm6,xmm0
 movaps xmm7,xmm0
 fxsave brain_fx_before
 mov esi,brain_controller
 mov edi,14141414h
 mov ebx,offset brain_target
 mov ebp,31313131h
 xor eax,eax
 mov ecx,12121212h
 mov edx,23232323h
 jmp dword ptr [brain_entry]
}}
void brain_tests(const char* path,bool stock){
 brain_actor=spawn();brain_token=word(address(brain_actor),0x7dc);brain_controller=address(resolve(brain_token));word(address(brain_actor),0x800)=30;const auto team=join(brain_actor);const auto team_address=address(reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x525100)(ptr(cai),nullptr,team));
 brain_lookup_body=VirtualAlloc(nullptr,64,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);std::memcpy(brain_lookup_body,ptr(0x525100),5);const auto tail=patch_transaction::relocation(address(brain_lookup_body)+5,"e9",true,0x525105,0,"fixture lookup continuation").bytes;std::memcpy(static_cast<unsigned char*>(brain_lookup_body)+5,tail.data(),tail.size());jump(0x525100,reinterpret_cast<void*>(&brain_lookup));
 brain_target_vt[0x9c/4]=address(reinterpret_cast<void*>(&brain_action));brain_target=address(brain_target_vt.data());jump(0x41b5b0,reinterpret_cast<void*>(&brain_activation));jump(0x525460,reinterpret_cast<void*>(&brain_cleanup));jump(0x50e4af,reinterpret_cast<void*>(&brain_exit));
 const auto run=[&](unsigned branch,unsigned nulls){word(brain_controller,0x538)=nulls==1?0:team;word(brain_controller,0x344)=0x55;word(brain_controller,0x340)=0;word(address(brain_actor),0x1c)=0x123;word(team_address,0x260)=word(team_address,0x264)=0;
  brain_entry=branch==0?0x50e1ac:branch==1?0x50e3ac:0x50e492;brain_lookups=brain_activations=brain_cleanups=brain_actions=0;brain_free_team=nulls==2;brain_fx_before.fill(0);brain_fx_after.fill(0);brain_enter();
  check(brain_fx_before==brain_fx_after,"T8/T9 core brain path preserves complete x87/MMX/XMM/MXCSR state");
  const auto norm=[&](u32 v){if(v>=brain_controller&&v<brain_controller+0x61c)return 0xc0000000u+v-brain_controller;if(v>=team_address&&v<team_address+0x268)return 0xd0000000u+v-team_address;if(v>=address(brain_actor)&&v<address(brain_actor)+0x844)return 0xa0000000u+v-address(brain_actor);if(v==cai)return 0xe0000000u;if(v==brain_token)return 0xf1000000u;if(v==team)return 0xf2000000u;return v;};
  std::vector<u32> result;for(unsigned i=0;i<8;++i)if(i!=3)result.push_back(norm(brain_registers[i]));result.push_back(brain_registers[8]&0x8d5);result.insert(result.end(),{word(team_address,0x260),word(team_address,0x264),word(brain_controller,0x340),brain_lookups,brain_activations,brain_cleanups,brain_actions});return result;
 };
 std::ofstream out(path,std::ios::binary);for(unsigned branch=0;branch<3;++branch){const auto result=run(branch,0);for(auto value:result)out<<value<<',';out<<'\n';}
 if(stock){destroy(brain_actor);return;}
 const auto first=run(0,1);check(first[11]==1&&first[12]==1&&first[10]==1&&first[8]==0,"T8 real null first team lookup skips second lookup and preserves activation/state");
 const auto second=run(0,2);check(second[11]==2&&second[12]==1&&second[10]==1&&second[8]==0,"T8 real callback team destruction makes second lookup null safely");
 word(brain_controller,0x538)=0;brain_free_controller=true;
 const auto cave=0x50e1c7+*reinterpret_cast<int*>(0x50e1c3);u32 returned=0;
 for(unsigned i=0;i<256;++i)if(*reinterpret_cast<unsigned char*>(cave+i)==0xe8&&cave+i+5+*reinterpret_cast<int*>(cave+i+1)==0x41b5b0)returned=cave+i+5;
 run(0,1);const auto observed=e4_observe::json();std::ofstream("brain-observation.json")<<observed;
 check(returned&&observed.find("["+std::to_string(returned)+"]")!=std::string::npos,"T19 authored null-team callback retains its actual cave return address");
 check(observed.find("\"counts\":[1,0,0,0,0,0,")!=std::string::npos,"T19 authored null-team callback detects native release while borrowed");
 word(address(brain_actor),0x7dc)=0;reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x461700)(ptr(allocator),nullptr,brain_actor);boundary();
}
unsigned team_index(u32 token){const auto t=address(reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x525100)(ptr(cai),nullptr,token));return t?(t-actor_ai::installed().team_base)/0x268+1:0;}
struct live_actor {void* actor;unsigned id;};
void team_trace(const char* path,std::vector<live_actor> live={},unsigned next_id=0){
 trace_dependencies();std::ofstream out(path,std::ios::binary);std::mt19937 random(0x32141521);
 const auto& d=actor_ai::installed();const auto arena=d.team_base,active=actor_ai::team_active(d),gens=actor_ai::team_generations(d);
 const auto emit=[&](unsigned step,unsigned operation){
  out<<step<<'|'<<operation<<'|';for(auto i:accept_calls)out<<i<<',';out<<'|';
  for(const auto& a:live){const auto c=address(resolve(word(address(a.actor),0x7dc)));out<<a.id<<':'<<team_index(word(c,0x538))<<',';}out<<'|';
  for(unsigned i=0;i<4;++i)if(word(arena,active)&(1u<<i)){out<<i<<'[';for(unsigned j=0;j<word(arena+i*0x268,0x28);++j){const auto token=word(arena+i*0x268,4+j*4);unsigned id=0xffffffff;
    for(const auto& a:live)if(word(address(a.actor),0x7dc)==token){id=a.id;break;}out<<id<<',';}out<<']';}out<<'\n';
 };
 for(const auto& a:live)reregister_fragment(resolve(word(address(a.actor),0x7dc)));accept_calls.clear();emit(0,0);
 for(unsigned step=1;step<=10000;++step){accept_calls.clear();unsigned op=random()%7;
  if(live.empty()||(op==0&&live.size()<32)){
   auto* p=spawn();check(p!=nullptr,"T14 native trace admission");if(!p)break;
   const auto id=next_id++;word(address(p),0x800)=std::array<unsigned,4>{29,30,31,7}[random()%4];real(address(p),0x20)=float(random()%4)*250.f+(random()%2?0.f:-.1f);
   live.push_back({p,id});join(p);op=0;
  }else if(op==1){const auto j=random()%live.size();destroy(live[j].actor);live.erase(live.begin()+j);}
  else if(op==2){const auto j=random()%live.size();join(live[j].actor);} // native reactivation's earlier-slot accept
  else if(op==3){clock_value+=6;for(unsigned i=0;i<4;++i)if((word(arena,active)&(1u<<i))&&word(arena+i*0x268,0x28)<=1){
    const auto expired=!reinterpret_cast<unsigned char(__fastcall*)(void*,void*)>(0x527640)(ptr(arena+i*0x268),nullptr);
    if(expired)reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x5251c0)(ptr(cai),nullptr,word(arena,gens+i*4));
   }}
  else if(op==4)reinterpret_cast<method>(0x5234c0)(ptr(arena),nullptr);
  else if(op==5){for(const auto& a:live)reregister_fragment(resolve(word(address(a.actor),0x7dc)));}
  else {const auto j=random()%live.size();real(address(live[j].actor),0x20)=random()%2?249.9f:250.f;}
  emit(step,op);if(step%32==0)boundary();
 }
 for(const auto& a:live)destroy(a.actor);boundary();check(bool(out),"T14/T15/T21 complete normalized native trace");
}
void save_fold_state(const char* path,const std::vector<void*>& actors){
 std::ofstream out(path,std::ios::binary);const auto put=[&](const void* p,unsigned n){out.write(static_cast<const char*>(p),n);};
 const auto& d=actor_ai::installed();const u32 shift=word(d.team_base,e4_core_state::team_shift),count=static_cast<u32>(actors.size());put(&shift,4);put(&count,4);
 for(auto* p:actors){const auto token=word(address(p),0x7dc);put(&token,4);put(p,0x844);put(resolve(token),0x61c);}
 put(ptr(d.team_base),4*0x268);
 for(const auto field:{e4_core_state::team_constructed,e4_core_state::team_active,e4_core_state::team_enqueue,e4_core_state::team_dequeue})put(ptr(d.team_base+field),4);
 put(ptr(d.team_base+e4_core_state::team_ring),16);put(ptr(d.team_base+e4_core_state::team_generations),16);
 check(bool(out),"T15 serialized folded native state and tier0 FIFO");
}
std::vector<live_actor> load_fold_state(const char* path){
 std::ifstream in(path,std::ios::binary);const auto get=[&](void* p,unsigned n){in.read(static_cast<char*>(p),n);};u32 shift=0,count=0;get(&shift,4);get(&count,4);check(count==32&&shift<16,"T15 reference snapshot shape");
 std::vector<live_actor> live;std::map<u32,u32> tokens;const auto& d=actor_ai::installed();
 for(unsigned i=0;i<count;++i){u32 old=0;get(&old,4);auto* p=spawn();const auto token=word(address(p),0x7dc);const auto controller=address(resolve(token));get(p,0x844);get(ptr(controller),0x61c);word(address(p),0x7dc)=token;word(controller,0x10)=address(p);tokens[old]=token;live.push_back({p,58+i});}
 get(ptr(d.team_base),4*0x268);for(const auto field:{0x9a0,0x9c4,0x9b8,0x9bc})get(ptr(d.team_base+field),4);
 get(ptr(d.team_base+0x9a4),16);get(ptr(d.team_base+0x9cc),16);const auto active=word(d.team_base,0x9c4)&15u;word(d.team_base,0x9c8)=std::popcount(active);word(d.team_base,0x9c0)=4-std::popcount(active);
 for(unsigned i=0;i<4;++i){word(d.team_base,0x9cc+i*4)=((word(d.team_base,0x9cc+i*4)>>shift)<<2)|i;
  if(active&(1u<<i))for(unsigned j=0;j<word(d.team_base+i*0x268,0x28);++j)word(d.team_base+i*0x268,4+j*4)=tokens.at(word(d.team_base+i*0x268,4+j*4));
 }
 for(const auto& a:live){const auto c=address(resolve(word(address(a.actor),0x7dc)));const auto old=word(c,0x538);word(c,0x538)=old?((old>>shift)<<2)|(old&((1u<<shift)-1)):0;}
 check(bool(in),"T15 stock-layout clone loaded byte payloads, converted opaque token domains");return live;
}
void save_tref_state(const std::string& path,const std::vector<void*>& actors){
 std::ofstream out(path,std::ios::binary);const auto put=[&](const void* p,unsigned n){out.write(static_cast<const char*>(p),n);};const auto& d=actor_ai::installed();
 const u32 mask=word(cai,actor_ai::team_mask(d)),count=static_cast<u32>(actors.size());put(&mask,4);put(&count,4);
 for(auto* p:actors){const auto c=address(resolve(word(address(p),0x7dc))),team=word(c,0x538);const u32 extended=team&&(team&mask)>=4;const auto owner=word(c,0x10);put(&extended,4);put(&owner,4);put(p,0x844);put(ptr(c),0x61c);}
 check(bool(out),"T22 complete pre-fold controller/actor clone captured");
}
void save_controller_bytes(const std::string& path,const std::vector<void*>& actors){std::ofstream out(path,std::ios::binary);for(auto* p:actors)out.write(static_cast<const char*>(resolve(word(address(p),0x7dc))),0x61c);check(bool(out),"T22 complete post-fold bytes captured");}
void timeout_reference(const char* input,const char* output){
 struct record {u32 extended=0,owner=0;std::array<unsigned char,0x844> actor{};std::array<unsigned char,0x61c> controller{};};
 std::ifstream in(input,std::ios::binary);u32 mask=0,count=0;in.read(reinterpret_cast<char*>(&mask),4);in.read(reinterpret_cast<char*>(&count),4);check(count==32,"T22 stock reference has32 controllers");
 std::vector<record> records(count);u32 low=0x7fffffffu,high=0;
 for(auto& r:records){in.read(reinterpret_cast<char*>(&r.extended),4);in.read(reinterpret_cast<char*>(&r.owner),4);in.read(reinterpret_cast<char*>(r.actor.data()),r.actor.size());in.read(reinterpret_cast<char*>(r.controller.data()),r.controller.size());low=std::min(low,r.owner);high=std::max(high,r.owner+0x844);}
 check(bool(in)&&low>=0x1000000&&high<0x70000000,"T22 owned synthetic clone addresses");if(failures)return;
 if(!(low>=allocator&&high<=allocator+0x1aa520)){const auto base=low&~0xffffu;check(VirtualAlloc(ptr(base),(high-base+0xffff)&~0xffffu,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==ptr(base),"T22 private owner clone at original addresses");if(failures)return;}
 std::ofstream out(output,std::ios::binary);clock_value=100;unsigned exercised=0,heroes=0;
 for(auto& r:records){auto* actor=spawn();const auto token=word(address(actor),0x7dc),c=address(resolve(token));std::memcpy(ptr(c),r.controller.data(),r.controller.size());std::memcpy(ptr(r.owner),r.actor.data(),r.actor.size());
  if(r.extended){std::array<unsigned char,0x268> team{};word(address(team.data()),4)=token;word(address(team.data()),0x28)=1;real(address(team.data()),0x254)=0;
   check(!reinterpret_cast<unsigned char(__fastcall*)(void*,void*)>(0x527640)(team.data(),nullptr),"T22 actual retail one-member timeout forced");++exercised;heroes+=word(r.owner,0x800)==29;
  }out.write(reinterpret_cast<const char*>(c),0x61c);
 }
 check(exercised>0&&heroes>0&&bool(out),"T22 retail timeout covers hero and nonhero extended members");
}
void reservation_tests(){
 std::vector<void*> live;for(unsigned i=0;i<95;++i)live.push_back(spawn());
 const auto leaked=word(address(live[0]),0x7dc);
 reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x461700)(ptr(allocator),nullptr,live[0]);live[0]=spawn();
 check(live[0]&&word(cai+4,e4_core_state::primary_live)==96,"T17 leaked controller retained while actor replaced");
 check(!spawn()&&word(allocator,0x31ca4)==95,"T17 controller exhaustion uses factory null below actor cap");
 reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x523960)(ptr(cai),nullptr,leaked);
 for(auto* p:live)destroy(p);boundary();
 live.clear();for(unsigned i=0;i<95;++i)live.push_back(spawn());
 nested_result=ptr(1);nested=true;auto* outer=spawn();check(outer&&nested_result==nullptr,"T17 nested actor refused on one reserved free controller");
 check(outer&&word(address(outer),0x7dc)&&e4_core::reserved()==0,"T17 outer reservation consumed with nonzero token");
 if(outer)destroy(outer);for(auto* p:live)destroy(p);boundary();
 auto* pending=reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4615a0)(ptr(allocator),nullptr,0x844);
 check(pending&&e4_core::reserved()==1,"T17 pre-controller reservation exists");
 reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x461700)(ptr(allocator),nullptr,pending);check(e4_core::reserved()==0,"T17 actor-slot free cancels pending reservation");
}
void churn_tests(){
 std::mt19937 random(0xe423);std::vector<void*> live;
 for(unsigned i=0;i<10000;++i){if(live.empty()||(live.size()<96&&random()%2)){auto* p=spawn();check(p!=nullptr,"T23 native churn admission");if(p)live.push_back(p);}
  else {const auto j=random()%live.size();destroy(live[j]);live.erase(live.begin()+j);}
  if(i%16==0)boundary();check(e4_core::reserved()==0,"T23 no reservation drift after native operation");
 }for(auto* p:live)destroy(p);boundary();
}
void* __cdecl damage_payload(void* p){std::memset(p,0,0x6c);return p;}
void* __fastcall damage_copy(void* p,void*,void* src){std::memcpy(p,src,0x6c);return p;}
void __fastcall damage_schedule(void*,void*,void*,u32){}
void damage_tests(){
 jump(0x498890,reinterpret_cast<void*>(&damage_payload));jump(0x453d10,reinterpret_cast<void*>(&damage_copy));jump(0x41a730,reinterpret_cast<void*>(&damage_schedule));
 const auto pool=word(0x499b42);const auto threshold=real(0x680030,0);
 const float delays[]{threshold,threshold+1,-1,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
 std::array<unsigned char,0x400> entity{};std::array<unsigned char,0x6c> descriptor{};
 for(float delay:delays)for(u32 flags:{0u,1u})for(u32 pending:{0u,1u})for(u32 live:{0u,31u,32u,33u,124u}){
  reinterpret_cast<method>(0x499ad0)(ptr(pool),nullptr);entity.fill(0);descriptor.fill(0);std::memcpy(descriptor.data()+4,&delay,4);word(address(descriptor.data()),0x14)=flags;word(address(entity.data()),0x2fc)=pending;word(pool,0x3670)=live;
  const auto expected=e4_observe_rules::classify(delay,threshold,flags,pending,live,124);
  const auto accepted=reinterpret_cast<unsigned char(__fastcall*)(void*,void*,void*,u32)>(0x499b10)(entity.data(),nullptr,descriptor.data(),0);
  check(bool(accepted)==(expected==e4_observe_rules::damage_reason::accepted),"T24 actual124-capacity damage return equals classification");
 }
 check(e4_observe::json().find(",32,40,40,8,24,120]")!=std::string::npos,"T24 exact actual-return/reason/would-at32 counters with core installed");
}
void alias_tests(){
 bad_handler=true;auto* actor=spawn();const auto token=word(address(actor),0x7dc),controller=address(resolve(token));word(controller,0x4c4)=0;
 e4_core_state::on_event_borrow(controller,false,0x50e1fb);resolve_in_dtor=true;destroy(actor);
 std::vector<void*> live;for(unsigned i=0;i<96;++i)live.push_back(spawn());
 e4_core_state::on_event_borrow(controller,true,0x50e1fb);check(!observed_resolve(cai,token),"T19 stale token refused after borrowed-slot reuse");
 for(auto* p:live)destroy(p);
 std::array<u32,2> stack{0x12345678,0};std::array<u32,9> registers{};registers[3]=address(stack.data())-4;e4_observe::test_emit(0x522ed0,false,registers.data());
 // Close only the synthetic diagnostic DESTROYING state before checking the
 // real quiescent pool. No native allocation/release is performed here.
 e4_observe::test_emit(0x522f57,false,registers.data());boundary();
 const auto observed=e4_observe::json();std::ofstream("aliases.json")<<observed;
 check(observed.find("\"counts\":[1,1,1,1,1,1,0,0,0,0,0,0,0,0]")!=std::string::npos,"T19 six injected faults detected exactly once on raised native paths");
 check(observed.find("[0,0,"+std::to_string(0x523977)+",")!=std::string::npos,"T19 release caller is native523977");
 check(observed.find("[2,0,"+std::to_string(expected_resolve_caller)+",")!=std::string::npos,"T19 resolve-during-destroy caller exact");
 check(observed.find("[5,0,"+std::to_string(expected_resolve_caller)+",")!=std::string::npos,"T19 stale resolver caller exact");
 check(observed.find("[5300731]")!=std::string::npos,"T19 retained callback return address");
}
void destructor_tests(){
 for(unsigned i=0;i<96;++i)check(spawn()!=nullptr,"T6 primary destructor fixture allocates96 constructed brains");
 controller_dtor_hits=0;reinterpret_cast<method>(0x5227b0)(ptr(cai+4),nullptr);
 check(controller_dtor_hits==96,"T6 actual primary arena destructor visits96 slots");
 for(unsigned w=0;w<3;++w)check(word(cai+4,e4_core_state::primary_constructed+w*4)==0,"T6 primary destructor clears all3 constructed words");
 const auto arena=cai+e4_core_state::team_offset;
 // Synthetic constructed payloads exercise every physical storage word,
 // including slots beyond P2's twelve simultaneous live teams.
 for(unsigned i=0;i<96;++i)word(arena+i*0x268)=0x695cac;
 for(unsigned w=0;w<3;++w)word(arena,e4_core_state::team_constructed+w*4)=0xffffffff;
 team_dtor_hits=0;reinterpret_cast<method>(0x5228b0)(ptr(arena),nullptr);
 check(team_dtor_hits==96,"T6 actual team arena destructor visits96 constructed slots");
 for(unsigned w=0;w<3;++w)check(word(arena,e4_core_state::team_constructed+w*4)==0,"T6 team destructor clears all3 constructed words");
 // Destructed native pools are never resumed; this fixture exits immediately.
}
void actor_tests(){
 std::vector<void*> actors;
 for(unsigned i=0;i<96;++i){auto* p=reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4615a0)(ptr(allocator),nullptr,0x844);check(p!=nullptr,"T12 actor band allocation");actors.push_back(p);if(i==30||i==31||i==32||i==94||i==95)check(word(allocator,0x31ca4)==i+1,"T12 31/32/33/95/96 live count");}
 for(unsigned w=0;w<3;++w)check(word(allocator,0x31c98+w*4)==0xffffffff,"T12 all three actor bitmap words used");
 std::printf("T12 filled native live=%u reserved=%u\n",word(allocator,0x31ca4),e4_core::reserved());check(reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4615a0)(ptr(allocator),nullptr,0x844)==nullptr,"T12 97th actor refused");
 std::mt19937 random(0x964844);std::shuffle(actors.begin(),actors.end(),random);std::printf("freeing first=%08x tag=%u\n",address(actors[0]),word(address(actors[0])-4));for(auto* p:actors)reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x461700)(ptr(allocator),nullptr,p);
 for(unsigned w=0;w<3;++w)check(word(allocator,0x31c98+w*4)==0,"T12 all three actor bitmap words reset");
 std::printf("T12 freed native live=%u reserved=%u\n",word(allocator,0x31ca4),e4_core::reserved());check(word(allocator,0x31ca4)==0&&e4_core::reserved()==0,"T12 address free and pending cancellation");
 std::vector<void*> generic;for(unsigned i=0;i<768;++i){auto* p=reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4615a0)(ptr(allocator),nullptr,0x7d0);check(p!=nullptr,"T12 rebased generic allocation");generic.push_back(p);}
 check(reinterpret_cast<void*(__fastcall*)(void*,void*,u32)>(0x4615a0)(ptr(allocator),nullptr,0x7d0)==nullptr,"T12 generic769 refused");for(auto* p:generic)reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x461700)(ptr(allocator),nullptr,p);
 for(auto& table:reset_tables)table.fill(0xdeadbeef);reset_hits=0;check_reset=true;actor_ai::reset_controller_slot=&reset_probe;
 std::puts("T12 generic768/free complete; T13 factories");for(unsigned i=0;i<96;++i){auto* p=spawn();check(p!=nullptr,"T13 factory actor/controller96");actors[i]=p;const auto token=word(address(p),0x7dc);check(token&&resolve(token)!=nullptr,"T13 native generation resolver");}
 check(reset_hits==96&&reset_last==95,"W11 callback executes for every raised native controller slot including95");
 check(!spawn(),"T13 factory97 refuses rather than token0");check(word(cai+4,0x24dac)==127&&word(cai+4,0x24db0)==7,"T13 mask127 shift7");
 for(unsigned w=0;w<3;++w)check(word(cai+4,0x24a80+w*4)==0xffffffff&&word(cai+4,0x24c1c+w*4)==0xffffffff,"T13 three constructed/active controller words full");
 std::printf("T13 freeing first token=%08x ptr=%08x vt=%08x expected=%08x dtor=%08x\n",word(address(actors[0]),0x7dc),address(resolve(word(address(actors[0]),0x7dc))),word(address(resolve(word(address(actors[0]),0x7dc)))),address(controller_vt.data()),controller_vt[0]);for(auto* p:actors){const auto token=word(address(p),0x7dc);destroy(p);check(!resolve(token),"T13 released token rejected");}
 for(unsigned w=0;w<3;++w)check(word(cai+4,0x24a80+w*4)==0&&word(cai+4,0x24c1c+w*4)==0,"T13 three constructed/active controller words released");
 boundary();check(e4_core::reserved()==0,"T23 quiescent reservation accounting");
}
void plan_mutations(){
 const e4_core_plan::targets targets{1,1,1,1,1,1,1,1};std::vector<e4_core_sites::edit> rows(e4_core_sites::edits.begin(),e4_core_sites::edits.end());
 rows.pop_back();check(!e4_core_plan::make(0x20000000,0x21000000,targets,rows),"T3 partial core operand ledger refused");
 rows.assign(e4_core_sites::edits.begin(),e4_core_sites::edits.end());rows[0].value^=1;check(!e4_core_plan::make(0x20000000,0x21000000,targets,rows),"T3 altered core replacement value refused");
 rows.assign(e4_core_sites::edits.begin(),e4_core_sites::edits.end());rows[0].hex="00";check(!e4_core_plan::make(0x20000000,0x21000000,targets,rows),"T3 altered core guard refused");
}
bool clean_events(const std::string& observed){
 const auto start=observed.find("\"counts\":[");if(start==std::string::npos)return false;
 std::istringstream in(observed.substr(start+10));for(unsigned i=0;i<14;++i){unsigned n=0;char separator=0;if(!(in>>n))return false;if(i!=5&&n)return false;in>>separator;}return true;
}
std::vector<void*> stale_fold_tests(bool destroy_reactivated,const std::string& state){
 // Retail activation re-registers without leaving the old team (0x51684e);
 // destruction leaves only +0x538 (0x511ce4). Keep that old token listed.
 std::vector<void*> actors;for(unsigned i=0;i<34;++i){auto* p=spawn();check(p!=nullptr,"T15 stale sequence spawns34 controllers");word(address(p),0x800)=30;real(address(p),0x20)=float(i/8)*300;actors.push_back(p);}
 for(auto* p:actors)join(p);
 const auto arena=actor_ai::installed().team_base,mask=word(arena,e4_core_state::team_mask);
 const auto moved=word(address(actors[32]),0x7dc),old_team=word(address(resolve(moved)),0x538);
 check((old_team&mask)==4&&word(arena+4*0x268,0x28)==2,"T15 extended team initially holds two members");
 destroy(actors[0]);real(address(actors[32]),0x20)=0;
 const auto current=join(actors[32]);
 check(current&&(current&mask)==0&&word(arena+4*0x268,0x28)==2,"T15 reactivation overwrites current team and retains old membership");
 destroy(actors[destroy_reactivated?32:1]);
 check(word(cai+4,e4_core_state::primary_live)==32,"T15 reactivate then destroy shrinks34 to32");
 check((resolve(moved)==nullptr)==destroy_reactivated,"T15 stale membership covers dead and living resolvers");
 word(address(actors[33]),0x800)=29;
 std::vector<void*> survivors;for(unsigned i=1;i<34;++i)if(i!=(destroy_reactivated?32u:1u))survivors.push_back(actors[i]);
 save_tref_state(state+".pre",survivors);
 std::vector<bytes> before;for(auto* p:survivors){const auto c=address(resolve(word(address(p),0x7dc)));before.emplace_back(reinterpret_cast<unsigned char*>(c),reinterpret_cast<unsigned char*>(c)+0x61c);}
 boundary();
 check(word(arena,e4_core_state::team_live)==4,"T15 stale extended team retired without fail-stop");
 for(unsigned i=4;i<96;++i)check(!(word(arena,e4_core_state::team_active+(i/32)*4)&(1u<<(i%32))),"T15 no stale extended team survives");
 for(unsigned i=0;i<survivors.size();++i){const auto c=address(resolve(word(address(survivors[i]),0x7dc)));u32 team=0;std::memcpy(&team,before[i].data()+0x538,4);if(team&&(team&mask)>=4)std::memset(before[i].data()+0x538,0,4);check(std::memcmp(before[i].data(),ptr(c),0x61c)==0,"T22 stale fold preserves all controller bytes except current extended membership");}
 if(!destroy_reactivated)check(word(address(resolve(moved)),0x538)==current,"T22 old membership retirement preserves reactivated tier0 team");
 check(clean_events(e4_observe::json()),"T19/T23 stale retirement raises no illegal lifetime/accounting/team events");
 save_controller_bytes(state+".post",survivors);return survivors;
}
std::vector<void*> team_tests(bool keep=false,const std::string& tref={}){
 std::vector<void*> actors;
 for(unsigned i=0;i<90;++i){auto* p=spawn();check(p!=nullptr,"T15 expanded actor");word(address(p),0x800)=30;real(address(p),0x20)=float(i%12)*300;actors.push_back(p);
  for(auto* q:actors)if(!word(address(resolve(word(address(q),0x7dc))),0x538))join(q);
  const auto live=word(cai+e4_core_state::team_offset,e4_core_state::team_live);check(live<=e4_core_rules::team_cap(i+1),"T16 P2 4/8/12 bands");
 }
 const auto arena=cai+e4_core_state::team_offset;check(word(arena,e4_core_state::team_live)>4,"T15 tier1 live before shrink");
 for(unsigned i=0;i<58;++i){destroy(actors[i]);if(i==25){
  check(word(cai+4,e4_core_state::primary_live)==64&&word(arena,e4_core_state::team_live)==12,"T16 dropping65 to64 does not dissolve12 teams");
  const auto token=word(arena,e4_core_state::team_generations),count=word(arena,0x28);std::vector<u32> members;for(unsigned j=0;j<count;++j)members.push_back(word(arena,4+j*4));
  for(auto member:members){auto* c=resolve(member);reinterpret_cast<void(__fastcall*)(void*,void*,u32,u32)>(0x522b30)(ptr(cai),nullptr,token,member);word(address(c),0x538)=0;}
  check(word(arena,e4_core_state::team_live)==11,"T16 native leaves create tier0 FIFO hole while above cap8");
  word(address(actors[60]),0x800)=7;check(join(actors[60])==0&&word(arena,e4_core_state::team_live)==11,"T16 new team waits below cap even with tier0 slot free");word(address(actors[60]),0x800)=30;
 }}
 // At32, tier0 admission is stock-sized and independent of extended teams
 // still awaiting the next boundary. Above32 the preceding case must wait.
 word(address(actors[60]),0x800)=7;
 check(join(actors[60])!=0,"T16 at32 free tier0 slot admits before extended fold-back");
 word(address(actors[60]),0x800)=30;
 word(address(actors[58]),0x800)=29; // a living hero member in an extended team
 for(unsigned i=58;i<90;++i){const auto c=address(resolve(word(address(actors[i]),0x7dc)));for(auto off:{0x34u,0x38u,0x3cu,0x154u,0x17cu,0x2c0u,0x2e4u,0x2e8u,0x340u,0x344u})word(c,off)=0x10000+i*16+off;*reinterpret_cast<unsigned char*>(c+0x2ec)=1;*reinterpret_cast<unsigned char*>(c+0x350)=1;}
 const std::vector<void*> survivors(actors.begin()+58,actors.end());if(!tref.empty())save_tref_state(tref+".pre",survivors);
 std::vector<bytes> before;for(unsigned i=58;i<90;++i){const auto c=address(resolve(word(address(actors[i]),0x7dc)));before.emplace_back(reinterpret_cast<unsigned char*>(c),reinterpret_cast<unsigned char*>(c)+0x61c);}
 boundary();for(unsigned i=4;i<96;++i)check(!(word(arena,e4_core_state::team_active+(i/32)*4)&(1u<<(i%32))),"T15 fold-back clears tier1");
 if(!tref.empty())save_controller_bytes(tref+".post",survivors);
 for(unsigned i=58;i<90;++i){const auto c=address(resolve(word(address(actors[i]),0x7dc)));auto expected=before[i-58];std::memcpy(expected.data()+0x538,ptr(c+0x538),4);check(std::memcmp(expected.data(),ptr(c),0x61c)==0,"T22 fold-back preserves every non-membership byte");if(!keep&&!word(c,0x538))join(actors[i]);}
 if(keep)return {actors.begin()+58,actors.end()};for(unsigned i=58;i<90;++i)destroy(actors[i]);boundary();
 std::array<u32,96> generations{};for(unsigned i=0;i<96;++i)generations[i]=word(arena,e4_core_state::team_generations+i*4);
 reinterpret_cast<method>(0x5234c0)(ptr(arena),nullptr);
 for(unsigned i=0;i<96;++i)check(word(arena,e4_core_state::team_generations+i*4)==generations[i]+128,"T6/T15 team reset advances all96 generations");
 for(unsigned w=0;w<3;++w)check(word(arena,e4_core_state::team_constructed+w*4)==0&&word(arena,e4_core_state::team_active+w*4)==0,"T6/T15 team reset clears all3 bitmap words");return {};
}
}
int main(int argc,char** argv){
 if(argc<3){std::puts("usage: e4_core_host owned-XMen2.exe mode");return 2;}mode=argv[2];std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 AddVectoredExceptionHandler(0,crash);if(address(fixture_space)!=0x401000||!load(read(argv[1])))return 3;
 const std::array<const wchar_t*,6> names{L"EntityCapacity",L"ActorPools",L"AnimEnums",L"CharSounds",L"ResidentStats",L"MotionPool"};
 const std::array<const char*,6> values{"1","124","512","127","320","22528"};
 settings[{L"Limits",L"Actors"}]="96";for(unsigned i=0;i<6;++i)settings[{L"Limits",names[i]}]=values[i];
 const bool stock=mode=="stock-trace"||mode=="stockclone"||mode=="tref"||mode=="stock-brain";
 if(mode=="off"||stock)settings.erase({L"Limits",L"Actors"});if(mode=="invalid")settings[{L"Limits",L"Actors"}]="95";
 if(mode.starts_with("missing"))settings.erase({L"Limits",names.at(std::stoul(mode.substr(7)))});
 if(mode.starts_with("wrong"))settings[{L"Limits",names.at(std::stoul(mode.substr(5)))}]="wrong";
 if(mode=="on")plan_mutations();e4_core::configure();e4_observe::configure();actor_ai::consumers=31;e4_core::prepare(reinterpret_cast<HMODULE>(0x400000));
 if(mode.starts_with("guard")){const auto i=std::stoul(mode.substr(5));*reinterpret_cast<unsigned char*>(e4_core_sites::edits.at(i).va)^=1;}
 const auto original=snapshot();patch_transaction::transaction tx;
 std::array<unsigned char,6> verified_bytes{};
 const std::array<const char*,6> owners{"entity_capacity","ActorPools","AnimEnums","CharSounds","ResidentStats","MotionPool"};
 for(unsigned i=0;i<6;++i){if(mode=="unverified"+std::to_string(i))continue;
  if(e4_core_state::prepared&&i==0){phase_a(tx);continue;}
  if(e4_core_state::prepared&&i==1){check(actor_pools::contribute(tx,reinterpret_cast<HMODULE>(0x400000)),"prepare real ActorPools contribution");continue;}
  patch_transaction::contribution c;c.owner=owners[i];c.guards.push_back({address(&verified_bytes[i]),{0},"external prerequisite fixture"});c.writes.push_back({address(&verified_bytes[i]),{0},"external prerequisite fixture"});tx.add(std::move(c));
 }
 e4_core::contribute(tx,reinterpret_cast<HMODULE>(0x400000));const auto result=tx.commit();e4_core::after_commit(result==patch_transaction::outcome::committed);
 std::printf("core transaction: %s (%s)\n",patch_transaction::name(result),tx.report().c_str());
 const bool execute=stock||mode=="on"||mode=="reservation"||mode=="backstop"||mode=="aliases"||mode=="destructors"||mode.starts_with("brain")||mode.starts_with("throw")||mode.starts_with("trace")||mode.starts_with("foldtrace")||mode.starts_with("stalefold")||mode.starts_with("stalelive");
 if(!execute){
  check(!e4_core::installed()&&snapshot()==original,"T25/OFF no Stage2 image write or descriptor publication");
  if(mode.starts_with("missing")||mode.starts_with("wrong")){
   // Review should-fix: a missing/wrong prerequisite key leaves Stage 2 with
   // zero written bytes, while prerequisite-free contributions (here the six
   // one-byte external fixtures) still commit in their own transaction.
   const auto i=std::stoul(mode.substr(mode.starts_with("missing")?7:5));
   check(result==patch_transaction::outcome::committed&&tx.written_bytes()==6,"T25 prerequisite-free contributions commit; Stage2 contributes zero written bytes");
   check(!e4_core_state::prepared&&!e4_core_state::cai&&!save_enabled,"T25 no Stage2 backing, caves or save capability");
   std::string name;for(auto p=names[i];*p;++p)name+=char(*p);const auto status=e4_core::status();
   const auto expected=mode.starts_with("missing")?"missing prerequisite [Limits] "+name+"="+values[i]:"wrong prerequisite [Limits] "+name+"=wrong (needs "+std::string(values[i])+")";
   check(status.find(expected)!=std::string::npos,"T25 refusal names the exact missing or wrong prerequisite");
   check(save_refused&&save_refusal==expected,"T25 the save format is told why Actors=96 did not install (an actor save's refusal names it)");
  }
  if(mode.starts_with("unverified")){
   // An unprepared prerequisite contribution is a veto inside the shared
   // startup transaction: nothing in it installs, and the log says so.
   const auto i=std::stoul(mode.substr(10));
   check(result!=patch_transaction::outcome::committed,"T25 an unprepared prerequisite refuses the whole startup transaction");
   check(e4_core::status().find("prerequisite "+std::string(owners[i])+" not prepared")!=std::string::npos,"T25 the transaction refusal names the unprepared prerequisite");
   check(save_refused&&save_refusal.find("prerequisite "+std::string(owners[i])+" not prepared")!=std::string::npos,"T25 the save format is told the transaction refusal");
  }
  if(mode=="off"){gameplay_capabilities::record released;released.finalize();agreement.finalize();check(std::equal(agreement.bytes().begin(),agreement.bytes().end(),released.bytes().begin()),"OFF agreement unchanged");}
  else check(!agreement.finalize(),"T25 rule44 unverified");
 }else {
  if(!stock){check(e4_core::installed()&&save_enabled&&!refused&&!save_refused,"complete core and save capability publication");if(!e4_core::installed())return 1;}
  std::puts("dependencies");dependencies();std::puts("CAI getter");cai=address(reinterpret_cast<void*(__cdecl*)()>(0x5252e0)());if(!stock)check(cai==e4_core_state::cai,"A3 native getter and retained init guard");std::puts("allocator constructor");
  allocator=address(VirtualAlloc(nullptr,0x1aa520+64,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!allocator)return 4;std::memset(ptr(allocator+0x1aa520),0xa5,64);
  reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x4614f0)(ptr(allocator),nullptr);word(0x718fbc)=allocator;
  if(mode=="stock-brain")brain_tests(argv[3],true);
  else if(mode.starts_with("brain")){check(e4_core::test_set_encoding(std::stoul(mode.substr(5))),"T21 native brain test encoding");brain_tests(argv[3],false);}
  else if(mode.starts_with("trace")){check(argc>=4&&e4_core::test_set_encoding(std::stoul(mode.substr(5))),"T21 test-only native token encoding");team_trace(argv[3]);}
  else if(mode=="stock-trace")team_trace(argv[3]);
  else if(mode=="stockclone"){auto live=load_fold_state(argv[4]);team_trace(argv[3],live,90);}
  else if(mode=="tref")timeout_reference(argv[3],argv[4]);
  else if(mode.starts_with("stalefold")||mode.starts_with("stalelive")){check(e4_core::test_set_encoding(std::stoul(mode.substr(9))),"T21 stale fold encoding");auto survivors=stale_fold_tests(mode.starts_with("stalefold"),argv[4]);save_fold_state(argv[4],survivors);std::vector<live_actor> live;for(unsigned i=0;i<survivors.size();++i)live.push_back({survivors[i],58+i});team_trace(argv[3],live,90);}
  else if(mode.starts_with("foldtrace")){check(e4_core::test_set_encoding(std::stoul(mode.substr(9))),"T21 fold-back encoding");auto survivors=team_tests(true,argv[4]);save_fold_state(argv[4],survivors);std::vector<live_actor> live;for(unsigned i=0;i<survivors.size();++i)live.push_back({survivors[i],58+i});team_trace(argv[3],live,90);}
  else if(mode=="backstop"){for(unsigned i=0;i<96;++i)spawn();reinterpret_cast<u32(__fastcall*)(void*,void*,void*,void*)>(0x523060)(ptr(cai),nullptr,ptr(0x1234),nullptr);check(false,"T17 backstop must fail-stop");}
  else if(mode.starts_with("throw")){
   injected_phase=mode=="throw"?2:std::stoi(mode.substr(5));throw_brain=injected_phase==2;bool caught=false;void* published=nullptr;
   try{published=spawn();if(injected_phase==4)throw std::runtime_error("after publication fault");}catch(const std::runtime_error&){caught=true;}
   const auto live=word(cai+4,e4_core_state::primary_live),pending=e4_core::reserved();
   check(caught&&live==(injected_phase<2?0u:1u)&&pending==(injected_phase<2?1u:0u),"T18/T20 phase exception retains native allocation/reservation ownership");
   if(injected_phase==2)check((word(cai+4,e4_core_state::primary_constructed)&1)&&(word(cai+4,e4_core_state::primary_active)&1),"T18 native popped controller remains active and constructed after throw");
   std::printf("T18/T20 phase=%d caught=%u live=%u reserved=%u\n",injected_phase,caught,live,pending);if(failures)return 1;
   injected_phase=-1;throw_brain=false;if(mode.find("-step")!=std::string::npos)boundary();else{auto* next=spawn();if(mode=="throw4"){check(next&&published,"T20 exception after publication does not reopen construction");destroy(next);destroy(published);boundary();}else check(false,"T20 escaped constructor must fail-stop");}
  }else if(mode=="reservation")reservation_tests();
  else if(mode=="destructors"){destructor_tests();std::printf("E4 core destructor host: %u checks, %u failures\n",checks,failures);return failures?1:0;}
  else if(mode=="aliases")alias_tests();
  else {actor_tests();team_tests();reservation_tests();churn_tests();damage_tests();}
  bool canary=true;for(unsigned i=0;i<64;++i)canary&=*reinterpret_cast<unsigned char*>(allocator+0x1aa520+i)==0xa5;check(canary,"T12 allocator trailing canary intact");
  const auto observed=e4_observe::json();std::ofstream("e4-core-observation.json")<<observed;if(!stock&&mode!="aliases"&&!mode.starts_with("brain")&&!mode.starts_with("trace")&&!mode.starts_with("foldtrace")&&!mode.starts_with("stale"))check(clean_events(observed),"T19/T23 zero illegal lifetime/accounting events (T13 intentional stale probes excluded)");
 }
 std::printf("E4 core host: %u checks, %u failures\n",checks,failures);return failures?1:0;
}

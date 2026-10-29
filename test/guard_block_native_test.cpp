// Native block lifecycle, attachment/cache and damage/energy execution with
// synthetic registry/pool services. No game, rendering or input device starts.
// The third-party node is supplied as XMLB from the companion builder module.
#include "shared_handler_fixture.hpp"
#include "tk_guard_bridges.hpp"
#include "lcsave/xmlb.hpp"
#include <cstdarg>
#include <cstdlib>
using namespace handler_fixture;
extern "C" unsigned char fixture_space[0x400000];
namespace logger {void write(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}}
namespace {
using XNode=lcsave::detail::XNode;
struct ref {u32 token=0;void* pool=nullptr;};
struct pool {void** vt;u32 token;void* object;bool live;};
std::array<unsigned char,0x1000> actor{},stats{},move{},idle{},context{},buttons{},power{},effect{},attachment{},event{},usage{},damage{},factory{};
std::array<unsigned char,0x1000> guard{},query{},manager{},victim{},victim_stats{},foreign_attachment{};
std::array<void*,128> query_vt{},manager_vt{};
std::array<void*,160> actor_vt{},move_vt{},power_vt{},effect_vt{},factory_vt{},usage_vt{};
std::array<void*,3> pool_vt{};
pool definitions{},effects{},attached{},foreign{};
float clock_now=10.f,literal=0,upkeep=0,life=0;
int entries=0,allocations=0,removals=0,generic_inputs=0,dispatches=0;
int contact_case=0,queries=0;
bool remove_at_end=false,lookup_missing=false;
bool generic_pending=false;
std::string requested;
u32 empty_name=0;
ref empty_ref{};
float __cdecl clock_value(){return clock_now;}
int __fastcall team(void* p,void*){return p==victim.data()?(contact_case==4?0x1c:3):2;}
void __fastcall noop(void*,void*){}
void __fastcall noop_arg(void*,void*,void*){}
void __fastcall no_payload(void*,void*,u32,void*,int,u32,int,int){}
bool __fastcall no(void*,void*){return false;}
bool __fastcall yes(void*,void*){return true;}
int __fastcall zero(void*,void*){return 0;}
float __fastcall negative(void*,void*){return -1.f;}
float __fastcall one_arg(void*,void*,void*){return 1.f;}
bool __fastcall no_attr(void*,void*,int,void*){return false;}
bool __fastcall no_status(void*,void*,int){return false;}
bool __fastcall token_valid(u32* token,void*){return *token==0x1006f;}
void* __fastcall token_get(u32*,void*){return actor.data();}
void* __cdecl actor_cast(void* p){return p==actor.data()?p:nullptr;}
void* __cdecl no_cast(void*){return nullptr;}
void* __cdecl actor_by_token(u32){return actor.data();}
void* __cdecl get_factory(){return factory.data();}
bool __fastcall pool_valid(pool* p,void*,u32 t){return p->live&&t==p->token;}
void* __fastcall pool_get(pool* p,void*,u32 t){check(p->live&&t==p->token,"generation-valid native pool access");return p->object;}
ref* __fastcall none_ref(void*,void*,ref* out){*out={};return out;}
ref* __fastcall next_effect(void*,void*){return &empty_ref;}
ref* __fastcall none_index(void*,void*,ref* out,int){*out={};return out;}
ref* __fastcall head_effect(void*,void*,ref* out){*out={effects.token,&effects};return out;}
ref* __fastcall effect_definition(void*,void*,ref* out){*out={definitions.token,&definitions};return out;}
u32* __fastcall zero_bits(void*,void*,u32* out){*out=0;return out;}
bool __fastcall effect_enabled(void*,void*,int,int){return true;}
int __fastcall attribute(void*,void*){return 31;}
int __fastcall scale_kind(void*,void*){return 1;}
float __fastcall effect_range(void*,void*,void*,float,void* maximum){
 // Native literal evaluator, no saved talent or fabricated damage formula.
 std::array<unsigned char,12> descriptor{};field<float>(descriptor.data(),0)=literal;
 return reinterpret_cast<float(__thiscall*)(void*,void*,void*)>(0x4e7810)(descriptor.data(),stats.data(),maximum);
}
float __fastcall effect_value(void* self,void*,void* caster,float scale){return effect_range(self,nullptr,caster,scale,nullptr);}
bool __fastcall node_end_flag(void*,void*){return remove_at_end;}
ref* __fastcall allocate(void*,void*,ref* out){
 ++allocations;check(!attached.live,"one native attachment per block entry");
 attached.live=true;attachment.fill(0);field<void*>(attachment.data(),0)=at(0x696e7c);
 reinterpret_cast<void(__thiscall*)(void*)>(0x537400)(attachment.data());
 *out={attached.token,&attached};return out;
}
void __fastcall free_attachment(void*,void*,u32 token,void* p){
 check(p==&attached&&token==attached.token,"native unlink frees only its complete attachment token");
 attached.live=false;++removals;
}
bool __fastcall admit_effect(void*,void*,u32,void*,u32){return true;}
bool __fastcall existing_effect(void*,void*,ref* out,u32,void*,u32,int){*out={};return false;}
void* __fastcall move_event(void*,void*,int index){return index==0?usage.data():index==1?event.data():nullptr;}
u32* __fastcall node_power(void*,void*){return &empty_name;}
void* __fastcall handler(void* p,void*){return p==move.data()?&tk_guard_bridges::guard_block_handler:nullptr;}
void* __fastcall get_style(void* p,void*){return p;}
void* __fastcall intern(u32* p,void*,const char* name){requested=name;*p=1;return p;}
void* __fastcall lookup(void*,void*,const u32*,void*){return lookup_missing?nullptr:requested=="guardblocking"?move.data():idle.data();}
void* __cdecl get_manager(){return manager.data();}
void* __cdecl contact_character(void* p){return p==victim.data()&&contact_case!=5?p:nullptr;}
void* __cdecl physical(void* p){return p;}
void* __fastcall query_ctor(void** p,void*,const void*,const u32*){*p=query.data();return p;}
void __fastcall query_filter(void*,void*,int,int){}
void __cdecl query_team(void**,void*){}
tk_guard_bridges::vec* __fastcall position(void* p,void*,tk_guard_bridges::vec*){return &field<tk_guard_bridges::vec>(p,0x20);}
void __fastcall probe(void*,void*,const void*,const void*,float distance,const tk_guard_bridges::vec* box){++queries;check(distance==48&&box->x==16&&box->y==16&&box->z==16,"block fallback retains native Guard probe");}
void** __fastcall query_contact(void*,void*,void** out){*out=contact_case?victim.data():nullptr;return out;}
void* __fastcall contact_entity(void** p,void*){return *p;}
bool __fastcall no_lift(void*,void*,void*){return false;}
const char* __fastcall error_text(void*,void*,int){return "cannot lift";}
u32 __fastcall error_message(void*,void*,u32,const char*,float,std::array<float,4>,int){return 1;}
void* __fastcall action(void*,void*,u32 id){check(id==0x18||id==0,"release resolves Special-to-Idle");return idle.data();}
bool __fastcall generic(void*,void*,void*,void* input){
 ++generic_inputs;check(!(field<u32>(input,0)&0x100)&&!(field<u32>(input,4)&0x100),"held block never reacquires Guard");
 if(generic_pending)field<void*>(actor.data(),0x3bc)=idle.data();return generic_pending;
}
void __fastcall ep_set(void* p,void*,float value){field<float>(p,0x288)=value;}
void __fastcall hp_set(void* p,void*,float value){field<float>(p,0x27c)=value;}
u32 debit_body=0x4319b5;
__declspec(naked) void finite_debit(){__asm {
 sub esp,0x78
 push esi
 mov esi,ecx
 mov eax,[esp+0x80]
 mov [esp+4],eax
 jmp dword ptr [debit_body]
}}
u32 receive_body=0x437ced;
__declspec(naked) void receive_conversion(void*){__asm {
 push ebx
 push ebp
 push esi
 push edi
 sub esp,0x40
 mov esi,[esp+0x54]
 lea ebp,[esi+0xc]
 jmp dword ptr [receive_body]
}}
__declspec(naked) void receive_done(){__asm {
 add esp,0x40
 pop edi
 pop esi
 pop ebp
 pop ebx
 ret
}}
u32 hp_body=0x4983a8;
__declspec(naked) void receive_health(void*,float){__asm {
 push esi
 mov esi,[esp+8]
 fld dword ptr [esp+12]
 sub esp,0x14
 jmp dword ptr [hp_body]
}}
__declspec(naked) void health_done(){__asm {add esp,0x14} __asm {pop esi} __asm {ret}}
// Native scheduler/dispatcher slices preserve the selected node, invoke the
// block handler input once, then use native move cancel and node installation.
void transition(void* next){
 ++dispatches;
 if(field<void*>(actor.data(),0x378)==move.data())reinterpret_cast<void(__thiscall*)(void*)>(0x42c270)(actor.data());
 reinterpret_cast<void(__thiscall*)(void*,void*)>(0x41d890)(actor.data(),next);
 if(next==move.data()){
  std::array<unsigned char,0x80> incoming{};field<void*>(incoming.data(),4)=next;
  ++entries;tk_guard_bridges::guard_block_entry(&tk_guard_bridges::guard_block_handler,nullptr,actor.data(),buttons.data(),incoming.data());
  // Native CCEPowerup dispatch -> application -> attached initialization.
  reinterpret_cast<bool(__thiscall*)(void*,void*,void*)>(0x4e8360)(event.data(),actor.data(),actor.data());
 }
}
void __fastcall dispatch(void*,void*,void* next,int){transition(next);}
void refresh(){reinterpret_cast<void(__thiscall*)(void*)>(0x53bf20)(actor.data()+0x470);}
void attachment_tick(){if(attached.live)reinterpret_cast<void(__thiscall*)(void*,u32,void*)>(0x537470)(attachment.data(),attached.token,&attached);}
void input_tick(){
 const bool pending=reinterpret_cast<bool(__thiscall*)(void*,void*,void*)>(0x4f56e0)(move.data(),actor.data(),buttons.data());
 if(pending){void* next=field<void*>(actor.data(),0x3bc);field<void*>(actor.data(),0x3bc)=nullptr;transition(next);}
 attachment_tick();refresh();
}
void hit(float amount){
 damage.fill(0);field<void*>(damage.data(),0)=actor.data();field<float>(damage.data(),0x9c)=amount;
 receive_conversion(damage.data());
 receive_health(actor.data(),field<float>(damage.data(),0x9c));
}
const std::string& attr(const XNode& n,const char* key){static const std::string empty;auto* p=n.get(key);return p?*p:empty;}
void bind(const image& pc){
 // Load original owners wholesale; hooks below are explicit host boundaries.
 for(auto section:pc.sections){const u32 end=std::min(section.va+section.size,0x801000u);if(section.va>=0x401000&&end>section.va)pc.load(section.va,end-section.va);}
 jump(0x45fe60,&clock_value);jump(0x41a220,&team);jump(0x4654d0,&token_valid);jump(0x4654b0,&token_get);
 jump(0x474a80,&actor_cast);jump(0x546070,&no_cast);jump(0x537030,&actor_by_token);jump(0x42d8a0,&actor_by_token);jump(0x53e910,&actor_by_token);
 jump(0x54d710,&no_attr);jump(0x543f80,&get_factory);jump(0x54e9d0,&admit_effect);jump(0x54eca0,&existing_effect);
 jump(0x54e650,&noop); // periodic payload; the native node-end branch precedes it
 // These two services take a reference pair. Their periodic/UI work is outside
 // this test; node ownership, list insertion/removal and cache work stay native.
 at(0x54e650)[0]=0xc2;at(0x54e650)[1]=8;at(0x54e650)[2]=0;
 jump(0x41d0d0,&get_style);jump(0x41b120,&intern);jump(0x501a50,&lookup);jump(0x427c80,&action);jump(0x42c340,&dispatch);
 jump(0x4fce50,&generic);jump(0x4c87a0,&no);jump(0x4318d0,&finite_debit);jump(0x437d6a,&receive_done);jump(0x4983c5,&health_done);
 jump(0x423f00,&noop);
 jump(0x46dce0,&get_manager);jump(0x48de40,&get_manager);jump(0x484990,&get_manager);jump(0x4c05b0,&get_manager);
 jump(0x4ab6d0,&query_ctor);jump(0x4ab720,&noop);jump(0x44fe10,&query_team);jump(0x4a9590,&contact_entity);
 jump(0x429110,&physical);jump(0x402b60,&contact_character);jump(0x41a160,&no_status);jump(0x427f60,&no_lift);
 jump(0x59ee20,&get_manager);jump(0x4bdb30,&get_manager);jump(0x421080,&noop);
 pool_vt[0]=reinterpret_cast<void*>(&pool_valid);pool_vt[2]=reinterpret_cast<void*>(&pool_get);
 definitions={pool_vt.data(),0x10001,power.data(),true};effects={pool_vt.data(),0x20001,effect.data(),true};attached={pool_vt.data(),0x30001,attachment.data(),false};
 foreign={pool_vt.data(),0x40001,foreign_attachment.data(),false};
 power_vt.fill(reinterpret_cast<void*>(&zero));effect_vt.fill(reinterpret_cast<void*>(&zero));
 power_vt[0x2c/4]=reinterpret_cast<void*>(&no_payload);
 power_vt[0x34/4]=reinterpret_cast<void*>(&zero_bits);power_vt[0x64/4]=power_vt[0x7c/4]=reinterpret_cast<void*>(&none_index);
 power_vt[0x78/4]=reinterpret_cast<void*>(&head_effect);power_vt[0xb4/4]=reinterpret_cast<void*>(&one_arg);
 power_vt[0xc8/4]=reinterpret_cast<void*>(&yes);power_vt[0xfc/4]=reinterpret_cast<void*>(&node_end_flag);power_vt[0xa0/4]=reinterpret_cast<void*>(&negative);power_vt[0x104/4]=reinterpret_cast<void*>(&yes);
 effect_vt[0x1c/4]=reinterpret_cast<void*>(&next_effect);effect_vt[0x3c/4]=reinterpret_cast<void*>(&none_ref);effect_vt[0x2c/4]=reinterpret_cast<void*>(&attribute);
 effect_vt[0x24/4]=reinterpret_cast<void*>(&noop_arg);
 effect_vt[0x30/4]=reinterpret_cast<void*>(&effect_value);effect_vt[0x34/4]=reinterpret_cast<void*>(&effect_range);effect_vt[0x38/4]=reinterpret_cast<void*>(&scale_kind);
 effect_vt[0x44/4]=reinterpret_cast<void*>(&effect_enabled);effect_vt[0x64/4]=reinterpret_cast<void*>(&effect_definition);
 factory_vt[0x3c/4]=reinterpret_cast<void*>(&allocate);factory_vt[0x40/4]=reinterpret_cast<void*>(&free_attachment);
 field<void*>(factory.data(),0)=factory_vt.data();field<void*>(power.data(),0)=power_vt.data();field<void*>(effect.data(),0)=effect_vt.data();
 manager_vt[0x1a8/4]=manager_vt[0x5c/4]=manager_vt[0x3c/4]=reinterpret_cast<void*>(&no);
 manager_vt[8/4]=reinterpret_cast<void*>(&error_text);manager_vt[0x34/4]=reinterpret_cast<void*>(&error_message);
 field<void*>(manager.data(),0)=manager_vt.data();field<void*>(query.data(),0)=query_vt.data();
 query_vt[0x40/4]=reinterpret_cast<void*>(&query_filter);query_vt[0x70/4]=reinterpret_cast<void*>(&probe);query_vt[0x20/4]=reinterpret_cast<void*>(&query_contact);
}
void reset(){
 actor.fill(0);stats.fill(0);move.fill(0);idle.fill(0);buttons.fill(0);context.fill(0);event.fill(0);usage.fill(0);victim.fill(0);victim_stats.fill(0);
 field<void*>(actor.data(),0)=actor_vt.data();field<void*>(actor.data(),0x35c)=stats.data();field<u32>(actor.data(),0x1c)=0x1006f;
 field<u32>(actor.data(),4)=1u<<20;field<int>(actor.data(),0x3d4)=0;field<unsigned char>(actor.data(),0x258)=1;
 field<float>(actor.data(),0x27c)=100;field<float>(actor.data(),0x288)=100;field<float>(actor.data(),0x314)=100;field<void*>(actor.data(),0x470)=actor.data();
 field<void*>(actor.data(),0x378)=idle.data();field<void*>(move.data(),0)=move_vt.data();field<void*>(idle.data(),0)=move_vt.data();
 field<void*>(move.data(),0xc8)=&tk_guard_bridges::guard_block_handler;
 field<void*>(event.data(),4)=move.data();field<ref>(event.data(),0x14)={definitions.token,&definitions};field<float>(event.data(),0x24)=life;
 field<void*>(event.data(),0)=at(0x6912c4);
 field<void*>(usage.data(),0)=usage_vt.data();field<short>(usage.data(),0x10)=static_cast<short>(upkeep);
 field<void*>(context.data(),4)=move.data();field<u32>(buttons.data(),0)=field<u32>(buttons.data(),4)=0x100;
 field<void*>(victim.data(),0x35c)=victim_stats.data();field<float>(victim.data(),0x27c)=100;field<u32>(victim.data(),0x1c)=0x200de;
 attached.live=foreign.live=false;entries=allocations=removals=dispatches=generic_inputs=0;queries=0;lookup_missing=false;
 generic_pending=false;
 tk_guard_bridges::guard_blocks.clear();
}
void run(const image& pc,const XNode& node){
 check(attr(node,"name")=="guardblocking"&&attr(node,"handler")=="ch_guard_block"&&attr(node,"animenum")=="ea_blocking","authored private node selects real PC block lifecycle");
 for(const auto& n:node.children){
  check(n.tag!="require","authored node needs no saved block skill");
  if(n.tag=="trigger"&&attr(n,"name")=="powerusage"){upkeep=std::strtof(attr(n,"powerusage").c_str(),nullptr);check(attr(n,"time")=="-1","authored upkeep is continuous");}
  if(n.tag=="trigger"&&attr(n,"name")=="powerup"){
   life=std::strtof(attr(n,"life").c_str(),nullptr);remove_at_end=attr(n,"remove_on_node_end")=="true";
   check(attr(n,"time")=="0"&&remove_at_end&&life==-1,"authored time-zero effect ends with its node");
   check(n.children.size()==1&&attr(n.children[0],"attribute")=="damage_to_energy"&&attr(n.children[0],"affect_type")=="scale"&&attr(n.children[0],"level")=="1","authored literal rank1 conversion");
   literal=std::strtof(attr(n.children[0],"level").c_str(),nullptr);
  }
  if(n.tag=="chain")check(attr(n,"action")=="special"&&attr(n,"result")=="idle","authored release chain returns Idle");
 }
 check(upkeep==1&&literal==1,"both authored native energy terms resolve without a talent");
 bind(pc);tk_guard_bridges::prepare_guard_block();
 actor_vt[0x1a4/4]=reinterpret_cast<void*>(&hp_set);actor_vt[0x1a8/4]=reinterpret_cast<void*>(&ep_set);
 move_vt[0xc/4]=reinterpret_cast<void*>(0x4f5fb0);move_vt[0x18/4]=reinterpret_cast<void*>(&move_event);move_vt[0x30/4]=reinterpret_cast<void*>(0x500640);move_vt[0x38/4]=reinterpret_cast<void*>(0x4f5670);move_vt[0x3c/4]=reinterpret_cast<void*>(0x4f5690);move_vt[0xcc/4]=reinterpret_cast<void*>(&node_power);
 actor_vt[0x24/4]=reinterpret_cast<void*>(&position);
 usage_vt[0x20/4]=reinterpret_cast<void*>(0x4dae60);
 usage_vt[0x24/4]=reinterpret_cast<void*>(0x4dbb30);usage_vt[0x28/4]=reinterpret_cast<void*>(&no);
 reset();transition(move.data());refresh();
 auto* protected_input=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS));
 void* committed=protected_input?VirtualAlloc(protected_input,4096,MEM_COMMIT,PAGE_READWRITE):nullptr;
 check(committed!=nullptr,"input extent fixture reserves a protected next page");
 if(committed){
  auto* edge=protected_input+4096-0x24;field<u32>(edge,0)=0x100;
  check(!tk_guard_bridges::guard_block_input(&tk_guard_bridges::guard_block_handler,nullptr,actor.data(),edge)&&field<u32>(edge,0)==0x100,"held bridge reads only the normalized input extent and preserves the caller's Guard mask");
 }
 if(protected_input)VirtualFree(protected_input,0,MEM_RELEASE);
 for(contact_case=0;contact_case<6;++contact_case){
  reset();field<void*>(context.data(),4)=guard.data();field<unsigned char>(context.data(),0x6c)=0x80;
  if(contact_case==1)field<float>(victim.data(),0x2f0)=clock_now+1.5f;
  if(contact_case==2)field<unsigned char>(victim_stats.data(),0x2ad)=8;
  if(contact_case==3)field<unsigned char>(victim.data(),0x578)=4;
  tk_guard_bridges::guard_entry(nullptr,nullptr,actor.data(),buttons.data(),context.data());refresh();
  check(queries==1&&entries==1&&allocations==1&&dispatches==1&&field<float>(actor.data(),0x4ec)==1&&(field<unsigned char>(actor.data(),0x765)&2),"actual refused player guard entry dispatches one real block with native effects (empty/deadline/large/def_grab/neutral/heavy)");
  check(field<unsigned char>(context.data(),0x6c)==0x84,"incoming Guard aborts its parent and preserves unrelated context bits");
 }
 for(int duration:{16,300,500,800}){
  reset();transition(move.data());refresh();
  check(entries==1&&allocations==1&&(field<unsigned char>(actor.data(),0x765)&2)&&field<float>(actor.data(),0x4ec)==1,"real entry, CCE application and native effect cache active once");
  field<u32>(buttons.data(),4)=0;
  for(int ms=0;ms<duration;ms+=16)input_tick();
  check(entries==1&&allocations==1&&attached.live,"tap/300/500/800ms hold keeps one node/effect owner");
  field<u32>(buttons.data(),0)=0;input_tick();
  check(!(field<unsigned char>(actor.data(),0x765)&2)&&!attached.live&&removals==1&&field<float>(actor.data(),0x4ec)==0,"release invokes native cancel, attachment node-end unlink and cache cleanup");
 }
 for(bool blocked:{false,true})for(float ep:{0.f,5.f,20.f,100.f}){
  reset();if(blocked){transition(move.data());refresh();}
  field<float>(actor.data(),0x288)=ep;hit(20.f);
  check(field<float>(actor.data(),0x27c)==(blocked?100.f-std::max(20.f-ep,0.f):80.f),"native receive HP conversion differs blocked/released at insufficient/equal/sufficient energy");
  check(field<float>(actor.data(),0x288)==(blocked?std::max(ep-20.f,0.f):ep),"native debit clamps EP at zero while preserving released EP");
 }
 reset();transition(move.data());refresh();
 reinterpret_cast<void(__thiscall*)(void*,void*)>(0x4f5670)(move.data(),actor.data());refresh();
 check(!attached.live&&tk_guard_bridges::guard_blocks.empty()&&!(field<unsigned char>(actor.data(),0x765)&2)&&field<float>(actor.data(),0x4ec)==0,"native normal exit and cancellation both finish bit and effect ownership");
 for(int cause=0;cause<5;++cause){
  reset();transition(move.data());refresh();field<u32>(buttons.data(),4)=0;
  if(cause==0)field<float>(buttons.data(),8)=1; // movement
  if(cause==1)field<u32>(buttons.data(),4)=0x10; // another action
  if(cause==2)field<float>(actor.data(),0x27c)=0; // death
  if(cause==3)field<float>(actor.data(),0x288)=0; // exhaustion
  if(cause==4)transition(idle.data()); // external hit/cancel
  else input_tick();
  attachment_tick();refresh();
  if(attached.live||(field<unsigned char>(actor.data(),0x765)&2)||field<float>(actor.data(),0x4ec)!=0)std::printf("cleanup cause=%d alive=%d bit=%u effect=%g next=%p current=%p\n",cause,int(attached.live),unsigned(field<unsigned char>(actor.data(),0x765)&2),field<float>(actor.data(),0x4ec),field<void*>(actor.data(),0x3bc),field<void*>(actor.data(),0x378));
  check(!attached.live&&!(field<unsigned char>(actor.data(),0x765)&2)&&field<float>(actor.data(),0x4ec)==0,"movement/other input/death/exhaustion/external cancel fully clean native effect and block flag");
 }
 reset();transition(move.data());refresh();field<u32>(buttons.data(),4)=0;
 generic_pending=true;field<u32>(buttons.data(),4)=0x10;input_tick();
 check(generic_inputs==1&&!attached.live&&entries==1&&!(field<unsigned char>(actor.data(),0x765)&2),"native generic interruption result dispatches once and completes block cleanup");
 reset();transition(move.data());refresh();field<u32>(buttons.data(),4)=0;
 for(int i=0;i<3;++i)reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4daa30)(usage.data(),actor.data(),actor.data());
 check(field<float>(actor.data(),0x288)==97&&entries==1&&allocations==1,"actual native powerusage event performs rank1 upkeep without re-entry");
 reset();transition(move.data());refresh();foreign.live=true;foreign_attachment.fill(0);
 field<void*>(foreign_attachment.data(),0)=at(0x696e7c);reinterpret_cast<void(__thiscall*)(void*)>(0x537400)(foreign_attachment.data());
 field<ref>(foreign_attachment.data(),0x20)={definitions.token,&definitions};field<u32>(foreign_attachment.data(),0x18)=0x1006f;field<void*>(foreign_attachment.data(),0x60)=idle.data();
 field<ref>(attachment.data(),4)={foreign.token,&foreign};transition(idle.data());
 check(!attached.live&&foreign.live&&field<ref>(actor.data(),0x21c).token==foreign.token,"block exit native unlink preserves another node's complete-token effect owner");
 reset();transition(move.data());refresh();field<void*>(actor.data(),0x378)=idle.data();
 tk_guard_bridges::guard_block_cancel(&tk_guard_bridges::guard_block_handler,nullptr,actor.data());refresh();
 check(!attached.live&&tk_guard_bridges::guard_blocks.empty(),"late cancel cleans its incoming node owner after actor current move has changed");
 reset();transition(move.data());refresh();field<u32>(actor.data(),0x1c)+=0x10000;
 tk_guard_bridges::guard_block_cancel(&tk_guard_bridges::guard_block_handler,nullptr,actor.data());
 check(attached.live&&removals==0&&(field<unsigned char>(actor.data(),0x765)&2),"stale actor generation cannot authorize another actor's flag/effect cleanup");
 reset();transition(move.data());refresh();const auto generation=attached.token;attached.token+=0x10000;transition(idle.data());refresh();
 check(removals==0&&field<float>(actor.data(),0x4ec)==0&&!(field<unsigned char>(actor.data(),0x765)&2),"stale attachment generation authorizes no removal or aliased effect access");attached.token=generation;
}
}
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 AddVectoredExceptionHandler(1,[](PEXCEPTION_POINTERS e)->LONG{std::printf("exception %08lX at %p ecx=%08lX eax=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX return=%08X\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,e->ContextRecord->Ecx,e->ContextRecord->Eax,e->ContextRecord->Esi,e->ContextRecord->Edi,e->ContextRecord->Ebp,e->ContextRecord->Esp,*reinterpret_cast<u32*>(e->ContextRecord->Esp));ExitProcess(1);});
 if(argc!=3)return 2;DWORD old=0;if(!VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old))return 2;
 image pc(argv[1],pc_hash);if(failures)return 1;
 std::ifstream f(argv[2],std::ios::binary);std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(f),{}};
 const auto root=lcsave::detail::decode_xmlb(bytes.data(),bytes.size());run(pc,root);
 std::printf("Guard block native: %d checks, %d failures\n",checks,failures);return failures?1:0;
}

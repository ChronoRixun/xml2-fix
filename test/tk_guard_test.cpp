#include "shared_handler_fixture.hpp"
#include "tk_guard_bridges.hpp"
#include "shared_handler_c_rules.hpp"
#include "pad_input_rules.hpp"
#include "test_input_rules.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
using namespace handler_fixture;
using vec=tk_guard_bridges::vec;
namespace logger {std::vector<std::string> captured;void write(const char* format,...) {char out[1024]{};va_list args;va_start(args,format);std::vsnprintf(out,sizeof out,format,args);va_end(args);captured.emplace_back(out);}}
extern "C" unsigned char fixture_space[0x400000];
constexpr u32 bias=0x10000000;
namespace lift_rule {int source_override=-1;int heaviness(void* p){return source_override>=0?source_override:field<unsigned char>(p,0x31d);}}
namespace {
std::array<unsigned char,0x1000> actor{},source_actor{},target{},stats{},input{},context{},node{},event{},attack{},type{},move{},query{},manager{};
std::array<void*,170> actor_vt{},target_vt{},node_vt{},event_vt{},move_vt{},query_vt{},audio_vt{},manager_vt{};
std::array<unsigned char,16> ui{};std::array<void*,32> ui_vt{};
// The real audio lookup reads its disabled byte at +d0ae. Keep its complete
// footprint so the contract check executes retail instructions, not a fake ABI.
std::array<unsigned char,0xd100> audio{};
std::array<unsigned char,16> ordered{};std::array<void*,4> ordered_vt{};
bool source=false,target_actor=true,token_valid=true,target_alive=true,wall=false,can_lift=true,target_ok=true,can_grab=true,friend_damage=false,session_busy=false,fallback_result=false,immune=false;
int acrobatics=0,might=0,effective_might_override=-1,owner_team=1,target_team=2,query_calls=0,destructs=0,action_id=-1,starts=0,sounds=0,stops=0,debits=0,publications=0;
float time_now=10.f,energy=100.f,last_cost=0.f;vec last_velocity{},release_velocity{},placed{},turned{};float release_heading=0;int release_level=0,release_mode=0;
int preentries=0;std::int8_t queried_level=0,queried_heavy=0;std::int16_t queried_impact=0;
u32 acquired_token=222,auxiliary_token=0;int auxiliary_calls=0,auxiliary_events=0;
u32 audit_before[4]{},audit_after[4]{};
bool base_damage_parsed=false;
bool stale_resolution=false;
bool retail_arm=false;float arm_speed=100.f;
void native_arm(void*,void*,int);
std::string selected_name,played_name;std::vector<int> order;
void* self_actor(){return source?source_actor.data():actor.data();}
void* __fastcall get_type(void*,void*){return type.data();}
void* __fastcall resolve(u32* token,void*){return *token==222&&(token_valid||stale_resolution)?target.data():nullptr;}
bool __fastcall valid(u32* token,void*){return *token&&token_valid;}
void* __cdecl character(void* p){return p==target.data()&&target_actor?p:nullptr;}
void* __cdecl physical(void* p){return p==target.data()?p:nullptr;}
void* __cdecl token_character(u32 token){return token==222&&target_actor?target.data():nullptr;}
void* __cdecl token_physical(u32 token){return token==222?target.data():nullptr;}
void* __cdecl no_cast(void*){return nullptr;}
void __fastcall publish(void*,void*){++publications;order.push_back(1);}
float __cdecl clock_value(){return time_now;}
float __fastcall sim_time(void*,void*){return time_now;}
u32 __fastcall effect_serial(void*,void*){return 77;}
void* __cdecl get_manager(){return manager.data();}
void* __cdecl get_audio(){return audio.data();}
void* __cdecl get_ui(){return ui.data();}
bool __fastcall yes(void*,void*){return true;}
bool __fastcall not_busy(void*,void*){return session_busy;}
bool __fastcall friends(void*,void*){return friend_damage;}
bool __fastcall camera_visible(void*,void*,const vec*,float,int){return true;}
float __fastcall get_energy(void*,void*){return energy;}
void __fastcall spend(void*,void*,float cost){last_cost=cost;++debits;order.push_back(2);}
bool __cdecl no_mode(){return false;}
int __fastcall team(void* p,void*){return p==target.data()?(source&&target_team==0x1c?0x1a:target_team):owner_team;}
bool __fastcall grab_ok(void*,void*){return can_grab;}
// The production helper enters the matching PC service after its extra gate;
// finish the native frame in this fixture stub.
__declspec(naked) void grabbable_tail(){__asm {
 mov al,can_grab
 pop esi
 pop ecx
 ret
}}
bool __fastcall target_allowed(void*,void*){return target_ok;}
bool scripted=false;
bool __fastcall script_allowed(void*,void*,int){return scripted;}
bool __fastcall liftable(void*,void*,void*){return can_lift;}
bool __fastcall fallback(void*,void*,void*,void*){return fallback_result;}
int native_fallback_parameters=0;
float __fastcall native_movement_parameter(void*,void*,int id){++native_fallback_parameters;check(id==2,"zero-input guard fallback requests only movement scale");return 100.f;}
bool __fastcall immunity(void*,void*,int id,int){check(id==(source?0x17:0x2c),"def_pickup enum translated by name");return immune;}
void* __fastcall source_action(void*,void*,int id){action_id=id;return move.data();}
void* __fastcall entry_action(void*,void*,void* from,int id,void* who){check(from==move.data()&&who==actor.data(),"entry action resolves from the incoming move");source_action(who,nullptr,id);return node.data();}
void* __fastcall named_lookup(void*,void*,const u32*,void*){return move.data();}
void* __fastcall get_style(void* p,void*){return p;}
void* __fastcall make_name(u32* p,void*,const char* name){selected_name=name;*p=1;return p;}
void* __fastcall source_named(void*,void*,const u32*,int){return move.data();}
void __fastcall play(void*,void*,void* m,int){check(m==move.data()||m==node.data(),"resolved selected move dispatched");++starts;played_name=selected_name;order.push_back(3);}
void __fastcall lock(void* p,void*,u32 token){check(token==222,"selection locks target token");field<u32>(p,source?0x554:0x76c)=token;}
void __fastcall velocity(void*,void*,const vec* p){last_velocity=*p;order.push_back(4);}
void __fastcall set_position(void*,void*,const vec* p){placed=*p;}
void __fastcall set_angles(void*,void*,const vec* p){turned=*p;}
const vec* __fastcall position(void* p,void*,vec* temporary){*temporary=field<vec>(p,0x20);return temporary;}
void* __fastcall handler_of(void*,void*){return move.data();}
vec* __cdecl facing(vec* p,const void*){*p={1,0,0};return p;}
vec* __cdecl angles(vec* out,const vec*){*out={0,0,3.1415927f};return out;}
float __cdecl yaw(const vec*){return 3.1415927f;}
float __fastcall pose(void*,void*,const void*){return 0;}
bool __fastcall input_held(void*,void*,void*,void* p,int bit){return (field<u32>(p,0)&(1u<<bit))!=0;}
float __fastcall normalize(vec* p,void*){const float n=std::sqrt(p->x*p->x+p->y*p->y+p->z*p->z);if(n>0){p->x/=n;p->y/=n;p->z/=n;}return n;}
float __fastcall vector_length(vec* p,void*){return std::sqrt(p->x*p->x+p->y*p->y+p->z*p->z);}
void* __fastcall vector_sub(vec* p,void*,const vec* q){p->x-=q->x;p->y-=q->y;p->z-=q->z;return p;}
void* __fastcall vector_add(vec* p,void*,const vec* q){p->x+=q->x;p->y+=q->y;p->z+=q->z;return p;}
void* __fastcall vector_difference(vec* p,void*,vec* out,const vec* q){*out={p->x-q->x,p->y-q->y,p->z-q->z};return out;}
__declspec(naked) void modulus(){__asm {
 fxch st(1)
 again: fprem
 fnstsw ax
 test ah,4
 jnz again
 fstp st(1)
 ret
}}
__declspec(naked) void truncate(){__asm {
 sub esp,16
 fnstcw [esp+8]
 mov ax,[esp+8]
 or ax,0xc00
 mov [esp+10],ax
 fldcw [esp+10]
 fistp qword ptr [esp]
 fldcw [esp+8]
 mov eax,[esp]
 mov edx,[esp+4]
 add esp,16
 ret
}}
u32 __fastcall sound_resource(void*,void*,const char* name,int a,int b){if(!source)check(name&&(!std::strcmp(name,"character/phoen_m/power1a")||!std::strcmp(name,"character/phoen_m/power1b"))&&!a&&!b,"PC name lookup takes name and two zero modes");return 77;}
bool __fastcall sound_valid(void*,void*,u32){return true;}
u32 __fastcall start_sound(void*,void*,u32,const vec*,float,float,float){++sounds;return 88;}
void __fastcall sound_position(void*,void*,u32,const vec*){}
void __fastcall one_shot(void*,void*,u32){}
void __fastcall stop_sound(void*,void*,u32){++stops;order.push_back(5);}
void __fastcall physical_mode(void*,void*,int){}
void __fastcall physical_cleanup(void*,void*,int,int){}
void __fastcall pc_release(void*,void*,vec v,float heading,std::int16_t level,int mode,int extra){if(retail_arm){arm_speed=(v.x==0&&v.y==0&&v.z==0)?100.f:std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);native_arm(target.data(),actor.data(),extra);}else check(extra==(mode?2:0),"TK throw uses its private native arming mode; cancellation stays native zero");release_velocity=v;release_heading=heading;release_level=level;release_mode=mode;order.push_back(6);}
void __fastcall source_release(void*,void*,vec v,float heading,std::int16_t level,int mode){release_velocity=v;release_heading=heading;release_level=level;release_mode=mode;order.push_back(6);}
void __fastcall reset_owner(void*,void*){}
void __fastcall object_tick(void*,void*,float,int){}
void __fastcall source_sound(void*,void*,void* state,void*,int initial){if(initial){++sounds;field<u32>(state,4)=88;}}
void __fastcall set_heading(void*,void*,float){}
const char* __fastcall message(void*,void*,int){return "cannot lift";}
const char* __cdecl immune_message(void*,int,void*){return "";}
void __fastcall notification(void*,void*,const u32*,const char*){order.push_back(7);}
u32 __fastcall ui_error(void*,void*,u32,const char*,float,std::array<float,4>,int){order.push_back(8);return 99;}
void* __fastcall get_event(void*,void*,int id){check(id==1,"entry reads suspend tag1 event");return event.data();}
void* __fastcall event_stats(void*,void*,void*){return stats.data();}
void* __cdecl dynamic_cast_event(void* p,int,void*,void*,int){return p;}
void __stdcall preentry(void*,void*){++preentries;}
void __cdecl auxiliary_effects(void* p,int initial){check(p==target.data()&&initial==1,"auxiliary effect uses selected physical object");++auxiliary_calls;}
void __fastcall auxiliary_event(void*,void*,void* victim,void* owner){check(victim==target.data()&&owner==self_actor(),"auxiliary event preserves victim/source arguments");++auxiliary_events;}
std::uint16_t __fastcall parse_value(void*,void*,const char* text,float* out,int){if(*text=='%')return 7;*out=std::strtof(text,nullptr);return 0;}
float __fastcall talent_value(void*,void*,void* supplied,u32* id,int,int){check(supplied==stats.data()&&*id==7,"native evaluator resolves authored talent reference against event stats");return 42.f;}
int __cdecl compare_key(const char* a,const char* b){return _stricmp(a,b);}
bool __fastcall base_parse(void*,void*,const char*,const char*){base_damage_parsed=true;return true;}
void* __fastcall event_constructor(void* p,void*){
 field<void*>(p,0x14)=attack.data();reinterpret_cast<void*(__thiscall*)(void*)>(0x4dc5d0)(attack.data());return p;
}
void __cdecl record_query(void* parameters){queried_impact=field<std::int16_t>(parameters,8);queried_heavy=field<std::int8_t>(parameters,0xa);queried_level=field<std::int8_t>(parameters,0xb);}
__declspec(naked) void pc_acquire(){__asm {
 push dword ptr [esp+12]
 call record_query
 add esp,4
 mov eax,[esp+4]
 mov edx,acquired_token
 mov [eax],edx
 mov eax,[esp+8]
 mov edx,auxiliary_token
 mov [eax],edx
 ret
}}
__declspec(naked) void source_acquire(){__asm {
 mov eax,[esp+4]
 mov edx,acquired_token
 mov [eax],edx
 mov eax,[esp+8]
 mov edx,auxiliary_token
 mov [eax],edx
 ret
}}
void* __fastcall query_ctor(void** p,void*,const void*,const u32*){*p=query.data();return p;}
void __fastcall query_dtor(void*,void*){++destructs;}
void __fastcall query_filter(void*,void*,int,int){}
void __cdecl query_team(void**,void*){}
void __fastcall query_pc(void*,void*,const vec*,const void*,float d,const vec* box){check(d==48&&box->x==16&&box->y==16&&box->z==16,"guard native 48/16 probe");++query_calls;}
void __fastcall query_source(void* a,void*,const vec* p,const void* f,float d,const vec* box,int flag,int axes){check(flag==1&&axes==7,"source query flag and axes");query_pc(a,nullptr,p,f,d,box);}
void** __fastcall query_contact(void*,void*,void** p){*p=target.data();return p;}
void* __fastcall contact_entity(void* ref,void*){return field<void*>(ref,0)==node.data()?node.data():target.data();}
bool __fastcall contact_level(void*,void*){return wall;}
vec normal{-1,0,0};
const vec* __fastcall contact_normal(void*,void*){return &normal;}
void __fastcall contact_reset(void** p,void*,int){*p=nullptr;}
bool __fastcall trace(void*,void*,const vec*,const vec*,int,vec* shift,void** p){*shift={2,0,0};*p=target.data();return wall;}
void __fastcall cone(void*,void*,const vec*,const void*,float,float,int){++query_calls;}
void __fastcall radius(void*,void*,const vec*,float,int,int){}
int __fastcall ordered_count(void*,void*){return 1;}
void* __fastcall ordered_at(void*,void*,int){return target.data();}
void* __cdecl order_query(void*,void*){return ordered.data();}
void __cdecl release_order(void*){}
bool los_blocked=false;int los_calls=0;
bool __cdecl line_trace(u32,const vec*,const vec*,const vec*,void** contact){++los_calls;if(los_blocked)*contact=node.data();return los_blocked;}
vec* __fastcall vector_zero(vec* p,void*){*p={};return p;}
vec* __fastcall vector_copy(vec* p,void*,const vec* v){*p=*v;return p;}
vec* __fastcall vector_construct(vec* p,void*,float x,float y,float z){*p={x,y,z};return p;}
float __fastcall component(const vec* p,void*,int i){return reinterpret_cast<const float*>(p)[i];}
float* __fastcall component_ref(vec* p,void*,int i){return reinterpret_cast<float*>(p)+i;}
float __fastcall length_squared(const vec* p,void*){return p->x*p->x+p->y*p->y+p->z*p->z;}
void* __fastcall find_talent(void*,void*,int* id){field<int>(node.data(),0x100)=*id;return node.data();}
const char* __fastcall talent_name(void*,void*,const char*){return field<int>(node.data(),0x100)==1?"acrobatics":"might";}
unsigned char __fastcall effective_might(void*,void*){return static_cast<unsigned char>(effective_might_override<0?might:effective_might_override);}
void* __fastcall source_stats(void* p,void*){return p;}
unsigned char __fastcall source_rank(void*,void*,const void*){return static_cast<unsigned char>(acrobatics);}
// Source's inline acrobatics lookup, ending in a low-nibble extraction.
u32 __fastcall source_talent_index(void*,void*,const void*,u32){return 0;}
void* __fastcall source_talent_stats(void*,void*){return stats.data();}
unsigned char __fastcall source_might(void*,void*,int,void* context_actor,void*){return context_actor?effective_might(nullptr,nullptr):static_cast<unsigned char>(might);}
void reset(){
 lift_rule::source_override=-1;
 stale_resolution=false;
 actor.fill(0);source_actor.fill(0);target.fill(0);stats.fill(0);input.fill(0);context.fill(0);node.fill(0);event.fill(0);attack.fill(0);type.fill(0);move.fill(0);query.fill(0);audio.fill(0);manager.fill(0);field<unsigned char>(audio.data(),0xd0ae)=1;
 actor_vt.fill(nullptr);target_vt.fill(nullptr);node_vt.fill(nullptr);move_vt.fill(nullptr);query_vt.fill(nullptr);audio_vt.fill(nullptr);manager_vt.fill(nullptr);
 for(auto* a:{actor.data(),source_actor.data()}){field<void*>(a,0)=actor_vt.data();field<u32>(a,0x1c)=111;field<void*>(a,0x35c)=field<void*>(a,0x2d8)=stats.data();field<void*>(a,0x378)=field<void*>(a,0x2f4)=move.data();field<void*>(a,0x3c0)=field<void*>(a,0x328)=node.data();}
 field<void*>(target.data(),0)=target_vt.data();field<u32>(target.data(),0x1c)=222;field<float>(target.data(),0x240)=field<float>(target.data(),0x27c)=100;field<void*>(target.data(),0x35c)=field<void*>(target.data(),0x2d8)=stats.data();
 field<void*>(node.data(),0)=node_vt.data();field<void*>(node.data(),8)=node.data();field<void*>(move.data(),0)=move_vt.data();field<void*>(query.data(),0)=query_vt.data();field<void*>(audio.data(),0)=audio_vt.data();field<void*>(manager.data(),0)=manager_vt.data();field<void*>(context.data(),4)=node.data();
 field<void*>(event.data(),0)=event_vt.data();event_vt[8/4]=reinterpret_cast<void*>(&auxiliary_event);
 actor_vt[0]=target_vt[0]=reinterpret_cast<void*>(&get_type);actor_vt[0x24/4]=target_vt[0x24/4]=reinterpret_cast<void*>(&position);actor_vt[0x1c/4]=reinterpret_cast<void*>(&set_position);actor_vt[0x2c/4]=target_vt[0x2c/4]=reinterpret_cast<void*>(&set_angles);
 actor_vt[0x124/4]=actor_vt[0x130/4]=reinterpret_cast<void*>(&pose);
 move_vt[0x28/4]=move_vt[0x30/4]=reinterpret_cast<void*>(&handler_of);
 target_vt[0x84/4]=target_vt[0x8c/4]=reinterpret_cast<void*>(&one_shot);
 query_vt[0x40/4]=reinterpret_cast<void*>(&query_filter);query_vt[0x70/4]=reinterpret_cast<void*>(source?&query_source:reinterpret_cast<decltype(&query_source)>(&query_pc));query_vt[0x20/4]=reinterpret_cast<void*>(&query_contact);
 audio_vt[0x34/4]=source?reinterpret_cast<void*>(&sound_resource):reinterpret_cast<void*>(0x590120);audio_vt[0x38/4]=reinterpret_cast<void*>(&sound_resource);audio_vt[0x4c/4]=reinterpret_cast<void*>(&start_sound);audio_vt[0x60/4]=reinterpret_cast<void*>(&sound_valid);audio_vt[0x70/4]=reinterpret_cast<void*>(&sound_position);audio_vt[0x74/4]=reinterpret_cast<void*>(&stop_sound);
 manager_vt[0x104/4]=manager_vt[0x160/4]=reinterpret_cast<void*>(&sim_time);manager_vt[0x148/4]=manager_vt[0x1a8/4]=reinterpret_cast<void*>(&not_busy);manager_vt[0x54/4]=manager_vt[0x5c/4]=reinterpret_cast<void*>(&friends);manager_vt[0x34/4]=manager_vt[0x3c/4]=reinterpret_cast<void*>(&not_busy);manager_vt[0x15c/4]=manager_vt[0x178/4]=reinterpret_cast<void*>(&camera_visible);manager_vt[0x1c/4]=manager_vt[0x20/4]=reinterpret_cast<void*>(&effect_serial);
 field<u32>(type.data(),0x18)=2;field<int>(stats.data(),0xbc)=2;field<std::uint16_t>(stats.data(),0x20)=1;field<std::uint16_t>(stats.data(),0x28)=2;field<unsigned char>(stats.data(),0x22)=static_cast<unsigned char>(acrobatics);field<unsigned char>(stats.data(),0x2a)=static_cast<unsigned char>(might);field<unsigned char>(stats.data(),0x198)=static_cast<unsigned char>(acrobatics);
 node_vt[0x14/4]=reinterpret_cast<void*>(&find_talent);manager_vt[0x14/4]=reinterpret_cast<void*>(&find_talent);node_vt[0x1c/4]=reinterpret_cast<void*>(&get_event);node_vt[0x28/4]=reinterpret_cast<void*>(&event_stats);
 field<void*>(ui.data(),0)=ui_vt.data();ui_vt[8/4]=reinterpret_cast<void*>(&message);ui_vt[0x24/4]=ui_vt[0x34/4]=reinterpret_cast<void*>(&ui_error);ui_vt[0x2c/4]=ui_vt[0x40/4]=reinterpret_cast<void*>(&notification);
 field<u32>(at(bias+0x4c0a60),0)=0;field<u32>(at(bias+0x48338c),0)=1;field<u32>(at(bias+0x485878),0)=2;
 *at(0x70b6fd)=*at(bias+0x485840)=0;
 token_valid=target_alive=can_lift=target_ok=can_grab=true;target_actor=true;wall=friend_damage=session_busy=fallback_result=immune=false;normal={-1,0,0};
 acquired_token=222;auxiliary_token=0;auxiliary_calls=auxiliary_events=0;preentries=0;query_calls=destructs=starts=sounds=stops=debits=publications=0;action_id=-1;last_velocity=release_velocity=placed=turned={};release_heading=0;release_level=release_mode=0;last_cost=0;selected_name.clear();played_name.clear();order.clear();tk_guard_bridges::tk_states.clear();tk_guard_bridges::tk_attempts.clear();
}
void bind(){
 jump(0x4654b0,&resolve);jump(bias+0x6bf80,&resolve);jump(0x4654d0,&valid);jump(bias+0x6bfa0,&valid);
 jump(0x402b60,&character);jump(bias+0x27620,&character);jump(0x429110,&physical);jump(bias+0x275e0,&physical);jump(0x41e4d0,&no_cast);jump(bias+0x2f590,&no_cast);jump(0x42be20,&no_cast);jump(bias+0x3ae80,&no_cast);jump(0x41e510,&no_cast);jump(bias+0x29210,&no_cast);jump(0x41b0e0,&no_cast);jump(bias+0x27ba0,&no_cast);
 jump(0x45fe40,&publish);jump(bias+0x66a90,&publish);jump(0x45fe60,&clock_value);jump(bias+0x66ab0,&clock_value);
 for(u32 a:{0x46dce0u,0x48de40u,0x484990u,0x4457e0u,0x4c05b0u,0x450030u,0x59ee20u,0x4bdb30u})jump(a,&get_manager);
 for(u32 a:{0x72cf0u,0x8a600u,0x80c80u,0x51100u,0x59ce0u,0x158860u,0xb3470u})jump(bias+a,&get_manager);
 jump(0x592480,&get_audio);jump(bias+0x151ef0,&get_audio);
 jump(0x41d5d0,&get_energy);jump(bias+0x2e8c0,&get_energy);jump(0x4318d0,&spend);jump(bias+0x3f410,&spend);
 jump(0x4c87c0,&no_mode);jump(bias+0xbf750,&no_mode);
 jump(0x41a220,&team);jump(bias+0x26e60,&team);jump(0x429228,&grabbable_tail);jump(bias+0x391a0,&grab_ok);jump(0x41c540,&yes);jump(0x427e00,&target_allowed);jump(bias+0x38340,&target_allowed);
 jump(bias+0x29080,&yes);jump(bias+0x27410,&script_allowed);jump(0x41a820,&script_allowed);
 jump(0x427f60,&liftable);jump(bias+0x38400,&liftable);jump(0x4fbe80,&fallback);jump(bias+0xe6da0,&fallback);jump(0x54d710,&immunity);jump(bias+0x2b250,&immunity);
 jump(0x501af0,&entry_action);jump(0x427c80,&source_action);jump(bias+0x381f0,&source_action);jump(0x41d0d0,&get_style);jump(0x501a50,&named_lookup);jump(0x41b120,&make_name);jump(bias+0x2e290,&get_style);jump(bias+0xed6e0,&source_named);
 jump(0x42c340,&play);jump(bias+0x3b100,&play);jump(0x42d8e0,&lock);jump(bias+0x3cdf0,&lock);jump(0x473640,&velocity);jump(bias+0x760d0,&velocity);
 jump(0x560990,&facing);jump(bias+0x1209c0,&facing);jump(0x5607b0,&angles);jump(bias+0x120860,&angles);
 jump(bias+0xe6f40,&input_held);jump(bias+0x129c0,&normalize);jump(bias+0x199ba0,&vector_length);jump(bias+0x1960d0,&vector_sub);jump(bias+0x196100,&vector_add);jump(bias+0x13c60,&vector_difference);jump(bias+0x341916,&modulus);jump(bias+0x1a41f0,&truncate);
 jump(bias+0xdf4a0,&source_sound);jump(bias+0x34740,&set_heading);
 jump(0x421080,&reset_owner);jump(bias+0x31a00,&reset_owner);jump(bias+0x94a70,&immune_message);jump(bias+0x120720,&yaw);
 jump(0x41a2e0,&physical_mode);jump(0x471a20,&physical_cleanup);jump(0x42e570,&pc_release);jump(bias+0x3db50,&source_release);jump(0x475d10,&object_tick);jump(bias+0x793f0,&object_tick);
 jump(0x4ab6d0,&query_ctor);jump(bias+0xa2a70,&query_ctor);jump(0x4ab720,&query_dtor);jump(bias+0xa2ac0,&query_dtor);jump(0x44fe10,&query_team);jump(bias+0x59960,&query_team);
 jump(0x4a9590,&contact_entity);jump(bias+0xa0780,&contact_entity);jump(0x4a95c0,&contact_level);jump(bias+0xa07b0,&contact_level);jump(0x4a9600,&contact_normal);jump(bias+0xa07f0,&contact_normal);jump(0x4a9570,&contact_reset);jump(bias+0xa0770,&contact_reset);jump(0x42b6c0,&trace);jump(bias+0x3c720,&trace);
 jump(0x5649c0,&talent_name);jump(0x427dc0,&effective_might);jump(bias+0x801f0,&source_talent_stats);jump(bias+0xb3be0,&source_talent_index);jump(bias+0x40db0,&source_rank);jump(bias+0xb1ba0,&source_might);
 jump(bias+0x27ef0,&make_name);
 for(u32 a:{0x59ee20u,0x4bdb30u,bias+0x158860u,bias+0xb3470u})jump(a,&get_ui);
 jump(bias+0x293f0,&token_character);jump(bias+0x2b810,&token_physical);jump(0x4297b0,&token_physical);jump(0x4f2ed0,&pc_acquire);jump(bias+0xdf740,&source_acquire);jump(0x67234e,&dynamic_cast_event);jump(0x4f2b00,&preentry);jump(0x4f29a0,&auxiliary_effects);jump(bias+0xdf310,&auxiliary_effects);
}
void load_source(const image& x1,u32 va,std::size_t size){
 x1.load(va,size,bias);
 // Only absolute data references from the executed source callbacks.
 const u32 data[]{0x4c0a60,0x48338c,0x485878,0x498d90,0x485840,0x3c6de4,0x3c6e40,0x3c6de0,0x3d6610,0x3c97d0,0x3d65c0,0x3d65d0,0x3c7084,0x3cdaec,0x3cb240,0x3cb69c,0x3c8310,0x3c832c,0x3c93c0,0x3c8c88,0x5bc9d0,0x5bc9d4,0x5bc9d8,0x3c8c30,0x3c9784,0x4f314c,0x3d63c4,0x3d63b8,0x3cd144};
 for(std::size_t i=0;i+4<=size;++i){u32 v=field<u32>(at(va+bias),static_cast<unsigned>(i));if(std::find(std::begin(data),std::end(data),v)!=std::end(data)){field<u32>(at(va+bias),static_cast<unsigned>(i))=v+bias;i+=3;}}
 for(u32 v:data)if(x1.read(v,8))x1.load(v,(v==0x3d63c4||v==0x3d63b8)?16:8,bias);
}
struct cancel_result{vec v;int level;float heading;int mode,stops,published;u32 flags;};
cancel_result cancel_case(bool original,int level,bool exists){
 source=original;reset();bind();token_valid=exists;stale_resolution=!exists;
 field<vec>(target.data(),0x100)=field<vec>(target.data(),0x120)={10,-20,30};field<u32>(target.data(),4)=0x84;
 if(original){field<u32>(source_actor.data(),0x554)=222;field<std::int8_t>(source_actor.data(),0x53c)=static_cast<std::int8_t>(level);field<float>(source_actor.data(),0x4d4)=1.25f;field<u32>(source_actor.data(),0x4b0)=88;
  reinterpret_cast<void(__thiscall*)(void*,void*)>(bias+0xdf640)(nullptr,source_actor.data());
 }else{field<u32>(actor.data(),0x76c)=222;field<unsigned char>(actor.data(),0x259)=2;
  tk_guard_bridges::tk_state s;s.victim=333;s.level=static_cast<std::int8_t>(level);s.heading=1.25f;s.sound=88;tk_guard_bridges::tk_states[111]=s;
  tk_guard_bridges::tk_cancel(nullptr,nullptr,actor.data());check(tk_guard_bridges::tk_states.empty(),"cancel erases complete token sidecar");check(!(field<unsigned char>(actor.data(),0x259)&2),"cancel clears active flag");
 }
 return {release_velocity,release_level,release_heading,release_mode,stops,publications,field<u32>(target.data(),4)};
}
void cancel_tests(){for(int level:{-128,-1,0,1,11,127})for(bool exists:{false,true}){
 auto a=cancel_case(true,level,exists),b=cancel_case(false,level,exists);
 check(!std::memcmp(&a.v,&b.v,sizeof(vec))&&a.level==b.level&&a.heading==b.heading&&a.mode==b.mode&&a.stops==b.stops&&a.published==b.published&&a.flags==b.flags,"cancel agrees with owner XML1 velocity, signed level, flags, release and sound");
}}
void entry_tests(const image& pc){
 pc.load(0x4abf70,0x50);pc.load(0x4dad20,0x4d);
 for(int level:{-128,-1,1,11,127}){
  source=true;reset();bind();field<vec>(target.data(),0x20)={20,0,10};field<float>(target.data(),0x34)=1.25f;
  field<std::int8_t>(source_actor.data(),0x53c)=static_cast<std::int8_t>(level);
  reinterpret_cast<void(__thiscall*)(void*,void*,void*,void*)>(bias+0xdfc80)(nullptr,source_actor.data(),input.data(),context.data());
  const vec source_offset=field<vec>(source_actor.data(),0x4bc),source_last=field<vec>(source_actor.data(),0x4c8);const float source_distance=field<float>(source_actor.data(),0x4b4);const int old_sounds=sounds,old_starts=starts,old_publish=publications;const u32 old_owner=field<u32>(target.data(),0x158);
  source=false;reset();bind();field<vec>(target.data(),0x20)={20,0,10};field<float>(target.data(),0x34)=1.25f;
  field<void*>(event.data(),0x14)=attack.data();field<std::int8_t>(attack.data(),0x1c)=17;field<std::int16_t>(event.data(),0x18)=19;field<std::int16_t>(event.data(),0x1a)=2;field<std::int16_t>(event.data(),0x1c)=static_cast<unsigned char>(level);
  tk_guard_bridges::tk_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
  check(tk_guard_bridges::tk_states.count(111)==1,"entry owns a full-token sidecar");if(!tk_guard_bridges::tk_states.count(111))continue;
  const auto s=tk_guard_bridges::tk_states[111];
  check(s.level==level&&s.damage==17&&s.impact==19&&s.heaviness==2&&queried_level==level&&queried_heavy==2&&queried_impact==19,"native word/byte evaluators preserve separate signed TK metadata");
  check(!std::memcmp(&s.offset,&source_offset,sizeof(vec))&&!std::memcmp(&s.last,&source_last,sizeof(vec))&&s.distance==source_distance&&s.heading==1.25f,"entry distance, lifted offset, last position and heading agree with owner XML1");
  check(sounds==old_sounds&&starts==old_starts&&publications==old_publish&&field<u32>(target.data(),0x178)==old_owner&&played_name=="psycarry","entry shares source psycarry, sound, flags and physical owner lifecycle");
  check(preentries==1&&(field<unsigned char>(actor.data(),0x259)&2),"PC suspend dispatch happens once before acquisition");
  tk_guard_bridges::tk_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
  check(preentries==1&&tk_guard_bridges::tk_states[111].offset.z==s.offset.z&&tk_guard_bridges::tk_states[111].level==s.level,"same-family hold entry retains metadata and source state without a second trigger");
 }
 for(bool original:{true,false}){
  source=original;reset();bind();acquired_token=0;auxiliary_token=222;target_team=owner_team;
  field<void*>(event.data(),0x14)=attack.data();
  __asm {
   mov audit_before,esp
   mov audit_before[4],ebx
   mov audit_before[8],esi
   mov audit_before[12],edi
  }
  if(original)reinterpret_cast<void(__thiscall*)(void*,void*,void*,void*)>(bias+0xdfc80)(nullptr,source_actor.data(),input.data(),context.data());
  else tk_guard_bridges::tk_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
  __asm {
   mov audit_after,esp
   mov audit_after[4],ebx
   mov audit_after[8],esi
   mov audit_after[12],edi
  }
  check(std::equal(std::begin(audit_before),std::end(audit_before),std::begin(audit_after)),"auxiliary entry preserves stack and every native callee-saved register");
  check(auxiliary_calls==1&&auxiliary_events==1,"auxiliary acquisition dispatch agrees with source even on same-team objects");
 }
}
void retail_audio_contract_tests(const image& pc){
 // Execute the real disabled-audio paths through both retail vtable entries.
 // Only the external stack-cookie verifier is stubbed. Returning via ret4 at
 // slot34 instead of ret12 at slot38 corrupts this caller and fails the test.
 pc.load(0x590120,0x66);pc.load(0x590bd0,0x223);at(0x672161)[0]=0xc3;
 pc.load(0x69d058,4);
 source=false;reset();bind();audio_vt[0x38/4]=reinterpret_cast<void*>(0x590bd0);
 field<void*>(event.data(),0x14)=attack.data();
 field<std::int16_t>(event.data(),0x18)=19;field<std::int16_t>(event.data(),0x1a)=2;field<std::int16_t>(event.data(),0x1c)=11;
 unsigned before=0,after=0;__asm mov before,esp
 tk_guard_bridges::tk_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
 __asm mov after,esp
 check(before==after&&sounds==1&&tk_guard_bridges::tk_states.count(111)==1,"TK entry survives the real retail audio lookup ABI");
 auto state=tk_guard_bridges::tk_states[111];
 __asm mov before,esp
 tk_guard_bridges::release(actor.data(),state,{1,2,3},true);
 __asm mov after,esp
 check(before==after&&stops==1&&state.sound==0xffffffffu&&release_mode==1,"TK throw/release survives the real retail audio lookup ABI and stops its loop");
}

// Execute the retail release's descriptor stores, including its new PC-only
// overwrite flag. Computed L2 must replace the earlier suspend amount.
u32 arm_site=0x42e914;
std::array<unsigned char,16> l2_values{};std::array<void*,4> l2_vt{};
void* __cdecl l2_manager(){return l2_values.data();}
float __fastcall l2_value(void*,void*,int id,int level){check(id==1&&level==0,"native release evaluates L2 once");return 28.f;}
void* __cdecl release_thrower(u32 id){return id==111?actor.data():nullptr;}
float __fastcall l2_scale(void* owner,void*,int id,void* who,int mode){check(owner==stats.data()&&who==actor.data()&&id==0&&mode==0,"native L2 release multiplier evaluates caster stats");return 1.5f;}
__declspec(naked) void native_arm(void*,void*,int){__asm {
 push ebx
 push ebp
 push esi
 push edi
 mov esi,[esp+20]
 mov ebp,[esp+24]
 mov eax,[esp+28]
 sub esp,084h
 mov [esp+080h],eax
 mov dword ptr [esp+074h],042280000h
 mov dword ptr [esp+078h],2
 fld arm_speed
 fstp dword ptr [esp+010h]
 mov ebx,1
 jmp dword ptr [arm_site]
}}
__declspec(naked) void arm_done(){__asm {
 add esp,084h
 pop edi
 pop esi
 pop ebp
 pop ebx
 ret
}}
void release_damage_test(const image& pc){
 source=false;reset();bind();pc.load(0x42e914,0xff);jump(0x42ea13,&arm_done);jump(0x67217c,&truncate);
 for(u32 a:{0x68012cu,0x680194u,0x683f30u})pc.load(a,4);
 shared_handler_c_rules::targets gate_targets{};
 gate_targets.tk_release_arm=&tk_guard_bridges::release_arming_gate;
 for(const auto& patch:shared_handler_c_rules::patches(shared_handler_c_rules::telekinesis,gate_targets))if(patch.address==0x42e923)std::copy(patch.after.begin(),patch.after.end(),at(patch.address));
 field<void*>(l2_values.data(),0)=l2_vt.data();l2_vt[3]=reinterpret_cast<void*>(&l2_value);
 auto bind_release=[&](){jump(0x4c4bb0,&l2_manager);jump(0x41fb10,&release_thrower);jump(0x4b7970,&l2_scale);time_now=10.f;friend_damage=false;};
 for(bool moving:{false,true})for(bool object:{false,true}){
  reset();bind();bind_release();target_actor=!object;retail_arm=true;
  tk_guard_bridges::tk_state state;state.victim=222;state.impact=19;state.damage=2;
  unsigned before=0,after=0;__asm mov before,esp
  tk_guard_bridges::release(actor.data(),state,moving?vec{1,0,0}:vec{},true);
  __asm mov after,esp
  retail_arm=false;
  check(before==after&&field<float>(target.data(),0x2b8)==42.f,"real release stores select native XML1 L2 throw damage for still/moving actors/objects");
  check(field<float>(target.data(),0x2f0)==12.f&&field<u32>(target.data(),0x2b4)==111&&(field<unsigned char>(target.data(),0x2f8)&1),"real release arms deadline, attribution and active bit");
 }
 for(int mode:{0,1,2})for(float speed:{0.f,1.f,1.0001f,500.f}){
  reset();bind();bind_release();field<float>(target.data(),0x2b8)=19.f;field<float>(target.data(),0x2f0)=0.f;arm_speed=speed;
  unsigned before=0,after=0;__asm mov before,esp
  native_arm(target.data(),actor.data(),mode);
  __asm mov after,esp
  const bool armed=mode==2||speed>1.f;
  check(before==after&&field<float>(target.data(),0x2f0)==(armed?12.f:0.f),"real speed gate retains modes 0/1 and always admits private TK mode 2");
  check(field<float>(target.data(),0x2b8)==(armed&&mode!=0?42.f:19.f),"native overwrite selector retains ordinary preserve/replace behavior");
 }
}
void guard_entry_timing_test(){
 source=false;reset();bind();field<void*>(context.data(),4)=move.data();field<unsigned char>(target.data(),0x765)=1;
 tk_guard_bridges::guard_entry(nullptr,nullptr,actor.data(),nullptr,context.data());
 check(query_calls==1&&starts==1&&action_id==25,"Guard admission with no input object still makes its contextual stunned-target decision");
 check(field<unsigned char>(context.data(),0x6c)&4,"null-input Guard admission also aborts its parent transition");
 for(int duration:{300,500,800})for(bool pad:{false,true}){
  pad_input_rules::synthetic_pad controller;test_input_rules::synthetic_keys keyboard;
  controller.press(XINPUT_GAMEPAD_X,duration);keyboard.press(0x12,duration); // DirectInput E
  auto raw_held=[&](int ms){
   if(pad){xinput_pad::raw_state state{};std::vector<std::string> expired;controller.merge(state,ms,expired);return (state.buttons&XINPUT_GAMEPAD_X)!=0;}
   std::array<unsigned char,256> state{};std::vector<unsigned char> expired;keyboard.merge(state.data(),ms,expired);return (state[0x12]&0x80)!=0;
  };
  source=false;reset();bind();field<unsigned char>(target.data(),0x765)=1;check(raw_held(0),"raw pad-X/keyboard-E hold begins without a wake key");field<u32>(input.data(),0)=field<u32>(input.data(),4)=0x100;
  field<void*>(context.data(),4)=move.data();
  tk_guard_bridges::guard_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
  check(query_calls==1&&starts==1&&action_id==25,"human keyboard/pad Guard entry selects and dispatches the contextual stunned grab before a later input tick");
  check((field<unsigned char>(context.data(),0x6c)&4)!=0,"entry decision aborts parent transition after exactly one dispatch");
  // Neither the held-only frames nor the release introduces another entry.
  for(int ms=16;ms<duration;ms+=16){check(raw_held(ms),"raw injected Guard stays held for the human-length duration");field<u32>(input.data(),4)=0;}
  check(!raw_held(duration),"raw pad and keyboard Guard release at the requested duration");
  field<u32>(input.data(),0)=0;
  check(query_calls==1&&starts==1,"300-800 ms Guard holds cannot loop the entry decision");
 }
}

void round3_guard_and_tk_test(){
 // 082c admitted the ordinary Guard chain for a target which was merely
 // grabbable. That service is not a stun predicate: exercise both flags.
 for(bool stunned:{false,true})for(bool entry:{false,true}){
  source=false;reset();bind();field<unsigned char>(target.data(),0x765)=stunned?1:0;
  field<void*>(context.data(),4)=move.data();
  if(entry)tk_guard_bridges::guard_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
  else tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data());
  check(action_id==(stunned?25:0),"round3 untouched enemy never selects grab; stunned enemy retains pickup");
  if(!stunned)check(field<u32>(actor.data(),0x76c)==0,"round3 untouched enemy never becomes held target");
 }
 // A two-second hold must survive a transient acquisition miss at entry.
 // Native admission policies stay in acquire/filter; no extra tag/debit.
 for(int trial=0;trial<10;++trial){
  source=false;reset();bind();field<u32>(input.data(),0)=16;
  field<void*>(event.data(),0x14)=attack.data();field<std::int16_t>(event.data(),0x18)=19;
  field<std::int16_t>(event.data(),0x1a)=2;field<std::int16_t>(event.data(),0x1c)=11;
  acquired_token=trial%3==0?0u:222u;
  tk_guard_bridges::tk_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
  if(!acquired_token){
   check(!tk_guard_bridges::tk_input(nullptr,nullptr,actor.data(),input.data())&&action_id==-1,"round3 held TK miss retains acquisition window");
   acquired_token=222;
   tk_guard_bridges::tk_input(nullptr,nullptr,actor.data(),input.data());
  }
  check(tk_guard_bridges::tk_states.count(111)==1,"round3 ten valid held targets acquire, including four initial misses");
  check(preentries==1,"round3 TK retry never repeats preentry events");
  tk_guard_bridges::tk_cancel(nullptr,nullptr,actor.data());
 }
 for(bool cancelled:{false,true}){
  source=false;reset();bind();acquired_token=0;auxiliary_token=222;
  field<u32>(input.data(),0)=16;field<void*>(event.data(),0x14)=attack.data();
  tk_guard_bridges::tk_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
  for(int frame=0;frame<120;++frame)tk_guard_bridges::tk_input(nullptr,nullptr,actor.data(),input.data());
  check(auxiliary_calls==1&&auxiliary_events==1&&preentries==1,"round3 two-second failed TK hold dispatches auxiliary and entry events once");
  if(cancelled)tk_guard_bridges::tk_cancel(nullptr,nullptr,actor.data());
  else{field<u32>(input.data(),0)=0;tk_guard_bridges::tk_input(nullptr,nullptr,actor.data(),input.data());}
  acquired_token=222;field<u32>(input.data(),0)=16;
  tk_guard_bridges::tk_input(nullptr,nullptr,actor.data(),input.data());
  check(tk_guard_bridges::tk_states.empty()&&tk_guard_bridges::tk_attempts.empty(),"round3 release or interruption ends retry and cannot acquire a later target");
 }
}

struct live_ref {u32 handle;void* pool;};
std::array<unsigned char,0x80> live_stun_factory{},live_attach_pool{},live_definition_pool{},live_attachment{};
std::array<void*,32> live_factory_vt{},live_attach_pool_vt{},live_attached_vt{};
bool __fastcall live_attach_valid(void*,void*,u32 token){return token==0x1234;}
void* __fastcall live_attach_get(void*,void*,u32 token){return token==0x1234?live_attachment.data():nullptr;}
live_ref* __fastcall live_stun_named(void*,void*,live_ref* out,const char* name){check(std::strcmp(name,"shared_stunned")==0,"guard queries the actual shared_stunned definition");*out={0x50,live_definition_pool.data()};return out;}
void* __cdecl live_stun_manager(){return live_stun_factory.data();}
bool __fastcall live_status(void*,void*,int tag){check(tag==4,"native grab gate keeps its unrelated status exclusion");return false;}
void live_stun_guard_tests(const image& pc){
 // live-082d records idle+visible stars, but not raw reaction/attachment state.
 // Pin the native-supported active-timed-stun/reaction-clear combination.
 pc.load(0x538140,0x12);pc.load(0x5381c0,0x12);
 field<void*>(live_stun_factory.data(),0)=live_factory_vt.data();live_factory_vt[0x58/4]=reinterpret_cast<void*>(&live_stun_named);
 field<void*>(live_attach_pool.data(),0)=live_attach_pool_vt.data();live_attach_pool_vt[0]=reinterpret_cast<void*>(&live_attach_valid);live_attach_pool_vt[2]=reinterpret_cast<void*>(&live_attach_get);
 live_attached_vt[0x4c/4]=at(0x538140);live_attached_vt[0x64/4]=at(0x5381c0);
 for(int state:{0,1,2,3,4})for(bool entry:{false,true}){
  source=false;acrobatics=0;might=0;owner_team=1;target_team=2;reset();bind();jump(0x543f80,&live_stun_manager);live_attachment.fill(0);field<void*>(live_attachment.data(),0)=live_attached_vt.data();
  field<live_ref>(target.data(),0x21c)={state==3?0x2234u:0x1234u,state==0?nullptr:live_attach_pool.data()};
  field<live_ref>(live_attachment.data(),0x20)={state==2?0x51u:0x50u,live_definition_pool.data()};
  if(state==1||state==4){pc.load(0x429210,0x109);pc.load(0x42b840,0x25);pc.load(0x427de0,0x1e);jump(0x41a160,&live_status);if(state==4)field<float>(target.data(),0x2f0)=time_now+1.f;}
  field<void*>(context.data(),4)=move.data();
  if(entry)tk_guard_bridges::guard_entry(nullptr,nullptr,actor.data(),input.data(),context.data());else tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data());
  check(action_id==((state==1||state==4)?25:0),"active timed stun admits pickup, including native recent-hit deadline; fresh, unrelated and stale attachments do not");
 }
 for(bool blocked:{false,true}){
  source=false;acrobatics=0;might=1;owner_team=target_team=1;effective_might_override=1;reset();bind();jump(0x543f80,&live_stun_manager);
  field<unsigned char>(stats.data(),0x2ae)=0x20;field<unsigned char>(stats.data(),0x2ad)=0x80;
  field<live_ref>(target.data(),0x21c)={0x1234,live_attach_pool.data()};field<live_ref>(live_attachment.data(),0x20)={0x50,live_definition_pool.data()};
  pc.load(0x429210,0x109);pc.load(0x42b840,0x25);pc.load(0x427de0,0x1e);jump(0x41a160,&live_status);field<float>(target.data(),0x2f0)=time_now+1.f;
  field<unsigned char>(target.data(),0x579)=blocked?0x40:0;
  tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data());
  check(blocked?played_name.empty():played_name=="throwally","timed-stun ally follows flagged-stun Fastball eligibility and still honors native status exclusion");
 }
 effective_might_override=-1;owner_team=1;target_team=2;
 for(bool reaction:{false,true}){
  source=false;acrobatics=0;might=1;owner_team=target_team=1;effective_might_override=1;reset();bind();jump(0x543f80,&live_stun_manager);
  field<unsigned char>(stats.data(),0x2ae)=0x20;field<unsigned char>(stats.data(),0x2ad)=0x80;
  field<u32>(target.data(),4)=1u<<20;field<int>(target.data(),0x3d4)=0;field<unsigned char>(target.data(),0x765)=reaction?1:0;
  field<live_ref>(target.data(),0x21c)={0x1234,live_attach_pool.data()};field<live_ref>(live_attachment.data(),0x20)={0x50,live_definition_pool.data()};
  pc.load(0x429210,0x109);pc.load(0x42b840,0x25);pc.load(0x427de0,0x1e);jump(0x41a160,&live_status);field<float>(target.data(),0x2f0)=time_now+1.f;
  tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data());
  check(reaction?played_name=="throwally":played_name.empty(),"party pending timed stun cannot undo rule18 reaction ownership; actual flagged stun retains Fastball");
 }
 effective_might_override=-1;owner_team=1;target_team=2;
}
void* __fastcall source_value_name(u32* key,void*,const char* text){check(std::strcmp(text,"L2")==0,"source throw requests the literal L2 value");*key=1;return key;}
int __cdecl source_value_digit(int c){return c>='0'&&c<='9';}
int __fastcall source_value_find(void*,void*,const u32*,u32){return 0;}
bool __fastcall source_throw_parse_base(void*,void*,const char*,const char*){return true;}
bool __fastcall pc_throw_parse_base(void*,void*,const char*,const char*){return false;}
double __cdecl throw_parse_number(const char* text){return std::strtod(text,nullptr);}
void live_tk_zero_selection_test(const image& pc){
 source=false;acrobatics=0;might=0;owner_team=1;target_team=2;reset();bind();target_actor=true;field<vec>(target.data(),0x20)={149,0,4};
 field<unsigned char>(target.data(),0x31d)=1;field<u32>(type.data(),0x18)=96;field<u32>(at(0x70b67c),0)=1;field<u32>(at(0x70b840),0)=2;
 query_vt[0x64/4]=reinterpret_cast<void*>(&cone);query_vt[0x68/4]=reinterpret_cast<void*>(&radius);
 field<void*>(ordered.data(),0)=ordered_vt.data();ordered_vt[0]=reinterpret_cast<void*>(&ordered_count);ordered_vt[1]=reinterpret_cast<void*>(&ordered_at);
 jump(0x4ab770,&order_query);jump(0x4ab7d0,&release_order);jump(0x4ab960,&line_trace);
 field<void*>(at(0x67fa60),0)=reinterpret_cast<void*>(&vector_zero);field<void*>(at(0x67fa2c),0)=field<void*>(at(0x67f9e0),0)=reinterpret_cast<void*>(&vector_copy);
 field<void*>(at(0x67f9c8),0)=reinterpret_cast<void*>(&vector_construct);field<void*>(at(0x67f9d8),0)=reinterpret_cast<void*>(&component);field<void*>(at(0x67f9c4),0)=reinterpret_cast<void*>(&component_ref);
 field<void*>(at(0x67fa58),0)=reinterpret_cast<void*>(&length_squared);field<void*>(at(0x67fa5c),0)=reinterpret_cast<void*>(&normalize);field<void*>(at(0x67fa0c),0)=reinterpret_cast<void*>(&vector_difference);
 for(u32 a:{0x682f88u,0x6872d0u,0x680030u})pc.load(a,4);
 pc.load(0x4f2ed0,0x539);jump(0x4f2da0,reinterpret_cast<void*>(&tk_guard_bridges::tk_filter));
 field<void*>(event.data(),0x14)=attack.data();field<std::int16_t>(event.data(),0x18)=19;field<std::int16_t>(event.data(),0x1a)=2;field<std::int16_t>(event.data(),0x1c)=11;
 field<u32>(input.data(),0)=16;field<u32>(node.data(),0x1c)=999;
 tk_guard_bridges::trace_acquisition=true;logger::captured.clear();los_blocked=true;los_calls=0;
 tk_guard_bridges::tk_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
 for(int frame=1;frame<128;++frame){time_now=10.f+frame/60.f;tk_guard_bridges::tk_input(nullptr,nullptr,actor.data(),input.data());}
 check(tk_guard_bridges::tk_states.empty()&&los_calls==128,"149-ahead valid enemy can remain unselected for128 native queries when first LOS contact is a different entity");
 check(std::any_of(logger::captured.begin(),logger::captured.end(),[](const std::string& line){return line.find("accepted=1")!=std::string::npos&&line.find("post_filter_miss=1")!=std::string::npos;}),"failed acquisition trace distinguishes a native post-filter miss from eligibility rejection");
 los_blocked=false;tk_guard_bridges::tk_input(nullptr,nullptr,actor.data(),input.data());
 check(tk_guard_bridges::tk_states.count(111)==1,"same held native query acquires the valid target once LOS clears");
 tk_guard_bridges::tk_cancel(nullptr,nullptr,actor.data());tk_guard_bridges::trace_acquisition=false;
}
void source_throw_value_test(const image& pc,const image& x1){
 reset();bind();load_source(x1,0xb7e60,0x99);
 jump(bias+0x13eb0,&source_value_name);jump(bias+0x1186a0,&source_value_find);
 jump(bias+0x341f4c,&source_value_digit);
 field<float>(manager.data(),0x998)=9.f;field<float>(manager.data(),0x99c)=11.f;
 auto source_value=reinterpret_cast<float(__thiscall*)(void*,const char*,float*)>(bias+0xb7e60);
 float maximum=0;
 check(source_value(manager.data(),"L2",nullptr)==9.f,"actual XML1 values getter returns min nine when throw omits range output");
 check(source_value(manager.data(),"L2",&maximum)==9.f&&maximum==11.f,"XML1 eleven is the optional maximum, not a random throw draw");
 u32 name=0;std::memcpy(&name,pc.read(0x6da24c,4),4);
 check(std::strcmp(reinterpret_cast<const char*>(pc.read(name,5)),"DMG3")==0,"PC numeric throw selector one names DMG3, not XML1 L2");
 pc.load(0x4c49f0,0x3b);field<float>(at(0x778ad0),0)=3.f;field<float>(at(0x778ad4),0)=5.f;
 auto native_value=reinterpret_cast<float(__thiscall*)(void*,int,float*)>(0x4c49f0);
 check(native_value(manager.data(),1,nullptr)==3.f,"actual PC getter reads the DMG3 minimum at numeric selector one");

 // The shipped source Smash authors velocityup, while both binaries parse
 // upvelocity. Preserve that source behavior, and test the real parsers so
 // a manually populated -200 field cannot masquerade as the authored node.
 pc.load(0x4db650,0x1d9);load_source(x1,0xce8f0,0x195);
 const u32 strings[]{0x3d07a4,0x3d5ad4,0x3d5b28,0x3d5b18,0x3d5b0c,0x3d5afc,0x3d5af0,0x3d5ae4,0x3d5a64};
 for(u32 value:strings){
  x1.load(value,40,bias);
  for(unsigned i=0;i+4<=0x195;++i)if(field<u32>(at(bias+0xce8f0),i)==value){field<u32>(at(bias+0xce8f0),i)=value+bias;i+=3;}
 }
 for(u32 value:{0x68a2d0u,0x690918u,0x69096cu,0x69095cu,0x690950u,0x690940u,0x690934u,0x690928u,0x690890u})pc.load(value,40);
 jump(0x672562,&compare_key);jump(bias+0x343612,&compare_key);
 jump(0x672210,&throw_parse_number);jump(bias+0x341736,&throw_parse_number);
 jump(0x4f8240,&pc_throw_parse_base);jump(bias+0xe3fb0,&source_throw_parse_base);
 for(bool original:{false,true})for(const char* key:{"upvelocity","velocityup","forwardvelocity"}){
  event.fill(0);auto parse=reinterpret_cast<bool(__thiscall*)(void*,const char*,const char*)>(original?bias+0xce8f0:0x4db650);
  parse(event.data(),key,std::strcmp(key,"forwardvelocity")==0?"600":"-200");
  check(field<float>(event.data(),0x14)==(std::strcmp(key,"upvelocity")==0?-200.f:0.f),"native/source authored Smash spelling retains zero upvelocity; explicit upvelocity parses");
  check(field<float>(event.data(),0x18)==(std::strcmp(key,"forwardvelocity")==0?600.f:0.f),"native/source Attack forwardvelocity parses to six hundred");
 }
}

void constructor_tests(const image& pc,const image& x1){
 source=false;reset();bind();pc.load(0x4dc5d0,0x6e);pc.load(0x4f8d30,0x38);load_source(x1,0xe46a0,0x24);
 jump(0x4dccf0,&event_constructor);jump(bias+0xcfbf0,&event_constructor);
 auto native=reinterpret_cast<void*(__thiscall*)(void*)>(0x4f8d30);
 check(native(event.data())==event.data()&&field<std::int16_t>(event.data(),0x18)==0,"owner native suspend constructor defaults ImpactDamage to zero");
 reinterpret_cast<void*(__thiscall*)(void*)>(bias+0xe46a0)(event.data());
 const int source_impact=field<std::int16_t>(event.data(),0x18),source_damage=field<std::int8_t>(event.data(),0x1b),source_level=field<std::int8_t>(event.data(),0x1c);
 for(const auto& p:shared_handler_c_rules::patches(shared_handler_c_rules::telekinesis,{}))if(p.address==0x4f8d60)std::copy(p.after.begin(),p.after.end(),at(p.address));
 check(native(event.data())==event.data()&&field<std::int16_t>(event.data(),0x18)==source_impact&&source_impact==5&&field<std::int8_t>(attack.data(),0x1c)==source_damage&&field<std::int8_t>(event.data(),0x1c)==source_level,"installed native constructor agrees with owner source defaults and object return");
}
void parser_tests(const image& pc){
 source=false;reset();bind();
 pc.load(0x4e39f0,0xc0);pc.load(0x4dad70,0x3f);pc.load(0x4dad20,0x4d);
 for(u32 a:{0x690fc4u,0x689dacu,0x690fb4u})pc.load(a,24);
 jump(0x672562,&compare_key);jump(0x67217c,&truncate);jump(0x4c1dd0,&get_manager);jump(0x4dc810,&base_parse);
 manager_vt[0x18/4]=reinterpret_cast<void*>(&parse_value);manager_vt[4/4]=reinterpret_cast<void*>(&talent_value);
 field<std::int16_t>(event.data(),0x18)=0;base_damage_parsed=false;
 auto native=reinterpret_cast<bool(__thiscall*)(void*,const char*,const char*)>(&tk_guard_bridges::suspend_parse_native);
 check(native(event.data(),"damage","%xph_telekinesis_dmg")&&base_damage_parsed&&field<std::int16_t>(event.data(),0x18)==0,"owner native parser leaves ImpactDamage zero for converted damage spelling");
 base_damage_parsed=false;jump(0x4e39f0,reinterpret_cast<void*>(&tk_guard_bridges::tk_parse));
 auto installed=reinterpret_cast<bool(__thiscall*)(void*,const char*,const char*)>(0x4e39f0);
 check(installed(event.data(),"damage","%xph_telekinesis_dmg")&&!base_damage_parsed&&field<std::uint16_t>(event.data(),0x18)==0x8007,"installed suspend alias keeps encoded native talent reference");
 check(reinterpret_cast<std::int16_t(__thiscall*)(void*,void*)>(0x4dad20)(event.data()+0x18,stats.data())==42,"converted Phoenix damage reaches evaluated suspend metadata");
 for(const char* key:{"ImpactDamage","DAMAGE"})check(installed(event.data(),key,"19")&&field<std::int16_t>(event.data(),0x18)==19,"native ImpactDamage and source damage use same word parser");
 check(installed(event.data(),"tklevel","11")&&field<unsigned char>(event.data(),0x1c)==11,"tklevel forwards to owner native parser");
 base_damage_parsed=false;check(installed(event.data(),"other-key","0")&&base_damage_parsed,"unrelated suspend attributes retain native parser fallback");
}
bool invoke_filter(u32 site,void* a,void* entity,void* parameters){
 bool result=false;u32 before=0,after=0;
 __asm {
  push ebx
  push edi
  mov before,esp
  mov ebx,a
  mov eax,entity
  mov edi,parameters
  call site
  mov result,al
  mov after,esp
  pop edi
  pop ebx
 }
 check(before==after,"candidate bridge preserves native stack depth");return result;
}
void filter_tests(){
 jump(0x4f2da0,reinterpret_cast<void*>(&tk_guard_bridges::tk_filter));
 for(int h:{0,1,2,3,4,5})for(int limit:{-1,0,1,2,3,4,5})for(int mode=0;mode<8;++mode){
  bool answer=false;
  for(bool original:{true,false}){
   source=original;reset();bind();target_actor=mode!=1;friend_damage=mode==2;target_team=mode==3?owner_team:2;can_grab=mode!=4;target_ok=mode!=5;scripted=mode==6;
   field<unsigned char>(target.data(),0x2c3)=field<unsigned char>(target.data(),0x31d)=static_cast<unsigned char>(h);
   field<std::int8_t>(source_actor.data(),0x53a)=static_cast<std::int8_t>(limit);field<std::int8_t>(input.data(),0xa)=static_cast<std::int8_t>(limit);
   if(scripted)field<u32>(target.data(),4)=original?0x20000u:0x40000u;
   if(mode==7)field<unsigned char>(target.data(),original?0xc9:0xc5)=0x40;
   bool current=invoke_filter(original?bias+0xdf3b0:0x4f2da0,original?source_actor.data():actor.data(),target.data(),input.data());
   if(original)answer=current;else check(answer==current,"candidate script, claim, actor, ally and signed heaviness policy agrees with owner XML1");
  }
 }
 scripted=false;
}
void acquisition_tests(const image& pc,const image& x1){
 for(u32 a:{0x682f88u,0x6872d0u,0x680030u})pc.load(a,4);
 for(bool locked:{false,true})for(bool same_team:{false,true})for(float height:{10.f,120.f,120.01f}){
  u32 old_selected=0,old_aux=0;
  for(bool original:{true,false}){
   source=original;reset();bind();target_actor=false;target_team=same_team?owner_team:2;
   field<vec>(target.data(),0x20)={20,0,height};field<unsigned char>(target.data(),0x2c3)=field<unsigned char>(target.data(),0x31d)=1;
   field<std::int8_t>(source_actor.data(),0x53a)=2;field<std::int8_t>(input.data(),0xa)=2;
   field<u32>(type.data(),0x18)=original?6u:32u;field<u32>(at(0x70b67c),0)=1;field<u32>(at(0x70b840),0)=2;
   field<u32>(original?source_actor.data():actor.data(),original?0x338:0x3e0)=locked?222u:0u;
   query_vt[0x64/4]=reinterpret_cast<void*>(&cone);query_vt[0x68/4]=reinterpret_cast<void*>(&radius);
   field<void*>(ordered.data(),0)=ordered_vt.data();ordered_vt[0]=reinterpret_cast<void*>(&ordered_count);ordered_vt[1]=reinterpret_cast<void*>(&ordered_at);
   jump(0x4ab770,&order_query);jump(bias+0xa2b10,&order_query);jump(0x4ab7d0,&release_order);jump(bias+0xa2b70,&release_order);jump(0x4ab960,&line_trace);jump(bias+0xa2d00,&line_trace);
   field<void*>(at(0x67fa60),0)=reinterpret_cast<void*>(&vector_zero);field<void*>(at(0x67fa2c),0)=field<void*>(at(0x67f9e0),0)=reinterpret_cast<void*>(&vector_copy);
   field<void*>(at(0x67f9c8),0)=reinterpret_cast<void*>(&vector_construct);
   field<void*>(at(0x67f9d8),0)=reinterpret_cast<void*>(&component);field<void*>(at(0x67f9c4),0)=reinterpret_cast<void*>(&component_ref);field<void*>(at(0x67fa58),0)=reinterpret_cast<void*>(&length_squared);field<void*>(at(0x67fa5c),0)=reinterpret_cast<void*>(&normalize);field<void*>(at(0x67fa0c),0)=reinterpret_cast<void*>(&vector_difference);
   if(original)load_source(x1,0xdf740,0x53a);else pc.load(0x4f2ed0,0x539);
   field<u32>(at(bias+0x4c0a60),0)=0;field<u32>(at(bias+0x48338c),0)=1;field<u32>(at(bias+0x485878),0)=2;
   jump(0x4f2da0,reinterpret_cast<void*>(&tk_guard_bridges::tk_filter));
   u32 selected=0,auxiliary=0;void* a=original?source_actor.data():actor.data();void* parameters=input.data();const u32 service=original?bias+0xdf740:0x4f2ed0;
   __asm {
    push parameters
    lea eax,auxiliary
    push eax
    lea eax,selected
    push eax
    mov ecx,a
    call service
    add esp,12
   }
   if(original){old_selected=selected;old_aux=auxiliary;}else check(old_selected==selected&&old_aux==auxiliary,"native PC acquisition agrees with owner XML1 for ordered and locked physical targets");
   if(!same_team&&(locked||height<=120.f))check(selected==222&&auxiliary==222,"native acquisition publishes both selected/auxiliary handles");
  }
 }
}
void guard_tests(const image& pc){
 source=false;reset();bind();field<unsigned char>(context.data(),0x6c)=0x80;
 field<void*>(context.data(),4)=move.data();
 tk_guard_bridges::guard_entry(nullptr,nullptr,actor.data(),input.data(),context.data());
 check(field<unsigned char>(context.data(),0x6c)==0x84&&query_calls==1&&starts==1,"guard entry makes one contextual decision and aborts the parent transition");
 for(int kind=0;kind<4;++kind)for(bool original:{true,false}){
  source=original;acrobatics=0;might=0;reset();bind();target_actor=kind<2;target_team=kind==1?0x1c:2;can_lift=kind!=3;
  if(original)reinterpret_cast<bool(__thiscall*)(void*,void*,void*)>(bias+0xd99d0)(nullptr,source_actor.data(),input.data());
  else tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data());
  if(kind!=3){check(query_calls==1&&destructs==1,"guard query acquires/releases once");
   if(kind==0)check(action_id==(original?3:0),"source generic admission is broader; requested ground policy rejects untouched enemies");
   if(kind==2)check(action_id==24&&field<unsigned char>(target.data(),original?0x2b9:0x30d)&4,"object guard retains pickup claim");
  }
  else check(action_id==0&&std::find(order.begin(),order.end(),8)!=order.end(),"cannot lift reports source feedback and takes idle");
 }
 for(int r:{0,2,3})for(float z:{-.8f,0.f,.2f,.8f}){
  struct result{int starts,action;vec position,angles;bool pending;};result a{};
  for(bool original:{true,false}){
   source=original;acrobatics=r;might=0;reset();bind();wall=true;normal={-1,0,z};field<unsigned char>(target.data(),0x54d)=field<unsigned char>(target.data(),0x765)=1;
   const bool pending=original?reinterpret_cast<bool(__thiscall*)(void*,void*,void*)>(bias+0xd99d0)(nullptr,source_actor.data(),input.data()):tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data());
   result b{starts,action_id,placed,turned,pending};
   if(original)a=b;else{
    check(a.starts==b.starts&&a.action==b.action&&!std::memcmp(&a.position,&b.position,sizeof(vec))&&!std::memcmp(&a.angles,&b.angles,sizeof(vec)),"rank3 wall acquisition and signed normal boundary agree with owner XML1");
    check(a.pending==b.pending,"guard returns source false after immediate wall-lunge, true for pending decisions");
    if(r==3&&z<.2f)check(played_name=="walllunge"&&destructs==1,"guard selects walllunge and releases query, independently of rank2 cling/backflip");
   }
  }
 }
 for(int r:{0,2,3})for(bool is_immune:{false,true}){
  int a_action=0,a_starts=0,a_notices=0;
  for(bool original:{true,false}){
   source=original;acrobatics=r;might=0;reset();bind();immune=is_immune;field<unsigned char>(target.data(),0x54d)=field<unsigned char>(target.data(),0x765)=1;
   if(original)reinterpret_cast<bool(__thiscall*)(void*,void*,void*)>(bias+0xd99d0)(nullptr,source_actor.data(),input.data());
   else tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data());
   const int notices=static_cast<int>(std::count(order.begin(),order.end(),7));
   if(original){a_action=action_id;a_starts=starts;a_notices=notices;}else check(a_action==action_id&&a_starts==starts&&a_notices==notices,"stun immunity and acrobatics air-grab follow source decisions");
  }
 }
 for(bool fastball:{false,true}){
  int old_action=0,old_starts=0;u32 old_held=0;
  for(bool original:{true,false}){
   source=original;acrobatics=0;might=0;effective_might_override=3;reset();bind();target_team=owner_team;
   field<unsigned char>(stats.data(),0x3a3)=1;field<unsigned char>(stats.data(),0x2ae)=0x20;
   field<unsigned char>(stats.data(),0x3a2)=fastball?1:0;field<unsigned char>(stats.data(),0x2ad)=fastball?0x80:0;
   if(original)reinterpret_cast<bool(__thiscall*)(void*,void*,void*)>(bias+0xd99d0)(nullptr,source_actor.data(),input.data());
   else tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data());
   const u32 held_token=field<u32>(self_actor(),original?0x554:0x76c);
   if(original){old_action=action_id;old_starts=starts;old_held=held_token;}
   else{
    check(old_action==action_id&&old_starts==starts&&old_held==held_token,"actor-aware Might, distinct from raw rank zero, preserves source ally policy");
    if(fastball)check(played_name=="throwally"&&held_token==222,"eligible ally starts throwally with the rule45 owner established");
   }
  }
 }
 effective_might_override=-1;
 source=false;reset();bind();field<u32>(target.data(),4)=0x40010;
 pc.load(0x4fbe80,0x32);pc.load(0x680030,4);jump(0x425b70,&native_movement_parameter);native_fallback_parameters=0;
 check(tk_guard_bridges::guard_input(nullptr,nullptr,actor.data(),input.data()),"released empty guard produces one schedulable decision");
 check(native_fallback_parameters==1&&action_id==0&&starts==0&&field<void*>(actor.data(),0x3bc)==move.data(),"real native zero-input fallback selects Idle without recursively entering guard");
}
struct control_result{vec v;float cost,deadline,damage;int debits;u32 token,hit_flags;};
control_result control_case(bool original,int level,int damage,float available,bool button,float current_time=10.f,int source_heaviness=0,vec initial_velocity={10,0,0},bool as_character=true,bool caster_ai=false){
 source=original;reset();bind();time_now=current_time;energy=available;
 target_actor=as_character;if(caster_ai)field<unsigned char>(actor.data(),0x3d8)|=1;
 if(!original&&!as_character&&tk_guard_bridges::trace_acquisition)field<u32>(target.data(),0x3d8)=0xdeadbeef;
 field<vec>(target.data(),0x20)={20,0,0};field<vec>(target.data(),0x100)=field<vec>(target.data(),0x120)=initial_velocity;
 field<unsigned char>(target.data(),0x2c3)=static_cast<unsigned char>(source_heaviness);field<unsigned char>(target.data(),0x31d)=static_cast<unsigned char>(source_heaviness>0?std::min(source_heaviness-1,3):0);lift_rule::source_override=source_heaviness;
 field<u32>(input.data(),0)=button?16:0;
 if(original){
  field<u32>(source_actor.data(),0x554)=222;field<unsigned char>(source_actor.data(),0x235)=1;
  field<float>(source_actor.data(),0x3b0)=9.9f;field<float>(source_actor.data(),0x4b4)=20;field<float>(source_actor.data(),0x4b8)=9.9f;
  field<vec>(source_actor.data(),0x4bc)={20,0,120};field<vec>(source_actor.data(),0x4c8)={20,0,0};field<float>(source_actor.data(),0x4d4)=1.25f;field<u32>(source_actor.data(),0x4b0)=88;
  field<std::int16_t>(source_actor.data(),0x538)=19;field<std::int8_t>(source_actor.data(),0x53b)=static_cast<std::int8_t>(damage);field<std::int8_t>(source_actor.data(),0x53c)=static_cast<std::int8_t>(level);
  reinterpret_cast<bool(__thiscall*)(void*,void*,void*)>(bias+0xdff10)(nullptr,source_actor.data(),input.data());
 }else{
  field<u32>(actor.data(),0x76c)=222;field<unsigned char>(actor.data(),0x259)=2;
  tk_guard_bridges::tk_state s;s.victim=222;s.time=9.9f;s.distance=20;s.offset={20,0,120};s.last={20,0,0};s.heading=1.25f;s.sound=88;s.impact=19;s.level=static_cast<std::int8_t>(level);s.damage=static_cast<std::int8_t>(damage);tk_guard_bridges::tk_states[111]=s;
  tk_guard_bridges::tk_update(nullptr,nullptr,actor.data(),input.data());
 }
 return {last_velocity,last_cost,field<float>(target.data(),original?0x28c:0x2f0),original?static_cast<float>(field<std::int16_t>(target.data(),0x294)):field<float>(target.data(),0x2b8),debits,field<u32>(target.data(),original?0x558:0x770),field<u32>(target.data(),original?0x288:0x2c8)};
}
void control_tests(){
 tk_guard_bridges::trace_acquisition=true;logger::captured.clear();
 control_case(false,1,17,100,true,10.05f,0,{10,0,0},false,false);
 check(std::any_of(logger::captured.begin(),logger::captured.end(),[](const std::string& line){return line.find("tk control")!=std::string::npos&&line.find("actor_flags=00000000")!=std::string::npos;}),"object TK tracing never reads character-only fields past the physical object layout");
 tk_guard_bridges::trace_acquisition=false;
 // A still target must reach the native release's facing fallback as zero.
 // The original callback's final unguarded reciprocal produces NaNs here.
 for(bool ai:{false,true})for(bool body:{false,true})for(vec v:{vec{},vec{0,0,90},vec{1.e-30f,0,0}}){
  control_case(false,1,17,100,false,10.05f,0,v,body,ai);
  check(std::isfinite(release_velocity.x)&&std::isfinite(release_velocity.y)&&std::isfinite(release_velocity.z),"stationary/vertical/tiny TK release direction is finite");
  check(release_velocity.x==0&&release_velocity.y==0&&release_velocity.z==0,"zero horizontal TK release reaches native facing fallback");
  check(release_mode==1&&stops==1&&tk_guard_bridges::tk_states.empty(),"stationary throw releases once and clears hold/sound");
 }
 const auto stopped=control_case(false,1,17,100,true,9.9f);
 check(stopped.v.x==0&&stopped.v.y==0&&stopped.v.z==0,"zero timestep retains finite stationary hold without a time division");
 source=false;reset();bind();check(tk_guard_bridges::tk_input(nullptr,nullptr,actor.data(),input.data())&&action_id==0,"failed acquisition returns through PC input scheduling to idle");
 for(int level:{-1,1,11,127})for(int damage:{-128,-1,1,17,127})for(float current_time:{10.f,10.05f}){
  auto a=control_case(true,level,damage,100,true,current_time),b=control_case(false,level,damage,100,true,current_time);
  if(std::memcmp(&a.v,&b.v,sizeof(vec)))std::printf("velocity source %.9g %.9g %.9g PC %.9g %.9g %.9g\n",a.v.x,a.v.y,a.v.z,b.v.x,b.v.y,b.v.z);
  if(a.cost!=b.cost||a.deadline!=b.deadline||a.damage!=b.damage||a.debits!=b.debits||a.token!=b.token)std::printf("level %d damage %d: cost %.9g/%.9g deadline %.9g/%.9g damage %.9g/%.9g debits %d/%d token %u/%u\n",level,damage,a.cost,b.cost,a.deadline,b.deadline,a.damage,b.damage,a.debits,b.debits,a.token,b.token);
  check(!std::memcmp(&a.v,&b.v,sizeof(vec))&&a.cost==b.cost&&a.deadline==b.deadline&&a.damage==b.damage&&a.debits==b.debits&&a.token==b.token,"TK control agrees with owner XML1 movement, signed cost/damage, deadline, owner");
 }
 for(bool button:{false,true})for(float available:{1.f,2.f,100.f}){
  auto a=control_case(true,11,17,available,button,10.05f);const vec av=release_velocity;const int am=release_mode,al=release_level,as=stops;
  auto b=control_case(false,11,17,available,button,10.05f);
  check(!std::memcmp(&av,&release_velocity,sizeof(vec))&&am==release_mode&&al==release_level&&as==stops&&a.cost==b.cost&&a.damage==b.damage,"release, affordability equality and depletion agree with owner XML1");
 }
 for(int source_heaviness:{0,1,2,3,4,5})for(int damage:{17,18,127}){
  auto a=control_case(true,11,damage,100,true,10.05f,source_heaviness),b=control_case(false,11,damage,100,true,10.05f,source_heaviness);
  check(a.hit_flags==b.hit_flags&&a.damage==b.damage,"TK hit flags use decoded XML1 heaviness, including source2 -> PC byte1");
 }
}
}
// 082e tkA (live-082e m32 18:00:48.968): attempt 1 selected 2CD7, owner Phoenix, 11 control
// lines at z 6.000 with offset_z 123.440, never raised. The old trace gave no reason. A held
// target that does not move for 15 control frames while the hold asks for motion must be
// reported once per hold with its contact byte and receiver impact age (+0x2F4, set by 499100).
// No decision changes: the line is trace-only.
void blocked_trace_test(){
 for(bool moving:{false,true}){
  source=false;reset();bind();time_now=10.f;energy=100;target_actor=true;
  tk_guard_bridges::trace_acquisition=true;logger::captured.clear();
  field<vec>(target.data(),0x20)={20,0,6};field<vec>(target.data(),0x100)=field<vec>(target.data(),0x120)={0,0,0};
  field<unsigned char>(target.data(),0x30c)=0x84;field<float>(target.data(),0x2f4)=9.95f;
  field<u32>(input.data(),0)=16;field<u32>(actor.data(),0x76c)=222;field<unsigned char>(actor.data(),0x259)=2;
  tk_guard_bridges::tk_state s;s.victim=222;s.time=9.9f;s.distance=20;s.offset={20,0,123.44f};s.last={20,0,6};s.heading=1.25f;s.sound=88;s.impact=19;s.level=11;s.damage=17;tk_guard_bridges::tk_states[111]=s;
  for(int frame=0;frame<40;++frame){
   time_now+=1.f/60.f;
   if(moving){auto& p=field<vec>(target.data(),0x20);p.z+=2.f;}
   tk_guard_bridges::tk_update(nullptr,nullptr,actor.data(),input.data());
   if(tk_guard_bridges::tk_states.empty())break;
  }
  const auto blocked=std::count_if(logger::captured.begin(),logger::captured.end(),[](const std::string& line){return line.find("tk blocked")!=std::string::npos;});
  if(moving)check(blocked==0,"a rising TK target is never reported blocked");
  else{
   check(blocked==1,"a held TK target that never moves is reported blocked exactly once per hold");
   check(std::any_of(logger::captured.begin(),logger::captured.end(),[](const std::string& line){return line.find("tk blocked")!=std::string::npos&&line.find("contact=84")!=std::string::npos&&line.find("impact_age=")!=std::string::npos&&line.find("z=6.000")!=std::string::npos;}),"blocked TK trace names the contact byte, impact age and the unchanged height");
  }
  tk_guard_bridges::tk_states.clear();tk_guard_bridges::trace_acquisition=false;
 }
}
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 AddVectoredExceptionHandler(1,[](PEXCEPTION_POINTERS e)->LONG{std::printf("exception %08lX at %p EAX=%08lX ECX=%08lX ESI=%08lX ESP=%08lX return=%08X EDX=%08lX EDI=%08lX\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,e->ContextRecord->Eax,e->ContextRecord->Ecx,e->ContextRecord->Esi,e->ContextRecord->Esp,*reinterpret_cast<u32*>(e->ContextRecord->Esp),e->ContextRecord->Edx,e->ContextRecord->Edi);ExitProcess(1);});
 if(argc<3)return 2;DWORD old=0;if(!VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old))return 2;
 if(VirtualAlloc(at(bias),0x600000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)!=at(bias))return 2;
 image pc(argv[1],pc_hash),x1(argv[2],x1_hash);if(failures)return 1;
 check(field<u32>(const_cast<unsigned char*>(pc.read(0x69d1ac,0x3c)),0x34)==0x590120&&field<u32>(const_cast<unsigned char*>(pc.read(0x69d1ac,0x3c)),0x38)==0x590bd0,"retail audio table distinguishes boolean from resource lookup");
 pc.load(0x590120,0x66);
 load_source(x1,0xdf640,0xfd);load_source(x1,0xd99d0,0x7c6);load_source(x1,0xdff10,0xc71);load_source(x1,0xdfc80,0x28b);load_source(x1,0x38210,0x15);load_source(x1,0xdf3b0,0xed);
 std::puts("cancel");cancel_tests();std::puts("entry");entry_tests(pc);std::puts("constructors");constructor_tests(pc,x1);std::puts("parser");parser_tests(pc);std::puts("filter");filter_tests();std::puts("acquisition");acquisition_tests(pc,x1);std::puts("guard");guard_tests(pc);std::puts("control");control_tests();std::puts("retail audio ABI");retail_audio_contract_tests(pc);release_damage_test(pc);guard_entry_timing_test();round3_guard_and_tk_test();
 live_stun_guard_tests(pc);live_tk_zero_selection_test(pc);source_throw_value_test(pc,x1);
 std::puts("blocked trace");blocked_trace_test();
 std::printf("TK/guard native: %d checks, %d failures\n",checks,failures);return failures?1:0;
}

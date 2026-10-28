// Full production installer plus carry callbacks in an invented native image.
// The included lifecycle harness supplies retail input, OFF/guard/rollback probes
// and shared-lookup coexistence; no game, rendering or world startup executes.
#include <Windows.h>
#include "tk_guard_bridges.hpp"
#define main lifecycle_main
#include "xml1_grab_carry_install_test.cpp"
#undef main
#include "xml1_air_carry.hpp"
#include "det_math.hpp"
#include "balance_parity_rules.hpp"
#include <cmath>
namespace {
struct V{float x,y,z;};struct P{V position,angles;};
std::array<unsigned char,0x80> air_context{},air_input{},air_manager{},air_ui{},air_session{},air_query{},air_sound{};
std::array<void*,100> manager_vt{},ui_vt{},session_vt{},query_vt{};
std::array<unsigned char,0x900> victim_stats{};
std::string requested_name;
std::vector<std::string> air_events;
int flight_rank=3,actor_team=1,victim_team=2,first_reads=0,releases=0,queries=0,preparations=0,notifications=0,ui_errors=0,feedbacks=0,debits=0,flight_sets=0,air_starts=0,bone_mask=0x61,anim_id=-1,generic_calls=0,selects=0,air_forces=0,sounds_played=0,displacements=0,air_placements=0;
int drops=0;
bool sequence_mode=false,event_mode=false;
bool round3_pose=false;
bool clearance_blocked=false;
bool live_character_clearance=false;
bool sequence_states=false;
std::array<void*,3> sequence_nodes{};
void* sequence_carry=nullptr;
void* sequence_handler_now=nullptr;
void* __fastcall air_move_handler(void* move,void*){if(round3_pose&&move==other.data())return xml1_air_carry::lookup("ch_air_grab_victim");return sequence_mode?sequence_handler_now:nullptr;}
int sequence_chains=0;
int probes=0,carry_entries=0,descent_forces=0;bool probe_hit=true,native_descent_blocked=false,current_flying=true;
u32 current_move_key=102;
int action_chosen=-1,last_flight=-1,mode_value=0;
bool mode_a=false,mode_b=false,mode_c=false,map_restricted=false,session_gate=false,old_map_bit=false,neutral_ok=false,immunity=false,valid=true,liftable=true,friendly=false,hit=false,available=true,contact_exists=true,fallback=false,sound_valid=true,sound_empty=false;
float energy=1000.f,last_debit=0.f,parameter2=1.f,parameter8=50.f,parameter9=100.f;
void* physical_target=nullptr;void* char_target=nullptr;
V last_position{},last_angles{};P last_pose{};
std::vector<u32> air_published;
std::vector<std::pair<int,int>> filters;
std::vector<void*> idle_actors;
bool close_float(float a,float b){return std::fabs(a-b)<0.0001f;}
void air_reset(){
 reset();air_context.fill(0);air_input.fill(0);requested_name.clear();air_events.clear();air_published.clear();filters.clear();idle_actors.clear();
 sequence_states=false;drops=0;first_reads=releases=queries=preparations=notifications=ui_errors=feedbacks=debits=flight_sets=air_starts=generic_calls=selects=air_forces=sounds_played=displacements=air_placements=0;action_chosen=last_flight=-1;
 flight_rank=3;actor_team=1;victim_team=2;bone_mask=0x61;anim_id=-1;energy=1000.f;last_debit=0.f;parameter2=1.f;parameter8=50.f;parameter9=100.f;clearance_blocked=false;
 mode_a=mode_b=mode_c=map_restricted=session_gate=old_map_bit=neutral_ok=immunity=friendly=hit=fallback=sound_empty=false;
 mode_value=0;valid=liftable=available=contact_exists=sound_valid=true;physical_target=char_target=victim.data();now=0;
 put(actor.data(),0x3d8,4u);put(actor.data(),0xbc,0u);put(actor.data(),4,0x80u);put(victim.data(),4,0x80u);put(victim.data(),0x27c,100.f);put(actor.data(),0x6a4,0.f);put(actor.data(),0x6a0,-2.f);
 put(victim.data(),0x35c,victim_stats.data());put<unsigned char>(victim_stats.data(),0x2ad,0);put<unsigned char>(victim.data(),0x764,0x80);put<unsigned char>(victim.data(),0x765,0);
 put(actor.data(),0x20,V{10,20,30});put(victim.data(),0x20,V{0,0,0});put(actor.data(),0x2c,V{0,0,1});put(actor.data(),0x1f4,V{1,2,3});put(victim.data(),0x2c,V{0.1f,0.2f,0.3f});put(actor.data(),0x378,move.data());put(victim.data(),0x378,move.data());
 put(air_context.data(),0x64,0.375f);put<void*>(actor.data(),0x3bc,nullptr);
 probes=carry_entries=descent_forces=0;probe_hit=current_flying=true;native_descent_blocked=false;
 put(actor.data(),0x3d4,0);put(ptr(0x70b814),0,0x1111u);put<unsigned char>(victim_stats.data(),0x2ad,0);
}
void* __cdecl air_cast(void* e){return e==actor.data()||e==char_target?e:nullptr;}
void* __cdecl air_physical(void* e){return e==physical_target?e:nullptr;}
void* __fastcall intern(u32* token,void*,const char* name){requested_name=name;*token=std::strcmp(name,"flying")==0?102u:101u;return token;}
void* __fastcall air_named(void*,void*,const u32*,void* optional){check(optional==nullptr,"air named lookup uses null optional actor");return requested_name=="idle"?idle.data():move_present?move.data():nullptr;}
void* __fastcall flight_named(void*,void*,const u32* token){check(token==ptr(0x78a8c8),"native landing resolves named Idle");return idle.data();}
void __fastcall air_start(void* who,void*,void* chosen,int reset){++air_starts;check(reset==1&&(chosen==idle.data()||(event_mode&&chosen==move.data())),"source recovery starts named Idle reset1");idle_actors.push_back(who);air_events.push_back("idle");if(sequence_mode)put(who,0x378,chosen);}
void __fastcall air_publish(void* who,void*){air_published.push_back(get<u32>(who,4));air_events.push_back("publish");}
void __fastcall air_force(void* who,void*,void* chosen,int priority,float timing,u32 a,u32 b,u32 c){if(who==actor.data()){++descent_forces;if(chosen)++carry_entries;check(priority==-1&&timing==-1.f&&a==0&&b==0&&c==0&&requested_name=="flyguarddecide","descent enters named decide with source forced ABI");return;}++air_forces;check(who==victim.data()&&priority==-1&&timing==0.375f&&a==0&&b==1&&c==0,"carry forced transition six-argument ABI/timing");air_events.push_back("force");if(sequence_mode){reinterpret_cast<void(__thiscall*)(void*)>(0x42c270)(who);put(who,0x378,chosen);}}
void* __fastcall air_action(void*,void*,int a){action_chosen=a;air_events.push_back("action");if(sequence_mode&&sequence_states&&(a==0||a==9||a==11))return sequence_nodes[a==0?0:a==9?1:2];return selected;}
void __fastcall air_lock(void* who,void*,u32 handle){++locks;check(who==actor.data()&&(handle==0x2222||(sequence_mode&&handle==0)),"carry locks selected handle");air_events.push_back("lock");if(sequence_mode)put(who,0x76c,handle);}
void __fastcall selection(void* who,void*,u32 h){++selects;check(who==actor.data()&&h==0x2222,"selection uses attacker target handle");air_events.push_back("select");}
unsigned char __fastcall air_rank(void* who,void*,const void* rank_key,void* a,void* output){check(who==stats.data()+4&&rank_key==ptr(0x711c28)&&a==actor.data()&&output==nullptr,"rank uses stats skill view, cached flight ID and actor");return static_cast<unsigned char>(flight_rank);}
void* __cdecl manager(){return air_manager.data();}
void* __cdecl ui(){return air_ui.data();}
void* __cdecl session(){return air_session.data();}
bool __fastcall restriction(void*,void*){return map_restricted;}
bool __fastcall acquisition_gate(void*,void*){return session_gate;}
bool __fastcall old_admission(void*,void*){return old_map_bit;}
bool __fastcall air_neutral(void*,void*,u32 id){check(id==get<u32>(victim.data(),0x7dc),"neutral queries victim AI ID");return neutral_ok;}
bool __fastcall friendly_test(void*,void*){return friendly;}
int __fastcall air_team(void* who,void*){return who==actor.data()?actor_team:victim_team;}
bool __fastcall target_valid(void*,void*){return valid;}
bool __fastcall immune(void* who,void*,int id,void* source){check(who==victim.data()&&id==44&&source==nullptr,"conditional pickup immunity uses def_pickup 44");return immunity;}
bool __fastcall object_lift(void*,void*,void* object){check(object==victim.data(),"object admission calls project lift gate");return liftable;}
bool __cdecl ma(){return mode_a;}bool __cdecl mb(){return mode_b;}bool __cdecl mc(){return mode_c;}int __cdecl mv(){return mode_value;}
float __fastcall air_parameter(void*,void*,int p){check(p==2||p==8||p==9,"movement parameter is 2/8/9");return p==2?parameter2:p==8?parameter8:parameter9;}
bool __fastcall generic(void*,void*,void*,void*){++generic_calls;air_events.push_back("generic");return true;}
bool __fastcall air_fallback(void*,void*,void*,void*){return fallback;}
int __cdecl free_query(){return available?1:0;}
void** __fastcall acquire(void** out,void*,const void* type,const u32* excluded){++queries;check(type==ptr(0x70b83c)&&*excluded==0x1111,"query excludes self through native constructor");*out=air_query.data();return out;}
void __fastcall release(void** out,void*){++releases;check(*out==air_query.data(),"query released exactly once");}
void __fastcall filter(void*,void*,int a,int b){filters.emplace_back(a,b);}
void __fastcall query_mode(void*,void*,int m){check(m==0,"PC sweep mode zero");}
V* __fastcall center(void*,void*,V* out){*out={10,20,30};return out;}
V* __cdecl air_facing(V* out,const void*){*out={1,0,0};return out;}
void __fastcall sweep(void*,void*,const V* begin,const V* end,const V* box){check(begin->x==10&&begin->y==20&&begin->z==30&&end->x==58&&end->y==20&&end->z==-18,"source 48 ahead, -48 below geometry");check(box->x==16&&box->y==16&&box->z==16,"16x16x16 acquisition box");}
void** __fastcall first(void*,void*,void** out){++first_reads;*out=contact_exists?victim.data():nullptr;return out;}
void* __fastcall contact_entity(void** contact,void*){return *contact;}
bool __fastcall air_bone(void* who,void*,int id,V* out,bool a,bool b){check(!a&&!b&&(id==0||id==5||id==6),"carry requests real animated bones, never PC approximate (1,1) positions");if(!(bone_mask&(1<<id)))return false;if(sequence_mode){*out=get<V>(who,0x20);out->z+=id==0?38.f:id==5?20.f:40.f;return true;}if(id==0)*out={100,200,300};else if(who==actor.data())*out=id==5?V{10,30,50}:V{30,50,70};else *out=id==5?V{0,10,20}:V{20,30,40};return true;}
void __fastcall air_position(void* who,void*,const V* p){last_position=*p;air_events.push_back("position");if(sequence_mode)put(who,0x20,*p);}
void __fastcall air_angles(void* who,void*,const V* p){last_angles=*p;air_events.push_back("angles");if(sequence_mode)put(who,0x2c,*p);}
V* __cdecl inverse(V* out,const V* direction){if(!sequence_mode)check(direction->x==-10&&direction->y==-20&&direction->z==-30,"carrier faces target through inverse facing");*out={4,5,6};return out;}
void __fastcall air_place(void* who,void*,const V* p,const V* angles){++air_placements;last_position=*p;last_angles=*angles;if(sequence_mode){put(who,0x20,*p);put(who,0x2c,*angles);}}
float __fastcall displace(void* who,void*,const P* p){++displacements;last_pose=*p;if(sequence_mode){auto pos=get<V>(who,0x20),ang=get<V>(who,0x2c);put(who,0x20,V{pos.x+p->position.x,pos.y+p->position.y,pos.z+p->position.z});put(who,0x2c,V{ang.x+p->angles.x,ang.y+p->angles.y,ang.z+p->angles.z});}return 1.f;}
int __fastcall animation(void*,void*){return anim_id;}
void* __cdecl sound_set(int){put(air_sound.data(),0x14,sound_empty?0u:77u);return air_sound.data();}
bool __fastcall sound_ok(void*,void*,u32 sound){check(sound==77,"pickup sound handle");return sound_valid;}
void __fastcall play(void*,void*,u32 sound){check(sound==77,"play pickup sound after force");++sounds_played;}
const char* __fastcall air_message(void*,void*,int id){check(id==0x89||id==0x8a,"source feedback notification ID");return id==0x8a?"restricted":"lift";}
void __fastcall notify(void*,void*,const u32* h,const char*){++notifications;check(*h==0x1111,"notification uses actor handle by pointer");}
u32 __fastcall error_ui(void*,void*,u32,const char*,float duration,std::array<float,4> color,int zero){++ui_errors;check(duration==0.75f&&color[0]==0.8f&&color[3]==1.f&&zero==0,"native feedback duration/color/by-value ABI");return 123;}
void __fastcall feedback(void*,void*){++feedbacks;}
void __fastcall emergency_drop(void* who,void*){check(who==actor.data(),"emergency release receives carrier");++drops;}
void __fastcall prepare_flight(void*,void*){++preparations;}
void __fastcall air_flight(void* who,void*,int on,int emit){check(who==actor.data()&&emit==(descent_forces?0:1),"flight setter uses source emission argument");++flight_sets;last_flight=on;}
const u32* __fastcall flight_name(void*,void*){current_move_key=current_flying?102u:103u;return &current_move_key;}
bool __fastcall downward_probe(void* who,void*,const V* start,const V* delta,int flag,V* shift,void** contact){++probes;const auto position=sequence_mode?get<V>(actor.data(),0x20):V{10,20,30};check(who==actor.data()&&start->x==position.x&&start->y==position.y&&start->z==position.z&&delta->x==0&&delta->y==0&&((delta->z==-20&&flag==1)||(delta->z<0&&flag==0)),"entry probes down; clearance movement excludes the contacted character");*shift=flag==0&&!clearance_blocked?*delta:V{};if(flag==0&&live_character_clearance&&(get<u32>(victim.data(),4)&0x100)&&get<u32>(actor.data(),0x178)!=get<u32>(victim.data(),0x1c))*shift={};*contact=contact_exists?victim.data():nullptr;return probe_hit;}
bool __cdecl descent_blocked(int index){check(index==get<int>(actor.data(),0x3d4),"native descent controller gate");return native_descent_blocked;}
u32 descent_site=0x433e46;
__declspec(naked) bool __fastcall descent_probe(void*,void*,void*){
 __asm {
  push esi
  push edi
  mov edi,ecx
  mov esi,[esp+12]
  call dword ptr [descent_site]
  pop edi
  pop esi
  ret 4
 }
}
__declspec(naked) void descent_rejected(){__asm {add esp,4} __asm {xor eax,eax} __asm {ret}}
__declspec(naked) void descent_accepted(){__asm {mov eax,1} __asm {ret}}
float __fastcall air_energy(void*,void*){return energy;}
bool finite_debit=false;
void __fastcall energy_set(void* who,void*,float value){energy=value;put(who,0x288,value);}
__declspec(naked) void __fastcall native_debit(void*,void*,float){
 __asm {push esi} __asm {sub esp,10h} __asm {mov esi,ecx}
 __asm {mov eax,[esp+18h]} __asm {mov [esp+4],eax}
 __asm {mov eax,4319b5h} __asm {jmp eax}
}
__declspec(naked) void debit_done(){__asm {add esp,10h} __asm {pop esi} __asm {ret 4}}
void __fastcall spend(void* who,void*,float cost){++debits;last_debit=cost;if(finite_debit)native_debit(who,nullptr,cost);}
void air_bind(){
 // Older installers do not guard this table entry. Supply its retail value
 // for the baseline differential without replacing a newly installed hook.
 if(!get<void*>(ptr(0x692378),0))native(0x692378,4);
 move_vt[0x30/4]=reinterpret_cast<void*>(&air_move_handler);
 // Keep the real Guard/no-power/native-refusal instructions. Only the
 // vector descent tail is replaced by a return in this invented caller.
 const auto installed_shortcut=get<std::uint16_t>(ptr(0x433e6a),0);
 native(0x433e46,0x26);put(ptr(0x433e6a),0,installed_shortcut);
 if(get<unsigned char>(ptr(0x433e6c),0)!=0xe9)jump(0x433e6c,&descent_rejected);jump(0x433e72,&descent_rejected);jump(0x433ea5,&descent_accepted);jump(0x61a3a0,&descent_blocked);jump(0x42b6c0,&downward_probe);
 jump(0x427ca0,&flight_named);jump(0x431e00,&emergency_drop);jump(0x402b60,&air_cast);jump(0x429110,&air_physical);jump(0x41b120,&intern);jump(0x501a50,&air_named);jump(0x42c340,&air_start);jump(0x45fe40,&air_publish);jump(0x432030,&air_force);jump(0x427c80,&air_action);jump(0x42d8e0,&air_lock);jump(0x42c7c0,&selection);jump(0x43b180,&air_rank);
 jump(0x484990,&manager);jump(0x59ee20,&ui);jump(0x4bdb30,&manager);jump(0x48de40,&session);jump(0x5252e0,&manager);jump(0x41a220,&air_team);jump(0x429210,&target_valid);jump(0x54d710,&immune);jump(0x427f60,&object_lift);jump(0x4c87a0,&ma);jump(0x4c87b0,&mb);jump(0x4c87c0,&mc);jump(0x4c8820,&mv);jump(0x425b70,&air_parameter);jump(0x4fce50,&generic);jump(0x4fbe80,&air_fallback);
 jump(0x4ab740,&free_query);jump(0x4ab6d0,&acquire);jump(0x4ab720,&release);jump(0x4a9590,&contact_entity);jump(0x560990,&air_facing);jump(0x5607b0,&inverse);jump(0x41d620,&air_bone);jump(0x438cb0,&sound_set);jump(0x477310,&manager);jump(0x421080,&feedback);jump(0x41f7d0,&air_flight);jump(0x41d5d0,&air_energy);jump(0x4318d0,&spend);
 actor_vt[0x24/4]=reinterpret_cast<void*>(&center);actor_vt[0x1c/4]=reinterpret_cast<void*>(&air_position);actor_vt[0x2c/4]=reinterpret_cast<void*>(&air_angles);actor_vt[0x158/4]=reinterpret_cast<void*>(&air_place);actor_vt[0x130/4]=reinterpret_cast<void*>(&displace);actor_vt[0x8c/4]=reinterpret_cast<void*>(&play);actor_vt[0x1e0/4]=reinterpret_cast<void*>(&prepare_flight);
 move_vt[0xe8/4]=reinterpret_cast<void*>(&animation);move_vt[0x58/4]=reinterpret_cast<void*>(&animation);
 move_vt[0xd0/4]=reinterpret_cast<void*>(&flight_name);
 manager_vt[0]=reinterpret_cast<void*>(&sound_ok);manager_vt[2]=reinterpret_cast<void*>(&air_message);manager_vt[0x3c/4]=reinterpret_cast<void*>(&acquisition_gate);manager_vt[0x44/4]=reinterpret_cast<void*>(&old_admission);manager_vt[0x8c/4]=reinterpret_cast<void*>(&restriction);manager_vt[0x78/4]=reinterpret_cast<void*>(&air_neutral);
 ui_vt[0x40/4]=reinterpret_cast<void*>(&notify);ui_vt[0x34/4]=reinterpret_cast<void*>(&error_ui);session_vt[0x5c/4]=reinterpret_cast<void*>(&friendly_test);
 query_vt[0x40/4]=reinterpret_cast<void*>(&filter);query_vt[0x80/4]=reinterpret_cast<void*>(&query_mode);query_vt[0x74/4]=reinterpret_cast<void*>(&sweep);query_vt[0x20/4]=reinterpret_cast<void*>(&first);
 put(air_manager.data(),0,manager_vt.data());put(air_ui.data(),0,ui_vt.data());put(air_session.data(),0,session_vt.data());put(air_query.data(),0,query_vt.data());put<unsigned char>(ptr(0x70b6fd),0,0);
}
void* air_handler(const char* name){void* h=xml1_air_carry::lookup(name);check(h!=nullptr,"carry handler registered");return h;}
template<class T>T callback(void* h,unsigned index){return get<T>(get<void*>(h,0),index*4);}
void admission_tests(){
 void* h=air_handler("CH_FLY_GUARD_DECIDE");auto enter=callback<entry_t>(h,1);auto decide=callback<update_t>(h,6);
 air_reset();put<unsigned char>(air_context.data(),0x6c,0x80);enter(h,nullptr,actor.data(),air_input.data(),air_context.data());check(get<unsigned char>(air_context.data(),0x6c)==0x81,"decide entry sets context bit0 only");
 for(int r:{0,1,2,3,5,6}){air_reset();flight_rank=r;selected=move.data();check(decide(h,nullptr,actor.data(),air_input.data()),"decide always returns true");check(queries==(r>2?1:0)&&first_reads==(r>2?1:0)&&releases==(r>2?1:0),"rank>2 query first result only and release");if(r>2)check(filters==std::vector<std::pair<int,int>>{{1,1},{1,4}},"both source acquisition filters in order");}
 for(int mode=0;mode<3;++mode){air_reset();mode_a=true;mode_b=mode==0;mode_c=mode==1;decide(h,nullptr,actor.data(),air_input.data());check(queries==(mode==2?1:0),"special mode suppression needs A and B/C");}
 for(int c=0;c<7;++c){air_reset();selected=move.data();if(c==0)contact_exists=false;if(c==1)physical_target=nullptr;if(c==2)put(victim.data(),4,0x40000u);if(c==3)session_gate=true;if(c==4)available=false;if(c==5){contact_exists=false;fallback=true;}if(c==6)valid=false;decide(h,nullptr,actor.data(),air_input.data());check(action_chosen==(c==5?-1:0)&&locks==0,"rejections fall back/Idle; invalid character directly Idle");}
 for(int branch=0;branch<6;++branch){air_reset();selected=move.data();if(branch==0)victim_team=actor_team;if(branch==1||branch==2){victim_team=0x1c;neutral_ok=branch==1;}if(branch>=3)put<unsigned char>(victim.data(),0x765,branch==3?0:1);immunity=branch==5;decide(h,nullptr,actor.data(),air_input.data());check(action_chosen==(branch==2?-1:branch==4?25:3)&&locks==(branch==2?0:1),"ally/neutral/enemy/immunity source selection branches");check(notifications==(branch==5?1:0),"resistance feedback only in conditional immune branch");}
 for(int branch=0;branch<3;++branch){air_reset();selected=move.data();char_target=nullptr;liftable=branch==0;if(branch==2)put<unsigned char>(actor.data(),0x3d8,5);decide(h,nullptr,actor.data(),air_input.data());check(action_chosen==(branch==0?24:0)&&locks==(branch==0?1:0),"lift object selects Special after lock; rejected object Idle");check(ui_errors==(branch==1?1:0)&&feedbacks==(branch==1?1:0),"failed object feedback only for local/human carrier");}
}
void pickup_tests(){
 void* h=air_handler("ch_air_grab_pickup");auto enter=callback<entry_t>(h,1);auto frame=callback<frame_t>(h,4);
 air_reset();victim_team=actor_team;enter(h,nullptr,actor.data(),nullptr,air_context.data());check(requested_name=="airgrabpickupally"&&air_forces==1,"ally pickup entry selects the authored ally pose before its time-zero event");
 air_reset();enter(h,nullptr,actor.data(),nullptr,air_context.data());check(get<unsigned char>(victim.data(),0x764)==0x90&&air_published==std::vector<u32>{0x84}&&air_forces==1&&selects==1&&sounds_played==1&&requested_name=="airgrabpickupvictim","pickup marker/bit2/named victim/selection/force/sound");check(air_events==std::vector<std::string>{"publish","select","angles","force"},"pickup ordering before sound");
 for(int c=0;c<2;++c){air_reset();if(c==0)target_present=false;else char_target=nullptr;enter(h,nullptr,actor.data(),nullptr,air_context.data());check(air_starts==1&&air_forces==0,"pickup missing or noncharacter target Idles");}
 for(int c=0;c<2;++c){air_reset();sound_valid=c==0;sound_empty=c==0;enter(h,nullptr,actor.data(),nullptr,air_context.data());check(sounds_played==0,"missing or invalid sound not played");}
 for(int c=0;c<2;++c){air_reset();put(victim.data(),0x20,V{2,4,99});if(c)bone_mask=0;frame(h,nullptr,actor.data(),ptr(0x123));check(close_float(last_position.x,c?2.08f:2.28f)&&close_float(last_position.y,c?4.16f:4.46f)&&last_position.z==99&&close_float(last_angles.z,4.14f),"pickup per-callback XY 1% interpolation, fixed Z, yaw+3.14");}
}
void carry_tests(){
 void* h=air_handler("ch_air_grab_pickup_idle");auto enter=callback<entry_t>(h,1);auto tick=callback<update_t>(h,6);auto frame=callback<frame_t>(h,4);
 for(int c=0;c<2;++c){air_reset();if(c)bone_mask=0;enter(h,nullptr,actor.data(),nullptr,air_context.data());check(air_published==std::vector<u32>{0x82,0x86,0x87}&&air_placements==1&&last_position.z==(c?13.f:245.f)&&last_angles.z==1.f,"carry entry publishes flags1/2/0 and bone0 / fallback -17 position with live angles");}
 for(int c=0;c<3;++c){air_reset();if(c==0)target_present=false;if(c==1)char_target=nullptr;if(c==2)put(victim.data(),0x27c,0.f);enter(h,nullptr,actor.data(),nullptr,air_context.data());check(air_starts==1&&air_placements==0,"carry entry missing/noncharacter/dead target Idles");}
 for(auto test:std::array<std::array<int,3>,8>{{{0,0,0},{0,50,0},{0,51,9},{0,100,9},{0,101,11},{16,1,1},{32,1,2},{48,1,1}}}){air_reset();selected=other.data();put(air_input.data(),0,u32(test[0]));put(air_input.data(),8,float(test[1]));check(tick(h,nullptr,actor.data(),air_input.data())&&action_chosen==test[2]&&generic_calls==1&&air_events[0]=="generic","carry generic first, attack/smash precedence and speed boundaries");}
 for(u32 buttons:{0u,0x100u}){air_reset();selected=move.data();put(air_input.data(),0,buttons);check(!tick(h,nullptr,actor.data(),air_input.data())&&get<void*>(actor.data(),0x3bc)==nullptr,"held or released Guard keeps the current carry move without restarting its entry");}
 air_reset();selected=nullptr;put(actor.data(),0x3bc,idle.data());check(!tick(h,nullptr,actor.data(),air_input.data())&&get<void*>(actor.data(),0x3bc)==idle.data(),"absent carry action does not overwrite generic next move");
 for(const auto* name:{"ea_flyingcarry_flyfast_v","ea_flyingcarry_flyslow_v","ea_flyingcarry_idle_v","ea_flyingcarry_pickup_v","ea_flyingcarry_pickup_a","ea_flyingcarry_idle_a"}){air_reset();flight_rank=6;anim_id=anim_enums::extension_id(name);frame(h,nullptr,actor.data(),nullptr);bool hand=true,interpolate=std::string(name).find("pickup")!=std::string::npos;check(displacements==1&&close_float(last_pose.position.x,(hand?10.f:100.f)*(interpolate?0.1f:1.f))&&close_float(last_pose.position.z,(hand?30.f:245.f)*(interpolate?0.1f:1.f))&&close_float(last_pose.angles.z,2.7f),"victim enum attachment case maps published IDs and movement deltas");check(get<unsigned char>(victim.data(),0x764)==0x90,"carry frame reasserts attachment marker");}
 air_reset();flight_rank=6;bone_mask=0;frame(h,nullptr,actor.data(),nullptr);check(last_pose.position.z==13,"attachment missing bone0 uses +38-55 fallback");
 air_reset();flight_rank=6;put<void*>(victim.data(),0x378,nullptr);frame(h,nullptr,actor.data(),nullptr);check(displacements==0,"no victim move means no attachment displacement");
 air_reset();flight_rank=6;put(victim.data(),0x27c,0.f);frame(h,nullptr,actor.data(),nullptr);check(displacements==0&&air_starts==1,"dead victim returns carrier Idle during attachment");
 air_reset();auto interrupt=callback<cleanup_t>(h,3);interrupt(h,nullptr,actor.data());check(drops==1,"carry interrupt uses emergency release independently of authored throw");
}
void release_tests(){
 void* h=air_handler("ch_air_grab_pickup_throw");auto enter=callback<entry_t>(h,1);
 for(int c=0;c<6;++c){air_reset();if(c<3||c==5)victim_team=actor_team;if(c==3)victim_team=0x1c;if(c==0||c==2)put<unsigned char>(victim_stats.data(),0x2ad,0x80);if(c==2)friendly=true;if(c==5)put<unsigned char>(ptr(0x70b6fd),0,1);enter(h,nullptr,actor.data(),nullptr,air_context.data());check(requested_name==(c==0?"fastball":c==1||c==3?"jumploop":"thrownupsidedown")&&air_forces==1&&selects==1,"throw same-team capability/friendly/neutral/enemy branches");check(get<u32>(victim.data(),0x770)==0x1111&&get<unsigned char>(victim.data(),0x764)==0x90&&air_published==std::vector<u32>{0x84},"throw marker, bit2, attacker handle");put<unsigned char>(ptr(0x70b6fd),0,0);}
 for(int c=0;c<2;++c){air_reset();if(c)char_target=nullptr;else target_present=false;enter(h,nullptr,actor.data(),nullptr,air_context.data());check(air_starts==1&&air_forces==0,"throw absent or noncharacter target Idles");}
 for(int slot:{2,3}){air_reset();put(actor.data(),4,0x87u);callback<cleanup_t>(h,slot)(h,nullptr,actor.data());check(air_published==std::vector<u32>{0x83},"both throw cleanup slots clear/publish actor bit2 only");}
 h=air_handler("ch_air_grab_victim");auto frame=callback<frame_t>(h,4);
 for(int c=0;c<6;++c){air_reset();if(c==1)put(actor.data(),0x76c,0x3333u);if(c==2)put<unsigned char>(actor.data(),0x3d8,0);if(c==3)put(actor.data(),0xbc,0x1234u);if(c==4)put(actor.data(),4,0x84u);if(c==5)put(victim.data(),0x178,0x9999u);frame(h,nullptr,victim.data(),ptr(0x456));if(c==0)check(air_published==std::vector<u32>{0x82,0x86,0x87}&&air_starts==0&&exclusions==0,"reciprocal airborne flying pair reasserts constraints");else check(air_published==std::vector<u32>{0x80,0x80,0x80}&&exclusions==(c==5?0:2)&&relatives==1&&idle_actors==(c==5?std::vector<void*>{victim.data()}:std::vector<void*>{actor.data(),victim.data()}),"mismatch/flight loss/landing/owner loss detaches and idles pair in source order");}
 // XML1 DC720 (0xDC7B7..0xDC7D6) keeps the pair on the carrier's state alone: the carried
 // one's own ground handle or contacts (082e moving-carry bumps) never release it.
 for(u32 ground:{0x4444u,0x1u}){air_reset();put(victim.data(),0xbc,ground);put<unsigned char>(victim.data(),0x30c,0x84);frame(h,nullptr,victim.data(),ptr(0x456));check(air_published==std::vector<u32>{0x82,0x86,0x87}&&air_starts==0&&exclusions==0&&relatives==0,"victim ground/contact alone keeps the reciprocal airborne pair (carrier-only release rule)");put(victim.data(),0xbc,0u);put<unsigned char>(victim.data(),0x30c,0);}
 for(int c=0;c<2;++c){air_reset();put(victim.data(),4,0x87u);put<unsigned char>(victim.data(),0x3db,c?4:0);callback<cleanup_t>(h,3)(h,nullptr,victim.data());check(air_published==std::vector<u32>{0x85,0x81,0x80}&&aux==(c?2:0)&&relatives==1&&air_starts==0,"victim interrupt clears bits and conditionally both auxiliary states");}
 for(const char* n:{"ch_fly_guard_decide","ch_air_grab_pickup_throw"}){h=air_handler(n);air_reset();unsigned before=0,after=0;__asm mov before,esp
 callback<frame_t>(h,4)(h,nullptr,actor.data(),ptr(0x123));
 __asm mov after,esp
 check(before==after,"default carry slot4 callback obeys PC ret8");}
}

// Execute the real PC input/chain decision and CCEAtkVictim ownership writes.
// Rendering, bone output and collision movement remain deterministic services;
// their setters advance actual fixture actor positions across frames.
void* __fastcall sequence_handler(void*,void*){return sequence_handler_now;}
bool __fastcall sequence_input(void*,void*,void* who,void* input){return callback<update_t>(sequence_carry,6)(sequence_carry,nullptr,who,input);}
unsigned char __fastcall sequence_kind(void*,void*){return 0;}
void __fastcall sequence_exit(void*,void*,void*){}
void __fastcall sequence_transition(void* who,void*,void*,void* next,int,float,int,int,int,float){
 ++sequence_chains;check(who==actor.data()&&next!=nullptr,"native PC dispatcher receives the selected carry node");
 reinterpret_cast<void(__thiscall*)(void*)>(0x42c270)(who);
 put(who,0x378,next);
 callback<entry_t>(sequence_carry,1)(sequence_carry,nullptr,who,air_input.data(),air_context.data());
 put<unsigned char>(who,0x764,get<unsigned char>(who,0x764)&~0x20);
}
u32 sequence_control_site=0x4341bb;
__declspec(naked) void __fastcall sequence_control(void*,void*,void*){__asm {
 push esi
 push edi
 mov esi,ecx
 mov edi,[esp+12]
 jmp dword ptr [sequence_control_site]
}}
__declspec(naked) void sequence_control_done(){__asm {pop edi} __asm {pop esi} __asm {ret 4}}
void sequence_tests(){
 std::array<unsigned char,0x400> fast_move{};put(fast_move.data(),0,move_vt.data());sequence_nodes={move.data(),other.data(),fast_move.data()};
 native(0x4341bb,0x35);jump(0x4341f0,&sequence_control_done);native(0x4315d0,0x9e);jump(0x430d40,&sequence_transition);
 native(0x4daf00,0x430);native(0x4db580,0xc8);native(0x4f9120,0x5c);native(0x680030,4);
 sequence_carry=air_handler("ch_air_grab_pickup_idle");
 void* pickup=air_handler("ch_air_grab_pickup"),*decide=air_handler("ch_fly_guard_decide"),*victim_handler=air_handler("ch_air_grab_victim"),*throw_handler=air_handler("ch_air_grab_pickup_throw");
 const auto saved48=move_vt[0x48/4],saved58=move_vt[0x58/4],saved3c=move_vt[0x3c/4],saved30=move_vt[0x30/4];
 move_vt[0x30/4]=reinterpret_cast<void*>(&sequence_handler);
 move_vt[0x48/4]=reinterpret_cast<void*>(&sequence_input);move_vt[0x58/4]=reinterpret_cast<void*>(&sequence_kind);move_vt[0x3c/4]=reinterpret_cast<void*>(&sequence_exit);
 put(other.data(),0,move_vt.data());
 for(bool held_guard:{false,true})for(bool throwing:{false,true}){
  air_reset();sequence_mode=true;sequence_chains=0;victim_team=actor_team;selected=other.data();
  put(actor.data(),0x76c,0u);put(victim.data(),0x178,0u);put(actor.data(),0x178,0u);put(victim.data(),0x378,move.data());
  put(air_input.data(),0,0x100u);check(descent_probe(actor.data(),nullptr,air_input.data())&&carry_entries==1,"full chain admits genuine Guard contact once");current_flying=false;
  callback<entry_t>(decide,1)(decide,nullptr,actor.data(),air_input.data(),air_context.data());
  callback<update_t>(decide,6)(decide,nullptr,actor.data(),air_input.data());
  check(action_chosen==3&&get<u32>(actor.data(),0x76c)==0x2222,"full chain decide chooses and locks eligible ally");
  callback<entry_t>(pickup,1)(pickup,nullptr,actor.data(),air_input.data(),air_context.data());
  std::array<unsigned char,0x24> event{};
  reinterpret_cast<void*(__thiscall*)(void*)>(0x4f9120)(event.data());put(event.data(),0x18,-1.f);
  reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4daf00)(event.data(),actor.data(),nullptr);
  check(get<u32>(actor.data(),0x178)==0x2222&&get<u32>(victim.data(),0x178)==0x1111,"retail time-zero victim event establishes both owners from empty handles");
  put(air_input.data(),0,held_guard?0x100u:0u);
  sequence_handler_now=pickup;probe_hit=false;
  for(int frame=0;frame<4;++frame){callback<frame_t>(pickup,4)(pickup,nullptr,actor.data(),nullptr);const int old_probes=probes;descent_probe(actor.data(),nullptr,air_input.data());check(probes==old_probes,"held Guard cannot resume native descent during paired pickup even when its contact probe would miss");callback<frame_t>(victim_handler,4)(victim_handler,nullptr,victim.data(),nullptr);}
  check(air_starts==0&&get<u32>(victim.data(),4)==0x87,"pickup remains paired across frames with Guard held or released at contact");
  selected=move.data();put(actor.data(),0x378,move.data());put<void*>(actor.data(),0x3bc,nullptr);put(actor.data(),0x20,V{20,30,130});anim_id=anim_enums::extension_id("ea_flyingcarry_idle_a");
  sequence_handler_now=sequence_carry;
  callback<entry_t>(sequence_carry,1)(sequence_carry,nullptr,actor.data(),air_input.data(),air_context.data());
  sequence_states=true;
  const int established_entries=air_placements;
  for(int frame=0;frame<120;++frame){
   now=frame*.016f;put(actor.data(),0x20,V{20.f+frame*3.f,30.f+frame*2.f,130.f+frame});put(actor.data(),0x1f4,V{0,0,frame*.1f});
   // Real changes between idle and forward/turning/up/down movement nodes.
   // A native victim reset can run after carrier entry/time-zero setup and
   // before either actor's next frame, so test that ordering explicitly.
   const int phase=frame/20;const float speed=phase==0||phase==5?0.f:phase==1||phase==3?75.f:150.f;
   put(air_input.data(),8,speed);put(air_input.data(),0,(held_guard&&phase<2?0x100u:0u)|(phase==3?64u:phase==4?256u:0u));
   anim_id=anim_enums::extension_id(speed==0?"ea_flyingcarry_idle_a":speed<100?"ea_flyingcarry_flyslow_a":"ea_flyingcarry_flyfast_a");
   if(phase==3)put(actor.data(),0x28,180.f+(frame-60));else if(phase==4)put(actor.data(),0x28,200.f-(frame-80));
   sequence_control(actor.data(),nullptr,air_input.data());
   if(frame&&frame%20==0)reinterpret_cast<void(__thiscall*)(void*)>(0x42c270)(victim.data());
   probe_hit=phase!=4;const int previous_probes=probes;const bool consumed=descent_probe(actor.data(),nullptr,air_input.data());
   const bool down=(get<u32>(air_input.data(),0)&0x100)!=0;
   check(probes==previous_probes+(down&&!(held_guard&&phase<2)?1:0)&&carry_entries==1,"established carry retains source descent probe without readmitting decide");
   if(phase==4)check(!consumed,"established carry can descend with Guard through empty airspace");
   callback<frame_t>(sequence_carry,4)(sequence_carry,nullptr,actor.data(),nullptr);
   check(get<u32>(victim.data(),4)==0x87,"carrier frame closes native victim reset gap before physical movement");
   callback<frame_t>(victim_handler,4)(victim_handler,nullptr,victim.data(),nullptr);
   const auto a=get<V>(actor.data(),0x20),v=get<V>(victim.data(),0x20);
   check(close_float(a.x,v.x)&&close_float(a.y,v.y)&&close_float(a.z,v.z)&&get<u32>(victim.data(),0x178)==0x1111,"established victim follows moving carrier in XYZ for every frame");
  }
  check(sequence_chains==5&&air_placements==established_entries+5&&air_starts==0,"120 frames retain pair over five genuine movement transitions and suppress same-node restarts");
  if(throwing){put(air_input.data(),0,16u);selected=get<void*>(actor.data(),0x378)==move.data()?other.data():move.data();check(callback<update_t>(sequence_carry,6)(sequence_carry,nullptr,actor.data(),air_input.data())&&action_chosen==1,"established carry can select throw");callback<entry_t>(throw_handler,1)(throw_handler,nullptr,actor.data(),air_input.data(),air_context.data());}
  else{put(actor.data(),0xbc,1u);callback<frame_t>(victim_handler,4)(victim_handler,nullptr,victim.data(),nullptr);check(air_starts==2,"landing drops and Idles the carried pair");}
  callback<cleanup_t>(victim_handler,3)(victim_handler,nullptr,victim.data());
  put(event.data(),0x10,-1.f);reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4db580)(event.data(),actor.data(),nullptr);
  check(get<u32>(actor.data(),0x76c)==0&&get<u32>(actor.data(),0x178)==0&&get<u32>(victim.data(),0x178)==0&&get<u32>(victim.data(),4)==0x80,"native unvictim and carry cleanup release both owners, selection and constraints");
  sequence_mode=false;
 }
 move_vt[0x48/4]=saved48;move_vt[0x58/4]=saved58;move_vt[0x3c/4]=saved3c;move_vt[0x30/4]=saved30;
}


void held_acquisition_test(){
 auto pickup=air_handler("ch_air_grab_pickup"),carry=air_handler("ch_air_grab_pickup_idle");
 air_reset();sequence_mode=true;sequence_handler_now=pickup;put(air_input.data(),0,0x100u);
 callback<entry_t>(pickup,1)(pickup,nullptr,actor.data(),air_input.data(),air_context.data());
 sequence_handler_now=carry;callback<entry_t>(carry,1)(carry,nullptr,actor.data(),air_input.data(),air_context.data());
 probe_hit=false;selected=move.data();
 for(int frame=0;frame<600;++frame){
  callback<update_t>(carry,6)(carry,nullptr,actor.data(),air_input.data());const int before=probes;
  check(descent_probe(actor.data(),nullptr,air_input.data())&&probes==before,"holding the acquisition Guard through 600 established carry frames consumes descent even when the probe misses");
 }
 put(air_input.data(),0,0u);callback<update_t>(carry,6)(carry,nullptr,actor.data(),air_input.data());
 put(air_input.data(),0,0x100u);const int before=probes;
 check(!descent_probe(actor.data(),nullptr,air_input.data())&&probes==before+1,"a fresh Guard press after release descends through empty air");
 sequence_handler_now=pickup;callback<entry_t>(pickup,1)(pickup,nullptr,actor.data(),air_input.data(),air_context.data());
 put(air_input.data(),0,0u);callback<update_t>(pickup,6)(pickup,nullptr,actor.data(),air_input.data());
 put(air_input.data(),0,0x100u);sequence_handler_now=carry;callback<entry_t>(carry,1)(carry,nullptr,actor.data(),air_input.data(),air_context.data());
 check(!descent_probe(actor.data(),nullptr,air_input.data()),"release during pickup clears acquisition latch before a fresh Guard press at carry entry");
 sequence_mode=false;
}
std::array<unsigned char,16> damage_values{};std::array<void*,4> damage_values_vt{};
void* __cdecl get_damage_values(){return damage_values.data();}
float __fastcall l2(void*,void*,int index,int level){check(index==1&&level==0,"carry uses the native L2 value selector");return 12.f;}
float __fastcall throw_scale(void* stats,void*,int id,void* who,int add){check(stats==get<void*>(actor.data(),0x35c)&&id==0&&who==actor.data()&&add==0,"carry L2 multiplier uses caster stats and native ABI");return 1.5f;}
V* __fastcall event_zero(V* out,void*){*out={};return out;}
V* __fastcall event_copy(V* out,void*,const V* in){*out=*in;return out;}
V* __fastcall event_scaled(V* out,void*,float scale,const V* in){*out={scale*in->x,scale*in->y,scale*in->z};return out;}
float* __fastcall event_component(V* out,void*,int i){return &out->x+i;}
V* __fastcall event_sub(V* out,void*,const V* in){out->x-=in->x;out->y-=in->y;out->z-=in->z;return out;}
void __fastcall event_velocity(void* who,void*,const V* v){put(who,0x120,*v);}
void __fastcall event_tick(void*,void*,float,int){}
void __fastcall event_exclusion(void*,void*,u32 handle,float duration){++exclusions;check(handle==0&&duration==.5f,"retail victim_throw preserves authored half-second ownership exclusion");}
void carry_damage_test(){
 native(0x4db830,0x2d0);native(0x680030,4);
 put(ptr(0x67fa60),0,reinterpret_cast<void*>(&event_zero));put(ptr(0x67fa2c),0,reinterpret_cast<void*>(&event_copy));
 put(ptr(0x67f920),0,reinterpret_cast<void*>(&event_scaled));put(ptr(0x67f9c4),0,reinterpret_cast<void*>(&event_component));put(ptr(0x67f91c),0,reinterpret_cast<void*>(&event_sub));
 jump(0x473640,&event_velocity);jump(0x4c4bb0,&get_damage_values);jump(0x4b7970,&throw_scale);
 put(damage_values.data(),0,damage_values_vt.data());damage_values_vt[3]=reinterpret_cast<void*>(&l2);
 auto execute=reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(get<void*>(ptr(0x692378),0));
 std::array<unsigned char,0x30> event{};
 const auto old_exclusion=actor_vt[0x104/4];actor_vt[0x104/4]=reinterpret_cast<void*>(&event_exclusion);
 for(bool smash:{false,true})for(int relation:{0,1,2,3}){
  air_reset();sequence_mode=event_mode=true;sequence_handler_now=air_handler("ch_air_grab_pickup_throw");
  actor_vt[0xf0/4]=reinterpret_cast<void*>(&event_tick);put(victim.data(),0x2b8,0.f);put(victim.data(),0x2f0,0.f);
  victim_team=relation==1?actor_team:relation==2?0x1c:2;friendly=relation==3;
  if(friendly)victim_team=actor_team;
  event.fill(0);put(event.data(),0x14,0.f);put(event.data(),0x18,smash?0.f:600.f);put(event.data(),0x2c,.5f);
  put(event.data(),0x1c,1u);put(event.data(),0x20,1u);put(event.data(),0x24,1u);
  unsigned before=0,after=0;__asm mov before,esp
  execute(event.data(),actor.data(),nullptr);
  __asm mov after,esp
  const bool damaging=relation==0||relation==3;
  check(before==after&&get<u32>(actor.data(),0x76c)==0,"real retail victim_throw executes once and releases selection with balanced stack");
  check(get<float>(victim.data(),0x2b8)==(damaging?13.f:0.f),"round3 carry Attack/Smash use XML1 L2 minimum nine times caster scale truncated to source word");
  check(get<u32>(victim.data(),4)==0x80&&relatives==1,"carry trigger restores all source release constraints and detaches once");
  if(damaging)check(get<float>(victim.data(),0x2f0)==now+2.f&&get<u32>(victim.data(),0x2b4)==0x1111&&(get<unsigned char>(victim.data(),0x2f8)&1),"carry throw arms source-length deadline and caster attribution");
  const auto v=get<V>(victim.data(),0x120);check(smash?v.z==0.f:v.x==600.f,"carry damage adaptation preserves parsed source motion (Smash velocityup is ignored in both binaries)");
  sequence_mode=event_mode=false;
 }
 // 082e 10.4 (live-082e evidence/throws.json): one throw armed by this event reached the
 // tunnel's end wall (x 334) and cost a "Physical Resistant" Brotherhood Revolutionary 4 HP;
 // the open-floor Attack and both Smashes cost 0. The armed amount is consumed only by a
 // contact impact (499100); the authored Smash has no velocity (above). Downstream, the
 // receive stages (ReceiveRounding=xml1, default.xbe 0x45517..0x45558) take that amount
 // through defense then the def_damage scale (physical_resistant: a 20% reduction here;
 // the live soldier's defense was not captured, so 3-4 is an example, not a measurement).
 // The amount and the stages are device independent: pad and keyboard reach this handler
 // as the same input bits (Attack bit4 -> action 1, Smash bit5 -> action 2).
 for(float multiplier:{1.f,1.5f}){
  const std::int32_t armed=static_cast<std::int16_t>(static_cast<int>(9.f*multiplier));
  for(float defense:{0.f,3.f,4.f}){
   const auto resistant=balance_parity_rules::receive_xml1(armed,defense,0.8f),plain=balance_parity_rules::receive_xml1(armed,defense,1.f);
   check(resistant<=plain&&plain==static_cast<std::int32_t>(armed-defense),"resistant target never loses more than an unresisted one from the same armed throw");
   if(multiplier==1.f&&defense>=3.f)check(resistant==4,"9 x 1.0 through defense 3-4 and a 0.8 resistance scale is consistent with the live wall hit (100 -> 96)");
  }
 }
 {
  void* idle_handler=air_handler("ch_air_grab_pickup_idle");auto update=callback<update_t>(idle_handler,6);
  for(u32 bits:{1u<<4,1u<<5}){
   int chosen[2]{};
   for(int device=0;device<2;++device){air_reset();put(air_input.data(),0,bits);put(air_input.data(),8,device?1.f:.73f);update(idle_handler,nullptr,actor.data(),air_input.data());chosen[device]=action_chosen;}
   check(chosen[0]==chosen[1]&&chosen[0]==(bits==(1u<<4)?1:2),"carry Attack/Smash select the same throw action from pad and keyboard input");
  }
 }
 for(int negative:{0,1,2}){
  air_reset();sequence_mode=event_mode=true;sequence_handler_now=air_handler(negative==0?"ch_air_grab_pickup_idle":"ch_air_grab_pickup_throw");
  actor_vt[0xf0/4]=reinterpret_cast<void*>(&event_tick);put(victim.data(),0x2b8,0.f);put(victim.data(),0x2f0,0.f);
  if(negative==1)target_present=false;if(negative==2)move_present=false;
  execute(event.data(),actor.data(),nullptr);
  check(get<float>(victim.data(),0x2b8)==0&&get<float>(victim.data(),0x2f0)==0&&relatives==0,"unrelated/absent-victim/failed-native-event paths never arm or detach a carry throw");
  sequence_mode=event_mode=false;
 }
 actor_vt[0x104/4]=old_exclusion;air_bind();
}



void fastball_authored_event_test(){
 // XML1 shared_nodes.eng authors throwally playspeed=1.25 and victim_throw
 // time=.5, forwardvelocity=600, upvelocity=100. There is no Attack gate.
 air_reset();sequence_mode=true;event_mode=true;move_name("throwally");
 actor_vt[0xf0/4]=reinterpret_cast<void*>(&event_tick);
 const auto old_exclusion=actor_vt[0x104/4];actor_vt[0x104/4]=reinterpret_cast<void*>(&event_exclusion);
 put(victim.data(),4,0x87u);put(victim.data(),0x178,0x1111u);
 for(int frame=0;frame<24;++frame){now=frame/60.f;get<frame_t>(ptr(victim_vtable),16)(nullptr,nullptr,victim.data(),nullptr);check(get<u32>(victim.data(),4)==0x87&&relatives==0&&starts==0,"converted ally victim remains held on every callback before the authored .5 animation-time event");}
 std::array<unsigned char,0x30> event{};put(event.data(),0x14,100.f);put(event.data(),0x18,600.f);put(event.data(),0x2c,.5f);put(event.data(),0x1c,1u);put(event.data(),0x20,1u);put(event.data(),0x24,1u);
 now=.4f;reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4db830)(event.data(),actor.data(),nullptr);
 const auto velocity=get<V>(victim.data(),0x120);
 check(get<u32>(actor.data(),0x76c)==0&&velocity.x==600.f&&velocity.z==100.f,"actual retail event launches Fastball automatically with authored velocity and releases selection without Attack");
 actor_vt[0x104/4]=old_exclusion;sequence_mode=event_mode=false;
}

bool __fastcall infinite_energy(void*,void*,int query){check(query==1,"native player energy getter queries the infinite-energy switch");return false;}
void finite_energy_hold_test(){
 auto carry=air_handler("ch_air_grab_pickup_idle");
 // Keep the native clamp/store calculation. Only descriptor lookup, UI and
 // the terminal setter are outside this fixture's boundary.
 native(0x4319b5,0x56);native(0x680030,4);native(0x41d5d0,0x41);put<unsigned char>(ptr(0x70b705),0,0);jump(0x431a0b,&debit_done);
 put<unsigned char>(ptr(0x70b704),0,0);jump(0x44cce0,&manager);const auto old_switch=manager_vt[2];manager_vt[2]=reinterpret_cast<void*>(&infinite_energy);
 const auto old_setter=actor_vt[0x1a8/4];actor_vt[0x1a8/4]=reinterpret_cast<void*>(&energy_set);finite_debit=true;
 for(float initial:{40.f,60.f,200.f,400.f}){
  air_reset();actor_team=0x1d;energy=initial;put(actor.data(),0x288,initial);put(actor.data(),0x314,1000.f);put(actor.data(),0x6a4,1.f);put(air_input.data(),0,0x100u);
  int landing=-1;
  for(int frame=0;frame<(initial==400.f?600:240);++frame){
   now=2.f+frame*(initial==400.f?1.f/60.f:.016f);callback<frame_t>(carry,4)(carry,nullptr,actor.data(),air_input.data());
   if(last_flight==0){landing=frame;break;}
  }
  if(initial==40.f)check(landing>=63&&landing<=66,"a real 40-energy budget lands about one second after pickup while Guard remains held");
  if(initial==60.f)check(landing>=126&&landing<=130,"a real 60-energy budget reproduces the two-second held carry break");
  if(initial==200.f)check(landing==-1&&energy==120.f,"a verified sufficient budget survives four seconds of established held carry");
  if(initial==400.f)check(landing==-1&&energy==200.f,"a finite Flight3 budget supports ten seconds of carry with ten real upkeep debits");
 }
 finite_debit=false;actor_vt[0x1a8/4]=old_setter;manager_vt[2]=old_switch;jump(0x41d5d0,&air_energy);
}

std::array<unsigned char,0xc700> shortcut_state{};
std::array<u32,4> shortcut_buttons{};
bool __fastcall shortcut_enabled(void*,void*){return true;}
void* __cdecl shortcut_input(){return shortcut_state.data();}
void native_shortcut_admission_test(){
 air_reset();native(0x61a3a0,0x32);jump(0x551ed0,&manager);jump(0x55a390,&shortcut_input);
 for(unsigned i=0;i+4<=0x32;++i)if(get<u32>(ptr(0x61a3a0),i)==0xa6ad58)put(ptr(0x61a3a0),i,u32(reinterpret_cast<std::uintptr_t>(shortcut_buttons.data())));
 auto saved=manager_vt[0x28/4];manager_vt[0x28/4]=reinterpret_cast<void*>(&shortcut_enabled);
 shortcut_buttons[0]=0x40u;put(air_input.data(),0,0x100u);
 check(reinterpret_cast<bool(__cdecl*)(int)>(0x61a3a0)(0),"retail PC shortcut predicate really returns true in this input state");
 check(descent_probe(actor.data(),nullptr,air_input.data())&&probes==1&&carry_entries==1,"XML1 Guard admission survives the PC-only shortcut refusal that precedes the old hook");
 air_reset();put(air_input.data(),0,0x180u);descent_probe(actor.data(),nullptr,air_input.data());check(probes==0&&carry_entries==0,"real power modifier still blocks Guard carry admission");
 manager_vt[0x28/4]=saved;air_bind();
}
void native_entry_pose_test(){
 air_reset();native(0x500640,7);native(0x500840,7);native(0x4f5a30,0x70);
 const auto old30=move_vt[0x30/4],old_e8=move_vt[0xe8/4];
 move_vt[0x30/4]=ptr(0x500640);move_vt[0xe8/4]=ptr(0x500840);
 put(other.data(),0,move_vt.data());put(other.data(),0xc8,air_handler("ch_air_grab_pickup_idle"));put<unsigned char>(other.data(),0x146,0);
 put(actor.data(),0x378,other.data());put(air_context.data(),4,other.data());put(move.data(),0xbc,anim_enums::extension_id("ea_flyingcarry_idle_a"));
 unsigned before=0,after=0;__asm mov before,esp
 reinterpret_cast<void(__thiscall*)(void*,void*,void*,void*)>(0x4f5a30)(other.data(),actor.data(),air_input.data(),air_context.data());
 __asm mov after,esp
 // Literal independently derived hand midpoints: carrier(20,40,60),
 // victim(10,20,30), victim root zero => desired root(10,20,30).
 check(before==after&&last_position.x==10&&last_position.y==20&&last_position.z==30,"native carry entry aligns the hand socket instead of resetting the victim to pelvis-minus-55");
 move_vt[0x30/4]=old30;move_vt[0xe8/4]=old_e8;air_bind();
}

struct Matrix{float f[16];};
std::array<unsigned char,16> render{},skeleton{},bone_model_data{};
std::array<void*,32> render_vt{},skeleton_vt{};
int native_bone_reads=0;
Matrix* __fastcall matrix_identity(Matrix* m,void*){std::fill_n(m->f,16,0.f);m->f[0]=m->f[5]=m->f[10]=m->f[15]=1.f;return m;}
V* __fastcall vector_construct(V* v,void*,float x,float y,float z){*v={x,y,z};return v;}
void __fastcall matrix_translation(Matrix* m,void*,const V* v){matrix_identity(m,nullptr);m->f[12]=v->x;m->f[13]=v->y;m->f[14]=v->z;}
void __fastcall matrix_position(const Matrix* m,void*,V* v){*v={m->f[12],m->f[13],m->f[14]};}
void* __fastcall bone_model(void*,void*){return bone_model_data.data();}
void* __fastcall bone_skeleton(void*,void*){return skeleton.data();}
void* __fastcall no_transform(void*,void*){return nullptr;}
bool __fastcall skeleton_bone(void*,void*,int id,Matrix* out){++native_bone_reads;V v{id==5?-10.f:10.f,0,id==0?85.f:65.f};matrix_translation(out,nullptr,&v);return true;}
void native_bone_tests(){
 // Execute every retail layer down to the renderer's real bone getter.
 // Only renderer matrices are invented; no fake chooses the ABI flags.
 air_reset();const auto saved188=actor_vt[0x188/4];actor_vt[0x188/4]=reinterpret_cast<void*>(&bone_model);
 put(bone_model_data.data(),0,render.data());put(render.data(),0,render_vt.data());put(skeleton.data(),0,skeleton_vt.data());
 render_vt[0x14/4]=reinterpret_cast<void*>(&bone_skeleton);render_vt[8/4]=reinterpret_cast<void*>(&no_transform);skeleton_vt[0x64/4]=reinterpret_cast<void*>(&skeleton_bone);
 native(0x41d620,0x5f);native(0x569370,0x4b);native(0x5692e0,0x90);native(0x568e90,0x448);
 put(ptr(0x67fa28),0,reinterpret_cast<void*>(&matrix_identity));put(ptr(0x67fa1c),0,reinterpret_cast<void*>(&vector_construct));put(ptr(0x67fa94),0,reinterpret_cast<void*>(&matrix_translation));put(ptr(0x67fa00),0,reinterpret_cast<void*>(&matrix_position));
 put(actor.data(),4,0x10080u);native_bone_reads=0;
 auto h=air_handler("ch_air_grab_pickup_idle");callback<entry_t>(h,1)(h,nullptr,actor.data(),nullptr,air_context.data());
 check(native_bone_reads==1&&last_position.z==30.f,"retail carry bone0 reaches animated skeleton (85-55), not approximate root-30");
 actor_vt[0x188/4]=saved188;air_bind();
}

std::array<std::array<unsigned char,16>,2> world_models{},world_renders{},world_skeletons{},world_objects{};
std::array<void*,8> world_render_vt{},world_object_vt{};std::array<void*,32> world_skeleton_vt{};
void* __fastcall world_model(void* who,void*){return world_models[who==actor.data()?0:1].data();}
void* __fastcall world_skeleton(void* who,void*){return world_skeletons[who==world_renders[0].data()?0:1].data();}
void* __fastcall world_object(void* who,void*){return world_objects[who==world_renders[0].data()?0:1].data();}
void __fastcall world_matrix(void* who,void*,Matrix* out,int zero){
 check(zero==0,"native renderer requests the world matrix without extra pose flags");
 const bool carrier=who==world_objects[0].data();void* a=carrier?actor.data():victim.data();
 const auto position=get<V>(a,0x20);matrix_translation(out,nullptr,&position);
 // Independently specified animated renderer transforms: carrier +90deg,
 // victim 180deg. The renderer owns this sampled pose during the callback.
 out->f[0]=out->f[5]=carrier?0.f:-1.f;out->f[1]=carrier?1.f:0.f;out->f[4]=carrier?-1.f:0.f;
}
bool __fastcall world_bone(void* who,void*,int id,Matrix* out){
 check(id==5||id==6,"real animated path requests LHand/RHand selectors from the common bone table");
 const bool carrier=who==world_skeletons[0].data();const V mid=carrier?V{20,0,50}:V{5,10,40};
 const V point{mid.x+(id==5?-2.f:2.f),mid.y,mid.z};matrix_translation(out,nullptr,&point);return true;
}
Matrix* __fastcall matrix_product(Matrix* out,void*,const Matrix* a,const Matrix* b){
 Matrix result{};for(int row=0;row<4;++row)for(int col=0;col<4;++col)for(int k=0;k<4;++k)result.f[row*4+col]+=a->f[row*4+k]*b->f[k*4+col];*out=result;return out;
}
V* __fastcall pose_add(V* out,void*,const V* in){out->x+=in->x;out->y+=in->y;out->z+=in->z;return out;}
float __fastcall pose_component(const V* v,void*,int i){return (&v->x)[i];}
void* __fastcall no_powerup(void*,void*,void*){return nullptr;}
void native_world_pose_test(){
 air_reset();sequence_mode=true;flight_rank=3;energy=400.f;finite_debit=true;
 const auto saved_energy_set=actor_vt[0x1a8/4];actor_vt[0x1a8/4]=reinterpret_cast<void*>(&energy_set);
 const auto saved188=actor_vt[0x188/4],saved30=move_vt[0x30/4],saved28=move_vt[0x28/4],savede8=move_vt[0xe8/4];
 const auto saved130=actor_vt[0x130/4],saved158=actor_vt[0x158/4],saved34=actor_vt[0x34/4],saved150=actor_vt[0x150/4];
 actor_vt[0x188/4]=reinterpret_cast<void*>(&world_model);move_vt[0x30/4]=ptr(0x500640);move_vt[0xe8/4]=ptr(0x500840);move_vt[0x28/4]=reinterpret_cast<void*>(&no_powerup);
 for(int i=0;i<2;++i){put(world_models[i].data(),0,world_renders[i].data());put(world_renders[i].data(),0,world_render_vt.data());put(world_skeletons[i].data(),0,world_skeleton_vt.data());put(world_objects[i].data(),0,world_object_vt.data());}
 world_render_vt[5]=reinterpret_cast<void*>(&world_skeleton);world_render_vt[2]=reinterpret_cast<void*>(&world_object);world_skeleton_vt[25]=reinterpret_cast<void*>(&world_bone);world_object_vt[4]=reinterpret_cast<void*>(&world_matrix);
 native(0x41d620,0x5f);native(0x569370,0x4b);native(0x5692e0,0x90);native(0x568e90,0x448);native(0x500640,7);native(0x500840,7);native(0x4f5a30,0x70);native(0x4f5cf0,0x187);
 // Include all three bytes of each ret8, not only its opcode.
 native(0x42d450,0xfb);native(0x41c400,0x2b);native(0x41c430,0x33);
 actor_vt[0x130/4]=ptr(0x42d450);actor_vt[0x158/4]=ptr(0x41c430);actor_vt[0x34/4]=ptr(0x41c400);actor_vt[0x150/4]=reinterpret_cast<void*>(&air_position);
 put(ptr(0x67fa2c),0,reinterpret_cast<void*>(&event_copy));put(ptr(0x67f9e0),0,reinterpret_cast<void*>(&event_copy));put(ptr(0x67f9f4),0,reinterpret_cast<void*>(&pose_add));put(ptr(0x67f9c4),0,reinterpret_cast<void*>(&event_component));put(ptr(0x67f9d8),0,reinterpret_cast<void*>(&pose_component));
 put(ptr(0x67fa28),0,reinterpret_cast<void*>(&matrix_identity));put(ptr(0x67fa1c),0,reinterpret_cast<void*>(&vector_construct));put(ptr(0x67fa94),0,reinterpret_cast<void*>(&matrix_translation));put(ptr(0x67fa00),0,reinterpret_cast<void*>(&matrix_position));put(ptr(0x67f8c0),0,reinterpret_cast<void*>(&matrix_product));
 put(actor.data(),4,0x10080u);put(victim.data(),4,0x10080u);put(actor.data(),0x20,V{100,200,300});put(victim.data(),0x20,V{10,20,30});
 put(other.data(),0,move_vt.data());put(other.data(),0xc8,air_handler("ch_air_grab_pickup_idle"));put<unsigned char>(other.data(),0x146,0);put(actor.data(),0x378,other.data());put(air_context.data(),4,other.data());put(move.data(),0xbc,anim_enums::extension_id("ea_flyingcarry_idle_a"));
 reinterpret_cast<void(__thiscall*)(void*,void*,void*,void*)>(0x4f5a30)(other.data(),actor.data(),air_input.data(),air_context.data());
 auto v=get<V>(victim.data(),0x20);check(v.x==105&&v.y==230&&v.z==310,"real CMove entry plus renderer world transforms aligns hand midpoints at (100,220,350)");
 for(int frame=0;frame<600;++frame){now=2.f+frame/60.f;put(actor.data(),0x20,V{100.f+frame,200.f+2*frame,300.f+frame});reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4f5cf0)(other.data(),actor.data(),air_input.data());v=get<V>(victim.data(),0x20);check(v.x==105.f+frame&&v.y==230.f+2*frame&&v.z==310.f+frame,"600 moving carry frames keep native CMove/world hand attachment aligned");}
 check(energy==200.f&&debits==10&&air_starts==0,"ten simulated seconds of moving native hand attachment retain the pair with ten finite Flight3 debits");
 finite_debit=false;actor_vt[0x1a8/4]=saved_energy_set;
 actor_vt[0x188/4]=saved188;actor_vt[0x130/4]=saved130;actor_vt[0x158/4]=saved158;actor_vt[0x34/4]=saved34;actor_vt[0x150/4]=saved150;move_vt[0x30/4]=saved30;move_vt[0x28/4]=saved28;move_vt[0xe8/4]=savede8;sequence_mode=false;air_bind();
}

void* __fastcall native_input_chain(void* who,void*,int action){
 check(who==actor.data(),"native generic input resolves actions on the carrier");
 if(action==0)return get<void*>(who,0x378);
 if(action==9||action==11)return other.data();
 return nullptr;
}
__declspec(naked) void cookie_check(){__asm {ret}}
void native_generic_stick_test(){
 native(0x4fce50,0x8b0);native(0x6dc35c,0x40);native(0x6f38f8,4);native(0x680030,4);jump(0x672161,&cookie_check);jump(0x427c80,&native_input_chain);
 auto carry=air_handler("ch_air_grab_pickup_idle");
 // Human input magnitude is normalized here; PC physics scales it later.
 // The old fixture used magnitude 50/150 and parameter2=1 instead.
 for(bool shortcut:{false,true})for(float stick:{0.f,.15f,.5f,1.f}){
  air_reset();native_descent_blocked=shortcut;parameter2=500.f;parameter8=100.f;parameter9=300.f;put(air_input.data(),0,0x100u);put(air_input.data(),4,0u);put(air_input.data(),8,stick);
  put(other.data(),0,move_vt.data());
  const auto old_flags=get<unsigned char>(actor.data(),0x764);
  const bool changed=callback<update_t>(carry,6)(carry,nullptr,actor.data(),air_input.data());
  check(changed==(stick>.2f)&&get<void*>(actor.data(),0x3bc)==(stick>.2f?other.data():move.data()),"native generic side effects plus normalized held-Guard stick input preserve the selected carry chain");
  check(get<unsigned char>(actor.data(),0x764)==old_flags&&drops==0&&air_starts==0,"native generic held/stick path never interrupts or drops the pair");
 }
 air_bind();
}

int expression_parses=0;
void __fastcall expression_parse(void* expression,void*,const char* value){++expression_parses;check(expression==move.data()+0xb8&&value!=nullptr,"non-flight expression uses native field/parser ABI");}
u32 expression_entry=0x4f74b2;
// Invented caller frame for a parsed attribute: four saved nonvolatile regs,
// then return address and two args, matching the interior parser's live frame.
__declspec(naked) bool __fastcall expression_probe(void*,void*,const char*,const char*){
 __asm {
  push ebx
  push ebp
  push esi
  push edi
  mov esi, ecx
  jmp dword ptr [expression_entry]
 }
}
void powerup_tests(){
 jump(0x4dbea0,&expression_parse);
 for(const char* value:{"%flight_pwr","%FLIGHT_PWR","%other_power","20","0","%flight_pwr_suffix"}){
  expression_parses=0;put<unsigned char>(move.data(),0x146,0x85);put(move.data(),0xb8,0x12345678u);
  check(expression_probe(move.data(),nullptr,"energypersecond",value),"per-second parser succeeds with original ret8");
  const bool flight=_stricmp(value,"%flight_pwr")==0;
  check(expression_parses==(flight?0:1)&&get<unsigned char>(move.data(),0x146)==(flight?0x85:0xc5),"only flight_pwr parser flag/debit suppressed; other power drains stay native");
  check(get<u32>(move.data(),0xb8)==0x12345678u,"suppression leaves expression storage and unrelated bits alone");
 }
}
void round3_contact_pose_tests(){
 air_reset();sequence_mode=true;selected=move.data();put(air_input.data(),0,0x100u);
 put(actor.data(),0x20,V{10,20,73});put(victim.data(),0x20,V{10,20,3});
 check(descent_probe(actor.data(),nullptr,air_input.data()),"round3 descending flight contact is consumed");
 check(get<V>(actor.data(),0x20).z==43.f&&get<V>(victim.data(),0x20).z==3.f,"round3 carrier descends to authored pickup height before moving the victim");
 contact_exists=false; // The forward sweep misses a target directly below.
 auto* decide=air_handler("ch_fly_guard_decide");
 callback<update_t>(decide,6)(decide,nullptr,actor.data(),air_input.data());
 check(action_chosen==3&&locks==1&&queries==0,"round3 carry decision retains the full-token downward contact instead of losing it in a second sweep");
 sequence_mode=false;
 air_reset();sequence_mode=true;clearance_blocked=true;put(air_input.data(),0,0x100u);
 put(actor.data(),0x20,V{10,20,73});put(victim.data(),0x20,V{10,20,3});
 descent_probe(actor.data(),nullptr,air_input.data());
 check(carry_entries==0&&get<V>(actor.data(),0x20).z==73.f&&get<V>(victim.data(),0x20).z==3.f,"round3 solid-world obstruction prevents lowering and pickup instead of pulling target through it");
 sequence_mode=false;
 for(bool ally:{false,true}){
  air_reset();flight_rank=6;victim_team=ally?actor_team:2;round3_pose=true;
  native(0x404030,4);move_vt[0xd0/4]=ptr(0x404030);move_name(ally?"airpickupidleally":"airpickupidlevictim");
  put(other.data(),8,1u);
  put(other.data(),0,move_vt.data());put(victim.data(),0x378,other.data());
  auto* carry=air_handler("ch_air_grab_pickup_idle");
  callback<entry_t>(carry,1)(carry,nullptr,actor.data(),air_input.data(),air_context.data());
  check(last_position.z==30.f,"round3 name-selected carry animation enum minus one attaches to actual hands on entry");
  callback<frame_t>(carry,4)(carry,nullptr,actor.data(),nullptr);
  check(last_pose.position.z==30.f,"round3 name-selected ally and enemy poses keep hand attachment on frame");
  move_name("airgrabbreakvictim");
  callback<frame_t>(carry,4)(carry,nullptr,actor.data(),nullptr);
  check(last_pose.position.z==245.f,"round3 unrecognized/break pose retains source base attachment");
  round3_pose=false;
  move_vt[0xd0/4]=reinterpret_cast<void*>(&flight_name);
 }
}
void live_character_clearance_test(){
 air_reset();sequence_mode=true;selected=move.data();put(air_input.data(),0,0x100u);
 put(actor.data(),0x20,V{10,20,73});put(victim.data(),0x20,V{10,20,3});put(victim.data(),4,0x180u);
 put(actor.data(),0x178,0x3333u);live_character_clearance=true;
 descent_probe(actor.data(),nullptr,air_input.data());
 check(carry_entries==1&&get<V>(actor.data(),0x20).z==43.f&&get<V>(victim.data(),0x20).z==3.f,"PC property8 victim must be excluded explicitly from lowering probe, not treated as a solid ledge");
 check(get<u32>(actor.data(),0x178)==0x3333u,"lowering probe restores the original physical owner after its scoped exclusion");
 live_character_clearance=false;sequence_mode=false;
 callback<cleanup_t>(air_handler("ch_fly_guard_decide"),3)(nullptr,nullptr,actor.data());
}
void live_tk_victim_order_test(){
 // Selected1CC1 -> owner power1 -> victim callback -> telekinesis_hold.
 xml1_grab_carry::set_telekinesis_pair_test(&tk_guard_bridges::owns_tk_pair);
 for(int state:{0,1,2}){
 air_reset();move_name("power1");requested_name="idle";put(victim.data(),4,0x87u);put<unsigned char>(actor.data(),0x259,2);
 tk_guard_bridges::tk_state held_state;held_state.victim=state==1?0x12222:0x2222;if(state!=2)tk_guard_bridges::tk_states[0x1111]=held_state;
 auto frame=get<frame_t>(ptr(0x693360),16);
 frame(nullptr,nullptr,victim.data(),nullptr);
 if(!state)check(get<u32>(victim.data(),4)==0x87&&relatives==0&&air_starts==0,"live selected TK victim retains constraints during power1 before telekinesis_hold");
 else check(get<u32>(victim.data(),4)==0x80&&relatives==1&&air_starts==1,"recycled victim generation or completed/cancelled TK ownership releases constraints on the next callback");
 tk_guard_bridges::tk_states.clear();
 }
}
#include "carry_native_probe_fixture.hpp"
void flight_tests(){
 const auto enter=get<entry_t>(ptr(0x6931f8),4),move_enter=get<entry_t>(ptr(0x6932d0),4);
 const auto tick=get<update_t>(ptr(0x6931f8),24);
 // The installed native frame calls the new central cdecl upkeep helper.
 auto upkeep=reinterpret_cast<void(__cdecl*)(void*)>(0x4ec7a0);
 for(auto e:{enter,move_enter})for(int c=0;c<4;++c){air_reset();map_restricted=c<2;old_map_bit=true;if(c==1)put<unsigned char>(actor.data(),0x3d8,5);if(c==3)put<unsigned char>(actor.data(),0x3d8,0);put<unsigned char>(air_context.data(),0x6c,0x80);e(nullptr,nullptr,actor.data(),air_input.data(),air_context.data());check(air_starts==(c<2?1:0)&&notifications==(c==0?1:0)&&flight_sets==(c<2?0:1)&&preparations==(c==3?1:0),"fix7 mapped map8c/default-true restriction, AI notification skip, source preparation");check(get<unsigned char>(air_context.data(),0x6c)==0x80,"old PC map44 refusal does not set context failure");}
 for(float speed:{0.f,0.2f,0.21f,0.6f,0.61f}){air_reset();anim_id=1;put(air_input.data(),8,speed);enter(nullptr,nullptr,actor.data(),air_input.data(),air_context.data());check(get<int>(air_context.data(),0)==(speed>0.6f?0x13:speed>0.2f?0x12:0x11),"source flight entry animation thresholds");}
 for(u32 buttons:{0x100u,0x08u}){air_reset();put(air_input.data(),0,buttons);check(tick(nullptr,nullptr,actor.data(),air_input.data())&&carry_entries==0&&generic_calls==1&&get<void*>(actor.data(),0x3bc)==nullptr,"flying update retains native generic input before descent");}
 for(int c=0;c<13;++c){air_reset();put(air_input.data(),0,c==1?0x08u:c==2?0x180u:0x100u);
  if(c==3)flight_rank=2;if(c==4)probe_hit=false;if(c==5)contact_exists=false;if(c==6)char_target=nullptr;
  if(c==7)put<unsigned char>(victim_stats.data(),0x2ad,8);if(c==8)put(ptr(0x70b814),0,0x3333u);
  if(c==9)current_flying=false;if(c==10)native_descent_blocked=true;if(c==11)put(actor.data(),0x3d4,-1);if(c==12)move_present=false;
  const bool handled=descent_probe(actor.data(),nullptr,air_input.data());check(carry_entries==((c==0||c==10)?1:0),"Guard 0x100 admission; 0x08/power/rank2/miss/noncharacter/large/inactive/move/native refusal negatives");
  if(c==3||c==9)check(handled&&probes==1,"source valid contact suppresses descent even when rank/current move prevents pickup");
  if(c==4||c==7)check(!handled,"miss or large contact retains native descent");
  if(c==12)check(handled&&descent_forces==1&&flight_sets==1,"missing decide node retains source null-force and flight path");
  if(c==0)check(probes==1&&flight_sets==1&&last_flight==1,"successful downward contact enters decide and keeps flight without emission");
 }
 air_reset();check(tick(nullptr,nullptr,actor.data(),air_input.data())&&generic_calls==1&&flight_sets==0,"flying wrapper retains native airborne generic input");
 air_reset();put(actor.data(),0xbc,0x3333u);check(tick(nullptr,nullptr,actor.data(),air_input.data())&&last_flight==0&&get<void*>(actor.data(),0x3bc)==idle.data(),"native landing tail selects Idle and disables flight");
 for(int on=0;on<2;++on){air_reset();flight_rank=0;put<unsigned char>(actor.data(),0x3d8,on?4:0);get<frame_t>(ptr(0x6931f8),16)(nullptr,nullptr,actor.data(),ptr(0x123));check(air_starts==on,"native flight frame dispatches central upkeep only while flying, ret8");}
 check(get<void*>(ptr(0x6932d0),24)==ptr(0x4ecf30),"fly_move matching native landing/input path retained");
 constexpr float costs[]{40,30,20,10,5};
 for(int r=1;r<=5;++r)for(int c=0;c<4;++c){air_reset();flight_rank=r;energy=costs[r-1];now=c==0?1.f:c==1?1.0001f:100.f;if(c==3)energy-=1;upkeep(actor.data());check(debits==((c==1||c==2)?1:0)&&air_starts==(c==3?1:0)&&last_flight==(c==3?0:-1),"rank cost exact threshold, strict1s, insufficient energy cancellation");if(debits)check(last_debit==costs[r-1]&&get<float>(actor.data(),0x6a4)==now&&get<float>(actor.data(),0x6a0)==now,"one cost and both timestamps advance without catch-up");}
 for(int r:{0,6}){air_reset();flight_rank=r;now=100;upkeep(actor.data());check(debits==0&&air_starts==(r==0?1:0)&&get<float>(actor.data(),0x6a4)==0,"rank0 cancels and above5 bypasses upkeep");}
 air_reset();put<unsigned char>(actor.data(),0x3d8,5);now=3;upkeep(actor.data());check(debits==0&&get<float>(actor.data(),0x6a4)==3&&get<float>(actor.data(),0x6a0)==3,"AI skips debit while advancing both timestamps");
 air_reset();mode_c=true;mode_value=2;now=3;energy=20;upkeep(actor.data());check(debits==1&&last_debit==120&&air_starts==0,"special mode120 substituted after base energy admission");
 air_reset();now=10;upkeep(actor.data());upkeep(actor.data());check(debits==1,"same callback time never double debits");
 // All new arithmetic must be independent of the caller's FP settings and
 // restore both settings exactly (including non-default MXCSR FTZ/rounding).
 std::array<u32,3> pickup_bits{};bool first_fp=true;
 for(unsigned pc:{_PC_24,_PC_53})for(unsigned rc:{_RC_NEAR,_RC_DOWN,_RC_UP,_RC_CHOP}){
  unsigned saved=0,ignored=0;_controlfp_s(&saved,0,0);_controlfp_s(&ignored,pc|rc,_MCW_PC|_MCW_RC);const unsigned csr=_mm_getcsr();_mm_setcsr((csr&~0x6000u)|0x6000u|0x8000u);const unsigned entered=_mm_getcsr();unsigned short before_cw=0,after_cw=0;__asm fnstcw before_cw
  air_reset();now=2;upkeep(actor.data());__asm fnstcw after_cw
  check(debits==1&&last_debit==20&&before_cw==after_cw&&_mm_getcsr()==entered,"upkeep preserves x87/MXCSR across caller precision/rounding");
  air_reset();callback<frame_t>(air_handler("ch_air_grab_pickup"),4)(nullptr,nullptr,actor.data(),nullptr);check(close_float(last_position.x,0.3f)&&close_float(last_position.y,0.5f)&&_mm_getcsr()==entered,"pickup FP bits stable under caller rounding/FTZ");
  const std::array<u32,3> actual_bits{std::bit_cast<u32>(last_position.x),std::bit_cast<u32>(last_position.y),std::bit_cast<u32>(last_angles.z)};if(first_fp){pickup_bits=actual_bits;first_fp=false;}check(actual_bits==pickup_bits,"pickup coordinates/yaw bit-identical across caller FP environments");
  _mm_setcsr(csr);_controlfp_s(&ignored,saved,_MCW_PC|_MCW_RC);
 }
}

}
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 AddVectoredExceptionHandler(1,[](PEXCEPTION_POINTERS e)->LONG{std::printf("air fixture exception %08lX at %p ESP=%08lX ECX=%08lX ESI=%08lX\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,e->ContextRecord->Esp,e->ContextRecord->Ecx,e->ContextRecord->Esi);for(unsigned i=0;i<12;++i)std::printf(" %08X",reinterpret_cast<u32*>(e->ContextRecord->Esp)[i]);std::puts("");ExitProcess(1);});
 const int lifecycle=lifecycle_main(argc,argv);if(lifecycle&&!xml1_grab_carry::installed())return lifecycle;
 air_bind();complete_native_clearance_test();live_character_clearance_test();live_tk_victim_order_test();round3_contact_pose_tests();std::puts("admission");admission_tests();std::puts("finite energy");finite_energy_hold_test();std::puts("native shortcut");native_shortcut_admission_test();std::puts("native entry pose");native_entry_pose_test();std::puts("pickup");pickup_tests();std::puts("carry");carry_tests();std::puts("release");release_tests();std::puts("sequence");sequence_tests();std::puts("held acquisition");held_acquisition_test();std::puts("carry damage");carry_damage_test();std::puts("fastball authored event");fastball_authored_event_test();std::puts("flight");flight_tests();powerup_tests();native_bone_tests();std::puts("native world pose");native_world_pose_test();std::puts("native generic stick");native_generic_stick_test();
 std::printf("%s flying carry callbacks: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}

// Full production installer plus carry callbacks in an invented native image.
// The included lifecycle harness supplies retail input, OFF/guard/rollback probes
// and shared-lookup coexistence; no game, rendering or world startup executes.
#define main lifecycle_main
#include "xml1_grab_carry_install_test.cpp"
#undef main
#include "xml1_air_carry.hpp"
#include "det_math.hpp"
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
bool sequence_mode=false;
bool sequence_states=false;
std::array<void*,3> sequence_nodes{};
void* sequence_carry=nullptr;
void* sequence_handler_now=nullptr;
void* __fastcall air_move_handler(void*,void*){return sequence_mode?sequence_handler_now:nullptr;}
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
 flight_rank=3;actor_team=1;victim_team=2;bone_mask=0x61;anim_id=-1;energy=1000.f;last_debit=0.f;parameter2=1.f;parameter8=50.f;parameter9=100.f;
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
void __fastcall air_start(void* who,void*,void* chosen,int reset){++air_starts;check(reset==1&&chosen==idle.data(),"source recovery starts named Idle reset1");idle_actors.push_back(who);air_events.push_back("idle");if(sequence_mode)put(who,0x378,chosen);}
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
bool __fastcall downward_probe(void* who,void*,const V* start,const V* delta,int flag,V*,void** contact){++probes;const auto position=sequence_mode?get<V>(actor.data(),0x20):V{10,20,30};check(who==actor.data()&&start->x==position.x&&start->y==position.y&&start->z==position.z&&delta->x==0&&delta->y==0&&delta->z==-20&&flag==1,"entry probes from actor position 20 down with character stop flag");*contact=contact_exists?victim.data():nullptr;return probe_hit;}
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
void __fastcall spend(void*,void*,float cost){++debits;last_debit=cost;}
void air_bind(){
 move_vt[0x30/4]=reinterpret_cast<void*>(&air_move_handler);
 // Keep the real Guard/no-power/native-refusal instructions. Only the
 // vector descent tail is replaced by a return in this invented caller.
 native(0x433e46,0x26);if(get<unsigned char>(ptr(0x433e6c),0)!=0xe9)jump(0x433e6c,&descent_rejected);jump(0x433e72,&descent_rejected);jump(0x433ea5,&descent_accepted);jump(0x61a3a0,&descent_blocked);jump(0x42b6c0,&downward_probe);
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
 for(const auto* name:{"ea_flyingcarry_flyfast_v","ea_flyingcarry_flyslow_v","ea_flyingcarry_idle_v","ea_flyingcarry_pickup_v","ea_flyingcarry_pickup_a","ea_flyingcarry_idle_a"}){air_reset();flight_rank=6;anim_id=anim_enums::extension_id(name);frame(h,nullptr,actor.data(),nullptr);bool hand=std::string(name).ends_with("_v"),interpolate=std::string(name).find("pickup")!=std::string::npos;check(displacements==1&&close_float(last_pose.position.x,(hand?10.f:100.f)*(interpolate?0.1f:1.f))&&close_float(last_pose.position.z,(hand?30.f:245.f)*(interpolate?0.1f:1.f))&&close_float(last_pose.angles.z,2.7f),"victim enum attachment case maps published IDs and movement deltas");check(get<unsigned char>(victim.data(),0x764)==0x90,"carry frame reasserts attachment marker");}
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
   put(air_input.data(),8,speed);put(air_input.data(),0,(held_guard?0x100u:0u)|(phase==3?64u:phase==4?256u:0u));
   anim_id=anim_enums::extension_id(speed==0?"ea_flyingcarry_idle_a":speed<100?"ea_flyingcarry_flyslow_a":"ea_flyingcarry_flyfast_a");
   if(phase==3)put(actor.data(),0x28,180.f+(frame-60));else if(phase==4)put(actor.data(),0x28,200.f-(frame-80));
   sequence_control(actor.data(),nullptr,air_input.data());
   if(frame&&frame%20==0)reinterpret_cast<void(__thiscall*)(void*)>(0x42c270)(victim.data());
   probe_hit=phase!=4;const int previous_probes=probes;const bool consumed=descent_probe(actor.data(),nullptr,air_input.data());
   const bool down=(get<u32>(air_input.data(),0)&0x100)!=0;
   check(probes==previous_probes+(down?1:0)&&carry_entries==1,"established carry retains source descent probe without readmitting decide");
   if(phase==4)check(!consumed,"established carry can descend with Guard through empty airspace");
   callback<frame_t>(sequence_carry,4)(sequence_carry,nullptr,actor.data(),nullptr);
   check(get<u32>(victim.data(),4)==0x87,"carrier frame closes native victim reset gap before physical movement");
   callback<frame_t>(victim_handler,4)(victim_handler,nullptr,victim.data(),nullptr);
   const auto a=get<V>(actor.data(),0x20),v=get<V>(victim.data(),0x20);
   check(close_float(a.x,v.x)&&close_float(a.y,v.y)&&close_float(a.z-17.f,v.z)&&get<u32>(victim.data(),0x178)==0x1111,"established victim follows moving carrier in XYZ for every frame");
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
  const bool handled=descent_probe(actor.data(),nullptr,air_input.data());check(carry_entries==(c==0?1:0),"Guard 0x100 admission; 0x08/power/rank2/miss/noncharacter/large/inactive/move/native refusal negatives");
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
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 AddVectoredExceptionHandler(1,[](PEXCEPTION_POINTERS e)->LONG{std::printf("air fixture exception %08lX at %p ESP=%08lX ECX=%08lX ESI=%08lX\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,e->ContextRecord->Esp,e->ContextRecord->Ecx,e->ContextRecord->Esi);ExitProcess(1);});
 const int lifecycle=lifecycle_main(argc,argv);if(lifecycle&&!xml1_grab_carry::installed())return lifecycle;
 air_bind();admission_tests();pickup_tests();carry_tests();release_tests();sequence_tests();flight_tests();powerup_tests();native_bone_tests();
 std::printf("%s flying carry callbacks: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}

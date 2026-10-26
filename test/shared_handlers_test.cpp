#include "shared_handler_fixture.hpp"
#include "shared_handlers.hpp"
#include "shared_handler_rules.hpp"
#include "shared_handler_bridges.hpp"
#include "gameplay_capabilities.hpp"
#include "anim_enum_sites.hpp"
#include <optional>
#include <cwchar>
#include <cmath>
#include <algorithm>
using namespace handler_fixture;
constexpr u32 x1_bias=0x10000000;
extern "C" unsigned char fixture_space[0x400000];
namespace ini {
 std::array<std::optional<std::string>,7> settings{};
 std::optional<std::string> text(const wchar_t*,const wchar_t* key) { for(unsigned i=0;i<settings.size();++i)if(!std::wcscmp(key,shared_handler_rules::keys[i]))return settings[i];return {}; }
}
namespace logger { void write(const char*,...) {} }
namespace game_version { bool refused=false;void refuse_startup(){refused=true;} }
namespace entity_capacity { u32 owner_site=0,owner_target=0;u32 installed_target(u32 a){return a==owner_site?owner_target:0;} }
namespace {
void seed(const image& pc) { for(const auto& g:shared_handler_rules::guards)pc.load(g.bytes.va,g.bytes.hex.size()/2); }
void keys(u32 mask,const char* value="xml1") { for(unsigned i=0;i<7;++i)ini::settings[i]=(mask&(1u<<i))?std::optional<std::string>(value):std::nullopt; }
void __fastcall refresh(void*,void*) {}
int random_choice=0,animation=0,base_calls=0;
void __fastcall record_base(void*,void*,void*,void*,void*){++base_calls;}
int __cdecl pc_random(int lo,int hi,int inclusive) {check(lo==0&&hi==3&&inclusive==1,"PC random inclusive range is four choices");return random_choice;}
int __cdecl x1_random(int lo,int hi) {check(lo==0&&hi==3,"XML1 random range is four choices");return random_choice;}
void __fastcall animate(void*,void*,int id,int priority,float speed,int a,int b) {animation=id;check(priority==12&&speed==1.f&&!a&&!b,"landing native animation parameters");}
std::array<unsigned char,0x900> actor{},original_actor{},target{},object{},node{},sound{},event_object{},ai{},style{},move{};
std::array<void*,160> node_vt{},event_vt{},audio_vt{},ai_vt{},actor_vt{};
void** audio=audio_vt.data();void** ai_manager=ai_vt.data();
std::vector<int> events;
float current_time=100.f;
bool original=false,valid_sound=true,has_sound=true,has_update_event=true,fallback_input=false;
void* selected_action=move.data();
float __cdecl clock_value(){return current_time;}
void* __fastcall get_event(void*,void*,u32 id) {events.push_back(static_cast<int>(id));return id==100?(has_sound?sound.data():nullptr):(has_update_event?event_object.data():nullptr);}
void* __cdecl cast_sound(void* p,int,void* from,void* to,int) {check(from==reinterpret_cast<void*>(original?0x450bc0:0x6db300)&&to==reinterpret_cast<void*>(original?0x450dd0:0x6db604),"sound cast uses mapped combat/sound descriptors");return p;}
void* __cdecl get_audio(){return &audio;}
u32 __fastcall play_sound(void*,void*,u32 id,void*,float gain,float far_range,float near_range) {check(id==321&&gain==1.f&&far_range==1300.f&&near_range==150.f,"charge sound play parameters match XML1");events.push_back(1);return 77;}
bool __fastcall sound_exists(void*,void*,u32 id){check(id==77,"charge retained sound handle");events.push_back(2);return valid_sound;}
void __fastcall update_sound(void*,void*,u32,void*){events.push_back(3);}
void __fastcall stop_sound(void*,void*,u32){events.push_back(4);}
void __fastcall dispatch_event(void*,void*,void* a,void* b){check(a==b,"charge event200 receives actor twice");events.push_back(5);}
void* __fastcall resolve_action(void*,void*,u32 id){check(id==0x18,"charge expiry uses action 0x18");events.push_back(6);return selected_action;}
void __fastcall play_move(void*,void*,void* m,int flag){check(m==selected_action&&flag==1,"charge update dispatches resolved expiry move");events.push_back(7);}
bool __fastcall default_input(void*,void*,void*,void*){events.push_back(8);return fallback_input;}
int __fastcall team(void* a,void*){return field<int>(a,0x10);}
bool eligible=false;
void* __cdecl get_ai(){return &ai_manager;}
bool __fastcall ai_eligible(void*,void*,u32 token){check(token==345,"Nightcrawler uses target AI token");return eligible;}
__declspec(naked) void yes(){__asm {
 mov eax,1
 ret
}}
__declspec(naked) void no(){__asm {
 xor eax,eax
 ret
}}
u32 branch_site=0;
__declspec(naked) int drive_branch(void*,void*) {
 __asm {
 push ebx
 push ebp
 push esi
 push edi
 mov esi,[esp+20]
 mov ebp,[esp+24]
 mov edi,ebp
 call dword ptr [branch_site]
 pop edi
 pop esi
 pop ebp
 pop ebx
 ret
}
}
float fixed_direction[3]{1,0,0},observed_velocity[3]{};
float* __cdecl direction(float* out,void*){std::copy_n(fixed_direction,3,out);return out;}
void* __fastcall vec_init(void* p,void*){std::fill_n(static_cast<float*>(p),3,0.f);return p;}
float* __fastcall vec_copy(float* p,void*,const float* a){std::copy_n(a,3,p);return p;}
float* __fastcall vec_scale(float* p,void*,const float* a,const float* s){for(int i=0;i<3;++i)p[i]=a[i]**s;return p;}
float* __fastcall vec_index(float* p,void*,int i){return p+i;}
float __fastcall vec_value(float* p,void*,int i){return p[i];}
void __fastcall velocity(void*,void*,const float* value){std::copy_n(value,3,observed_velocity);}
void __fastcall after_velocity(void*,void*,void*,void*){}
const float* __fastcall actor_position(void* a,void*,float*){return reinterpret_cast<float*>(static_cast<unsigned char*>(a)+0x20);}
const float* __fastcall actor_bounds(void*,void*,float* scratch){scratch[0]=scratch[1]=scratch[2]=4.f;return scratch;}
bool trace_hit=true,is_player=false,token_valid=true,hit_actor=false,physical=true,protected_object=false,has_move=true;
float probe_endpoint[3]{};
void* __fastcall trace_init(void* p,void*,u32 initial){field<u32>(p,0)=initial;return p;}
void* __fastcall token_entity(u32* token,void*) {return *token==10?actor.data():object.data();}
bool __fastcall valid_token(u32*,void*){return token_valid;}
bool __cdecl trace(u32 token,const float* begin,const float* end,const float* bounds,void** out){check(token==10&&begin[0]==3.f&&begin[1]==5.f&&begin[2]==7.f&&bounds[0]==4.f,"probe native owner, origin and bounds");std::copy_n(end,3,probe_endpoint);*out=object.data();return trace_hit;}
bool __fastcall trace_player(void**,void*){return is_player;}
void* __fastcall trace_entity(void**,void*){return object.data();}
void* __cdecl actor_token(u32){return hit_actor?object.data():nullptr;}
void* __cdecl physical_token(u32){return physical?object.data():nullptr;}
bool __fastcall protected_flag(void*,void*,int id,int zero){check(id==(original?0x39:0x5c)&&zero==0,"probe maps the exclusion affecter ID");return protected_object;}
void* __fastcall action_name(void*,void*,int id){check(id==0x19,"forward object acquisition uses original action 0x19");return move.data();}
void* __fastcall resolve_style(void*,void*,const void* name,int zero){check(name==move.data()&&zero==0,"object move resolver receives name and original applicability flag");return has_move?move.data():nullptr;}
__declspec(naked) void probe_yes(){__asm {
 mov eax,esi
 ret
}}
__declspec(naked) void probe_x1_yes(){__asm {ret}}
u32 probe_site=0;
__declspec(naked) void* drive_probe(void*){
 __asm {
 push ebx
 push ebp
 push esi
 push edi
 sub esp,0x48
 mov esi,[esp+0x5c]
 mov ebx,esi
 call dword ptr [probe_site]
 add esp,0x48
 pop edi
 pop esi
 pop ebp
 pop ebx
 ret
}
}
void bind_helpers() {
 jump(x1_bias+0xa0770,&trace_init);
 jump(0x4fbdd0,&record_base);jump(x1_bias+0xe6d80,&record_base);
 jump(0x5601e0,&pc_random);jump(x1_bias+0x1233c0,&x1_random);jump(0x430cd0,&animate);jump(x1_bias+0x40350,&animate);
 jump(0x45fe60,&clock_value);jump(x1_bias+0x66ab0,&clock_value);jump(0x45fe40,&refresh);jump(x1_bias+0x66a90,&refresh);
 jump(0x592480,&get_audio);jump(x1_bias+0x151ef0,&get_audio);jump(0x67234e,&cast_sound);jump(x1_bias+0x34238d,&cast_sound);
 jump(0x427c80,&resolve_action);jump(x1_bias+0x381f0,&resolve_action);jump(0x42c340,&play_move);jump(x1_bias+0x3b100,&play_move);
 jump(0x4fce50,&default_input);jump(x1_bias+0xe7460,&default_input);
 jump(0x41a220,&team);jump(x1_bias+0x26e60,&team);jump(0x5252e0,&get_ai);jump(x1_bias+0xf9a00,&get_ai);
 jump(0x560990,&direction);jump(x1_bias+0x1209c0,&direction);jump(0x473640,&velocity);jump(x1_bias+0x760d0,&velocity);
 field<void*>(at(0x67fa60),0)=reinterpret_cast<void*>(&vec_init);field<void*>(at(0x67fa2c),0)=reinterpret_cast<void*>(&vec_copy);
 field<void*>(at(0x67f9f8),0)=reinterpret_cast<void*>(&vec_scale);field<void*>(at(0x67f9c4),0)=reinterpret_cast<void*>(&vec_index);field<void*>(at(0x67f9d8),0)=reinterpret_cast<void*>(&vec_value);
 node_vt[0x1c/4]=reinterpret_cast<void*>(&get_event);audio_vt[0x4c/4]=reinterpret_cast<void*>(&play_sound);audio_vt[0x60/4]=reinterpret_cast<void*>(&sound_exists);
 audio_vt[0x20/4]=reinterpret_cast<void*>(&after_velocity);audio_vt[0x70/4]=reinterpret_cast<void*>(&update_sound);audio_vt[0x74/4]=reinterpret_cast<void*>(&stop_sound);event_vt[1]=reinterpret_cast<void*>(&dispatch_event);
 field<void*>(node.data(),0)=node_vt.data();field<void*>(event_object.data(),0)=event_vt.data();field<u32>(sound.data(),0x14)=321;field<u32>(sound.data(),0x18)=321;
 ai_vt[0x78/4]=reinterpret_cast<void*>(&ai_eligible);
 actor_vt[0x20/4]=reinterpret_cast<void*>(&after_velocity);actor_vt[0x24/4]=reinterpret_cast<void*>(&actor_position);actor_vt[0x28/4]=reinterpret_cast<void*>(&actor_bounds);
 jump(0x4654b0,&token_entity);jump(x1_bias+0x6bf80,&token_entity);jump(0x4654d0,&valid_token);jump(x1_bias+0x6bfa0,&valid_token);
 jump(0x4ab960,&trace);jump(x1_bias+0xa2d00,&trace);jump(0x4a95c0,&trace_player);jump(x1_bias+0xa07b0,&trace_player);jump(0x4a9590,&trace_entity);jump(x1_bias+0xa0780,&trace_entity);
 jump(0x41fb10,&actor_token);jump(x1_bias+0x293f0,&actor_token);jump(0x4297b0,&physical_token);jump(x1_bias+0x2b810,&physical_token);
 jump(0x54d710,&protected_flag);jump(x1_bias+0x2b250,&protected_flag);jump(0x501a50,&resolve_style);jump(x1_bias+0xed6e0,&resolve_style);
 node_vt[0xf4/4]=reinterpret_cast<void*>(&action_name);node_vt[0x118/4]=reinterpret_cast<void*>(&action_name);
}
void reset_actor() {
 actor.fill(0);original_actor.fill(0);
 for(auto* a:{actor.data(),original_actor.data()}){field<void*>(a,0)=actor_vt.data();field<void*>(a,0x328)=node.data();field<void*>(a,0x3c0)=node.data();field<u32>(a,0x73c)=0xffffffffu;field<u32>(a,0x52c)=0xffffffffu;}
}
// Resolve a named enum through the executable's constructor table. No fixed
// numeric delta is assumed between the two animation registries.
int named_enum(const image& img,const char* name) {
 for(auto s:img.sections) {
  const auto* b=img.read(s.va,s.size);if(!b)continue;
  for(std::size_t n=0;n+std::strlen(name)+1<s.size;++n) {
   if(std::memcmp(b+n,name,std::strlen(name)+1))continue;
   const auto va=s.va+static_cast<u32>(n);
   for(std::size_t k=0;k+15<img.bytes.size();++k)if(!std::memcmp(img.bytes.data()+k,&va,4)&&!std::memcmp(img.bytes.data()+k+4,"\xc7\x84\x24",3))return static_cast<int>(img.word(k+11));
  }
 }
 return -1;
}
void landing_tests(const image& pc,const image& x1) {
 constexpr const char* names[]{"ea_power2","ea_power3","ea_power4","ea_power6"};constexpr int pc_ids[]{0x75,0x78,0x79,0x7e},x1_ids[]{0x87,0x89,0x8a,0x8d};
 for(int i=0;i<4;++i){const int a=named_enum(pc,names[i]),b=named_enum(x1,names[i]);if(a!=pc_ids[i]||b!=x1_ids[i])std::printf("enum %s PC=%x X1=%x\n",names[i],a,b);check(a==pc_ids[i]&&b==x1_ids[i],"animation enums mapped by matching registered names");}
 for(random_choice=0;random_choice<4;++random_choice) {
  original=true;base_calls=0;reinterpret_cast<shared_handler_bridges::entry>(x1_bias+0xde3c0)(nullptr,actor.data(),nullptr,nullptr);const int observed=animation;check(base_calls==1,"original landing calls base entry once");
  original=false;base_calls=0;reinterpret_cast<shared_handler_bridges::entry>(0x4f0010)(nullptr,actor.data(),nullptr,nullptr);
  check(observed==x1_ids[random_choice]&&animation==pc_ids[random_choice]&&base_calls==1,"restored landing selects every original choice without a name gate");
 }
}
void popup_tests() {
 for(unsigned bit:{0u,0x80u}) {
  reset_actor();std::array<unsigned char,0x400> stats{};stats[0x2ae]=static_cast<unsigned char>(bit);field<void*>(actor.data(),0x35c)=stats.data();field<float>(actor.data(),0x278)=.4f;field<float>(original_actor.data(),0x478)=.4f;
  reinterpret_cast<shared_handler_bridges::entry>(x1_bias+0xdefa0)(nullptr,original_actor.data(),nullptr,nullptr);
  reinterpret_cast<shared_handler_bridges::entry>(0x4f0fa0)(nullptr,actor.data(),nullptr,nullptr);
  check(field<float>(actor.data(),0x278)==field<float>(original_actor.data(),0x478)&&field<float>(actor.data(),0x278)==1.f,"popup landing reset matches original for both stats flags");
 }
 reset_actor();field<float>(actor.data(),0x278)=.4f;reinterpret_cast<shared_handler_bridges::entry>(0x4f0fa0)(nullptr,actor.data(),nullptr,nullptr);check(field<float>(actor.data(),0x278)==1.f,"popup landing needs no stats pointer");
 const auto before=actor; __asm {fld1}
 reinterpret_cast<void(__thiscall*)(void*,void*)>(x1_bias+0x15fdf0)(nullptr,actor.data());
 reinterpret_cast<shared_handler_bridges::update>(0x4f0f00)(nullptr,actor.data(),nullptr);float sentinel=0;__asm {fstp sentinel}
 check(actor==before&&sentinel==1.f,"popup/air-spin decision is a no-op and preserves a live x87 sentinel");
}
void skating_tests() {
 field<float>(at(0x6916fc),0)=120.f;field<float>(at(0x680030),0)=0.f;
 const float heights[]{-1.f,35.999f,36.f,36.001f,47.999f,48.f,48.001f,59.99f,60.f,119.99f,120.f,120.01f};
 for(float h:heights) {
  reset_actor();field<float>(actor.data(),0x28)=h;field<float>(original_actor.data(),0x28)=h;
  field<float>(actor.data(),0x6d0)=0.f;field<float>(actor.data(),0x798)=100.f; // old PC exit would trigger
  float frame[9]{};frame[8]=.1f;
  reinterpret_cast<void(__thiscall*)(void*,void*)>(x1_bias+0xdac50)(&audio,original_actor.data());const std::array<float,3> expected{observed_velocity[0],observed_velocity[1],observed_velocity[2]};
  reinterpret_cast<shared_handler_bridges::update>(0x4ec870)(&audio,actor.data(),frame);
  check(std::equal(expected.begin(),expected.end(),observed_velocity)&&observed_velocity[0]==400.f,"skating velocity matches XML1 across lower/upper threshold boundaries");
  check(field<float>(actor.data(),0x798)==100.f,"skating removed PC low-speed elapsed-time update");
 }
 reset_actor();field<float>(actor.data(),0x434)=.75f;
 reinterpret_cast<shared_handler_bridges::entry>(0x4ecf80)(nullptr,actor.data(),nullptr,nullptr);
 check((field<u32>(actor.data(),4)&4)&&field<float>(actor.data(),0x434)==.75f,"skating entry preserves actor coefficient while setting traversal flag");
 reinterpret_cast<void(__thiscall*)(void*,void*)>(0x4ecfb0)(nullptr,actor.data());check(!(field<u32>(actor.data(),4)&4)&&field<float>(actor.data(),0x434)==.75f,"skating exit does not clear PC-only entry coefficient");
}
void charge_tick(void* who) {
 reinterpret_cast<shared_handler_bridges::update>(field<void*>(at(0x6937b4),0))(nullptr,who,nullptr);
}
void charge_tests() {
 for(bool sound_present:{false,true})for(bool live:{false,true}) {
  has_sound=sound_present;valid_sound=live;has_update_event=true;reset_actor();
  field<float>(actor.data(),0x740)=2.f;field<float>(original_actor.data(),0x530)=2.f;
  original=true;events.clear();reinterpret_cast<shared_handler_bridges::entry>(x1_bias+0xd91e0)(nullptr,original_actor.data(),nullptr,nullptr);auto expected=events;
  original=false;events.clear();reinterpret_cast<shared_handler_bridges::entry>(0x4ebe20)(nullptr,actor.data(),nullptr,nullptr);
  check(events==expected&&field<float>(actor.data(),0x5e8)==field<float>(original_actor.data(),0x3f0)&&field<u32>(actor.data(),0x73c)==field<u32>(original_actor.data(),0x52c),"charge entry deadline and optional sound lifecycle match XML1");
  field<float>(actor.data(),0x2f0)=current_time+10.f;field<float>(original_actor.data(),0x28c)=current_time+10.f;
  original=true;events.clear();reinterpret_cast<void(__thiscall*)(void*,void*)>(x1_bias+0xd9330)(nullptr,original_actor.data());expected=events;
  original=false;events.clear();charge_tick(actor.data());
  check(events==expected,"charge per-frame sound position and event200 order match XML1");
  original=true;events.clear();reinterpret_cast<void(__thiscall*)(void*,void*)>(x1_bias+0xd9270)(nullptr,original_actor.data());expected=events;
  original=false;events.clear();reinterpret_cast<void(__thiscall*)(void*,void*)>(0x4ebe50)(nullptr,actor.data());
  check(events==expected&&field<u32>(actor.data(),0x73c)==field<u32>(original_actor.data(),0x52c)&&field<float>(actor.data(),0x2f0)==0.f,"charge exit sound validity/stop and contact reset match XML1");
 }
 has_sound=false;has_update_event=false;
 for(float duration:{-1.f,0.f,2.f})for(float now:{99.999f,100.f,100.001f})for(bool fallback:{false,true})for(bool null_action:{false,true}) {
  current_time=now;fallback_input=fallback;selected_action=null_action?nullptr:move.data();reset_actor();
  field<float>(actor.data(),0x740)=duration;field<float>(original_actor.data(),0x530)=duration;
  field<float>(actor.data(),0x5e8)=100.f;field<float>(original_actor.data(),0x3f0)=100.f;
  original=true;events.clear();bool expected=reinterpret_cast<shared_handler_bridges::input>(x1_bias+0xd92d0)(nullptr,original_actor.data(),nullptr);auto order=events;
  original=false;events.clear();bool got=reinterpret_cast<shared_handler_bridges::input>(0x4ebe70)(nullptr,actor.data(),nullptr);
  check(got==expected&&events==order&&field<void*>(actor.data(),0x3bc)==field<void*>(original_actor.data(),0x324),"charge input positive-duration strict expiry and default input match original");
  field<float>(actor.data(),0x2f0)=current_time+10.f;field<float>(original_actor.data(),0x28c)=current_time+10.f;
  original=true;events.clear();reinterpret_cast<void(__thiscall*)(void*,void*)>(x1_bias+0xd9330)(nullptr,original_actor.data());order=events;
  original=false;events.clear();charge_tick(actor.data());
  check(events==order,"charge update positive-duration strict expiry and absent event200 match original");
 }
 current_time=100.f;selected_action=move.data();
}
void night_tests() {
 jump(0x4f065d,&yes);jump(0x4f06a5,&no);jump(x1_bias+0xdbc3c,&yes);jump(x1_bias+0xdbc85,&no);
 for(int a:{0x1c,0x1d,0x1e})for(int t:{0x1c,0x1d,0x1e})for(bool e:{false,true})for(int controller:{-1,0}) {
  field<int>(actor.data(),0x10)=a;field<int>(target.data(),0x10)=t;field<int>(target.data(),0x3d4)=controller;field<u32>(target.data(),0x7dc)=345;field<u32>(target.data(),0x5b4)=345;eligible=e;
  branch_site=0x4f0640;const bool got=drive_branch(actor.data(),target.data())!=0;
  // Original neutral/team numbers are two lower than PC's.
  field<int>(actor.data(),0x10)=a-2;field<int>(target.data(),0x10)=t-2;
  branch_site=x1_bias+0xdbc05;const bool expected=drive_branch(actor.data(),target.data())!=0;
  check(got==expected,"Nightcrawler same team and neutral/AI eligibility match XML1 independent of controller");
 }
}
void gambit_tests() {
 jump(0x4ed659,&probe_yes);jump(0x4ed5dd,&no);jump(x1_bias+0xdb892,&probe_x1_yes);jump(x1_bias+0xdb89c,&no);
 for(int scenario=0;scenario<13;++scenario) {
  reset_actor();field<void*>(actor.data(),0)=actor_vt.data();field<u32>(actor.data(),0x1c)=10;field<float>(actor.data(),0x20)=3.f;field<float>(actor.data(),0x24)=5.f;field<float>(actor.data(),0x28)=7.f;
  trace_hit=scenario!=1;is_player=scenario==2;token_valid=scenario!=3;hit_actor=scenario==4;physical=scenario!=5;protected_object=scenario==6;has_move=scenario!=7;
  field<u32>(object.data(),0x1c)=scenario==8?0:20;object[0x31c]=object[0x2c2]=static_cast<unsigned char>(scenario==9?10:9);
  actor[0x3d8]=actor[0x334]=static_cast<unsigned char>(scenario==10?1:0);
  if(scenario==11){fixed_direction[0]=0.f;fixed_direction[1]=-1.f;}
  if(scenario==12){fixed_direction[0]=-.10101787000894547f;fixed_direction[1]=0.f;}
  original=true;probe_site=x1_bias+0xdb70a;auto* expected=drive_probe(actor.data());const std::array<float,3> endpoint{probe_endpoint[0],probe_endpoint[1],probe_endpoint[2]};
  original=false;probe_site=0x4ed5d7;auto* got=drive_probe(actor.data());
  check(got==expected,"Gambit/Jubilee forward acquisition agrees with original probe eligibility");
  if(scenario!=10)check(std::equal(endpoint.begin(),endpoint.end(),probe_endpoint)&&(scenario==12 || probe_endpoint[0]==(scenario==11?3.f:63.f))&&probe_endpoint[1]==(scenario==11?-55.f:5.f),"Gambit/Jubilee probe endpoint is 60 units forward");
 }
 fixed_direction[0]=1.f;fixed_direction[1]=0.f;
}
u32 machine_site=0,machine_regs[7]{},before_esp=0,after_esp=0,before_flags=0,after_flags=0;
float observed_fpu=0;
u32 vector_seed=0x3fc00000,observed_sse=0;
void* machine_actor=actor.data();void* machine_target=target.data();
__declspec(naked) void return_here(){__asm {ret}}
__declspec(naked) void drive_machine() {
 __asm {
  pushad
  mov ebx,machine_actor
  mov esi,ebx
  mov ebp,machine_target
  mov edi,0x11223344
  mov ecx,0x22334455
  mov edx,0x33445566
  mov eax,0x44556677
  fld1
  movss xmm0,dword ptr [vector_seed]
  cmp eax,eax
  pushfd
  pop before_flags
  mov before_esp,esp
  call dword ptr [machine_site]
  mov after_esp,esp
  mov machine_regs[0],eax
  mov machine_regs[4],ebx
  mov machine_regs[8],ecx
  mov machine_regs[12],edx
  mov machine_regs[16],esi
  mov machine_regs[20],edi
  mov machine_regs[24],ebp
  pushfd
  pop after_flags
  fstp observed_fpu
  movss dword ptr [observed_sse],xmm0
  popad
  ret
 }
}
void abi_tests() {
 for(u32 site:{0x4f0640u,0x4ed5d7u,0x4ec02fu})for(bool outcome:{false,true}) {
  reset_actor();field<int>(actor.data(),0x10)=0x1d;field<int>(target.data(),0x10)=outcome?0x1d:0x1e;
  field<u32>(actor.data(),0x1c)=10;field<float>(actor.data(),0x20)=3.f;field<float>(actor.data(),0x24)=5.f;field<float>(actor.data(),0x28)=7.f;
  actor[0x3d8]=outcome?0:1;object[0x31c]=9;field<u32>(object.data(),0x1c)=20;
  trace_hit=true;is_player=false;token_valid=true;hit_actor=false;physical=true;protected_object=false;has_move=true;
  has_update_event=outcome;field<short>(actor.data(),0x744)=outcome?1:0;
  shared_handler_bridges::charge_event_resume=static_cast<u32>(reinterpret_cast<std::uintptr_t>(&return_here));
  machine_site=site;drive_machine();
  check(before_esp==after_esp,"all new bridges preserve exact native stack depth");
  check(observed_fpu==1.f&&observed_sse==vector_seed,"new bridges retain live x87 and SSE sentinels");
  const bool acquired=site==0x4ed5d7&&outcome;
  check(machine_regs[1]==reinterpret_cast<u32>(actor.data())&&machine_regs[2]==0x22334455&&machine_regs[3]==0x33445566&&machine_regs[4]==reinterpret_cast<u32>(acquired?move.data():actor.data())&&machine_regs[5]==0x11223344&&machine_regs[6]==reinterpret_cast<u32>(target.data()),"new bridges preserve native live registers apart from acquired-move result");
  if(site==0x4f0640)check(before_flags==after_flags,"Nightcrawler eligibility bridge preserves caller flags");
  if(site==0x4ec02f)check(((after_flags&0x40)!=0)==!outcome,"charge event bridge replays the displaced movement compare flags");
 }
 shared_handler_bridges::charge_event_resume=0x4ec037;
}
}
int main(int argc,char** argv) {
 std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 AddVectoredExceptionHandler(1,[](PEXCEPTION_POINTERS e)->LONG{std::printf("native fixture exception %08lX at %p EAX=%08lX ECX=%08lX ESI=%08lX ESP=%08lX\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,e->ContextRecord->Eax,e->ContextRecord->Ecx,e->ContextRecord->Esi,e->ContextRecord->Esp);return EXCEPTION_CONTINUE_SEARCH;});
 if(argc<3){std::puts("usage: xml2_shared_handlers_test XMen2.exe default.xbe [mask]");return 2;}
 if(reinterpret_cast<std::uintptr_t>(fixture_space)>0x4ebe20){std::printf("fixture too high %p\n",fixture_space);return 2;}
 DWORD old=0;if(!VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)){std::printf("protect error %lu\n",GetLastError());return 2;}
 if(VirtualAlloc(at(x1_bias),0x400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)!=at(x1_bias))return 2;
 image pc(argv[1],pc_hash),x1(argv[2],x1_hash);if(failures)return 1;
 const u32 mask=argc>3?static_cast<u32>(std::stoul(argv[3],nullptr,0)):127;
 if(!mask||mask>127)return 2;
 // No guard may cover a byte [Limits] AnimEnums rewrites (rule 48 installs first in the startup transaction): an
 // overlapping guard refuses rule 42 whenever both are on (080c: the 0x430cd0 entry guard reached site C13).
 for(const auto& g:shared_handler_rules::guards)for(const auto& s:anim_enum_sites::sites){const u32 n=static_cast<u32>(g.bytes.hex.size()/2),m=static_cast<u32>(s.old_hex.size()/2);check(g.bytes.va+n<=s.va||s.va+m<=g.bytes.va,g.bytes.what);}
 // Seed all guards from owner input and compare literal guard bytes independently.
 for(const auto& g:shared_handler_rules::guards){const auto* b=pc.read(g.bytes.va,g.bytes.hex.size()/2);check(b&&limits_rules::matches(b,g.bytes.hex),g.bytes.what);}
 if(failures)return 1;
 seed(pc);const auto baseline=std::vector<unsigned char>(at(0x401000),at(0x801000));
 shared_handlers::install(reinterpret_cast<HMODULE>(0x500000));check(!shared_handlers::requested()&&shared_handlers::verified()&&std::equal(baseline.begin(),baseline.end(),at(0x401000)),"absent keys read no image and preserve every image byte");
 keys(127,"xml2");shared_handlers::install(nullptr);check(!shared_handlers::requested()&&shared_handlers::verified()&&std::equal(baseline.begin(),baseline.end(),at(0x401000)),"explicit native keys preserve every image byte");
 keys(127,"");shared_handlers::install(nullptr);check(!shared_handlers::requested()&&shared_handlers::verified()&&std::equal(baseline.begin(),baseline.end(),at(0x401000)),"empty keys preserve every image byte and agreement participation");
 keys(mask,"invalid");shared_handlers::install(GetModuleHandleW(nullptr));check(shared_handlers::requested()&&!shared_handlers::verified()&&!shared_handlers::installed()&&std::equal(baseline.begin(),baseline.end(),at(0x401000)),"invalid selection is unwritten and unverified");
 keys(mask);shared_handlers::install(reinterpret_cast<HMODULE>(0x500000));check(!shared_handlers::verified()&&!shared_handlers::installed(),"relocated image refused");
 for(const auto& g:shared_handler_rules::guards)if(g.parts&mask) {
  seed(pc);*at(g.bytes.va)^=1;const auto before=std::vector<unsigned char>(at(0x401000),at(0x801000));
  shared_handlers::install(GetModuleHandleW(nullptr));check(!shared_handlers::installed()&&!shared_handlers::verified()&&std::equal(before.begin(),before.end(),at(0x401000)),"each selected guard mismatch refuses all writes");
 }
 // A capacity-owned query accessor accepts only its exact published target.
 if(mask&64) {
  seed(pc);entity_capacity::owner_site=0x4ab960;entity_capacity::owner_target=0x10001230;jump(0x4ab960,reinterpret_cast<void*>(0x10001234));
  shared_handlers::install(GetModuleHandleW(nullptr));check(!shared_handlers::installed(),"foreign jump in capacity-owned query API refused");
  entity_capacity::owner_site=entity_capacity::owner_target=0;
 }
 seed(pc);
 const bool owned_query=argc>4 && (mask&64);
 if(owned_query){entity_capacity::owner_site=0x4ab960;entity_capacity::owner_target=0x10001230;jump(0x4ab960,reinterpret_cast<void*>(0x10001230));}
 shared_handlers::install(GetModuleHandleW(nullptr));check(shared_handlers::installed()&&shared_handlers::verified()&&shared_handlers::values()[0]==mask,"selected rules install atomically and advertise the exact effective mask");
 using namespace shared_handler_bridges;
 const shared_handler_rules::targets callbacks{&teleport_land,&popup_decision,&charge_enter,&charge_exit,&charge_input,&charge_update,&charge_event_bridge,&nightcrawler_bridge,&gambit_bridge};
 auto expected_image=baseline;
 if(owned_query)std::copy_n(at(0x4ab960),5,expected_image.begin()+0x4ab960-0x401000);
 const auto writes=shared_handler_rules::patches(mask,callbacks);
 for(const auto& p:writes){check(std::equal(p.before.begin(),p.before.end(),pc.read(p.address,p.before.size())),"every changed byte is guarded against retail input");std::copy(p.after.begin(),p.after.end(),expected_image.begin()+p.address-0x401000);}
 check(std::equal(expected_image.begin(),expected_image.end(),at(0x401000)),"production transaction has exactly the declared write footprint");
 gameplay_capabilities::record off,on,other;check(off.finalize(),"OFF agreement finalizes without new entry");
 check(gameplay_capabilities::shared_handlers_id==42,"shared handlers use coordinator-assigned rule ID 42");
 check(on.add(gameplay_capabilities::shared_handlers_id,1,shared_handlers::values(),shared_handlers::verified())&&on.finalize()&&on.compare(off.bytes())!=gameplay_capabilities::result::match,"selected handler rules refuse an OFF peer");
 gameplay_capabilities::record former_id;
 check(former_id.add(31,1,shared_handlers::values(),true)&&former_id.finalize()&&former_id.compare(on.bytes())!=gameplay_capabilities::result::match,"rule 31 reserved for IGBCache cannot negotiate as shared-handler rule 42");
 check(other.add(gameplay_capabilities::shared_handlers_id,1,{mask^127u,0,0,0},true)&&other.finalize()&&other.compare(on.bytes())!=gameplay_capabilities::result::match,"different selected handler masks refuse online");
 if(mask!=127){std::printf("shared handlers mask %u: %d checks, %d failures\n",mask,checks,failures);return failures?1:0;}
 // Load only functions executed by this fixture, from the user's read-only
 // binaries at runtime. No game assets, original instruction bodies or DLLs ship.
 x1.load(0xde3c0,0x62,x1_bias);x1.load(0xdefa0,0x33,x1_bias);x1.load(0x15fdf0,3,x1_bias);x1.load(0xdac50,0xc1,x1_bias);x1.load(0x3d640c,20,x1_bias);
 x1.load(0xd91e0,0x303,x1_bias);x1.load(0x3c6de4,4,x1_bias);x1.load(0x3dd054,4,x1_bias);x1.load(0xdbc05,0x84,x1_bias);x1.load(0xdb70a,0x192,x1_bias);x1.load(0x3caa50,4,x1_bias);
 // Unchanged retail bodies around the patches were not part of the minimal
 // guard seed. Reload them and reapply the already-reviewed patch bytes.
 pc.load(0x4ec870,0x165);pc.load(0x4ecf80,0x70);pc.load(0x4f0fa0,0x45);pc.load(0x41d0d0,7);x1.load(0x2e290,7,x1_bias);
 for(const auto& p:writes)std::memcpy(at(p.address),p.after.data(),p.after.size());
 bind_helpers();
 const auto* cadence=x1.read(0x3d635c,4);check(cadence&&std::equal(cadence,cadence+4,pc.read(0x68012c,4)),"original and retained PC charge contact cadence is exactly 1.0 binary32");
 std::puts("popup");popup_tests();std::puts("landing");landing_tests(pc,x1);std::puts("skating");skating_tests();std::puts("charge");charge_tests();std::puts("nightcrawler");night_tests();std::puts("gambit");gambit_tests();abi_tests();
 check(!game_version::refused,"ordinary guard refusals do not terminate startup");
 std::printf("shared handlers: %d checks, %d failures\n",checks,failures);return failures?1:0;
}

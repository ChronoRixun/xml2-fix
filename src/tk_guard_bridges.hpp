#pragma once
#include "shared_handler_bridges.hpp"
#include "xml1_cling_rules.hpp"
#include "det_math.hpp"
#include "lift_rule.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_map>

// Rule 46's coupled handlers. All addresses below are PC services; source
// anchors and field translations are recorded in docs/tk-guard.md.
namespace tk_guard_bridges {
using namespace shared_handler_bridges;
struct vec {float x=0,y=0,z=0;};
inline vec add(vec a,vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline vec sub(vec a,vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline vec mul(vec a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float length(vec a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
inline vec unit(vec a){const float n=length(a);return n>0&&std::isfinite(n)?mul(a,1.f/n):vec{};}
inline void* resolve(u32 token){return token?fn<void*(__thiscall*)(u32*)>(0x4654b0)(&token):nullptr;}
inline bool valid(u32 token){return token&&fn<bool(__thiscall*)(u32*)>(0x4654d0)(&token);}
inline void* cast(void* p,u32 va){return fn<void*(__cdecl*)(void*)>(va)(p);}
inline void* character(void* p){return cast(p,0x402b60);}
inline void publish(void* p){fn<void(__thiscall*)(void*)>(0x45fe40)(p);}
inline void* named(void* actor,const char* name){
 u32 key=0;fn<void*(__thiscall*)(u32*,const char*)>(0x41b120)(&key,name);
 return fn<void*(__thiscall*)(void*,const u32*,void*)>(0x501a50)(fn<void*(__thiscall*)(void*)>(0x41d0d0)(actor),&key,nullptr);
}
inline void chain(void* actor,int id){field<void*>(actor,0x3bc)=action(actor,id);}
inline void lock(void* actor,void* victim){fn<void(__thiscall*)(void*,u32)>(0x42d8e0)(actor,field<u32>(victim,0x1c));}
inline vec facing(void* actor){vec v;fn<vec*(__cdecl*)(vec*,void*)>(0x560990)(&v,static_cast<unsigned char*>(actor)+0x2c);return v;}
inline int team(void* p){return fn<int(__thiscall*)(void*)>(0x41a220)(p);}
inline bool manager_flag(u32 getter_va,unsigned slot){void* p=fn<getter>(getter_va)();return method<bool(__thiscall*)(void*)>(p,slot)(p);}
inline int rank(void* actor,const char* name){
 void* stats=field<void*>(actor,0x35c);if(!stats)return 0;
 auto* registry=fn<getter>(0x4c05b0)();
 const int count=std::min(field<int>(stats,0xbc),19);
 for(int i=0;i<count;++i){int id=field<std::uint16_t>(stats,0x20+i*8);
  auto* talent=method<void*(__thiscall*)(void*,int*)>(registry,0x14)(registry,&id);
  if(!talent)continue;auto* node=field<void*>(talent,8);if(!node)continue;
  const char* key=fn<const char*(__thiscall*)(void*,const char*)>(0x5649c0)(node,"name");
  if(key&&!_stricmp(key,name))return field<unsigned char>(stats,0x22+i*8)&0xf;
 }return 0;
}
inline void notify(void* actor,const char* text){
 auto* ui=fn<getter>(0x59ee20)();const u32 token=field<u32>(actor,0x1c);
 method<void(__thiscall*)(void*,const u32*,const char*)>(ui,0x40)(ui,&token,text);
}
inline void cannot_lift(void* actor){
 if(field<unsigned char>(actor,0x3d8)&1)return;
 auto* ui=fn<getter>(0x59ee20)();auto* messages=fn<getter>(0x4bdb30)();
 const char* text=method<const char*(__thiscall*)(void*,int)>(messages,8)(messages,0x89);
 const std::array<float,4> color{.8f,.8f,.8f,1.f};
 auto& previous=*reinterpret_cast<u32*>(0x789ae0);
 previous=method<u32(__thiscall*)(void*,u32,const char*,float,std::array<float,4>,int)>(ui,0x34)(ui,previous,text,.75f,color,0);
 fn<void(__thiscall*)(void*)>(0x421080)(actor);
}
struct query_holder {
 void* q=nullptr;
 query_holder(void* actor){const u32 excluded=field<u32>(actor,0x1c);fn<void**(__thiscall*)(void**,const void*,const u32*)>(0x4ab6d0)(&q,reinterpret_cast<void*>(0x753f64),&excluded);}
 ~query_holder(){fn<void(__thiscall*)(void**)>(0x4ab720)(&q);}
};
inline bool fallback(void* self,void* actor,void* input){return fn<bool(__thiscall*)(void*,void*,void*)>(0x4fbe80)(self,actor,input);}
inline u32 grabbable_body=0x429228;
// Source 391A0 has no PC-only nonhumanoidskeleton preamble. Its remaining
// native counterpart is identical in shape and preserves rule18 stun ownership.
__declspec(naked) inline bool source_grabbable(void*){__asm {
 push ecx
 push esi
 mov esi,ecx
 jmp dword ptr [grabbable_body]
}}
inline bool wall_lunge(void* actor,void* initial,void* entity){
 if(rank(actor,"acrobatics")<=2)return false;
 auto level=fn<bool(__thiscall*)(void**)>(0x4a95c0);
 if(!level(&initial)&&!cast(entity,0x41e4d0)&&!(entity&&(field<u32>(entity,4)&8)))return false;
 void* contact=nullptr;fn<void(__thiscall*)(void**,int)>(0x4a9570)(&contact,0);
 vec shift{},delta=mul(facing(actor),24.f);
 if(!fn<bool(__thiscall*)(void*,const vec*,const vec*,int,vec*,void**)>(0x42b6c0)(actor,&field<vec>(actor,0x20),&delta,0,&shift,&contact))return false;
 if(!level(&contact)&&!cast(fn<void*(__thiscall*)(void**)>(0x4a9590)(&contact),0x41e4d0))return false;
 const vec normal=*fn<const vec*(__thiscall*)(void**)>(0x4a9600)(&contact);
 const vec forward=facing(actor);
 // Guard D9D49 compares signed Z; rule39's jump path instead compares fabs(Z).
 if(!(normal.z<.2f&&forward.x*normal.x+forward.y*normal.y+forward.z*normal.z<-.5f))return false;
 const vec into{-normal.x,-normal.y,0};vec angles{};
 fn<vec*(__cdecl*)(vec*,const vec*)>(0x5607b0)(&angles,&into);
 method<void(__thiscall*)(void*,const vec*)>(actor,0x2c)(actor,&angles);
 const vec position=add(field<vec>(actor,0x20),shift);
 method<void(__thiscall*)(void*,const vec*)>(actor,0x1c)(actor,&position);
 if(auto* next=named(actor,"walllunge")){play(actor,next);return true;}return false;
}
// XML1 DA3A0 entry requests immediate input. D99D0 is the input policy,
// not an entry callback: the PC scheduler must consume its pending chain once.
inline void __fastcall guard_entry(void*,void*,void*,void*,void* context){
 if(context)field<unsigned char>(context,0x6c)|=1;
}
inline bool __fastcall guard_input(void* self,void*,void* actor,void* input){
 if(!actor||!input)return false;
 det_math::fp_env fp;
 const u32 held=field<u32>(actor,0x774);
 if(valid(held)&&!manager_flag(0x46dce0,0x1a8)){
  auto* entity=resolve(held);
  if(!cast(entity,0x42be20)&&!character(entity)&&!(cast(entity,0x41e510)&&(field<unsigned char>(entity,0x259)&1))){
   field<void*>(actor,0x3bc)=named(actor,"use_button");
  }else{
   fn<void(__thiscall*)(void*)>(0x42c120)(actor);
   if(!fallback(self,actor,input))chain(actor,0);
  }return true;
 }
 const bool friendly=!(*reinterpret_cast<unsigned char*>(0x70b6fd))&&!manager_flag(0x48de40,0x5c);
 // Source B1BA0(actor) includes active Might; PC's actor lift statistics expose
 // that same effective rank. The later raw-stats test deliberately uses rank().
 const bool allies=friendly&&(field<unsigned char>(field<void*>(actor,0x35c),0x2ae)&0x20)&&
  fn<unsigned char(__thiscall*)(void*)>(0x427dc0)(static_cast<unsigned char*>(actor)+0x470)>0;
 {
  query_holder holder(actor);auto* q=holder.q;if(!q){if(!fallback(self,actor,input))chain(actor,0);return true;}
  method<void(__thiscall*)(void*,int,int)>(q,0x40)(q,1,1);
  if(friendly&&!allies)fn<void(__cdecl*)(void**,void*)>(0x44fe10)(&holder.q,actor);
  const vec box{16,16,16};vec center{};const auto* p=method<const vec*(__thiscall*)(void*,vec*)>(actor,0x24)(actor,&center);center=*p;
  method<void(__thiscall*)(void*,const vec*,const void*,float,const vec*)>(q,0x70)(q,&center,static_cast<unsigned char*>(actor)+0x2c,48.f,&box);
  void* contact=nullptr;method<void**(__thiscall*)(void*,void**)>(q,0x20)(q,&contact);
  auto* entity=cast(fn<void*(__thiscall*)(void**)>(0x4a9590)(&contact),0x429110);
  // XML1 D9E73: the immediate move is already staged, so do not ask the
  // PC input scheduler to dispatch a pending chain on top of it.
  if(wall_lunge(actor,contact,entity))return false;
  if(!entity||(field<u32>(entity,4)&0x40010)||manager_flag(0x484990,0x3c)){
   if(!fallback(self,actor,input))chain(actor,0);
  }else if(auto* victim=character(entity)){
   if(!fn<bool(__thiscall*)(void*)>(reinterpret_cast<u32>(&source_grabbable))(victim))chain(actor,0);
   else if(allies&&team(actor)==team(victim)&&!manager_flag(0x48de40,0x5c)){
    if(field<unsigned char>(field<void*>(victim,0x35c),0x2ad)&0x80){play(actor,named(actor,"throwally"));lock(actor,victim);}
    else if(!fallback(self,actor,input))chain(actor,0);
   }else if(team(victim)!=0x1c){
    chain(actor,3);
    if(field<unsigned char>(victim,0x765)&1){
     if(fn<bool(__thiscall*)(void*,int,int)>(0x54d710)(victim,0x2c,0))notify(actor,"");
     else if(rank(actor,"acrobatics")>2){
      const std::array<vec,2> up{{{0,0,20},{0,0,0}}};method<float(__thiscall*)(void*,const void*)>(actor,0x130)(actor,up.data());
      play(actor,named(actor,"airgrabpickup"));
     }else if(rank(actor,"might")<=0&&!(field<unsigned char>(victim,0x765)&1)){play(actor,named(actor,"grabfailed"));}
     else chain(actor,25);
    }lock(actor,victim);
   }
  }else if(fn<bool(__thiscall*)(void*,void*)>(0x427f60)(actor,entity)){
   lock(actor,entity);chain(actor,24);field<unsigned char>(entity,0x30d)|=4;
  }else{cannot_lift(actor);chain(actor,0);}
 }
 return true;
}

struct tk_state {
 u32 victim=0,sound=0xffffffffu;std::int16_t impact=5;
 std::int8_t damage=1,heaviness=0,level=1;
 float distance=0,time=0,heading=0;vec offset{},last{};
};
inline u32 suspend_parse_resume=0x4e39f6;
__declspec(naked) inline void suspend_parse_native(){__asm {
 push esi
 push edi
 mov edi,[esp+12]
 jmp dword ptr [suspend_parse_resume]
}}
inline bool __fastcall tk_parse(void* event,void*,const char* key,const char* value){
 // Converted XML1 suspend triggers author `damage`. PC otherwise parses that
 // into CAttackInfo, leaving CCEAtkSuspend::ImpactDamage at its default five.
 // Use the native talent-aware word parser, including its encoded reference.
 if(key&&!_stricmp(key,"damage"))key="ImpactDamage";
 return reinterpret_cast<bool(__thiscall*)(void*,const char*,const char*)>(&suspend_parse_native)(event,key,value);
}
inline std::unordered_map<u32,tk_state> tk_states;
inline bool __cdecl tk_eligible(void* actor,void* entity,void* parameters){
 if(!entity||!parameters||(field<unsigned char>(entity,0xc5)&0x40))return false;
 if((field<u32>(entity,4)&0x40000)&&fn<bool(__thiscall*)(void*,int)>(0x41a820)(entity,0))return true;
 auto* target=cast(entity,0x429110);if(!target)return false;
 if((field<unsigned char>(target,0x30d)&4)||(field<u32>(target,4)&0x18)||!fn<bool(__thiscall*)(void*)>(0x41c540)(target))return false;
 if(lift_rule::heaviness(target)>field<std::int8_t>(parameters,0xa))return false;
 if(auto* victim=character(target))if(!fn<bool(__thiscall*)(void*)>(reinterpret_cast<u32>(&source_grabbable))(victim)||!fn<bool(__thiscall*)(void*)>(0x427e00)(victim))return false;
 if(cast(target,0x41e4d0))return false;
 return manager_flag(0x48de40,0x5c)||team(target)!=team(actor);
}
__declspec(naked) inline void tk_filter(){__asm {
 push edi
 push eax
 push ebx
 call tk_eligible
 add esp,12
 ret
}}
inline void acquire(void* actor,u32* selected,u32* auxiliary,void* parameters){
 const u32 service=0x4f2ed0;
 __asm {
  push parameters
  push auxiliary
  push selected
  mov ecx,actor
  call service
  add esp,12
 }
}
inline void auxiliary_effects(void* actor,void* target,int initial){
 const u32 service=0x4f29a0;
 __asm {
  push esi
  mov esi,actor
  push initial
  push target
  call service
  add esp,8
  pop esi
 }
}
inline float simulation_time(){void* game=fn<getter>(0x46dce0)();return method<float(__thiscall*)(void*)>(game,0x160)(game);}
inline u32 effect_serial(){void* manager=fn<getter>(0x450030)();return method<u32(__thiscall*)(void*)>(manager,0x20)(manager);}
inline void velocity(void* p,const vec& v){fn<void(__thiscall*)(void*,const vec*)>(0x473640)(p,&v);}
inline void tk_sound(tk_state& s,void* target,bool initial){
 auto audio=fn<getter>(0x592480);
 if(initial||!sound_valid(audio(),s.sound)){
  // XML1 DF556 uses Xbox slot34. PC's matching name resolver is slot38
  // (590BD0, ret12); PC slot34 is a bool query (590120, ret4).
  void* manager=audio();const u32 resource=method<u32(__thiscall*)(void*,const char*,int,int)>(manager,0x38)(manager,"character/phoen_m/power1a",0,0);
  manager=audio();s.sound=method<u32(__thiscall*)(void*,u32,const vec*,float,float,float)>(manager,0x4c)(manager,resource,&field<vec>(target,0x20),1.f,1300.f,150.f);
 }
 if(sound_valid(audio(),s.sound)){void* manager=audio();method<void(__thiscall*)(void*,u32,const vec*)>(manager,0x70)(manager,s.sound,&field<vec>(target,0x20));}
}
inline void stop_sound(tk_state& s){auto* manager=fn<getter>(0x592480)();method<void(__thiscall*)(void*,u32)>(manager,0x74)(manager,s.sound);s.sound=0xffffffffu;}
// PC's release service takes the vector by value and two additional mode
// arguments. Preserve its physical-mode cleanup while supplying source values.
inline void release(void* actor,tk_state& s,vec v,bool thrown){
 if(valid(s.victim))if(auto* target=cast(resolve(s.victim),0x429110)){
  field<u32>(target,4)&=~4u;publish(target);
  if(field<float>(target,0x27c)>0.f){
   fn<void(__thiscall*)(void*,int)>(0x41a2e0)(target,2);
   fn<void(__thiscall*)(void*,int,int)>(0x471a20)(target,0,1);
  }
  if(thrown){
   field<float>(target,0x2b8)=static_cast<float>(s.impact);field<unsigned char>(target,0x2e2)=static_cast<unsigned char>(s.damage);
   void* manager=fn<getter>(0x592480)();const u32 sound=method<u32(__thiscall*)(void*,const char*,int,int)>(manager,0x38)(manager,"character/phoen_m/power1b",0,0);
   method<void(__thiscall*)(void*,u32)>(target,0x8c)(target,sound);
  }
 }
 fn<void(__thiscall*)(void*,vec,float,std::int16_t,int,int)>(0x42e570)(actor,v,s.heading,s.level,thrown?1:0,0);
 field<unsigned char>(actor,0x259)&=~2;
 stop_sound(s);
}
inline void __fastcall tk_cancel(void*,void*,void* actor){
 const u32 token=field<u32>(actor,0x1c);auto it=tk_states.find(token);
 if(it==tk_states.end())return;
 auto& s=it->second;s.victim=field<u32>(actor,0x76c);vec v{};
 if(valid(s.victim))if(auto* target=cast(resolve(s.victim),0x429110))v=field<vec>(target,0x120);
 release(actor,s,v,false);tk_states.erase(token);
}
inline void __fastcall tk_entry(void*,void*,void* actor,void*,void* context){
 det_math::fp_env fp;
 const u32 token=field<u32>(actor,0x1c);
 if((field<unsigned char>(actor,0x259)&2)&&tk_states.count(token))return;
 tk_states.erase(token);
 if(!context||!field<void*>(context,4))return;
 // The native pre-entry executes suspend/tag dispatch and initial energy once.
 fn<void(__stdcall*)(void*,void*)>(0x4f2b00)(actor,context);
 auto* node=field<void*>(context,4);
 auto* event=method<void*(__thiscall*)(void*,int)>(node,0x1c)(node,1);
 event=fn<void*(__cdecl*)(void*,int,void*,void*,int)>(0x67234e)(event,0,reinterpret_cast<void*>(0x6db300),reinterpret_cast<void*>(0x6db700),0);
 if(!event)return;
 auto* stats=method<void*(__thiscall*)(void*,void*)>(node,0x28)(node,actor);if(!stats)return;
 tk_state s;
 s.impact=fn<std::int16_t(__thiscall*)(void*,void*)>(0x4dad20)(static_cast<unsigned char*>(event)+0x18,stats);
 s.heaviness=fn<std::int8_t(__thiscall*)(void*,void*)>(0x4abf70)(static_cast<unsigned char*>(event)+0x1a,stats);
 s.level=fn<std::int8_t(__thiscall*)(void*,void*)>(0x4abf70)(static_cast<unsigned char*>(event)+0x1c,stats);
 s.damage=field<std::int8_t>(field<void*>(event,0x14),0x1c);
 // Native acquisition retains engine world enumeration, scripts, class tests
 // and the installed LiftRule heaviness predicate. Its compact state is local;
 // it never aliases the source's distance/time/sound sidecar.
 std::array<unsigned char,0x2c> parameters{};
 field<std::int16_t>(parameters.data(),8)=s.impact;field<std::int8_t>(parameters.data(),0xa)=s.heaviness;field<std::int8_t>(parameters.data(),0xb)=s.level;
 u32 auxiliary=0,selected=0;
 acquire(actor,&selected,&auxiliary,parameters.data());
 if(valid(auxiliary))if(auto* object=fn<void*(__cdecl*)(u32)>(0x4297b0)(auxiliary)){
  if(auto* e=method<void*(__thiscall*)(void*,int)>(node,0x1c)(node,1)){
   auxiliary_effects(actor,object,1);method<void(__thiscall*)(void*,void*,void*)>(e,8)(e,object,actor);
  }
 }
 if(!valid(selected))return;
 auto* target=resolve(selected);if(!target)return;
 if(auto* scripted=cast(target,0x41b0e0);scripted&&(field<u32>(scripted,4)&0x40000)&&fn<bool(__thiscall*)(void*,int)>(0x41a820)(scripted,0)){
  method<void(__thiscall*)(void*,void*)>(scripted,0x9c)(scripted,actor);field<float>(actor,0x460)=simulation_time();return;
 }
 s.victim=selected;s.time=simulation_time();field<float>(actor,0x460)=s.time;field<float>(actor,0x5e8)=now();
 if(auto* victim=character(target)){play(victim,named(victim,"psycarry"));field<unsigned char>(victim,0x3d8)|=8;}
 if(auto* physical=cast(target,0x429110)){
  field<u32>(physical,0x2dc)=effect_serial();s.heading=field<float>(physical,0x34);velocity(physical,{});tk_sound(s,physical,true);
  field<u32>(physical,0x178)=token;
 }
 lock(actor,target);
 const vec difference=sub(field<vec>(target,0x20),field<vec>(actor,0x20));
 s.distance=length(difference);s.offset=difference;s.offset.z+=120.f;s.last=field<vec>(target,0x20);
 field<u32>(target,4)|=4;publish(target);field<unsigned char>(actor,0x259)|=2;
 tk_states[token]=s;
}
inline bool held(void* input,int bit){return input&&(field<u32>(input,0)&(1u<<bit));}
inline void __fastcall tk_update(void* self,void*,void* actor,void* input){
 det_math::fp_env fp;
 if(!(field<unsigned char>(actor,0x259)&2))return;
 auto it=tk_states.find(field<u32>(actor,0x1c));if(it==tk_states.end())return;
 auto& s=it->second;s.victim=field<u32>(actor,0x76c);auto* target=valid(s.victim)?cast(resolve(s.victim),0x429110):nullptr;
 if(!target||!(field<float>(target,0x27c)>0.f)){
  tk_cancel(self,nullptr,actor);field<void*>(actor,0x3bc)=named(actor,"idle");return;
 }
 const float time=simulation_time(),dt=time-s.time;
 bool exhausted=false;
 // The original divisor is a binary32 0.2 widened to binary64 (3D6610).
 const double period=static_cast<double>(.2f);
 if(static_cast<float>(std::fmod(static_cast<double>(time),period))<static_cast<float>(std::fmod(static_cast<double>(s.time),period))){
  float cost=(21-static_cast<int>(s.level))*.2f;
  if(fn<bool(__cdecl*)()>(0x4c87c0)()&&fn<int(__cdecl*)()>(0x4c8820)()==2)cost=100.f;
  exhausted=fn<float(__thiscall*)(void*)>(0x41d5d0)(actor)<cost;
  fn<void(__thiscall*)(void*,float)>(0x4318d0)(actor,cost);
 }
 const u32 name=field<u32>(actor,0x18);bool rogue=false;
 if(name){auto* table=fn<unsigned char*(__cdecl*)()>(0x602280)();const u32 index=name&0xffffff;const char* text=reinterpret_cast<const char*>(table+field<u32>(table,4+index*4)+0x4008);rogue=*text=='r'||*text=='R';}
 if(exhausted||!held(input,rogue?5:4)){
  vec v=field<vec>(target,0x120);const float horizontal=std::sqrt(v.x*v.x+v.y*v.y);
  if(horizontal>=5.f){v=sub(field<vec>(target,0x20),field<vec>(actor,0x20));v.z=0;v=mul(unit(v),5.f);}
  // XML1 3DCA9/3DD0B and PC 42E6DE/42E752 use the victim's facing
  // when release receives zero motion. Keep zero intact instead of the
  // source callback E0A6E's undefined reciprocal (which bypasses that test).
  const vec direction=unit({v.x,v.y,0.f});
  release(actor,s,direction,true);tk_states.erase(it);chain(actor,0);return;
 }
 if(static_cast<int>(s.time)<static_cast<int>(time))field<u32>(target,0x2dc)=effect_serial();
 s.time=time;field<float>(actor,0x460)=time;tk_sound(s,target,false);
 if(!character(target)){
  const vec angles{0,.25f,now()*1.7f+s.heading};method<void(__thiscall*)(void*,const vec*)>(target,0x2c)(target,&angles);
 }
 vec desired;
 const float max_step=dt*600.f;
 if(field<unsigned char>(actor,0x3d8)&1){
  if(held(input,6))s.distance+=.5f;else if(held(input,8))s.distance-=.5f;
  desired=mul(facing(actor),s.distance);desired.z+=50.f;
 }else{
  float speed=1.f;auto* camera=fn<getter>(0x4457e0)();vec temporary{};
  const auto* position=method<const vec*(__thiscall*)(void*,vec*)>(target,0x24)(target,&temporary);
  if(!method<bool(__thiscall*)(void*,const vec*,float,int)>(camera,0x178)(camera,position,40.f,0))speed=.1f;
  if(held(input,6))s.offset.z+=speed*dt*360.f;else if(held(input,8))s.offset.z-=speed*dt*360.f;
  s.offset.x+=field<float>(input,0x14)*field<float>(input,8)*speed*max_step;
  s.offset.y+=field<float>(input,0x18)*field<float>(input,8)*speed*max_step;
  desired=s.offset;
  vec toward=sub(field<vec>(target,0x20),field<vec>(actor,0x20));toward.z=0;vec angles{};
  fn<vec*(__cdecl*)(vec*,const vec*)>(0x5607b0)(&angles,&toward);
  const vec yaw_only{0,0,angles.z};method<void(__thiscall*)(void*,const vec*)>(actor,0x2c)(actor,&yaw_only);
 }
 const vec current=field<vec>(target,0x20);
 vec v=mul(sub(current,s.last),.98f);s.last=current;
 if((field<unsigned char>(target,0x30c)&8)&&length(field<vec>(target,0x120))>.01f){s.offset=sub(add(current,mul(unit(field<vec>(target,0x120)),120.f)),field<vec>(actor,0x20));desired=s.offset;}
 field<unsigned char>(target,0x30c)&=~8;
 vec point=add(desired,field<vec>(actor,0x20)),delta=sub(point,current);
 if(length(delta)>120.f){point=add(current,mul(unit(delta),120.f));s.offset=sub(point,field<vec>(actor,0x20));delta=sub(point,current);}
 vec step=delta;if(length(step)>.0001f&&length(step)>max_step)step=mul(unit(step),max_step);
 v=add(v,step);if(length(v)>.0001f&&length(v)>max_step)v=mul(unit(v),max_step);
 if(dt>.001f&&length(v)>.001f)v=mul(unit(v),length(v)/dt);
 if(length(v)<.01f)v={};
 if(length(delta)<length(v)&&delta.x*v.x+delta.y*v.y+delta.z*v.z>0.f)v=delta;
 velocity(target,v);
 if(length(v)*length(v)>1.f){
  const int damage=static_cast<std::int16_t>(s.damage/3);
  field<float>(target,0x2b8)=static_cast<float>(damage);field<u32>(target,0x2c0)=5;field<std::int16_t>(target,0x2bc)=500;
  field<u32>(target,0x2c4)=0;field<unsigned char>(target,0x2e2)=1;field<float>(target,0x2f0)=now()+1.f;
  if(damage>5&&lift_rule::heaviness(target)>1)field<u32>(target,0x2c8)=8;
  field<unsigned char>(target,0x2f8)=(field<unsigned char>(target,0x2f8)&~2)|1;field<u32>(target,0x2b4)=field<u32>(actor,0x1c);
 }else field<float>(target,0x2f0)=0;
 if(auto* victim=character(target))field<u32>(victim,0x770)=field<u32>(actor,0x1c);
 else fn<void(__thiscall*)(void*,float,int)>(0x475d10)(target,-1.f,0);
}
inline bool __fastcall tk_input(void*,void*,void* actor,void*){
 if(!(field<unsigned char>(actor,0x259)&2)){
  if(!field<void*>(actor,0x3bc))chain(actor,0);
  return field<void*>(actor,0x3bc)!=nullptr;
 }
 auto* next=action(actor,5);auto* current=field<void*>(actor,0x378);
 if(next&&next!=current&&method<void*(__thiscall*)(void*)>(next,0x30)(next)==method<void*(__thiscall*)(void*)>(current,0x30)(current)){
  field<void*>(actor,0x3bc)=next;return true;
 }return false;
}
}

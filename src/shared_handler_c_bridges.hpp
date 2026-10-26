#pragma once
#include "shared_handler_bridges.hpp"
#include <cmath>
#include <cstring>
namespace shared_handler_c_bridges {
using namespace shared_handler_bridges;
using contact_callback=void(__thiscall*)(void*,void*,void*,void*);
inline void dispatch(void* actor,u32 id,void* recipient) {
 auto* move=field<void*>(actor,0x378);
 method<void(__thiscall*)(void*,u32,void*,void*)>(move,0x14)(move,id,actor,recipient);
}
inline void* cast_actor(void* entity) { return fn<void*(__cdecl*)(void*)>(0x402b60)(entity); }
inline bool category(void* entity,u32 index_address) {
 if(!entity)return false;
 auto* type=method<void*(__thiscall*)(void*)>(entity,0)(entity);
 const u32 index=field<u32>(reinterpret_cast<void*>(index_address),0)+0x24;
 return (field<u32>(type,0x14+4*(index/32))&(1u<<(index%32)))!=0;
}
// PC's fixed five-name value manager cannot look up XML1's L3. Use the
// native resource reader, including its archive resolution, and select MIN.
// No random call and no mutation of the shared DMG3 table.
inline bool source_loaded=false;inline float source_minimum=0.f;
inline float source_l3() {
 auto& loaded=source_loaded;auto& minimum=source_minimum;
 if(loaded)return minimum;
 alignas(4) unsigned char root[12]{},top[16]{},row[16]{};
 fn<void*(__thiscall*)(void*)>(0x564a80)(root);
 if(fn<bool(__thiscall*)(void*,const char*,int,int,int)>(0x564eb0)(root,"data/values.xmlb",1,3,0)) {
  fn<void*(__thiscall*)(void*,void*,const char*)>(0x564b00)(top,root,"values");
  if(fn<bool(__thiscall*)(void*)>(0x564b60)(top)) {
   fn<void*(__thiscall*)(void*,void*,const char*)>(0x564df0)(row,top,"value");
   while(fn<bool(__thiscall*)(void*)>(0x564b60)(row)) {
    const char* name=fn<const char*(__thiscall*)(void*,const char*,const char*)>(0x564b70)(row,"name","");
    if(!_stricmp(name,"L3")) {minimum=fn<float(__thiscall*)(void*,const char*,float)>(0x564bf0)(row,"min",0.f);break;}
    fn<void(__thiscall*)(void*,int)>(0x564e70)(row,0);
   }
   loaded=true;
  }
 }
 fn<void(__thiscall*)(void*)>(0x564d00)(root);
 return minimum;
}
inline short narrow(float value) {return static_cast<short>(static_cast<std::int64_t>(value));}
inline float __cdecl jump_damage(float timing) {
 short value=narrow(source_l3());
 if(timing>.2f) {
  value=narrow(static_cast<float>(value)*.75f);
  // XML1 keeps 1-timing in x87 until the integer conversion.
  std::int64_t converted=0;const int signed_value=value;
  __asm {
   fld1
   fsub timing
   fimul signed_value
   sub esp, 4
   fnstcw [esp]
   mov ax,[esp]
   or ax,0xc00
   mov [esp+2],ax
   fldcw [esp+2]
   fistp qword ptr converted
   fldcw [esp]
   add esp,4
  }
  value=static_cast<short>(converted);
 }
 return static_cast<float>(value);
}
inline void __cdecl produce_jump(void* actor,float timing) {field<float>(actor,0x2b8)=jump_damage(timing);}
inline void __cdecl produce_fly(void* actor) {field<float>(actor,0x2b8)=static_cast<float>(narrow(source_l3()));}
inline u32 jump_resume=0x4ee9ef,fly_resume=0x4eebb9;
__declspec(naked) inline void jump_damage_bridge() {
 __asm {
  pushfd
  pushad
  mov ebx,esp
  sub esp,528
  and esp,-16
  mov [esp+512],ebx
  fxsave [esp]
  push dword ptr [ebx+36+0x18]
  push esi
  call produce_jump
  add esp,8
  fxrstor [esp]
  mov esp,[esp+512]
  popad
  popfd
  jmp dword ptr [jump_resume]
 }
}
__declspec(naked) inline void fly_damage_bridge() {
 __asm {
  pushfd
  pushad
  mov ebx,esp
  sub esp,528
  and esp,-16
  mov [esp+512],ebx
  fxsave [esp]
  push esi
  call produce_fly
  add esp,4
  fxrstor [esp]
  mov esp,[esp+512]
  popad
  popfd
  jmp dword ptr [fly_resume]
 }
}
inline void __fastcall bounce_contact(void*,void*,void* actor,void* hit,void*) {
 dispatch(actor,1,cast_actor(hit));
 const float zero[3]{};
 fn<void(__thiscall*)(void*,const float*)>(0x473640)(actor,zero);
 field<float>(actor,0x740)=now();
}
// The rest of fastball's collision response remains native. Its event is
// emitted here before the unchanged normal/angle/speed response.
inline void __cdecl fastball_event(void* actor,void* hit) {dispatch(actor,1,cast_actor(hit));}
inline u32 fastball_resume=0x4f4b16;
__declspec(naked) inline void fastball_bridge() {
 __asm {
  pushfd
  pushad
  mov ebx,esp
  sub esp,528
  and esp,-16
  mov [esp+512],ebx
  fxsave [esp]
  push esi
  push edi
  call fastball_event
  add esp,8
  fxrstor [esp]
  mov esp,[esp+512]
  popad
  popfd
  jmp dword ptr [fastball_resume]
 }
}
inline bool __fastcall colossus_input(void* self,void*,void* actor,void* buttons) {
 auto* current=field<void*>(actor,0x378);
 if(auto* e=method<void*(__thiscall*)(void*,u32)>(current,0x1c)(current,100)) {
  const float energy=fn<float(__thiscall*)(void*)>(0x41d5d0)(actor);
  const float cost=method<float(__thiscall*)(void*)>(e,0x20)(e);
  if(cost>energy) {
   field<void*>(actor,0x3bc)=action(actor,0);
   method<void(__thiscall*)(void*,void*)>(self,0xc)(self,actor);
   return true;
  }
 }
 if(field<u32>(buttons,0)&0x40) {field<void*>(actor,0x3bc)=action(actor,4);return true;}
 return fn<input>(0x4fce50)(self,actor,buttons);
}
// Preserve the source x87 product/store order at the crash threshold and
// recoil. In particular X uses the live reciprocal while Y uses its float store.
inline bool crash_angle(const float* velocity,const float* normal) {
 const float horizontal_squared=velocity[0]*velocity[0]+velocity[1]*velocity[1];
 if(!(horizontal_squared>0.f)||!std::isfinite(horizontal_squared))return false;
 float direction[3]{},reciprocal=0.f;unsigned char result=0;
 constexpr float limit=-.71f;
 __asm {
  mov edx,velocity
  fld dword ptr [edx]
  fld dword ptr [edx+4]
  fld st(0)
  fmul st(0),st(1)
  fld st(2)
  fmul st(0),st(3)
  faddp st(1),st(0)
  fsqrt
  fld1
  fdivrp st(1),st(0)
  fst reciprocal
  fmul st(0),st(2)
  fstp direction[0]
  fmul reciprocal
  fstp direction[4]
  fstp st(0)
  fldz
  fmul reciprocal
  fstp direction[8]
  mov edx,normal
  fld direction[8]
  fmul dword ptr [edx+8]
  fld direction[4]
  fmul dword ptr [edx+4]
  faddp st(1),st(0)
  fld direction[0]
  fmul dword ptr [edx]
  faddp st(1),st(0)
  fcomp limit
  fnstsw ax
  test ah,5
  setnp result
 }
 return result!=0;
}
inline void recoil(float* output,const float* velocity,const float* normal) {
 constexpr float strength=450.f;float z=0;
 __asm {
  mov edx,normal
  fld dword ptr [edx]
  fmul strength
  fld dword ptr [edx+4]
  fmul strength
  fld dword ptr [edx+8]
  fmul strength
  fstp z
  mov edx,velocity
  mov eax,output
  fxch st(1)
  fadd dword ptr [edx]
  fstp dword ptr [eax]
  fadd dword ptr [edx+4]
  fstp dword ptr [eax+4]
  fld z
  fadd dword ptr [edx+8]
  fstp dword ptr [eax+8]
 }
}
inline void __fastcall colossus_contact(void*,void*,void* actor,void* hit,void* contact) {
 auto* damageable=fn<void*(__cdecl*)(void*)>(0x429110)(hit);
 auto* struck=cast_actor(hit);
 bool damaged=true;
 if(damageable) {
  const short before=narrow(field<float>(damageable,0x27c));
  // PC CCEAtkMeleePunch's second actor is the talent-evaluation owner
  // (4DC40D -> 4DC353/4DC37E and 4DC44D), not a direct hit recipient.
  // XML1 CF250 ignores its second actor and uses caster/literal data. Native
  // PC Colossus supplies actor twice; a soldier here resolves %xco_* to zero.
  // Keep the collision victim for the health/crash decision below.
  dispatch(actor,1,actor);
  damaged=!(static_cast<float>(before)<=field<float>(damageable,0x27c));
 }
 const bool level=fn<bool(__thiscall*)(void*)>(0x4a95c0)(contact);
 const bool crash=level || (!category(hit,0x72a4d8) && damageable && ((field<u32>(damageable,4)&8)||!damaged)) ||
  (struck&&(field<unsigned char>(field<void*>(struck,0x35c),0x2ad)&8));
 if(!crash) {
  auto* camera=fn<getter>(0x4457e0)();
  method<void(__thiscall*)(void*,float,float)>(camera,0x98)(camera,3.f,.5f);return;
 }
 const auto* motion=reinterpret_cast<const float*>(static_cast<unsigned char*>(actor)+0x120);
 const auto* normal=fn<const float*(__thiscall*)(void*)>(0x4a9610)(contact);
 if(!crash_angle(motion,normal))return;
 const auto* position=fn<const float*(__thiscall*)(void*)>(0x4a95f0)(contact);
 if(!damageable&&level&&position[2]-field<float>(actor,0x28)<14.f)return;
 fn<void(__thiscall*)(void*,void*,int,float,int,int,int)>(0x432030)(actor,action(actor,0x18),-1,-1.f,0,0,0);
 const auto* rebound=fn<const float*(__thiscall*)(void*)>(0x4a9600)(contact);
 float velocity[3];recoil(velocity,motion,rebound);
 fn<void(__thiscall*)(void*,const float*)>(0x473640)(actor,velocity);
 // Play/velocity/effect services may advance a query owner's scratch
 // record. XML1 re-fetches and copies the effect vectors after the move.
 const auto* effect_point=fn<const float*(__thiscall*)(void*)>(0x4a95f0)(contact);
 const float effect_position[3]{effect_point[0],effect_point[1],effect_point[2]};
 const auto* effect_direction=fn<const float*(__thiscall*)(void*)>(0x4a9610)(contact);
 const float effect_normal[3]{effect_direction[0],effect_direction[1],effect_direction[2]};
 auto* effects=fn<getter>(0x450030)();
 auto effect=method<u32(__thiscall*)(void*,int,int,const float*,const float*,int,int)>(effects,0x14)(effects,0xf,-1,effect_position,effect_normal,0,0);
 method<void(__thiscall*)(void*,u32)>(actor,0x84)(actor,effect);
 // The material accessor has no stack arguments. The following sound
 // method consumes the four arguments (PC ret 0x10, matching XML1).
 const u32 sound_parameter=field<u32>(reinterpret_cast<void*>(0x6db5b0),0);
 const auto* sound_position=fn<const float*(__thiscall*)(void*)>(0x4a95f0)(contact);
 auto* impact=fn<void*(__thiscall*)(void*)>(0x4a97a0)(contact);
 fn<void(__thiscall*)(void*,int,const float*,float,u32)>(0x487970)(impact,0,sound_position,1.f,sound_parameter);
}
}

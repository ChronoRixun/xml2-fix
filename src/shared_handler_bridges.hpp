#pragma once
#include <cstdint>

// Native callback ABIs and PC field/enum mappings, derived from the owner's two
// executables. These are selected only by the startup transaction in shared_handlers.
namespace shared_handler_bridges {
using u32=std::uint32_t;
template<class T> inline T& field(void* p,unsigned offset) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset); }
template<class F> inline F fn(u32 va) { return reinterpret_cast<F>(va); }
template<class F> inline F method(void* p,unsigned slot) { return reinterpret_cast<F>(field<void**>(p,0)[slot/4]); }
using getter=void*(__cdecl*)();
using entry=void(__thiscall*)(void*,void*,void*,void*);
using update=void(__thiscall*)(void*,void*,void*);
using input=bool(__thiscall*)(void*,void*,void*);
inline float now() { return fn<float(__cdecl*)()>(0x45fe60)(); }
inline void* action(void* actor,u32 id) { return fn<void*(__thiscall*)(void*,u32)>(0x427c80)(actor,id); }
inline void play(void* actor,void* move) { fn<void(__thiscall*)(void*,void*,int)>(0x42c340)(actor,move,1); }
inline void* event(void* actor,u32 id) {
 auto* node=field<void*>(actor,0x3c0);
 return method<void*(__thiscall*)(void*,u32)>(node,0x1c)(node,id);
}
inline void __fastcall teleport_land(void* self,void*,void* actor,void* move,void* previous) {
 fn<entry>(0x4fbdd0)(self,actor,move,previous);
 // Map the original animation NAMES, not a numeric delta: XML1 power2/3/4/6.
 constexpr u32 animations[]{0x75,0x78,0x79,0x7e};
 const int choice=fn<int(__cdecl*)(int,int,int)>(0x5601e0)(0,3,1);
 fn<void(__thiscall*)(void*,u32,int,float,int,int)>(0x430cd0)(actor,animations[choice],12,1.f,0,0);
}
inline void __fastcall popup_decision(void*,void*,void*,void*) {}

inline bool expired(void* actor) { return field<float>(actor,0x740)>0.f && now()>field<float>(actor,0x5e8); }
inline bool sound_valid(void* audio,u32 handle) { return method<bool(__thiscall*)(void*,u32)>(audio,0x60)(audio,handle); }
inline void __fastcall charge_enter(void*,void*,void* actor,void*,void*) {
 field<float>(actor,0x5e8)=now()+field<float>(actor,0x740);
 auto* e=event(actor,100);
 if(e && field<u32>(actor,0x73c)==0xffffffffu) {
  auto* sound=fn<void*(__cdecl*)(void*,int,void*,void*,int)>(0x67234e)(e,0,reinterpret_cast<void*>(0x6db300),reinterpret_cast<void*>(0x6db604),0);
  if(sound) {
   auto* audio=fn<getter>(0x592480)();
   field<u32>(actor,0x73c)=method<u32(__thiscall*)(void*,u32,void*,float,float,float)>(audio,0x4c)(audio,field<u32>(sound,0x18),static_cast<unsigned char*>(actor)+0x20,1.f,1300.f,150.f);
  }
 }
 field<float>(actor,0x2f0)=0.f;
}
inline void __fastcall charge_exit(void*,void*,void* actor) {
 auto& handle=field<u32>(actor,0x73c);
 if(handle!=0xffffffffu) {
  auto* audio=fn<getter>(0x592480)();
  if(sound_valid(audio,handle)) {
   // XML1 re-fetches the manager before each operation; retain that lifecycle.
   audio=fn<getter>(0x592480)();
   method<void(__thiscall*)(void*,u32)>(audio,0x74)(audio,handle);
   handle=0xffffffffu;
  }
 }
 field<float>(actor,0x2f0)=0.f;
}
inline bool __fastcall charge_input(void* self,void*,void* actor,void* buttons) {
 if(expired(actor)) { field<void*>(actor,0x3bc)=action(actor,0x18);return true; }
 return fn<input>(0x4fce50)(self,actor,buttons);
}
inline void __fastcall charge_update(void* self,void*,void* actor,void* frame) {
 if(expired(actor))play(actor,action(actor,0x18));
 const auto handle=field<u32>(actor,0x73c);
 if(handle!=0xffffffffu) {
  auto* audio=fn<getter>(0x592480)();
  if(sound_valid(audio,handle)) {
   audio=fn<getter>(0x592480)();
   method<void(__thiscall*)(void*,u32,void*)>(audio,0x70)(audio,field<u32>(actor,0x73c),static_cast<unsigned char*>(actor)+0x20);
  }
 }
 fn<update>(0x4ebf30)(self,actor,frame); // unchanged one-second contact and movement body
}
inline void __cdecl charge_event(void* actor) {
 if(auto* e=event(actor,200))method<void(__thiscall*)(void*,void*,void*)>(e,4)(e,actor,actor);
}
inline u32 charge_event_resume=0x4ec037;
__declspec(naked) inline void charge_event_bridge() {
 __asm {
  pushfd
  pushad
  mov ebx, esp
  sub esp, 528
  and esp, -16
  mov [esp+512], ebx
  fxsave [esp]
  push esi
  call charge_event
  add esp, 4
  fxrstor [esp]
  mov esp, [esp+512]
  popad
  popfd
  cmp word ptr [esi+0x744], 0
  jmp dword ptr [charge_event_resume]
 }
}
inline bool __cdecl nightcrawler_eligible(void* actor,void* target) {
 const auto team=fn<int(__thiscall*)(void*)>(0x41a220);
 if(team(actor)==team(target))return true;
 if(team(target)!=0x1c)return false; // XML1 team 0x1a = PC neutral 0x1c
 auto* ai=fn<getter>(0x5252e0)();
 return method<bool(__thiscall*)(void*,u32)>(ai,0x78)(ai,field<u32>(target,0x7dc));
}
inline u32 night_yes=0x4f065d,night_no=0x4f06a5;
__declspec(naked) inline void nightcrawler_bridge() {
 __asm {
  pushfd
  pushad
  push ebp
  push esi
  call nightcrawler_eligible
  add esp, 8
  test al, al
  jz denied
  popad
  popfd
  jmp dword ptr [night_yes]
 denied:
  popad
  popfd
  jmp dword ptr [night_no]
 }
}

inline constexpr float probe_distance=60.f;
inline void probe_end(float* inout,const float* origin) {
 // XML1 retains X's product in x87 until the addition, but stores Y/Z's
 // products as binary32 first. Ordinary SSE float expressions round X early.
 __asm {
  mov eax,inout
  mov edx,origin
  fld dword ptr [eax]
  fmul dword ptr [probe_distance]
  fld dword ptr [eax+4]
  fmul dword ptr [probe_distance]
  fstp dword ptr [eax+4]
  fld dword ptr [eax+8]
  fmul dword ptr [probe_distance]
  fstp dword ptr [eax+8]
  fadd dword ptr [edx]
  fstp dword ptr [eax]
  fld dword ptr [eax+4]
  fadd dword ptr [edx+4]
  fstp dword ptr [eax+4]
  fld dword ptr [eax+8]
  fadd dword ptr [edx+8]
  fstp dword ptr [eax+8]
 }
}
inline void* __cdecl gambit_acquire(void* actor) {
 if(field<unsigned char>(actor,0x3d8)&1)return nullptr;
 float position[3]{},direction[3]{},bounds[3]{};
 const auto* origin=method<const float*(__thiscall*)(void*,float*)>(actor,0x24)(actor,position);
 for(unsigned i=0;i<3;++i)position[i]=origin[i];
 fn<float*(__cdecl*)(float*,void*)>(0x560990)(direction,static_cast<unsigned char*>(actor)+0x2c);
 probe_end(direction,position);
 u32 token=field<u32>(actor,0x1c);
 auto* source=fn<void*(__thiscall*)(u32*)>(0x4654b0)(&token);
 const auto* extent=method<const float*(__thiscall*)(void*,float*)>(source,0x28)(source,bounds);
 void* trace=nullptr;
 if(!fn<bool(__cdecl*)(u32,const float*,const float*,const float*,void**)>(0x4ab960)(token,position,direction,extent,&trace))return nullptr;
 if(fn<bool(__thiscall*)(void**)>(0x4a95c0)(&trace))return nullptr;
 auto* entity=fn<void*(__thiscall*)(void**)>(0x4a9590)(&trace);
 if(!entity)return nullptr;
 token=field<u32>(entity,0x1c);
 if(!token || !fn<bool(__thiscall*)(u32*)>(0x4654d0)(&token))return nullptr;
 if(fn<void*(__cdecl*)(u32)>(0x41fb10)(token))return nullptr; // XML1 excludes actors
 auto* object=fn<void*(__cdecl*)(u32)>(0x4297b0)(token);
 if(!object || field<unsigned char>(object,0x31c)>=10)return nullptr;
 if(fn<bool(__thiscall*)(void*,int,int)>(0x54d710)(object,0x5c,0))return nullptr; // XML1 effect 0x39
 auto* node=field<void*>(actor,0x3c0);
 const auto* name=method<void*(__thiscall*)(void*,int)>(node,0x118)(node,0x19);
 auto* style=fn<void*(__thiscall*)(void*)>(0x41d0d0)(actor);
 return fn<void*(__thiscall*)(void*,const void*,int)>(0x501a50)(style,name,0);
}
inline u32 gambit_yes=0x4ed659,gambit_no=0x4ed5dd;
__declspec(naked) inline void gambit_bridge() {
 __asm {
  pushfd
  pushad
  mov ebp, esp
  sub esp, 528
  and esp, -16
  mov [esp+512], ebp
  fxsave [esp]
  push ebx
  call gambit_acquire
  add esp, 4
  mov ebp, [esp+512]
  mov [ebp+28], eax
  fxrstor [esp]
  mov esp, ebp
  popad
  popfd
  test eax, eax
  jz no_object
  mov esi, eax
  jmp dword ptr [gambit_yes]
 no_object:
  mov eax, [ebx+0x3c0]
  jmp dword ptr [gambit_no]
 }
}
}

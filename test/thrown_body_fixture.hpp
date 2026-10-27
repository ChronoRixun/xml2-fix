#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

// Real collision forwarding and retail impact receiver/descriptor/cooldown.
// Only the terminal damage sink and world/render services are synthetic.
namespace thrown_body_fixture {
using u32=std::uint32_t;
struct vec {float x,y,z;};
inline std::array<unsigned char,0x900> body{},world{},type{};
inline std::array<void*,120> body_vt{},world_vt{};
inline int receipts=0;inline u32 credited=0;inline bool arguments=false;
inline float clock_now=1.f;inline int derived_calls=0;
inline vec zero{};inline std::array<unsigned char,16> game{};inline std::array<void*,100> game_vt{};
template<class T>T& at(void* p,u32 n){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+n);}
inline void branch(u32 va,const void* f){auto* p=reinterpret_cast<unsigned char*>(va);p[0]=0xe9;at<u32>(p,1)=u32(reinterpret_cast<std::uintptr_t>(f))-va-5;}
inline void* __fastcall resolve(u32* h,void*){return *h==333?world.data():nullptr;}
inline void* __fastcall kind(void*,void*){return type.data();}
inline vec* __fastcall copy(vec* out,void*,const vec* in){*out=*in;return out;}
inline void __fastcall receive(void* who,void*,u32 collision,vec motion,u32 owner){
 ++receipts;credited=owner;arguments=who==body.data()&&collision==333&&motion.x==600&&motion.y==0&&motion.z==-200;
}
inline float __fastcall length(vec* p,void*){return std::sqrt(p->x*p->x+p->y*p->y+p->z*p->z);}
inline float __fastcall normalize(vec* p,void*){const float n=length(p,nullptr);if(n>0){p->x/=n;p->y/=n;p->z/=n;}return n;}
inline float* __fastcall component(vec* p,void*,int i){return &p->x+i;}
inline float __cdecl now(){return clock_now;}
inline float __fastcall game_time(void*,void*){return clock_now;}
inline void* __cdecl get_game(){return game.data();}
inline bool __fastcall valid(u32* p,void*){return *p==111||*p==333;}
inline void* __cdecl character(void* p){return p==body.data()?p:nullptr;}
inline void* __cdecl no_character(u32){return nullptr;}
inline float __fastcall derived(void*,void*,float,u32,int){++derived_calls;return 0.f;}
inline void __fastcall health_sink(void* who,void*,void* info,int,int,int){++receipts;credited=at<u32>(info,0);at<float>(who,0x27c)-=at<float>(info,4);}
template<class Load,class Check>void run(Load load,Check check){
 const u32 indices[]{0x718448,0x753f68,0x70b840,0x778a50};std::array<u32,4> old_indices{};for(unsigned i=0;i<4;++i)old_indices[i]=at<u32>(reinterpret_cast<void*>(indices[i]),0);
 const auto old_world_flag=at<unsigned char>(reinterpret_cast<void*>(0x753139),0);
 std::vector<std::pair<u32,std::vector<unsigned char>>> ranges;
 auto native=[&](u32 a,std::size_t n){auto* p=reinterpret_cast<unsigned char*>(a);ranges.push_back({a,{p,p+n}});load(a,n);};
 auto diagnostic=AddVectoredExceptionHandler(1,[](PEXCEPTION_POINTERS e)->LONG{
  std::printf("body fixture exception %08lX at %p ESP=%08lX return=%08X ECX=%08lX ESI=%08lX EBP=%08lX\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,e->ContextRecord->Esp,*reinterpret_cast<u32*>(e->ContextRecord->Esp),e->ContextRecord->Ecx,e->ContextRecord->Esi,e->ContextRecord->Ebp);ExitProcess(1);
 });
 auto* gate=reinterpret_cast<unsigned char*>(0x42c09e);
 const std::array<unsigned char,2> installed{gate[0],gate[1]};
 std::array<unsigned char,5> resolver{};std::memcpy(resolver.data(),reinterpret_cast<void*>(0x4654b0),5);
 const auto old_copy=*reinterpret_cast<void**>(0x67f9e0);*reinterpret_cast<void**>(0x67f9e0)=reinterpret_cast<void*>(&copy);
 native(0x42bf70,0x1a7);if(installed[0]==0x90&&installed[1]==0x90)std::memcpy(gate,installed.data(),2);
 branch(0x4654b0,&resolve);
 for(int physical:{0,1})for(float damage:{5.f,19.f,42.f}){
  body.fill(0);world.fill(0);type.fill(0);body_vt.fill(nullptr);world_vt.fill(nullptr);
  at<void*>(body.data(),0)=body_vt.data();at<void*>(world.data(),0)=world_vt.data();world_vt[0]=reinterpret_cast<void*>(&kind);
  body_vt[0x1b0/4]=reinterpret_cast<void*>(&receive);at<u32>(world.data(),0x1c)=333;at<float>(world.data(),0x27c)=100;
  at<u32>(reinterpret_cast<void*>(0x718448),0)=0;at<u32>(reinterpret_cast<void*>(0x753f68),0)=1;at<u32>(reinterpret_cast<void*>(0x70b840),0)=2;
  at<u32>(type.data(),0x18)=0x10|(physical?0x20:0);
  at<float>(body.data(),0x27c)=100;at<float>(body.data(),0x2b8)=damage;at<u32>(body.data(),0x770)=111;
  receipts=0;credited=0;arguments=false;
  reinterpret_cast<bool(__thiscall*)(void*,u32,vec)>(0x42bf70)(body.data(),333,{600,0,-200});
  check(receipts==1&&arguments&&credited==111,"thrown body reaches native impact receiver on world and physical contact with thrower attribution");
  check(at<float>(body.data(),0x27c)==100,"forwarding-only receiver does not fabricate damage");
 }
 // Keep the real 499100 decision, 429860 constructor, 453DB0 descriptor copy,
 // and 4353B0 conversion. Verify health at the terminal virtual+B0 sink.
 const u32 services[]{0x45fe60,0x46dce0,0x4654d0,0x402b60,0x41fb10};
 std::array<std::array<unsigned char,5>,5> saved{};for(unsigned i=0;i<5;++i)std::memcpy(saved[i].data(),reinterpret_cast<void*>(services[i]),5);
 const u32 imports[]{0x67fa2c,0x67fa50,0x67fa54,0x67fa5c,0x67f9c4};std::array<void*,5> old_imports{};for(unsigned i=0;i<5;++i)old_imports[i]=*reinterpret_cast<void**>(imports[i]);
 native(0x499100,0x632);native(0x4297f0,0xef);native(0x4539a0,0x55);native(0x453c20,0xc5);native(0x453db0,0x1e);native(0x4350d0,0xb9);native(0x4353b0,0x86);
 for(u32 a:{0x680030u,0x680034u,0x68012cu,0x681ebcu})native(a,4);
 at<unsigned char>(reinterpret_cast<void*>(0x753139),0)=0;at<u32>(reinterpret_cast<void*>(0x778a50),0)=3;
 *reinterpret_cast<void**>(0x67fa2c)=reinterpret_cast<void*>(&copy);*reinterpret_cast<void**>(0x67fa50)=reinterpret_cast<void*>(&length);*reinterpret_cast<void**>(0x67fa54)=&zero;
 *reinterpret_cast<void**>(0x67fa5c)=reinterpret_cast<void*>(&normalize);
 *reinterpret_cast<void**>(0x67f9c4)=reinterpret_cast<void*>(&component);
 branch(0x45fe60,&now);branch(0x46dce0,&get_game);branch(0x4654d0,&valid);branch(0x402b60,&character);branch(0x41fb10,&no_character);
 at<void*>(game.data(),0)=game_vt.data();game_vt[0x160/4]=reinterpret_cast<void*>(&game_time);
 for(float damage:{5.f,19.f,42.f})for(int mode:{0,1,2}){
  body.fill(0);at<void*>(body.data(),0)=body_vt.data();at<u32>(type.data(),0x18)=0x10;
  body_vt[0x1b0/4]=reinterpret_cast<void*>(0x499100);body_vt[0xb0/4]=reinterpret_cast<void*>(&health_sink);body_vt[0x1b8/4]=reinterpret_cast<void*>(&derived);
  at<float>(body.data(),0x27c)=100;at<u32>(body.data(),0x2b4)=111;at<float>(body.data(),0x2b8)=damage;at<u32>(body.data(),0x2c0)=3;at<u32>(body.data(),0x2c4)=0x80;
  at<float>(body.data(),0x2f0)=mode==1?0.f:10.f;at<unsigned char>(body.data(),0x2f8)=1;at<u32>(body.data(),0x770)=111;
  receipts=0;credited=0;derived_calls=0;clock_now=1.f;const vec motion=mode==2?vec{}:vec{600,0,-200};
  auto hit=reinterpret_cast<bool(__thiscall*)(void*,u32,vec)>(0x42bf70);hit(body.data(),333,motion);
  check(receipts==(mode==0?1:0)&&at<float>(body.data(),0x27c)==100-(mode==0?damage:0),"retail impact receiver applies armed throw amount; expired and stationary contacts do not damage");
  if(mode==0){check(credited==111&&at<float>(body.data(),0x2f4)==clock_now,"retail receiver retains thrower and stamps impact cooldown");hit(body.data(),333,motion);check(receipts==1&&at<float>(body.data(),0x27c)==100-damage,"retail receiver prevents a second damage application inside cooldown");}
  if(mode==1)check(derived_calls==1,"expired throw delegates to installed ordinary world-impact formula");
 }
 for(unsigned i=0;i<5;++i)std::memcpy(reinterpret_cast<void*>(services[i]),saved[i].data(),5);
 for(unsigned i=0;i<5;++i)*reinterpret_cast<void**>(imports[i])=old_imports[i];
 std::memcpy(reinterpret_cast<void*>(0x4654b0),resolver.data(),5);
 *reinterpret_cast<void**>(0x67f9e0)=old_copy;
 for(auto i=ranges.rbegin();i!=ranges.rend();++i)std::memcpy(reinterpret_cast<void*>(i->first),i->second.data(),i->second.size());
 for(unsigned i=0;i<4;++i)at<u32>(reinterpret_cast<void*>(indices[i]),0)=old_indices[i];at<unsigned char>(reinterpret_cast<void*>(0x753139),0)=old_world_flag;
 RemoveVectoredExceptionHandler(diagnostic);
}
}

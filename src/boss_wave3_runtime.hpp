#pragma once
#include "boss_runtime.hpp"
#include "boss_owner_rules.hpp"
#include "mold_effects.hpp"
#include "xml1_hook_transaction.hpp"
#include <algorithm>
#include <vector>
namespace boss_wave3_runtime {
using namespace boss_runtime;
inline bool alternate(void* b,void* a,int slot){return at<void*>(a,0x378)&&fn<unsigned char(__fastcall*)(void*,void*,int)>(0x529690)(b,nullptr,slot)!=0;}
inline bool facing(void* b,void* a,void* other,u32* output,float tolerance){
    const int turn=fn<int(__fastcall*)(void*,void*,void*,void*,unsigned char,float)>(0x509ba0)(b,nullptr,a,other,0,tolerance);
    if(turn)buttons(output,turn==-1?0x08000000:0x10000000);return turn==0;
}
inline void move_to(void* actor,void* point){
    fn<void(__fastcall*)(void*,void*,u32)>(0x42c7c0)(actor,nullptr,0);
    virt<void(__fastcall*)(void*,void*,const float*,u32,float)>(actor,0x160)(actor,nullptr,
        reinterpret_cast<const float*>(static_cast<const unsigned char*>(point)+0x20),0,3.f);
}
inline void clear_motion(void* a){stop(a);fn<void(__fastcall*)(void*,void*)>(0x421b90)(a,nullptr);}
inline bool target_blocked(void* a){return (at<unsigned char>(a,0x764)&0x40)||(at<unsigned char>(a,0x765)&1);}
inline bool camera(void* entity){
    auto* c=fn<void*(__cdecl*)()>(0x4457e0)();float scratch[3]{};
    auto* pos=virt<const float*(__fastcall*)(void*,void*,float*)>(entity,0x24)(entity,nullptr,scratch);
    return virt<unsigned char(__fastcall*)(void*,void*,const float*,float,void*)>(c,0x178)(c,nullptr,pos,60,nullptr)!=0;
}
inline void activate(void* entity,void* actor){virt<unsigned char(__fastcall*)(void*,void*,void*,int)>(entity,0xd4)(entity,nullptr,actor,0);}
inline void wobble(void* tile){
    fn<void(__fastcall*)(void*,void*,const char*,const char*)>(0x475240)(tile,nullptr,"asteroid_m/tile_wobble/mp_tile",nullptr);
    fn<void(__fastcall*)(void*,void*,int,int)>(0x470d60)(tile,nullptr,1,0);
}
// XML1 AI-manager vt+0x22c -> PC vt+0x240 (0x523c50 -> 0x5044f0).
// PC adds two response-policy bytes. Zero keeps the base radial-danger policy.
// Team none is XML1 26 -> PC 28; lifetime .5, null source handle are unchanged.
inline void danger(void* entity,float radius){
    auto* manager=fn<void*(__cdecl*)()>(0x5252e0)();
    virt<void(__fastcall*)(void*,void*,const float*,int,float,float,u32,unsigned char,unsigned char)>(manager,0x240)(manager,nullptr,
        reinterpret_cast<const float*>(static_cast<const unsigned char*>(entity)+0x20),28,radius,.5f,0,0,0);
}
inline u32 load_fx(const char* name){auto* manager=fn<void*(__cdecl*)()>(0x407750)();return manager?fn<u32(__fastcall*)(void*,void*,const char*,int)>(0x4079a0)(manager,nullptr,name,11):0;}
inline void emit(u32 fx,const float* start,const float* delta,u32 owner){
    if(!fx)return;auto* manager=fn<void*(__cdecl*)()>(0x407750)();if(!manager)return;
    fn<void(__fastcall*)(void*,void*,u32,const float*,const float*,u32,int,const char*,float)>(0x40b2b0)(manager,nullptr,fx,start,delta,owner,0,nullptr,-1.f);
}
inline xml1_hook_transaction::patch jump(u32 va,std::vector<unsigned char> before,void* to){
    std::vector<unsigned char> after(before.size(),0x90);after[0]=0xe9;
    const u32 delta=static_cast<u32>(reinterpret_cast<std::uintptr_t>(to))-va-5;std::memcpy(after.data()+1,&delta,4);
    return {va,std::move(before),std::move(after)};
}
inline constexpr std::array<limits_rules::guard,25> guards{{
    {0x509ba0,"83ec188b442420","facing"}, {0x529690,"8b41108a8864070000","alternate slot"},
    {0x50a250,"6aff6880556700","target-centred hazard query"},
    {0x502db0,"8a817c040000c3","camera cache"}, {0x4457e0,"64a1000000008a0dac767100","camera manager"},
    {0x4403a0,"8b44240483ec0c81c10c020000","camera sphere gate"},
    {0x475240,"81ec04010000","motion load"}, {0x470d60,"568bf18b06ff9054010000","motion start"},
    {0x4752d0,"568bf18b06ff9054010000","motion stop"}, {0x475330,"51568bf18b8690010000","motion running"},
    {0x45fe40,"f6416c017418","tile flags notification"}, {0x45ff10,"a07884710083ec08","tile world notification"},
    {0x695efc,"503c5200","danger vtable"}, {0x523c50,"8b44241c8b4c24188b542414","danger ABI"},
    {0x5044f0,"5657c60500aa780001","danger queue"},
    {0x427f60,"568b74240885f657","lift gate"}, {0x427dc0,"568bf1f6860a01000001","Might getter"},
    {0x5025b0,"837904030f94c0c3","movement completion"}, {0x421b90,"83ec0c566800008000","movement stop"},
    {0x5169de,"c786dc02000000800944","native companion jump vertical parameter"},
    {0x5241b1,"d980dc020000","AI jump parameter consumer"},
    {0x550410,"558b6c240c5657","actual protection affecter"},
    {0x503810,"568bf18b46108b8878030000","immediate facing"},
    {0x5021a0,"568bf18b867803000085c0","controller-gated throw orientation"},
    {0x5031b0,"568bf18b46108b8878030000","close pursuit orientation"}}};
inline bool supported(HMODULE game){
    if(!boss_runtime::supported(game)||!mold_effects::supported())return false;
    for(const auto& g:guards)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)){
        logger::write("wave3 services: guard failed %08X",g.va);return false;}
    return true;
}
}

#pragma once
#include <Windows.h>
#include "mold_query.hpp"
#include "limits_rules.hpp"
#include "boss_dopple_sites.hpp"
#include "boss_target_rules.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

// Shared game services for the wave-two clients. No install or writes happen here.
namespace boss_runtime {
using u32=std::uint32_t;
template<class T> T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T> void put(void* p,u32 off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T> T fn(u32 a){return reinterpret_cast<T>(static_cast<std::uintptr_t>(a));}
template<class T> T virt(void* p,u32 off){return at<T>(at<void*>(p,0),off);}
inline bool listed(std::string_view name){
    const auto list=ini::text(L"Game",L"Xml1Bosses").value_or("");
    for(std::size_t i=0;i<list.size();){auto j=list.find(',',i);if(j==std::string::npos)j=list.size();
        if(std::string_view(list).substr(i,j-i)==name)return true;i=j+1;}
    return false;
}
inline float now(){return fn<float(__cdecl*)()>(0x45fe60)();}
inline bool zone(const char* name){return std::strcmp(reinterpret_cast<const char*>(0x72a758),name)==0;}
inline void* resolve(u32 h){return h?fn<void*(__fastcall*)(const u32*,void*)>(0x4654b0)(&h,nullptr):nullptr;}
inline void* find(const char* name,u32& h){fn<u32*(__cdecl*)(u32*,const char*)>(0x4a1700)(&h,name);return resolve(h);}
inline bool living(void* a){return mold_query::is_a(a,0x70b83c)&&at<float>(a,0x27c)>0;}
inline bool identity(void* a,const char* name,u32& h){return a&&find(name,h)==a&&at<u32>(a,0x1c)==h&&mold_query::is_a(a,0x70b83c);}
inline bool blocked(void* b,void* a){return !living(a)||*reinterpret_cast<const unsigned char*>(0x70b70a)||(at<u32>(b,0x410)&&at<signed char>(b,0x2b4)>=0);}
inline float distance(void* a,void* b){float sum=0;for(u32 i=0;i<3;++i){float d=at<float>(a,0x20+4*i)-at<float>(b,0x20+4*i);sum+=d*d;}return std::sqrt(sum);}
inline bool ai(void* a){return (at<unsigned char>(a,0x3d8)&1)!=0;}
// Native fixed-capacity handle list (five handles, count +0x14). Index -1 includes all players and AI heroes.
inline std::array<u32,5> heroes(){
    std::array<u32,6> list{};
    fn<void*(__fastcall*)(void*,void*)>(0x4218f0)(list.data(),nullptr);
    auto* game=fn<void*(__cdecl*)()>(0x46dce0)();
    virt<unsigned char(__fastcall*)(void*,void*,void*,int,unsigned char)>(game,0x120)(game,nullptr,list.data(),-1,0);
    std::array<u32,5> result{};
    if(list[5]<=result.size())for(u32 i=0;i<list[5];++i)result[i]=list[i];
    return result;
}
inline std::array<boss_target_rules::hero,5> hero_view(void* actor){
    std::array<boss_target_rules::hero,5> result{};const auto ids=heroes();
    for(std::size_t i=0;i<ids.size();++i){auto* a=resolve(ids[i]);if(!living(a))continue;
        result[i]={ids[i],distance(actor,a),at<float>(a,0x28)-at<float>(actor,0x28),true,ai(a)};}
    return result;
}
inline bool available(void* b,void* a,int slot){return at<void*>(a,0x378)&&fn<unsigned char(__fastcall*)(void*,void*,int)>(0x529630)(b,nullptr,slot)!=0;}
inline void buttons(u32* out,u32 bits){out[0]|=bits;out[1]|=bits;}
inline float random(){return fn<float(__cdecl*)(unsigned char)>(0x560190)(1);}
inline void taunt(void* b,float p){fn<void(__fastcall*)(void*,void*,float)>(0x5279c0)(b,nullptr,p);}
inline void wait(void* b,float seconds){fn<void(__fastcall*)(void*,void*,float)>(0x502580)(b,nullptr,seconds);}
inline void target(void* b,void* a,u32 h){fn<void(__fastcall*)(void*,void*,u32)>(0x538290)(b,nullptr,h);fn<void(__fastcall*)(void*,void*,u32)>(0x42c7c0)(a,nullptr,h);}
inline void follow(void* b,void* a,u32 h){fn<void(__fastcall*)(void*,void*,unsigned char)>(0x502cc0)(b,nullptr,1);fn<unsigned char(__fastcall*)(void*,void*,u32)>(0x4a7da0)(a,nullptr,h);put(b,0x1c4,1.f);}
inline void stop(void* a){fn<void(__fastcall*)(void*,void*)>(0x423c30)(a,nullptr);}
inline void face(void* b,void* a,void* other,const float* home=nullptr){
    float d[3]{at<float>(other,0x20)-at<float>(a,0x20),at<float>(other,0x24)-at<float>(a,0x24),0};
    if(!fn<unsigned char(__fastcall*)(void*,void*,float*)>(0x502c50)(b,nullptr,d))return;
    float angles[3]{};fn<float*(__cdecl*)(float*,const float*)>(0x5607b0)(angles,d);
    const float pos[3]{at<float>(a,0x20),at<float>(a,0x24),at<float>(a,0x28)};
    fn<void(__fastcall*)(void*,void*,const float*,const float*)>(0x503180)(b,nullptr,home?home:pos,angles);
}
inline bool play(void* a,const char* name){
    u32 token=0;fn<u32*(__fastcall*)(u32*,void*,const char*)>(0x41b120)(&token,nullptr,name);
    auto* n=fn<void*(__fastcall*)(void*,void*,const u32*)>(0x427ca0)(a,nullptr,&token);
    if(!n)return false;fn<void(__fastcall*)(void*,void*,void*,int)>(0x42c340)(a,nullptr,n,1);return true;
}
inline void* brain_of(void* a){auto* manager=fn<void*(__cdecl*)()>(0x5252e0)();return virt<void*(__fastcall*)(void*,void*,u32)>(manager,0x258)(manager,nullptr,at<u32>(a,0x7dc));}
// Extra service signatures are tested against retail and checked before installing any wave-two client.
inline constexpr std::array<limits_rules::guard,20> guards{{
    {0x4218f0,"568bf1e858e2ffff","hero list constructor"},
    {0x46dce0,"64a1000000008a0d","game singleton"},
    {0x46d460,"6aff6800386700","all-player hero list"},
    {0x502c50,"568b7424088bce","zero-safe normalize"},
    {0x41d3e0,"568bf18b4604","team setter"},
    {0x5252e0,"8a0d9c887900","AI system"},
    {0x695f14,"60515200","generation-checked AI lookup vtable"},
    {0x525160,"568b74240885f6744c","generation-checked AI identifier lookup"},
    {0x505030,"83ec0c535657","brain state selector"},
    {0x4a7da0,"518b44240885c0","movement target"},
    {0x502cc0,"8a9178020000","movement enable"},
    {0x41b120,"56578b7c240c85ff","name token"},
    {0x427ca0,"8b4424045150e82554ffff","node lookup"},
    {0x42c340,"56578b7c240c85ff","node play"},
    {0x5061d0,"81ec30010000","native close movement"},
    {0x505f50,"518b4424085356","attacker reservation layout"},
    {0x42b240,"83ec0853568bf1","skin-preserving restore"},
    {0x4b8090,"83ec44a1f8386f00","stats skin string"},
    {0x411be0,"568b742408d906","finite movement vector"},
    {0x42d8e0,"538b5c24085657","actor movement target"}}};
inline bool supported(HMODULE game){
    if(reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base||!mold_query::supported())return false;
    for(const auto& g:boss_dopple_sites::guards)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex))return false;
    for(const auto& g:guards)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)){logger::write("boss services: guard failed %08X",g.va);return false;}
    return true;
}
}

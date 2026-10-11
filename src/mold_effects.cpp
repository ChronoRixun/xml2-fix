#include "mold_effects.hpp"
#include "mold_query.hpp"
#include "limits_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "log.hpp"
#include <array>
#include <cstdio>

namespace mold_effects {
namespace {
using u32=std::uint32_t;
template<class T>T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>T fn(u32 address){return reinterpret_cast<T>(address);}
using manager_t=void*(__cdecl*)();
using load_t=u32(__fastcall*)(void*,void*,const char*,int);
using emit_t=void(__fastcall*)(void*,void*,u32,const float*,const float*,u32,int,const char*,float);
using head_t=unsigned char(__fastcall*)(void*,void*,int,float*,unsigned char,unsigned char);
using lookup_t=u32*(__cdecl*)(u32*,const char*);
using resolve_t=void*(__fastcall*)(const u32*,void*);
using position_t=const float*(__fastcall*)(void*,void*,float*);
std::array<u32,2> handles{};
bool loaded=false;
}
bool supported(){
    constexpr std::array<limits_rules::guard,4> guards{{
        {0x407750,"64a1000000008a0de08e6f006aff","gameplay effects manager"},
        {0x4079a0,"8b4424088b4c24045051e821ffff","effect resource loader"},
        {0x41d620,"83ec0c568bf1f6460601578b7c24","actor attachment position"},
        {0x40b2b0,"83ec6855568be957896c240c8d74","native effect emission"}}};
    xml1_hook_transaction::memory memory;
    for(const auto& g:guards){
        std::array<unsigned char,16> bytes{};
        if(!memory.read(g.va,bytes.data(),g.hex.size()/2)||!limits_rules::matches(bytes.data(),g.hex))return false;
    }
    return true;
}
void reset(){
    // Gameplay group 11 is purged by native zone teardown. These handles are
    // borrowed from that group; purging it here would free other entities' FX.
    handles={};loaded=false;
}
void update(void* actor){
    auto* manager=fn<manager_t>(0x407750)();
    if(!manager)return;
    if(!loaded){
        handles[0]=fn<load_t>(0x4079a0)(manager,nullptr,"puzzles/warpcore_beam",11);
        handles[1]=fn<load_t>(0x4079a0)(manager,nullptr,"puzzles/warpcore_beamoff",11);
        loaded=true;
        logger::write("mold effects: zone resources %08X/%08X",handles[0],handles[1]);
    }
    float head[3]{};
    fn<head_t>(0x41d620)(actor,nullptr,4,head,0,0);
    constexpr std::array<const char*,2> prefixes{"warpcore_broken","warpcore_leak"};
    for(unsigned kind=0;kind<handles.size();++kind){
        if(!handles[kind])continue;
        for(int index=1;index<=8;++index){
            char name[32]{};std::snprintf(name,sizeof name,"%s%02d",prefixes[kind],index);
            u32 token=0;auto* entity=fn<resolve_t>(0x4654b0)(fn<lookup_t>(0x4a1700)(&token,name),nullptr);
            if(!entity)continue;
            if(!mold_query::is_a(entity,0x70b678)||!(at<u32>(entity,4)&(1u<<20)))continue;
            float scratch[3]{};
            const auto* start=at<position_t>(at<const void*>(entity,0),0x24)(entity,nullptr,scratch);
            float delta[3]{};for(unsigned axis=0;axis<3;++axis)delta[axis]=head[axis]-start[axis];
            // The displacement carries length. PC's final argument is a life
            // override: -1 preserves authored life; XBE used a different flag.
            // Source owner token 12 is unused by these non-attached effects.
            fn<emit_t>(0x40b2b0)(manager,nullptr,handles[kind],start,delta,12,0,nullptr,-1.f);
            logger::write_once(kind?"mold-tether-leak":"mold-tether-broken","mold effects: native tether %s to head %.3f/%.3f/%.3f",name,head[0],head[1],head[2]);
        }
    }
}
}

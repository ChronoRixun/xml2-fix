#include "boss_avalanche.hpp"
#include "avalanche_rules.hpp"
#include "boss_dopple_sites.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "limits_rules.hpp"
#include "mold_query.hpp"
#include "boss_runtime.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

// Game adapter of Avalanche's brain (avalanche_rules.hpp; docs/xml1-controllers.md, "Avalanche"). Default off: only
// [Game] Xml1Bosses=...avalanche... installs it, through the shared dispatcher's one transaction. Lockstep inputs only:
// the game clock, the gameplay random stream, entity state and script requests.
namespace boss_avalanche {
namespace {
using u32=std::uint32_t;
template<class T> T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T> void put(void* p,u32 off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T> T fn(u32 a){return reinterpret_cast<T>(static_cast<std::uintptr_t>(a));}
using resolve_t=void*(__fastcall*)(const u32*,void*);
using lookup_t=u32*(__cdecl*)(u32*,const char*);
constexpr u32 character_class=0x70b83c;
constexpr std::array<const char*,2> zones{"mount/mount/mount2","astral/ast3/astral3_4"};
// The PC brain's copy of XML1's +0x28b flags (+0x2b7): bit 3 is tested at the same two decision sites in both games.
constexpr std::array<limits_rules::guard,2> mode_bit_readers{{
    {0x513146,"f686b702000008","brain flags +0x2b7 bit 3 reader (XML1 0x10afa7)"},
    {0x514613,"f683b702000008","brain flags +0x2b7 bit 3 reader (XML1 0x10bf51)"}}};
bool requested=false,ready=false,enabled=false,trace=false,ended=false;
avalanche_rules::state s;
u32 owner=0;
float last_time=-1;
float now(){return fn<float(__cdecl*)()>(0x45fe60)();}
bool scope(){const auto* z=reinterpret_cast<const char*>(0x72a758);for(const auto* n:zones)if(std::strcmp(z,n)==0)return true;return false;}
void* find(const char* name,u32& h){return fn<resolve_t>(0x4654b0)(fn<lookup_t>(0x4a1700)(&h,name),nullptr);}
void* resolve(u32 h){return h?fn<resolve_t>(0x4654b0)(&h,nullptr):nullptr;}
bool available(void* brain,void* actor,int slot){
    if(!at<void*>(actor,0x378))return false;
    return fn<unsigned char(__fastcall*)(void*,void*,int)>(0x529630)(brain,nullptr,slot)!=0;
}
void buttons(u32* output,u32 bits){output[0]|=bits;output[1]|=bits;}
void taunt(void* brain,float chance){fn<void(__fastcall*)(void*,void*,float)>(0x5279c0)(brain,nullptr,chance);}
float random(){return fn<float(__cdecl*)(unsigned char)>(0x560190)(1);}
void face(void* brain,void* actor,void* other){
    if(!other)return;
    float d[3]{at<float>(other,0x20)-at<float>(actor,0x20),at<float>(other,0x24)-at<float>(actor,0x24),0.f};
    const float squared=d[0]*d[0]+d[1]*d[1];
    if(!(squared>=std::numeric_limits<float>::min()))return;
    const float length=std::sqrt(squared);d[0]/=length;d[1]/=length;
    float angles[3]{};
    fn<float*(__cdecl*)(float*,const float*)>(0x5607b0)(angles,d);
    float origin[3]{at<float>(actor,0x20),at<float>(actor,0x24),at<float>(actor,0x28)};
    fn<void(__fastcall*)(void*,void*,const float*,const float*)>(0x503180)(brain,nullptr,origin,angles);
}
}
bool prepare(HMODULE game){
    const auto list=ini::text(L"Game",L"Xml1Bosses").value_or("");
    requested=false;
    for(std::size_t i=0;i<list.size();){
        auto j=list.find(',',i);if(j==std::string::npos)j=list.size();
        if(list.compare(i,j-i,"avalanche")==0)requested=true;
        i=j+1;
    }
    ready=enabled=false;
    if(!requested)return false;
    trace=ini::flag(L"Test",L"BossTrace",false);
    if(reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base)return false;
    if(!boss_runtime::supported(game)){logger::write("avalanche brain: query/hero services differ; native Avalanche retained");return false;}
    for(const auto& g:boss_dopple_sites::guards)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)){logger::write("avalanche brain: guard failed %08X",g.va);return false;}
    for(const auto& g:mode_bit_readers)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)){logger::write("avalanche brain: guard failed %08X",g.va);return false;}
    ready=true;return true;
}
void committed(bool ok){
    enabled=ok&&ready;
    if(enabled)logger::write("avalanche brain: installed (mount2, astral3_4)");
    else if(ready)logger::write("avalanche brain: shared transaction refused; native Avalanche retained");
}
bool installed(){return enabled;}
bool in_scope(){return enabled&&scope();}
void on_sequence(const char* source,const char* sequence){
    // endbattle (setstate 0x2a) leaves the brain's state: the original then runs the generic AI.
    if(!enabled||!scope()||std::strcmp(source,"avalanche")!=0||std::strcmp(sequence,"endbattle")!=0)return;
    ended=true;logger::write("avalanche brain: endbattle, native AI from now on (owner %08X)",owner);
}
bool brain(void* brain,u32* output){
    if(!enabled)return false;
    if(!scope()){if(owner){owner=0;s={};ended=false;}return false;}
    auto* actor=brain?at<void*>(brain,0x10):nullptr;
    u32 handle=0;auto* boss=find("avalanche",handle);
    if(!actor||actor!=boss||at<u32>(actor,0x1c)!=handle||!mold_query::is_a(actor,character_class))return false;
    if(owner!=handle||now()<last_time){
        if(!mold_query::supported())return false;
        owner=handle;s={};ended=false;
    }   // the spawn's init (0x10f6f0) zeroes the brain's globals
    last_time=now();
    if(ended||at<float>(actor,0x27c)<=0)return false;
    if(*reinterpret_cast<const unsigned char*>(0x70b70a))return false;
    if(at<u32>(brain,0x410)&&at<signed char>(brain,0x2b4)>=0)return false;
    // Close targeting includes AI heroes. Mid-range facing separately remembers the last controlled hero.
    const auto chosen=boss_target_rules::avalanche(boss_runtime::hero_view(actor));
    const float nearest=chosen.distance;void* last=resolve(chosen.controlled);const u32 nearest_handle=chosen.nearest;
    if(nearest_handle)s.target=nearest_handle;
    avalanche_rules::view v;
    v.now=now();v.health=at<float>(actor,0x27c);v.health_max=at<float>(actor,0x284);v.nearest=nearest;
    v.slot_available=available(brain,actor,avalanche_rules::power_smash_slot);
    const int band=s.band;
    const auto a=avalanche_rules::update(s,v);
    if(a.band_changed){
        const auto flags=at<unsigned char>(brain,0x2b7);
        put(brain,0x2b7,static_cast<unsigned char>(a.mode_bit?flags|8u:flags&~8u));
        if(trace)logger::write("avalanche trace: band %d -> %d, mode bit %d",band,s.band,int(a.mode_bit));
    }
    if(a.buttons)buttons(output,a.buttons);
    if(a.taunt_always)taunt(brain,1.f);
    if(a.roll_smash)buttons(output,random()<.5f?avalanche_rules::smash_buttons:avalanche_rules::attack_buttons);
    if(a.taunt_half)taunt(brain,.5f);
    if(a.face)face(brain,actor,resolve(nearest_handle));
    if(a.face_last)face(brain,actor,last);
    if(trace&&a.buttons)logger::write("avalanche trace: buttons %X at %.3f, nearest %.1f",a.buttons,v.now,nearest);
    return true;
}
}

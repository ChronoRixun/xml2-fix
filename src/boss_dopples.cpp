#include "boss_dopples.hpp"
#include "dopple_rules.hpp"
#include "boss_dopple_sites.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "limits_rules.hpp"
#include "mold_query.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

// Game adapter of the colosseum dopples (dopple_rules.hpp; docs/xml1-controllers.md, "Doppelgangers"). Default off:
// only [Game] Xml1Bosses=...dopples... installs it, through the shared dispatcher's one transaction. Online play is
// lockstep: every peer runs it from the same native AI update with the game clock, the gameplay random stream (the
// taunt roll) and entity state only; the active hero follows the shared player-slot rule (mold_query::active_hero).
namespace boss_dopples {
namespace {
using u32=std::uint32_t;
template<class T> T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T> void put(void* p,u32 off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T> T fn(u32 a){return reinterpret_cast<T>(static_cast<std::uintptr_t>(a));}
using resolve_t=void*(__fastcall*)(const u32*,void*);
using lookup_t=u32*(__cdecl*)(u32*,const char*);
using thiscall0_t=void(__fastcall*)(void*,void*);
constexpr u32 character_class=0x70b83c;
constexpr const char* zone="astral/ast3/colosseum";
// Slot order of the original (0x4feda0..ac): Wolverine, Colossus, Jean, Cyclops; partners 1, 0, 3, 2.
constexpr std::array<const char*,4> names{"evilwolverine","evilcolossus","eviljean","evilcyc"};
constexpr std::array<int,4> partner{1,0,3,2};
bool requested=false,ready=false,enabled=false,write_bossfull=true,trace=false;
dopple_rules::shared shared;
std::array<u32,4> bound{};        // handles seen in this visit (a new one restarts the overlay, as each dop_* init did)
std::array<u32,4> flagged{};      // handles whose brain got the bossfull flag
float now(){return fn<float(__cdecl*)()>(0x45fe60)();}
bool scope(){return std::strcmp(reinterpret_cast<const char*>(0x72a758),zone)==0;}
void* find(const char* name,u32& h){return fn<resolve_t>(0x4654b0)(fn<lookup_t>(0x4a1700)(&h,name),nullptr);}
void* resolve(u32 h){return h?fn<resolve_t>(0x4654b0)(&h,nullptr):nullptr;}
float distance(const void* a,const void* b){float s=0;for(u32 i=0;i<3;++i){const auto d=at<float>(a,0x20+4*i)-at<float>(b,0x20+4*i);s+=d*d;}return std::sqrt(s);}
bool living(void* e){return e&&at<float>(e,0x27c)>0;}
bool valid_partner(void* e){return mold_query::is_a(e,character_class)&&living(e);}
bool available(void* brain,void* actor,int slot,bool alternate){
    if(!at<void*>(actor,0x378))return false;   // no controller: the original would read stack garbage
    return fn<unsigned char(__fastcall*)(void*,void*,int)>(alternate?0x529690:0x529630)(brain,nullptr,slot)!=0;
}
void buttons(u32* output,u32 bits){output[0]|=bits;output[1]|=bits;}
void wait(void* brain,float seconds){fn<void(__fastcall*)(void*,void*,float)>(0x502580)(brain,nullptr,seconds);}
// Movement: XML1 0x325a0 / 0x34770 / vt+0x14c = PC 0x421b90 / 0x423c30 / vt+0x154; navigation 0x345a0 = 0x423af0.
void stop(void* actor){
    fn<thiscall0_t>(0x421b90)(actor,nullptr);
    fn<thiscall0_t>(0x423c30)(actor,nullptr);
    const auto physical=at<thiscall0_t>(at<const void*>(actor,0),0x154);
    if(physical==fn<thiscall0_t>(0x421da0))physical(actor,nullptr);
}
void cancel_navigation(void* actor){
    fn<thiscall0_t>(0x423c30)(actor,nullptr);
    if(at<void*>(actor,0x3e8))fn<thiscall0_t>(0x423af0)(actor,nullptr);
}
void move_to(void* actor,void* other){
    using move_t=void(__fastcall*)(void*,void*,const float*,u32,float);
    const auto movement=at<move_t>(at<const void*>(actor,0),0x160);
    if(movement!=fn<move_t>(0x424a90)){logger::write_once("dopple-move","dopple overlay: actor movement differs; move skipped");return;}
    movement(actor,nullptr,reinterpret_cast<const float*>(static_cast<const unsigned char*>(other)+0x20),0,dopple_rules::move_speed);
}
// Face the partner's flattened direction (XML1 0xfce20 with its zero guard, 0x120860, 0xfc630 / 0xfc5d0).
void face(void* brain,void* actor,void* other,bool immediate){
    float d[3]{at<float>(other,0x20)-at<float>(actor,0x20),at<float>(other,0x24)-at<float>(actor,0x24),0.f};
    const float squared=d[0]*d[0]+d[1]*d[1];
    if(!(squared>=std::numeric_limits<float>::min()))return;
    const float length=std::sqrt(squared);d[0]/=length;d[1]/=length;
    float angles[3]{};
    fn<float*(__cdecl*)(float*,const float*)>(0x5607b0)(angles,d);
    float origin[3]{at<float>(actor,0x20),at<float>(actor,0x24),at<float>(actor,0x28)};
    fn<void(__fastcall*)(void*,void*,const float*,const float*)>(immediate?0x503810:0x503180)(brain,nullptr,origin,angles);
}
// Jean: the nearest eligible aishootme hazard around the brain's target (the hero), activated by Jean.
void activate_hazard(void* brain,void* actor){
    u32 out=0;
    fn<void(__fastcall*)(void*,void*,u32*,float,int)>(0x50a250)(brain,nullptr,&out,dopple_rules::hazard_radius,1);
    auto* hazard=out?fn<void*(__cdecl*)(u32)>(0x4297b0)(out):nullptr;
    if(!hazard)return;
    using activate_t=int(__fastcall*)(void*,void*,void*,unsigned char);
    const auto activate=at<activate_t>(at<const void*>(hazard,0),0xd4);
    if(activate!=fn<activate_t>(0x497b10)&&activate!=fn<activate_t>(0x41b5b0)){logger::write_once("dopple-hazard","dopple overlay: hazard activation differs; skipped");return;}
    activate(hazard,nullptr,actor,0);
    if(trace)logger::write("dopple trace: jean hazard %08X at %.3f",out,now());
}
void restart(){shared=dopple_rules::shared{};}
}
bool prepare(HMODULE game){
    const auto list=ini::text(L"Game",L"Xml1Bosses").value_or("");
    requested=false;
    for(std::size_t i=0;i<list.size();){
        auto j=list.find(',',i);if(j==std::string::npos)j=list.size();
        if(list.compare(i,j-i,"dopples")==0)requested=true;
        i=j+1;
    }
    ready=enabled=false;
    if(!requested)return false;
    write_bossfull=ini::flag(L"Game",L"Xml1BossFull",true);
    trace=ini::flag(L"Test",L"BossTrace",false);
    if(reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base)return false;
    if(!mold_query::supported()){logger::write("dopple overlay: query/hero services differ; native dopples retained");return false;}
    for(const auto& g:boss_dopple_sites::guards)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)){logger::write("dopple overlay: guard failed %08X",g.va);return false;}
    ready=true;return true;
}
void committed(bool ok){
    enabled=ok&&ready;
    if(enabled)logger::write("dopple overlay: installed (colosseum, bossfull %d)",int(write_bossfull));
    else if(ready)logger::write("dopple overlay: shared transaction refused; native dopples retained");
}
bool installed(){return enabled;}
bool bossfull(){return write_bossfull;}
bool brain(void* brain,u32* output){
    if(!enabled)return false;
    if(!scope()){if(bound!=std::array<u32,4>{}){bound={};flagged={};restart();}return false;}
    auto* actor=brain?at<void*>(brain,0x10):nullptr;
    if(!actor||!mold_query::is_a(actor,character_class))return false;
    const u32 self=at<u32>(actor,0x1c);
    std::array<u32,4> handles{};std::array<void*,4> actors{};int me=-1;
    for(int i=0;i<4;++i){actors[i]=find(names[i],handles[i]);if(actors[i]==actor&&handles[i]==self)me=i;}
    if(me<0)return false;
    if(bound[me]!=self){bound[me]=self;restart();}
    if(write_bossfull&&flagged[me]!=self){
        // XML1 0xf4355: the dop_* setstate writes the brain's bossfull flags (bit 0 set, bit 2 clear); PC brain +0x2b8.
        flagged[me]=self;put(brain,0x2b8,static_cast<unsigned char>((at<unsigned char>(brain,0x2b8)&~4u)|1u));
    }
    // Native script commands keep their dispatcher and precedence (as Master Mold).
    if(at<u32>(brain,0x410)&&at<signed char>(brain,0x2b4)>=0)return false;
    const float t=now();
    auto* wolverine=resolve(handles[0]);auto* colossus=resolve(handles[1]);
    if(dopple_rules::tick(shared,t,living(wolverine)&&living(colossus),wolverine&&colossus?distance(wolverine,colossus):0.f)){
        cancel_navigation(wolverine);
        if(trace)logger::write("dopple trace: fastball armed at %.3f",t);
    }
    const auto who=static_cast<dopple_rules::who>(me);
    // The taunt roll comes first in every handler (Jean's hero lookup draws nothing): random < 0.05 -> boss taunt.
    fn<void(__fastcall*)(void*,void*,float)>(0x5279c0)(brain,nullptr,dopple_rules::taunt_chance);
    auto* other=actors[partner[me]];
    u32 hero_handle=0;auto* hero=mold_query::active_hero(hero_handle);
    dopple_rules::view v;
    v.now=t;v.partner_valid=valid_partner(other);v.partner_distance=v.partner_valid?distance(actor,other):0.f;
    v.hero=mold_query::is_a(hero,character_class);
    const auto test=dopple_rules::slot_for(shared,who);
    v.slot_available=test.slot>=0&&available(brain,actor,test.slot,test.alternate);
    const int before=me==0?shared.wolverine:me==1?shared.colossus:me==2?shared.jean:shared.cyclops;
    const auto a=dopple_rules::update(shared,who,v);
    if(a.move_to_partner)move_to(actor,other);
    if(a.stop)stop(actor);
    if(a.face_partner)face(brain,actor,other,a.face_immediate);
    if(a.target_hero)fn<void(__fastcall*)(void*,void*,u32)>(0x42c7c0)(actor,nullptr,hero_handle);
    if(a.brain_target_hero)fn<void(__fastcall*)(void*,void*,u32)>(0x538290)(brain,nullptr,hero_handle);
    if(a.activate_hazard)activate_hazard(brain,actor);
    if(a.buttons)buttons(output,a.buttons);
    if(a.wait>0)wait(brain,a.wait);
    const int after=me==0?shared.wolverine:me==1?shared.colossus:me==2?shared.jean:shared.cyclops;
    if(trace&&before!=after)logger::write("dopple trace: %s state %d -> %d at %.3f buttons %X",names[me],before,after,t,a.buttons);
    return a.handled;
}
}

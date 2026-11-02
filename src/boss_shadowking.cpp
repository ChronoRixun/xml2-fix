#include "boss_shadowking.hpp"
#include "boss_runtime.hpp"
#include "shadowking_rules.hpp"
namespace boss_shadowking {
namespace {
using namespace boss_runtime;
struct owner_state {u32 owner=0;float last_time=-1;bool initialized=false;shadowking_rules::state rules;};
std::array<owner_state,2> bosses;
constexpr std::array<const char*,2> names{"shadowking","shadowking2"};
bool ready=false,enabled=false,trace=false;
bool scope(){return zone("astral/savepx/final_astral");}
bool bind(owner_state& b,u32 h,int form,void* brain){
    if(b.owner!=h||now()<b.last_time){if(!mold_query::supported())return false;b={};b.owner=h;b.rules=shadowking_rules::initial(now(),form);}
    if(!b.initialized&&brain){
        // XML1 initializer 0x10f7c0: six-second initial deadlines, bossfull bits 0+2 and preferred distance 350.
        put(brain,0x2b8,static_cast<unsigned char>(at<unsigned char>(brain,0x2b8)|5u));put(brain,0x280,350.f);b.initialized=true;
    }
    b.last_time=now();return true;
}
void close_move(void* brain,void* actor,u32* output){
    float direction[3]{},extra=0;
    put(actor,0x764,static_cast<unsigned char>(at<unsigned char>(actor,0x764)&~4u));
    const bool moving=fn<unsigned char(__fastcall*)(void*,void*,float*,u32*,float*)>(0x5061d0)(brain,nullptr,direction,output,&extra)!=0;
    if(!moving||(at<unsigned char>(brain,0x2a4)&1))return;
    if(fn<unsigned char(__cdecl*)(const float*)>(0x411be0)(direction)){
        std::memcpy(output+5,direction,sizeof direction);std::memcpy(output+2,static_cast<unsigned char*>(brain)+0x1c4,4);
    }
    output[0]|=0x2000;
    fn<void(__fastcall*)(void*,void*,u32)>(0x42c7c0)(actor,nullptr,at<u32>(brain,0x18));
    fn<void(__fastcall*)(void*,void*,u32)>(0x42d8e0)(actor,nullptr,0);
}
}
bool prepare(HMODULE game){ready=enabled=false;if(!boss_runtime::listed("shadowking"))return false;trace=ini::flag(L"Test",L"BossTrace",false);return ready=boss_runtime::supported(game);}
void committed(bool ok){enabled=ready&&ok;if(enabled)logger::write("shadowking brain: installed (attacks, authored shield, movement; floor relocation retained)");}
bool installed(){return enabled;}
bool in_scope(){return enabled&&scope();}
void on_default_target(const char* name){if(!in_scope())return;for(int i=0;i<2;++i)if(std::strcmp(name,names[i])==0)bosses[i]={};}
void on_sequence(const char* name,const char* sequence){
    if(!in_scope())return;
    for(int i=0;i<2;++i){
        if(std::strcmp(name,names[i])!=0)continue;
        u32 h=0;auto* a=find(name,h);if(!living(a))return;auto* brain=brain_of(a);
        if(!bind(bosses[i],h,i,brain))return;
        auto& s=bosses[i].rules;
        if(std::strcmp(sequence,"sk")==0){s=shadowking_rules::initial(now(),0);bosses[i].initialized=false;bind(bosses[i],h,0,brain);}
        else if(std::strcmp(sequence,"sk2")==0){s=shadowking_rules::initial(now(),1);bosses[i].initialized=false;bind(bosses[i],h,1,brain);}
        else if(std::strcmp(sequence,"sk3")==0)s.boost=true;
        else if(std::strcmp(sequence,"sk4")==0){s.xtreme=true;if(auto* b=brain_of(a))put(b,0x280,350.f);}
        else return;
        if(trace)logger::write("shadowking trace: request %s owner %08X mode %d",sequence,h,s.mode);
    }
}
bool brain(void* b,u32* output){
    if(!enabled)return false;
    if(!scope()){bosses={};return false;}
    auto* actor=b?at<void*>(b,0x10):nullptr;
    for(int i=0;i<2;++i){u32 h=0;if(!identity(actor,names[i],h))continue;
        if(blocked(b,actor)||!bind(bosses[i],h,i,b))return false;
        u32 hero_id=0;auto* hero=mold_query::active_hero(hero_id);if(!living(hero))return true;
        fn<void(__fastcall*)(void*,void*,u32)>(0x538290)(b,nullptr,hero_id);auto& s=bosses[i].rules;
        const auto a=shadowking_rules::update(s,now(),distance(actor,hero),[&](int slot){return available(b,actor,slot);});
        if(a.buttons){buttons(output,a.buttons);if(a.taunt){taunt(b,.5f);fn<void(__fastcall*)(void*,void*,u32)>(0x42c7c0)(actor,nullptr,hero_id);}if(a.wait)wait(b,a.wait);if(a.preferred90)put(b,0x280,90.f);
            if(trace)logger::write("shadowking trace: buttons %X at %.3f mode %d wait %.1f",a.buttons,now(),s.mode,a.wait);}
        if(a.follow){
            if(s.mode==0)fn<unsigned char(__fastcall*)(void*,void*,u32)>(0x4a7da0)(actor,nullptr,hero_id);
            else {fn<void(__fastcall*)(void*,void*,u32)>(0x42c7c0)(actor,nullptr,0);follow(b,actor,hero_id);}
        }
        if(a.close){
            if(s.mode==0){stop(actor);close_move(b,actor,output);}
            else {fn<void(__fastcall*)(void*,void*,u32)>(0x42c7c0)(actor,nullptr,hero_id);output[0]|=0x2000;output[1]|=0x10;} // original second-form combat output uses different words
        }
        return true;
    }
    return false;
}
}

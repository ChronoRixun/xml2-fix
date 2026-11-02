#include "boss_pyro.hpp"
#include "boss_runtime.hpp"
#include "pyro_rules.hpp"
namespace boss_pyro {
namespace {
using namespace boss_runtime;
bool ready=false,enabled=false,trace=false;u32 owner=0;float last_time=-1,home[3]{};
pyro_rules::state s;
bool scope(){return zone("astral/ast3/astral3_3");}
bool bind(void* actor,u32 h){
    if(owner!=h||now()<last_time){
        if(!mold_query::supported())return false;
        owner=h;s={};for(u32 i=0;i<3;++i)home[i]=at<float>(actor,0x20+4*i);
    }
    last_time=now();return true;
}
void release(void* brain){
    for(int i=4;i>=0;--i){
        const u32 off=0x18c+8*i;auto* actor=resolve(at<u32>(brain,off));
        if(!mold_query::is_a(actor,0x70b83c))continue;
        auto* other=brain_of(actor);if(!other)continue;
        put(other,0x18,0u);put(brain,off,0u);
        fn<unsigned char(__fastcall*)(void*,void*,int)>(0x505030)(other,nullptr,0);
        fn<void(__fastcall*)(void*,void*)>(0x423af0)(actor,nullptr);stop(actor);
        // PC reset clears the two destination vectors and movement-target handle.
        put(other,0x1d4,0u); // XML1 brain +0x1c0, reservation release clears its movement latch
        if(trace)logger::write("pyro trace: released reservation %d",i);
    }
}
}
bool prepare(HMODULE game){ready=enabled=false;if(!boss_runtime::listed("pyro"))return false;trace=ini::flag(L"Test",L"BossTrace",false);return ready=boss_runtime::supported(game);}
void committed(bool ok){enabled=ready&&ok;if(enabled)logger::write("pyro brain: installed (astral3_3)");}
bool installed(){return enabled;}
bool in_scope(){return enabled&&scope();}
void on_default_target(const char* name){if(in_scope()&&std::strcmp(name,"pyro")==0){owner=0;s={};}}
void on_sequence(const char* name,const char* sequence){
    if(!in_scope()||std::strcmp(name,"pyro")!=0)return;
    u32 h=0;auto* a=find(name,h);if(!living(a)||!bind(a,h))return;
    if(std::strcmp(sequence,"fireup")==0)s.fire_up=true;
    if(std::strcmp(sequence,"firedown")==0)s.fire_up=false;
}
bool brain(void* b,u32* output){
    if(!enabled)return false;
    if(!scope()){owner=0;s={};last_time=-1;return false;}
    auto* actor=b?at<void*>(b,0x10):nullptr;u32 h=0;
    if(!identity(actor,"pyro",h)||!bind(actor,h)||blocked(b,actor))return false;
    const auto chosen=boss_target_rules::pyro(hero_view(actor));
    auto* hero=resolve(chosen.handle);const float nearest=chosen.distance;
    if(!hero)return true; // No target: no team or home writes, as the original.
    home[0]=at<float>(actor,0x20);face(b,actor,hero,home);
    fn<void(__fastcall*)(void*,void*,int)>(0x41d3e0)(actor,nullptr,s.fire_up?pyro_rules::none_team:pyro_rules::enemy_team);
    const bool due=now()>s.deadline;
    const bool slot5=due&&!s.fire_up&&nearest<200&&available(b,actor,5);
    const bool slot8=due&&nearest<300&&(s.fire_up||nearest>=200)&&available(b,actor,8);
    const auto a=pyro_rules::update(s,now(),nearest,slot5,slot8);
    if(a.buttons){buttons(output,a.buttons);taunt(b,a.taunt);if(trace)logger::write("pyro trace: buttons %X at %.3f mode %d deadline %.3f",a.buttons,now(),int(s.fire_up),s.deadline);}
    if(s.fire_up)release(b);
    return true;
}
}

#include "boss_magneto.hpp"
#include "boss_wave3_runtime.hpp"
#include "magneto_rules.hpp"
namespace boss_magneto {
namespace {
using namespace boss_wave3_runtime;
bool ready=false,enabled=false,trace=false;u32 owner=0,shield_hit=0;void* owner_brain=nullptr;float last=-1;
magneto_rules::state s;
bool scope(){return zone("astroid_m/visit1/asteroid2_1");}
void reset(){owner=0;owner_brain=nullptr;last=-1;s={};shield_hit=0;}
void bind(void* b,u32 h){
    if(boss_owner_rules::reset_required(owner,reinterpret_cast<std::uintptr_t>(owner_brain),1,last,h,reinterpret_cast<std::uintptr_t>(b),1,now()))reset();
    owner=h;if(b)owner_brain=b;last=now();
}
struct game {
    void* b;void* a;void* hero;u32* out;
    bool play(const char* n){const bool ok=boss_runtime::play(a,n);if(ok&&trace)logger::write("magneto node %s owner %08X at %.3f",n,owner,now());return ok;}
    bool available(int n){return boss_runtime::available(b,a,n);}
    void buttons(u32 n){boss_runtime::buttons(out,n);}
    void taunt(float p){boss_runtime::taunt(b,p);}
    void wait(float t){boss_runtime::wait(b,t);}
    bool visible(){return at<unsigned char>(b,0x47c)!=0;}
    bool face(float t){return fn<int(__fastcall*)(void*,void*,void*,void*,unsigned char,float)>(0x509ba0)(b,nullptr,a,hero,0,t)==0;}
};
// Observe the PC hurt-powerup entry without replacing damage/protection. XML1's missing
// magnetohurt callback emitted an owner FX at most every quarter second.
u32 hurt_resume=0x54f906;
void __cdecl hurt(void* actor,void* hit){
    u32 h=0;if(!enabled||!scope()||!identity(actor,"magneto",h)||h!=owner)return;
    if(now()<last){reset();return;}
    // Query the actual authored affecter, including animation onset and interruption.
    // Request flags never determine protection or prolong a removed shield.
    float scale=1;
    const bool found=fn<unsigned char(__fastcall*)(void*,void*,int,float*,void*,int)>(0x550410)(actor,nullptr,39,&scale,hit,1)!=0;
    s.shield_fx=found&&scale==0;s.shield_at=0;
    if(!magneto_rules::hurt(s,now()))return;
    if(trace)logger::write("magneto hurt FX owner %08X at %.3f",owner,now());
    if(!shield_hit)shield_hit=load_fx("powers/magneto_shield_hit");
    const auto* start=reinterpret_cast<const float*>(static_cast<const unsigned char*>(actor)+0x20);
    float direction[3]{0,0,1};
    if(hit)for(u32 i=0;i<3;++i)direction[i]=-at<float>(hit,0x2c+4*i);
    emit(shield_hit,start,direction,owner);
}
__declspec(naked) void hurt_stub(){
    __asm {
        mov eax,[esp+4]
        pushfd
        pushad
        push eax
        push ecx
        call hurt
        add esp,8
        popad
        popfd
        sub esp,054h
        push ebp
        mov ebp,ecx
        jmp dword ptr [hurt_resume]
    }
}
void tiles(void* hero){
    for(auto& tile:s.tiles){auto* entity=resolve(tile.handle);
        if(!entity){tile={};continue;}
        if(!mold_query::is_a(entity,0x753f64)){tile={};continue;}
        const bool running=fn<unsigned char(__fastcall*)(void*,void*)>(0x475330)(entity,nullptr)!=0;
        const float current=distance(entity,hero);
        if(magneto_rules::release_tile(running,current,tile.distance)){
            if(trace)logger::write("magneto tile release owner %08X tile %08X running %d distance %.3f initial %.3f",owner,tile.handle,int(running),current,tile.distance);
            fn<void(__fastcall*)(void*,void*)>(0x4752d0)(entity,nullptr);activate(entity,hero);tile={};}
    }
}
bool environment(game& g){
    if(!s.environment||!(now()>s.environment_at)||!mold_query::pools_ready())return false;
    u32 h=0;fn<u32*(__fastcall*)(void*,void*,u32*,float,unsigned char)>(0x50a250)(g.b,nullptr,&h,400,1);
    auto* entity=resolve(h);if(!mold_query::is_a(entity,0x753f64))return false;
    const auto* name=fn<const char*(__fastcall*)(void*,void*)>(0x41fbb0)(entity,nullptr);
    const bool general=name&&std::strstr(name,"general");
    std::array<bool,5> valid{};for(unsigned i=0;i<valid.size();++i)valid[i]=resolve(s.tiles[i].handle)!=nullptr;
    const auto plan=magneto_rules::plan_environment(s,now(),h,general,g.visible()&&camera(entity),valid);
    if(trace)logger::write("magneto environment owner %08X entity %08X action %d slot %d next %.3f",owner,h,int(plan.kind),plan.slot,plan.deadline);
    if(plan.kind==magneto_rules::environmental::general){
        s.environment_at=plan.deadline;activate(entity,g.a);danger(entity,30);wobble(entity);g.taunt(.5f);g.wait(plan.wait);s.wait_until=now()+plan.wait;return true;
    }
    if(!s.floor||general)return false;
    // XML1 changes the brain target here, after the query, rather than before it.
    put(g.b,0x18,at<u32>(g.hero,0x1c));
    if(plan.kind==magneto_rules::environmental::tracked||plan.kind==magneto_rules::environmental::full){s.environment_at=plan.deadline;return false;}
    if(plan.kind!=magneto_rules::environmental::floor)return false;
    g.buttons(0xc0);put(entity,4,at<u32>(entity,4)|2u);
    fn<void(__fastcall*)(void*,void*)>(0x45fe40)(entity,nullptr);fn<void(__fastcall*)(void*,void*)>(0x45ff10)(entity,nullptr);
    wobble(entity);s.tiles[plan.slot]={h,distance(entity,g.hero)};
    const float radius=virt<float(__fastcall*)(void*,void*)>(entity,0xe0)(entity,nullptr);
    danger(entity,radius*2);s.environment_at=plan.deadline;s.wait_until=now()+plan.wait;g.wait(plan.wait);return true;
}
}
bool prepare(HMODULE image,std::vector<xml1_hook_transaction::patch>& patches){
    ready=enabled=false;reset();if(!listed("magneto")||!boss_wave3_runtime::supported(image))return false;
    if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(0x54f900),"83ec54558be9"))return false;
    patches.push_back(jump(0x54f900,{0x83,0xec,0x54,0x55,0x8b,0xe9},reinterpret_cast<void*>(&hurt_stub)));
    trace=ini::flag(L"Test",L"BossTrace",false);return ready=true;
}
void committed(bool ok){enabled=ready&&ok;if(enabled)logger::write("magneto brain: installed (asteroid2_1; authored shield lifetime)");}
bool installed(){return enabled;}
bool in_scope(){return enabled&&scope();}
void on_default_target(const char* name){if(in_scope()&&std::strcmp(name,"magneto")==0)reset();}
void on_sequence(const char* name,const char* phase){
    if(!in_scope()||std::strcmp(name,"magneto"))return;u32 h=0;auto* a=find(name,h);if(!living(a))return;
    bind(nullptr,h);magneto_rules::request(s,phase);
    if(trace)logger::write("magneto phase %s owner %08X flags %d%d%d%d%d%d%d intervals %.3f/%.3f deadlines %.3f/%.3f",phase,owner,
        int(s.shield),int(s.off),int(s.radial),int(s.environment),int(s.floor),int(s.smash),int(s.attack),s.smash_interval,s.attack_interval,s.smash_at,s.attack_at);
}
bool brain(void* b,u32* output){
    if(!enabled)return false;if(!scope()){reset();return false;}
    // Death gates can skip this actor's dispatch entirely. Observe the bound
    // owner on other AI updates too, so same-handle resurrection starts fresh.
    if(owner&&!living(resolve(owner)))reset();
    auto* a=b?at<void*>(b,0x10):nullptr;u32 h=0;if(!identity(a,"magneto",h))return false;
    if(!living(a)){reset();return false;}bind(b,h);if(blocked(b,a))return false;
    if(now()<s.wait_until)return true;
    game g{b,a,nullptr,output};if(magneto_rules::pending(s,now(),g))return true;
    const auto ids=heroes();const auto count=unsigned(std::count_if(ids.begin(),ids.end(),[](u32 id){return id!=0;}));
    if(!count)return true;
    auto* indexed=resolve(ids[s.hero_index%count]);if(!living(indexed))s.hero_index=(s.hero_index+1)%count;
    auto* hero=resolve(s.target);
    if(!living(hero)||ai(hero)||now()>=s.target_until){
        for(unsigned i=0;i<count;++i){auto* candidate=resolve(ids[(s.cache_index+i)%count]);
            if(living(candidate)&&!ai(candidate)){s.cache_index=(s.cache_index+i)%count;s.target=at<u32>(candidate,0x1c);s.target_until=now()+8;hero=candidate;break;}}
    }
    if(!living(hero))return true;g.hero=hero;
    void* close=nullptr;float nearest=120;for(auto id:ids){auto* candidate=resolve(id);if(!living(candidate))continue;
        const float d=distance(a,candidate);if(d<nearest){nearest=d;close=candidate;}}
    tiles(hero);if(environment(g))return true;
    if(magneto_rules::powers(s,now(),g)){
        if(trace)logger::write("magneto power owner %08X at %.3f deadlines %.3f/%.3f",owner,now(),s.smash_at,s.attack_at);
        return true;
    }
    if(close&&distance(a,hero)<150&&g.visible()){
        if(fn<int(__fastcall*)(void*,void*,void*,void*,unsigned char,float)>(0x509ba0)(b,nullptr,a,close,0,.8f)){
            float d[3]{at<float>(close,0x20)-at<float>(a,0x20),at<float>(close,0x24)-at<float>(a,0x24),0};
            if(fn<unsigned char(__fastcall*)(void*,void*,float*)>(0x502c50)(b,nullptr,d)){
                float angles[3]{};fn<float*(__cdecl*)(float*,const float*)>(0x5607b0)(angles,d);
                fn<void(__fastcall*)(void*,void*,const float*,const float*)>(0x503810)(b,nullptr,
                    reinterpret_cast<const float*>(static_cast<unsigned char*>(a)+0x20),angles);
            }
        }else if(!target_blocked(close))buttons(output,random()>.5f?0x10:0x20);
        return true;
    }
    target(b,a,at<u32>(hero,0x1c));follow(b,a,at<u32>(hero,0x1c));
    if(distance(a,hero)<150){
        if(living(indexed)){
            float d[3]{at<float>(hero,0x20)-at<float>(a,0x20),at<float>(hero,0x24)-at<float>(a,0x24),0};
            if(fn<unsigned char(__fastcall*)(void*,void*,float*)>(0x502c50)(b,nullptr,d))
                fn<void(__fastcall*)(void*,void*,const float*)>(0x5031b0)(b,nullptr,d);
        }
        clear_motion(a);virt<void(__fastcall*)(void*,void*)>(a,0x154)(a,nullptr);
    }else move_to(a,hero);
    return true;
}
}

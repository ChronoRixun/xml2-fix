#include "boss_blob.hpp"
#include "boss_wave3_runtime.hpp"
#include "blob_rules.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "mold_query_rules.hpp"
#include <cstdio>
namespace boss_blob {
namespace {
using namespace boss_wave3_runtime;
bool ready=false,enabled=false,trace=false;u32 owner=0;void* owner_brain=nullptr;float last=-1;unsigned bound_zone=0;
blob_rules::state s;std::array<blob_rules::jump,5> jumps{};std::array<u32,2> effects{};
// Kept separately from controller lifetime: objects can be parsed before the boss binds.
// Full handles include generations; reparsing without the attribute erases the old entry.
blob_rules::weight_store<entity_capacity_layout::max_world> weights;
bool scope(){return zone("nyc/alison/nyc1_1_3")||zone("mansion/dr_mag2/mag_nyc4")||zone("astral/ast3/astral3_5");}
unsigned zone_id(){return zone("nyc/alison/nyc1_1_3")?1:zone("mansion/dr_mag2/mag_nyc4")?2:3;}
void reset(){owner=0;owner_brain=nullptr;last=-1;bound_zone=0;s={};jumps={};effects={};}
void bind(void* b,u32 h){
    if(boss_owner_rules::reset_required(owner,reinterpret_cast<std::uintptr_t>(owner_brain),bound_zone,last,h,reinterpret_cast<std::uintptr_t>(b),zone_id(),now()))reset();
    owner=h;owner_brain=b;bound_zone=zone_id();last=now();
}
std::optional<int> original_weight(void* prop){
    const u32 id=at<u32>(prop,0x1c);return weights.get(id&entity_capacity::installed().tokens.mask(),id);
}
u32 parsed_resume=0x4989af;
void __cdecl parsed(void* object,void* reader){
    short h=-1;virt<void(__fastcall*)(void*,void*,const char*,short*)>(reader,0x18)(reader,nullptr,"xml1heaviness",&h);
    const u32 id=at<u32>(object,0x1c);weights.set(id&entity_capacity::installed().tokens.mask(),id,h);
}
// Distinct from LiftRule's 0x498991 clamp hook: both can observe the same reader.
__declspec(naked) void parsed_stub(){
    __asm {
        pushfd
        pushad
        push edi
        push esi
        call parsed
        add esp,8
        popad
        popfd
        mov edx,[edi]
        push 068902ch
        jmp dword ptr [parsed_resume]
    }
}
bool liftable(void* actor,void* object){
    // Preserve the native carried/debug overrides and any installed LiftRule gate.
    const bool native=fn<unsigned char(__fastcall*)(void*,void*,void*)>(0x427f60)(actor,nullptr,object)!=0;
    const auto h=original_weight(object);const int byte=at<unsigned char>(object,0x31d);
    if(!h||blob_rules::engine_heaviness(*h)!=byte)return native;
    if(at<unsigned char>(object,0x30d)&4)return false;
    const int might=fn<unsigned char(__fastcall*)(void*,void*)>(0x427dc0)(static_cast<unsigned char*>(actor)+0x470,nullptr);
    return native||(*h<5&&might+1>=*h);
}
struct metric {u32 vtable=0x683fe4;float position[3]{};u32 axes=3;};
struct query_guard {
    void* query=nullptr;void* results=nullptr;
    ~query_guard(){if(results)fn<void(__cdecl*)(void*)>(0x4ab7d0)(results);if(query)fn<void(__fastcall*)(void**,void*)>(0x4ab720)(&query,nullptr);}
};
u32 query_prop(void* actor){
    if(!mold_query::pools_ready())return 0;query_guard q;u32 exclude=at<u32>(actor,0x1c);
    fn<void(__fastcall*)(void**,void*,const void*,const u32*)>(0x4ab6d0)(&q.query,nullptr,reinterpret_cast<const void*>(0x753f64),&exclude);
    if(!q.query)return 0;
    // XML1 filters 1,4,28,29,27 -> PC 1,4,30,31,29, in that order.
    for(int flag:{1,4,30,31,29})virt<void(__fastcall*)(void*,void*,int,int)>(q.query,0x40)(q.query,nullptr,1,flag);
    metric order;std::memcpy(order.position,static_cast<unsigned char*>(actor)+0x20,12);
    virt<void(__fastcall*)(void*,void*,const float*,float,u32,unsigned char)>(q.query,0x68)(q.query,nullptr,order.position,blob_rules::prop_radius,0,0);
    q.results=fn<void*(__cdecl*)(void*,const void*)>(0x4ab770)(q.query,&order);if(!q.results)return 0;
    const int count=virt<int(__fastcall*)(void*,void*)>(q.results,0)(q.results,nullptr);
    if(!mold_query_rules::result_count_ok(count,entity_capacity::installed().world))return 0;
    int best=-1;u32 chosen=0;
    for(int i=0;i<count;++i){auto* prop=virt<void*(__fastcall*)(void*,void*,int)>(q.results,4)(q.results,nullptr,i);if(!prop)continue;
        blob_rules::prop p{at<u32>(prop,0x1c),at<u32>(prop,4),at<float>(prop,0x28)-at<float>(actor,0x28),
            blob_rules::heaviness(original_weight(prop),at<unsigned char>(prop,0x31d)),false};
        if(!blob_rules::prop_geometry(p))continue;
        p.liftable=liftable(actor,prop);
        if(blob_rules::eligible(p)&&p.weight>best){chosen=p.handle;best=p.weight;}
    }
    if(trace)logger::write("blob prop query owner %08X chosen %08X weight %d count %d",owner,chosen,best,count);
    return chosen;
}
void tethers(void* actor){
    if(!effects[0])effects[0]=load_fx("puzzles/blob_beam");
    if(!effects[1])effects[1]=load_fx("puzzles/blob_beamspawn2");
    float end[3]{};fn<unsigned char(__fastcall*)(void*,void*,int,float*,unsigned char,unsigned char)>(0x41d620)(actor,nullptr,2,end,0,0);
    for(int i=1;i<=8;++i){char name[24]{};std::snprintf(name,sizeof name,"blob_effect%02d",i);u32 h=0;auto* entity=find(name,h);
        if(!mold_query::is_a(entity,0x70b678)||!(at<u32>(entity,4)&(1u<<20)))continue;
        float scratch[3]{};const auto* start=virt<const float*(__fastcall*)(void*,void*,float*)>(entity,0x24)(entity,nullptr,scratch);
        float d[3]{};for(unsigned axis=0;axis<3;++axis)d[axis]=end[axis]-start[axis];
        emit(effects[0],start,d,owner);
        const float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(length>0){float unit[3]{};for(unsigned axis=0;axis<3;++axis)unit[axis]=d[axis]/length;
            emit(effects[1],start,unit,owner);for(auto& axis:unit)axis=-axis;emit(effects[1],end,unit,owner);}
    }
}
void schedule_jump(void* actor,float deadline){
    const auto ids=heroes();for(unsigned i=0;i<ids.size();++i){auto* hero=resolve(ids[i]);
        const bool alive=living(hero);
        if(blob_rules::jump_candidate(alive,alive&&ai(hero),alive?distance(actor,hero):0)){
            jumps[i]={owner,ids[i],deadline};if(trace)logger::write("blob jump scheduled owner %08X hero %08X at %.3f",owner,ids[i],deadline);
        }}
}
struct game {
    void* b;void* a;void* hero;u32* output;
    // -1 means short-circuited, so tracing never calls a gameplay predicate twice.
    int arrival=-1,prop_facing=-1,alternate3=-1;
    bool available(int n,bool alt){const bool result=alt?alternate(b,a,n):boss_runtime::available(b,a,n);if(alt&&n==3)alternate3=result;return result;}
    void buttons(u32 n){boss_runtime::buttons(output,n);}
    void taunt(float p){boss_runtime::taunt(b,p);}
    void wait(float t){boss_runtime::wait(b,t);}
    float distance(){return boss_runtime::distance(a,hero);}
    void jump(float deadline){schedule_jump(a,deadline);}
    void stop(){clear_motion(a);}
    bool face(float t){return facing(b,a,hero,output,t);}
    bool target_blocked(){return boss_wave3_runtime::target_blocked(hero);}
    float random(){return boss_runtime::random();}
    u32 query(){return query_prop(a);}
    bool valid(u32 h){return mold_query::is_a(resolve(h),0x753f64);}
    void approach(u32 h){auto* prop=resolve(h);if(!prop)return;move_to(a,prop);fn<unsigned char(__fastcall*)(void*,void*,u32)>(0x4a7da0)(a,nullptr,h);}
    bool arrived(u32 h){const bool result=arrived_impl(h);arrival=result;return result;}
    bool arrived_impl(u32 h){
        auto* prop=resolve(h);if(!prop)return false;
        auto* nav=at<void*>(a,0x3e8);
        if((nav&&virt<int(__fastcall*)(void*,void*)>(nav,8)(nav,nullptr)==3)||
            fn<unsigned char(__fastcall*)(void*,void*)>(0x5025b0)(b,nullptr)||at<int>(b,0x1c0)>0)return true;
        // Bounds getter returns min/max vectors. Ignore Z for the approach radius.
        const auto* bounds=virt<const float*(__fastcall*)(void*,void*)>(prop,0x38)(prop,nullptr);
        const float dx=bounds[3]-bounds[0],dy=bounds[4]-bounds[1];
        return boss_runtime::distance(a,prop)<(std::min)(100.f,std::sqrt(dx*dx+dy*dy));
    }
    bool face_prop(u32 h,float t){const bool result=facing(b,a,resolve(h),output,t);prop_facing=result;return result;}
    void clear_navigation_counter(){put(b,0x1c0,0);}
    u32 carried(){return at<u32>(a,0x76c);}
    void drop_other(){
        buttons(0x10);put(output,8,1.f);put(output,0x14,1.f);put(output,0x18,0.f);put(output,0x1c,0.f);
        fn<void(__fastcall*)(void*,void*,u32)>(0x42d8e0)(a,nullptr,0);
    }
    bool aim(){
        if(fn<int(__fastcall*)(void*,void*,void*,void*,unsigned char,float)>(0x509ba0)(b,nullptr,a,hero,0,.7f)==0)return true;
        float d[3]{at<float>(hero,0x20)-at<float>(a,0x20),at<float>(hero,0x24)-at<float>(a,0x24),0};
        if(!fn<unsigned char(__fastcall*)(void*,void*,float*)>(0x502c50)(b,nullptr,d))return false;
        for(u32 i=0;i<3;++i)put(output,0x14+4*i,d[i]);put(output,8,1.f);return false;
    }
    void throw_object(){
        float d[3]{at<float>(hero,0x20)-at<float>(a,0x20),at<float>(hero,0x24)-at<float>(a,0x24),0};
        if(fn<unsigned char(__fastcall*)(void*,void*,float*)>(0x502c50)(b,nullptr,d)){
            float angles[3]{};fn<float*(__cdecl*)(float*,const float*)>(0x5607b0)(angles,d);
            fn<void(__fastcall*)(void*,void*,const float*)>(0x5021a0)(a,nullptr,angles);
        }
        buttons(0x10);
    }
};
}
bool prepare(HMODULE image,std::vector<xml1_hook_transaction::patch>& patches){
    ready=enabled=false;reset();weights={};if(!listed("blob")||!boss_wave3_runtime::supported(image))return false;
    if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(0x4989a8),"8b17682c906800"))return false;
    patches.push_back(jump(0x4989a8,{0x8b,0x17,0x68,0x2c,0x90,0x68,0},reinterpret_cast<void*>(&parsed_stub)));
    trace=ini::flag(L"Test",L"BossTrace",false);return ready=true;
}
void committed(bool ok){enabled=ready&&ok;if(enabled)logger::write("blob brain: installed (NYC/Danger Room/astral; opcode interpreter and companion jumps)");}
bool installed(){return enabled;}
bool in_scope(){return enabled&&scope();}
void on_default_target(const char* name){if(in_scope()&&std::strcmp(name,"blob")==0)reset();}
bool companion(void* b,u32* output){
    if(!enabled||!b)return false;if(!scope()){reset();return false;}if(now()<last||(owner&&bound_zone!=zone_id())){reset();return false;}
    if(owner&&!living(resolve(owner))){reset();return false;}
    auto* a=at<void*>(b,0x10);if(!living(a)||!ai(a)||blocked(b,a))return false;
    if(!owner)return false;
    for(auto& j:jumps){if(j.hero!=at<u32>(a,0x1c))continue;
        if(j.owner!=owner||!resolve(j.hero)||blob_rules::jump_stale(j,now())){j={};return false;}
        if(!blob_rules::jump_due(j,now())||!alternate(b,a,4))return false;
        // XML1 output[1] only. PC brain +0x2dc is XML1 +0x2ac; +0x2b5 bit0
        // asks native postprocessing to apply this vertical jump parameter.
        output[1]|=0x40;put(b,0x2dc,550.f);put(b,0x2b5,static_cast<unsigned char>(at<unsigned char>(b,0x2b5)|1));
        if(trace)logger::write("blob jump owner %08X hero %08X deadline %.3f now %.3f",owner,j.hero,j.at,now());
        wait(b,.4f);j={};return true;
    }
    return false;
}
bool brain(void* b,u32* output){
    if(!enabled)return false;if(!scope()){reset();return false;}auto* a=b?at<void*>(b,0x10):nullptr;u32 h=0;
    if(!identity(a,"blob",h))return false;if(!living(a)){reset();return false;}bind(b,h);if(blocked(b,a))return false;
    if(now()<s.wait_until)return true;
    const auto ids=heroes();const auto count=unsigned(std::count_if(ids.begin(),ids.end(),[](u32 id){return id!=0;}));if(!count)return true;
    tethers(a);s.hero_index%=count;auto* hero=resolve(ids[s.hero_index]);
    if(!blob_rules::indexed_target(living(hero),hero?distance(a,hero):0)){s.hero_index=(s.hero_index+1)%count;u32 active=0;hero=mold_query::active_hero(active);}
    if(!living(hero))return true;const u32 target_id=at<u32>(hero,0x1c);target(b,a,target_id);follow(b,a,target_id);
    game g{b,a,hero,output};const auto pc=s.pc,op=blob_rules::opcode(s,zone("astral/ast3/astral3_5"));blob_rules::update(s,op,now(),g);
    if(trace){
        logger::write("blob trace owner %08X pc %u opcode %u next %u prop %08X until %.3f wait_until %.3f now %.3f",owner,pc,op,s.pc,s.prop,s.until,s.wait_until,now());
        if(op==12)logger::write("blob pickup owner %08X prop %08X arrival=%d facing=%d alternate_action_slot_3=%d carried=%08X (-1=not_evaluated)",owner,s.prop,g.arrival,g.prop_facing,g.alternate3,g.carried());
    }
    return true;
}
}

#include "mold_controller.hpp"
#include "xml1_turrets.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "limits_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "mold_query.hpp"
#include "game_version.hpp"
#include "mold_combat_rules.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Work-in-progress scoped compatibility adapter. Kept opt-in until the live
// collision, phase, save/load and authority gates have all been demonstrated.
namespace mold_controller {
namespace {
using u32=std::uint32_t;
template<class T> T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T> void put(void* p,u32 off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T> T fn(u32 a){return reinterpret_cast<T>(static_cast<std::uintptr_t>(a));}
using resolve_t=void*(__fastcall*)(const u32*,void*);
using lookup_t=u32*(__cdecl*)(u32*,const char*);
using clock_t=float(__cdecl*)();
using intern_t=u32*(__fastcall*)(u32*,void*,const char*);
using node_t=void*(__fastcall*)(void*,void*,const u32*);
using play_t=void(__fastcall*)(void*,void*,void*,int);
using target_t=void(__fastcall*)(void*,void*,u32);
using aim_t=int(__fastcall*)(void*,void*,void*,void*,unsigned char,float);
using default_t=int(__fastcall*)(void*,void*,void*,u32*,void*);
using script_t=void*(__cdecl*)(void*);
using arg_t=void*(__fastcall*)(void*,void*,int);
using text_t=const char*(__fastcall*)(void*,void*);
struct state {
    u32 owner=0,target=0;
    int phase=0,step=0;
    bool forced=false,pending_shield=false,shield=false,catwalk_latch=false,catwalk_pending=false;
    float wait_until=0,retry_at=0,shot_until=0;
    mold_combat_rules::deadlines attacks{};
} boss;
bool enabled=false;
mold_combat_rules::stage_binding saved_stage;
script_t real_sequence=nullptr,real_node=nullptr,real_default_target=nullptr;
default_t real_default=fn<default_t>(0x51dfe0);
float now(){return fn<clock_t>(0x45fe60)();}
void reset(u32 owner){
    boss=state{};boss.owner=owner;boss.attacks=mold_combat_rules::deadlines(now());
    u32 stage=0;
    for(int i=0;i<saved_stage.width;++i)
        if(fn<int(__stdcall*)(const char*,int)>(0x4a0190)(saved_stage.flag.c_str(),saved_stage.first+i))stage|=1u<<i;
    if(stage==2)boss.phase=1;
    else if(stage==3||stage==4)boss.phase=2;
    logger::write("mold controller: saved stage %u, combat phase %d",stage,boss.phase);
}
void* find(const char* name,u32& h){return fn<resolve_t>(0x4654b0)(fn<lookup_t>(0x4a1700)(&h,name),nullptr);}
bool scope(){return std::strcmp(reinterpret_cast<const char*>(0x72a758),"mastermold/mastermold2")==0;}
void* move(void* actor,const char* name){u32 h=0;fn<intern_t>(0x41b120)(&h,nullptr,name);return fn<node_t>(0x427ca0)(actor,nullptr,&h);}
bool play(void* actor,const char* name){auto* n=move(actor,name);if(!n)return false;fn<play_t>(0x42c340)(actor,nullptr,n,1);return true;}
const char* argument(void* args,int i){auto* a=fn<arg_t>(0x4d5830)(args,nullptr,i);return a?at<text_t>(at<void*>(a,0),0x14)(a,nullptr):"";}
void* resolve(u32 h){return h?fn<resolve_t>(0x4654b0)(&h,nullptr):nullptr;}
void target(void* actor,u32 h){fn<target_t>(0x42c7c0)(actor,nullptr,h);}
float distance2(void* a,void* b){float sum=0;for(u32 i=0;i<3;++i){const auto d=at<float>(a,0x20+4*i)-at<float>(b,0x20+4*i);sum+=d*d;}return sum;}
void buttons(u32* output,u32 bits){output[0]|=bits;output[1]|=bits;}
bool face(void* brain,void* actor,void* other,u32* output,float tolerance){
    const auto turn=fn<aim_t>(0x509ba0)(brain,nullptr,actor,other,0,tolerance);
    if(turn==-1)buttons(output,0x08000000);
    else if(turn!=0)buttons(output,0x10000000);
    return turn==0;
}
void wait(void* brain,float seconds){boss.wait_until=now()+seconds;fn<void(__fastcall*)(void*,void*,float)>(0x502580)(brain,nullptr,seconds);}
bool available(void* brain,void* actor,int action,bool alternate=false){
    if(!at<void*>(actor,0x378))return false;
    return fn<unsigned char(__fastcall*)(void*,void*,int)>(alternate?0x529690:0x529630)(brain,nullptr,action)!=0;
}
void voice(void* brain){fn<void(__fastcall*)(void*,void*,float)>(0x5279c0)(brain,nullptr,1.f);}
void move_to(void* actor,void* point){
    if(!point||!mold_query::is_a(point,0x778b3c))return;
    using move_t=void(__fastcall*)(void*,void*,const float*,u32,float);
    const auto movement=at<move_t>(at<const void*>(actor,0),0x160);
    if(movement!=fn<move_t>(0x424a90))return;
    target(actor,0);
    movement(actor,nullptr,reinterpret_cast<const float*>(static_cast<const unsigned char*>(point)+0x20),0,3.f);
    fn<unsigned char(__fastcall*)(void*,void*,u32)>(0x4a7da0)(actor,nullptr,at<u32>(point,0x1c));
}
void* select_core(void* actor,u32& selected){
    selected=mold_query::select(actor,boss.forced);
    return selected?fn<resolve_t>(0x4654b0)(&selected,nullptr):nullptr;
}
void tick(void* brain,void* actor,u32* output){
    if(at<unsigned char>(reinterpret_cast<const void*>(0x70b70a),0)||at<float>(actor,0x27c)<=0)return;
    const float t=now();
    if(t<boss.wait_until||t<at<float>(brain,0x1c)){if(boss.step==3&&resolve(boss.target))target(actor,boss.target);return;}
    if(boss.step==3){boss.step=0;boss.target=0;}
    if(boss.pending_shield){
        if(move(actor,"blaster")&&move(actor,"doubleblaster")&&move(actor,"shockshield_off")&&play(actor,"shockshield_on")){
            boss.shield=true;boss.pending_shield=false;wait(brain,2);
            logger::write("mold controller: shield requested at phase 3, owner %08X",boss.owner);
        }else logger::write("mold controller: required native combat nodes unavailable; shield not enabled");
        return;
    }
    std::array<void*,12> markers{};int first_count=0;
    for(int i=0;i<12;++i){char name[]{char('A'+i%6),char('1'+i/6),0};u32 h=0;auto* p=find(name,h);
        if(mold_query::is_a(p,0x753f64)){markers[i]=p;if(i<6)++first_count;}}
    if(boss.catwalk_pending){
        boss.catwalk_pending=false;u32 h=0;auto* action=find("killcatwalks",h);
        using activate_t=int(__fastcall*)(void*,void*,void*,unsigned char);
        if(mold_query::is_a(action,0x70b678)&&at<activate_t>(at<void*>(action,0),0xd4)==fn<activate_t>(0x41b5b0)){
            fn<activate_t>(0x41b5b0)(action,nullptr,actor,0);
            logger::write("mold controller: native catwalk action activated");
        }
    }
    if(boss.phase!=0&&!boss.catwalk_latch){
        boss.catwalk_latch=true;boss.catwalk_pending=first_count!=0;
        return;
    }
    u32 hero_id=0;auto* hero=find("_ACTIVE_HERO_",hero_id);
    const bool living_hero=mold_query::is_a(hero,0x70b83c)&&at<float>(hero,0x27c)>0;
    if(!living_hero)return;
    fn<target_t>(0x538290)(brain,nullptr,hero_id);target(actor,hero_id);
    if(boss.forced){
        u32 chosen=0;if(select_core(actor,chosen)){boss.target=chosen;boss.step=1;boss.forced=false;}
    }
    if(boss.step==1){
        auto* core=resolve(boss.target);
        if(mold_query::is_a(core,0x753f64)&&at<float>(core,0x27c)>0){
            if(face(brain,actor,core,output,.98f))boss.step=2;
            return;
        }
        boss.step=0;boss.target=0;
    }
    if(boss.step==2){
        auto* core=resolve(boss.target);
        const auto decision=mold_combat_rules::ready_core(living_hero,mold_query::is_a(core,0x753f64)&&at<float>(core,0x27c)>0,move(actor,"blaster")!=nullptr);
        if(decision==mold_combat_rules::core_action::fire){
            play(actor,"blaster");target(actor,boss.target);
            boss.step=3;boss.shot_until=t+4;boss.retry_at=t+9;wait(brain,4);
            logger::write("mold controller: ordinary blaster, source %08X target %08X",boss.owner,boss.target);
        }else if(decision==mold_combat_rules::core_action::abandon){boss.step=0;boss.target=0;boss.retry_at=t+9;}
        return;
    }
    if(boss.phase==2&&t>boss.retry_at){
        u32 h=0;if(select_core(actor,h)){boss.target=h;boss.step=1;voice(brain);logger::write("mold controller: aiming at %08X (party bait)",h);}
    }
    const float range=distance2(actor,hero);
    auto& deadline=boss.attacks.value;
    if(boss.phase==0){
        if(std::fabs(at<float>(hero,0x28)-at<float>(actor,0x28))<60&&range<250.f*250.f){
            if(t>deadline[3]){play(actor,"stomp");deadline[3]=t+5;}return;
        }
        if(!first_count){boss.phase=1;deadline[0]=t+2;deadline[1]=t+5;return;}
        int nearest=-1;float distance=std::numeric_limits<float>::max();
        for(int i=0;i<12;++i)if(markers[i]){const auto d=distance2(hero,markers[i]);if(d<distance){distance=d;nearest=i;}}
        if(nearest<0)return;
        if(!face(brain,actor,nearest<6?markers[nearest]:hero,output,.9f))return;
        if(nearest>=6){if(t>deadline[1]&&available(brain,actor,7)){deadline[1]=t+8;buttons(output,0x180);target(actor,hero_id);}return;}
        using kill_t=void(__fastcall*)(void*,void*);
        if(t>deadline[0]&&available(brain,actor,2,true)&&at<kill_t>(at<void*>(markers[nearest],0),8)==fn<kill_t>(0x499740)){
            fn<kill_t>(0x499740)(markers[nearest],nullptr);deadline[0]=t+10;buttons(output,0x20);voice(brain);
            logger::write("mold controller: native platform marker death %d",nearest);
        }else buttons(output,0x10);
        return;
    }
    if(at<float>(hero,0x28)<-100&&range<300.f*300.f){
        target(actor,0);fn<void(__fastcall*)(void*,void*,unsigned char)>(0x502cc0)(brain,nullptr,1);
        fn<unsigned char(__fastcall*)(void*,void*,u32)>(0x4a7da0)(actor,nullptr,hero_id);put(brain,0x1c4,1.f);
        const int due=boss.attacks.floor_due(t);
        if(due<0)return;
        constexpr std::array<int,3> slots{7,5,8};constexpr std::array<u32,3> masks{0x180,0x90,0xc0};
        const bool ready=available(brain,actor,slots[due]);boss.attacks.floor_result(due,t,ready);
        if(ready){
            buttons(output,masks[due]);if(due!=1)voice(brain);target(actor,hero_id);wait(brain,1);
            logger::write("mold controller: floor action %X at %.3f, deadline %.3f",masks[due],t,deadline[due]);
        }
        return;
    }
    auto* point=resolve(mold_query::waypoint(hero));
    if(point&&distance2(actor,point)<100.f*100.f){
        if(!face(brain,actor,hero,output,.9f)){
            // Source clears only actor movement state here, without changing
            // the brain's movement mode or dispatching a stop-movement node.
            for(u32 offset=0x1d0;offset<=0x1e4;offset+=4)put(actor,offset,0x00800000u);
            put(actor,0x180,0u);put(actor,0x184,0u);put(actor,0x3ec,0u);return;
        }
        if(range<250.f*250.f){output[1]|=0x10;return;}
        if(t>deadline[0]&&available(brain,actor,5)){
            deadline[0]=t+4;const float random=fn<float(__cdecl*)(unsigned char)>(0x560190)(1);
            voice(brain);buttons(output,mold_combat_rules::ranged_choice(random));
            logger::write("mold controller: waypoint action %X at %.3f, deadline %.3f",mold_combat_rules::ranged_choice(random),t,deadline[0]);
        }
        return;
    }
    if(!point||distance2(actor,point)>=840.f*840.f){u32 h=0;point=find("Centre",h);}
    move_to(actor,point);
}
void request(const char* source,const char* sequence){
    if(!enabled||!scope()||std::strcmp(source,"mastermold")!=0)return;
    u32 h=0;auto* actor=find(source,h);if(!actor)return;
    if(h!=boss.owner)reset(h);
    if(std::strcmp(sequence,"mold2")==0)boss.phase=1;
    else if(std::strcmp(sequence,"mold3")==0){boss.phase=2;boss.pending_shield=true;}
    else if(std::strcmp(sequence,"moldblowcore")==0)boss.forced=true;
    else return;
    logger::write("mold controller: request %s, owner %08X",sequence,h);
}
void* __cdecl sequence_hook(void* args){const auto* source=argument(args,0);const auto* sequence=argument(args,1);request(source,sequence);return real_sequence(args);}
void* __cdecl spawn_hook(void* args){
    // The authored spawn script selects its owner before reading saved stage
    // flags. Reset transient state even when a same-zone reload reuses an ID.
    if(enabled&&scope()){
        u32 named=0,owner=0;
        auto* candidate=find(argument(args,0),named);
        auto* actor=find("mastermold",owner);
        if(actor&&candidate==actor){reset(owner);logger::write("mold controller: spawn/default-target reset %08X",owner);}
    }
    return real_default_target(args);
}
void* __cdecl node_hook(void* args){
    if(enabled&&scope()&&std::strcmp(argument(args,0),"mastermold")==0&&std::strcmp(argument(args,1),"shockshield_off")==0){boss.target=0;boss.step=0;boss.shield=false;boss.pending_shield=false;}
    return real_node(args);
}
int __fastcall default_hook(void* node,void* edx,void* first,u32* output,void* last){
    if(!scope())boss=state{};
    if(!enabled||!boss.owner||!scope())return real_default(node,edx,first,output,last);
    auto* brain=at<void*>(node,4);auto* actor=brain?at<void*>(brain,0x10):nullptr;
    if(enabled&&scope()&&actor&&at<u32>(actor,0x1c)==boss.owner){
        logger::write_once("mold-default", "mold controller: native AI update reached, first %p last %p",first,last);
        tick(brain,actor,output);
        return 1;
    }
    return real_default(node,edx,first,output,last);
}
}
bool beam_target(void* actor,void*& target){
    target=nullptr;if(!enabled||!scope()||at<u32>(actor,0x1c)!=boss.owner||boss.step!=3||now()>boss.shot_until)return false;
    target=fn<resolve_t>(0x4654b0)(&boss.target,nullptr);return target&&at<float>(target,0x27c)>0;
}
void install(HMODULE game){
    if(!ini::flag(L"Game",L"Xml1MasterMold",false))return;
    if(reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base)return;
    const auto binding=mold_combat_rules::parse_binding(ini::text(L"Game",L"Xml1MoldStage").value_or(""));
    if(!binding){logger::write("mold controller: saved-stage binding unavailable; native fallback retained");return;}
    saved_stage=*binding;
    xml1_hook_transaction::patch beam;
    if(!xml1_turrets::prepare_beam(beam)||!mold_query::supported()){logger::write("mold controller: combat/query compatibility unavailable; no shield/controller hooks enabled");return;}
    constexpr std::array<limits_rules::guard,20> guards{{
        {0x694994,"e0df5100","default AI update vtable"},
        {0x509ba0,"83ec18","native facing helper"},
        {0x41a730,"d9442408","native entity scheduler"},
        {0x68adb8,"30484a00","native sequence script entry"},
        {0x49f726,"a20ab77000","enemy no-target gate"},
        {0x538290,"8b442404894118c20400","brain target setter"},
        {0x529630,"8b41108a886407000081ec84000000f6","normal action availability"},
        {0x529690,"8b41108a886407000081ec84000000f6","alternate action availability"},
        {0x4a7da0,"518b44240885c05356578bf174768d4c","movement target setter"},
        {0x502cc0,"8a91780200008a442404538a99780200","movement mode"},
        {0x502580,"568bf1808eb502000001e851b7f6ff8b","native brain wait"},
        {0x5279c0,"566a018bf1e8c6870300d85c240c83c4","attack voice helper"},
        {0x560190,"8a44240484c07405e9f3feffffe93eff","native random stream"},
        {0x424a90,"8b44240483ec0c55568bf1578daed001","actor movement"},
        {0x499740,"83ec6ca154fa6700565050506a016a00","physent native death"},
        {0x41b5b0,"83ec18558b6c242085ed56578bf17461","action activation"},
        {0x41fbb0,"568b711885f67507b8681968005ec3e8","instance name"},
        {0x402b60,"568b74240885f6742a8b06578bceff10","native ancestry layout"},
        {0x45fe60,"8b0d348471008b01ffa060010000","game clock"},
        {0x4a0190,"8b4424045650e8d58503008bc8e8fe660300","saved game flag read"}}};
    for(const auto& g:guards)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)){logger::write("mold controller: guard failed %08X",g.va);return;}
    const auto table=at<u32>(reinterpret_cast<const void*>(0x49fe31),0);
    const auto sequence=table+(0x68adb8-0x68a908),node=table+(0x68ae58-0x68a908),spawn=table+(0x68ae88-0x68a908);
    real_sequence=at<script_t>(reinterpret_cast<const void*>(sequence),0);real_node=at<script_t>(reinterpret_cast<const void*>(node),0);
    real_default_target=at<script_t>(reinterpret_cast<const void*>(spawn),0);
    if(real_sequence!=fn<script_t>(0x4a4830)||real_node!=fn<script_t>(0x4a51a0)||real_default_target!=fn<script_t>(0x4a4e20)){logger::write("mold controller: active script table differs; no shield/controller hooks enabled");return;}
    using xml1_hook_transaction::pointer_bytes;
    const std::vector<xml1_hook_transaction::patch> writes{
        beam,
        {sequence,pointer_bytes(real_sequence),pointer_bytes(&sequence_hook)},
        {node,pointer_bytes(real_node),pointer_bytes(&node_hook)},
        {spawn,pointer_bytes(real_default_target),pointer_bytes(&spawn_hook)},
        {0x694994,pointer_bytes(real_default),pointer_bytes(&default_hook)}};
    xml1_hook_transaction::memory memory;
    bool safe=true;
    if(xml1_hook_transaction::apply(memory,writes,&safe)){enabled=true;logger::write("mold controller: scoped opt-in requests and native AI update installed (no external scheduled events)");}
    else if(safe)logger::write("mold controller: dependency transaction refused; native fallback retained");
    else {game_version::refuse_startup();logger::write("mold controller: rollback/protection verification failed; process startup refused");}
}
}

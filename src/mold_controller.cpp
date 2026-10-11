#include "mold_controller.hpp"
#include "xml1_turrets.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "limits_rules.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// Work-in-progress scoped compatibility adapter. Kept opt-in until the live
// collision, phase, save/load and authority gates have all been demonstrated.
namespace mold_controller {
namespace {
using u32=std::uint32_t;
template<class T> T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
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
using schedule_t=void(__fastcall*)(void*,void*,void*,float);
struct event {void** vtable=nullptr;float due=0;};
struct state {
    u32 owner=0,target=0;
    int phase=1,step=0;
    bool forced=false,pending_shield=false,shield=false;
    float wait_until=0,retry_at=0,shot_until=0;
    event timer;
} boss;
bool enabled=false;
script_t real_sequence=nullptr,real_node=nullptr;
default_t real_default=fn<default_t>(0x51dfe0);
float now(){return fn<clock_t>(0x45fe60)();}
void* find(const char* name,u32& h){return fn<resolve_t>(0x4654b0)(fn<lookup_t>(0x4a1700)(&h,name),nullptr);}
bool scope(){return std::strcmp(reinterpret_cast<const char*>(0x72a758),"mastermold/mastermold2")==0;}
void* move(void* actor,const char* name){u32 h=0;fn<intern_t>(0x41b120)(&h,nullptr,name);return fn<node_t>(0x427ca0)(actor,nullptr,&h);}
bool play(void* actor,const char* name){auto* n=move(actor,name);if(!n)return false;fn<play_t>(0x42c340)(actor,nullptr,n,1);return true;}
const char* argument(void* args,int i){auto* a=fn<arg_t>(0x4d5830)(args,nullptr,i);return a?at<text_t>(at<void*>(a,0),0x14)(a,nullptr):"";}
void arm(void* actor,float delay){if(boss.timer.due!=0)return;boss.timer.due=now()+delay;fn<schedule_t>(0x41a730)(actor,nullptr,&boss.timer,delay);}
float distance2(void* a,void* b){float n=0;for(u32 i=0;i<3;++i){float d=at<float>(a,0x20+i*4)-at<float>(b,0x20+i*4);n+=d*d;}return n;}
void* select_core(void* actor,u32& selected){
    std::array<void*,4> heroes{};
    if(!boss.forced){
        for(unsigned i=0;i<4;++i){char name[]="_HERO1_";name[5]=char('1'+i);u32 h=0;auto* hero=find(name,h);
            if(hero&&at<float>(hero,0x27c)>0&&distance2(actor,hero)<380*380)heroes[i]=hero;
        }
    }
    void* best=nullptr;float best_distance=800*800;
    for(unsigned i=0;i<3;++i){char name[]="warp1";name[4]=char('1'+i);u32 h=0;auto* core=find(name,h);
        if(!core||at<float>(core,0x27c)<=0||(at<u32>(core,4)&0x100000)==0)continue;
        const float d=distance2(actor,core);if(d>=best_distance)continue;
        bool eligible=boss.forced;
        for(auto* hero:heroes)if(hero&&distance2(hero,core)<360*360)eligible=true;
        if(eligible){best=core;selected=h;best_distance=d;}
    }
    return best;
}
void tick(void* actor){
    if(!scope()||at<u32>(actor,0x1c)!=boss.owner||at<float>(actor,0x27c)<=0||boss.phase>=4)return;
    const float t=now();
    if(boss.pending_shield){
        if(move(actor,"blaster")&&move(actor,"doubleblaster")&&move(actor,"shockshield_off")&&play(actor,"shockshield_on")){
            boss.shield=true;boss.pending_shield=false;boss.wait_until=t+2;
            logger::write("mold controller: shield requested at phase 3, owner %08X",boss.owner);
        }else{logger::write("mold controller: required native combat nodes unavailable; shield not enabled");return;}
    }
    if(t<boss.wait_until){arm(actor,.05f);return;}
    if(boss.step==3){
        if(t>=boss.shot_until){boss.step=0;boss.target=0;fn<target_t>(0x42c7c0)(actor,nullptr,0);}
        arm(actor,.05f);return;
    }
    if(boss.step==2){
        if(play(actor,"blaster")){
            fn<target_t>(0x42c7c0)(actor,nullptr,boss.target);
            boss.step=3;boss.shot_until=t+4;boss.retry_at=t+9;boss.forced=false;
            logger::write("mold controller: ordinary blaster, source %08X target %08X",boss.owner,boss.target);
        }else{boss.step=0;boss.target=0;}
    }else if(boss.step==0&&t>=boss.retry_at&&(boss.forced||boss.phase==3)){
        u32 h=0;if(select_core(actor,h)){boss.target=h;boss.step=1;logger::write("mold controller: aiming at %08X (%s)",h,boss.forced?"forced request":"party bait");}
    }
    arm(actor,.05f);
}
void __fastcall callback(event* e,void*,void* actor,const float* time){if(e->due!=*time)return;e->due=0;tick(actor);}
const float* __fastcall payload(event* e,void*,u32* size){*size=4;return &e->due;}
unsigned char __fastcall pending(event* e,void*){return e->due!=0;}
std::array<void*,3> event_vtable{reinterpret_cast<void*>(&callback),reinterpret_cast<void*>(&payload),reinterpret_cast<void*>(&pending)};
void request(const char* source,const char* sequence){
    if(!enabled||!scope()||std::strcmp(source,"mastermold")!=0)return;
    u32 h=0;auto* actor=find(source,h);if(!actor)return;
    if(h!=boss.owner){boss=state{};boss.owner=h;boss.timer.vtable=event_vtable.data();}
    if(std::strcmp(sequence,"mold2")==0)boss.phase=2;
    else if(std::strcmp(sequence,"mold3")==0){boss.phase=3;boss.pending_shield=!boss.shield;}
    else if(std::strcmp(sequence,"moldblowcore")==0)boss.forced=true;
    else return;
    logger::write("mold controller: request %s, owner %08X",sequence,h);arm(actor,.05f);
}
void* __cdecl sequence_hook(void* args){const auto* source=argument(args,0);const auto* sequence=argument(args,1);request(source,sequence);return real_sequence(args);}
void* __cdecl node_hook(void* args){
    if(enabled&&scope()&&std::strcmp(argument(args,0),"mastermold")==0&&std::strcmp(argument(args,1),"shockshield_off")==0){boss.phase=4;boss.target=0;boss.step=0;boss.shield=false;}
    return real_node(args);
}
int __fastcall default_hook(void* node,void* edx,void* first,u32* output,void* last){
    if(!enabled||!boss.owner||!scope())return real_default(node,edx,first,output,last);
    auto* brain=at<void*>(node,4);auto* actor=brain?at<void*>(brain,0x10):nullptr;
    if(enabled&&scope()&&actor&&at<u32>(actor,0x1c)==boss.owner){
        if(boss.step==1){
            auto* core=fn<resolve_t>(0x4654b0)(&boss.target,nullptr);
            if(!core||at<float>(core,0x27c)<=0){boss.step=0;boss.target=0;}
            else{
                const int turn=fn<aim_t>(0x509ba0)(brain,nullptr,actor,core,0,.98f);
                if(turn==0)boss.step=2;
                else if(turn==-1){output[0]|=0x08000000;output[1]|=0x08000000;}
                else{output[0]|=0x10000000;output[1]|=0x10000000;}
                return 1;
            }
        }
        if(boss.step==2||boss.step==3)return 1;
    }
    return real_default(node,edx,first,output,last);
}
struct replacement{u32 address;void* pointer;};
bool replace_all(const std::array<replacement,3>& writes){
    std::array<DWORD,3> protection{};
    for(unsigned i=0;i<writes.size();++i){
        if(!VirtualProtect(reinterpret_cast<void*>(writes[i].address),4,PAGE_READWRITE,&protection[i])){
            for(unsigned j=i;j-- >0;){DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(writes[j].address),4,protection[j],&ignored);}
            return false;
        }
    }
    for(const auto& w:writes)*reinterpret_cast<void**>(w.address)=w.pointer;
    for(unsigned i=0;i<writes.size();++i){DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(writes[i].address),4,protection[i],&ignored);}
    return true;
}
}
bool beam_target(void* actor,void*& target){
    target=nullptr;if(!enabled||!scope()||at<u32>(actor,0x1c)!=boss.owner||boss.step!=3||now()>boss.shot_until)return false;
    target=fn<resolve_t>(0x4654b0)(&boss.target,nullptr);return target&&at<float>(target,0x27c)>0;
}
void install(HMODULE){
    if(!ini::flag(L"Game",L"Xml1MasterMold",false))return;
    if(!xml1_turrets::beam_ready()){logger::write("mold controller: beam compatibility unavailable; no shield/controller hooks enabled");return;}
    constexpr std::array<limits_rules::guard,4> guards{{
        {0x694994,"e0df5100","default AI update vtable"},
        {0x509ba0,"83ec18","native facing helper"},
        {0x41a730,"d9442408","native entity scheduler"},
        {0x68adb8,"30484a00","native sequence script entry"}}};
    for(const auto& g:guards)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)){logger::write("mold controller: guard failed %08X",g.va);return;}
    const auto table=at<u32>(reinterpret_cast<const void*>(0x49fe31),0);
    const auto sequence=table+(0x68adb8-0x68a908),node=table+(0x68ae58-0x68a908);
    real_sequence=at<script_t>(reinterpret_cast<const void*>(sequence),0);real_node=at<script_t>(reinterpret_cast<const void*>(node),0);
    if(real_sequence!=fn<script_t>(0x4a4830)||real_node!=fn<script_t>(0x4a51a0)){logger::write("mold controller: active script table differs; no shield/controller hooks enabled");return;}
    const std::array<replacement,3> writes{{{sequence,reinterpret_cast<void*>(&sequence_hook)},{node,reinterpret_cast<void*>(&node_hook)},{0x694994,reinterpret_cast<void*>(&default_hook)}}};
    if(replace_all(writes)){enabled=true;logger::write("mold controller: scoped opt-in requests and native AI/scheduler installed");}
}
}

#include "mold_controller.hpp"
#include "xml1_turrets.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "limits_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "mold_query.hpp"
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
struct state {
    u32 owner=0,target=0;
    int phase=1,step=0;
    bool forced=false,pending_shield=false,shield=false;
    float wait_until=0,retry_at=0,shot_until=0;
} boss;
bool enabled=false;
script_t real_sequence=nullptr,real_node=nullptr,real_default_target=nullptr;
default_t real_default=fn<default_t>(0x51dfe0);
float now(){return fn<clock_t>(0x45fe60)();}
void* find(const char* name,u32& h){return fn<resolve_t>(0x4654b0)(fn<lookup_t>(0x4a1700)(&h,name),nullptr);}
bool scope(){return std::strcmp(reinterpret_cast<const char*>(0x72a758),"mastermold/mastermold2")==0;}
void* move(void* actor,const char* name){u32 h=0;fn<intern_t>(0x41b120)(&h,nullptr,name);return fn<node_t>(0x427ca0)(actor,nullptr,&h);}
bool play(void* actor,const char* name){auto* n=move(actor,name);if(!n)return false;fn<play_t>(0x42c340)(actor,nullptr,n,1);return true;}
const char* argument(void* args,int i){auto* a=fn<arg_t>(0x4d5830)(args,nullptr,i);return a?at<text_t>(at<void*>(a,0),0x14)(a,nullptr):"";}
void* select_core(void* actor,u32& selected){
    selected=mold_query::select(actor,boss.forced);
    return selected?fn<resolve_t>(0x4654b0)(&selected,nullptr):nullptr;
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
    if(t<boss.wait_until)return;
    if(boss.step==3){
        if(t>=boss.shot_until){boss.step=0;boss.target=0;fn<target_t>(0x42c7c0)(actor,nullptr,0);}
        return;
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
}
void request(const char* source,const char* sequence){
    if(!enabled||!scope()||std::strcmp(source,"mastermold")!=0)return;
    u32 h=0;auto* actor=find(source,h);if(!actor)return;
    if(h!=boss.owner){boss=state{};boss.owner=h;}
    if(std::strcmp(sequence,"mold2")==0){boss=state{};boss.owner=h;boss.phase=2;}
    else if(std::strcmp(sequence,"mold3")==0){boss.phase=3;boss.pending_shield=true;boss.step=0;boss.target=0;}
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
        if(actor&&candidate==actor){boss=state{};boss.owner=owner;logger::write("mold controller: spawn/default-target reset %08X",owner);}
    }
    return real_default_target(args);
}
void* __cdecl node_hook(void* args){
    if(enabled&&scope()&&std::strcmp(argument(args,0),"mastermold")==0&&std::strcmp(argument(args,1),"shockshield_off")==0){boss.phase=4;boss.target=0;boss.step=0;boss.shield=false;}
    return real_node(args);
}
int __fastcall default_hook(void* node,void* edx,void* first,u32* output,void* last){
    if(!scope())boss=state{};
    if(!enabled||!boss.owner||!scope())return real_default(node,edx,first,output,last);
    auto* brain=at<void*>(node,4);auto* actor=brain?at<void*>(brain,0x10):nullptr;
    if(enabled&&scope()&&actor&&at<u32>(actor,0x1c)==boss.owner){
        logger::write_once("mold-default", "mold controller: native AI update reached, first %p last %p",first,last);
        tick(actor);
        if(now()<boss.wait_until)return 1;
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
        if(boss.step==2||boss.step==3){
            // Ordinary AI updates can clear +0x3e0. Keep the generation-checked
            // token for the native attack and correct its beam at emission too.
            if(fn<resolve_t>(0x4654b0)(&boss.target,nullptr))fn<target_t>(0x42c7c0)(actor,nullptr,boss.target);
            return 1;
        }
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
    xml1_hook_transaction::patch beam;
    if(!xml1_turrets::prepare_beam(beam)||!mold_query::supported()){logger::write("mold controller: combat/query compatibility unavailable; no shield/controller hooks enabled");return;}
    constexpr std::array<limits_rules::guard,4> guards{{
        {0x694994,"e0df5100","default AI update vtable"},
        {0x509ba0,"83ec18","native facing helper"},
        {0x41a730,"d9442408","native entity scheduler"},
        {0x68adb8,"30484a00","native sequence script entry"}}};
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
    if(xml1_hook_transaction::apply(memory,writes)){enabled=true;logger::write("mold controller: scoped opt-in requests and native AI update installed (no external scheduled events)");}
    else logger::write("mold controller: dependency transaction refused; native fallback retained");
}
}

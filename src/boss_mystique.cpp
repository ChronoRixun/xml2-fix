#include "boss_mystique.hpp"
#include "boss_runtime.hpp"
#include "mystique_rules.hpp"
namespace boss_mystique {
namespace {
using namespace boss_runtime;
bool ready=false,enabled=false,trace=false;u32 owner=0;float last_time=-1;
mystique_rules::state s;
bool scope(){return zone("nyc/alison/nyc1_1_2b")||zone("mansion/dr_mag2/mag_nyc3")||zone("astroid_m/visit1/asteroid2_1");}
bool bind(void* actor,u32 h){
    if(owner!=h||now()<last_time){
        if(!mold_query::supported())return false;
        owner=h;s={};s.active=false; // myst1 does not enter the custom state; mystique2 starts empty.
    }
    last_time=now();return actor!=nullptr;
}
void restore(void* actor){
    // Same own-stats/skin-preserving path as the PC setSkin script's empty-skin branch (0x4a3e40).
    char skin[64]{};
    fn<void(__fastcall*)(void*,void*,char*,int,int,int)>(0x4b8090)(at<void*>(actor,0x35c),nullptr,skin,64,-1,0);
    const int variant=virt<int(__fastcall*)(void*,void*)>(actor,0x90)(actor,nullptr);
    fn<void(__fastcall*)(void*,void*,const char*,int)>(0x42b240)(actor,nullptr,skin,variant);
    play(actor,"transform_back");s.copied=false;s.restore_at=0;s.copy_target=0;
    if(trace)logger::write("mystique trace: restore owner %08X at %.3f",owner,now());
}
// Observe the actual native skin-copy success; retain its skin selection and query. The entry wrapper keeps
// the authored trigger alive only for this synchronous call. Nested calls restore the previous context.
thread_local void* copying_effect=nullptr;
u32 copy_resume=0x4dff57,skin_resume=0x4e01af;
__declspec(naked) void copy_original(){
    __asm {
        push -1
        push 0674a20h
        jmp dword ptr [copy_resume]
    }
}
unsigned char __fastcall copy_hook(void* effect,void*,void* event){
    auto* previous=copying_effect;copying_effect=effect;
    const auto result=reinterpret_cast<unsigned char(__fastcall*)(void*,void*,void*)>(&copy_original)(effect,nullptr,event);
    copying_effect=previous;return result;
}
void __cdecl copied(void* actor,void* hero){
    u32 h=0;
    if(!enabled||!scope()||!identity(actor,"mystique",h)||!living(hero)||!bind(actor,h)||!copying_effect)return;
    auto* trigger=at<void*>(copying_effect,0x14);if(!trigger)return;
    // PC Knockback parser 0x4dc05f stores the literal word at trigger +4, matching the XML1 duration source.
    const auto duration=at<unsigned short>(trigger,4);
    if(duration==0||duration>=0x8000){logger::write("mystique brain: unsupported nonliteral copy duration");return;}
    mystique_rules::copy(s,now(),float(duration),at<u32>(hero,0x1c));
    if(trace)logger::write("mystique trace: copy owner %08X target %08X duration %u until %.3f",owner,s.target,unsigned(duration),s.restore_at);
}
__declspec(naked) void skin_stub(){
    __asm {
        call dword ptr [ebx+0178h]
        pushfd
        pushad
        push esi
        push dword ptr [edi]
        call copied
        add esp,8
        popad
        popfd
        jmp dword ptr [skin_resume]
    }
}
xml1_hook_transaction::patch jump(u32 va,std::vector<unsigned char> before,void* to){
    auto after=std::vector<unsigned char>(before.size(),0x90);after[0]=0xe9;
    const u32 relative=static_cast<u32>(reinterpret_cast<std::uintptr_t>(to))-va-5;std::memcpy(after.data()+1,&relative,4);
    return {va,std::move(before),std::move(after)};
}
}
bool prepare(HMODULE game,std::vector<xml1_hook_transaction::patch>& patches){
    ready=enabled=false;if(!listed("mystique")||!supported(game))return false;
    if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(0x4dff50),"6aff68204a6700")||
       !limits_rules::matches(reinterpret_cast<const unsigned char*>(0x4e01a9),"ff9378010000")||
       !limits_rules::matches(reinterpret_cast<const unsigned char*>(0x4dc05f),"8d4e04e809edffff"))return false;
    trace=ini::flag(L"Test",L"BossTrace",false);
    patches.push_back(jump(0x4dff50,{0x6a,0xff,0x68,0x20,0x4a,0x67,0x00},reinterpret_cast<void*>(&copy_hook)));
    patches.push_back(jump(0x4e01a9,{0xff,0x93,0x78,0x01,0x00,0x00},reinterpret_cast<void*>(&skin_stub)));
    return ready=true;
}
void committed(bool ok){enabled=ready&&ok;if(enabled)logger::write("mystique brain: installed (NYC, Danger Room, asteroid; native copy + timed restore)");}
bool installed(){return enabled;}
bool in_scope(){return enabled&&scope();}
void on_default_target(const char* name){if(in_scope()&&std::strcmp(name,"mystique")==0){owner=0;s={};s.active=false;}}
void on_sequence(const char* name,const char* sequence){
    if(!in_scope()||std::strcmp(name,"mystique")!=0)return;
    u32 h=0;auto* actor=find(name,h);if(!living(actor)||!bind(actor,h))return;
    if(!mystique_rules::request(s,sequence,now()))return;
    if(auto* b=brain_of(actor)){
        if(std::strcmp(sequence,"myst1")==0)put(b,0x438,0u);
        if(std::strcmp(sequence,"myst3")==0){put(b,0x438,0x204u);fn<unsigned char(__fastcall*)(void*,void*,int)>(0x505030)(b,nullptr,0);}
    }
    if(trace)logger::write("mystique trace: request %s owner %08X mode %d active %d",sequence,owner,s.mode,int(s.active));
}
bool brain(void* b,u32* output){
    if(!enabled)return false;
    if(!scope()){owner=0;s={};last_time=-1;return false;}
    auto* actor=b?at<void*>(b,0x10):nullptr;u32 h=0;
    if(!identity(actor,"mystique",h)||!bind(actor,h)||blocked(b,actor))return false;
    auto* hero=resolve(s.target);
    if(mystique_rules::restore_due(s,now(),living(resolve(s.copy_target)))){restore(actor);return s.active;}
    if(!s.active)return false; // native waypoint/retreat owns this update, including alongside Magneto
    if(!living(hero)){
        s.target=boss_target_rules::mystique(hero_view(actor));hero=resolve(s.target);
    }
    if(!living(hero))return true;
    fn<void(__fastcall*)(void*,void*,u32)>(0x538290)(b,nullptr,s.target);
    if(s.mode==1){
        face(b,actor,hero);
        if(mystique_rules::grenade_due(s,now())&&play(actor,"power_xtreme")){
            s.grenade_at=now()+4;if(trace)logger::write("mystique trace: grenade at %.3f next %.3f",now(),s.grenade_at);
        }
        return true;
    }
    fn<unsigned char(__fastcall*)(void*,void*,u32)>(0x4a7da0)(actor,nullptr,s.target);
    if(distance(actor,hero)<70){
        face(b,actor,hero);
        const float chance=s.copied?random():0;
        const auto bits=mystique_rules::close_buttons(s.copied,chance,s.copied&&chance>.98f?random():0);
        if(bits){buttons(output,bits);if(s.copied)taunt(b,.5f);if(trace)logger::write("mystique trace: buttons %X copied %d",bits,int(s.copied));}
    }
    return true;
}
}

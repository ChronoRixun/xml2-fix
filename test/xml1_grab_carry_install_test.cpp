// Production installer/callbacks against invented services in our own reserved
// executable image. Original hold and cleanup bytes are loaded read-only from
// the owned retail input at runtime, never stored in the repository.
#include <Windows.h>
#include "xml1_grab_carry.hpp"
#include "xml1_grab_carry_rules.hpp"
#include "xml1_air_carry_rules.hpp"
#include "anim_enums.hpp"
#include "anim_enum_names.hpp"
#include "xml1_cling.hpp"
#include "xml1_rogue_decide.hpp"
#include "xml1_rogue_decide_rules.hpp"
#include "entity_capacity.hpp"
#include "entity_query.hpp"
#include "ini.hpp"
#include "game_version.hpp"
#include "thrown_body_fixture.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <cfloat>
#include <xmmintrin.h>
namespace {
using namespace xml1_grab_carry_rules;
using u32=std::uint32_t;
int checks=0,failures=0;
std::string key,cling_key,rogue_key;
std::string guard_key="xml1";
std::optional<bool> flying_carry_key; // [Game] Xml1FlyingCarry (absent = default on)
bool refused=false,target_present=true,is_character=true,reciprocal=true,move_present=true;
int forced=0,lookup_count=0,publishes=0,relatives=0,starts=0,exclusions=0,aux=0,actions=0,last_action=-1,times=0;
std::vector<u32> published;
std::vector<std::string> events;
float now=0;
void* selected=nullptr;
std::array<unsigned char,0x900> actor{},victim{},stats{},move{},other{},idle{},style{};
std::array<void*,128> actor_vt{},move_vt{};
std::array<u32,16> class_info{};
std::array<unsigned char,0x4100> name_table{};
bool log_resolved=false, ally_mode=false, physical_class=true, button4=false, button5=false;
int motions=0, placements=0, angle_sets=0;
void* started_on=nullptr;
bool collision_mode=false;
int locks=0, tags=0, zero_velocities=0, flight_offs=0, contact_reads=0;
std::array<float,3> zero_vector{};
bool acquiring=false, default_victim_move=false;
int move_exits=0, staged=0;
void check(bool b,const char* why){++checks;if(!b){++failures;std::printf("FAIL %s\n",why);}}
template<class T>T get(const void* p,u32 off){T value{};std::memcpy(&value,static_cast<const unsigned char*>(p)+off,sizeof value);return value;}
template<class T>void put(void* p,u32 off,T value){std::memcpy(static_cast<unsigned char*>(p)+off,&value,sizeof value);}
void* ptr(u32 va){return reinterpret_cast<void*>(va);}
void signature(u32 va,std::string_view hex){for(std::size_t i=0;i<hex.size();i+=2){auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};put<unsigned char>(ptr(va),u32(i/2),static_cast<unsigned char>(digit(hex[i])*16+digit(hex[i+1])));}}
template<class T>void jump(u32 va,T target){put<unsigned char>(ptr(va),0,0xe9);put<u32>(ptr(va),1,u32(reinterpret_cast<std::uintptr_t>(target))-va-5);}
std::vector<unsigned char> retail_bytes;
void native(u32 va,std::size_t length){
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(retail_bytes.data()+get<u32>(retail_bytes.data(),0x3c));
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];const auto rva=va-0x400000;
        if(rva>=s.VirtualAddress&&rva+length<=s.VirtualAddress+s.SizeOfRawData){std::memcpy(ptr(va),retail_bytes.data()+s.PointerToRawData+rva-s.VirtualAddress,length);return;}
    }
    check(false,"native fixture range exists");
}
void* __fastcall resolve(const u32* handle,void*){
    if(*handle==0x1111)return actor.data();
    if(*handle==0x2222)return target_present?victim.data():nullptr;
    if(*handle==0x3333)return other.data();
    return nullptr;
}
bool __fastcall valid_handle(const u32* handle,void*){return resolve(handle,nullptr)!=nullptr;}
void* __cdecl cast(void* entity){return is_character?entity:nullptr;}
void* __fastcall styles(void* who,void*){check(who==victim.data()||who==actor.data(),"named move uses an actor style collection");return style.data();}
void* __fastcall named(void* who,void*,const u32* name,void* optional){
    if(name!=ptr(0x78a87c)&&name!=ptr(0x78a8d0)&&name!=ptr(0x78a8c8)){
        check(*name==104,"diagnostic resolves authored victim move separately");return other.data();
    }
    check(who==style.data()&&optional==nullptr,"named lookup style and null optional actor");++lookup_count;
    if(name==ptr(0x78a87c))return move_present?move.data():nullptr;
    if(name==ptr(0x78a8d0))return move_present?move.data():nullptr;
    check(name==ptr(0x78a8c8),"only hard-coded attack victim or idle name requested");return idle.data();
}
void* __fastcall authored_name(u32* key,void*,const char* name){check(std::strcmp(name,"juggrabthrowforwardvictim")==0,"diagnostic names authored victim transition");*key=104;return key;}
void __fastcall force(void* who,void*,void* chosen,int priority,float timing,u32 a,u32 b,u32 c){
    ++forced;check(who==victim.data()&&priority==-1&&timing==(ally_mode?0.375f:-1.f)&&a==0&&b==(ally_mode?1u:0u)&&c==0,"forced transition exact six-argument ABI");
    if(ally_mode)check(publishes==3&&get<u32>(who,4)==0x87&&(get<unsigned char>(who,0x764)&0x10),"ally flags/marker precede victim transition");
    events.push_back("force");
    if(chosen)put(who,0x378,chosen); // native null path is inert
}
void __fastcall publish(void* who,void*){check(who==victim.data(),"flags published on victim");if(acquiring)check(get<u32>(actor.data(),0x178)==0x2222&&get<u32>(victim.data(),0x178)==0x1111,"ownership established before acquisition flags publish");++publishes;published.push_back(get<u32>(who,4));events.push_back("publish");}
void __fastcall relative(void* who,void*,void* owner){++relatives;check(who==victim.data()&&owner==resolve(reinterpret_cast<const u32*>(victim.data()+0x178),nullptr),"owner-relative cleanup receives current resolved owner");events.push_back("relative");}
void __fastcall start(void* who,void*,void* chosen,int reset){++starts;started_on=who;check((who==victim.data()||who==actor.data())&&chosen==(collision_mode?move.data():idle.data())&&reset==1,"actor starts selected move reset1");events.push_back(collision_mode?"start":"idle");}
void __fastcall exclusion(void* who,void*,u32 none,float duration){++exclusions;check((who==actor.data()||who==victim.data())&&none==0&&duration==0.6f,"temporary exclusion null handle and 0.6");events.push_back(who==actor.data()?"exclude-owner":"exclude-victim");}
void __fastcall auxiliary(void* who,void*,bool value){++aux;check(who==victim.data()&&!value,"auxiliary state false");}
const u32* __fastcall name_key(void* who,void*){check(who==move.data(),"name from owner's current move");return reinterpret_cast<const u32*>(move.data()+8);}
void* __cdecl names(){return name_table.data();}
void* __fastcall class_token(void*,void*){return class_info.data();}
float __cdecl time_now(){++times;return now;}
void* __fastcall action(void* who,void*,int which){++actions;last_action=which;check(who==actor.data()||who==victim.data(),"action resolves on an actor");return selected;}
void* __cdecl physical(void* entity){return physical_class?entity:nullptr;}
void __fastcall motion(void* who,void*,int on,int emit){++motions;check(who==victim.data()&&on==1&&emit==0,"pickup disables motion timers with PC expanded ABI");}
float* __cdecl direction(float* out,const void*){out[0]=1;out[1]=out[2]=0;return out;}
void __fastcall place(void* who,void*,const void* position){++placements;check(who==victim.data()&&forced==1&&std::memcmp(position,actor.data()+0x20,12)==0,"ally placement follows force and uses holder position");}
void __fastcall angles(void* who,void*,const void* value){++angle_sets;check(who==victim.data()&&std::memcmp(value,actor.data()+0x2c,12)==0,"ally reference angles come from holder");}
bool __fastcall pressed(void*,void*,void* who,void*,int bit){check(who==victim.data()&&(bit==4||bit==5),"recovery tests attack/smash on victim");return bit==4?button4:button5;}
bool __fastcall collision_tag(void* who,void*,unsigned char tag,void* carrier,void* recipient){++tags;check(who==move.data()&&tag==1&&carrier==actor.data()&&recipient==victim.data(),"collision tag1 preserves struck actor recipient");events.push_back("tag");return true;}
void __fastcall lock_target(void* who,void*,u32 handle){++locks;check(who==actor.data()&&handle==0x2222&&tags==1&&starts==0,"live victim acquired after event and before transition");put(who,0x76c,handle);events.push_back("lock");}
void __fastcall zero_velocity(void* who,void*,const void* value){++zero_velocities;check(who==actor.data()&&value==zero_vector.data(),"collision clears carrier velocity through native service");}
void __fastcall flight(void* who,void*,int on,int emit){++flight_offs;check(who==actor.data()&&on==0&&emit==1,"collision disables flight with native emission argument");}
void* __fastcall contact_component(void*,void*,int){++contact_reads;return zero_vector.data();}
float __fastcall component_value(void*,void*){return -1.f;}
void __fastcall stage_move(void* who,void*,void* next){++staged;check(who==victim.data()&&next==idle.data(),"native transition stages the next move after reset");put(who,0x378,next);}
void __fastcall move_exit(void*,void*,void* who){++move_exits;void* handler=ptr(default_victim_move?0x692f4c:0x693360);auto cleanup=get<void(__fastcall*)(void*,void*,void*)>(handler,8);cleanup(&handler,nullptr,who);}
u32 accepted_grab=0x4de880;
// Invented caller frame for the already-accepted native acquisition branch.
__declspec(naked) void __fastcall acquire_probe(void*,void*,void*){
    __asm {
        push ebx
        push esi
        push edi
        mov esi, ecx
        mov edi, [esp + 16]
        jmp dword ptr [accepted_grab]
    }
}
u32 __fastcall sound(void*,void*,int){return 0;}
void reset(){
    forced=lookup_count=publishes=relatives=starts=exclusions=aux=actions=times=0;last_action=-1;
    published.clear();events.clear();
    started_on=nullptr;motions=placements=angle_sets=0;ally_mode=false;physical_class=true;button4=button5=false;
    collision_mode=false;locks=tags=zero_velocities=flight_offs=contact_reads=0;
    acquiring=default_victim_move=false;move_exits=staged=0;
    put(actor.data(),0x76c,0x2222u);put(victim.data(),0x178,0x1111u);put(actor.data(),0x378,move.data());
    put(victim.data(),0x378,other.data());put(victim.data(),4,0x87u);put<unsigned char>(victim.data(),0x3db,0);
    target_present=is_character=reciprocal=move_present=true;
}
void move_name(const char* name){
    std::memcpy(name_table.data()+0x4008,name,std::strlen(name)+1);put(move.data(),8,1u);put(name_table.data(),8,0u);
}
void bind(){
    jump(0x41b120,&authored_name);
    jump(0x4654b0,&resolve);jump(0x4654d0,&valid_handle);jump(0x402b60,&cast);jump(0x41d0d0,&styles);jump(0x501a50,&named);jump(0x432030,&force);
    jump(0x45fe40,&publish);jump(0x42c340,&start);jump(0x602280,&names);jump(0x41d020,&auxiliary);jump(0x41d060,&auxiliary);
    jump(0x45fe60,&time_now);jump(0x427c80,&action);jump(0x420b10,&sound);
    jump(0x429110,&physical);jump(0x471a20,&motion);jump(0x560990,&direction);jump(0x4fc020,&pressed);
    jump(0x42d8e0,&lock_target);jump(0x473640,&zero_velocity);jump(0x41f7d0,&flight);jump(0x4a9610,&contact_component);
    put(ptr(0x67fa54),0,zero_vector.data());put(ptr(0x67f9d8),0,&component_value);
    actor_vt[0]=reinterpret_cast<void*>(&class_token);actor_vt[0x104/4]=reinterpret_cast<void*>(&exclusion);actor_vt[0x138/4]=reinterpret_cast<void*>(&relative);
    actor_vt[0x150/4]=reinterpret_cast<void*>(&place);actor_vt[0x15c/4]=reinterpret_cast<void*>(&angles);
    put(actor.data(),0,actor_vt.data());put(victim.data(),0,actor_vt.data());put(other.data(),0,actor_vt.data());
    move_vt[0xd0/4]=reinterpret_cast<void*>(&name_key);put(move.data(),0,move_vt.data());
    move_vt[0x14/4]=reinterpret_cast<void*>(&collision_tag);
    move_vt[0x3c/4]=reinterpret_cast<void*>(&move_exit);
    put(actor.data(),0x35c,stats.data());put(actor.data(),0x1c,0x1111u);put(victim.data(),0x1c,0x2222u);put(victim.data(),0x27c,100.f);
    put<unsigned char>(victim.data(),0x57a,0);put<unsigned char>(victim.data(),0x578,0);class_info.fill(~0u);
    put(ptr(0x70b840),0,0u);put(ptr(0x72988c),0,0u);put(ptr(0x69d058),0,0u);
    move_name("grabhold");reset();
}
using entry_t=void(__fastcall*)(void*,void*,void*,void*,void*);
using cleanup_t=void(__fastcall*)(void*,void*,void*);
using frame_t=void(__fastcall*)(void*,void*,void*,void*);
using update_t=bool(__fastcall*)(void*,void*,void*,void*);
void callbacks(void* handler){
    auto* table=get<void**>(handler,0);
    check(table[6]==ptr(0x4fce50),"attack retains generic update");
    check(reinterpret_cast<void*(__fastcall*)(void*,void*,unsigned)>(table[0])(handler,nullptr,1)==handler,"persistent destructor");
    auto entry=reinterpret_cast<entry_t>(table[1]);
    reset();put(actor.data(),0x5e8,123.f);entry(handler,nullptr,actor.data(),ptr(0x123),ptr(0x456));
    check(forced==1&&lookup_count==1&&get<void*>(victim.data(),0x378)==move.data()&&get<float>(actor.data(),0x5e8)==123.f,"attack force and no default entry deadline write");
    reset();move_present=false;entry(handler,nullptr,actor.data(),nullptr,nullptr);check(forced==1&&get<void*>(victim.data(),0x378)==other.data(),"absent grabattackvictim is inert");
    reset();target_present=false;entry(handler,nullptr,actor.data(),nullptr,nullptr);check(forced==0&&lookup_count==0,"missing attack target");
    reset();is_character=false;entry(handler,nullptr,actor.data(),nullptr,nullptr);check(forced==0&&lookup_count==0,"noncharacter attack target");
    reset();put(actor.data(),0x76c,0x9999u);entry(handler,nullptr,actor.data(),nullptr,nullptr);check(forced==0,"stale/recycled target rejected by resolver");
    for(const auto off:{8u,12u}){
        reset();auto cleanup=get<cleanup_t>(ptr(hold_vtable),off);cleanup(nullptr,nullptr,actor.data());
        check(publishes==1&&get<u32>(victim.data(),4)==0x83&&get<u32>(actor.data(),0x76c)==0x2222&&relatives==0,"both hold cleanup slots clear only victim bit2");
        reset();target_present=false;cleanup(nullptr,nullptr,actor.data());check(publishes==0,"cleanup invalid handle does nothing");
    }
    auto frame=get<frame_t>(ptr(victim_vtable),16);
    for(const auto* prefix:{"grabattack","PICKup","teleport","ability"}){
        reset();move_name(prefix);frame(nullptr,nullptr,victim.data(),ptr(0x456));check(publishes==0&&exclusions==0&&starts==0,"paired prefix retained");
    }
    for(int branch=0;branch<7;++branch){
        reset();move_name("run");
        if(branch==1)put(actor.data(),0x76c,0x3333u);
        if(branch==2)put<void*>(actor.data(),0x378,nullptr);
        if(branch==3)put(victim.data(),0x178,0x9999u);
        if(branch==4)is_character=false;
        if(branch==5)put<unsigned char>(victim.data(),0x3d8,8); // PC early-out must not survive
        if(branch==6){move_name("grabhold");put(actor.data(),0x76c,0x9999u);}
        frame(nullptr,nullptr,victim.data(),nullptr);
        check(publishes==3&&published==std::vector<u32>{0x85,0x81,0x80}&&relatives==1&&starts==1,"release publishes bits1/2/0 separately and detaches then idles");
        check(exclusions==((branch==3||branch==4)?0:2),"existing character owner gets pair exclusion");
        check(events.back()=="idle"&&events[events.size()-2]=="relative","release completes in same callback after exclusion");
    }
    auto cleanup=get<cleanup_t>(ptr(victim_vtable),12);
    reset();cleanup(nullptr,nullptr,victim.data());check(publishes==3&&relatives==1&&aux==0&&starts==0,"victim interrupt cleanup no idle and no auxiliary changes when clear");
    reset();put<unsigned char>(victim.data(),0x3db,4);cleanup(nullptr,nullptr,victim.data());check(aux==2,"victim interrupt clears both auxiliary states when marked");
    check(get<void*>(ptr(victim_vtable),4)==ptr(0x4fbdd0)&&get<void*>(ptr(victim_vtable),8)==ptr(0x59eb10),"XML1 default entry and no normal cleanup retained");
}

void fastball_victim_hold_test(){
 reset();move_name("throwally");put(victim.data(),4,0x87u);put(victim.data(),0x27c,100.f);
 // Source throwallyvictim has no handler; its converted ch_grab_victim must
 // not run the generic prefix-based drop before the authored throw trigger.
 get<frame_t>(ptr(victim_vtable),16)(nullptr,nullptr,victim.data(),nullptr);
 if(guard_key=="xml1")check(get<u32>(victim.data(),4)==0x87&&starts==0&&relatives==0,"converted fastball victim stays constrained until its owner's authored throw event");
 else check(get<u32>(victim.data(),4)==0x80&&starts==1&&relatives==1,"Guard OFF retains the prior generic victim cleanup for throwally");
}

void hold_branches(){
    auto update=reinterpret_cast<update_t>(0x4eda40);std::array<unsigned char,0x40> input{};selected=move.data();
    // Zero axes take native action15, not the deadline block. Preserve that
    // behavior and enter the deadline branch explicitly for its focused cases.
    reset();check(update(nullptr,nullptr,actor.data(),input.data())&&last_action==15&&times==0,"retail zero-axis branch is action15");
    std::array<unsigned char,5> direction_entry{};std::memcpy(direction_entry.data(),ptr(0x4edb05),direction_entry.size());
    jump(0x4edb05,ptr(0x4edb76));
    for(const auto pc:{_PC_24,_PC_53})for(const auto rc:{_RC_NEAR,_RC_DOWN,_RC_UP,_RC_CHOP}){
        unsigned int saved=0;_controlfp_s(&saved,0,0);unsigned int ignored=0;_controlfp_s(&ignored,pc|rc,_MCW_PC|_MCW_RC);
        const auto mxcsr=_mm_getcsr();
        for(int large=0;large<2;++large)for(int delta=-1;delta<=1;++delta){
            reset();put<unsigned char>(stats.data(),0x2ad,large?8:0);put(actor.data(),0x5e8,10.f);now=10.f+float(delta);
            const bool result=update(nullptr,nullptr,actor.data(),input.data());
            check(result==(!large&&delta>0)&&actions==(!large&&delta>0?1:0)&&times==1,"strict timeout and large exception under live fp environment");
            if(actions)check(last_action==24,"timeout chooses Special");
        }
        check((_controlfp(0,0)&(_MCW_PC|_MCW_RC))==(unsigned(pc)|unsigned(rc))&&_mm_getcsr()==mxcsr,"callbacks preserve x87 control and MXCSR");
        _controlfp_s(&ignored,saved,_MCW_PC|_MCW_RC);
    }
    std::memcpy(ptr(0x4edb05),direction_entry.data(),direction_entry.size());
    for(int large=0;large<2;++large)for(const auto direction:std::array<std::array<float,3>,4>{{{1,0,15},{-1,0,16},{0,1,14},{0,-1,13}}}){
        reset();put<unsigned char>(stats.data(),0x2ad,large?8:0);put(input.data(),0xc,direction[0]);put(input.data(),0x10,direction[1]);
        check(update(nullptr,nullptr,actor.data(),input.data())&&last_action==int(direction[2])&&times==0,"directional branch bypasses deadline for large and normal");
    }
    input.fill(0);reset();target_present=false;check(update(nullptr,nullptr,actor.data(),input.data())&&last_action==24,"missing victim chooses Special");
    reset();put(victim.data(),0x27c,0.f);check(update(nullptr,nullptr,actor.data(),input.data())&&last_action==24,"dead victim chooses Special");put(victim.data(),0x27c,100.f);
}
void lifecycle(){
    const auto ally=get<entry_t>(ptr(0x693318),4);
    const bool restored_ally=ally!=reinterpret_cast<entry_t>(0x4edbf0);
    check(restored_ally,"fix3 restores ally throw entry as part of shared victim lifecycle");
    std::array<unsigned char,0x80> context{};put(context.data(),0x64,0.375f);
    if(restored_ally){
        reset();ally_mode=true;put(victim.data(),4,0x80u);put<unsigned char>(victim.data(),0x764,0x80);put(actor.data(),0x178,0x3333u);
        ally(nullptr,nullptr,actor.data(),nullptr,context.data());
        check(published==std::vector<u32>{0x82,0x86,0x87}&&forced==1&&placements==1&&angle_sets==1,"ally throw publishes source flags then transitions and places victim");
        check(get<u32>(actor.data(),0x178)==0x3333&&get<u32>(victim.data(),0x178)==0x1111,"ally entry preserves acquisition tokens rather than inventing reciprocal owners");
        reset();target_present=false;ally(nullptr,nullptr,actor.data(),nullptr,context.data());check(starts==1&&started_on==actor.data()&&forced==0,"missing ally selects holder idle");
        reset();is_character=false;ally(nullptr,nullptr,actor.data(),nullptr,context.data());check(starts==1&&forced==0,"noncharacter ally selects idle");
    }
    const auto pickup=get<entry_t>(ptr(0x693438),4);
    const bool restored_pickup=pickup!=reinterpret_cast<entry_t>(0x4f0dd0);
    check(restored_pickup,"fix3 restores complete source pickup admission");
    if(restored_pickup){
        for(int marked=0;marked<2;++marked){reset();put<unsigned char>(victim.data(),0xc5,0x81);put<unsigned char>(victim.data(),0x30d,marked?0x84:0x80);put(victim.data(),0x178,0x3333u);put<unsigned char>(context.data(),0x6c,0x80);put<unsigned char>(victim.data(),0x259,0x10);put(victim.data(),0x23c,0u);put(victim.data(),0x240,0u);
            pickup(nullptr,nullptr,actor.data(),nullptr,context.data());
            check(get<u32>(victim.data(),0x23c)==0xbf800000&&get<u32>(victim.data(),0x240)==0xbf800000&&get<unsigned char>(victim.data(),0xc5)==0xc1&&get<unsigned char>(victim.data(),0x30d)==0x84,"marked/unmarked targets receive unconditional XML1 motion timer and marker setup");
            check(get<u32>(victim.data(),0x178)==0x3333&&get<u32>(actor.data(),0x76c)==0x2222&&get<unsigned char>(context.data(),0x6c)==0x80,"pickup preserves acquisition ownership/selection and context flags");
        }
        reset();target_present=false;pickup(nullptr,nullptr,actor.data(),nullptr,context.data());check(motions==0,"missing pickup target inert");
        reset();physical_class=false;pickup(nullptr,nullptr,actor.data(),nullptr,context.data());check(motions==0,"nonphysical pickup target inert");
    }
    for(const auto table:{0x693438u,0x69345cu})check(get<void*>(ptr(table),12)==ptr(0x59eb10),"both pickup cancellations are empty source callbacks");
    const auto timer_target=0x4f0b6eu+5+get<u32>(ptr(0x4f0b6e),1);
    check(timer_target!=0x471a20,"normal pickup entry also uses the unconditional XML1 timer setup");
    if(timer_target!=0x471a20){reset();put<unsigned char>(victim.data(),0x259,0x10);put(victim.data(),0x23c,0u);put(victim.data(),0x240,0u);
        reinterpret_cast<void(__fastcall*)(void*,void*,int,int)>(timer_target)(victim.data(),nullptr,1,0);
        check(get<u32>(victim.data(),0x23c)==0xbf800000&&get<u32>(victim.data(),0x240)==0xbf800000,"pickup timer setup ignores PC-only motion refusal flag");
    }
    const auto complete=get<cleanup_t>(ptr(0x6933a8),8);
    const bool restored_complete=complete!=reinterpret_cast<cleanup_t>(0x4f4680);
    check(restored_complete,"thrown-land completion always dispatches cleanup");
    void* table=ptr(0x6933a8);
    if(restored_complete)for(const auto owner:{0u,0x1111u,0x9999u}){reset();put(victim.data(),0x178,owner);put<unsigned char>(victim.data(),0x764,0xdf);put(victim.data(),0x434,3.f);put(victim.data(),0x770,0x3333u);
        complete(&table,nullptr,victim.data());check(get<unsigned char>(victim.data(),0x764)==0x1f&&get<u32>(victim.data(),0x434)==0&&get<u32>(victim.data(),0x770)==0&&get<u32>(victim.data(),0x178)==owner,"completion cleans for valid, stale and absent owners without clearing owner");
    }
    const bool restored_recovery=get<unsigned char>(ptr(0x4f49e9),0)==0xe9;
    check(restored_recovery,"thrown-land recovery bypasses PC owner gate");
    if(restored_recovery){
        auto update=reinterpret_cast<update_t>(0x4f49c0);std::array<unsigned char,0x40> input{};
        for(const auto owner:{0u,0x1111u,0x9999u})for(int button=0;button<3;++button){reset();put(victim.data(),0x178,owner);put(victim.data(),0x27c,100.f);put(victim.data(),0x5e8,10.f);put(victim.data(),0x434,3.f);put<void*>(victim.data(),0x3bc,nullptr);now=10.f;selected=move.data();button4=button==1;button5=button==2;
            const bool result=update(&table,nullptr,victim.data(),input.data());check(result==(button!=0)&&get<void*>(victim.data(),0x3bc)==(button?move.data():nullptr)&&get<u32>(victim.data(),0x178)==owner,"recovery follows attack/smash independent of owner and keeps owner token");
        }
        for(int delta=-1;delta<=1;++delta){reset();put(victim.data(),0x27c,100.f);put(victim.data(),0x5e8,10.f);put<void*>(victim.data(),0x3bc,move.data());selected=nullptr;now=10.f+float(delta);
            check(update(&table,nullptr,victim.data(),input.data())==(delta>0)&&get<void*>(victim.data(),0x3bc)==(delta>0?nullptr:move.data()),"recovery deadline is strict even when a valid owner exists");
        }
        reset();put(victim.data(),0x27c,0.f);button4=true;selected=move.data();check(!update(&table,nullptr,victim.data(),input.data())&&actions==0,"dead actor cannot recover");put(victim.data(),0x27c,100.f);
    }
}
void flying_collision(){
    const bool no_filter=get<unsigned char>(ptr(0x4ee87c),0)==0xe9;
    const bool acquisition=get<unsigned char>(ptr(0x4ee8b3),0)==0xe9;
    check(no_filter,"flying-charge nonactor Z filter removed");
    check(acquisition,"flying-charge live victim acquisition restored");
    if(!no_filter||!acquisition)return;
    auto collide=reinterpret_cast<entry_t>(0x4ee800);
    std::array<unsigned char,0x40> contact{};
    for(int kind=0;kind<7;++kind){
        reset();collision_mode=true;selected=move.data();class_info.fill(~0u);
        put(actor.data(),0x76c,0u);put(victim.data(),0x27c,100.f);
        void* struck=victim.data();void* record=contact.data();
        if(kind==1)put(victim.data(),0x27c,0.f);
        if(kind==2)put(actor.data(),0x76c,0x3333u);
        if(kind==3){class_info.fill(0);is_character=false;}
        if(kind==4)struck=nullptr;
        if(kind==5)record=nullptr;
        if(kind==6)selected=nullptr;
        collide(nullptr,nullptr,actor.data(),struck,record);
        const bool is_actor=kind!=3&&kind!=4;
        const bool acquired=kind==0||kind==5;
        check(tags==(is_actor?1:0)&&last_action==(is_actor?27:24),"collision preserves actor touch and object Special branches");
        check(locks==(acquired?1:0)&&starts==(kind==6?0:1),"acquire only live character with empty held slot and nonnull next move");
        check(contact_reads==0&&zero_velocities==1&&flight_offs==1,"nonactor/empty contacts always complete without consulting Z filter");
    }
    for(const auto health_bits:{0x80000000u,0xbf800000u,0x7fc00001u,0x3f800000u,0x7f800000u,1u}){
        reset();collision_mode=true;class_info.fill(~0u);selected=move.data();put(actor.data(),0x76c,0u);put(victim.data(),0x27c,health_bits);
        collide(nullptr,nullptr,actor.data(),victim.data(),contact.data());
        check(locks==((health_bits==0x3f800000u||health_bits==0x7f800000u||health_bits==1u)?1:0),"health predicate rejects negative/zero/NaN and accepts positive finite/infinity/subnormal");
    }
    class_info.fill(~0u);put(victim.data(),0x27c,100.f);
}
void lifecycle_boundaries(){
    const bool acquisition=get<unsigned char>(ptr(0x4de896),0)==0xe9;
    const bool reset_tail=get<unsigned char>(ptr(0x42c32f),0)==0xe9;
    check(acquisition,"review fix: native grab acquisition establishes complete XML1 constraints");
    check(reset_tail,"review fix: native actor reset restores marker-dependent source cleanup");
    if(acquisition){
        reset();acquiring=true;put(actor.data(),0x178,0u);put(victim.data(),0x178,0u);put(victim.data(),4,0x80u);put<unsigned char>(victim.data(),0x764,0x80);
        void* context_actor=actor.data();
        // The source/native sound path is outside this accepted-branch probe.
        const unsigned char finish[]{0x5f,0x5e,0x5b,0xc2,0x04,0x00};std::memcpy(ptr(0x4de89e),finish,sizeof finish);
        acquire_probe(&context_actor,nullptr,victim.data());
        check(get<u32>(actor.data(),0x178)==0x2222&&get<u32>(victim.data(),0x178)==0x1111,"real native acquisition establishes reciprocal owners from clear state");
        check(published==std::vector<u32>{0x82,0x86,0x87}&&get<unsigned char>(victim.data(),0x764)==0x90,"real acquisition publishes bits1/2/0 and marker from clear flags");
        acquiring=false;
    }
    if(reset_tail){
        native(0x42c340,0x45);jump(0x41d890,&stage_move);
        auto transition=reinterpret_cast<void(__fastcall*)(void*,void*,void*,int)>(0x42c340);
        for(int kind=0;kind<4;++kind){
            reset();default_victim_move=kind==1;put(victim.data(),0x378,move.data());put<void*>(victim.data(),0x384,nullptr);
            put<unsigned char>(victim.data(),0x764,kind==2?0x80:0x93);put(victim.data(),4,0x87u);put(victim.data(),0x5dc,123u);put<unsigned char>(victim.data(),0x3d8,8);
            transition(victim.data(),nullptr,idle.data(),kind==3?0:1);
            if(kind<2)check(published==std::vector<u32>{0x85,0x81,0x80}&&get<unsigned char>(victim.data(),0x764)==0xa0&&get<u32>(victim.data(),4)==0x80,"real reset cleans constraints for shared and default victim moves");
            else check(publishes==0&&get<u32>(victim.data(),4)==0x87,"unmarked actors and reset=false transitions retain unrelated flags");
            check(staged==1&&move_exits==(kind==3?0:1)&&relatives==0&&get<u32>(victim.data(),0x178)==0x1111,"real transition dispatch preserves owners without extra relative cleanup");
        }
        jump(0x42c340,&start);
    }
}
}
namespace lift_rule { bool fixture_installed=true;bool installed(){return fixture_installed;} int heaviness(void* p){return reinterpret_cast<unsigned char*>(p)[0x31d];} }
namespace anim_enums {
bool fixture_available=true;
const anim_enum_layout::descriptor* layout(){return fixture_available?&anim_enum_layout::raised:nullptr;}
int extension_id(const char* name){for(unsigned i=0;i<anim_enum_registry::builtin_names.size();++i)if(anim_enum_registry::builtin_names[i]==name)return int(anim_enum_layout::raised.builtin_first+i);return -1;}
}
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t* name){if(std::wstring(name)==L"GuardDecideRule")return guard_key;if(std::wstring(name)==L"Xml1GrabCarry")return key;if(std::wstring(name)==L"Xml1RogueDecide")return rogue_key;return cling_key;}
bool flag(const wchar_t*,const wchar_t* name,bool fallback){return std::wstring(name)==L"Xml1FlyingCarry"&&flying_carry_key?*flying_carry_key:fallback;}
}
namespace logger {void write(const char*,...){} }
namespace game_version {void refuse_startup(){refused=true;} }
namespace entity_capacity {
bool fixture_raised=false;
void* fixture_manager=reinterpret_cast<void*>(0x7f0000);
u32 installed_target(u32 va){if(!fixture_raised)return 0;for(unsigned i=0;i<xml1_rogue_decide_rules::query_entries.size();++i)if(xml1_rogue_decide_rules::query_entries[i].va==va)return 0x7f0100+i*16;return 0;}
const entity_capacity_layout::descriptor& installed(){static const auto raised=entity_capacity_layout::raised_descriptor_for(0,0,0x7f0000,0);return fixture_raised?raised:entity_capacity_layout::stock_descriptor;}
}
namespace entity_query {void* installed_manager(){return entity_capacity::fixture_raised?entity_capacity::fixture_manager:nullptr;} }

int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc<2){std::puts("owned retail executable argument required");return 1;}
    std::ifstream f(argv[1],std::ios::binary);retail_bytes.assign(std::istreambuf_iterator<char>(f),{});if(retail_bytes.size()<4096)return 1;
    DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"fixture writable");
    for(const auto& g:xml1_cling_rules::guards)signature(g.va,g.hex);
    for(const auto& g:xml1_rogue_decide_rules::guards)signature(g.va,g.hex);
    for(const auto& g:xml1_rogue_decide_rules::query_guards)signature(g.va,g.hex);
    for(const auto& e:xml1_rogue_decide_rules::query_entries)signature(e.va,e.retail);
    native(0x4eda40,0x1ab);native(0x4ed890,0x42);
    // Include the complete ret8 at 0x4f4abe (ends at 0x4f4ac1).
    native(0x4f49c0,0x101);native(0x4f4650,0x2c);
    native(0x4f0b68,11);
    native(0x4ecf30,0x4b);native(0x4ec820,0x17);native(0x4ee800,0xe5);native(0x4f74b2,39);
    native(0x4de880,0x1e);native(0x42c270,0xc7);native(0x59eb10,3);
    for(const auto table:{0x693318u,0x693438u,0x69345cu,0x6933a8u})native(table,32);
    for(const auto& g:guards)signature(g.va,g.hex);
    for(const auto& g:xml1_air_carry_rules::guards)signature(g.va,g.hex);
    const auto game=reinterpret_cast<HMODULE>(0x400000);
    std::vector<unsigned char> before(0x400000);std::memcpy(before.data(),ptr(0x401000),before.size());
    for(const auto* value:{"","xml2","off","0"}){key=value;xml1_grab_carry::install(game);check(!xml1_grab_carry::requested()&&xml1_grab_carry::verified(),"OFF unrequested and verified");check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"OFF entire4MiB byte-identical");}
    key="typo";xml1_grab_carry::install(game);check(xml1_grab_carry::requested()&&!xml1_grab_carry::verified(),"invalid key unverified");check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"invalid key changes nothing");
    key="xml1";anim_enums::fixture_available=false;xml1_grab_carry::install(game);check(!xml1_grab_carry::installed()&&std::memcmp(before.data(),ptr(0x401000),before.size())==0,"missing AnimEnums refuses entire rule with no writes");anim_enums::fixture_available=true;
    lift_rule::fixture_installed=false;xml1_grab_carry::install(game);check(!xml1_grab_carry::installed()&&std::memcmp(before.data(),ptr(0x401000),before.size())==0,"missing XML1 object lift policy refuses all rule45 writes");lift_rule::fixture_installed=true;
    xml1_grab_carry::install(nullptr);check(!xml1_grab_carry::installed(),"wrong base refused");
    for(const auto& g:guards){const auto saved=get<unsigned char>(ptr(g.va),0);put<unsigned char>(ptr(g.va),0,saved^0xff);xml1_grab_carry::install(game);check(!xml1_grab_carry::installed(),"each service guard refusal");put<unsigned char>(ptr(g.va),0,saved);check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"guard refusal writes no image bytes");}
    put<unsigned char>(ptr(0x4fd860),0,0xcc);xml1_grab_carry::install(game);check(!xml1_grab_carry::installed()&&!refused,"foreign lookup refuses and rolls back ground patches");put<unsigned char>(ptr(0x4fd860),0,before[0x4fd860-0x401000]);check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"lookup failure whole image restored");
    for(const auto& g:xml1_air_carry_rules::guards){const auto saved=get<unsigned char>(ptr(g.va),0);put<unsigned char>(ptr(g.va),0,saved^0xff);xml1_grab_carry::install(game);check(!xml1_grab_carry::installed(),"each flight/carry service guard refusal");put<unsigned char>(ptr(g.va),0,saved);check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"flight/carry refusal writes nothing");}
    const bool both=argc>2&&std::string(argv[2])=="both";
    if(argc>2&&std::string(argv[2])=="guard-off")guard_key="xml2";
    if(both){cling_key=rogue_key="xml1";xml1_cling::install(game);xml1_rogue_decide::install(game);check(xml1_cling::installed()&&xml1_rogue_decide::installed(),"cling and Rogue installed before grab");}
    const bool raised=argc>2&&std::string(argv[2])=="raised";
    if(raised){
        entity_capacity::fixture_raised=true;
        for(const auto& e:xml1_rogue_decide_rules::query_entries){const u32 destination=entity_capacity::installed_target(e.va);jump(e.va,ptr(destination));put<unsigned char>(ptr(destination),0,0xc3);}
        // Invented published query-manager getter, no retail layout assumptions.
        const u32 getter=entity_capacity::installed_target(0x4ab550);put<unsigned char>(ptr(getter),0,0xb8);put(ptr(getter),1,0x7f0000u);put<unsigned char>(ptr(getter),5,0xc3);
        auto saved=get<u32>(ptr(0x4ab6d0),1);put(ptr(0x4ab6d0),1,saved+1);xml1_grab_carry::install(game);check(!xml1_grab_carry::installed(),"raised query bridge must match exact published owner target");put(ptr(0x4ab6d0),1,saved);
        entity_capacity::fixture_manager=nullptr;xml1_grab_carry::install(game);check(!xml1_grab_carry::installed(),"raised carry requires published query manager");entity_capacity::fixture_manager=ptr(0x7f0000);
    }
    // [Game] Xml1FlyingCarry=0 (beta 8 080h): everything but the flying carry's handlers and descent admission.
    const bool nocarry=argc>2&&std::string(argv[2])=="nocarry";
    if(nocarry)flying_carry_key=false;
    std::vector<unsigned char> pre(0x400000);std::memcpy(pre.data(),ptr(0x401000),pre.size());
    auto changed=[&](u32 va,std::size_t n){return std::memcmp(pre.data()+(va-0x401000),ptr(va),n)!=0;};
    xml1_grab_carry::install(game);check(xml1_grab_carry::installed()&&!refused,"ground/carry installation succeeded with stock or published raised query bridges");
    check(xml1_grab_carry::flying_carry()==!nocarry&&xml1_grab_carry::feature_mask()==(nocarry?23u:15u),"Xml1FlyingCarry selects mask 15 (carry) or 23 (no carry)");
    check(xml1_grab_carry::status().find(nocarry?"flying carry off":"flying carry + flight upkeep")!=std::string::npos,"status names the flying carry state");
    check(changed(0x433e6c,6)!=nocarry,"descent admission 0x433e6c written only with the flying carry");
    check(changed(0x6931f8+4,4)&&changed(0x6932d0+4,4)&&changed(0x4ec7a0,5)&&changed(0x4f74b2,5),"fix7 entries, upkeep and %flight_pwr filter written either way");
    check(changed(timeout_va,5)&&changed(0x4f49e9,7)&&changed(0x4ee8b3,10)&&changed(0x4de896,8)&&changed(0x42c32f,6)&&changed(world_hit_gate,2)&&changed(hold_vtable+8,8)&&changed(victim_vtable+4,16),"ground grab, recovery, charge collision and world impact written either way");
    auto lookup=reinterpret_cast<void*(__cdecl*)(const char*)>(0x4fd860);void* handler=lookup("CH_GRAB_ATTACK");check(handler!=nullptr,"case-insensitive attack lookup");
    const unsigned char fallback[]{0x33,0xc0,0x5e,0xc3};std::memcpy(ptr(0x4fd865),fallback,sizeof fallback);
    check((lookup("ch_roguedecide")!=nullptr)==both&&(lookup("ch_clingwalldecide")!=nullptr)==both,"all enabled lookup resolvers coexist");
    for(const char* name:{"ch_fly_guard_decide","ch_air_grab_pickup","ch_air_grab_pickup_idle","ch_air_grab_pickup_throw","ch_air_grab_victim"})check((lookup(name)!=nullptr)==!nocarry,"the five carry handlers registered only with the flying carry");
    check(lookup("unknown")==nullptr,"unknown names fall through");
    bind();callbacks(handler);fastball_victim_hold_test();hold_branches();lifecycle();flying_collision();lifecycle_boundaries();
    thrown_body_fixture::run([](u32 a,std::size_t n){native(a,n);},[](bool b,const char* s){check(b,s);});
    std::printf("%s real grab installer/callbacks (%s): %d checks, %d failures\n",failures?"FAIL":"PASS",both?"cling+Rogue+grab":nocarry?"grab only, Xml1FlyingCarry=0":"grab only",checks,failures);return failures?1:0;
}

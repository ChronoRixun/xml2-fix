#include "xml1_air_carry.hpp"
#include "xml1_air_carry_rules.hpp"
#include "xml1_grab_carry_rules.hpp"
#include "xml1_rogue_decide_rules.hpp"
#include "anim_enums.hpp"
#include "anim_enum_names.hpp"
#include "entity_capacity.hpp"
#include "entity_query.hpp"
#include "det_math.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "lift_rule.hpp"
#include <array>
#include <cstring>

namespace xml1_air_carry {
namespace {
using u32=std::uint32_t;
struct vec { float x,y,z; };
struct pose { vec position,angles; };
template<class T>T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>void put(void* p,u32 off,T v){std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v);}
template<class T>T fn(u32 va){return reinterpret_cast<T>(va);}
template<class T>T slot(const void* p,u32 off){return at<T>(at<const void*>(p,0),off);}
using unary=void(__fastcall*)(void*,void*);
using pointer=void*(__fastcall*)(void*,void*);
using boolean=bool(__fastcall*)(void*,void*);
using getter=void*(__cdecl*)();
using vector_set=void(__fastcall*)(void*,void*,const vec*);
using update=bool(__fastcall*)(void*,void*,void*,void*);
using entry=void(__fastcall*)(void*,void*,void*,void*,void*);
using action_t=void*(__fastcall*)(void*,void*,int);
using handle_set=void(__fastcall*)(void*,void*,u32);
using start_t=void(__fastcall*)(void*,void*,void*,int);
using forced=void(__fastcall*)(void*,void*,void*,int,float,u32,u32,u32);
std::array<std::array<void*,8>,5> tables{};
std::array<void*,5> handlers{};
constexpr std::array<const char*,5> names{{"ch_fly_guard_decide","ch_air_grab_pickup","ch_air_grab_pickup_idle","ch_air_grab_pickup_throw","ch_air_grab_victim"}};
std::array<int,5> attachment_ids{};
void* character(void* p){return fn<void*(__cdecl*)(void*)>(0x402b60)(p);}
void* resolve(void* p,u32 off){const u32 h=at<u32>(p,off);return fn<void*(__fastcall*)(const u32*,void*)>(0x4654b0)(&h,nullptr);}
void* named(void* actor,const char* name){u32 key=0;fn<void*(__fastcall*)(u32*,void*,const char*)>(0x41b120)(&key,nullptr,name);return fn<void*(__fastcall*)(void*,void*,const u32*,void*)>(0x501a50)(fn<pointer>(0x41d0d0)(actor,nullptr),nullptr,&key,nullptr);}
void idle(void* actor){fn<start_t>(0x42c340)(actor,nullptr,named(actor,"idle"),1);}
void publish(void* actor){fn<unary>(0x45fe40)(actor,nullptr);}
void bits(void* actor,bool on){for(u32 bit:{2u,4u,1u}){const u32 f=at<u32>(actor,4);put(actor,4,on?f|bit:f&~bit);publish(actor);}}
void mark(void* actor){put<unsigned char>(actor,0x764,at<unsigned char>(actor,0x764)|0x10);}
bool alive(void* p){return at<float>(p,0x27c)>0.f;}
bool ai(void* p){return (at<unsigned char>(p,0x3d8)&1)!=0;}
bool flying(void* p){return (at<unsigned char>(p,0x3d8)&4)!=0;}
bool grounded(void* p){return at<u32>(p,0xbc)!=0||(at<u32>(p,4)&4)!=0;}
int team(void* p){return fn<int(__fastcall*)(void*,void*)>(0x41a220)(p,nullptr);}
void flight(void* p,bool on,int emit=1){fn<void(__fastcall*)(void*,void*,int,int)>(0x41f7d0)(p,nullptr,on,emit);}
int rank(void* actor){void* stats=at<void*>(actor,0x35c);return stats?fn<unsigned char(__fastcall*)(void*,void*,const void*,void*,void*)>(0x43b180)(static_cast<unsigned char*>(stats)+4,nullptr,reinterpret_cast<const void*>(0x711c28),actor,nullptr):0;}
bool mode(u32 va){return fn<bool(__cdecl*)()>(va)();}
bool manager_flag(u32 off){void* p=fn<getter>(0x484990)();return slot<boolean>(p,off)(p,nullptr);}
void chain(void* actor,int action){put(actor,0x3bc,fn<action_t>(0x427c80)(actor,nullptr,action));}
void lock(void* actor,void* target){fn<handle_set>(0x42d8e0)(actor,nullptr,at<u32>(target,0x1c));}
vec subtract(vec a,vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
vec blend(vec from,vec to,float amount){return {from.x+(to.x-from.x)*amount,from.y+(to.y-from.y)*amount,from.z+(to.z-from.z)*amount};}
// PC 568E90's (1,1) branch supplies fixed approximations (bone0=root+25),
// not the animated skeleton used by XML1 12CAD0. Both added flags must be off.
bool bone(void* actor,int id,vec& out){return fn<bool(__fastcall*)(void*,void*,int,vec*,bool,bool)>(0x41d620)(actor,nullptr,id,&out,false,false);}
void face(void* actor,void* victim){vec direction=subtract(at<vec>(victim,0x20),at<vec>(actor,0x20)),angles{};const vec* result=fn<vec*(__cdecl*)(vec*,const vec*)>(0x5607b0)(&angles,&direction);slot<vector_set>(actor,0x2c)(actor,nullptr,result);}
// Native hold entry's sound contract: pickup field at sound-set+0x14.
void pickup_sound(void* actor){void* sounds=fn<void*(__cdecl*)(int)>(0x438cb0)(at<int>(actor,0x444));const u32 sound=at<u32>(sounds,0x14);if(sound==*reinterpret_cast<const u32*>(0x69d058))return;void* manager=fn<getter>(0x477310)();if(slot<bool(__fastcall*)(void*,void*,u32)>(manager,0)(manager,nullptr,sound))slot<void(__fastcall*)(void*,void*,u32)>(actor,0x8c)(actor,nullptr,sound);}
void notify_text(void* actor,const char* text){void* ui=fn<getter>(0x59ee20)();const u32 handle=at<u32>(actor,0x1c);slot<void(__fastcall*)(void*,void*,const u32*,const char*)>(ui,0x40)(ui,nullptr,&handle,text);}
const char* message(int id){void* p=fn<getter>(0x4bdb30)();return slot<const char*(__fastcall*)(void*,void*,int)>(p,8)(p,nullptr,id);}
void cannot_lift(void* actor){if(ai(actor))return;void* ui=fn<getter>(0x59ee20)();const std::array<float,4> color{0.8f,0.8f,0.8f,1.f};using show=u32(__fastcall*)(void*,void*,u32,const char*,float,std::array<float,4>,int);const u32 previous=*reinterpret_cast<const u32*>(0x789ae0);*reinterpret_cast<u32*>(0x789ae0)=slot<show>(ui,0x34)(ui,nullptr,previous,message(0x89),0.75f,color,0);fn<unary>(0x421080)(actor,nullptr);}
void detach(void* victim){bits(victim,false);slot<void(__fastcall*)(void*,void*,void*)>(victim,0x138)(victim,nullptr,resolve(victim,0x178));}
void __fastcall victim_cleanup(void*,void*,void* victim){if(!victim)return;detach(victim);if(at<unsigned char>(victim,0x3db)&4){fn<void(__fastcall*)(void*,void*,bool)>(0x41d020)(victim,nullptr,false);fn<void(__fastcall*)(void*,void*,bool)>(0x41d060)(victim,nullptr,false);}}
void __fastcall throw_cleanup(void*,void*,void* actor){put(actor,4,at<u32>(actor,4)&~4u);publish(actor);}
void __fastcall interrupted(void*,void*,void* actor){fn<unary>(0x431e00)(actor,nullptr);}
void __fastcall frame_noop(void*,void*,void*,void*){} // PC slot4 must ret8, unlike slot2/3 ret4.
void* __fastcall destructor(void* self,void*,unsigned){return self;}

// PC retains this drain on flying melee/air-pain nodes as well as flying.
// Suppress only the engine-keyed flight upkeep expression while parsing it;
// other per-second powers keep their parser and debit. Rule45 owns the entire
// branch transaction, so OFF or a refused install cannot suppress any drain.
bool __cdecl flight_powerup(const char* value){return value&&_stricmp(value,"%flight_pwr")==0;}
auto flight_powerup_function=&flight_powerup;
u32 energy_parse_resume=0x4f74b7;
__declspec(naked) void flight_powerup_bridge(){
    __asm {
        mov eax, [esp + 18h]
        pushfd
        pushad
        push eax
        call dword ptr [flight_powerup_function]
        add esp, 4
        test al, al
        jz native_value
        popad
        popfd
        and byte ptr [esi + 146h], 0bfh
        pop edi
        pop esi
        pop ebp
        mov al, 1
        pop ebx
        ret 8
    native_value:
        popad
        popfd
        mov ecx, [esp + 18h]
        push ecx
        jmp dword ptr [energy_parse_resume]
    }
}

// XML1 0xdb0e0. One debit at a strict >1s boundary, never catch up.
// Converted flight_pwr values are inert talentvalues once the builder replaces
// PC's energypersecond move with XML1's (XML1_FLIGHT_UPKEEP=True).
void __cdecl upkeep(void* actor){
    if(!actor)return;
    det_math::fp_env fp;
    const int r=rank(actor);
    if(r>5)return;
    constexpr float costs[]{40.f,40.f,30.f,20.f,10.f,5.f};
    if(r<=0||fn<float(__fastcall*)(void*,void*)>(0x41d5d0)(actor,nullptr)<costs[r]){flight(actor,false);idle(actor);return;}
    if(!(double(fn<float(__cdecl*)()>(0x45fe60)())>double(at<float>(actor,0x6a4))+1.0))return;
    put(actor,0x6a4,fn<float(__cdecl*)()>(0x45fe60)());
    if(!ai(actor)){
        float cost=costs[r];if(mode(0x4c87c0)&&fn<int(__cdecl*)()>(0x4c8820)()==2)cost=120.f;
        fn<void(__fastcall*)(void*,void*,float)>(0x4318d0)(actor,nullptr,cost);
    }
    put(actor,0x6a0,fn<float(__cdecl*)()>(0x45fe60)());
}
// XML1 0xdc040; the reference angles are actor+0x1f4, not its live angles.
vec base_position(void* carrier){vec p{};if(!bone(carrier,0,p)){p=at<vec>(carrier,0x20);p.z+=38.f;}p.z-=55.f;return p;}
vec hands(void* carrier,void* victim){
    // Source leaves failed bone outputs unspecified. Seed them to each actor's
    // position so a missing pair has a stable position fallback.
    vec c=at<vec>(carrier,0x20),c5=c,v=at<vec>(victim,0x20),v5=v;
    if(bone(carrier,6,c)&&bone(carrier,5,c5))c=blend(c,c5,0.5f);
    if(bone(victim,6,v)&&bone(victim,5,v5))v=blend(v,v5,0.5f);
    const vec offset=subtract(at<vec>(victim,0x20),v);return {c.x+offset.x,c.y+offset.y,c.z+offset.z};
}
void attachment(void* carrier,void* victim){
    if(!alive(victim)){idle(carrier);return;}
    void* move=at<void*>(victim,0x378);if(!move)return;
    const int id=slot<int(__fastcall*)(void*,void*)>(move,0xe8)(move,nullptr);
    const bool hand=id==attachment_ids[0]||id==attachment_ids[1]||id==attachment_ids[2]||id==attachment_ids[3];
    vec desired=hand?hands(carrier,victim):base_position(carrier);
    if(id==attachment_ids[3]||id==attachment_ids[4])desired=blend(at<vec>(victim,0x20),desired,0.1f);
    const pose delta{subtract(desired,at<vec>(victim,0x20)),subtract(at<vec>(carrier,0x1f4),at<vec>(victim,0x2c))};
    slot<float(__fastcall*)(void*,void*,const pose*)>(victim,0x130)(victim,nullptr,&delta);
}
void __fastcall decide_entry(void*,void*,void*,void*,void* context){if(context)put<unsigned char>(context,0x6c,at<unsigned char>(context,0x6c)|1);}
bool __fastcall decide_update(void* self,void*,void* actor,void* input){
    if(!actor||!input)return true;
    det_math::fp_env fp;
    if(rank(actor)<=2)return true;
    if(mode(0x4c87a0)&&(mode(0x4c87c0)||mode(0x4c87b0)))return true;
    struct query_holder{void* q=nullptr;~query_holder(){if(q)fn<void(__fastcall*)(void**,void*)>(0x4ab720)(&q,nullptr);}} holder;
    void* entity=nullptr;
    if(fn<int(__cdecl*)()>(0x4ab740)()>0){
        const u32 excluded=at<u32>(actor,0x1c);
        fn<void**(__fastcall*)(void**,void*,const void*,const u32*)>(0x4ab6d0)(&holder.q,nullptr,reinterpret_cast<const void*>(0x70b83c),&excluded);
        if(holder.q){
            void* q=holder.q;
            slot<void(__fastcall*)(void*,void*,int,int)>(q,0x40)(q,nullptr,1,1);
            slot<void(__fastcall*)(void*,void*,int,int)>(q,0x40)(q,nullptr,1,4);
            vec center{},direction{};slot<vec*(__fastcall*)(void*,void*,vec*)>(actor,0x24)(actor,nullptr,&center);
            fn<vec*(__cdecl*)(vec*,const void*)>(0x560990)(&direction,static_cast<unsigned char*>(actor)+0x2c);
            const vec end{center.x+48.f*direction.x,center.y+48.f*direction.y,center.z-48.f},box{16.f,16.f,16.f};
            slot<void(__fastcall*)(void*,void*,int)>(q,0x80)(q,nullptr,0);
            slot<void(__fastcall*)(void*,void*,const vec*,const vec*,const vec*)>(q,0x74)(q,nullptr,&center,&end,&box);
            void* contact=nullptr;slot<void**(__fastcall*)(void*,void*,void**)>(q,0x20)(q,nullptr,&contact);
            entity=fn<pointer>(0x4a9590)(&contact,nullptr);
            entity=fn<void*(__cdecl*)(void*)>(0x429110)(entity);
        }
    }
    if(!entity||(at<u32>(entity,4)&0x40000)||manager_flag(0x3c)){
        if(!fn<update>(0x4fbe80)(self,nullptr,actor,input))chain(actor,0);
        return true;
    }
    if(void* victim=character(entity)){
        if(!fn<boolean>(0x429210)(victim,nullptr)){chain(actor,0);return true;}
        if(team(actor)==team(victim)){chain(actor,3);lock(actor,victim);}
        else if(team(victim)==0x1c){void* manager=fn<getter>(0x5252e0)();if(slot<bool(__fastcall*)(void*,void*,u32)>(manager,0x78)(manager,nullptr,at<u32>(victim,0x7dc))){chain(actor,3);lock(actor,victim);}}
        else{
            chain(actor,3);
            if(at<unsigned char>(victim,0x765)&1){
                if(fn<bool(__fastcall*)(void*,void*,int,void*)>(0x54d710)(victim,nullptr,44,nullptr)){
                    // Source 0x94a70(nullptr,def_pickup,nullptr) yields the empty
                    // feedback string; retain its owner-relative notification.
                    notify_text(actor,"");
                }else chain(actor,25);
            }
            lock(actor,victim);
        }
    }else if(fn<bool(__fastcall*)(void*,void*,void*)>(0x427f60)(actor,nullptr,entity)){lock(actor,entity);chain(actor,24);}
    else{cannot_lift(actor);chain(actor,0);}
    return true;
}
void __fastcall pickup_entry(void*,void*,void* actor,void*,void* context){
    if(!actor)return;det_math::fp_env fp;
    void* victim=character(resolve(actor,0x76c));if(!victim){idle(actor);return;}
    mark(victim);put(victim,4,at<u32>(victim,4)|4u);publish(victim);
    void* move=named(victim,"airgrabpickupvictim");
    fn<handle_set>(0x42c7c0)(actor,nullptr,at<u32>(actor,0x76c));face(actor,victim);
    fn<forced>(0x432030)(victim,nullptr,move,-1,context?at<float>(context,0x64):-1.f,0,1,0);
    mark(victim);pickup_sound(actor);
}
void __fastcall pickup_frame(void*,void*,void* actor,void*){
    if(!actor)return;det_math::fp_env fp;
    void* victim=character(resolve(actor,0x76c));if(!victim)return;
    const vec original=at<vec>(victim,0x20);vec target{};if(!bone(actor,6,target))target=at<vec>(actor,0x20);
    vec position=blend(original,target,0.01f);position.z=original.z;slot<vector_set>(victim,0x1c)(victim,nullptr,&position);
    vec angles=at<vec>(actor,0x2c);angles.z+=3.14f;slot<vector_set>(victim,0x2c)(victim,nullptr,&angles);
}
void __fastcall carry_entry(void*,void*,void* actor,void*,void*){
    if(!actor)return;det_math::fp_env fp;
    void* victim=character(resolve(actor,0x76c));if(!victim||!alive(victim)){idle(actor);return;}
    bits(victim,true);const vec position=base_position(actor),angles=at<vec>(actor,0x2c);
    slot<void(__fastcall*)(void*,void*,const vec*,const vec*)>(victim,0x158)(victim,nullptr,&position,&angles);
}
bool __fastcall carry_update(void* self,void*,void* actor,void* input){
    if(!actor||!input)return false;det_math::fp_env fp;
    fn<update>(0x4fce50)(self,nullptr,actor,input);
    const float speed=fn<float(__fastcall*)(void*,void*,int)>(0x425b70)(actor,nullptr,2)*at<float>(input,8);
    const u32 buttons=at<u32>(input,0);int action;
    if(buttons&(1u<<4))action=1;
    else if(buttons&(1u<<5))action=2;
    else if(fn<float(__fastcall*)(void*,void*,int)>(0x425b70)(actor,nullptr,8)>=speed)action=0;
    else if(fn<float(__fastcall*)(void*,void*,int)>(0x425b70)(actor,nullptr,9)>=speed)action=9;
    else action=11;
    void* next=fn<action_t>(0x427c80)(actor,nullptr,action);if(!next)return false;
    // XML1 DC0B0 selects the idle/walk/run chain on every input callback.
    // On PC, reporting the current node as a new chain sends it through
    // 4341E8 -> 4315D0 again, restarting entry and its time-zero victim event.
    // Keep the established node running so its frame attachment can advance.
    if(next==at<void*>(actor,0x378))return false;
    put(actor,0x3bc,next);return true;
}
void __fastcall carry_frame(void*,void*,void* actor,void*){
    if(!actor)return;det_math::fp_env fp;
    void* victim=character(resolve(actor,0x76c));if(!victim)return;
    mark(victim);
    // PC can reset the victim after the new carrier node's entry/trigger.
    // Reassert the source pair constraints before movement as well as in the
    // victim callback; actor callback order must not open a physics frame.
    if(character(resolve(victim,0x178))==actor&&flying(actor)&&!grounded(actor))bits(victim,true);
    attachment(actor,victim);upkeep(actor);
}
bool friendly_damage(){if(*reinterpret_cast<const unsigned char*>(0x70b6fd))return true;void* s=fn<getter>(0x48de40)();return slot<boolean>(s,0x5c)(s,nullptr);}
void __fastcall throw_entry(void*,void*,void* actor,void*,void* context){
    if(!actor)return;det_math::fp_env fp;
    void* victim=character(resolve(actor,0x76c));if(!victim){idle(actor);return;}
    fn<handle_set>(0x42c7c0)(actor,nullptr,at<u32>(actor,0x76c));face(actor,victim);
    const char* name="thrownupsidedown";
    if(team(victim)==team(actor)&&!friendly_damage()){
        void* stats=at<void*>(victim,0x35c);name=stats&&(at<unsigned char>(stats,0x2ad)&0x80)?"fastball":"jumploop";
    }else if(team(victim)==0x1c)name="jumploop";
    fn<forced>(0x432030)(victim,nullptr,named(victim,name),-1,context?at<float>(context,0x64):-1.f,0,1,0);
    mark(victim);put(victim,4,at<u32>(victim,4)|4u);publish(victim);put(victim,0x770,at<u32>(actor,0x1c));
}
void __fastcall victim_frame(void*,void*,void* victim,void*){
    if(!victim)return;
    void* carrier=character(resolve(victim,0x178));
    if(carrier&&character(resolve(carrier,0x76c))==victim&&flying(carrier)&&!grounded(carrier)){bits(victim,true);return;}
    if(carrier){const u32 none=*reinterpret_cast<const u32*>(0x72988c);for(void* p:{carrier,victim})slot<void(__fastcall*)(void*,void*,u32,float)>(p,0x104)(p,nullptr,none,0.6f);}
    detach(victim);if(carrier)idle(carrier);idle(victim);
}
// Two source test points, deliberately separate calls to the documented
// default-true emulation; map-state getter is vt+0x8c (0x483de0), never +0x44.
void __fastcall flying_entry(void*,void*,void* actor,void* input,void* context){
    if(!actor)return;det_math::fp_env fp;
    if(manager_flag(0x8c)&&xml1_grab_carry_rules::xml1_flight_eligible(actor)){if(!ai(actor))notify_text(actor,message(0x8a));idle(actor);return;}
    if(!flying(actor))slot<unary>(actor,0x1e0)(actor,nullptr);
    void* move=at<void*>(actor,0x378);
    if(move&&slot<unsigned char(__fastcall*)(void*,void*)>(move,0x58)(move,nullptr)==1&&input&&context){const float speed=at<float>(input,8);put<int>(context,0,speed>0.6f?0x13:speed>0.2f?0x12:0x11);}
    flight(actor,true);
}
void __fastcall fly_move_entry(void*,void*,void* actor,void*,void*){
    if(!actor)return;
    if(manager_flag(0x8c)&&xml1_grab_carry_rules::xml1_flight_eligible(actor)){if(!ai(actor))notify_text(actor,message(0x8a));idle(actor);return;}
    if(!flying(actor))slot<unary>(actor,0x1e0)(actor,nullptr);flight(actor,true);
}
// XML1 0x43312..0x43467, inside flight descent. The native PC branch
// 0x433e46..0x433e6a already requires Guard (0x100), no power modifier
// (0x80), and no native controller refusal. Keep those instructions intact.
bool __cdecl descend_carry(void* actor){
    det_math::fp_env fp;
    // Protect pickup's time-zero stop/interpolation from PC's general descent
    // tail once contact has established the pair. Established carry retains
    // the source probe/descent policy, including descending through empty air.
    void* current=at<void*>(actor,0x378);
    if(current){
        void* handler=slot<pointer>(current,0x30)(current,nullptr);
        if(handler==&handlers[1]){
            void* victim=character(resolve(actor,0x76c));
            if(victim&&alive(victim)&&character(resolve(victim,0x178))==actor&&flying(actor)&&!grounded(actor))return true;
        }
    }
    const vec start=at<vec>(actor,0x20),delta{0.f,0.f,-20.f};vec shift{};
    void* contact=nullptr;
    using probe=bool(__fastcall*)(void*,void*,const vec*,const vec*,int,vec*,void**);
    const bool hit=fn<probe>(0x42b6c0)(actor,nullptr,&start,&delta,1,&shift,&contact);
    void* victim=character(fn<pointer>(0x4a9590)(&contact,nullptr));
    if(!hit||!victim)return false;
    const int player=at<int>(actor,0x3d4);
    if(player<0||player>=4||at<u32>(reinterpret_cast<const void*>(0x70b814),4u*player)!=at<u32>(actor,0x1c))return false;
    void* stats=at<void*>(victim,0x35c);
    if(!stats||(at<unsigned char>(stats,0x2ad)&8))return false;
    // XML1's valid contact consumes this descent step even when rank or
    // current move prevents pickup (0x43418/0x43430 -> 0x4348d).
    if(rank(actor)<=2)return true;
    void* move=at<void*>(actor,0x378);if(!move)return true;
    u32 flying_key=0;fn<void*(__fastcall*)(u32*,void*,const char*)>(0x41b120)(&flying_key,nullptr,"flying");
    const auto* key=slot<const u32*(__fastcall*)(void*,void*)>(move,0xd0)(move,nullptr);
    if(!key||*key!=flying_key)return true;
    void* next=named(actor,"flyguarddecide");
    fn<forced>(0x432030)(actor,nullptr,next,-1,-1.f,0,0,0);
    flight(actor,true,0);
    return true;
}
auto descend_carry_function=&descend_carry;
u32 descent_resume=0x433e72,descent_done=0x433ea5;
__declspec(naked) void descent_bridge(){
    __asm {
        pushfd
        pushad
        push edi
        call dword ptr [descend_carry_function]
        add esp,4
        test al,al
        jz native_descent
        popad
        popfd
        jmp dword ptr [descent_done]
    native_descent:
        popad
        popfd
        push 2
        lea ecx,[esp+1ch]
        jmp dword ptr [descent_resume]
    }
}
}

bool prepare(std::vector<xml1_hook_transaction::patch>& writes,bool flying_carry){
    const auto* layout=anim_enums::layout();
    if(!layout||layout->builtin_count!=anim_enum_registry::builtin_names.size()||!lift_rule::installed())return false;
    // Validate every published builtin against the descriptor; attachment IDs
    // below come exclusively from extension_id(), never hard-coded PC numbers.
    for(std::size_t i=0;i<anim_enum_registry::builtin_names.size();++i)
        if(anim_enums::extension_id(anim_enum_registry::builtin_names[i].data())!=int(layout->builtin_first+i))return false;
    constexpr const char* attachments[]{"ea_flyingcarry_flyfast_v","ea_flyingcarry_flyslow_v","ea_flyingcarry_idle_v","ea_flyingcarry_pickup_v","ea_flyingcarry_pickup_a"};
    for(unsigned i=0;i<attachment_ids.size();++i)attachment_ids[i]=anim_enums::extension_id(attachments[i]);
    xml1_hook_transaction::memory memory;
    for(const auto& g:xml1_air_carry_rules::guards){std::vector<unsigned char> b(g.hex.size()/2);if(!memory.read(g.va,b.data(),b.size())||!limits_rules::matches(b.data(),g.hex)){logger::write("air carry: unsupported service %08X (%s)",g.va,g.what);return false;}}
    const bool raised=entity_capacity::installed().raised;
    for(const auto& e:xml1_rogue_decide_rules::query_entries){std::array<unsigned char,24>b{};if(!memory.read(e.va,b.data(),e.retail.size()/2)||!mold_query_rules::entry_ok(raised,b.data(),e,raised?entity_capacity::installed_target(e.va):0))return false;}
    if(raised&&(!entity_query::installed_manager()||fn<getter>(0x4ab550)()!=entity_query::installed_manager()))return false;
    if(!raised)for(const auto& g:xml1_rogue_decide_rules::query_guards){std::vector<unsigned char>b(g.hex.size()/2);if(!memory.read(g.va,b.data(),b.size())||!limits_rules::matches(b.data(),g.hex))return false;}
    handlers.fill(nullptr);
    if(flying_carry){
    for(unsigned i=0;i<tables.size();++i){std::memcpy(tables[i].data(),reinterpret_cast<const void*>(0x692f4c),32);tables[i][0]=reinterpret_cast<void*>(&destructor);tables[i][4]=reinterpret_cast<void*>(&frame_noop);handlers[i]=tables[i].data();}
    tables[0][1]=reinterpret_cast<void*>(&decide_entry);tables[0][6]=reinterpret_cast<void*>(&decide_update);
    tables[1][1]=reinterpret_cast<void*>(&pickup_entry);tables[1][4]=reinterpret_cast<void*>(&pickup_frame);
    tables[2][1]=reinterpret_cast<void*>(&carry_entry);tables[2][3]=reinterpret_cast<void*>(&interrupted);tables[2][4]=reinterpret_cast<void*>(&carry_frame);tables[2][6]=reinterpret_cast<void*>(&carry_update);
    tables[3][1]=reinterpret_cast<void*>(&throw_entry);tables[3][2]=tables[3][3]=reinterpret_cast<void*>(&throw_cleanup);
    tables[4][3]=reinterpret_cast<void*>(&victim_cleanup);tables[4][4]=reinterpret_cast<void*>(&victim_frame);
    }
    auto pointer_patch=[&](u32 va,const void* p){const auto* old=reinterpret_cast<const unsigned char*>(va);writes.push_back({va,{old,old+4},xml1_hook_transaction::pointer_bytes(p)});};
    pointer_patch(0x6931f8+4,reinterpret_cast<void*>(&flying_entry));
    pointer_patch(0x6932d0+4,reinterpret_cast<void*>(&fly_move_entry));
    const u32 va=0x4ec7a0;std::vector<unsigned char> after(5,0xe9);const u32 relative=u32(reinterpret_cast<std::uintptr_t>(&upkeep))-va-5;std::memcpy(after.data()+1,&relative,4);
    const auto* old=reinterpret_cast<const unsigned char*>(va);writes.push_back({va,{old,old+5},after});
    const u32 parser=0x4f74b2;const auto* before=reinterpret_cast<const unsigned char*>(parser);
    std::vector<unsigned char> filter(5,0xe9);const u32 target=u32(reinterpret_cast<std::uintptr_t>(&flight_powerup_bridge))-parser-5;std::memcpy(filter.data()+1,&target,4);
    writes.push_back({parser,{before,before+5},filter});
    if(!flying_carry)return true; // no carry admission: PC's own flight descent at 0x433e6c
    const u32 descent=0x433e6c;const auto* original=reinterpret_cast<const unsigned char*>(descent);
    std::vector<unsigned char> bridge(6,0x90);bridge[0]=0xe9;const u32 offset=u32(reinterpret_cast<std::uintptr_t>(&descent_bridge))-descent-5;std::memcpy(bridge.data()+1,&offset,4);
    writes.push_back({descent,{original,original+6},bridge});
    return true;
}
void abandon(){handlers.fill(nullptr);}
void* lookup(const char* name){if(name)for(unsigned i=0;i<names.size();++i)if(handlers[i]&&_stricmp(name,names[i])==0)return &handlers[i];return nullptr;}
}

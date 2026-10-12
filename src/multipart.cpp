#include "multipart.hpp"
#include "multipart_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// X-Men Legends 1's breakable parts ([Game] Xml1Multipart, default off; the builder turns it on for LC builds).
//
// XML1 (the XBE's CMultipartManager, getter 0x89fc0, vtable 0x3cf6e4) gives a stats <Multipart> part health,
// effects and a power link. XMen2.exe kept only the skin bookkeeping (getter 0x48d1f0, vtable 0x689864: create,
// remove, parse 0x48cec0 = hideSkin/showSkin/NonMenuOnly, apply 0x48cbc0, -, clone 0x48cde0) and applies the
// hidden ("broken") look at spawn. This module adds what was cut, keyed by XMen2.exe's own record handles:
// - parse (vtable slot 2): after the native parser, read XML1's health / effectOnDeath / loopEffect /
//   effectBone / bone / linkToPower from the same node (0x5649c0). A part with health > 0 is an XML1 part;
//   XMen2.exe's own rows (health="0", menu-only boltons) keep the native behaviour.
// - apply (slot 3): XML1's start look for XML1 parts (0x88a40 -> 0x888d0 swapped: hideSkin shown, showSkin
//   hidden) instead of XMen2.exe's broken look.
// - clone (slot 5) / remove (slot 1): per-actor part state (multipart_rules::state).
// - damage: the CALL of 0x435440 at 0x438b0f (the XBE's 0x43fa0 ends with the manager's damage slot 0x899c0):
//   a hit that was not deflected goes to the current part; a part that breaks plays effectOnDeath and
//   loopEffect on effectBone (spawnEffectBone's path), hides hideSkin(2)/shows showSkin(2) (0x48c700) and
//   disables its linkToPower attacks.
// - AI (the brains' update slots): the buttons of a disabled attack are cleared, as the XBE's attack handler
//   0xeea60 does not press them (manager slot 7, 0x89440).
// Online play is lockstep: the break roll uses the native damage random stream (0x5601e0, stream 1) and shared
// gameplay rule 6 refuses peers whose effective state differs.
namespace multipart {
namespace {
using u32=std::uint32_t;
template<class T> T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T> T fn(u32 a){return reinterpret_cast<T>(static_cast<std::uintptr_t>(a));}

constexpr std::uint16_t revision=1;
constexpr u32 vt_remove=0x689868,vt_parse=0x68986c,vt_apply=0x689870,vt_clone=0x689878;
constexpr u32 damage_call=0x438b0f;
constexpr std::array<u32,6> brain_slots{0x694c60,0x695418,0x6953f0,0x695440,0x695388,0x695468};
constexpr std::array<u32,6> brain_native{0x517370,0x517370,0x51d610,0x51d880,0x51cad0,0x51cad0};

struct names { std::string death,loop,effect_bone,bone,hide; };
struct record_data { multipart_rules::parts parts; std::array<names,multipart_rules::max_parts> text; bool any=false; };
struct instance { u32 tpl=0; void* actor=nullptr; multipart_rules::state state; std::array<float,multipart_rules::max_parts> loop_at{}; };

std::unordered_map<u32,record_data> templates;
std::unordered_map<u32,instance> instances;
std::unordered_map<const void*,u32> actors;
bool was_requested=false,installed=false,trace=false;

using parse_t=u32(__fastcall*)(void*,void*,void*,u32,u32);
using remove_t=void(__fastcall*)(void*,void*,u32);
using apply_t=void(__fastcall*)(void*,void*,u32,void*,u32);
using clone_t=u32(__fastcall*)(void*,void*,u32,void*);
using damage_t=void(__fastcall*)(void*);
using brain_t=u32(__fastcall*)(void*,void*,void*,u32*);
parse_t real_parse=nullptr;remove_t real_remove=nullptr;apply_t real_apply=nullptr;clone_t real_clone=nullptr;
damage_t real_damage=nullptr;
std::array<brain_t,brain_slots.size()> real_brain{};
using script_t=void*(__cdecl*)(void*);
script_t real_segment=nullptr;
constexpr u32 segment_record=0x68b828;   // setSegmentVisible's {func,name,ret,args} record in the retail table 0x68a908

void* manager(){return fn<void*(__cdecl*)()>(0x48d1f0)();}
void* record(void* mgr,u32 h){return fn<void*(__fastcall*)(void*,void*,u32)>(0x48ca50)(mgr,nullptr,h);}
const char* attribute(void* node,const char* name){
    const char* v=fn<const char*(__fastcall*)(void*,void*,const char*)>(0x5649c0)(node,nullptr,name);
    return v?v:"";
}
// 0x48c700 (XBE 0x888d0): hide part `part`'s hideSkin[j] (+ "_outline") and show its showSkin[j]; swapped, the
// reverse.
void segments(void* rec,void* skin,int part,int j,bool swapped){
    fn<void(__fastcall*)(void*,void*,void*,int,int,int)>(0x48c700)(rec,nullptr,skin,part,j,swapped?1:0);
}
// spawnEffectBone's path (0x4a5360): load the effect (CFxManager vt+0x1c, flags 0xb), resolve the bolt name
// (0x575da0), take the bone's transform (0x41d710) and spawn attached to the actor (CFxManager vt+0x34).
u32 spawn_effect(void* actor,const std::string& effect,const std::string& bone){
    if(effect.empty()||bone.empty())return 0;
    auto* fx=fn<void*(__cdecl*)()>(0x407750)();
    const void* table=at<const void*>(fx,0);
    const u32 handle=at<u32(__fastcall*)(void*,void*,const char*,int)>(table,0x1c)(fx,nullptr,effect.c_str(),0xb);
    if(!handle)return 0;
    char found=0;
    const int bolt=fn<int(__cdecl*)(const char*,char*)>(0x575da0)(bone.c_str(),&found);
    if(found!=1)return 0;
    float position[3]{},direction[3]{};
    if(fn<char(__fastcall*)(void*,void*,int,float*,float*,int,int)>(0x41d710)(actor,nullptr,bolt,position,direction,0,1)!=1)return 0;
    at<void(__fastcall*)(void*,void*,u32,float*,float*,u32,int,const char*,float)>(table,0x34)(
        fx,nullptr,handle,position,direction,at<u32>(actor,0x1c),0,bone.c_str(),-1.f);
    return handle;
}
float now(){return fn<float(__cdecl*)()>(0x45fe60)();}
// The XBE's effect length (effect manager vt+0x24 = 0x184f0 -> 0x25ee0): LoopTime + random * RandLoopTime of the
// loaded effect, 0.3 for a handle that is gone. XMen2.exe keeps the same root record (0x44 bytes from the effect
// store at manager 0x4078d0 + 0x4874; LoopTime +0x34, RandLoopTime +0x38, PersistLoop +0x3c - parser 0x4182c0,
// reader 0x418d50), handle checked by 0x405100.
float loop_time(u32 effect){
    auto* store=reinterpret_cast<unsigned char*>(fn<void*(__cdecl*)()>(0x4078d0)())+0x4874;
    if(!effect||(fn<u32(__fastcall*)(void*,void*,u32)>(0x405100)(store,nullptr,effect)&0xff)==0)return .3f;
    const auto* rec=store+(at<u32>(store,0x419c)&effect)*0x44;
    float t=at<float>(rec,0x34);
    const float spread=at<float>(rec,0x38);
    if(spread!=0.f)t+=spread*fn<float(__cdecl*)(unsigned char)>(0x560190)(1);
    return t;
}
// The bolt table's first name (0x6e25f0[0]): where the XBE re-spawns a loop when no part is current (bone 0).
const char* first_bolt(){return at<const char*>(reinterpret_cast<const void*>(0x6e25f0),0);}
int last_roll=-1;
int roll(){last_roll=fn<int(__cdecl*)(int,int,int)>(0x5601e0)(0,2,1);return last_roll;}

u32 __fastcall parse_hook(void* mgr,void* edx,void* node,u32 key,u32 menu){
    const u32 h=real_parse(mgr,edx,node,key,menu);
    if(!h||!node)return h;
    auto* rec=record(mgr,h);
    if(!rec)return h;
    const int index=at<int>(rec,0xac)-1;
    if(index<0)return h;
    const char* health=attribute(node,"health");
    const int value=static_cast<int>(std::atof(health));
    if(value<=0)return h;   // XMen2.exe's own rows, or no XML1 data: native behaviour
    if(index>=multipart_rules::max_parts){
        logger::write("multipart: a fifth XML1 part was ignored (the first game holds four)");
        return h;
    }
    auto& t=templates[h];
    auto& p=t.parts.value[index];
    p.breakable=true;p.health=value;p.power=multipart_rules::link_bits(attribute(node,"linkToPower"));
    t.text[index]={attribute(node,"effectOnDeath"),attribute(node,"loopEffect"),attribute(node,"effectBone"),attribute(node,"bone"),attribute(node,"hideSkin")};
    if(t.parts.count<index+1)t.parts.count=index+1;
    t.any=true;
    return h;
}
const record_data* data_of(u32 h,u32* tpl=nullptr){
    auto i=instances.find(h);
    const u32 key=i==instances.end()?h:i->second.tpl;
    auto t=templates.find(key);
    if(t==templates.end()||!t->second.any)return nullptr;
    if(tpl)*tpl=key;
    return &t->second;
}
const record_data* data_of_instance(const instance& in){
    auto t=templates.find(in.tpl);
    return t==templates.end()||!t->second.any?nullptr:&t->second;
}
void __fastcall apply_hook(void* mgr,void* edx,u32 h,void* skin,u32 menu){
    real_apply(mgr,edx,h,skin,menu);
    const auto* d=data_of(h);
    if(!d||!skin)return;
    auto* rec=record(mgr,h);
    if(!rec)return;
    for(int i=0;i<d->parts.count;++i)
        if(d->parts.value[i].breakable)
            for(int j=0;j<2;++j)segments(rec,skin,i,j,true);
}
u32 __fastcall clone_hook(void* mgr,void* edx,u32 tpl,void* actor){
    const u32 h=real_clone(mgr,edx,tpl,actor);
    if(!h)return h;
    auto t=templates.find(tpl);
    if(t==templates.end()||!t->second.any||!actor)return h;
    instances[h]={tpl,actor,multipart_rules::spawn(t->second.parts)};
    actors[actor]=h;
    if(trace)logger::write("multipart: actor %08X spawned with %d XML1 part(s)",at<u32>(actor,0x1c),t->second.parts.count);
    return h;
}
void __fastcall remove_hook(void* mgr,void* edx,u32 h){
    auto i=instances.find(h);
    if(i!=instances.end()){
        auto a=actors.find(i->second.actor);
        if(a!=actors.end()&&a->second==h)actors.erase(a);
        instances.erase(i);
    }
    templates.erase(h);
    real_remove(mgr,edx,h);
}
void break_part(instance& in,const record_data& d,u32 inst,int k){
    auto* rec=record(manager(),inst);
    if(rec)for(int j=0;j<2;++j)segments(rec,static_cast<unsigned char*>(in.actor)+0x1cc,k,j,false);
    const auto& t=d.text[k];
    spawn_effect(in.actor,t.death,t.effect_bone);
    if(const u32 loop=spawn_effect(in.actor,t.loop,t.effect_bone))in.loop_at[k]=now()+loop_time(loop);
    logger::write("multipart: actor %08X part %d broke (effect %s on %s, attacks disabled %X)",at<u32>(in.actor,0x1c),k+1,
        t.death.c_str(),t.effect_bone.c_str(),in.state.disabled);
}
void __fastcall damage_hook(void* event){
    const float before=at<float>(event,0x10);
    real_damage(event);
    void* victim=at<void*>(event,0);
    auto a=actors.find(victim);
    if(a==actors.end()){
        // [Test] MultipartTrace: every other character hit too, with its resistance bytes (actor+0x550..0x557,
        // summed by 0x4294d0), for checking the first game's powerup removals.
        if(trace&&victim)logger::write("multipart: victim %08X hit %d, resist %d %d %d %d %d %d %d %d",at<u32>(victim,0x1c),
            static_cast<int>(at<float>(event,0x10)),at<signed char>(victim,0x550),at<signed char>(victim,0x551),
            at<signed char>(victim,0x552),at<signed char>(victim,0x553),at<signed char>(victim,0x554),
            at<signed char>(victim,0x555),at<signed char>(victim,0x556),at<signed char>(victim,0x557));
        return;
    }
    const float after=at<float>(event,0x10);
    if(before!=0.f&&after==0.f)return;   // deflected: the XBE returns before its multipart call
    auto i=instances.find(a->second);
    if(i==instances.end())return;
    const auto* d=data_of(a->second);
    if(!d)return;
    auto& in=i->second;
    last_roll=-1;
    const int k=multipart_rules::hit(in.state,d->parts,static_cast<int>(after),roll);
    if(trace)logger::write("multipart: actor %08X hit %d (before deflect %d), current part %d, health %d/%d, roll %d, resist %d %d %d %d %d %d %d %d",at<u32>(victim,0x1c),
        static_cast<int>(after),static_cast<int>(before),in.state.current,in.state.health[0],in.state.health[1],last_roll,
        at<signed char>(victim,0x550),at<signed char>(victim,0x551),at<signed char>(victim,0x552),at<signed char>(victim,0x553),
        at<signed char>(victim,0x554),at<signed char>(victim,0x555),at<signed char>(victim,0x556),at<signed char>(victim,0x557));
    if(k>=0)break_part(in,*d,a->second,k);
}
// destroyMultipartPiece (XBE 0x9ce30 -> 0x88fa0) has no XMen2.exe function; the builder writes it as
// setSegmentVisible(actor, hideSkin, "0") + setSegmentVisible(actor, showSkin, "1") + spawnEffectBone(...). Hiding a
// whole XML1 part's hideSkin from script is that call: the part is broken here too (health 0, its attacks
// disabled, the current part moved on - multipart_rules::destroy), so later hits cannot break it a second time.
// The script plays the look and the effect itself.
const char* argument(void* args,int i){
    using arg_t=void*(__fastcall*)(void*,void*,int);
    using text_t=const char*(__fastcall*)(void*,void*);
    auto* a=fn<arg_t>(0x4d5830)(args,nullptr,i);
    return a?at<text_t>(at<void*>(a,0),0x14)(a,nullptr):"";
}
void* __cdecl segment_hook(void* args){
    void* result=real_segment(args);
    if(actors.empty())return result;
    const char* visible=argument(args,2);
    if(std::atoi(visible?visible:"")!=0)return result;
    const std::string name=argument(args,0),segment=argument(args,1);
    u32 h=0;
    using lookup_t=u32*(__cdecl*)(u32*,const char*);
    using resolve_t=void*(__fastcall*)(const u32*,void*);
    void* actor=fn<resolve_t>(0x4654b0)(fn<lookup_t>(0x4a1700)(&h,name.c_str()),nullptr);
    auto a=actors.find(actor);
    if(a==actors.end())return result;
    auto i=instances.find(a->second);
    const auto* d=data_of(a->second);
    if(i==instances.end()||!d)return result;
    for(int k=0;k<d->parts.count;++k)
        if(d->parts.value[k].breakable&&_stricmp(d->text[k].hide.c_str(),segment.c_str())==0){
            const int broke=multipart_rules::destroy(i->second.state,d->parts,k+1);
            logger::write("multipart: script hid %s on %s: part %d %s (current part %d, attacks disabled %X)",segment.c_str(),
                name.c_str(),k+1,broke>=0?"broken":"already broken",i->second.state.current,i->second.state.disabled);
            break;
        }
    return result;
}
// 0x88aa0 (from the actor update 0x3ae60): a broken part's loopEffect is spawned again each time it ends - at the
// CURRENT part's bone (bone 0 when none), not at the part that broke - for as long as the actor lives.
void tick_loops(instance& in){
    const auto* d=data_of_instance(in);
    if(!d)return;
    float t=-1;
    for(int k=0;k<d->parts.count;++k){
        if(in.loop_at[k]<=0.f||d->text[k].loop.empty())continue;
        if(t<0)t=now();
        if(t<=in.loop_at[k])continue;
        const std::string bone=in.state.current>0?d->text[in.state.current-1].bone:std::string(first_bolt()?first_bolt():"");
        const u32 loop=spawn_effect(in.actor,d->text[k].loop,bone);
        in.loop_at[k]=t+loop_time(loop);
        if(trace){
            char key[48];std::snprintf(key,sizeof key,"multipart-loop-%08X-%d",at<u32>(in.actor,0x1c),k);
            logger::write_once(key,"multipart: actor %08X part %d loop %s re-spawned on %s (handle %08X, next in %.2f s)",
                at<u32>(in.actor,0x1c),k+1,d->text[k].loop.c_str(),bone.c_str(),loop,in.loop_at[k]-t);
        }
    }
}
template<std::size_t I> u32 __fastcall brain_hook(void* brain,void* edx,void* actor,u32* output){
    const u32 result=real_brain[I](brain,edx,actor,output);
    if(!actors.empty()&&output){
        auto a=actors.find(actor);
        if(a!=actors.end()){
            auto i=instances.find(a->second);
            if(i!=instances.end()){
                tick_loops(i->second);
                const u32 before[2]{output[0],output[1]};
                multipart_rules::filter_buttons(i->second.state.disabled,output);
                if(trace&&(before[0]!=output[0]||before[1]!=output[1]))
                    logger::write("multipart: actor %08X attack buttons %X/%X refused (disabled %X)",at<u32>(actor,0x1c),
                        before[0]&~output[0],before[1]&~output[1],i->second.state.disabled);
            }
        }
    }
    return result;
}
template<std::size_t... I> std::array<void*,sizeof...(I)> brain_hooks(std::index_sequence<I...>){
    return {reinterpret_cast<void*>(&brain_hook<I>)...};
}
}
bool requested(){return was_requested;}
registration capability(){
    if(!was_requested)return {{0,0,0,0},true};
    if(!installed)return {{0,0,0,0},false};
    return {{1,revision,0,0},true};
}
void install(HMODULE game){
    was_requested=ini::flag(L"Game",L"Xml1Multipart",false);installed=false;
    if(!was_requested)return;
    trace=ini::flag(L"Test",L"MultipartTrace",false);
    if(reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base){logger::write("multipart: image isn't retail; native parts retained");return;}
    constexpr std::array<limits_rules::guard,22> guards{{
        {0x48d1f0,"8a0de0c47200b8","multipart manager getter"},
        {0x48ca50,"568b74240885f6744c8b81e0","multipart record lookup"},
        {0x48c700,"83ec68a1f8386f0053555657","multipart segment hide/show"},
        {0x5649c0,"5355568be9578b7d0c8d4701","XML node attribute getter"},
        {0x435440,"81ec8800000053568bf18b46","damage: deflect stage"},
        {0x438b0b,"8d4c2414e82cc9ffff8d4c24","damage: deflect stage call"},
        {0x407750,"64a1000000008a0de08e6f00","effect manager getter"},
        {0x575da0,"578b7c240868d450680057e8","bolt name lookup"},
        {0x41d710,"83ec0c538b5c2418568bf1f6","bone transform"},
        {0x5601e0,"8a44240c568b742408578b7c24103bf77e44","native integer RNG"},
        {0x689864,"f0cc480090d14800c0ce4800c0cb4800b0cb4800e0cd4800","multipart manager vtable"},
        {0x48cec0,"83ec24a1f8386f00538b5c24","multipart parser"},
        {0x48cde0,"6aff68813e670064a1000000","multipart clone"},
        {0x48cbc0,"8b44240450e886feffff85c0","multipart apply"},
        {0x48d190,"568b74240885f674408b81e0","multipart remove"},
        {0x4a1700,"83ec1c568b74242856e82e0c","entity name lookup"},
        {0x4654b0,"568bf1e8682a06008b16518b","entity handle resolve"},
        {0x4d5830,"8b511c33c085d2740d568b74","script argument getter"},
        {0x45fe60,"8b0d348471008b01ffa060010000","game clock"},
        {0x4078d0,"a13c3c6f0085c07562566a0b","effect store getter"},
        {0x405100,"8b442404568bf18b969c4100","effect handle check"},
        {0x560190,"8a44240484c07405e9f3feffffe93eff","native random stream"}}};
    for(const auto& g:guards)
        if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)){
            logger::write("multipart: guard failed at %08X (%s); native parts retained",g.va,g.what);return;}
    real_parse=fn<parse_t>(0x48cec0);real_remove=fn<remove_t>(0x48d190);real_apply=fn<apply_t>(0x48cbc0);
    real_clone=fn<clone_t>(0x48cde0);real_damage=fn<damage_t>(0x435440);
    using xml1_hook_transaction::pointer_bytes;
    std::vector<xml1_hook_transaction::patch> writes{
        {vt_parse,pointer_bytes(reinterpret_cast<void*>(real_parse)),pointer_bytes(reinterpret_cast<void*>(&parse_hook))},
        {vt_remove,pointer_bytes(reinterpret_cast<void*>(real_remove)),pointer_bytes(reinterpret_cast<void*>(&remove_hook))},
        {vt_apply,pointer_bytes(reinterpret_cast<void*>(real_apply)),pointer_bytes(reinterpret_cast<void*>(&apply_hook))},
        {vt_clone,pointer_bytes(reinterpret_cast<void*>(real_clone)),pointer_bytes(reinterpret_cast<void*>(&clone_hook))}};
    xml1_hook_transaction::patch call{damage_call+1,{0x2c,0xc9,0xff,0xff},{0,0,0,0}};
    const u32 rel=static_cast<u32>(reinterpret_cast<std::uintptr_t>(&damage_hook))-(damage_call+5);
    std::memcpy(call.after.data(),&rel,4);
    writes.push_back(call);
    const auto hooks=brain_hooks(std::make_index_sequence<brain_slots.size()>{});
    for(std::size_t i=0;i<brain_slots.size();++i){
        real_brain[i]=fn<brain_t>(brain_native[i]);
        writes.push_back({brain_slots[i],pointer_bytes(reinterpret_cast<void*>(real_brain[i])),pointer_bytes(hooks[i])});
    }
    // The active script function table (forced_teams may have moved it into the DLL), as mold_controller reads it.
    const u32 table=at<u32>(reinterpret_cast<const void*>(0x49fe31),0);
    const u32 segment_slot=table+(segment_record-0x68a908);
    real_segment=at<script_t>(reinterpret_cast<const void*>(segment_slot),0);
    const bool bridge=real_segment==fn<script_t>(0x4a5bd0);
    if(bridge)writes.push_back({segment_slot,pointer_bytes(reinterpret_cast<void*>(real_segment)),pointer_bytes(reinterpret_cast<void*>(&segment_hook))});
    else logger::write("multipart: setSegmentVisible isn't the retail handler; scripted part removal stays cosmetic");
    xml1_hook_transaction::memory memory;
    bool safe=true;
    if(xml1_hook_transaction::apply(memory,writes,&safe)){
        installed=true;
        logger::write("multipart: XML1 breakable parts installed (parser, start look, damage at 0x438B0F, %u AI update slots, scripted removal %s)",unsigned(brain_slots.size()),bridge?"on":"off");
    }
    else if(safe)logger::write("multipart: patch transaction refused; native parts retained");
    else {game_version::refuse_startup();logger::write("multipart: rollback/protection verification failed; process startup refused");}
}
}

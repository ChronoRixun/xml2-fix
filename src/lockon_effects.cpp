#include "lockon_effects.hpp"
#include "lockon_effects_rules.hpp"
#include "lockon_effects_hooks.hpp"
#include "xml1_hook_memory.hpp"
#include "entity_capacity.hpp"
#include "mold_query_rules.hpp"
#include "game_version.hpp"
#include "ini_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

// The PC retained both handlers' scan/target logic but removed XML1's common
// effects 19/20/21. Restore only those emissions at the corresponding native
// branches. Paths come from the builder's authored dormant effect references.
namespace lockon_effects {
namespace {
using u32=std::uint32_t;
lockon_effects_rules::paths effects;
lockon_effects_rules::handle_cache handles;
bool trace=false, wanted=false, applied=false, hooks_applied=false;
template<class T>T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T>T fn(u32 address){return reinterpret_cast<T>(address);}
const float* vector(const void* p,u32 off){return reinterpret_cast<const float*>(static_cast<const unsigned char*>(p)+off);}
using getter_t=void*(__cdecl*)();
using load_t=u32(__fastcall*)(void*,void*,const char*,int);
using emit_t=void(__fastcall*)(void*,void*,u32,const float*,const float*,u32,int,const char*,float);
using point_t=const float*(__fastcall*)(const void*,void*);
struct effect_resources {
    void* manager;
    bool valid(u32 handle) {
        auto* pool=static_cast<unsigned char*>(fn<getter_t>(0x4078d0)())+0x4874;
        return fn<unsigned char(__fastcall*)(void*,void*,u32)>(0x405100)(pool,nullptr,handle)!=0;
    }
    u32 load(const std::string& name){return fn<load_t>(0x4079a0)(manager,nullptr,name.c_str(),11);}
};

void emit(unsigned kind,void* actor,const float* origin,const float* direction) {
    auto* manager=fn<getter_t>(0x407750)();
    // Loading increments the definition's 16-bit use counter. Resolve once,
    // then validate the native generation before every use. If native teardown
    // invalidates a definition, even a same-zone reload resolves a fresh handle.
    effect_resources resources{manager};
    const auto handle=handles.resolve(kind,resources,effects);
    if(!handle)return;
    const auto owner=at<u32>(actor,0x1c);
    fn<emit_t>(0x40b2b0)(manager,nullptr,handle,origin,direction,owner,0,nullptr,-1.f);
    if(trace)logger::write("lockon effects: spawn %u handle %08X owner %u at %.3f/%.3f/%.3f direction %.3f/%.3f/%.3f",
        kind,handle,owner,origin[0],origin[1],origin[2],direction[0],direction[1],direction[2]);
}
void __cdecl observe(void* actor,const void* frame,unsigned phase,void* target) {
    if(phase==0)emit(0,actor,vector(frame,0x28),vector(frame,0xc));
    else if(phase==1) {
        const auto* ref=static_cast<const unsigned char*>(frame)+0x18;
        const auto* point=fn<point_t>(0x4a95f0)(ref,nullptr);
        const auto* normal=fn<point_t>(0x4a9600)(ref,nullptr);
        emit(0,actor,vector(frame,0x28),vector(frame,0x68));
        emit(1,actor,point,normal);
    }
    else if(phase==2) {
        const auto* ref=static_cast<const unsigned char*>(frame)+0x18;
        emit(2,actor,vector(target,0x20),fn<point_t>(0x4a9600)(ref,nullptr));
    }
    else if(phase==3)emit(2,actor,vector(target,0x20),vector(frame,8));
}
bool dependencies(xml1_hook_transaction::memory& memory) {
    for(const auto& g:lockon_effects_rules::guards) {
        std::vector<unsigned char> b(g.hex.size()/2);
        if(!memory.read(g.va,b.data(),b.size())||!limits_rules::matches(b.data(),g.hex))return false;
    }
    // The entity-capacity transaction owns these accessors. Accept only stock
    // bytes or its published jump target, never an arbitrary compatible-looking hook.
    for(const auto& e:std::array<mold_query_rules::entry,3>{{
        {0x4a95f0,"8b0185c07505","point"},{0x4a9600,"8b0185c07404","normal"},
        {0x4a9590,"568bf1833e00","entity"}}}) {
        const auto target=entity_capacity::installed_target(e.va);
        std::array<unsigned char,6> b{};
        if(!memory.read(e.va,b.data(),b.size())||!mold_query_rules::entry_ok(target!=0,b.data(),e,target))return false;
    }
    return true;
}
}
bool requested() { return wanted; }
bool installed() { return applied; }
bool verified() { return !wanted || applied; }
void install(HMODULE game) {
    if(hooks_applied)return;
    const auto aim_text=ini::text(L"Game",L"Xml1LockOnAim");
    const auto aim=ini_rules::flag(aim_text ? std::optional<std::string_view>(*aim_text) : std::nullopt);
    wanted=aim_text.has_value() && (!aim.has_value() || *aim);
    if(wanted && !aim.has_value()) {
        logger::write("lockon effects: invalid aiming option; native behavior retained");return;
    }
    const bool animated_head=aim.value_or(false);
    const auto text=ini::text(L"Game",L"Xml1LockOnEffects");
    if(!text||*text=="0") {
        if(animated_head)logger::write("lockon effects: aiming requires valid effect paths; native behavior retained");
        return;
    }
    const auto parsed=lockon_effects_rules::parse(*text);
    xml1_hook_transaction::memory memory;
    if(!parsed||reinterpret_cast<std::uintptr_t>(game)!=limits_rules::image_base||!dependencies(memory)) {
        logger::write("lockon effects: invalid paths or dependency guard; native behavior retained");return;
    }
    effects=*parsed;trace=ini::flag(L"Test",L"LockOnEffectsTrace",false);
    lockon_effects_hooks::observe=&observe;
    auto address=[](const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));};
    const auto writes=lockon_effects_rules::plan({address(&lockon_effects_hooks::miss_bridge),address(&lockon_effects_hooks::hit_bridge),
        address(&lockon_effects_hooks::acquired_bridge),address(&lockon_effects_hooks::locked_bridge)},animated_head);
    bool safe=true;
    if(xml1_hook_transaction::apply(memory,writes,&safe)) {
        hooks_applied=true;applied=animated_head;
        logger::write("lockon effects: scan, surface and acquired-target emissions restored; head query %s",
            animated_head?"animated":"stock approximate");
    }
    else if(safe)logger::write("lockon effects: transaction refused; native behavior retained");
    else {game_version::refuse_startup();logger::write("lockon effects: rollback/protection restoration failed; startup refused");}
}
}

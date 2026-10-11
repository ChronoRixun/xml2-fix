#include "xml1_turrets.hpp"
#include "mold_controller.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "limits_rules.hpp"
#include "game_version.hpp"
#include "xml1_hook_memory.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

// First, test the native non-character combat bridge. This diagnostic is deliberately
// opt-in and is not enabled by the builder. It runs on the game's script thread.
namespace xml1_turrets {
namespace {
using u32 = std::uint32_t;
template<class T> T read(const void* p, u32 off) { T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p)+off,sizeof v); return v; }
template<class T> void put(void* p,u32 off,T v) { std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof v); }
template<class T> T fn(u32 va) { return reinterpret_cast<T>(static_cast<std::uintptr_t>(va)); }
using by_name_t = u32*(__cdecl*)(u32*,const char*);
using resolve_t = void*(__fastcall*)(const u32*,void*);
using intern_t = u32*(__fastcall*)(u32*,void*,const char*);
using construct_t = void*(__fastcall*)(void*,void*,u32,float,short,u32,u32,u32,unsigned char,const float*,const float*,const float*);
using system_t = void*(__cdecl*)();
using spawn_t = void(__fastcall*)(void*,void*,u32*,void*,const void*,const void*,u32,float,u32);
using reader_ctor_t = void(__fastcall*)(void*,void*);
using reader_load_t = unsigned char(__fastcall*)(void*,void*,const char*,unsigned char,u32,unsigned char);
using reader_root_t = void*(__fastcall*)(void*,void*);
using definitions_t = void(__fastcall*)(void*,void*,void*,void*,u32,u32,unsigned char,unsigned char);
using definition_t = void*(__fastcall*)(void*,void*,const char*);
using node_t = void*(__fastcall*)(void*,void*,const u32*);
using play_t = void(__fastcall*)(void*,void*,void*,int);
using target_t = void(__fastcall*)(void*,void*,u32);
using angles_t = float*(__cdecl*)(float*,const float*);
using rotation_t = void(__fastcall*)(void*,void*,const float*);
using position_t = const float*(__fastcall*)(void*,void*,float*);
using time_t = float(__cdecl*)();
std::string source_name, target_name, entity_name;
std::string entity_file;
std::string probe_mode, probe_node;
float damage = 0, speed = 0;
// The diagnostic keeps its reader until process exit. Whether the registry
// retains views into the XMLB buffer is a separate production-lifetime gate.
std::array<unsigned char,12> definition_reader{};
bool loaded=false;
bool installed_beam=false;
u32 beam_resume=0x452312;
u32 active_caster=0, active_core=0;
float active_until=0;
void* find(const std::string& name,u32& out);
void __cdecl adjust_core_beam(void* actor,const float* start,float* direction) {
    void* target=nullptr;
    if(!mold_controller::beam_target(actor,target)){
        if(probe_mode!="corebeam"||read<u32>(actor,0x1c)!=active_caster||fn<time_t>(0x45fe60)()>active_until)return;
        target=fn<resolve_t>(0x4654b0)(&active_core,nullptr);
    }
    if(!target||read<float>(target,0x27c)<=0) return;
    float scratch[3]{};
    const auto* point=read<position_t>(read<const void*>(target,0),0x24)(target,nullptr,scratch);
    float d[3]{},length2=0;
    for(u32 i=0;i<3;++i){d[i]=point[i]-start[i];length2+=d[i]*d[i];}
    if(length2<=0) return;
    const float length=std::sqrt(length2);
    for(u32 i=0;i<3;++i)direction[i]=d[i]/length;
    logger::write("core probe: native beam from %.3f/%.3f/%.3f to core %.3f/%.3f/%.3f, range %.3f; ordinary trace/damage unchanged",
        start[0],start[1],start[2],point[0],point[1],point[2],length);
}
__declspec(naked) void beam_stub() {
    __asm {
        pushfd
        pushad
        lea eax,[esp+074h]
        lea ecx,[esp+068h]
        push eax
        push ecx
        push edi
        call adjust_core_beam
        add esp,12
        popad
        popfd
        lea ecx,[esp+0214h]
        jmp dword ptr [beam_resume]
    }
}

u32* intern(u32& out,const char* name) { return fn<intern_t>(0x41b120)(&out,nullptr,name); }
void* find(const std::string& name,u32& out) {
    auto* h=fn<by_name_t>(0x4a1700)(&out,name.c_str());
    return fn<resolve_t>(0x4654b0)(h,nullptr);
}
void fire() {
    u32 source_id=0,target_id=0,entity_key=0;
    void* source=find(source_name,source_id); void* target=find(target_name,target_id);
    if(!source || !target) { logger::write("turret probe: missing source/target %08X/%08X",source_id,target_id); return; }
    void* manager=fn<system_t>(0x4625c0)();
    const auto* manager_vt=read<const void*>(manager,0);
    auto definition=read<definition_t>(manager_vt,0x20);
    if(probe_mode!="corebeam"&&!definition(manager,nullptr,entity_name.c_str())&&!loaded) {
        fn<reader_ctor_t>(0x564a80)(definition_reader.data(),nullptr);
        const auto path="data/entities/"+entity_file;
        const auto ok=fn<reader_load_t>(0x564eb0)(definition_reader.data(),nullptr,path.c_str(),0,3,0);
        auto* root=fn<reader_root_t>(0x564aa0)(definition_reader.data(),nullptr);
        u32 file_key=0; intern(file_key,entity_file.c_str());
        if(ok&&root) read<definitions_t>(manager_vt,4)(manager,nullptr,root,root,11,file_key,1,1);
        loaded=true;
        logger::write("turret probe: definition file loaded %u root %p key %08X definition %p",unsigned(ok),root,file_key,definition(manager,nullptr,entity_name.c_str()));
    }
    intern(entity_key,entity_name.c_str());
    std::array<unsigned char,0x30> description{};
    float origin[3]{}, direction[3]{};
    float length2=0;
    for(u32 i=0;i<3;++i) {
        origin[i]=read<float>(source,0x20+i*4);
        direction[i]=read<float>(target,0x20+i*4)-origin[i];
        length2+=direction[i]*direction[i];
    }
    if(length2<=0) return;
    const float length=std::sqrt(length2);
    for(auto& v:direction) v/=length;
    if(probe_mode=="corebeam") {
        probe_node=ini::text(L"Test",L"ProbeNode").value_or(probe_node);
        u32 node_key=0; intern(node_key,probe_node.c_str());
        void* node=fn<node_t>(0x427ca0)(source,nullptr,&node_key);
        if(!node) { logger::write("core probe: native combat node missing"); return; }
        float facing[3]{}, horizontal[3]{direction[0],direction[1],0};
        fn<angles_t>(0x5607b0)(facing,horizontal);
        read<rotation_t>(read<const void*>(source,0),0x2c)(source,nullptr,facing);
        fn<play_t>(0x42c340)(source,nullptr,node,1);
        fn<target_t>(0x42c7c0)(source,nullptr,target_id);
        active_caster=source_id;active_core=target_id;active_until=fn<time_t>(0x45fe60)()+4;
        logger::write("core probe: staged native %s node %p on %08X targeting %08X; target health %.3f source orientation %.3f/%.3f/%.3f",
            probe_node.c_str(),node,source_id,target_id,read<float>(target,0x27c),facing[0],facing[1],facing[2]);
        return;
    }
    std::memcpy(description.data(),origin,12);
    std::memcpy(description.data()+12,direction,12);
    put(description.data(),0x1c,entity_key);
    put(description.data(),0x24,static_cast<short>(speed));
    put(description.data(),0x2c,static_cast<unsigned char>(1));
    std::array<unsigned char,0x6c> attack{};
    float zero[3]{};
    fn<construct_t>(0x429860)(attack.data(),nullptr,source_id,damage,0,5,0x80,0,1,zero,zero,zero);
    void* system=fn<system_t>(0x450030)();
    auto spawn=read<spawn_t>(read<const void*>(system,0),0x28);
    u32 result=0;
    spawn(system,nullptr,&result,source,description.data(),attack.data(),0,30,0);
    logger::write("turret probe: source %08X ptr %p, target %08X, prefab %08X, result %08X damage %.3f speed %.3f at %.3f/%.3f/%.3f",
        source_id,source,target_id,entity_key,result,damage,speed,origin[0],origin[1],origin[2]);
}
void* __cdecl probe(void*) { fire(); return nullptr; }
bool supported() {
    constexpr std::array<limits_rules::guard,15> guards{{
        {0x4510a0,"81ec8001000053555657","native projectile service"},
        {0x4a1700,"83ec1c568b74242856","entity lookup"},
        {0x4654b0,"568bf1e8682a0600","handle resolver"},
        {0x41b120,"56578b7c240c85ff","string handle constructor"},
        {0x429860,"8b44241c8b5424145650","attack constructor"},
        {0x564a80,"8bc18a480880e1fe","XML reader constructor"},
        {0x564eb0,"81ec08050000","XML reader loader"},
        {0x462610,"5153555657","definition registration"},
        {0x68698c,"10264600","definition registration vtable"},
        {0x427ca0,"8b442404","combat node lookup"},
        {0x42c340,"56578b7c240c85ff","combat node play"},
        {0x42c7c0,"53578bf9e8f7bf0900","manual combat target"},
        {0x5607b0,"d905300068008b4c2408","direction to angles"},
        {0x45230b,"8d8c2414020000","beam direction after native muzzle tracing"},
        {0x68b028,"f0af5a00","unused test script entry"}}};
    for(const auto& g:guards) if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)) {
        logger::write("turret probe: guard mismatch at %08X",g.va); return false;
    }
    return true;
}
}
void install(HMODULE) {
    const bool diagnostic=ini::number(L"Test",L"NativeProjectileProbe",0)==1;
    const bool mold=ini::flag(L"Game",L"Xml1MasterMold",false);
    if(!diagnostic&&!mold)return;
    if(!game_version::offline_gameplay_ready()) { logger::write("XML1 combat: online exclusion unavailable; no hooks installed"); return; }
    if(mold) {
        if(diagnostic)logger::write("XML1 combat: diagnostic and controller are mutually exclusive; diagnostic refused");
        return; // Controller installs its complete dependency set in one transaction.
    }
    source_name=ini::text(L"Test",L"ProjectileSource").value_or("");
    target_name=ini::text(L"Test",L"ProjectileTarget").value_or("");
    entity_name=ini::text(L"Test",L"ProjectileEntity").value_or("");
    entity_file=ini::text(L"Test",L"ProjectileFile").value_or("");
    probe_mode=ini::text(L"Test",L"ProbeMode").value_or("projectile");
    probe_node=ini::text(L"Test",L"ProbeNode").value_or("");
    damage=static_cast<float>(ini::number(L"Test",L"ProjectileDamage",0));
    speed=static_cast<float>(ini::number(L"Test",L"ProjectileSpeed",0));
    if(!supported())return;
    if(diagnostic&&(source_name.empty()||target_name.empty()||(probe_mode=="corebeam" ? probe_node.empty() : (entity_name.empty()||entity_file.empty()||damage<=0||speed<=0))))return;
    if(probe_mode=="corebeam"||mold) {
        std::array<unsigned char,7> patch{0xe9,0,0,0,0,0x90,0x90};
        const u32 delta=static_cast<u32>(reinterpret_cast<std::uintptr_t>(&beam_stub))-0x452310;
        std::memcpy(patch.data()+1,&delta,4);
        DWORD old=0;
        if(!VirtualProtect(reinterpret_cast<void*>(0x45230b),patch.size(),PAGE_EXECUTE_READWRITE,&old)) return;
        std::memcpy(reinterpret_cast<void*>(0x45230b),patch.data(),patch.size());
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x45230b),patch.size());
        VirtualProtect(reinterpret_cast<void*>(0x45230b),patch.size(),old,&old);
        installed_beam=true;
    }
    if(!diagnostic)return;
    // ForcedTeams has already installed its extended table, if requested. Patch
    // the active table's existing unused no-op rather than add another name.
    const auto table=read<u32>(reinterpret_cast<const void*>(0x49fe31),0);
    auto* slot=reinterpret_cast<void**>(table+(0x68b028-0x68a908));
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) return;
    *slot=reinterpret_cast<void*>(&probe);
    VirtualProtect(slot,sizeof(void*),old,&old);
    logger::write("turret probe: installed opt-in diagnostic; invoke causeTroubleForAutotest()");
}
bool prepare_beam(xml1_hook_transaction::patch& patch){
    if(!game_version::offline_gameplay_ready()||!supported())return false;
    patch.address=0x45230b;
    patch.before={0x8d,0x8c,0x24,0x14,0x02,0x00,0x00};
    patch.after={0xe9,0,0,0,0,0x90,0x90};
    const u32 delta=static_cast<u32>(reinterpret_cast<std::uintptr_t>(&beam_stub))-0x452310;
    std::memcpy(patch.after.data()+1,&delta,4);
    return true;
}
}

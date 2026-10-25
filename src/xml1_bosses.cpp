#include "xml1_bosses.hpp"
#include "mold_controller.hpp"
#include "boss_dopples.hpp"
#include "boss_avalanche.hpp"
#include "boss_pyro.hpp"
#include "boss_mystique.hpp"
#include "boss_shadowking.hpp"
#include "boss_magneto.hpp"
#include "boss_blob.hpp"
#include "boss_native_sites.hpp"
#include "xml1_hook_transaction.hpp"
#include "xml1_hook_memory.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <cstring>
#include <string>
#include <vector>

// One owner for the native brain-dispatch site and the script-table entries the XML1 boss controllers share. The
// clients check both zone and generation-checked actor identity: Mystique and Magneto share asteroid2_1.
// Online play is lockstep; each client's effective state is its own gameplay rule (3 Master Mold, 40 the rest).
namespace xml1_bosses {
namespace {
using u32=std::uint32_t;
template<class T> T at(const void* p,u32 off){T v{};std::memcpy(&v,static_cast<const unsigned char*>(p)+off,sizeof v);return v;}
template<class T> T fn(u32 a){return reinterpret_cast<T>(static_cast<std::uintptr_t>(a));}
using script_t=void*(__cdecl*)(void*);
using arg_t=void*(__fastcall*)(void*,void*,int);
using text_t=const char*(__fastcall*)(void*,void*);
// A script call's argument i as text (the native argument accessor, as Master Mold reads them).
const char* argument(void* args,int i){auto* a=fn<arg_t>(0x4d5830)(args,nullptr,i);return a?at<text_t>(at<void*>(a,0),0x14)(a,nullptr):"";}
script_t real_sequence=nullptr,real_node=nullptr,real_default_target=nullptr;
u32 brain_resume=0x5173f9,brain_postprocess=0x517a79;
bool mold_on=false,dopples_on=false,avalanche_on=false;
bool pyro_on=false,mystique_on=false,shadowking_on=false,marrow_on=false;
bool magneto_on=false,blob_on=false;
bool list_requested=false,list_known=true;u32 requested_bits=0;
void* __cdecl sequence_hook(void* args){
    if(mold_on)mold_controller::on_sequence(args);
    // Script arguments are read only in the boss's zone (as Master Mold reads them).
    if(avalanche_on&&boss_avalanche::in_scope())boss_avalanche::on_sequence(argument(args,0),argument(args,1));
    auto* result=real_sequence(args);
    if(pyro_on&&boss_pyro::in_scope())boss_pyro::on_sequence(argument(args,0),argument(args,1));
    if(mystique_on&&boss_mystique::in_scope())boss_mystique::on_sequence(argument(args,0),argument(args,1));
    if(shadowking_on&&boss_shadowking::in_scope())boss_shadowking::on_sequence(argument(args,0),argument(args,1));
    if(magneto_on&&boss_magneto::in_scope())boss_magneto::on_sequence(argument(args,0),argument(args,1));
    return result;
}
void* __cdecl default_target_hook(void* args){
    if(mold_on)mold_controller::on_default_target(args);
    if(pyro_on&&boss_pyro::in_scope())boss_pyro::on_default_target(argument(args,0));
    if(mystique_on&&boss_mystique::in_scope())boss_mystique::on_default_target(argument(args,0));
    if(shadowking_on&&boss_shadowking::in_scope())boss_shadowking::on_default_target(argument(args,0));
    if(magneto_on&&boss_magneto::in_scope())boss_magneto::on_default_target(argument(args,0));
    if(blob_on&&boss_blob::in_scope())boss_blob::on_default_target(argument(args,0));
    return real_default_target(args);
}
void* __cdecl node_hook(void* args){
    if(mold_on)mold_controller::on_node(args);
    return real_node(args);
}
bool __cdecl brain_update(void* brain,u32* output){
    if(blob_on&&boss_blob::companion(brain,output))return true;
    if(mold_on&&mold_controller::brain(brain,output))return true;
    if(dopples_on&&boss_dopples::brain(brain,output))return true;
    if(avalanche_on&&boss_avalanche::brain(brain,output))return true;
    if(pyro_on&&boss_pyro::brain(brain,output))return true;
    if(mystique_on&&boss_mystique::brain(brain,output))return true;
    if(shadowking_on&&boss_shadowking::brain(brain,output))return true;
    if(magneto_on&&boss_magneto::brain(brain,output))return true;
    if(blob_on&&boss_blob::brain(brain,output))return true;
    return false;
}
__declspec(naked) void brain_stub(){
    __asm {
        pushfd
        pushad
        push edi
        push esi
        call brain_update
        add esp,8
        test al,al
        jz native_state
        popad
        popfd
        jmp dword ptr [brain_postprocess]
    native_state:
        popad
        popfd
        mov eax,[esi+8]
        cmp eax,040h
        jmp dword ptr [brain_resume]
    }
}
void read_list(){
    const auto list=ini::text(L"Game",L"Xml1Bosses").value_or("");
    list_requested=false;list_known=true;requested_bits=0;
    for(std::size_t i=0;i<list.size();){
        auto j=list.find(',',i);if(j==std::string::npos)j=list.size();
        const auto name=std::string_view(list).substr(i,j-i);
        if(!name.empty()){
            list_requested=true;
            const auto bit=xml1_capability_rules::boss_bit(name);
            if(!bit)list_known=false;else requested_bits|=bit;
        }
        i=j+1;
    }
}
}
bool requested(){return list_requested;}
xml1_capability_rules::registration capability(){
    const u32 installed=(boss_dopples::installed()?1u:0u)|(boss_avalanche::installed()?2u:0u)|
        (boss_pyro::installed()?4u:0u)|(boss_mystique::installed()?8u:0u)|(boss_shadowking::installed()?16u:0u)|(marrow_on?32u:0u)|
        (boss_magneto::installed()?64u:0u)|(boss_blob::installed()?128u:0u);
    return xml1_capability_rules::bosses(list_known,requested_bits,installed,boss_dopples::installed()&&boss_dopples::bossfull()?1u:0u);
}
void install(HMODULE game){
    read_list();
    if(list_requested&&!list_known)logger::write("xml1 bosses: [Game] Xml1Bosses names an unknown boss; rule 40 unverified");
    xml1_hook_transaction::patch beam;
    const bool mold=mold_controller::prepare(game,beam);
    const bool dopples=boss_dopples::prepare(game);
    const bool avalanche=boss_avalanche::prepare(game);
    const bool pyro=boss_pyro::prepare(game);
    std::vector<xml1_hook_transaction::patch> extras;
    const bool mystique=boss_mystique::prepare(game,extras);
    const bool shadowking=boss_shadowking::prepare(game);
    const bool magneto=boss_magneto::prepare(game,extras);
    const bool blob=boss_blob::prepare(game,extras);
    // ON content has no SPEC 36 protection substitute. A failed install cannot safely
    // run that encounter offline either: refuse startup; rebuilding OFF is the fallback.
    if((requested_bits&64)&&!magneto){logger::write("magneto brain: required controller unavailable; startup refused; rebuild without controller contract for SPEC 36 fallback");game_version::refuse_startup();}
    marrow_on=(requested_bits&32)&&reinterpret_cast<std::uintptr_t>(game)==limits_rules::image_base;
    if(marrow_on)for(const auto& g:boss_native_sites::marrow)if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex))marrow_on=false;
    if(marrow_on)logger::write("marrow brain: native backboss (0x529890 / 0x529a10), no replacement installed");
    if(!mold&&!dopples&&!avalanche&&!pyro&&!mystique&&!shadowking&&!magneto&&!blob)return;
    const auto table=at<u32>(reinterpret_cast<const void*>(0x49fe31),0);
    const auto sequence=table+(0x68adb8-0x68a908),node=table+(0x68ae58-0x68a908),spawn=table+(0x68ae88-0x68a908);
    real_sequence=at<script_t>(reinterpret_cast<const void*>(sequence),0);real_node=at<script_t>(reinterpret_cast<const void*>(node),0);
    real_default_target=at<script_t>(reinterpret_cast<const void*>(spawn),0);
    if(real_sequence!=fn<script_t>(0x4a4830)||real_node!=fn<script_t>(0x4a51a0)||real_default_target!=fn<script_t>(0x4a4e20)){
        if(mold)mold_controller::script_table_refused();
        if(dopples||avalanche||pyro||mystique||shadowking||magneto||blob)logger::write("xml1 bosses: active script table differs; no boss hooks enabled");
        if(dopples)boss_dopples::committed(false);
        if(avalanche)boss_avalanche::committed(false);
        if(pyro)boss_pyro::committed(false);
        if(mystique)boss_mystique::committed(false);
        if(shadowking)boss_shadowking::committed(false);
        if(magneto){boss_magneto::committed(false);game_version::refuse_startup();}
        if(blob)boss_blob::committed(false);
        return;
    }
    using xml1_hook_transaction::pointer_bytes;
    xml1_hook_transaction::patch brain_patch{0x5173f3,{0x8b,0x46,0x08,0x83,0xf8,0x40},{0xe9,0,0,0,0,0x90}};
    const u32 delta=static_cast<u32>(reinterpret_cast<std::uintptr_t>(&brain_stub))-0x5173f8;
    std::memcpy(brain_patch.after.data()+1,&delta,4);
    // Master Mold's order (its beam, the three script entries, the brain dispatch). The script entries serve Master
    // Mold and the request-driven bosses, independent of whether Mold is enabled. Dopples need dispatch only.
    std::vector<xml1_hook_transaction::patch> writes;
    if(mold)writes.push_back(beam);
    if(mold||avalanche||pyro||mystique||shadowking||magneto||blob){
        writes.push_back({sequence,pointer_bytes(real_sequence),pointer_bytes(&sequence_hook)});
        writes.push_back({node,pointer_bytes(real_node),pointer_bytes(&node_hook)});
        writes.push_back({spawn,pointer_bytes(real_default_target),pointer_bytes(&default_target_hook)});
    }
    writes.push_back(brain_patch);
    writes.insert(writes.end(),extras.begin(),extras.end());
    xml1_hook_transaction::memory memory;
    bool safe=true;
    const bool ok=xml1_hook_transaction::apply(memory,writes,&safe);
    mold_on=ok&&mold;dopples_on=ok&&dopples;avalanche_on=ok&&avalanche;
    pyro_on=ok&&pyro;mystique_on=ok&&mystique;shadowking_on=ok&&shadowking;
    magneto_on=ok&&magneto;blob_on=ok&&blob;
    if(mold)mold_controller::committed(ok,safe);
    if(dopples)boss_dopples::committed(ok);
    if(avalanche)boss_avalanche::committed(ok);
    if(pyro)boss_pyro::committed(ok);
    if(mystique)boss_mystique::committed(ok);
    if(shadowking)boss_shadowking::committed(ok);
    if(magneto){boss_magneto::committed(ok);if(!ok)game_version::refuse_startup();}
    if(blob)boss_blob::committed(ok);
    if(!ok&&!safe){game_version::refuse_startup();if(!mold)logger::write("xml1 bosses: rollback/protection verification failed; process startup refused");}
}
}

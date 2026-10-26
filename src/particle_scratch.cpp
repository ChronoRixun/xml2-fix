#include "particle_scratch.hpp"
#include "particle_scratch_sites.hpp"
#include "patch_transaction.hpp"
#include "ini.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include <atomic>
#include <cstring>
#include <set>
#include <vector>

namespace particle_scratch {
namespace {
using u32 = std::uint32_t;
using namespace particle_scratch_sites;
bool installed = false, requested = false;
void* caves = nullptr;
struct counter { std::atomic<unsigned> peak{0}, clamps{0}; };
std::array<counter,11> counters;
// Operations run on the game thread. Deduplicate across all eleven operations,
// retaining addresses for the session so a persistent overflowing array logs once.
std::set<u32> logged;
std::atomic<unsigned> logged_arrays{0};
u32 addr(const void* p) { return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p)); }
bool enabled(const std::optional<std::string>& key) { return key && *key!="0" && *key!="off"; }
void settle(bool ok) {
    gameplay_agreement::register_rule(gameplay_capabilities::particle_scratch_id,1,{ok?capacity:0,0,0,0},ok);
}
void abandon() {
    installed=false;
    if(caves) { VirtualFree(caves,0,MEM_RELEASE); caves=nullptr; }
    settle(false);
    logger::write("ParticleScratch: refused; retail bytes retained, rule 60 unverified");
}
void retain() {
    installed=false; settle(false); game_version::refuse_startup();
    logger::write("ParticleScratch: uncertified rollback; caves retained, startup refused");
}
int __cdecl clamp(u32 array, int count, unsigned operation) {
    auto& c=counters[operation];
    if(count>0 && static_cast<unsigned>(count)>c.peak.load()) c.peak.store(static_cast<unsigned>(count));
    if(count<=static_cast<int>(capacity)) return count;
    ++c.clamps;
    if(logged.insert(array).second) {
        ++logged_arrays;
        logger::write("ParticleScratch: array %08x count %d exceeds scratch 40; processing first 40 (operation %08x)",array,count,sites[operation].operation);
    }
    return capacity;
}
}
void require_agreement(HMODULE game) {
    if(enabled(ini::text(L"Limits",L"ParticleScratch"))) game_version::require_gameplay_agreement(game);
}
void contribute(patch_transaction::transaction& tx, HMODULE game) {
    const auto key=ini::text(L"Limits",L"ParticleScratch");
    if(!enabled(key)) return;
    requested=true;
    game_version::require_gameplay_agreement(game);
    if(*key!="40" || addr(game)!=0x400000) {
        logger::write("ParticleScratch: requested %s; requires 40 and retail image base 0x400000",key->c_str());
        abandon(); return;
    }
    caves=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!caves) { abandon(); return; }
    patch_transaction::contribution c; c.owner="ParticleScratch";
    for(unsigned i=0;i<sites.size();++i) {
        const auto& s=sites[i];
        const auto old=patch_transaction::bytes_of(s.hex);
        auto* code=static_cast<unsigned char*>(caves)+128*i;
        std::vector<unsigned char> bytes;
        const auto emit=[&](std::string_view hex) { auto b=patch_transaction::bytes_of(hex); bytes.insert(bytes.end(),b.begin(),b.end()); };
        const auto word=[&](u32 v) { for(unsigned n=0;n<4;++n) bytes.push_back(static_cast<unsigned char>(v>>(8*n))); };
        // Save array ECX before the native getParticleCount. Save its post-call
        // registers/flags and floating state around telemetry. Restore EAX as the
        // bounded count, then replay the displaced instruction at the native ESP.
        emit("51"); bytes.insert(bytes.end(),old.begin(),old.begin()+3);
        emit("9c608bec81ec1002000083e4f00fae0424"); // pushfd/pushad; align; fxsave
        emit("68"); word(i); emit("50ff7524e8");
        word(addr(reinterpret_cast<const void*>(&clamp))-(addr(code)+static_cast<u32>(bytes.size())+4));
        emit("83c40c89451c0fae0c248be5619d8d642404");
        bytes.insert(bytes.end(),old.begin()+3,old.end());
        emit("e9"); word(s.va+static_cast<u32>(old.size())-(addr(code)+static_cast<u32>(bytes.size())+4));
        std::memcpy(code,bytes.data(),bytes.size());
        const auto label="ParticleScratch "+patch_transaction::hex(s.va);
        c.guards.push_back({s.va,old,label});
        c.writes.push_back(patch_transaction::relocation(s.va,"e9",true,addr(code),static_cast<unsigned>(old.size())-5,label));
    }
    DWORD old_protect=0;
    if(!VirtualProtect(caves,4096,PAGE_EXECUTE_READ,&old_protect) || !FlushInstructionCache(GetCurrentProcess(),caves,4096)) { abandon(); return; }
    c.publish=[] { installed=true; settle(true); logger::write("ParticleScratch: rule 60 revision 1 installed {40,0,0,0}; eleven operations, clamp only"); };
    c.abandon=&abandon; c.retain=&retain;
    if(!tx.add(std::move(c))) abandon();
}
void install(HMODULE game) {
    patch_transaction::transaction tx; contribute(tx,game);
    if(tx.contributions()) { const auto result=tx.commit(); logger::write("ParticleScratch transaction: %s (%s)",patch_transaction::name(result),tx.report().c_str()); }
}
bool clamp_installed() { return installed; }
std::string telemetry_json() {
    std::string s="{\"ok\":true,\"installed\":"+std::string(installed?"true":"false")+",\"requested\":"+(requested?"true":"false")+",\"capacity\":40,\"logged_arrays\":"+std::to_string(logged_arrays.load())+",\"operations\":[";
    for(unsigned i=0;i<sites.size();++i) {
        if(i) s+=",";
        s+="{\"va\":"+std::to_string(sites[i].operation)+",\"peak\":"+std::to_string(counters[i].peak.load())+",\"clamps\":"+std::to_string(counters[i].clamps.load())+"}";
    }
    return s+"]}";
}
}

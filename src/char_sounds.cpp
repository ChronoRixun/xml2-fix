#include "char_sounds.hpp"
#include "char_sound_sites.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "patch_transaction.hpp"
#include "ini.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "log.hpp"
#include <array>
#include <atomic>
#include <cstring>

namespace char_sounds
{
namespace
{
using u32=std::uint32_t;
using namespace char_sound_sites;
alignas(16) std::array<unsigned char,capacity*stride> cache{};
bool installed=false,requested=false;
std::atomic<unsigned> peak{0},refusals{0},empty{0},allocations{0};
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
bool enabled(const std::optional<std::string>& key){return key&&*key!="16";}
void settle(bool verified)
{
    gameplay_agreement::register_rule(gameplay_capabilities::char_sounds_id,1,{127,0,0,0},verified);
}
void abandon(){installed=false;settle(false);logger::write("CharSounds: refused; stock bytes retained, rule 49 unverified");}
void retain(){installed=false;settle(false);game_version::refuse_startup();logger::write("CharSounds: uncertified rollback; backing retained, startup refused");}
// Allocation is cdecl. The sole actor-init call site passes stats and actor+0x444.
// Keep the native early return, key sharing, handle loading and return value.
bool __cdecl allocate(const void* stats,int* out)
{
    const bool ok=reinterpret_cast<bool(__cdecl*)(const void*,int*)>(0x438d20)(stats,out);
    ++allocations;
    if(!stats||!*(static_cast<const char*>(stats)+0x230))++empty;
    else if(!ok){++refusals;logger::write("CharSounds: allocation refused for nonempty sounddir; record-0 fallback");}
    unsigned live=0;
    for(unsigned i=0;i<capacity;++i)live+=(cache[i*stride+1]&1)!=0;
    if(live>peak.load())peak.store(live);
    return ok;
}
}
void require_agreement(HMODULE game)
{
    if(enabled(ini::text(L"Limits",L"CharSounds")))game_version::require_gameplay_agreement(game);
}
void contribute(patch_transaction::transaction& tx,HMODULE game)
{
    const auto key=ini::text(L"Limits",L"CharSounds");
    if(!enabled(key))return;
    requested=true;
    require_agreement(game);
    if(*key!="127"||address(game)!=0x400000||entity_capacity::installed().actors>=capacity)
    {
        logger::write("CharSounds: refused [Limits] CharSounds=%s, published actor capacity %u (requires 127 and actors < 127)",key->c_str(),entity_capacity::installed().actors);
        settle(false);return;
    }
    patch_transaction::contribution c;c.owner="CharSounds";
    for(const auto& s:sites)
    {
        auto bytes=patch_transaction::bytes_of(s.hex);
        const auto label="CharSounds "+patch_transaction::hex(s.va);
        c.guards.push_back({s.va,bytes,label});
        if(s.replacement==value::bound)bytes[s.offset]=capacity;
        else
        {
            const u32 v=address(cache.data())+(s.replacement==value::key?4:s.replacement==value::end?4+capacity*stride:0);
            std::memcpy(bytes.data()+s.offset,&v,4);
        }
        c.writes.push_back({s.va,std::move(bytes),label});
    }
    // Observer is separate from the eight capacity sites. Only this one caller
    // creates actor-held records; no sampling can miss an allocation or refusal.
    c.guards.push_back({0x4239be,patch_transaction::bytes_of("e85d530100"),"CharSounds allocation observer"});
    c.writes.push_back(patch_transaction::relocation(0x4239be,"e8",true,address(reinterpret_cast<void*>(&allocate)),0,"CharSounds allocation observer"));
    // Certify the constructors before executing them at publication. The old
    // static's CRT construction/destruction and its two base operands stay native.
    c.guards.push_back({0x4391c0,patch_transaction::bytes_of("568bf1578d7e088bcfc7460400000000"),"CharSounds record constructor"});
    c.guards.push_back({0x438d10,patch_transaction::bytes_of("568bf1e8d8ffffff8bc65ec3"),"CharSounds handle-array constructor"});
    c.guards.push_back({0x438cf0,patch_transaction::bytes_of("8bc1c780d000000000000000c3"),"CharSounds handle-array count"});
    c.regions.push_back({"CharSounds record constructor",0x4391c0,0x439215,patch_transaction::sha256_of("bae8ce7dea90b6c45ed73840f732941b39f500ee9ea776e04857434f69973904")});
    c.regions.push_back({"CharSounds array constructor",0x438d10,0x438d1c,patch_transaction::sha256_of("cd0a3933516bf6c7d261c1e7ff745d6f7bd1c6a6dd53f6b5ef99a883a4d3aff7")});
    c.regions.push_back({"CharSounds array count",0x438cf0,0x438cfd,patch_transaction::sha256_of("2829160f9d6bcc45511f0b41c077e781ef9b08f9c05d58a837791d024849da70")});
    c.publish=[] {
        cache.fill(0);
        for(unsigned i=0;i<capacity;++i)
            reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x4391c0)(cache.data()+i*stride,nullptr);
        peak=0;refusals=0;empty=0;allocations=0;installed=true;settle(true);
        // Builder feature gate: this marker accompanies rule 49's registration.
        logger::write("CharSounds: rule 49 revision 1 installed {127,0,0,0}; 127 records, 8 capacity sites, allocation observer; actors %u",entity_capacity::installed().actors);
    };
    c.abandon=&abandon;c.retain=&retain;
    if(!tx.add(std::move(c)))abandon();
}
std::string telemetry_json()
{
    // Only atomics cross from game thread to pipe thread; backing is never read
    // concurrently with retail allocation/release.
    return "{\"ok\":true,\"installed\":"+std::string(installed?"true":"false")+
        ",\"requested\":"+(requested?"true":"false")+",\"capacity\":"+std::to_string(installed?capacity:16)+
        ",\"peak\":"+std::to_string(peak.load())+",\"refusals\":"+std::to_string(refusals.load())+
        ",\"empty_sounddir_actors\":"+std::to_string(empty.load())+",\"allocations\":"+std::to_string(allocations.load())+"}";
}
}

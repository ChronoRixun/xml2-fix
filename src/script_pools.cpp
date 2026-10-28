#include "script_pools.hpp"
#include "script_pools_plan.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "ini_rules.hpp"
#include "log.hpp"
#include <atomic>

namespace script_pools
{
namespace
{
bool installed=false,requested=false,readable=false;
std::array<std::atomic<u32>,4> live{},peak{},peak_zone{};
std::array<std::atomic<u32>,5> refusals{},refusals_zone{};
std::array<char,65> last_zone{};
u32 compiling_script=0,logged_mask=0;
// The object pool's separate filename tree also had only 120 nodes, with an
// unchecked ring pop. Keep its native owner layout intact and use bounded DLL
// metadata for all 512 compiled objects. No script body or local moves here.
struct cached_script {std::array<char,256> key{};void* script=nullptr;};
std::array<cached_script,raised[2].capacity> cache{};
u32 cache_owner=0;
std::atomic<u32> cached_live{0},cached_peak{0},cache_refusals{0};
const char* names[]={"statements","values","compiled scripts","running contexts","compiler null values"};
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
bool enabled(const std::optional<std::string>& key)
{
    if(!key)return false;
    const auto value=ini_rules::value_text(*key);
    return value!="0"&&value!="620"&&value!="stock";
}
std::vector<patch_transaction::expectation> maker_guards(const std::vector<patch_transaction::write>& writes={})
{
    std::vector<patch_transaction::expectation> guards;
    constexpr std::array<u32,2> starts{0x4d6570,0x4d9445},caps{0x4d6578,0x4d9450};
    constexpr std::array<std::string_view,2> gaps{"33c0","83ec0833c0"};
    for(unsigned k=0;k<2;++k)
    {
        std::vector<std::uint8_t> bytes;
        for(const auto& s:script_pool_sites::sites)if(s.va==starts[k])bytes=patch_transaction::bytes_of(s.before);
        const auto gap=patch_transaction::bytes_of(gaps[k]);bytes.insert(bytes.end(),gap.begin(),gap.end());
        for(const auto& s:script_pool_sites::sites)if(s.va==caps[k])
        {
            const auto cap=patch_transaction::bytes_of(s.before);bytes.insert(bytes.end(),cap.begin(),cap.end());
        }
        for(const auto& w:writes)for(unsigned j=0;j<w.bytes.size();++j)
            if(w.va+j>=starts[k]&&w.va+j<starts[k]+bytes.size())bytes[w.va+j-starts[k]]=w.bytes[j];
        guards.push_back({starts[k],std::move(bytes),"ScriptPools published value maker"});
    }
    return guards;
}
auto published_makers=maker_guards();

void settle(bool verified)
{
    // The brief assigns a single profile capacity; the descriptor supplies the
    // other three capacities. An unsuccessful request poisons rule 59.
    gameplay_agreement::register_rule(gameplay_capabilities::script_pools_id,1,
        {verified?raised[0].capacity:0u,0,0,0},verified);
}
void __cdecl begin_script(u32 script)
{
    compiling_script=script;logged_mask=0;
}
void __cdecl refused(unsigned pool)
{
    if(pool>=refusals.size())return;
    ++refusals[pool];
    ++refusals_zone[pool];
    if(!(logged_mask&(1u<<pool)))
    {
        logged_mask|=1u<<pool;
        logger::write("ScriptPools: %s refused (script 0x%08X); native line/execute refusal retained",names[pool],compiling_script);
    }
}
void clear_cache()
{
    for(auto& entry:cache){entry.script=nullptr;entry.key[0]=0;}
    cache_owner=0;cached_live=0;
}
std::array<char,256> cache_key(const char* name)
{
    std::array<char,256> key{};
    if(name)std::strncpy(key.data(),name,key.size());
    key.back()=0;return key;
}
cached_script* find_cached(void* self,const char* name)
{
    if(!self||address(self)!=cache_owner||!name)return nullptr;
    const auto key=cache_key(name);
    const auto compare=reinterpret_cast<int(__cdecl*)(const char*,const char*)>(script_pool_sites::cache_compare);
    for(auto& entry:cache)
        if(entry.script&&compare(entry.key.data(),key.data())==0)return &entry;
    return nullptr;
}
void* __fastcall cache_lookup(void* self,void*,const char* name)
{
    const auto* entry=find_cached(self,name);return entry?entry->script:nullptr;
}
void __fastcall cache_insert(void* self,void*,const char* name,void* script)
{
    // These compiler globals are cleared by native 0x4d71f0 even on a cache hit.
    for(const u32 va:{0x787b74u,0x7879e4u,0x787aecu,0x787854u,0x787734u,0x787738u,0x78773cu})
        *reinterpret_cast<volatile u32*>(va)=0;
    if(!self||!name||!script||(cache_owner&&cache_owner!=address(self))){++cache_refusals;return;}
    if(!cache_owner)cache_owner=address(self);
    if(find_cached(self,name))return; // the first non-null registration wins
    for(auto& entry:cache)if(!entry.script)
    {
        entry.key=cache_key(name);entry.script=script;
        const auto n=++cached_live;if(n>cached_peak.load())cached_peak=n;
        return;
    }
    ++cache_refusals;
    logger::write("ScriptPools: compiled filename cache full (512); insertion refused");
}
void __fastcall cache_unload(void* self,void*,const char* name)
{
    auto* entry=find_cached(self,name);if(!entry)return;
    const u32 pool=address(self)+raised[2].base,script=address(entry->script);
    if(script<pool||script>=pool+raised[2].capacity*raised[2].stride||(script-pool)%raised[2].stride)
    {
        ++cache_refusals;return;
    }
    u32 root=0;std::memcpy(&root,entry->script,4);
    // Exactly the native unload's ownership sequence: release the statement
    // tree, then return the compiled object to its pointer pool.
    reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x4d8af0)(entry->script,nullptr,root);
    reinterpret_cast<void(__fastcall*)(void*,void*,u32)>(0x4d7c90)(reinterpret_cast<void*>(pool),nullptr,(script-pool)/raised[2].stride);
    entry->script=nullptr;entry->key[0]=0;--cached_live;
}
void __fastcall cache_reset(void* self,void*)
{
    // Preserve the original call at 0x4d7fc8 before clearing metadata. The rest
    // of native bulk reset still releases all four pools in its usual order.
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x468d40)(self,nullptr);
    clear_cache();
}
bool native_layout()
{
    for(const auto& s:script_pool_sites::sites)
    {
        const auto expected=patch_transaction::bytes_of(s.before);
        std::vector<std::uint8_t> actual(expected.size());
        if(!patch_transaction::read_memory(s.va,actual.data(),actual.size())||actual!=expected)return false;
    }
    return true;
}
}

const std::array<pool_layout,4>& descriptor(){return installed?raised:stock;}
void require_agreement(HMODULE game)
{
    if(address(game)==0x400000&&enabled(ini::text(L"Limits",L"ScriptPools")))
        game_version::require_gameplay_agreement(game);
}
bool value_makers_match()
{
    patch_transaction::contribution c;c.guards=published_makers;
    std::string why;return patch_transaction::verify_expectations(c,why);
}

void install(HMODULE game)
{
    if(address(game)!=0x400000)return;
    readable=native_layout()&&value_makers_match();
    const auto key=ini::text(L"Limits",L"ScriptPools");
    if(!enabled(key))return;
    requested=true;
    u32 owner=0;
    if(ini_rules::value_text(*key)!="x1"||!readable||
       !patch_transaction::read_memory(system_pointer,&owner,4)||owner)
    {
        settle(false);logger::write("ScriptPools: refused profile, code layout or existing owner; rule 59 unverified");return;
    }
    constexpr std::size_t code_size=16384;
    auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,code_size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!code){settle(false);return;}
    const cache_hooks hooks{address(reinterpret_cast<const void*>(&cache_lookup)),address(reinterpret_cast<const void*>(&cache_insert)),
        address(reinterpret_cast<const void*>(&cache_unload)),address(reinterpret_cast<const void*>(&cache_reset))};
    auto plan=make_plan(address(code),address(reinterpret_cast<const void*>(&refused)),address(reinterpret_cast<const void*>(&begin_script)),hooks);
    DWORD protection=0;
    if(plan.code.size()>code_size)
    {
        VirtualFree(code,0,MEM_RELEASE);settle(false);return;
    }
    std::memcpy(code,plan.code.data(),plan.code.size());
    if(!VirtualProtect(code,code_size,PAGE_EXECUTE_READ,&protection)||!FlushInstructionCache(GetCurrentProcess(),code,plan.code.size()))
    {
        VirtualFree(code,0,MEM_RELEASE);settle(false);return;
    }
    auto& c=plan.contribution;
    auto makers=maker_guards(c.writes);
    for(const auto& g:published_makers)c.guards.push_back(g);
    // Guard the allocation state inside the same transaction, as well as in
    // the read-only preflight. Nothing can construct the owner during DllMain.
    c.guards.push_back({system_pointer,{0,0,0,0},"ScriptPools owner not constructed"});
    c.publish=[makers=std::move(makers)]() mutable
    {
        published_makers.swap(makers); // prepared before the first write; publication cannot allocate
        clear_cache();cached_peak=0;cache_refusals=0;
        installed=true;readable=true;settle(true);
        logger::write("ScriptPools: rule 59 revision 1 installed {4096,0,0,0}; statements 4096, values 8192, compiled scripts 512, running contexts 128; appended owner size 0x%X",raised_size);
    };
    c.abandon=[code]{VirtualFree(code,0,MEM_RELEASE);settle(false);logger::write("ScriptPools: transaction refused; rule 59 unverified");};
    c.retain=[]{readable=false;settle(false);game_version::refuse_startup();logger::write("ScriptPools: rollback uncertified; caves retained, startup refused");};
    patch_transaction::transaction tx;
    if(!tx.add(std::move(c)))
    {
        VirtualFree(code,0,MEM_RELEASE);settle(false);return;
    }
    const auto result=tx.commit();
    logger::write("ScriptPools: %s (%s)",patch_transaction::name(result),tx.report().c_str());
}

void sample()
{
    if(!readable)return;
    std::array<char,65> zone{};
    if(patch_transaction::read_memory(0x72a758,zone.data(),64)&&std::strncmp(zone.data(),last_zone.data(),64)!=0)
    {
        if(last_zone[0])logger::write("ScriptPools zone %s: stmts peak %u, values peak %u, scripts peak %u, contexts peak %u; refusals %u/%u/%u/%u null %u",
            last_zone.data(),peak_zone[0].load(),peak_zone[1].load(),peak_zone[2].load(),peak_zone[3].load(),
            refusals_zone[0].load(),refusals_zone[1].load(),refusals_zone[2].load(),refusals_zone[3].load(),refusals_zone[4].load());
        last_zone=zone;
        for(auto& n:peak_zone)n=0;
        for(auto& n:refusals_zone)n=0;
    }
    u32 owner=0;
    if(!patch_transaction::read_memory(system_pointer,&owner,4))return;
    const auto& d=descriptor();
    for(unsigned i=0;i<4;++i)
    {
        u32 n=0;
        if(owner&&!patch_transaction::read_memory(owner+d[i].base+d[i].live,&n,4))continue;
        live[i]=n;
        if(n>peak[i].load())peak[i]=n;
        if(n>peak_zone[i].load())peak_zone[i]=n;
    }
    if(!installed)
    {
        u32 n=0;
        if(!owner||patch_transaction::read_memory(owner+0x2ef20,&n,4))
        {
            cached_live=n;if(n>cached_peak.load())cached_peak=n;
        }
    }
}
std::string telemetry_json()
{
    std::string out="{\"installed\":"+std::string(installed?"true":"false")+",\"requested\":"+(requested?"true":"false")+",\"readable\":"+(readable?"true":"false")+",\"refusal_observers\":"+(installed?"true":"false");
    constexpr const char* keys[]={"statements","values","scripts","contexts"};
    const auto& d=descriptor();
    for(unsigned i=0;i<4;++i)
        out+=",\""+std::string(keys[i])+"\":{\"cap\":"+std::to_string(d[i].capacity)+",\"live\":"+std::to_string(live[i].load())+",\"peak_zone\":"+std::to_string(peak_zone[i].load())+",\"peak_session\":"+std::to_string(peak[i].load())+",\"refused\":"+std::to_string(refusals[i].load())+",\"refused_zone\":"+std::to_string(refusals_zone[i].load())+"}";
    return out+",\"cached_scripts\":{\"cap\":"+std::to_string(installed?512:120)+",\"live\":"+std::to_string(cached_live.load())+
        ",\"peak_session\":"+std::to_string(cached_peak.load())+",\"refused\":"+std::to_string(cache_refusals.load())+"},\"null_values\":"+std::to_string(refusals[4].load())+"}";
}
std::string status()
{
    const auto& d=descriptor();
    return "script stmts "+std::to_string(live[0].load())+"/"+std::to_string(d[0].capacity)+
        " values "+std::to_string(live[1].load())+"/"+std::to_string(d[1].capacity)+
        " scripts "+std::to_string(live[2].load())+"/"+std::to_string(d[2].capacity)+
        " contexts "+std::to_string(live[3].load())+"/"+std::to_string(d[3].capacity)+
        " (refused "+std::to_string(refusals[0].load())+"/"+std::to_string(refusals[1].load())+
        "/"+std::to_string(refusals[2].load())+"/"+std::to_string(refusals[3].load())+
        "; null values "+std::to_string(refusals[4].load())+")";
}
}

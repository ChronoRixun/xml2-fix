#include "effect_pools.hpp"
#include "effect_vector_sites.hpp"
#include "primitive_def_sites.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <algorithm>
#include <cstring>
#include <span>

namespace effect_pools {
namespace {
using namespace effect_pools_rules;
struct state {descriptor description;u32 object;bool applied=false,readable=false;};
state primitive{describe(pool::primitive_defs,816),0},vectors{describe(pool::vectors,710),0x706680};
state& get(pool p){return p==pool::vectors?vectors:primitive;}
const char* name(pool p){return p==pool::vectors?"EffectVectors":"PrimitiveDefs";}
std::span<const site> sites(pool p){return p==pool::vectors?std::span<const site>(effect_vector_sites::sites):std::span<const site>(primitive_def_sites::sites);}
std::span<const guard> guards(pool p){return p==pool::vectors?std::span<const guard>(effect_vector_sites::guards):std::span<const guard>(primitive_def_sites::guards);}
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
void settle(pool p,u32 n,bool verified){
    gameplay_agreement::register_rule(p==pool::vectors?gameplay_capabilities::effect_vectors_id:gameplay_capabilities::primitive_defs_id,
        1,{n,0,0,0},verified);
}
void raise(pool p,u32 n){
    void* block=nullptr;
    if(p==pool::vectors)block=VirtualAlloc(nullptr,describe(p,n).size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(p==pool::vectors&&!block){settle(p,stock_capacity(p),false);logger::write("limits: %s allocation failed; rule unverified",name(p));return;}
    patch_transaction::contribution c;std::string why;
    if(!prepare(p,n,address(block),c,why)){
        if(block)VirtualFree(block,0,MEM_RELEASE);
        settle(p,stock_capacity(p),false);
        logger::write("limits: %s refused (%s); rule unverified",name(p),why.c_str());return;
    }
    c.publish=[p,n,block]{auto& s=get(p);s.description=describe(p,n);s.applied=true;s.readable=true;
        if(p==pool::vectors)s.object=address(block);
        settle(p,n,true);
        logger::write("limits: %s installed {%u,0,0,0}; %zu ledger operands; native lazy constructor and teardown retained",name(p),n,sites(p).size());};
    c.abandon=[p,block]{if(block)VirtualFree(block,0,MEM_RELEASE);settle(p,stock_capacity(p),false);};
    c.retain=[p]{settle(p,stock_capacity(p),false);game_version::refuse_startup();};
    patch_transaction::transaction tx;tx.add(std::move(c));
    if(tx.commit()!=patch_transaction::outcome::committed)
        logger::write("limits: %s refused (%s); rule unverified",name(p),tx.report().c_str());
}
}
bool prepare(pool p,u32 n,u32 object,patch_transaction::contribution& out,std::string& why){
    if(n<=stock_capacity(p)||n>maximum(p)||(p==pool::vectors&&!object)) {why="capacity or vector backing out of range";return false;}
    u32 built=0;
    if(!patch_transaction::read_memory(init_guard(p),&built,4)||(p==pool::vectors?(built&1):built)){
        why="pool already built or unreadable";return false;
    }
    out={};out.owner=name(p);const auto d=describe(p,n);
    for(const auto& s:sites(p)){
        const auto bytes=patch_transaction::bytes_of(s.hex);
        auto w=std::find_if(out.writes.begin(),out.writes.end(),[&](const auto& w){return w.va==s.va;});
        if(w==out.writes.end()){
            out.guards.push_back({s.va,bytes,name(p)});out.writes.push_back({s.va,bytes,name(p)});w=std::prev(out.writes.end());
        }
        const auto v=value(s.member,d,object);
        for(unsigned j=0;j<s.size;++j)w->bytes[s.offset+j]=static_cast<std::uint8_t>(v>>(j*8));
    }
    for(const auto& g:guards(p))out.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),std::string(g.what)});
    // Preserve other init-guard bits; require exactly the read state through commit.
    std::vector<std::uint8_t> bytes(4);std::memcpy(bytes.data(),&built,4);
    out.guards.push_back({init_guard(p),std::move(bytes),"pool not built"});
    return patch_transaction::verify_expectations(out,why);
}
const descriptor& layout(pool p){return get(p).description;}
bool installed(pool p){return get(p).applied;}
u32 object_address(pool p){
    const auto& s=get(p);if(!s.readable)return 0;
    if(p==pool::vectors)return s.object;
    u32 ptr=0;return patch_transaction::read_memory(owner(p),&ptr,4)?ptr:0;
}
void install(HMODULE game){
    const bool retail=address(game)==0x400000;
    for(const auto p:{pool::primitive_defs,pool::vectors}){
        const auto key=ini::text(L"Limits",p==pool::vectors?L"EffectVectors":L"PrimitiveDefs");
        const auto n=capacity(p,key?std::optional<std::string_view>(*key):std::nullopt);
        if(retail){
            patch_transaction::contribution stock;
            for(const auto& s:sites(p))stock.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),name(p)});
            for(const auto& g:guards(p))stock.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),std::string(g.what)});
            std::string why;get(p).readable=patch_transaction::verify_expectations(stock,why);
        }
        if(n>stock_capacity(p)){
            if(!retail){logger::write("limits: %s not in retail image; not installed",name(p));continue;}
            game_version::require_gameplay_agreement(game);raise(p,n);
        }else if(key&&*key!=std::to_string(stock_capacity(p)))
            logger::write("limits: %s must be %u..%u; stock retained",name(p),stock_capacity(p)+1,maximum(p));
    }
}
void require_agreement(HMODULE game){
    if(address(game)!=0x400000)return;
    for(const auto p:{pool::primitive_defs,pool::vectors}){
        const auto key=ini::text(L"Limits",p==pool::vectors?L"EffectVectors":L"PrimitiveDefs");
        if(capacity(p,key?std::optional<std::string_view>(*key):std::nullopt)>stock_capacity(p)){
            game_version::require_gameplay_agreement(game);return;
        }
    }
}
std::string status(){
    std::string result;
    for(const auto p:{pool::vectors,pool::primitive_defs}){
        if(!result.empty())result+="; ";result+=p==pool::vectors?"vectors ":"prim defs ";
        const auto object=object_address(p);u32 live=0;
        if(object&&patch_transaction::read_memory(object+layout(p).live,&live,4))result+=std::to_string(live)+"/"+std::to_string(layout(p).capacity);
        else result+="-";
    }return result;
}
}

#pragma once
#include "script_pools_layout.hpp"
#include "script_pool_sites.hpp"
#include "patch_transaction.hpp"
#include <cstring>

namespace script_pools
{
struct patch_plan
{
    patch_transaction::contribution contribution;
    std::vector<std::uint8_t> code;
};
struct cache_hooks {u32 lookup,insert,unload,reset;};
inline patch_plan make_plan(u32 code_base, u32 notify, u32 begin_script,cache_hooks cache)
{
    patch_plan p;
    p.contribution.owner="ScriptPools";
    p.contribution.guards.push_back({script_pool_sites::cache_compare,
        patch_transaction::bytes_of(script_pool_sites::cache_compare_before),"ScriptPools native filename comparison"});
    for (const auto& s:script_pool_sites::sites)
    {
        const auto label="ScriptPools "+patch_transaction::hex(s.va);
        p.contribution.guards.push_back({s.va,patch_transaction::bytes_of(s.before),label});
        auto bytes=patch_transaction::bytes_of(s.after);
        if (!s.cave) p.contribution.writes.push_back({s.va,std::move(bytes),label});
        else
        {
            const u32 start=code_base+static_cast<u32>(p.code.size());
            for (unsigned i=0;i<s.fixup_count;++i)
            {
                const auto& f=s.fixups[i];
                const u32 target=f.target==1?notify:f.target==2?begin_script:
                    f.target==3?cache.lookup:f.target==4?cache.insert:f.target==5?cache.unload:f.target==6?cache.reset:f.target;
                const u32 relative=target-(start+f.offset+4);
                std::memcpy(bytes.data()+f.offset,&relative,4);
            }
            p.code.insert(p.code.end(),bytes.begin(),bytes.end());
            p.contribution.writes.push_back(patch_transaction::relocation(s.va,"e9",true,start,
                static_cast<unsigned>(s.before.size()/2-5),label));
        }
    }
    return p;
}
}

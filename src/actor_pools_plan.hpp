#pragma once
#include "entity_capacity_b_sites.hpp"
#include "patch_transaction.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <span>

namespace actor_pools_plan
{
    using bytes = std::vector<std::uint8_t>;
    using address = std::uint32_t;
    inline void u32(bytes& b,address v) { for(unsigned i=0;i<4;++i)b.push_back(std::uint8_t(v>>(8*i))); }
    inline void put(bytes& b,unsigned at,unsigned n,address v) { for(unsigned i=0;i<n;++i)b.at(at+i)=std::uint8_t(v>>(8*i)); }
    inline void branch(bytes& b,address base,address target,std::uint8_t opcode=0xe9)
    { b.push_back(opcode);u32(b,target-(base+static_cast<address>(b.size())+4)); }
    struct plan { patch_transaction::contribution contribution; bytes code; };

    // Generated stores preserve their original source register/immediate, without changing flags or the stack.
    inline bytes clear_code(bytes instruction)
    {
        const auto modrm=instruction.at(1);
        const bool immediate=instruction[0]==0xc7;
        const unsigned old_mod=modrm>>6;
        address disp=0;
        if(old_mod==2) for(unsigned j=0;j<4;++j)disp|=address(instruction.at(2+j))<<(8*j);
        bytes out;
        for(unsigned w=0;w<4;++w)
        {
            out.push_back(instruction[0]);out.push_back(std::uint8_t((modrm&0x3f)|0x80));u32(out,disp+4*w);
            if(immediate)out.insert(out.end(),instruction.end()-4,instruction.end());
        }
        return out;
    }
    inline bytes team_code(address va,address base,address observer=0)
    {
        bytes b;
        if(va==0x50e1c2)
        {
            b={0x85,0xc0,0x0f,0x84}; u32(b,0); // null -> activation below
            b.insert(b.end(),{0x8b,0x56,0x10}); branch(b,base,0x50e1c9);
            put(b,4,4,static_cast<address>(b.size())-8);
            const auto borrow=[&](unsigned post,address returned)->unsigned{
                if(!observer)return 0;
                auto save=patch_transaction::bytes_of("9c60fc8bdc81ec1002000083e4f00fae042468");b.insert(b.end(),save.begin(),save.end());
                const auto fix=static_cast<unsigned>(b.size());u32(b,returned);auto args=patch_transaction::bytes_of("ff73046a");args.push_back(static_cast<std::uint8_t>(post));
                b.insert(b.end(),args.begin(),args.end());branch(b,base,observer,0xe8);
                auto restore=patch_transaction::bytes_of("83c40c0fae0c248be3619d");b.insert(b.end(),restore.begin(),restore.end());return fix;
            };
            b.insert(b.end(),{0x8b,0x46,0x10,0x6a,0,0x50,0x8b,0xcf});const auto fix=borrow(0,0);
            branch(b,base,0x41b5b0,0xe8);const auto returned=base+static_cast<address>(b.size());if(observer)put(b,fix,4,returned);borrow(1,returned);branch(b,base,0x50e21e);
            return b;
        }
        // Preserve incoming flags for the load/call sites; the original compare/add
        // produces the flags used by its native continuation.
        b={0x9c,0x85,0xc0,0x0f,0x84};u32(b,0);
        b.push_back(0x9d);
        if(va==0x50e217)b.insert(b.end(),{0x05,0x60,0x02,0,0,0x89,0x10});
        if(va==0x50e3c8)b.insert(b.end(),{0x39,0xb8,0x64,0x02,0,0});
        if(va==0x50e3e6)b.insert(b.end(),{0x8b,0x88,0x64,0x02,0,0});
        if(va==0x50e4aa)branch(b,base,0x525460,0xe8);
        const address resume=va==0x50e217?0x50e21e:va==0x50e3c8?0x50e3ce:va==0x50e3e6?0x50e3ec:0x50e4af;
        branch(b,base,resume);
        put(b,5,4,static_cast<address>(b.size())-9);
        b.push_back(0x9d);
        branch(b,base,va==0x50e217?0x50e21e:va==0x50e4aa?0x50e4af:0x50e3f4);
        return b;
    }
    inline bytes observed_compare(address va,address base,address block,address observer,const bytes& cmp)
    {
        bytes b{0x9c,0x60}; // preserve every register and incoming flags
        unsigned pool=0;
        if(va==0x49684d) { b.insert(b.end(),{0x8b,0x80});u32(b,0x33b80); }
        else if(va==0x4b6ab0) { pool=1;b.insert(b.end(),{0x8b,0x81});u32(b,0x24fc); }
        else { pool=2;b.push_back(0xa1);u32(b,block+0x3670); }
        b.push_back(0x50);b.push_back(0x6a);b.push_back(std::uint8_t(pool));
        branch(b,base,observer,0xe8);
        b.insert(b.end(),{0x83,0xc4,8,0x61,0x9d});
        b.insert(b.end(),cmp.begin(),cmp.end());branch(b,base,va+static_cast<address>(cmp.size()));
        return b;
    }
    inline std::optional<plan> make(address block,address caves,address observer,
                                   std::span<const entity_capacity_b_sites::site> rows=entity_capacity_b_sites::sites,
                                   bool core=false,address event_observer=0)
    {
        using namespace entity_capacity_b_sites;
        // A partial/corrupt contribution must never install a subset of the stage.
        if(!block || !caves || !observer || rows.size()!=sites.size())return std::nullopt;
        for(std::size_t n=0;n<rows.size();++n)
        {
            const auto& a=rows[n];const auto& b=sites[n];
            if(a.va!=b.va||a.pool!=b.pool||a.role!=b.role||a.field!=b.field||a.offset!=b.offset||a.size!=b.size||a.value!=b.value||a.before!=b.before)return std::nullopt;
        }
        plan result;result.contribution.owner="ActorPools";
        std::map<address,std::vector<const site*>> grouped;
        for(const auto& s:rows)grouped[s.va].push_back(&s);
        for(const auto& [va,roles]:grouped)
        {
            auto before=patch_transaction::bytes_of(roles[0]->before),after=before;
            bool clear=false,team=false;
            for(const auto* s:roles)
            {
                if(s->before!=roles[0]->before)return std::nullopt;
                if(s->role=="clear")clear=true;
                else if(s->role=="guard")team=true;
                else put(after,s->offset,s->size,s->value+(s->role=="absolute"?block:0));
            }
            const bool observed=va==0x49684d||va==0x4b6ab0||va==0x499b38;
            if(clear||team||observed)
            {
                const auto entry=caves+static_cast<address>(result.code.size());
                bytes code=team?team_code(va,entry,core?event_observer:0):observed?observed_compare(va,entry,block,observer,after):clear_code(after);
                if(clear)branch(code,entry,va+static_cast<address>(before.size()));
                result.code.insert(result.code.end(),code.begin(),code.end());
                after=patch_transaction::relocation(va,"e9",true,entry,static_cast<unsigned>(before.size())-5,"cave").bytes;
            }
            result.contribution.guards.push_back({va,before,std::string(roles[0]->pool)+" "+std::string(roles[0]->field)});
            result.contribution.writes.push_back({va,after,result.contribution.guards.back().what});
        }
        for(const auto& r:regions)result.contribution.regions.push_back({"ActorPools owner "+patch_transaction::hex(r.lo),r.lo,r.hi,patch_transaction::sha256_of(r.hash)});
        // W11: a controller slot is reset immediately after the native FIFO pop,
        // before any of the six brain constructors. The callback is empty until
        // NpcPowerRule has published its data block.
        if(!core) {
            const auto entry=caves+static_cast<address>(result.code.size());
            auto code=patch_transaction::bytes_of(controller_reset.before);
            code.insert(code.end(),{0x9c,0x60,0x50,0x6a,3});
            branch(code,entry,observer,0xe8);
            code.insert(code.end(),{0x83,0xc4,8,0x61,0x9d});
            branch(code,entry,controller_reset.va+static_cast<address>(controller_reset.before.size()/2));
            result.code.insert(result.code.end(),code.begin(),code.end());
            result.contribution.guards.push_back({controller_reset.va,patch_transaction::bytes_of(controller_reset.before),"W11 controller FIFO pop"});
            result.contribution.writes.push_back(patch_transaction::relocation(controller_reset.va,"e9",true,entry,2,"W11 controller reset"));
            const auto& r=controller_reset_region;
            result.contribution.regions.push_back({"W11 controller FIFO",r.lo,r.hi,patch_transaction::sha256_of(r.hash)});
        }
        if(result.code.size()>(core?8192u:4096u))return std::nullopt;
        return result;
    }
}

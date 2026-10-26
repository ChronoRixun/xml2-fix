#include "npc_power_rules.hpp"
#define npc_power_rules npc_power_beta7
#include "npc_power_rules_beta7.hpp"
#undef npc_power_rules
#include "gameplay_capabilities.hpp"
#define gameplay_capabilities gameplay_capabilities_beta7
#include "gameplay_capabilities_beta7.hpp"
#undef gameplay_capabilities
#include <cassert>
#include <cstdio>

int main()
{
    const auto stock=actor_ai::stock;
    constexpr std::uint32_t base=0x20000000;
    assert(npc_power_rules::block(base,stock)==npc_power_beta7::block(base));
    for(const auto* s:npc_power_rules::sites)assert(actor_ai::guard(s->va,s->hex,stock)==s->hex);
    for(const auto va:{0x525160u,0x525302u})
    {
        const auto before=va==0x525160?"568b74240885f6744c8b81a0c400008d":"c70510ba7800bc5c6900";
        assert(actor_ai::guard(va,before,stock)==before);
    }
    assert(stock.cai_object==0x78ba10&&stock.primary_base==0x78ba14&&stock.controllers==32);
    std::puts("PASS T5: complete NpcPowerRule code page and all CAI guards equal beta-7 literals byte-for-byte");
    auto raised=stock;raised.stage=2;raised.cai_object=0x21000000;raised.primary_base=raised.cai_object+4;
    raised.controllers=96;raised.mask_offset_obj=0x24db0;
    const auto l=npc_power_rules::layout_at(base,raised);
    assert(l.react>=l.next+4*96&&l.react_power>=l.react+4*96&&l.react_attack>=l.react_power+4*96);
    assert(l.react_attack+4*96<=base+npc_power_rules::block_size);
    assert(actor_ai::guard(0x525160,"568b74240885f6744c8b81a0c40000",raised)=="568b74240885f6744c8b81b04d0200");
    const auto check=[&](const auto& b,unsigned base_at,unsigned stride_at,unsigned count_at)
    {
        std::uint32_t v=0;std::memcpy(&v,b.data()+base_at,4);assert(v==raised.primary_base+0x4c8);
        std::memcpy(&v,b.data()+stride_at,4);assert(v==raised.controller_stride);
        assert(b[count_at]==96);
    };
    for(const auto b:{npc_power_rules::cont_bridge(l),npc_power_rules::start_bridge(l),npc_power_rules::react_cont_bridge(l),npc_power_rules::pre_start_bridge(l)})check(b,3,0xc,0x18);
    check(npc_power_rules::power_front_bridge(l),4,0x11,0x21);
    check(npc_power_rules::attack_front_bridge(l),4,0x11,0x21);
    std::puts("PASS descriptor: all six caves bind base/stride/count; four 96-entry tables fit writable page");
    using namespace gameplay_capabilities;
    record released,on,same,failed,other;
    assert(on.add(actor_ai_id,1,{32,4,124u|(124u<<16),1},true));
    assert(same.add(actor_ai_id,1,{32,4,124u|(124u<<16),1},true));
    assert(other.add(actor_ai_id,1,{32,4,120u|(120u<<16),1},true));
    assert(!failed.add(actor_ai_id,1,{32,4,124u|(124u<<16),1},false));
    assert(released.finalize()&&on.finalize()&&same.finalize()&&other.finalize()&&!failed.finalize());
    assert(on.compare(same.bytes())==result::match);
    difference d{};
    assert(on.compare(released.bytes(),&d)==result::entries);
    char message[256]{};on.describe(d.buckets,d.local_only,true,message,sizeof message);assert(std::strstr(message,"44 ActorAI"));
    assert(released.compare(on.bytes(),&d)==result::entries);
    released.describe(d.buckets,d.local_only,true,message,sizeof message);assert(std::strstr(message,"44 ActorAI"));
    assert(on.compare(other.bytes(),&d)==result::value);
    assert((d.buckets&(1ull<<bucket_of(actor_ai_id)))!=0);
    assert(!failed.ready());
    gameplay_capabilities_beta7::record beta7;
    assert(beta7.finalize());
    assert(on.compare(beta7.bytes())==result::entries);
    gameplay_capabilities_beta7::difference old_difference;
    assert(beta7.compare(on.bytes(),&old_difference)==gameplay_capabilities_beta7::result::entries);
    beta7.describe(old_difference.buckets,old_difference.local_only,true,message,sizeof message);
    assert(std::strstr(message,"44 a rule this PC does not have"));
    std::puts("PASS T10: requested-only ActorAI values; matching peers accepted, mismatches/released peers refused both ways by name");
}

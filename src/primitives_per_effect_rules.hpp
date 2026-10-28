#pragma once
#include "effect_templates_rules.hpp"

namespace primitives_per_effect_rules {
using namespace effect_templates_rules;
constexpr u32 native_capacity=10, maximum=32, spill_columns=22;
constexpr unsigned saved(unsigned reg){return (7-reg)*4;}
// PUSHAD slots, indexed by the x86 register number.
// EAX ECX EDX EBX ESP EBP ESI EDI -> 28 24 20 16 12 8 4 0.
inline void load(std::vector<std::uint8_t>& b,unsigned dest,unsigned source){
    b.insert(b.end(),{0x8b,static_cast<std::uint8_t>(0x44+dest*8),0x24,static_cast<std::uint8_t>(saved(source))});
}
inline void store(std::vector<std::uint8_t>& b,unsigned dest){b.insert(b.end(),{0x89,0x44,0x24,static_cast<std::uint8_t>(saved(dest))});}
inline void row_address(std::vector<std::uint8_t>& b,u32 base,u32 spill){
    // EAX = definition. Divide its exact unsigned byte difference by 0x44,
    // using the same multiply/high-shift scheme as the native pointer releases.
    b.insert(b.end(),{0x8b,0x15});put32(b,owner_pointer); // mov edx,[owner]
    b.insert(b.end(),{0x2b,0xc2,0x2d});put32(b,base); // sub eax,edx; sub eax,base
    b.push_back(0xba);put32(b,0xf0f0f0f1);b.insert(b.end(),{0xf7,0xe2,0xc1,0xea,0x06,0x6b,0xc2,0x58,0x05});put32(b,spill);
}
inline void cell(std::vector<std::uint8_t>& b,unsigned base_reg,unsigned index_reg,bool list,u32 base,u32 spill){
    b.insert(b.end(),{0x9c,0x60});load(b,0,base_reg);load(b,1,index_reg);
    b.insert(b.end(),{0x83,0xf9,0x0a,0x0f,0x82});const auto native=b.size();put32(b,0);
    if(list)b.insert(b.end(),{0x83,0xe8,0x04});
    row_address(b,base,spill);
    b.insert(b.end(),{0x8d,0x44,0x88,0xd8,0xe9});const auto done=b.size();put32(b,0);
    const auto native_start=b.size();
    b.insert(b.end(),{0x8d,0x44,0x88,static_cast<std::uint8_t>(list?0:4)});
    const u32 n=static_cast<u32>(native_start-native-4),d=static_cast<u32>(b.size()-done-4);
    std::memcpy(b.data()+native,&n,4);std::memcpy(b.data()+done,&d,4);
}
inline plan plan_for(u32 capacity,const effect_templates::descriptor& d,u32 cave,u32 spill){
    plan p;p.patches.owner="PrimitivesPerEffect";
    if(capacity==10)return p;
    if(capacity<11||capacity>32||d.stride!=68||d.capacity<220||d.capacity>2048)return p;
    const auto expected=descriptor_for(layout_for(d.capacity));
    if(d.definition_base_offset!=expected.definition_base_offset||d.mask_offset!=expected.mask_offset)return p;
    auto& c=p.patches;auto& b=p.code;
    c.guards.push_back({owner_pointer,{0,0,0,0},"effect owner must not exist"});
    for(const auto& g:effect_template_sites::primitive_guards)c.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),"primitive native metadata"});
    c.guards.push_back({0x5615b0,patch_transaction::bytes_of(effect_template_sites::primitive_helper_hex),"shared ten-slot helper read only"});
    c.writes.push_back({0x4188b7,{static_cast<std::uint8_t>(capacity)},"primitive admission bound"});
    for(const auto& s:effect_template_sites::elements){
        const auto target=cave+static_cast<u32>(b.size());
        c.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),"primitive element"});
        c.writes.push_back(patch_transaction::relocation(s.va,"e9",true,target,s.end-s.va-5,"primitive spill element"));
        if(s.name=="append"){
            cell(b,6,1,false,d.definition_base_offset,spill);store(b,2);
            b.insert(b.end(),{0x61,0x9d,0x85,0xd2,0x89,0x02});
        }else if(s.name=="classify"){
            cell(b,1,2,true,d.definition_base_offset,spill);b.insert(b.end(),{0x8b,0x00});store(b,0);
            b.insert(b.end(),{0x61,0x9d,0x83,0x78,0x08,0x04});
        }else if(s.name=="spawn_cursor"){
            // This spawner reads [EDI] four more times in its inner loop.
            // Rebase the cursor itself, so all those reads see the selected cell.
            cell(b,0,1,true,d.definition_base_offset,spill);store(b,7);b.insert(b.end(),{0x8b,0x00});store(b,3);
            b.insert(b.end(),{0x61,0x9d,0x6a,0x00,0x83,0xc3,0x14});
        }else if(s.name=="spawn"){
            cell(b,2,7,true,d.definition_base_offset,spill);b.insert(b.end(),{0x8b,0x00});store(b,6);
            b.insert(b.end(),{0x61,0x9d,0x8b,0x46,0x40});
        }else if(s.name=="release"){
            cell(b,6,7,true,d.definition_base_offset,spill);b.insert(b.end(),{0x8b,0x00});store(b,0);
            b.insert(b.end(),{0x61,0x9d,0x50,0xe8});put32(b,0x418860-(cave+static_cast<u32>(b.size())+4));
        }else if(s.name=="flags"){
            cell(b,0,1,true,d.definition_base_offset,spill);b.insert(b.end(),{0x8b,0x00});store(b,7);
            b.insert(b.end(),{0x61,0x9d,0x85,0x77,0x44});
        }
        jump(b,cave,s.end);
    }
    const auto reset=cave+static_cast<u32>(b.size());
    b.insert(b.end(),{0x9c,0x60,0x8d,0x46,0xfc});row_address(b,d.definition_base_offset,spill);
    b.insert(b.end(),{0x8b,0xf8,0xb9});put32(b,spill_columns);
    b.insert(b.end(),{0x33,0xc0,0xfc,0xf3,0xab,0x61,0x9d,0xc7,0x46,0x2c,0,0,0,0});jump(b,cave,0x4189e2);
    c.writes.push_back(patch_transaction::relocation(0x4189db,"e9",true,reset,2,"release clears spill row"));
    const auto helper=cave+static_cast<u32>(b.size());
    auto copy=patch_transaction::bytes_of(effect_template_sites::primitive_helper_hex);
    for(const auto off:effect_template_sites::primitive_bounds)copy[off]=static_cast<std::uint8_t>(capacity);
    b.insert(b.end(),copy.begin(),copy.end());
    c.writes.push_back(patch_transaction::relocation(0x4186f3,"e9",true,helper,0,"definition destructor helper copy"));
    c.writes.push_back(patch_transaction::relocation(0x4189d5,"e8",true,helper,0,"definition release helper copy"));
    return p;
}
}

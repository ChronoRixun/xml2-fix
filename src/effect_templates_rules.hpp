#pragma once
#include "effect_template_layout.hpp"
#include "effect_template_sites.hpp"
#include "limits_rules.hpp"
#include "patch_transaction.hpp"
#include <map>
#include <optional>
#include <string>

namespace effect_templates_rules {
using u32=std::uint32_t;
constexpr u32 owner_pointer=0x6f3c3c;
struct pool {u32 ring,write,read,free,bitmap,live,values,size;};
struct definition_pool {u32 bitmap_a,ring,write,read,free,bitmap_b,live,ids,mask,shift,size;};
struct owner_layout {u32 n,words; pool files; definition_pool defs; u32 base,size;};
constexpr owner_layout layout_for(u32 n) {
    owner_layout a{}; a.n=n; a.words=(n+31)/32;
    auto& f=a.files; f.ring=n*76+4; f.write=f.ring+(n+1)*4;
    f.read=f.write+4; f.free=f.write+8; f.bitmap=f.write+12;
    f.live=f.bitmap+a.words*4; f.values=f.live+4; f.size=f.values+(n+1)*4;
    auto& d=a.defs; d.bitmap_a=n*68; d.ring=d.bitmap_a+a.words*4;
    d.write=d.ring+(n+1)*4; d.read=d.write+4; d.free=d.write+8;
    d.bitmap_b=d.write+12; d.live=d.bitmap_b+a.words*4; d.ids=d.live+4;
    d.mask=d.ids+n*4; d.shift=d.mask+4; d.size=d.mask+8;
    a.base=12+f.size; a.size=a.base+d.size; return a;
}
constexpr effect_templates::descriptor descriptor_for(const owner_layout& a) {return {a.base,a.defs.mask,68,a.n};}
inline u32 field(const owner_layout& a,std::string_view key) {
    const auto& f=a.files; const auto& d=a.defs;
    const std::pair<std::string_view,u32> fields[]{
        {"file_ring",f.ring},{"file_write",f.write},{"file_read",f.read},{"file_free",f.free},
        {"file_bitmap",f.bitmap},{"file_live",f.live},{"file_values",f.values},
        {"def_bitmap_a",d.bitmap_a},{"def_ring",d.ring},{"def_write",d.write},{"def_read",d.read},
        {"def_free",d.free},{"def_bitmap_b",d.bitmap_b},{"def_live",d.live},{"def_ids",d.ids},
        {"def_mask",d.mask},{"def_shift",d.shift},{"owner_def",a.base},{"owner_mask",a.base+d.mask},
        {"owner_live",a.base+d.live},{"owner_values",12+f.values},{"owner_file_live",12+f.live},
        {"owner_scope",a.base+64},{"owner_size",a.size},{"ring_write",(a.n+1)*4},
        {"ring_read",(a.n+2)*4},{"ring_free",(a.n+3)*4},{"capacity",a.n},{"last",a.n-1},{"words",a.words}};
    for(const auto& [k,v]:fields)if(k==key)return v;
    return ~0u;
}
struct setting {int value; bool requested,valid;};
inline setting parse(const std::optional<std::string>& text,int stock,int maximum) {
    if(!text)return {stock,false,true};
    const auto n=limits_rules::parse_count(*text);
    if(!n||*n<stock||*n>maximum)return {stock,true,false};
    return {*n,*n!=stock,true};
}
inline void put32(std::vector<std::uint8_t>& b,u32 v) {for(int i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(v>>(i*8)));}
inline void jump(std::vector<std::uint8_t>& b,u32 base,u32 target) {b.push_back(0xe9);put32(b,target-(base+static_cast<u32>(b.size())+4));}
struct plan {patch_transaction::contribution patches; std::vector<std::uint8_t> code; u32 find_next=0;};
inline plan plan_for(u32 n,u32 cave,u32 model_target=0) {
    plan p; p.patches.owner="EffectFiles";
    if(n==220)return p;
    if(n<221||n>2048)return p;
    const auto a=layout_for(n);
    std::map<u32,std::vector<std::uint8_t>> changed;
    for(const auto& s:effect_template_sites::operands) {
        auto [it,inserted]=changed.try_emplace(s.va,patch_transaction::bytes_of(s.hex));
        if(inserted)p.patches.guards.push_back({s.va,it->second,"effect layout operand"});
        const auto value=field(a,s.field);
        if(value==~0u)return {};
        std::memcpy(it->second.data()+s.offset,&value,4);
    }
    for(auto& [va,b]:changed)p.patches.writes.push_back({va,std::move(b),"effect layout"});
    for(const auto& s:effect_template_sites::clears) {
        const u32 dest=cave+static_cast<u32>(p.code.size());
        p.patches.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),"effect bitmap clear"});
        p.patches.writes.push_back(patch_transaction::relocation(s.va,"e9",true,dest,s.end-s.va-5,"effect bitmap clear"));
        auto& b=p.code;
        b.insert(b.end(),{0x9c,0x50,0x51,0x57,0x8b,static_cast<std::uint8_t>(0xf8+s.base),0x8b,static_cast<std::uint8_t>(0xc0+s.zero),0xb9});
        put32(b,a.words); b.insert(b.end(),{0xfc,0xf3,0xab,0x5f,0x59,0x58,0x9d}); jump(b,cave,s.end);
    }
    p.find_next=cave+static_cast<u32>(p.code.size());
    auto copy=patch_transaction::bytes_of(effect_template_sites::find_next_hex);
    for(const unsigned off:{5,15,60,137,144})std::memcpy(copy.data()+off,&n,4);
    p.code.insert(p.code.end(),copy.begin(),copy.end());
    p.patches.guards.push_back({0x403d40,patch_transaction::bytes_of(effect_template_sites::find_next_hex),"shared findNext read only"});
    p.patches.guards.push_back({0x4053c4,patch_transaction::bytes_of("e877e9ffff"),"effect findNext call"});
    p.patches.writes.push_back(patch_transaction::relocation(0x4053c4,"e8",true,p.find_next,0,"effect findNext copy"));
    const auto model=model_target ? patch_transaction::relocation(0x571724,"e8",true,model_target,0,"").bytes : patch_transaction::bytes_of("e81726e9ff");
    p.patches.guards.push_back({0x571724,model,"ModelPools published findNext call"});
    p.patches.guards.push_back({owner_pointer,{0,0,0,0},"effect owner must not exist"});
    return p;
}
}

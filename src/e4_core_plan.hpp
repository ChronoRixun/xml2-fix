#pragma once
#include "e4_core_sites.hpp"
#include "e4_core_state.hpp"
#include "patch_transaction.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string_view>

namespace e4_core_plan {
using u32=std::uint32_t;using bytes=std::vector<std::uint8_t>;
struct targets {u32 available,begin,actor_pop,team_pop,team_init,team_free,team_reset,registration;};
struct plan {patch_transaction::contribution contribution;bytes code;};
inline void word(bytes& b,u32 n){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
inline void put(bytes& b,unsigned off,unsigned size,u32 n){for(unsigned i=0;i<size;++i)b.at(off+i)=static_cast<std::uint8_t>(n>>(8*i));}
inline void hex(bytes& b,const char* s){auto x=patch_transaction::bytes_of(s);b.insert(b.end(),x.begin(),x.end());}
inline void jump(bytes& b,u32 base,u32 to,unsigned op=0xe9){b.push_back(static_cast<std::uint8_t>(op));word(b,to-base-static_cast<u32>(b.size())-4);}
inline bytes notify(u32 base,u32 callback,bool admission){
    bytes b;hex(b,"9c60fc8bdc81ec1002000083e4f00fae0424");
    if(admission)hex(b,"ff7304"); // original ESI = allocator
    else hex(b,"8b430cff7008"); // original entry [ESP+4] = actor
    jump(b,base,callback,0xe8);hex(b,"83c404");
    if(admission)hex(b,"89431c"); // return into saved EAX
    hex(b,"0fae0c248be3619d");return b;
}
inline bytes clear(const bytes& instruction){
    // Constructor/reset stores use disp32; destructor stores use [EDI].
    // Preserve their source and flags while widening to exactly three words.
    if(instruction.size()<2||(instruction[1]&7)==4)return {};
    const auto mod=instruction[1]>>6;if(mod!=0&&mod!=2)return {};
    u32 disp=0;if(mod==2)std::memcpy(&disp,instruction.data()+2,4);bytes b;
    for(unsigned i=0;i<3;++i){b.push_back(instruction[0]);b.push_back(static_cast<std::uint8_t>((instruction[1]&0x3f)|0x80));word(b,disp+i*4);if(instruction[0]==0xc7)b.insert(b.end(),instruction.end()-4,instruction.end());}
    return b;
}
inline std::optional<plan> make(u32 cai,u32 base,const targets& t,std::span<const e4_core_sites::edit> rows=e4_core_sites::edits){
    if(!cai||!base||!t.available||!t.begin||!t.actor_pop||!t.team_pop||!t.team_init||!t.team_free||!t.team_reset||!t.registration)return {};
    if(rows.size()!=e4_core_sites::edits.size())return {};
    for(std::size_t i=0;i<rows.size();++i){const auto& a=rows[i];const auto& b=e4_core_sites::edits[i];if(a.va!=b.va||a.offset!=b.offset||a.size!=b.size||a.value!=b.value||a.dynamic!=b.dynamic||std::string_view(a.hex)!=b.hex||std::string_view(a.role)!=b.role)return {};}
    plan p;p.contribution.owner="E4Core";auto& c=p.contribution;
    std::map<u32,bytes> before,after;
    for(const auto& e:rows){
        if(std::string_view(e.role)=="generic-object-live")continue; // Phase A owns these full instructions.
        if(!before.contains(e.va)){before[e.va]=patch_transaction::bytes_of(e.hex);after[e.va]=before[e.va];}
        put(after[e.va],e.offset,e.size,e.value+(e.dynamic?cai:0));
    }
    for(const auto& s:e4_core_sites::specials){
        const auto kind=std::string_view(s.kind);const auto label="E4 core "+patch_transaction::hex(s.va);
        auto old=patch_transaction::bytes_of(s.head);c.guards.push_back({s.va,old,label});
        c.regions.push_back({label,s.va,s.hi,patch_transaction::sha256_of(s.hash)});
        const u32 at=base+static_cast<u32>(p.code.size());u32 target=at;bytes code;
        if(kind=="clear"){
            code=clear(after.contains(s.va)?after.at(s.va):old);if(code.empty())return {};
            jump(code,at,s.hi);
        }else if(kind=="admit"){
            code=notify(at,t.available,true);hex(code,"84c00f84");word(code,0x461623-at-static_cast<u32>(code.size())-4);jump(code,at,0x4615b9);
        }else if(kind=="backstop"){
            code=notify(at,t.begin,false);code.insert(code.end(),old.begin(),old.end());jump(code,at,s.hi);
        }else if(kind=="actor-pop")target=t.actor_pop;
        else if(s.va==0x522a70)target=t.team_pop;
        else if(s.va==0x522ad0)target=t.team_init;
        else if(s.va==0x5251f0)target=t.team_free;
        else if(s.va==0x5234c0)target=t.team_reset;
        else if(s.va==0x523320)target=t.registration;
        else return {};
        p.code.insert(p.code.end(),code.begin(),code.end());
        c.writes.push_back(patch_transaction::relocation(s.va,kind=="actor-pop"?"e8":"e9",true,target,static_cast<unsigned>(old.size()-5),label));
        before.erase(s.va);after.erase(s.va);
    }
    for(const auto& [va,b]:after){c.guards.push_back({va,before.at(va),"E4 core operand"});c.writes.push_back({va,b,"E4 core operand"});}
    for(const auto& r:e4_core_sites::regions)c.regions.push_back({"E4 core owner",r.lo,r.hi,patch_transaction::sha256_of(r.hash)});
    c.guards.push_back({0x79889c,{0},"E4 CAI not initialized"});
    c.guards.push_back({0x718fbc,{0,0,0,0},"E4 allocator not created"});
    if(p.code.size()>8192)return {};
    return p;
}
}

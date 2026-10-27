#pragma once
#include "e4_observe_sites.hpp"
#include "patch_transaction.hpp"
#include <map>
#include <cstring>
#include <optional>
namespace e4_observe_plan {
using u32=std::uint32_t;
using bytes=std::vector<std::uint8_t>;
inline bytes stub(const e4_observe_sites::site& s,u32 base,u32 callback,unsigned id) {
    bytes b;
    const auto emit=[&](const char* h){auto x=patch_transaction::bytes_of(h);b.insert(b.end(),x.begin(),x.end());};
    const auto word=[&](u32 v){for(unsigned n=0;n<4;++n)b.push_back(static_cast<std::uint8_t>(v>>(8*n)));};
    const auto branch=[&](const char* op,u32 target){emit(op);word(target-(base+static_cast<u32>(b.size())+4));};
    const auto observe=[&](bool post){
        // Complete GPR/EFLAGS, DF and x87/MMX/XMM/MXCSR preservation. EBX
        // points at pushad's frame and is callee-saved by the cdecl callback.
        emit("9c60fc8bdc81ec1002000083e4f00fae04245368");word(id*2+(post?1:0));
        branch("e8",callback);emit("83c4080fae0c248be3619d");
    };
    // Compute branch labels before emitting. Internal branch targets execute the
    // same monitor as ordinary fallthrough into the target instruction.
    std::map<u32,u32> labels;
    constexpr unsigned observer_size=40;
    u32 pos=0;
    for(unsigned j=0;j<s.count;++j){const auto& i=s.code[j];labels[i.va]=base+pos;
        if(i.va==s.point&&(s.mode==0||s.mode==2))pos+=observer_size;
        pos+=i.op==1||i.op==2?5:i.op==3?6:static_cast<u32>(std::strlen(i.hex)/2);
        if(i.va==s.point&&(s.mode==1||s.mode==2))pos+=observer_size;
    }
    for(unsigned j=0;j<s.count;++j){const auto& i=s.code[j];
        if(i.va==s.point&&(s.mode==0||s.mode==2))observe(false);
        auto target=labels.contains(i.target)?labels.at(i.target):i.target;
        if(i.op==1)branch("e8",target);
        else if(i.op==2)branch("e9",target);
        else if(i.op==3){b.push_back(0x0f);b.push_back(static_cast<std::uint8_t>(0x80|i.cc));word(target-(base+static_cast<u32>(b.size())+4));}
        else emit(i.hex);
        if(i.va==s.point&&(s.mode==1||s.mode==2))observe(true);
    }
    if(b.size()!=pos)return {}; // refuses if the measured preservation sequence changes
    branch("e9",s.next);return b;
}
struct plan {patch_transaction::contribution contribution;bytes code;};
inline std::optional<plan> make(u32 base,u32 callback,const std::vector<patch_transaction::write>& prior, const std::vector<patch_transaction::write>& originals={},std::string* why=nullptr) {
    const auto fail=[&](std::string message)->std::optional<plan>{if(why)*why=std::move(message);return std::nullopt;};
    plan p;p.contribution.owner="E4Observe";
    for(unsigned id=0;id<e4_observe_sites::sites.size();++id){const auto& s=e4_observe_sites::sites[id];
        // Rule 16 intentionally removes this virtual call (ADD ESP,4). There
        // is no callback to borrow across; retain that certified replacement.
        bool elided=false;
        if(s.point==0x505011)for(const auto& w:prior)if(w.va==0x505011&&w.bytes==bytes{0x83,0xc4,0x04})elided=true;
        if(elided)continue;
        // A prior writer owns its span. Do not overwrite another rule's cave.
        for(const auto& w:prior)if(s.va<std::uint64_t(w.va)+w.bytes.size()&&w.va<s.next)return fail(std::string(s.name)+" overlaps "+w.what);
        auto at=base+static_cast<u32>(p.code.size());auto code=stub(s,at,callback,id);if(code.empty())return fail(std::string(s.name)+" stub length");
        p.code.insert(p.code.end(),code.begin(),code.end());
        p.contribution.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),s.name});
        p.contribution.writes.push_back(patch_transaction::relocation(s.va,"e9",true,at,s.next-s.va-5,s.name));
    }
    for(const auto& r:e4_observe_sites::regions){
        bytes expected(r.hi-r.lo);
        if(!patch_transaction::read_memory(r.lo,expected.data(),expected.size()))return fail("unreadable region "+patch_transaction::hex(r.lo));
        auto retail=expected;
        if(prior.size()!=originals.size())return std::nullopt;
        // Reverse only certified prior writes and verify the reconstructed retail
        // function hash. No whole native function body is embedded in the repo.
        for(std::size_t n=prior.size();n>0;--n){const auto& w=prior[n-1];const auto& old=originals[n-1];
            if(w.va!=old.va||w.bytes.size()!=old.bytes.size())return std::nullopt;
            for(unsigned j=0;j<w.bytes.size();++j)if(w.va+j>=r.lo&&w.va+j<r.hi){
                const auto at=w.va+j-r.lo;if(retail[at]!=w.bytes[j])return fail("prior write readback "+patch_transaction::hex(w.va+j));retail[at]=old.bytes[j];
            }
        }
        if(patch_transaction::sha256(retail.data(),retail.size())!=patch_transaction::sha256_of(r.hash))return fail("uncertified region change "+patch_transaction::hex(r.lo));
        p.contribution.regions.push_back({patch_transaction::hex(r.lo),r.lo,r.hi,patch_transaction::sha256(expected.data(),expected.size())});
    }
    return p;
}
}

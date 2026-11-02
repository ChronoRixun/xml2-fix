#pragma once
#include "e4_observe_sites.hpp"
#include "e4_unwind.hpp"
#include "patch_transaction.hpp"
#include <map>
#include <cstring>
#include <optional>
#include <span>
#include <set>
namespace e4_observe_plan {
using u32=std::uint32_t;
using bytes=std::vector<std::uint8_t>;
inline bytes stub(const e4_observe_sites::site& s,u32 base,u32 callback,unsigned id,std::span<const unsigned> group={}) {
    bytes b;
    const auto emit=[&](const char* h){auto x=patch_transaction::bytes_of(h);b.insert(b.end(),x.begin(),x.end());};
    const auto word=[&](u32 v){for(unsigned n=0;n<4;++n)b.push_back(static_cast<std::uint8_t>(v>>(8*n)));};
    const auto branch=[&](const char* op,u32 target){emit(op);word(target-(base+static_cast<u32>(b.size())+4));};
    const auto observe=[&](unsigned which,bool post){
        // Complete GPR/EFLAGS, DF and x87/MMX/XMM/MXCSR preservation. EBX
        // points at pushad's frame and is callee-saved by the cdecl callback.
        emit("9c60fc8bdc81ec1002000083e4f00fae04245368");word(which*2+(post?1:0));
        branch("e8",callback);emit("83c4080fae0c248be3619d");
    };
    const auto each=[&](u32 point,bool post,auto visit){
        const auto one=[&](const e4_observe_sites::site& probe,unsigned which){
            if(probe.point==point&&(probe.mode==2||probe.mode==(post?1u:0u)))visit(which,post);
        };
        if(group.empty())one(s,id);else for(auto which:group)one(e4_observe_sites::sites[which],which);
    };
    // Compute branch labels before emitting. Internal branch targets execute the
    // same monitor as ordinary fallthrough into the target instruction.
    std::map<u32,u32> labels;
    constexpr unsigned observer_size=40;
    u32 pos=0;
    for(unsigned j=0;j<s.count;++j){const auto& i=s.code[j];labels[i.va]=base+pos;
        each(i.va,false,[&](unsigned,bool){pos+=observer_size;});
        pos+=i.op==1||i.op==2?5:i.op==3?6:static_cast<u32>(std::strlen(i.hex)/2);
        each(i.va,true,[&](unsigned,bool){pos+=observer_size;});
    }
    for(unsigned j=0;j<s.count;++j){const auto& i=s.code[j];
        each(i.va,false,observe);
        auto target=labels.contains(i.target)?labels.at(i.target):i.target;
        if(i.op==1)branch("e8",target);
        else if(i.op==2)branch("e9",target);
        else if(i.op==3){b.push_back(0x0f);b.push_back(static_cast<std::uint8_t>(0x80|i.cc));word(target-(base+static_cast<u32>(b.size())+4));}
        else emit(i.hex);
        each(i.va,true,observe);
    }
    if(b.size()!=pos)return {}; // refuses if the measured preservation sequence changes
    branch("e9",s.next);return b;
}
// Wrap a complete native function using its callee-cleaned argument count.
// The original argument values/this pointer and complete returned CPU state are
// preserved. No native exception is handled: the stack SEH record observes unwind.
inline bytes boundary_stub(const e4_observe_sites::site& s,u32 base,u32 callback,unsigned id,u32 handler,unsigned arguments) {
    bytes b;
    const auto emit=[&](const char* h){auto v=patch_transaction::bytes_of(h);b.insert(b.end(),v.begin(),v.end());};
    const auto word=[&](u32 v){for(unsigned n=0;n<4;++n)b.push_back(static_cast<std::uint8_t>(v>>(n*8)));};
    const auto call=[&](u32 target){b.push_back(0xe8);word(target-(base+static_cast<u32>(b.size())+4));};
    const auto notify=[&](u32 code){emit("9c60fc8bdc81ec1002000083e4f00fae04245368");word(code);call(callback);emit("83c4080fae0c248be3619d");};
    emit("8da424b0ffffff5064a10000000089442404c7442408");word(handler);
    emit("c744240c");word(e4_unwind::magic);emit("c744241000000000c744243c00000000c7442444");word(s.point);
    emit("8d44240464a30000000058");notify(e4_unwind::begin);
    for(unsigned n=0;n<arguments/4;++n){emit("ff7424");b.push_back(static_cast<std::uint8_t>(sizeof(e4_unwind::frame)+arguments));}
    call(base+256);notify(e4_unwind::end);
    emit("508b44240464a300000000588da42450000000");
    if(arguments){emit("c2");b.push_back(static_cast<std::uint8_t>(arguments));b.push_back(0);}else emit("c3");
    if(b.size()>256)return {};
    b.resize(256,0xcc);auto body=stub(s,base+256,callback,id);if(body.empty())return {};
    b.insert(b.end(),body.begin(),body.end());return b;
}
struct plan {patch_transaction::contribution contribution;bytes code;};
inline std::optional<plan> make(u32 base,u32 callback,const std::vector<patch_transaction::write>& prior, const std::vector<patch_transaction::write>& originals={},std::string* why=nullptr,u32 unwind_handler=0,
    const std::vector<patch_transaction::write>& pending={},bool core=false,bool elide_power=false) {
    const auto fail=[&](std::string message)->std::optional<plan>{if(why)*why=std::move(message);return std::nullopt;};
    plan p;p.contribution.owner="E4Observe";std::set<u32> emitted;
    for(unsigned id=0;id<e4_observe_sites::sites.size();++id){const auto& s=e4_observe_sites::sites[id];
        if(!emitted.insert(s.va).second)continue;
        // The core's whole-function team-free replacement emits the same
        // destruction/provenance notifications around its native destructor.
        if(core&&(s.type==e4_observe_sites::kind::team_free||s.type==e4_observe_sites::kind::team_free_done))continue;
        std::vector<unsigned> group;for(unsigned n=0;n<e4_observe_sites::sites.size();++n)if(e4_observe_sites::sites[n].va==s.va)group.push_back(n);
        // Rule 16 intentionally removes this virtual call (ADD ESP,4). There
        // is no callback to borrow across; retain that certified replacement.
        bool elided=false;
        if(s.point==0x505011)for(const auto& w:prior)if(w.va==0x505011&&w.bytes==bytes{0x83,0xc4,0x04})elided=true;
        if(core&&elide_power&&s.point==0x505011)elided=true;
        if(elided)continue;
        // A prior writer owns its span. Do not overwrite another rule's cave.
        for(const auto& w:prior)if(s.va<std::uint64_t(w.va)+w.bytes.size()&&w.va<s.next)return fail(std::string(s.name)+" overlaps "+w.what);
        // Pending operand edits are emitted inside the observed instruction,
        // not written beneath the observation detour. Opcode/branch shape is
        // fixed by the audited observation ledger; only operands may compose.
        auto composed=s;std::vector<e4_observe_sites::instruction> instructions(s.code,s.code+s.count);
        std::vector<std::string> encoded;encoded.reserve(s.count);
        for(unsigned n=0;n<s.count;++n){auto b=patch_transaction::bytes_of(s.code[n].hex);const auto original=b;
            for(const auto& w:pending)for(unsigned j=0;j<w.bytes.size();++j)if(w.va+j>=instructions[n].va&&w.va+j<instructions[n].va+b.size())b[w.va+j-instructions[n].va]=w.bytes[j];
            if(b[0]!=original[0])return fail(std::string(s.name)+" pending opcode change");
            constexpr char digits[]="0123456789abcdef";std::string h;for(auto v:b){h+=digits[v>>4];h+=digits[v&15];}encoded.push_back(std::move(h));instructions[n].hex=encoded.back().c_str();
        }
        composed.code=instructions.data();
        auto at=base+static_cast<u32>(p.code.size());auto code=unwind_handler&&(s.type==e4_observe_sites::kind::factory_entry||s.type==e4_observe_sites::kind::step)?boundary_stub(composed,at,callback,id,unwind_handler,s.type==e4_observe_sites::kind::factory_entry?16:0):stub(composed,at,callback,id,group);if(code.empty())return fail(std::string(s.name)+" stub length");
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

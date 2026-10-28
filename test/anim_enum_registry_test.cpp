#include "anim_enum_registry.hpp"
#include "anim_enum_names.hpp"
#include "gameplay_capabilities.hpp"
#include "xml1_grab_carry_rules.hpp"
#include <Windows.h>
#include <cassert>
#include <cstdio>
using namespace anim_enum_registry;
int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    registry r;std::string why;
    assert(r.load("# base file\nea_mod_one\r\nea_mod_two # inline comment\n",why));
    assert(r.size()==25 && r.lookup("ea_mod_one")==233 && r.lookup("ea_mod_two")==234);
    for(int i=0;i<23;++i)assert(r.lookup(builtin_names[i])==210+i);
    for(auto n:native_names)assert(r.lookup(n)==-1); // native registry exclusively owns these
    assert(r.lookup("ea_unknown")==-1 && r.lookup("EA_MOD_ONE")==-1); // caller supplies native lowercased buffer
    const auto hash=r.hash();assert(r.load("ea_mod_two\nea_mod_one\n",why)&&r.hash()!=hash);
    for(const auto bad:{"ea_idle1","ea_flyingcarry_idle","ea_x\nea_x","EA_x","e","ea","ea_","ea_bad-name","ea_space name"})
        assert(!r.load(bad,why)&&!why.empty());
    assert(!r.load(std::string_view("ea_x\0junk",9),why));
    std::string longest="ea_"+std::string(60,'a');assert(r.load(longest,why)&&r.lookup(longest)==233);
    assert(!r.load(longest+"a",why));
    std::string full;for(int i=0;i<279;++i)full+="ea_mod_"+std::to_string(i)+"\n";
    assert(r.load(full,why)&&r.size()==302&&r.lookup("ea_mod_278")==511);
    assert(!r.load(full+"ea_overflow",why));
    assert(r.load("",why)&&r.size()==23);
    assert((r.agreement()==std::array<std::uint32_t,4>{0x00800200,23,855796497,1}));
    gameplay_capabilities::record stock,on,bad;
    assert(stock.finalize());assert(on.add(48,1,r.agreement(),true)&&on.finalize());
    assert(stock.compare(on.bytes())!=gameplay_capabilities::result::match);
    assert(on.compare(stock.bytes())!=gameplay_capabilities::result::match);
    gameplay_capabilities::difference diff;char reason[256]{};
    stock.compare(on.bytes(),&diff);stock.describe(diff.buckets,diff.local_only,true,reason,sizeof reason);
    assert(std::strstr(reason,"48 AnimEnums")&&std::strstr(reason,"disabled on this PC"));
    on.compare(stock.bytes(),&diff);on.describe(diff.buckets,diff.local_only,true,reason,sizeof reason);
    assert(std::strstr(reason,"48 AnimEnums")&&std::strstr(reason,"not on the other PC"));
    assert(!bad.add(48,1,r.agreement(),false)&&!bad.finalize());
    assert(std::string(gameplay_capabilities::rule_name(48))=="AnimEnums");
    assert(xml1_grab_carry_rules::xml1_flight_eligible(nullptr));
    std::puts("PASS AnimEnums registry, sidecar boundaries, fixed IDs, agreement refusal and flight predicate");
}

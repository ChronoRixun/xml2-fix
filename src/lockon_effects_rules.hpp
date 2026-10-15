#pragma once
#include "limits_rules.hpp"
#include "xml1_hook_transaction.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace lockon_effects_rules {
using u32 = std::uint32_t;
using paths = std::array<std::string,3>;
struct handle_cache {
    std::array<u32,3> handles{};
    template<class Engine> u32 resolve(unsigned kind,Engine& engine,const paths& names) {
        auto& handle=handles[kind];
        if(!handle||!engine.valid(handle))handle=engine.load(names[kind]);
        return handle;
    }
};
// Three extensionless paths relative to effects/. No filesystem interpretation.
inline std::optional<paths> parse(std::string_view text) {
    paths result;
    for(unsigned i=0;i<result.size();++i) {
        const auto end=text.find(',');
        if((i<2)==(end==std::string_view::npos))return {};
        const auto part=text.substr(0,end);
        if(part.empty()||part.size()>127||part.front()=='/'||part.back()=='/'||part.find("//")!=part.npos)return {};
        for(const auto c:part)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='/'||c=='-'))return {};
        result[i]=part;
        if(end!=text.npos)text.remove_prefix(end+1);
    }
    return result;
}
struct site { u32 address; std::string_view before; };
inline constexpr std::array<site,4> sites{{
    {0x4ef13f,"0f842f010000"},       // no collision: draw the scan before the native return
    {0x4ef1fd,"8d4c2418e88aa3fbff"}, // collision: native beam basis and surface normal are ready
    {0x4ef242,"518b4f1c8bc4"},       // accepted target, before the native target setter
    {0x4eef29,"83c4085f5e"}}};      // retained locked target: native orientation is ready
inline constexpr std::array<limits_rules::guard,17> guards{{
    {0x4eef60,"8b0d5cd0690081ec0001000053568bb4240c010000","scan frame and actor"},
    {0x4eefb2,"8d542414528d442434506a048bcee8cbe6f2ff","head transform destination"},
    {0x4eefae,"6a016a01","native approximate-head query arguments"},
    {0x568e90,"8a4c241081ec9000000084c9530f94c3","bone-query mode dispatch"},
    {0x569003,"6800006042","approximate head height is 56 units"},
    {0x4ef1b4,"8d842480000000d944242050","beam basis frame"},
    {0x4ef1ec,"e80f18070083c40c8d4c2418e803a4fbff","basis then native normal accessor"},
    {0x4eeec0,"83ec0c56578b7c24188b876c070000","locked actor and target field"},
    {0x4eef19,"751983c62c8d4424085650e8671a0700","live target and orientation destination"},
    {0x407750,"64a1000000008a0de08e6f006aff","effect manager getter"},
    {0x4079a0,"8b4424088b4c24045051e821ffffff","effect loader"},
    {0x4078d0,"a13c3c6f0085c075","effect resource pool getter"},
    {0x405100,"8b442404568bf18b969c41000023d081fadc000000","generation-checked effect handle"},
    {0x40b2b0,"83ec6855568be957896c240c8d742450","native effect emission"},
    {0x40b4a4,"6a00e8e54c1500","effect count uses the cosmetic random stream"},
    {0x560190,"8a44240484c07405e9f3feffffe93eff","separate cosmetic/gameplay random dispatch"},
    {0x4ef242,"518b4f1c8bc489088bcee88fe6f3ff","native target setter continuation"}}};
inline std::vector<unsigned char> bytes(std::string_view hex) {
    std::vector<unsigned char> out;
    auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(std::size_t i=0;i<hex.size();i+=2)out.push_back(static_cast<unsigned char>((digit(hex[i])<<4)|digit(hex[i+1])));
    return out;
}
inline std::vector<xml1_hook_transaction::patch> plan(const std::array<u32,4>& targets,bool animated_head=false) {
    std::vector<xml1_hook_transaction::patch> out;
    for(unsigned i=0;i<sites.size();++i) {
        const auto& s=sites[i];auto before=bytes(s.before),after=std::vector<unsigned char>(before.size(),0x90);
        after[0]=0xe9;const u32 delta=targets[i]-s.address-5;std::memcpy(after.data()+1,&delta,4);
        out.push_back({s.address,before,after});
    }
    if(animated_head)out.push_back({0x4eefb1,{1},{0}});
    return out;
}
}

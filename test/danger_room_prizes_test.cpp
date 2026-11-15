// Executes the owned retail result loop and prize formatter, without a game.
// The text database, item-name service and output buffer append are synthetic.
#include "danger_room_prizes.hpp"
#include "danger_room_prizes_rules.hpp"
#include "patch_transaction.hpp"
#include <Windows.h>
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" unsigned char fixture_space[0x700000];
namespace { bool enabled = true, fatal = false, wide = false; unsigned checks = 0, expected_item = 7; }
namespace ini { bool flag(const wchar_t*, const wchar_t*, bool fallback) { assert(fallback); return enabled; } }
namespace logger { void write(const char*, ...) {} }
namespace game_version { void refuse_startup() { fatal = true; } }
namespace item_capacity { unsigned installed_capacity() { return wide ? 4096 : 0; } }
namespace
{
    void check(bool value, const char* what) { ++checks; if (!value) { std::printf("FAIL %s\n", what); std::exit(1); } }
    template<class T> T read(const void* p, unsigned o) { T v; std::memcpy(&v, static_cast<const unsigned char*>(p)+o, sizeof v); return v; }
    template<class T> void put(void* p, unsigned o, T v) { std::memcpy(static_cast<unsigned char*>(p)+o, &v, sizeof v); }
    void* at(unsigned p) { return reinterpret_cast<void*>(p); }
    void redirect(unsigned site, const void* target)
    {
        const auto w=patch_transaction::relocation(site,"e9",true,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(target)),0,"synthetic edge");
        std::memcpy(at(site),w.bytes.data(),w.bytes.size());
    }
    void wide_formatter()
    {
        // Exact two W2 width recipes from item_capacity_sites, with private
        // synthetic cave storage; the rest of the real formatter still executes.
        for(unsigned site:{0x4ca129u,0x4ca138u})
        {
            const unsigned cave=site==0x4ca129 ? 0xaff000 : 0xaff040;
            auto code=patch_transaction::bytes_of(site==0x4ca129 ? "66837e08ff" : "0fb74e08518bc8");
            if(site==0x4ca129)
            {
                const auto branch=patch_transaction::relocation(cave+5,"0f84",true,0x4ca178,0,"W2 branch");
                code.insert(code.end(),branch.bytes.begin(),branch.bytes.end());
            }
            const auto tail=patch_transaction::relocation(cave+static_cast<unsigned>(code.size()),"e9",true,site==0x4ca129?0x4ca12f:0x4ca13e,0,"W2 return");
            code.insert(code.end(),tail.bytes.begin(),tail.bytes.end());std::memcpy(at(cave),code.data(),code.size());
            redirect(site,at(cave));
        }
    }
    bool load(const char* path)
    {
        std::ifstream f(path,std::ios::binary); std::vector<unsigned char> b{std::istreambuf_iterator<char>(f),{}};
        if(b.size()<0x1000) return false;
        const auto pe=read<unsigned>(b.data(),0x3c), table=pe+24+read<unsigned short>(b.data(),pe+20);
        for(unsigned i=0;i<read<unsigned short>(b.data(),pe+6);++i)
        {
            const auto r=table+40*i, va=0x400000+read<unsigned>(b.data(),r+12), n=read<unsigned>(b.data(),r+16), off=read<unsigned>(b.data(),r+20);
            if(off+std::uint64_t(n)>b.size() || va<0x401000 || va+std::uint64_t(n)>0xb01000) return false;
            std::memcpy(at(va),b.data()+off,n);
        }
        return true;
    }
    const char* __fastcall text(void*,void*,unsigned id)
    {
        switch(id)
        {
        case 731:return "TierA";case 732:return "TierB";case 733:return "TierC";
        case 740:return "Prize:";case 748:return "New";case 741:return "XP:%u";
        case 702:return "Grade:%s";default:return "Synthetic";
        }
    }
    void* text_vt[3]{}; void* text_object[1]{text_vt};
    void* __cdecl get_text() { return text_object; }
    void __fastcall append(char* dest,void*,const char* s) { std::strcat(dest,s); }
    char* __cdecl copy(char* dest,const char* s,std::size_t n) { return std::strncpy(dest,s,n); }
    int __cdecl format(char* dest,std::size_t n,const char* fmt,...) { va_list a;va_start(a,fmt);const int v=std::vsnprintf(dest,n,fmt,a);va_end(a);return v; }
    void __fastcall cookie(void*,void*) {}
    void* item_vt[7]{}; void* item[1]{item_vt}; void* manager_vt[7]{}; void* manager[1]{manager_vt};
    unsigned name_id=1; std::array<unsigned char,0x9000> strings{};
    void* registry_vt[0x68/4]{}; void* registry[1]{registry_vt};
    void* __cdecl get_manager() { return manager; }
    void* __fastcall definition(void*,void*,unsigned id) { check(id==expected_item,"named definition index retained");return item; }
    void* __fastcall name(void*,void*,void*) { return &name_id; }
    void* __cdecl get_strings() { return strings.data(); }
    void* __cdecl get_registry() { return registry; }
    void* __fastcall character_name(void*,void*,unsigned id) { check(id==23,"character reward index retained");return &name_id; }
    const char* __fastcall resolve_name(void*,void*) { return "Synthetic Hero"; }
    using Course=std::array<unsigned char,0x78>;
    Course blank()
    {
        Course c{};
        for(unsigned o:{4u,0x18u,0x2cu}) { c[o]=4;c[o+1]=6;c[o+8]=0xff;if(wide)c[o+9]=0xff;c[o+0x11]=9; }
        return c;
    }
    std::string run(Course& c,bool exam=false)
    {
        return reinterpret_cast<const char*(__fastcall*)(void*,void*,int)>(at(0x4cdf70))(c.data(),nullptr,exam?0:1);
    }
    using Overview=const char*(__fastcall*)(const unsigned char*,void*,unsigned,unsigned,const char*);
    Overview overview_target(unsigned site)
    {
        check(*static_cast<unsigned char*>(at(site))==0xe8,"overview remains a native CALL");
        return reinterpret_cast<Overview>(at(site+5+read<unsigned>(at(site),1)));
    }
}
int main(int argc,char** argv)
{
    check(argc==3,"usage: test retail-exe on|off|guardN|negative");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    DWORD old;check(VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old)!=0,"fixture executable");
    check(reinterpret_cast<unsigned>(fixture_space)==0x401000 && load(argv[1]),"map owned retail image without launching");
    const std::string mode=argv[2];wide=mode.starts_with("wide");enabled=!mode.ends_with("off") && !mode.ends_with("negative");
    if(wide)wide_formatter();
    const unsigned guards[]{0x4ce084,0x4ce19a,0x4ca0ef,0x4ca129,0x4ca178,0x4ca23d,0x4ca2ce,0x4ca330,0x4ca347,0x4ca138,0x4cd154,0x4cd184,0x4cd1ea,0x4ca3a0,0x4ca5eb};
    if(mode.starts_with("guard")) { const auto n=std::stoul(mode.substr(5));check(n<std::size(guards),"guard index");*static_cast<unsigned char*>(at(guards[n]))^=1; }
    if(mode.starts_with("wideguard")) { const unsigned sites[]{0x4ca129,0xaff000,0x4ca138,0xaff040};const auto n=std::stoul(mode.substr(9));check(n<std::size(sites),"wide guard index");*static_cast<unsigned char*>(at(sites[n]))^=1; }
    const std::vector<unsigned char> original(fixture_space,fixture_space+sizeof fixture_space);
    danger_room_prizes::install(reinterpret_cast<HMODULE>(0x400000));check(!fatal,"no startup refusal");
    if(mode.starts_with("guard") || mode.starts_with("wideguard") || mode=="off" || mode=="wideoff")
    {
        check(std::memcmp(original.data(),fixture_space,original.size())==0,"disabled/mismatched image makes no writes");
        std::printf("PASS %s: %u checks\n",mode.c_str(),checks);return 0;
    }
    unsigned changed=0;
    for(unsigned i=0;i<original.size();++i) if(original[i]!=fixture_space[i])
    {
        ++changed;const auto va=0x401000+i;
        check((va>=0x4ce097 && va<0x4ce09d)||(va>=0x4cd154 && va<0x4cd159)||
              (va>=0x4cd184 && va<0x4cd189)||(va>=0x4cd1ea && va<0x4cd1ef),"only result hook and three overview calls change");
    }
    if(enabled) check(changed>0,"production hook installed");
    text_vt[2]=reinterpret_cast<void*>(&text);manager_vt[6]=reinterpret_cast<void*>(&definition);item_vt[6]=reinterpret_cast<void*>(&name);
    registry_vt[0x64/4]=reinterpret_cast<void*>(&character_name);
    std::strcpy(reinterpret_cast<char*>(strings.data()+0x8008),"Synthetic Gear");put(strings.data(),8,0u);
    redirect(0x4bdb30,&get_text);redirect(0x4a01e0,&append);redirect(0x672170,&copy);redirect(0x5604f0,&format);redirect(0x672161,&cookie);
    redirect(0x480a00,&get_manager);redirect(0x602140,&get_strings);
    redirect(0x44b8f0,&get_registry);redirect(0x425bc0,&resolve_name);
    *static_cast<const char**>(at(0x6da754))="";*static_cast<const char**>(at(0x6da75c))="";
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    if(mode=="overviewnegative")
    {
        auto empty=blank();check(std::string(overview_target(0x4cd154)(empty.data()+4,nullptr,731,0,nullptr)).empty(),"empty Overview regression (expected failure against old formatter)");return 0;
    }
    auto c=blank();const auto first=run(c);
    if(mode=="negative" || mode=="widenegative") { check(first.empty(),"empty results regression (expected failure against old renderer)");return 0; }
    check(first.empty(),"ordinary course has no fictitious awards or Prize lines");
    for(unsigned o:{4u,0x18u,0x2cu}) check(c[o+0x11]==1,"empty rows consume only native one-shot bit");
    c=blank();c[0x4f]=8;put(at(0x782724),0,6u);*static_cast<unsigned char*>(at(0x782729))=0;
    const auto exam=run(c,true);check(exam.find("Grade:")!=std::string::npos && exam.find("Prize:")==std::string::npos,"exam keeps grade promotion without empty prizes");
    check((*static_cast<unsigned char*>(at(0x7825e8+0x2c))&1)!=0,"native exam unlock still executes");
    c=blank();put(c.data(),4+8,static_cast<unsigned short>(7));const auto item_result=run(c);
    check(item_result.find("TierA")!=std::string::npos && item_result.find("Synthetic Gear")!=std::string::npos,"named challenge reward uses native formatter");
    check(item_result.find("TierB")==std::string::npos && item_result.find("TierC")==std::string::npos,"item challenge has no phantom secondary awards");
    check(run(c).empty(),"repeat results do not reannounce an earned item");
    if(wide)
    {
        for(unsigned index:{0u,254u,255u,256u,511u,1023u})
        {
            c=blank();expected_item=index;put(c.data(),4+8,static_cast<unsigned short>(index));
            check(run(c).find("Synthetic Gear")!=std::string::npos,"W2 item index, including low byte 255, stays visible");
        }
        expected_item=7;
    }
    for(unsigned flag:{0u,1u,8u})
    {
        c=blank();for(unsigned o:{4u,0x18u,0x2cu}) c[o+0x11]=static_cast<unsigned char>(flag);
        const auto before=c;check(run(c).empty() && c==before,"native earned/new flag gates and row bytes unchanged");
    }
    c=blank();put(c.data(),4+4,123u);const auto xp=run(c);check(xp.find("XP:123")!=std::string::npos,"real XP prize remains visible");
    c=blank();for(unsigned o:{4u,0x18u,0x2cu}) put(c.data(),o+4,123u);
    const auto tiers=run(c);check(tiers.find("TierA")!=std::string::npos && tiers.find("TierB")!=std::string::npos && tiers.find("TierC")!=std::string::npos,"all three authored tiers remain visible");
    for(unsigned kind=0;kind<5;++kind)
    {
        c=blank();auto* reward=c.data()+4;
        switch(kind)
        {
        case 0:reward[0]=0;break;
        case 1:reward[1]=2;break;
        case 2:reward[0x11]|=2;break;
        case 3:reward[0x11]|=4;break;
        case 4:put(reward,0xa,static_cast<unsigned short>(23));break;
        }
        const auto rendered=run(c);
        check(rendered.find("TierA")!=std::string::npos && rendered.find("Prize:")!=std::string::npos,"native trait/generated/level/talent/character payload stays visible");
    }
    auto row=blank();auto* p=row.data()+4;check(!danger_room_prizes_rules::has_payload(p,wide),"thresholds/award flags alone carry no prize");
    p[0]=0;check(danger_room_prizes_rules::has_payload(p),"trait zero is a real payload");p[0]=4;
    p[1]=0;check(danger_room_prizes_rules::has_payload(p),"spawn type zero is a real payload");p[1]=6;
    for(unsigned flag:{2u,4u}) { p[0x11]=static_cast<unsigned char>(9|flag);check(danger_room_prizes_rules::has_payload(p),"level/talent payload remains visible"); }
    p[0x11]=9;put(p,0xa,static_cast<unsigned short>(23));check(danger_room_prizes_rules::has_payload(p),"character payload remains visible");
    const auto native_overview=reinterpret_cast<Overview>(at(0x4ca3a0));
    for(unsigned site:{0x4cd154u,0x4cd184u,0x4cd1eau})
    {
        const auto wrapper=overview_target(site);c=blank();const auto before=c;
        put(at(0x784980),0,0x12345678u);put(at(0x784984),0,0x12345678u);
        check(std::string(wrapper(c.data()+4,nullptr,731,0,nullptr)).empty(),"empty Overview row hidden at each authored call");
        check(c==before && read<unsigned>(at(0x784980),0)==0 && read<unsigned>(at(0x784984),0)==0,"Overview preserves native row bytes and scratch/preview resets");
        put(c.data(),4+8,static_cast<unsigned short>(7));
        const std::string stock=native_overview(c.data()+4,nullptr,731,0,nullptr);
        check(std::string(wrapper(c.data()+4,nullptr,731,0,nullptr))==stock && stock.find("Synthetic Gear")!=std::string::npos,"real Overview item text is byte-identical to native");
        c=blank();put(c.data(),4+4,123u);const std::string stock_xp=native_overview(c.data()+4,nullptr,733,0,nullptr);
        check(std::string(wrapper(c.data()+4,nullptr,733,0,nullptr))==stock_xp && stock_xp.find("XP:123")!=std::string::npos,"real Overview XP text is byte-identical to native");
    }
    std::printf("PASS production renderer (%s): %u checks; results/Overview, all payloads, native state retained\n",wide?"W2":"retail",checks);
}

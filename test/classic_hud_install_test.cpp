#include "classic_hud.hpp"
#include "classic_hud_rules.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>

namespace {
    std::optional<std::string> option="xml1";
    bool fatal=false;
}
namespace ini {
    std::optional<std::string> text(const wchar_t* section,const wchar_t* key) {assert(std::wstring_view(section)==L"Game" && std::wstring_view(key)==L"ClassicHud");return option;}
    bool flag(const wchar_t*,const wchar_t*,bool value) {return value;}
}
namespace logger { void write(const char*,...) {} void write_once(std::string_view,const char*,...) {} }
namespace game_version {void refuse_startup() {fatal=true;}}
namespace pad_prompts {const char* gameplay_power_label(int,int) {return nullptr;}}

int main(int argc,char** argv)
{
    assert(argc==3);
    const std::string mode=argv[2];
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)),{});
    assert(file.size()>0x1000);
    auto u16=[&](std::size_t p) {std::uint16_t n;std::memcpy(&n,file.data()+p,2);return n;};
    auto u32=[&](std::size_t p) {std::uint32_t n;std::memcpy(&n,file.data()+p,4);return n;};
    auto pe=u32(0x3c), sections=pe+24+u16(pe+20);
    const std::uint32_t sites[]{0x69dcf0,0x69dc60,0x59a53a,0x5a4376,0x5a438f,0x5a5bcf,0x69dc78,0x5a904f,
        0x59a140,0x5ec560,0x5ee180,0x5a3933,0x5a3a40,0x5a3acc,0x5a3c3c,0x5a3d4b,0x5a3dd7,0x59935e,0x69de88,
        0x599432,0x599531,0x5996a4,0x59e144,
        0x5a3f5b,0x69de8c,0x5a3f1e,
        0x5a70a0,0x5a71e5,0x5a772b,0x5a7223,0x5a722c,0x5a7237,0x5a7596,0x5a716b,0x69dd38,
        0x5a748e,0x4bdfa0,0x5f11b0,
        0x5a9030,0x5f1130,0x5f0fd0,0x597c20,0x596df0,0x69d6d0,0x5f1173,0x5ef275};
    // XMen2.exe's own small-map placement: the map draw slot and the two safe-area inset operands
    // (beta 5 redirected them to fit a frame). The adapter must leave all three byte-for-byte alone.
    const std::uint32_t native_map[]{0x69db98,0x5a023f,0x5a0717};
    std::vector<std::uint32_t> loaded(std::begin(sites),std::end(sites));
    loaded.insert(loaded.end(),std::begin(native_map),std::end(native_map));
    for(auto va:loaded) {
        bool found=false;
        for(unsigned i=0;i<u16(pe+6);++i) {
            auto row=sections+40*i,rva=u32(row+12);
            if(va-0x400000>=rva && va-0x400000+16<=rva+u32(row+16)) {
                DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(va),16,PAGE_EXECUTE_READWRITE,&old));
                std::memcpy(reinterpret_cast<void*>(va),file.data()+u32(row+20)+va-0x400000-rva,16);
                found=true;break;
            }
        }
        assert(found);
    }
    wchar_t own[MAX_PATH]{};assert(GetModuleFileNameW(nullptr,own,MAX_PATH));
    const auto root=std::filesystem::path(own).parent_path();
    // Synthetic existence fixtures, never game content. The engine is not run.
    std::filesystem::create_directories(root/L"Textures/ui");
    std::filesystem::create_directories(root/L"UI/hud");
    std::ofstream(root/L"Textures/ui/lch.IGB")<<"synthetic";
    std::ofstream(root/L"UI/hud/lc_healthpanel.IGB")<<"synthetic";
    std::ofstream(root/L"UI/hud/lc_partycross.IGB")<<"synthetic";
    std::ofstream(root/L"lc-hud.txt")<<(mode=="assets"?"bad":mode=="stale"?"LC-HUD-3":"LC-HUD-4")<<'\n';
    std::ofstream(root/L"UI/hud/lc_healthpanel_pc.IGB")<<"synthetic";
    std::ofstream(root/L"UI/hud/lc_partycross_pc.IGB")<<"synthetic";
    if(mode=="alias") std::filesystem::remove(root/L"UI/hud/lc_partycross_pc.IGB");
    if(mode=="off") option.reset();
    if(mode=="zero") option="0";
    if(mode=="named-off") option="off";
    if(mode=="legacy") option="1";
    if(mode=="invalid") option="bad";
    if(mode.starts_with("guard")) {
        const auto index=std::stoul(mode.substr(5));assert(index<std::size(sites));
        *reinterpret_cast<unsigned char*>(sites[index])^=1;
    }
    std::array<std::uint32_t,8> before{};
    for(unsigned i=0;i<before.size();++i)std::memcpy(&before[i],reinterpret_cast<void*>(sites[i]),4);
    std::array<std::uint32_t,3> map_before{};
    for(unsigned i=0;i<map_before.size();++i)std::memcpy(&map_before[i],reinterpret_cast<void*>(native_map[i]),4);
    classic_hud::install(reinterpret_cast<HMODULE>(mode=="base"?0x500000:0x400000));
    assert(!fatal);
    for(unsigned i=0;i<before.size();++i) {
        std::uint32_t after;std::memcpy(&after,reinterpret_cast<void*>(sites[i]),4);
        assert((after!=before[i])==(mode=="on"));
    }
    for(unsigned i=0;i<map_before.size();++i) {
        std::uint32_t after;std::memcpy(&after,reinterpret_cast<void*>(native_map[i]),4);
        assert(after==map_before[i]); // native small-map placement is never touched, installed or not
    }
    using namespace classic_hud_rules;
    // A fixed character count clips wide letters and unnecessarily shortens
    // narrow ones. Fit using the same measured advances as the native font.
    const auto measure=[](const std::string& value) {
        float width=0;
        for(char c:value) width+=c=='W' ? 12.f : c=='.' ? 2.f : 4.f;
        return width;
    };
    assert(fit_target_name("Short",24,measure)=="Short");
    assert(fit_target_name("WW",24,measure)=="WW");
    assert(fit_target_name("WWWW",24,measure)=="W...");
    assert(fit_target_name("WWWW (12)",40,measure)=="W... (12)");
    assert(fit_target_name("iiiiiiii",24,measure)=="iiii...");
    assert(fit_target_name("",24,measure).empty());
    assert(fit_target_name("Wide",0,measure).empty());
    assert(fit_target_name("Wide",5,measure).empty());
    assert(fit_target_name("Wide",6,measure)=="...");
    assert(fit_target_name("Wide",NAN,measure).empty());
    assert(fit_target_name("Wide",24,[](const std::string&){return NAN;}).empty());
    assert(hud_batch_address(hud_batch) && hud_batch_address(hud_batch+0x104) && hud_batch_address(hud_batch+0x208));
    assert(!hud_batch_address(hud_batch+1) && !hud_batch_address(hud_batch+0x30c));
    assert(energy_offset(3)==3.75f && energy_offset(-3)==-3.75f);
    const float old[]{.1f,.2f,.3f,.4f,.5f,.6f,.7f,.8f};
    const auto native=map_uv(27,old);
    for(unsigned i=0;i<8;++i)assert(native[i]==old[i]/2);
    for(unsigned id=10;id<=12;++id) {
        const float x=(id-10)*.25f;
        const uv stock{x,.625f,x,.5f,x+.25f,.625f,x+.25f,.5f};
        const auto mapped=map_uv(id,stock.data());
        assert(mapped[0]>.5f && mapped[4]>mapped[0]);
        const auto foreign=map_uv(id,old);
        for(unsigned i=0;i<8;++i)assert(foreign[i]==old[i]/2);
    }
    const auto map_arrow=map_uv(13,old);
    for(unsigned i=0;i<8;++i)assert(map_arrow[i]==old[i]/2);
    assert(map_uv(0,old)[0]>.5f && map_uv(0,old)[1]<.5f);
    assert(map_uv(health,old)[1]>.5f && map_uv(energy,old)[1]>map_uv(health,old)[1]);
    assert(hero_fill(0x5a3939)==health && hero_fill(0x5a393a)==-1);
    assert(hero_fill(0x5a3ddd)==energy);
    const float xp_empty[]{0,2},xp_half[]{36.5f,2},xp_full[]{73,2},xp_bad[]{74,2};
    assert(experience_draw(0x5a3f5e,0,27,xp_empty,0));
    assert(experience_draw(0x5a3f5e,0,27,xp_half,.5f));
    assert(experience_draw(0x5a3f5e,0,27,xp_full,1));
    assert(!experience_draw(0x5a3f5e,0,27,xp_bad,1));
    assert(!experience_draw(0x5a3f5e,1,27,xp_half,.5f));
    assert(!experience_draw(0x5a3f5f,0,27,xp_half,.5f));
    assert(!experience_draw(0x5a3f5e,0,26,xp_half,.5f));
    assert(!experience_draw(0x5a3f5e,0,27,xp_half,2));
    const auto xp_uv=map_uv(experience,old);
    assert(xp_uv[4]>xp_uv[0] && xp_uv[1]>xp_uv[3] && xp_uv[3]>358.f/512);
    assert(wheel_entry_visible(0,1) && !wheel_entry_visible(0,0));
    assert(wheel_entry_visible(1,0) && !wheel_entry_visible(1,0xffffffff));
    assert(wheel_entry_visible(2,1) && !wheel_entry_visible(2,0));
    assert(!wheel_entry_visible(3,1) && !wheel_entry_visible(2,0xffffffff));
    const float ring_size[]{40,40},bad_ring[]{40,39};
    assert(wheel_ring(0x5a7599,1,0,ring_size));
    assert(!wheel_ring(0x5a7598,1,0,ring_size));
    assert(!wheel_ring(0x5a7599,0,0,ring_size));
    assert(!wheel_ring(0x5a7599,1,2,ring_size));
    assert(!wheel_ring(0x5a7599,1,0,bad_ring));
    assert(!target_draw(0x5a8dec)); // the resistance text contract is outside this adapter
    const float prompt_size[]{133,20.8f}, stacked_prompt[]{200,83.2f}, bad_prompt[]{0,20};
    assert(prompt_background(0x59e147,1,27,prompt_size));
    assert(prompt_background(0x59e147,1,27,stacked_prompt));
    assert(!prompt_background(0x59e147,0,27,prompt_size));
    assert(!prompt_background(0x59e147,1,26,prompt_size));
    assert(!prompt_background(0x59e148,1,27,prompt_size));
    assert(!prompt_background(0x59e147,1,27,bad_prompt));
    // No small-map frame: sprite 38 is unused and falls back to the PC quadrant like any foreign id.
    const auto unused=map_uv(38,old);
    for(unsigned i=0;i<8;++i)assert(unused[i]==old[i]/2);
    for(const auto shape:std::array<std::array<float,2>,2>{{{224,24},{48,12}}}) {
        const float pos[]{100,10,300};const int skew=int(shape[1]*.75f);
        const auto box=target_origin(pos,shape.data(),skew);assert(box.valid);
        for(auto caller:{0x599361u,0x599435u,0x599534u,0x5996a7u}) {
            const auto art=target_art(box.width,caller);
            assert(art!=27); // point-UV fills failed after a menu/load round-trip
            const auto coverage=map_uv(art,old);
            assert(coverage[4]>coverage[0] && coverage[1]>coverage[3]);
            assert((art==frame || art==small_frame)==(caller==0x599361u));
        }
        assert(box.x+box.width/2==100+(shape[0]+skew)/2); // retain the native centre
        const auto interior=target_position(box,0x599435,1);
        assert(interior[0]==box.x+1 && interior[2]==299 && interior[1]==1);
        const auto fill=target_position(box,0x5996a7,-1);
        assert(fill[0]+shape[0]-8==box.x+shape[0]-4 && fill[2]-(shape[1]-8)==box.z-shape[1]+4);
    }
    const float pos[]{0,0,0},unknown[]{120,10};assert(!target_origin(pos,unknown,0).valid);
    std::cout<<"classic HUD "<<mode<<": PASS\n";
}

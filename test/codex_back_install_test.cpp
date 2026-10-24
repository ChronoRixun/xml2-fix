#include "codex_menu.hpp"
#include <Windows.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {bool enabled=true,fatal=false;int closes=0,focused=0;std::vector<std::array<int,2>> modes;std::vector<int> biographies;}
namespace ini {bool flag(const wchar_t* section,const wchar_t* key,bool) {
    assert(std::wstring_view(section)==L"Game" && std::wstring_view(key)==L"ClassicCodex");return enabled;}}
namespace logger {void write(const char*,...) {}}
namespace game_version {void refuse_startup() {fatal=true;}}
namespace {
    int __fastcall close_stub(void*,void*) {++closes;return 2;}
    void __fastcall mode_stub(void*,void*,int mode,int active) {modes.push_back({mode,active});}
    int __fastcall selection_stub(void* list,void*) {return 5+17*(*reinterpret_cast<short*>(static_cast<unsigned char*>(list)+0xac));}
    void __fastcall biography_stub(void*,void*,int id) {biographies.push_back(id);}
    void __fastcall focus_stub(void*,void*,int value) {focused=value;}
    void redirect(std::uint32_t site,const void* target) {
        DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READWRITE,&old));
        auto* out=reinterpret_cast<unsigned char*>(site);out[0]=0xe9;
        const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target))-site-5;
        std::memcpy(out+1,&delta,4);FlushInstructionCache(GetCurrentProcess(),out,5);
    }
}
int main(int argc,char** argv) {
    assert(argc==3);const std::string mode=argv[2];enabled=mode!="off";
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)),{});assert(file.size()>0x1000);
    auto u16=[&](std::size_t at) {std::uint16_t n;std::memcpy(&n,file.data()+at,2);return n;};
    auto u32=[&](std::size_t at) {std::uint32_t n;std::memcpy(&n,file.data()+at,4);return n;};
    const auto pe=u32(0x3c),sections=pe+24+u16(pe+20);
    const std::uint32_t sites[]{0x69e700,0x5b9fe0,0x5acf30,0x5b18ef,0x5b190f,0x5b18fa,0x5bfc70,0x5b1340};
    for(auto va:sites) {
        bool found=false;
        for(unsigned i=0;i<u16(pe+6);++i) {
            const auto row=sections+40*i,rva=u32(row+12);
            if(va-0x400000>=rva && va-0x400000+32<=rva+u32(row+16)) {
                DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(va),32,PAGE_EXECUTE_READWRITE,&old));
                std::memcpy(reinterpret_cast<void*>(va),file.data()+u32(row+20)+va-0x400000-rva,32);found=true;break;
            }
        }
        assert(found);
    }
    if(mode.starts_with("guard")) {
        const auto index=std::stoul(mode.substr(5));assert(index<std::size(sites));
        *reinterpret_cast<unsigned char*>(sites[index])^=1;
    }
    auto& slot=*reinterpret_cast<std::uint32_t*>(0x69e700);const auto before=slot;
    std::array<unsigned char,5> old_call{};std::memcpy(old_call.data(),reinterpret_cast<void*>(0x5b18fa),5);
    codex_menu::install(reinterpret_cast<HMODULE>(0x400000));assert(!fatal);
    assert((slot!=before)==(mode=="on"));
    if(mode!="on") assert(std::memcmp(old_call.data(),reinterpret_cast<void*>(0x5b18fa),5)==0);
    if(mode=="on") {
        redirect(0x5b9fe0,reinterpret_cast<void*>(&close_stub));
        redirect(0x5acf30,reinterpret_cast<void*>(&mode_stub));
        redirect(0x5bfc70,reinterpret_cast<void*>(&selection_stub));
        redirect(0x5b1340,reinterpret_cast<void*>(&biography_stub));
        std::array<unsigned char,0x1a00> menu{};std::array<unsigned char,0x100> list{};
        std::array<void*,32> table{};table[0x4c/4]=reinterpret_cast<void*>(&focus_stub);
        *reinterpret_cast<void***>(list.data())=table.data();
        *reinterpret_cast<void**>(menu.data()+0x18dc)=list.data();
        *reinterpret_cast<short*>(list.data()+0xac)=3;
        auto& state=*reinterpret_cast<int*>(menu.data()+0x18d8);state=1;
        using action=int(__fastcall*)(void*,void*);
        const auto back=reinterpret_cast<action>(slot);
        assert(back(menu.data(),nullptr)==2 && state==0 && closes==0 && focused==1);
        assert(modes.size()==2 && modes[0][0]==1 && modes[0][1]==0 && modes[1][0]==0 && modes[1][1]==1);
        assert(*reinterpret_cast<short*>(list.data()+0xac)==3);
        assert(back(menu.data(),nullptr)==2 && closes==1);
        state=1;*reinterpret_cast<void**>(menu.data()+0x18dc)=nullptr;
        assert(back(menu.data(),nullptr)==2 && closes==2);
        // Moving a row and pressing Details must refresh its biography even
        // without Accept. Native row IDs deliberately differ from list indices.
        *reinterpret_cast<void**>(menu.data()+0x18dc)=list.data();state=0;
        const auto relative=*reinterpret_cast<std::int32_t*>(0x5b18fb);
        using change_mode=void(__fastcall*)(void*,void*,int,int);
        const auto details=reinterpret_cast<change_mode>(0x5b18ff+relative);
        details(menu.data(),nullptr,0,0);
        if(biographies.size()!=1 || biographies.back()!=56) {std::cerr<<"Details did not bind the newly selected row\n";return 42;}
        *reinterpret_cast<short*>(list.data()+0xac)=4;
        details(menu.data(),nullptr,0,0);assert(biographies.size()==2 && biographies.back()==73);
        state=1;details(menu.data(),nullptr,1,0);assert(biographies.size()==2);
    }
    std::cout<<"Codex Back "<<mode<<" PASS\n";
}

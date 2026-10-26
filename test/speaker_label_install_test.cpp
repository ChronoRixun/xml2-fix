#include "speaker_label.hpp"
#include <Windows.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// usage: xml2_speaker_label_install_test <retail XMen2.exe> on|off|guard<N>|rule
// The retail bytes of every guarded site are laid at their own addresses (the image fixture), the installer runs,
// and with "on" the hooked parser call is driven through stubs of the parser, the attribute lookup and the intern.
namespace {bool enabled=true,fatal=false;int parses=0,lookups=0;bool parse_result=true;
    std::string parsed_text,parsed_textb;std::map<std::string,std::string> attributes;std::vector<std::string> interned;}
namespace ini {bool flag(const wchar_t* section,const wchar_t* key,bool) {
    assert(std::wstring_view(section)==L"Game" && std::wstring_view(key)==L"ClassicSpeakers");return enabled;}}
namespace logger {void write(const char*,...) {}}
namespace game_version {void refuse_startup() {fatal=true;}}
namespace {
    struct fields_t {const char* text;std::uint32_t text_tag;const char* textb;std::uint32_t textb_tag;unsigned char rest[0x80];};
    bool __fastcall parse_stub(void*,void*,void* fields,void*,int) {
        ++parses;auto* f=static_cast<fields_t*>(fields);f->text=parsed_text.c_str();f->textb=parsed_textb.c_str();return parse_result;}
    const char* __fastcall attribute_stub(void*,void*,const char* name) {
        ++lookups;for(const auto& [k,v]:attributes) if(_stricmp(k.c_str(),name)==0) return v.c_str();return nullptr;}
    void* __cdecl intern_stub(void* out,const char* text) {
        interned.emplace_back(text);auto* pair=static_cast<std::uint32_t*>(out);
        pair[0]=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_strdup(text)));pair[1]=0x5157;return out;}
    void redirect(std::uint32_t site,const void* target) {
        DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READWRITE,&old));
        auto* out=reinterpret_cast<unsigned char*>(site);out[0]=0xe9;
        const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target))-site-5;
        std::memcpy(out+1,&delta,4);FlushInstructionCache(GetCurrentProcess(),out,5);
    }
    std::uint32_t call_target(std::uint32_t site) {
        assert(*reinterpret_cast<unsigned char*>(site)==0xe8);
        return site+5+*reinterpret_cast<std::uint32_t*>(site+1);
    }
    void rule_cases() {
        std::string out;
        assert(speaker_label::relabel("Magma\nWhat am I doing here?","%MAGMA%What am I doing here?","Alison",out));
        assert(out=="Alison\nWhat am I doing here?");
        assert(speaker_label::relabel("Magma\nOk.","%MAGMA%: Ok.","Alison",out) && out=="Alison\nOk.");
        assert(speaker_label::relabel("Magma\n","%MAGMA%","Alison",out) && out=="Alison\n");
        out="untouched";
        assert(!speaker_label::relabel("%MAGMA%Hi","%MAGMA%Hi","Alison",out));          // unresolved: verbatim token
        assert(!speaker_label::relabel("Xx: Magma\nHi","Xx: %MAGMA%Hi","Alison",out));  // token not at the start
        assert(!speaker_label::relabel("Magma\nHi Magma\n","%MAGMA%Hi %MAGMA%","Alison",out)); // a second token
        assert(!speaker_label::relabel("Magma\nHi","%MAGMA%Hi","Ali%son",out));
        assert(!speaker_label::relabel("Magma\nHi","%MAGMA%Hi","Ali\nson",out));
        assert(!speaker_label::relabel("Magma\nHi","%MAGMA%Hi","",out));
        assert(!speaker_label::relabel("Magma\nHi","%MAGMA%Hi",std::string(64,'A').c_str(),out));
        assert(!speaker_label::relabel("Magma\nHi","%%Hi","Alison",out));
        assert(!speaker_label::relabel("Magma\nHi","%MAGMA_IS_A_VERY_LONG_SPEAKER_KEY%Hi","Alison",out));
        assert(!speaker_label::relabel(nullptr,"%MAGMA%Hi","Alison",out));
        assert(!speaker_label::relabel("Magma\nHi",nullptr,"Alison",out));
        assert(!speaker_label::relabel("Magma\nHi","%MAGMA%Hi",nullptr,out));
        const std::string huge(0x3fa,'x');
        assert(!speaker_label::relabel(("Magma\n"+huge).c_str(),("%MAGMA%"+huge).c_str(),"Alison",out)); // > 0x3ff
        assert(out=="untouched");
    }
}
int main(int argc,char** argv) {
    assert(argc==3);const std::string mode=argv[2];
    if(mode=="rule") {rule_cases();std::cout<<"Speaker label rule PASS\n";return 0;}
    enabled=mode!="off";
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)),{});assert(file.size()>0x1000);
    auto u16=[&](std::size_t at) {std::uint16_t n;std::memcpy(&n,file.data()+at,2);return n;};
    auto u32=[&](std::size_t at) {std::uint32_t n;std::memcpy(&n,file.data()+at,4);return n;};
    const auto pe=u32(0x3c),sections=pe+24+u16(pe+20);
    const std::uint32_t sites[]{0x458da8,0x458e7b,0x458820,0x458c37,0x458cd4,0x5649c0,0x4566b0};
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
        *reinterpret_cast<unsigned char*>(sites[index]+1)^=1;
    }
    std::array<unsigned char,5> first{},child{};
    std::memcpy(first.data(),reinterpret_cast<void*>(0x458da8),5);std::memcpy(child.data(),reinterpret_cast<void*>(0x458e7b),5);
    speaker_label::install(reinterpret_cast<HMODULE>(0x400000));assert(!fatal);
    const bool first_same=std::memcmp(first.data(),reinterpret_cast<void*>(0x458da8),5)==0;
    const bool child_same=std::memcmp(child.data(),reinterpret_cast<void*>(0x458e7b),5)==0;
    if(mode!="on") {assert(first_same && child_same);std::cout<<"Speaker label "<<mode<<" PASS (no writes)\n";return 0;}
    assert(!first_same && !child_same);
    const auto hook=call_target(0x458da8);assert(hook==call_target(0x458e7b));
    assert(hook<0x400000 || hook>=0x800000);
    redirect(0x458820,reinterpret_cast<void*>(&parse_stub));
    redirect(0x5649c0,reinterpret_cast<void*>(&attribute_stub));
    redirect(0x4566b0,reinterpret_cast<void*>(&intern_stub));
    using parse=bool(__fastcall*)(void*,void*,void*,void*,int);
    const auto call=reinterpret_cast<parse>(hook);
    int system=0,node=0;fields_t fields{};
    // An ex-%ALISON% line: label replaced, the rest and the textb (absent) untouched.
    attributes={{"text","%MAGMA%What am I doing here?"},{"speakerlabel","Alison"}};
    parsed_text="Magma\nWhat am I doing here?";parsed_textb="";
    assert(call(&system,nullptr,&fields,&node,0));
    assert(std::string(fields.text)=="Alison\nWhat am I doing here?" && fields.text_tag==0x5157 && std::string(fields.textb).empty());
    assert(interned.size()==1 && parses==1);
    // text and textb both carry the token: both relabelled.
    attributes={{"text","%MAGMA%A"},{"textb","%MAGMA%B"},{"speakerLabel","Alison"}};
    parsed_text="Magma\nA";parsed_textb="Magma\nB";fields={};
    assert(call(&system,nullptr,&fields,&node,1));
    assert(std::string(fields.text)=="Alison\nA" && std::string(fields.textb)=="Alison\nB" && interned.size()==3);
    // Magma's own lines (no speakerLabel) and the NPC Alison's (%ALISON%, no attribute) are left as parsed.
    attributes={{"text","%MAGMA%Mine."}};parsed_text="Magma\nMine.";fields={};
    assert(call(&system,nullptr,&fields,&node,0));
    assert(fields.text==parsed_text.c_str() && interned.size()==3);
    // A line the parser drops: no attribute read.
    parse_result=false;lookups=0;attributes={{"text","%MAGMA%x"},{"speakerlabel","Alison"}};parsed_text="Magma\nx";fields={};
    assert(!call(&system,nullptr,&fields,&node,0));
    assert(lookups==0 && fields.text==parsed_text.c_str() && interned.size()==3);
    std::cout<<"Speaker label on PASS\n";
}

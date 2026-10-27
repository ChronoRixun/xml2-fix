// Owned retail PE mapped in reserved fixture memory. Never starts XMen2.exe.
// Actual production installer, hooks, mailbox, ABI adapters and transaction.
#include "online_menus.hpp"
#include "online_menus_rules.hpp"
#include "online_menus_guards.hpp"
#include "script_function_overflow.hpp"
#include <Windows.h>
#include <cassert>
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

namespace {
using namespace online_menus_rules;
std::string mode="on",fail;
bool fatal=false,assets=true,close_during_tick=false;
int capacity=1024;
std::filesystem::path root;
std::vector<std::string> calls;
std::map<std::string,std::string> texts;
bool hidden=true;
std::vector<unsigned char> mgr(0xb0000),input(0x100);
std::array<unsigned char,0x1a00> main_menu{},hub{},child{};
std::array<unsigned char,0xb0> text_item{};
void put(void* p,unsigned off,std::uint32_t value) { std::memcpy(static_cast<char*>(p)+off,&value,4); }
void set_current(void* menu) { *reinterpret_cast<void**>(mgr.data()+0x86090)=menu; }
void* __cdecl manager_stub() { return mgr.data(); }
void* __cdecl seats_stub() { return reinterpret_cast<void*>(0x72c4f0); }
void* __cdecl network_stub() { return reinterpret_cast<void*>(0xa3df68); }
void* __cdecl session_stub() { return reinterpret_cast<void*>(0xa53058); }
void* __cdecl campaign_stub() { return reinterpret_cast<void*>(0xa3bc40); }
void* __cdecl profile_stub() { return reinterpret_cast<void*>(0x72c530); }
void* __cdecl console_stub() { return reinterpret_cast<void*>(0x7ac290); }
void* __cdecl input_stub() { return input.data(); }
void __fastcall bind_stub(void*,void*,int p) { assert(controller_valid(p)); calls.push_back("bind:"+std::to_string(p)); }
bool step(const char* s) { calls.push_back(s); return fail!=s; }
bool __fastcall network_init(void*,void*) { return step("network"); }
bool __fastcall session_init(void*,void*) { return step("session"); }
bool __fastcall campaign_init(void*,void*) { return step("campaign"); }
void __fastcall mode_stub(void*,void*,int n) { assert(n==1);calls.push_back("mode"); }
void __fastcall name_stub(void*,void*,const char* s) { assert(valid_name(s));calls.push_back("name"); }
void __fastcall profile_name_stub(void*,void*,const char* s) { assert(valid_name(s));calls.push_back("profile"); }
bool __fastcall command_stub(void*,void*,const char* s) {
    assert(std::string_view(s)=="runscript saveloadProcess(2)");return step("persist");
}
bool __fastcall queue_stub(void*,void*,const char* s) { calls.push_back(std::string("queue:")+s);return false; }
void __fastcall request_stub(void*,void*,const char* name,const char*) { calls.push_back(std::string("request:")+name); }
void __fastcall tick_stub(void*,void*) { if(close_during_tick) set_current(nullptr); }
std::uint32_t __fastcall close_stub(void*,void*) { set_current(nullptr);return 0x12345678; }
void __cdecl text_stub(void*,const char* item,const char* format,...) {
    assert(std::string_view(format)=="%s");
    va_list args;va_start(args,format);texts[item]=va_arg(args,const char*);va_end(args);
}
void __fastcall hide_stub(void*,void*,const char* item,bool value) { assert(std::string_view(item)=="lc_inline_error");hidden=value; }
void* __fastcall item_stub(void*,void*,const char*) { return text_item.data(); }
std::array<unsigned,2> int_value{0x6905fc,0};
void* __fastcall argument_stub(void*,void*,int i) { assert(i==0);return int_value.data(); }
void redirect(unsigned va,const void* function) {
    auto* p=reinterpret_cast<unsigned char*>(va);
    DWORD old;assert(VirtualProtect(p,5,PAGE_EXECUTE_READWRITE,&old));p[0]=0xe9;
    const auto delta=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(function))-va-5;
    std::memcpy(p+1,&delta,4);assert(FlushInstructionCache(GetCurrentProcess(),p,5));
}
// Simulate the guarded native call site's register/stack ABI exactly.
__declspec(naked) void invoke_capture(void*,void*,int,unsigned) {
    __asm {
        push ebx
        mov eax,[esp+8]
        mov ecx,[esp+12]
        mov edx,[esp+16]
        mov ebx,[esp+20]
        push edx
        call eax
        pop ebx
        ret
    }
}
__declspec(naked) int invoke_usecmd(void*,void*,const char*) {
    __asm {
        push ebx
        push esi
        push edi
        mov eax,[esp+16]
        mov ecx,[esp+20]
        mov edx,[esp+24]
        push 0aabbccddh
        push 0bbccddeeh
        push 034567890h
        push 012345678h
        push edx
        call eax
        add esp,8
        cmp ebx,012345678h
        jne bad_frame
        cmp edi,034567890h
        jne bad_frame
        mov eax,1
        jmp done_frame
    bad_frame:
        xor eax,eax
    done_frame:
        pop edi
        pop esi
        pop ebx
        ret
    }
}
void fake_services() {
    redirect(0x5d8920,manager_stub);redirect(0x48de40,seats_stub);redirect(0x60b210,network_stub);
    redirect(0x612be0,session_stub);redirect(0x6097b0,campaign_stub);redirect(0x48fed0,profile_stub);
    redirect(0x55c890,console_stub);redirect(0x551ed0,input_stub);
    redirect(0x48d5e0,bind_stub);redirect(0x615dc0,network_init);redirect(0x610d80,name_stub);
    redirect(0x613ba0,session_init);redirect(0x60a4f0,campaign_init);redirect(0x60a770,mode_stub);
    redirect(0x48f3f0,profile_name_stub);redirect(0x55beb0,command_stub);redirect(0x5d5db0,request_stub);
    redirect(0x5aaeb0,tick_stub);redirect(0x5af5a0,close_stub);redirect(0x5adcf0,text_stub);
    redirect(0x5ade10,hide_stub);redirect(0x4d5830,argument_stub);
    redirect(0x5adc10,item_stub);put(text_item.data(),0,0x6a0244);
    redirect(0x55c410,queue_stub);
    put(mgr.data(),0,0x6a236c);put(input.data(),0,0x69a134);
    put(reinterpret_cast<void*>(0x72c4f0),0,0x6898c4);
    put(reinterpret_cast<void*>(0xa53058),0,0x6a481c);
    put(reinterpret_cast<void*>(0x72c530),0,0x689994);
    put(reinterpret_cast<void*>(0x7ac290),0,0x69a81c);
    std::strcpy(reinterpret_cast<char*>(0x72c530+7),"100%~$#|;openmenu bad");
    for(auto* pair:{&main_menu,&hub,&child}) { put(pair->data(),0,0x69e41c);(*pair)[4]=1; }
    put(main_menu.data(),0,0x69f134);
    std::strcpy(reinterpret_cast<char*>(main_menu.data()+0xc),"main");
    std::strcpy(reinterpret_cast<char*>(hub.data()+0xc),"lc_online");
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_code");
    set_current(main_menu.data());
}
void asset_files() {
    root=std::filesystem::current_path()/"temp"/("online-menus-"+mode);
    std::filesystem::create_directories(root/"Data");
    std::ofstream(root/"Data/lc_online_menus.cap",std::ios::binary)<<capability;
    for(const auto* menu:{"lc_online","lc_host","lc_code","lc_browse"}) {
        std::filesystem::create_directories(root/"UI/menus");
        for(const auto* ext:{".XMLB",".engb",".IGB"}) std::ofstream(root/"UI/menus"/(std::string(menu)+ext))<<"synthetic";
        std::filesystem::create_directories(root/"Packages/generated/maps/package/menus");
        if(mode!="missing-file" || std::string_view(menu)!="lc_code")
            std::ofstream(root/"Packages/generated/maps/package/menus"/(std::string(menu)+".PKGB"))<<"synthetic";
    }
    if(mode=="bad-marker") std::ofstream(root/"Data/lc_online_menus.cap",std::ios::binary)<<"unsupported-contract\n";
}
struct fake_engine {
    std::vector<std::string> sequence;
    std::string failure;
    bool call(const char* name) { sequence.push_back(name);return failure!=name; }
    bool preflight(int) { return call("preflight"); } bool bind(int) { return call("bind"); }
    bool network() { return call("network"); } bool name() { return call("name"); }
    bool session() { return call("session"); } bool campaign() { return call("campaign"); }
    bool mode() { return call("mode"); } bool persist() { return call("persist"); }
};
void rules() {
    assert(!enabled(std::nullopt) && !enabled("0") && !enabled("true") && !enabled("invalid"));
    assert(enabled("1 ; opt in"));
    for(int p:{-100,-1,4,100}) { fake_engine e;assert(!initialize(e,p) && e.sequence.empty()); }
    for(int p=0;p<4;++p) { fake_engine e;assert(initialize(e,p) && e.sequence.size()==8); }
    std::vector<std::string> expected{"preflight","bind","network","name","session","campaign","mode","persist"};
    for(unsigned i=0;i<expected.size();++i) {
        fake_engine e;e.failure=expected[i];assert(!initialize(e,2));
        assert(e.sequence==std::vector<std::string>(expected.begin(),expected.begin()+i+1));
    }
    assert(valid_name(std::string(31,'a')) && !valid_name(std::string(32,'a')) && !valid_name("   "));
    assert(display("100%~$#|\n")=="100%?????" && display(std::string(1000,'a')).size()==96);
    mailbox box;assert(!box.post(999,1,screen::hub));assert(box.post(2,1,screen::hub));
    assert(!box.post(3,1,screen::hub));assert(box.take(2,screen::hub)==0);
    assert(box.post(2,1,screen::hub));box.clear();assert(box.take(1,screen::hub)==0);
    script_functions_rules::overflow_table table;table.reset(0);assert(!registration_fits(table,320));
    table.reset(1);assert(registration_fits(table,321));
    script_functions_rules::func_entry entry{nullptr,function_name,"n","i"};
    assert(table.add(&entry)==script_functions_rules::added::added && table.find(function_name)==&entry);
    assert(!registration_fits(table,321));
}
}
namespace ini { std::optional<std::string> text(const wchar_t*,const wchar_t*) {
    return mode=="off" ? std::nullopt : mode=="zero" ? std::optional<std::string>("0") : std::optional<std::string>("1");
} }
namespace script_functions { int capacity() { return ::capacity; } }
namespace logger { void write(const char*,...) {} }
namespace game_version { void refuse_startup() { fatal=true; } }
namespace mod_loader {
unsigned generation() { return assets ? 1u : 0u; }
std::wstring game_root() { return root.wstring(); }
std::wstring resolve_file(const std::wstring& path) { return path; }
}

int main(int argc,char** argv) {
    assert(argc>=2);if(argc>=3) mode=argv[2];rules();
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char> image((std::istreambuf_iterator<char>(file)),{});assert(image.size()>0x1000);
    auto u16=[&](unsigned at) { unsigned short n;std::memcpy(&n,image.data()+at,2);return n; };
    auto u32=[&](unsigned at) { unsigned n;std::memcpy(&n,image.data()+at,4);return n; };
    const auto pe=u32(0x3c),section=pe+24+u16(pe+20);
    DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(0x401000),0x700000,PAGE_EXECUTE_READWRITE,&old));
    for(unsigned i=0;i<u16(pe+6);++i) {
        const auto s=section+40*i;
        std::memcpy(reinterpret_cast<void*>(0x400000+u32(s+12)),image.data()+u32(s+20),u32(s+16));
    }
    for(const auto& g:online_menus_guards::guards) {
        const auto bytes=patch_transaction::bytes_of(g.hex);
        assert(std::memcmp(reinterpret_cast<void*>(g.va),bytes.data(),bytes.size())==0);
    }
    std::vector<unsigned char> before(reinterpret_cast<unsigned char*>(0x401000),reinterpret_cast<unsigned char*>(0xb01000));
    if(mode=="rollback" || mode=="protect" || mode=="flush" || mode=="retain") {
        auto c=online_menus::contribution();const auto site=c.writes.front().va;
        patch_transaction::transaction tx;tx.add(std::move(c));patch_transaction::hooks hooks;
        if(mode=="rollback" || mode=="retain") hooks.after_apply=[&] { *reinterpret_cast<unsigned char*>(site)^=1; };
        if(mode=="retain") hooks.after_rollback=[&] { *reinterpret_cast<unsigned char*>(site)^=1; };
        if(mode=="protect") hooks.protect=[](void*,std::size_t,DWORD,DWORD*) { return false; };
        if(mode=="flush") { bool first=true;hooks.flush=[first](void*,std::size_t) mutable { const bool ok=!first;first=false;return ok; }; }
        const auto result=tx.commit(&hooks);
        assert(result==(mode=="protect" ? patch_transaction::outcome::refused_protect : mode=="retain" ? patch_transaction::outcome::rollback_failed : patch_transaction::outcome::rolled_back));
        if(mode!="retain") assert(std::memcmp(before.data(),reinterpret_cast<void*>(0x401000),before.size())==0);
        std::cout<<"online menus "<<mode<<" PASS\n";return 0;
    }
    if(mode.starts_with("guard")) {
        const auto i=std::stoul(mode.substr(5));assert(i<std::size(online_menus_guards::guards));
        *reinterpret_cast<unsigned char*>(online_menus_guards::guards[i].va)^=1;
        std::memcpy(before.data(),reinterpret_cast<void*>(0x401000),before.size());
    }
    auto& overflow=script_function_overflow::table();overflow.reset(704);
    if(mode=="capacity") { ::capacity=320;overflow.reset(0); }
    online_menus::install(reinterpret_cast<HMODULE>(mode=="base" ? 0x500000 : 0x400000));
    assert(!fatal);
    if(mode=="off" || mode=="zero" || mode=="base" || mode=="capacity" || mode.starts_with("guard")) {
        assert(!online_menus::installed() && !overflow.find(function_name));
        assert(std::memcmp(before.data(),reinterpret_cast<void*>(0x401000),before.size())==0);
        std::cout<<"online menus "<<mode<<" PASS (no writes or registrations)\n";return 0;
    }
    assert(online_menus::installed());const auto* entry=overflow.find(function_name);assert(entry && std::string_view(entry->args)=="i");
    assert(script_function_overflow::script_overflow_find(function_name)==entry);
    const auto installed_image=std::vector<unsigned char>(reinterpret_cast<unsigned char*>(0x401000),reinterpret_cast<unsigned char*>(0xb01000));
    online_menus::install(reinterpret_cast<HMODULE>(0x400000));
    assert(overflow.count()==1 && std::memcmp(installed_image.data(),reinterpret_cast<void*>(0x401000),installed_image.size())==0);
    asset_files();fake_services();
    if(mode=="name31" || mode=="name32") {
        const auto n=mode=="name31" ? 31u : 32u;
        std::memset(reinterpret_cast<void*>(0x72c530+7),'a',n);
        *reinterpret_cast<char*>(0x72c530+7+n)=0;
    }
    if(mode=="blank-name") *reinterpret_cast<char*>(0x72c530+7)=0;
    using request_t=void(__fastcall*)(void*,void*,const char*,const char*);
    using tick_t=void(__fastcall*)(void*,void*);
    const auto request=*reinterpret_cast<request_t*>(0x6a23d8);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    const auto queue_target=reinterpret_cast<void*>(0x5bc3d5+*reinterpret_cast<unsigned*>(0x5bc3d1));
    const auto queue=[&](void* object,void*,const char* command) { return invoke_usecmd(queue_target,object,command); };
    const auto close=*reinterpret_cast<std::uint32_t(__fastcall**)(void*,void*)>(0x69e464);
    const auto relative=*reinterpret_cast<unsigned*>(0x5c971c);
    auto capture=reinterpret_cast<void*>(0x5c9720+relative);
    request(mgr.data(),nullptr,"online",nullptr);assert(calls.empty()); // no controller, no network pump
    invoke_capture(capture,mgr.data(),4,0x10);request(mgr.data(),nullptr,"online",nullptr);assert(calls.empty());
    invoke_capture(capture,mgr.data(),2,0x20);request(mgr.data(),nullptr,"online",nullptr);assert(calls.empty()); // navigation isn't Accept
    invoke_capture(capture,mgr.data(),2,0x10);
    if(mode.starts_with("fail-")) fail=mode.substr(5);
    if(mode=="assets") assets=false;
    request(mgr.data(),nullptr,"online",nullptr);
    if(!fail.empty() || mode=="assets" || mode=="bad-marker" || mode=="missing-file" || mode=="name32") {
        assert(calls.back()=="request:online");
        if(fail.empty()) assert(calls.size()==1);
        else { const auto it=std::find(calls.begin(),calls.end(),fail);assert(it!=calls.end() && it+2==calls.end()); }
        std::cout<<"online menus "<<mode<<" PASS (native fallback)\n";return 0;
    }
    const std::vector<std::string> expected{"bind:2","network","name","session","campaign","mode","profile","persist","request:lc_online"};
    assert(calls==expected);
    request(mgr.data(),nullptr,"online",nullptr);assert(calls==expected); // duplicate entry cannot reinitialize
    set_current(hub.data());tick(hub.data(),nullptr);
    assert(texts["local_name"]==(mode=="name31" ? std::string(31,'a') : mode=="blank-name" ? "Player" : "100%????;openmenu bad"));
    assert(texts["service"]=="Offline preview" && hidden);
    assert(queue(nullptr,nullptr,"openmenu original"));assert(calls.back()=="queue:openmenu original");calls.pop_back();
    texts.clear();put(text_item.data(),0xa0,1);tick(hub.data(),nullptr);assert(texts.empty());put(text_item.data(),0xa0,0);
    texts.clear();hub[4]=0;tick(hub.data(),nullptr);assert(texts.empty());hub[4]=1;
    auto post=[&](int value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(entry->handler))(nullptr); };
    post(int(intent::unavailable));tick(hub.data(),nullptr);assert(!hidden);
    post(int(intent::dismiss));tick(hub.data(),nullptr);assert(hidden);
    // Exercise real usecmd's queue boundary, then close before UI drain: no
    // script remains queued to target the newly opened/reused menu next frame.
    texts.clear();assert(queue(nullptr,nullptr,"runscript lcOnlineIntent(3)"));close_during_tick=true;tick(hub.data(),nullptr);
    assert(texts.empty() && calls==expected);close_during_tick=false;
    assert(close(hub.data(),nullptr)==0x12345678);set_current(hub.data());tick(hub.data(),nullptr);
    assert(calls==expected); // close invalidated the old intent, including reused address
    set_current(main_menu.data());assert(queue(nullptr,nullptr,"runscript lcOnlineIntent(2)"));assert(calls==expected);set_current(hub.data());
    post(int(intent::code));post(int(intent::host));tick(hub.data(),nullptr);assert(calls.back()=="request:lc_code");
    set_current(child.data());post(int(intent::back));tick(child.data(),nullptr);assert(calls.back()=="request:lc_online");
    set_current(hub.data());post(int(intent::other));tick(hub.data(),nullptr);assert(calls.back()=="request:online");
    set_current(hub.data());post(int(intent::back));tick(hub.data(),nullptr);assert(calls.back()=="request:main");
    set_current(main_menu.data());invoke_capture(capture,mgr.data(),3,0x10);request(mgr.data(),nullptr,"online",nullptr);
    assert(calls[calls.size()-9]=="bind:3" && calls.back()=="request:lc_online");
    std::cout<<"online menus "<<mode<<" PASS (guards, hooks, ABI, order, single init, no-controller wait, closed binding, escaping, fallback, reentry)\n";
}

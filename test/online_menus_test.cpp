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
#include <stdexcept>
#include <new>

namespace {
using namespace online_menus_rules;
std::string mode="on",fail;
bool fatal=false,assets=true,close_during_tick=false;
int capacity=1024;
DWORD ticks=10000;
unsigned asset_generation=1;
int root_reads=0,asset_throw=0,request_throw=0,tick_throw=0,native_ticks=0;
int manager_reads=0,name_fault_stage=0,name_bad_object=0;
void name_fault_at(int stage) {
    if(name_fault_stage==stage) { name_fault_stage=0;RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr); }
}
bool back_in_base=false;
void (*base_mutation)()=nullptr;
int (__fastcall* lobby_back_callback)(void*,void*)=nullptr;
bool missing_prompt=false,null_argument=false;
DWORD clock_stub() { return ticks; }
void throw_fault(int kind) {
    if(kind==1) throw std::bad_alloc();
    if(kind==2) throw std::filesystem::filesystem_error("synthetic",std::make_error_code(std::errc::invalid_argument));
}
void request_fault_stub() { throw_fault(request_throw); }
void tick_fault_stub() { throw_fault(tick_throw); }
void fail_registration() { script_function_overflow::table().reset(0); }
std::filesystem::path root;
std::vector<std::string> calls;
std::map<std::string,std::string> texts;
std::map<std::string,std::string> once_logs;
unsigned text_writes=0,hide_writes=0,enable_writes=0;
bool hidden=true;
std::map<std::string,bool> enabled_items;
std::array<unsigned char,0xb0> start_item{};
void __fastcall enable_stub(void*,void*,const char* name,bool value) {
    ++enable_writes;enabled_items[name]=value;
    if(std::string_view(name)=="text_startgame") start_item[0x54]=value ? 8 : 0;
}
unsigned mask=0;
unsigned __fastcall mask_stub(void*,void*) { return mask; }
std::vector<unsigned char> mgr(0xb0000),input(0x100);
std::array<unsigned char,0x1a00> main_menu{},hub{},child{};
std::array<unsigned char,0xb0> text_item{};
std::array<unsigned char,0x100> lobby_names{},lobby_checks{};
bool missing_list=false,missing_ready_list=false,lobby_marker=true;
void put(void* p,unsigned off,std::uint32_t value) { std::memcpy(static_cast<char*>(p)+off,&value,4); }
void set_current(void* menu) { *reinterpret_cast<void**>(mgr.data()+0x86090)=menu; }
void* __cdecl manager_stub() { ++manager_reads;return mgr.data(); }
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
void __fastcall name_stub(void*,void*,const char* s) { name_fault_at(9);assert(valid_name(s));calls.push_back("name"); }
void __fastcall profile_name_stub(void*,void*,const char* s) { name_fault_at(10);assert(valid_name(s));calls.push_back("profile"); }
bool __fastcall command_stub(void*,void*,const char* s) {
    name_fault_at(11);assert(std::string_view(s)=="runscript saveloadProcess(2)");return step("persist");
}
bool __fastcall queue_stub(void*,void*,const char* s) { calls.push_back(std::string("queue:")+s);return false; }
void __fastcall request_stub(void*,void*,const char* name,const char*) { if(std::string_view(name)=="text_entry") name_fault_at(12);calls.push_back(std::string("request:")+name); }
void __fastcall deactivate_stub(void*,void*) { set_current(nullptr); }
void __fastcall tick_stub(void* menu,void*) {
    ++native_ticks;
    if(base_mutation) base_mutation();
    if(back_in_base) { assert(lobby_back_callback);lobby_back_callback(menu,nullptr); }
    if(close_during_tick) set_current(nullptr);
}
std::uint32_t __fastcall close_stub(void*,void*) { set_current(nullptr);return 0x12345678; }
void __cdecl text_stub(void*,const char* item,const char* format,...) {
    ++text_writes;
    if(mode.starts_with("r4-") && std::string_view(format)!="%s") {
        assert(std::string_view(format)=="Unready" || std::string_view(format)=="Leave");texts[item]=format;return;
    }
    assert(std::string_view(format)=="%s");
    va_list args;va_start(args,format);texts[item]=va_arg(args,const char*);va_end(args);
}
std::map<std::string,bool> visibility;
void __fastcall hide_stub(void*,void*,const char* item,bool value) {
    ++hide_writes;visibility[item]=value;if(std::string_view(item)=="lc_inline_error")hidden=value;
    if(mode.starts_with("r4-")) {
        auto* list=std::string_view(item)=="text_playername1" ? lobby_names.data() : std::string_view(item)=="text_readycheck1" ? lobby_checks.data() : nullptr;
        if(list) list[0x54]=static_cast<unsigned char>((list[0x54]&~4) | (value ? 4 : 0));
    }
}
void* __fastcall item_stub(void*,void*,const char* name) {
    if(mode.starts_with("r4-")) {
        if(std::string_view(name)=="lc_lobby_v1" && !lobby_marker) return nullptr;
        if(std::string_view(name)=="text_playername1") return missing_list ? nullptr : lobby_names.data();
        if(std::string_view(name)=="text_readycheck1") return missing_ready_list ? nullptr : lobby_checks.data();
        if(std::string_view(name)=="text_startgame") return start_item.data();
    }
    return missing_prompt && std::string_view(name)=="lc_online_continue" ? nullptr : text_item.data();
}
std::array<unsigned,2> int_value{0x6905fc,0};
void* __fastcall argument_stub(void*,void*,int i) { assert(i==0);return null_argument ? nullptr : int_value.data(); }
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
std::array<char,256> native_seed{},native_result{};
std::array<char,64> native_title{};
bool native_ready=false;
int native_tag=4,native_max=0,consumes=0;
void* __cdecl name_request_stub() { name_fault_at(13);return name_bad_object==1 ? nullptr : reinterpret_cast<void*>(name_bad_object==2 ? 0xa3e16c : 0xa3e168); }
bool __fastcall ready_stub(void*,void*) { name_fault_at(1);return native_ready; }
int __fastcall tag_stub(void*,void*) { name_fault_at(2);return native_tag; }
void __fastcall set_tag_stub(void*,void*,int n) { name_fault_at(3);native_tag=n; }
void __fastcall max_stub(void*,void*,int n) { name_fault_at(4);native_max=n; }
void __fastcall seed_stub(void*,void*,const char* p) {
    name_fault_at(5);
    std::memcpy(native_seed.data(),p,256);
    const auto n=std::strlen(p); assert(n<=31);
    for(unsigned i=unsigned(n);i<256;++i) assert(p[i]==0);
}
void __fastcall title_stub(void*,void*,const char* p) {
    name_fault_at(6);
    std::memcpy(native_title.data(),p,64);assert(std::string_view(p)=="Screen name");
    for(unsigned i=11;i<64;++i) assert(p[i]==0);
}
const char* __fastcall consume_stub(void*,void*) {
    name_fault_at(7);
    const bool null_result=name_fault_stage==8;if(null_result) name_fault_stage=0;
    ++consumes;native_ready=false;native_tag=4;native_seed.fill(0);native_title.fill(0);return null_result ? nullptr : native_result.data();
}
void result(std::string_view text,int tag=0) {
    native_result.fill(0);std::copy(text.begin(),text.end(),native_result.begin());native_ready=true;native_tag=tag;
}
void fake_services() {
    redirect(0x60c7e0,name_request_stub);redirect(0x60b960,ready_stub);redirect(0x60b9a0,tag_stub);
    redirect(0x60b990,set_tag_stub);redirect(0x60b970,max_stub);redirect(0x60c6e0,seed_stub);
    redirect(0x60c710,title_stub);redirect(0x60c780,consume_stub);
    redirect(0x5d4b00,mask_stub);redirect(0x5d8920,manager_stub);redirect(0x48de40,seats_stub);redirect(0x60b210,network_stub);
    redirect(0x612be0,session_stub);redirect(0x6097b0,campaign_stub);redirect(0x48fed0,profile_stub);
    redirect(0x55c890,console_stub);redirect(0x551ed0,input_stub);
    redirect(0x48d5e0,bind_stub);redirect(0x615dc0,network_init);redirect(0x610d80,name_stub);
    redirect(0x613ba0,session_init);redirect(0x60a4f0,campaign_init);redirect(0x60a770,mode_stub);
    redirect(0x48f3f0,profile_name_stub);redirect(0x55beb0,command_stub);redirect(0x5d5db0,request_stub);
    redirect(0x5ace30,deactivate_stub);redirect(0x5aaeb0,tick_stub);redirect(0x5af5a0,close_stub);redirect(0x5adcf0,text_stub);
    redirect(0x5ade70,enable_stub);redirect(0x5ade10,hide_stub);redirect(0x4d5830,argument_stub);
    redirect(0x5adc10,item_stub);put(text_item.data(),0,0x6a0244);
    put(start_item.data(),0,0x6a0244);
    redirect(0x55c410,queue_stub);
    put(mgr.data(),0,0x6a236c);put(input.data(),0,0x69a134);
    put(reinterpret_cast<void*>(0x8aff18),0,unsigned(reinterpret_cast<std::uintptr_t>(mgr.data())));
    put(reinterpret_cast<void*>(0xa3e168),0,0x6a4730);
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
    texts["service"]="Offline preview";texts["self_state"]="Preview";
    set_current(main_menu.data());
}
void asset_files() {
    root=std::filesystem::current_path()/"temp"/("online-menus-"+mode+"-"+std::to_string(GetCurrentProcessId()));
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
// Replace only this fixture process's imported clipboard calls. No system
// clipboard ownership or content is ever changed/read by these tests.
void import_stub(const char* name,void* replacement) {
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto* dll=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;dll->Name;++dll) {
        auto* names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+dll->OriginalFirstThunk);
        auto* slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+dll->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if(std::strcmp(reinterpret_cast<const char*>(entry->Name),name)) continue;
            DWORD old;assert(VirtualProtect(&slots->u1.Function,4,PAGE_READWRITE,&old));
            slots->u1.Function=reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored;assert(VirtualProtect(&slots->u1.Function,4,old,&ignored));return;
        }
    }
    assert(false && "clipboard import missing");
}
std::vector<std::uint16_t> clip_data;
bool clip_available=true,clip_lockable=true,clip_missing=false,clip_zero_size=false;
SIZE_T clip_bytes=0;
int clip_opened=0,clip_closed=0,clip_locked=0,clip_unlocked=0;
BOOL WINAPI clip_open(HWND) { ++clip_opened;return clip_available; }
BOOL WINAPI clip_close() { ++clip_closed;return TRUE; }
HANDLE WINAPI clip_get(UINT format) { assert(format==CF_UNICODETEXT);return clip_missing ? nullptr : reinterpret_cast<HANDLE>(0x1234); }
SIZE_T WINAPI clip_size(HGLOBAL h) { assert(h==reinterpret_cast<HANDLE>(0x1234));return clip_zero_size ? 0 : clip_bytes ? clip_bytes : clip_data.size()*2; }
LPVOID WINAPI clip_lock(HGLOBAL h) { assert(h==reinterpret_cast<HANDLE>(0x1234));++clip_locked;return clip_lockable ? clip_data.data() : nullptr; }
BOOL WINAPI clip_unlock(HGLOBAL h) { assert(h==reinterpret_cast<HANDLE>(0x1234));++clip_unlocked;return FALSE; }
void clipboard_text(std::string_view s) {
    clip_data.clear();for(unsigned char ch:s)clip_data.push_back(ch);clip_data.push_back(0);
    clip_available=clip_lockable=true;clip_missing=clip_zero_size=false;clip_bytes=0;clip_opened=clip_closed=clip_locked=clip_unlocked=0;
}
void fake_clipboard_imports() {
    import_stub("OpenClipboard",reinterpret_cast<void*>(&clip_open));import_stub("CloseClipboard",reinterpret_cast<void*>(&clip_close));
    import_stub("GetClipboardData",reinterpret_cast<void*>(&clip_get));import_stub("GlobalSize",reinterpret_cast<void*>(&clip_size));
    import_stub("GlobalLock",reinterpret_cast<void*>(&clip_lock));import_stub("GlobalUnlock",reinterpret_cast<void*>(&clip_unlock));
}
struct fake_clipboard {
    std::vector<std::uint16_t> data;
    bool available=true,lockable=true;
    std::size_t byte_size=0;
    int opened=0,closed=0,locked=0,unlocked=0;
    bool open() { ++opened;return available; }
    void close() { ++closed; }
    std::size_t size() { return byte_size ? byte_size : data.size()*2; }
    const std::uint16_t* lock() { ++locked;return lockable ? data.data() : nullptr; }
    void unlock() { ++unlocked; }
};
void input_rules() {
    assert(alphabet.size()==31);
    for(auto text:{"abcd-efgh"," ABCDEFGH ","\r\nabcdefgh\t"}) assert(parse_code(text)=="ABCDEFGH");
    for(auto text:{"ABCD0FGH","ABCDOFGH","ABCD1FGH","ABCDIFGH","ABCDLFGH","ABCD-EFGHjunk","ABCD--EFGH","ABC DEFGH","ABCD/EFGH","ABCDEFG","ABCDEFGHI"}) assert(!parse_code(text));
    assert(!parse_code(std::string(257,' ')+"ABCDEFGH"));
    assert(!parse_code(std::string("ABCDEFGH\0junk",13)));
    auto clip=[](std::string_view value) { fake_clipboard c; for(unsigned char ch:value)c.data.push_back(ch);c.data.push_back(0);return c; };
    auto valid=clip(" abcd-efgh\n");assert(read_clipboard(valid)=="ABCDEFGH");assert(valid.closed==1 && valid.locked==1 && valid.unlocked==1);
    auto bad_clip=clip("ABCDEFGH");bad_clip.available=false;assert(!read_clipboard(bad_clip) && !bad_clip.closed && !bad_clip.locked);
    bad_clip=clip("ABCDEFGH");bad_clip.lockable=false;assert(!read_clipboard(bad_clip) && bad_clip.closed==1 && bad_clip.locked==1 && !bad_clip.unlocked);
    for(unsigned bytes:{1,3,4098}) { bad_clip=clip("ABCDEFGH");bad_clip.byte_size=bytes;assert(!read_clipboard(bad_clip) && bad_clip.closed==1 && !bad_clip.locked); }
    for(auto text:{"ABCDEFGHjunk","ABCD0FGH"," ","ABCD-EFGH\nextra"}) { bad_clip=clip(text);assert(!read_clipboard(bad_clip) && bad_clip.unlocked==1 && bad_clip.closed==1); }
    bad_clip=clip(std::string(257,'A'));assert(!read_clipboard(bad_clip) && bad_clip.unlocked==1);
    bad_clip.data.assign(8,'A');assert(!read_clipboard(bad_clip)); // no terminator
    for(unsigned ch:{0xd800,0xdc00,0xfffd,0x100}) { bad_clip=clip("ABCDEFGH");bad_clip.data[0]=std::uint16_t(ch);assert(!read_clipboard(bad_clip) && bad_clip.unlocked==1); }
    code_model c;c.open();for(char ch:std::string("ABCDEFGH")) assert(c.append(ch));
    auto ticket=c.resolve();assert(ticket && !c.resolve() && c.complete(ticket));
    assert(!c.append('J'));c.erase();assert(!c.complete(ticket));c.append('J');assert(c.resolve());
    ticket=c.resolve(true);c.close();assert(!c.complete(ticket));c.open();assert(!c.complete(ticket));
    name_request n;assert(n.begin(std::string(31,'x')));assert(n.seed[31]==0 && n.seed[255]==0 && n.title[63]==0);assert(!n.begin("other"));
    assert(!n.finish(0,n.seed.data(),256));assert(!n.begin(std::string(32,'x')));
    assert(n.begin("old"));std::array<char,256> text{};std::fill_n(text.begin(),31,'n');assert(n.finish(0,text.data(),256)==std::string(31,'n'));
    assert(n.begin("old"));text[31]='n';assert(!n.finish(0,text.data(),256));
    assert(n.begin("old"));assert(!n.finish(3,"new",4));
    assert(n.begin("old"));assert(!n.finish(4,nullptr,0));
    assert(n.begin("old"));text.fill('x');assert(!n.finish(0,text.data(),256));
    key_edges keys;std::array<unsigned char,256> state{};state[30]=128;assert(!keys.sample(state.data())[30]);
    state[30]=0;keys.sample(state.data());state[30]=128;assert(keys.sample(state.data())[30]);assert(!keys.sample(state.data())[30]);keys.reset();assert(!keys.sample(state.data())[30]);
    recent_game r{"ABCDEFGH","Offline fixture",123};const auto saved=encode_recent(r);const auto loaded=decode_recent(saved);
    assert(loaded && loaded->code==r.code && loaded->name==r.name && loaded->time==123);
    for(auto bad:{"lc-rejoin/1\nABCDEFGH\nName\n18446744073709551616\n","lc-rejoin/1\nABCDEFGH\nName\n0\n","lc-rejoin/1\nABCDEFGH\nName\n12\nextra"}) assert(!decode_recent(bad));
    assert(!decode_recent(std::string(129,'x')));
}
void rules() {
    input_rules();
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
    assert(display("100%~$#|\n")=="100??????" && display(std::string(1000,'a')).size()==96);
    assert(display("Alpha-9 _.' ,")=="Alpha-9 _.' ,");
    assert(display("^n")=="?n" && display("\\n")=="?n" && display("$~#|%:;")=="???????");
    assert(display("caret^slash\\n")=="caret?slash?n");
    assert(accept_fresh(2,10,1010) && !accept_fresh(2,10,1011) && !accept_fresh(-1,0,0));
    assert(accept_fresh(0,0xfffffff0u,4));
    entry_latch lease;lease.start(123,0xfffffff0u);assert(lease.waiting(123,0xfffffff0u+2499));
    assert(!lease.waiting(124,0xfffffff0u+2499) && lease.pending);
    assert(!lease.waiting(123,0xfffffff0u+2500) && !lease.pending);
    mailbox box;assert(!box.post(1,1,screen::hub));assert(!box.post(999,1,screen::hub));assert(box.post(2,1,screen::hub));
    assert(!box.post(3,1,screen::hub));assert(box.take(2,screen::hub)==0);
    assert(box.post(2,1,screen::hub));box.clear();assert(box.take(1,screen::hub)==0);
    script_functions_rules::overflow_table table;table.reset(0);assert(!registration_fits(table,320));
    table.reset(1);assert(registration_fits(table,321));
    script_functions_rules::func_entry entry{nullptr,function_name,"n","i"};
    assert(table.add(&entry)==script_functions_rules::added::added && table.find(function_name)==&entry);
    assert(!registration_fits(table,321));
}
#include "online_lobby_fixture.inl"
#include "online_menus_followup.inl"
}
namespace ini { std::optional<std::string> text(const wchar_t*,const wchar_t*) {
    return mode=="off" ? std::nullopt : mode=="zero" ? std::optional<std::string>("0") : std::optional<std::string>("1");
} }
namespace script_functions { int capacity() { return ::capacity; } }
namespace logger {
void write(const char*,...) {}
void write_once(std::string_view key,const char* format,...) {
    if(once_logs.contains(std::string(key))) return;
    char value[512]{};va_list args;va_start(args,format);std::vsnprintf(value,sizeof(value),format,args);va_end(args);
    once_logs.emplace(key,value);
}
}
namespace game_version { void refuse_startup() { fatal=true; } }
namespace mod_loader {
unsigned generation() { return assets ? asset_generation : 0u; }
std::wstring game_root() { ++root_reads;throw_fault(asset_throw);return root.wstring(); }
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
    online_menus::install_hooks install_hooks;
    install_hooks.clock=clock_stub;install_hooks.before_request=request_fault_stub;install_hooks.before_tick=tick_fault_stub;
    if(mode.starts_with("r4-")) install_hooks.lobby_fields=lobby_fields_stub;
    patch_transaction::hooks transaction_hooks;
    if(mode=="install-retain") {
        const auto site=online_menus::contribution().writes.front().va;
        transaction_hooks.after_apply=[site] { *reinterpret_cast<unsigned char*>(site)^=1; };
        transaction_hooks.after_rollback=[site] { *reinterpret_cast<unsigned char*>(site)^=1; };
        install_hooks.transaction=&transaction_hooks;
    }
    if(mode=="install-add-fail") install_hooks.before_registration=fail_registration;
    online_menus::install(reinterpret_cast<HMODULE>(mode=="base" ? 0x500000 : 0x400000),&install_hooks);
    if(mode=="install-retain" || mode=="install-add-fail") {
        assert(fatal && !online_menus::installed() && !overflow.find(function_name));
        assert(std::memcmp(before.data(),reinterpret_cast<void*>(0x401000),before.size())!=0);
        std::cout<<"online menus "<<mode<<" PASS (production install refuses startup)\n";return 0;
    }
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
    if(mode.starts_with("r4-")) { lobby_tests(entry);return 0; }
    SetEnvironmentVariableW(L"LOCALAPPDATA",(root/L"local").c_str());
    if(mode.starts_with("follow-")) follow_prepare();
    if(mode=="r3-load" || mode=="r3-load-invalid") {
        std::filesystem::create_directories(root/L"local/XML2 Fix/online");
        std::ofstream file(root/L"local/XML2 Fix/online/last-game.txt",std::ios::binary);
        file<<(mode=="r3-load" ? encode_recent({"23456789","Remembered fixture",123}) : std::string(512,'x'));
    }
    if(mode=="r3-local-only") SetEnvironmentVariableW(L"LOCALAPPDATA",L"\\\\no-such-rejoin-server\\share");
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
    const auto deactivate=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e438);
    const auto relative=*reinterpret_cast<unsigned*>(0x5c971c);
    auto capture=reinterpret_cast<void*>(0x5c9720+relative);
    // Verify unavailable custom entry forwards even WITHOUT a capture.
    if(mode=="assets" || mode=="bad-marker" || mode=="missing-file" || mode=="asset-badalloc" || mode=="asset-filesystem" || mode=="review-no-prompt") {
        if(mode=="assets") assets=false;
        if(mode=="asset-badalloc") asset_throw=1;
        if(mode=="asset-filesystem") asset_throw=2;
        if(mode=="review-no-prompt") missing_prompt=true;
        request(mgr.data(),nullptr,"online",nullptr);assert(calls==std::vector<std::string>{"request:online"});
        invoke_capture(capture,mgr.data(),2,0x10);++ticks;
        asset_throw=0;request(mgr.data(),nullptr,"online",nullptr);
        if(mode=="assets" || mode=="bad-marker" || mode=="missing-file") assert(calls==std::vector<std::string>({"request:online","request:online"}));
        else assert(calls.size()==10 && calls.back()=="request:lc_online");
        std::cout<<"online menus "<<mode<<" PASS (native fallback without capture / exception containment)\n";return 0;
    }
    if(mode=="review-entry") {
        request(mgr.data(),nullptr,"online",nullptr);
        assert(calls.empty() && !visibility["lc_online_continue"] && root_reads==1);
        missing_prompt=true;request(mgr.data(),nullptr,"online",nullptr);assert(calls==std::vector<std::string>{"request:online"});
        missing_prompt=false;calls.clear();request(mgr.data(),nullptr,"online",nullptr);assert(calls.empty());
        invoke_capture(capture,mgr.data(),2,0x10);ticks+=1000;
        request(mgr.data(),nullptr,"online",nullptr);assert(calls.size()==9 && visibility["lc_online_continue"]);
        // A popup resource close and unrelated custom tick cannot clear the lease.
        close(child.data(),nullptr);set_current(child.data());tick(child.data(),nullptr);set_current(main_menu.data());
        ticks+=2499;invoke_capture(capture,mgr.data(),2,0x10);request(mgr.data(),nullptr,"online",nullptr);assert(calls.size()==9);
        ++ticks;invoke_capture(capture,mgr.data(),3,0x10);request(mgr.data(),nullptr,"online",nullptr);assert(calls.size()==18 && calls[calls.size()-9]=="bind:3");
        set_current(hub.data());tick(hub.data(),nullptr);set_current(main_menu.data());
        invoke_capture(capture,mgr.data(),2,0x10);ticks+=1001;
        request(mgr.data(),nullptr,"online",nullptr);assert(calls.size()==18 && !visibility["lc_online_continue"]);
        invoke_capture(capture,mgr.data(),2,0x10);request(mgr.data(),nullptr,"online",nullptr);assert(calls.size()==27);
        assert(root_reads==1); // first successful asset validation is cached
        ++asset_generation;request(mgr.data(),nullptr,"online",nullptr);assert(root_reads==2 && calls.size()==27);
        ++asset_generation;asset_throw=1;request(mgr.data(),nullptr,"online",nullptr);assert(root_reads==3 && calls.size()==28 && calls.back()=="request:online");
        asset_throw=0;invoke_capture(capture,mgr.data(),2,0x10);request(mgr.data(),nullptr,"online",nullptr);assert(root_reads==4 && calls.size()==37);
        set_current(hub.data());tick(hub.data(),nullptr);queue(nullptr,nullptr,"runscript lcOnlineIntent(9)");tick(hub.data(),nullptr);
        ticks+=5000;tick(hub.data(),nullptr);assert(native_tag==0);
        ++ticks;tick(hub.data(),nullptr);assert(native_tag==4 && !visibility["lc_notice_name_open"]);
        std::cout<<"review entry PASS (1000/1001 ms; 2499/2500 ms; scoped lease; prompt; cached assets)\n";return 0;
    }
    if(mode=="review-exceptions") {
        for(int kind:{1,2}) { request_throw=kind;request(mgr.data(),nullptr,"online",nullptr); }
        assert(calls==std::vector<std::string>({"request:online","request:online"}));request_throw=0;
        invoke_capture(capture,mgr.data(),2,0x10);request(mgr.data(),nullptr,"online",nullptr);assert(calls.size()==11);
        set_current(hub.data());
        for(int kind:{1,2}) { const auto n=native_ticks;tick_throw=kind;tick(hub.data(),nullptr);assert(native_ticks==n+1); }
        tick_throw=0;tick(hub.data(),nullptr);assert(texts["local_name"]=="100??????openmenu bad");
        set_current(child.data());tick(child.data(),nullptr);
        for(int i=32;i<40;++i) { const auto command="runscript lcOnlineIntent("+std::to_string(i)+")";queue(nullptr,nullptr,command.c_str());tick(child.data(),nullptr); }
        tick_throw=1;tick(child.data(),nullptr);tick_throw=0;tick(child.data(),nullptr);
        assert(texts["code_value"]=="ABCD-EFGH" && visibility["lc_notice_fixture"]);
        assert(!std::filesystem::exists(root/L"local/XML2 Fix/online/last-game.txt"));
        std::cout<<"review exceptions PASS (request/tick bad_alloc and filesystem_error; native tick once)\n";return 0;
    }
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
    assert(texts["local_name"]==(mode=="name31" ? std::string(31,'a') : mode=="blank-name" ? "Player" : "100??????openmenu bad"));
    assert(texts["service"]=="Offline preview" && hidden);
    if(mode.starts_with("follow-")) { follow_tests(entry);return 0; }
    assert(queue(nullptr,nullptr,"openmenu original"));assert(calls.back()=="queue:openmenu original");calls.pop_back();
    auto writes_before=text_writes;put(text_item.data(),0xa0,1);tick(hub.data(),nullptr);assert(text_writes==writes_before);put(text_item.data(),0xa0,0);
    writes_before=text_writes;hub[4]=0;tick(hub.data(),nullptr);assert(text_writes==writes_before);hub[4]=1;
    auto post=[&](int value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(entry->handler))(nullptr); };
    post(int(intent::unavailable));tick(hub.data(),nullptr);assert(!visibility["lc_notice_unavailable"]);
    post(int(intent::dismiss));tick(hub.data(),nullptr);assert(visibility["lc_notice_unavailable"] && hidden);
    // Exercise real usecmd's queue boundary, then close before UI drain: no
    // script remains queued to target the newly opened/reused menu next frame.
    writes_before=text_writes;assert(queue(nullptr,nullptr,"runscript lcOnlineIntent(3)"));close_during_tick=true;tick(hub.data(),nullptr);
    assert(text_writes==writes_before && calls==expected);close_during_tick=false;
    assert(close(hub.data(),nullptr)==0x12345678);set_current(hub.data());tick(hub.data(),nullptr);
    assert(calls==expected); // close invalidated the old intent, including reused address
    set_current(main_menu.data());assert(queue(nullptr,nullptr,"runscript lcOnlineIntent(2)"));assert(calls.back()=="queue:runscript lcOnlineIntent(2)");calls.pop_back();assert(calls==expected);set_current(hub.data());
    post(int(intent::code));post(int(intent::host));tick(hub.data(),nullptr);assert(calls.back()=="request:lc_code");
    set_current(child.data());post(int(intent::back));tick(child.data(),nullptr);assert(calls.back()=="request:lc_online");
    set_current(hub.data());post(int(intent::other));tick(hub.data(),nullptr);assert(calls.back()=="request:online");
    set_current(hub.data());post(int(intent::back));tick(hub.data(),nullptr);assert(calls.back()=="request:main");
    set_current(main_menu.data());invoke_capture(capture,mgr.data(),3,0x10);request(mgr.data(),nullptr,"online",nullptr);
    assert(calls[calls.size()-9]=="bind:3" && calls.back()=="request:lc_online");
    if(mode=="review-binding") {
        set_current(hub.data());
        texts["service"]="Vorschau";texts["self_state"]="Apercu";texts["browse_hint"]="Hilfe";texts["code_status"]="Entrez huit symboles";
        tick(hub.data(),nullptr);const auto writes=text_writes,hidden_count=hide_writes,enabled_count=enable_writes;
        tick(hub.data(),nullptr);assert(text_writes==writes && hide_writes==hidden_count && enable_writes==enabled_count);
        assert(texts["service"]=="Vorschau" && texts["self_state"]=="Apercu" && texts["browse_hint"]=="Hilfe");
        post(int(intent::code));tick(hub.data(),nullptr);deactivate(hub.data(),nullptr);set_current(child.data());tick(child.data(),nullptr);
        const auto before=text_writes;post(32);tick(child.data(),nullptr);
        assert(text_writes==before+3 && texts["cell0"]=="A" && texts["code_remaining"]=="7");
        assert(texts["code_status"]=="Entrez huit symboles");const auto after=text_writes;tick(child.data(),nullptr);assert(text_writes==after);
        post(int(intent::unavailable));tick(child.data(),nullptr);assert(!visibility["lc_notice_unavailable"] && texts["code_status"]=="Entrez huit symboles");
        // Same pointer after normal deactivation must rebind; an unchanged tick must not.
        deactivate(child.data(),nullptr);set_current(child.data());const auto rebound=text_writes;tick(child.data(),nullptr);assert(text_writes>rebound);
        std::cout<<"review binding PASS (changed fields only, cached visibility/enabling, localized builder text retained, address reuse invalidates)\n";
    }
    if(mode=="review-contracts") {
        set_current(hub.data());tick(hub.data(),nullptr);calls.clear();
        null_argument=true;post(2);tick(hub.data(),nullptr);assert(calls.empty());null_argument=false;
        int_value[0]=0x12345678;post(2);tick(hub.data(),nullptr);assert(calls.empty());int_value[0]=0x6905fc;
        post(1);tick(hub.data(),nullptr);assert(calls.empty());
        put(hub.data(),0,0x69f134);post(2);queue(nullptr,nullptr,"runscript lcOnlineIntent(2)");tick(hub.data(),nullptr);assert(calls==std::vector<std::string>{"queue:runscript lcOnlineIntent(2)"});calls.clear();put(hub.data(),0,0x69e41c);
        const char* destinations[]{"lc_host","lc_code","lc_browse","online","main"};
        for(int value=2;value<=8;++value) {
            set_current(hub.data());const auto n=calls.size();const auto command="runscript lcOnlineIntent("+std::to_string(value)+")";
            queue(nullptr,nullptr,command.c_str());tick(hub.data(),nullptr);
            if(value<=6) assert(calls.size()==n+1 && calls.back()==std::string("request:")+destinations[value-2]);
            else assert(calls.size()==n);
        }
        std::cout<<"review contracts PASS (non-integer/null args, reserved 1, spoof, all seven authored queue constants)\n";
    }
    if(mode=="r3-load" || mode=="r3-load-invalid" || mode=="r3-local-only") {
        set_current(hub.data());tick(hub.data(),nullptr);
        const auto count=calls.size();post(int(intent::rejoin));tick(hub.data(),nullptr);
        if(mode=="r3-load") {
            assert(texts["last_name"]=="Remembered fixture" && texts["last_code"]=="23456789" && texts["last_time"]=="123" && enabled_items["rejoin"]);
            assert(calls.back()=="request:lc_code");close(child.data(),nullptr);set_current(child.data());tick(child.data(),nullptr);assert(texts["code_value"]=="2345-6789");
            deactivate(child.data(),nullptr);set_current(hub.data());tick(hub.data(),nullptr);
            std::ifstream file(root/L"local/XML2 Fix/online/last-game.txt",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});assert(decode_recent(bytes)->time==123);
        } else assert(calls.size()==count && !enabled_items["rejoin"] && texts["last_code"].empty());
    }
    if(mode=="r3") {
        set_current(hub.data());calls.clear();
        result("StaleName",3);const auto old_consumes=consumes;
        post(int(intent::edit_name));tick(hub.data(),nullptr);
        assert(consumes==old_consumes+1 && native_tag==0 && native_max==31);
        assert(std::string_view(native_seed.data())=="100%~$#|;openmenu bad");
        assert(calls==std::vector<std::string>{"request:text_entry"});
        tick(hub.data(),nullptr);assert(native_tag==0 && calls.size()==1); // queued, not yet closed
        deactivate(hub.data(),nullptr);set_current(hub.data());result(std::string(31,'N'));tick(hub.data(),nullptr);
        assert(texts["local_name"]==std::string(31,'N'));
        assert(calls==std::vector<std::string>({"request:text_entry","name","profile","persist"}));
        auto edit=[&]() { calls.clear();post(int(intent::edit_name));tick(hub.data(),nullptr);assert(calls.size()==1);deactivate(hub.data(),nullptr);set_current(hub.data()); };
        edit();result(std::string(32,'x'));tick(hub.data(),nullptr);assert(calls.size()==1 && texts["local_name"]==std::string(31,'N'));
        edit();result(std::string(31,'N'));tick(hub.data(),nullptr);assert(calls.size()==1); // Esc seed
        edit();native_ready=false;tick(hub.data(),nullptr);assert(calls.size()==1); // closure no result
        edit();result("Other",3);const auto count=consumes;tick(hub.data(),nullptr);assert(consumes==count && calls.size()==1); // foreign tag
        edit();result("CancelLate");request(mgr.data(),nullptr,"main",nullptr);tick(hub.data(),nullptr);assert(texts["local_name"]==std::string(31,'N'));
        std::array<unsigned char,256> keys{};online_menus::keyboard(keys.data());
        keys[0x30]=128;online_menus::keyboard(keys.data());assert(keys[0x30]==128); // outside Code: unchanged
        post(int(intent::code));tick(hub.data(),nullptr);set_current(child.data());tick(child.data(),nullptr);
        assert(texts["code_value"]=="____-____" && !enabled_items["done"]);
        // Held B through entry does not leak; fresh first A is accepted.
        keys[0x1e]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="A___-____");
        online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="A___-____");
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x18]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="A___-____" && !visibility["lc_notice_code_invalid"]);
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x1c]=128;online_menus::keyboard(keys.data());assert(keys[0x1c]==0);tick(child.data(),nullptr);assert(texts["code_value"]=="A___-____");
        const auto back=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e440);
        mask=0x200000;back(child.data(),nullptr);tick(child.data(),nullptr);assert(texts["code_value"]=="____-____");
        const auto calls_before=calls.size();back(child.data(),nullptr);tick(child.data(),nullptr);assert(calls.size()==calls_before);
        mask=0;tick(child.data(),nullptr);mask=0x200000;back(child.data(),nullptr);tick(child.data(),nullptr);assert(calls.back()=="request:lc_online");mask=0;
        fake_clipboard_imports();
        clipboard_text(" abcd-efgh\r\n");post(int(intent::paste));tick(child.data(),nullptr);
        assert(texts["code_value"]=="ABCD-EFGH" && clip_closed==1 && clip_locked==1 && clip_unlocked==1);
        // Avoid completing that pending fixture; invalid paste must preserve text.
        post(int(intent::erase));tick(child.data(),nullptr);
        for(auto bad:{"ABCDEFGHjunk","ABCD0FGH","ABCD-EFGH\nextra"}) {
            clipboard_text(bad);post(int(intent::paste));tick(child.data(),nullptr);
            assert(texts["code_value"]=="ABCD-EFG_" && clip_closed==1 && clip_unlocked==1 && !visibility["lc_notice_paste_invalid"]);
        }
        clipboard_text(std::string(257,'A'));post(int(intent::paste));tick(child.data(),nullptr);assert(texts["code_value"]=="ABCD-EFG_" && clip_unlocked==1);
        clipboard_text("ABCDEFGH");clip_data[0]=0xd800;post(int(intent::paste));tick(child.data(),nullptr);assert(texts["code_value"]=="ABCD-EFG_" && clip_unlocked==1);
        clipboard_text("ABCDEFGH");clip_lockable=false;post(int(intent::paste));tick(child.data(),nullptr);assert(clip_closed==1 && !clip_unlocked);
        clipboard_text("ABCDEFGH");clip_available=false;post(int(intent::paste));tick(child.data(),nullptr);assert(!clip_closed && !clip_locked);
        clipboard_text("ABCDEFGH");clip_bytes=3;post(int(intent::paste));tick(child.data(),nullptr);assert(clip_closed==1 && !clip_locked);
        // Ctrl+V and pad Y use the same real production bounded reader.
        clipboard_text("ABCDEFGH");keys.fill(0);online_menus::keyboard(keys.data());keys[0x1d]=keys[0x2f]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="ABCD-EFGH");
        // Pad X clear cancels pending auto completion; Y Paste then Delete tools.
        mask=0x1000000;tick(child.data(),nullptr);mask=0;assert(texts["code_value"]=="____-____");
        clipboard_text("23456789");mask=0x400000;tick(child.data(),nullptr);mask=0;assert(texts["code_value"]=="2345-6789");
        keys.fill(0);online_menus::keyboard(keys.data());keys[0xd3]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="2345-678_");
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x2a]=keys[0xd3]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="____-____");
        // Every symbol is a real authored usecmd; auto response is a later tick.
        tick(child.data(),nullptr);
        for(int i=32;i<40;++i) { const auto command="runscript lcOnlineIntent("+std::to_string(i)+")";queue(nullptr,nullptr,command.c_str());tick(child.data(),nullptr); }
        assert(texts["code_value"]=="ABCD-EFGH" && enabled_items["done"] && texts["cell0"]=="A" && texts["cell7"]=="H");
        post(int(intent::erase));tick(child.data(),nullptr); // invalidate pending completion before it applies
        assert(texts["code_value"]=="ABCD-EFG_");
        assert(!std::filesystem::exists(root/L"local/XML2 Fix/online/last-game.txt"));
        post(39);tick(child.data(),nullptr);tick(child.data(),nullptr);
        assert(!visibility["lc_notice_fixture"]);
        std::ifstream saved(root/L"local/XML2 Fix/online/last-game.txt",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(saved)),{});assert(decode_recent(bytes)->code=="ABCDEFGH");
        saved.close(); // release the Windows reader before the atomic replacement retry
        auto stamp=std::filesystem::last_write_time(root/L"local/XML2 Fix/online/last-game.txt");tick(child.data(),nullptr);tick(child.data(),nullptr);assert(std::filesystem::last_write_time(root/L"local/XML2 Fix/online/last-game.txt")==stamp);
        post(int(intent::done));tick(child.data(),nullptr);tick(child.data(),nullptr);assert(!visibility["lc_notice_fixture"]);
        close(child.data(),nullptr);set_current(hub.data());tick(hub.data(),nullptr);post(int(intent::rejoin));tick(hub.data(),nullptr);assert(calls.back()=="request:lc_code");set_current(child.data());tick(child.data(),nullptr);assert(texts["code_value"]=="ABCD-EFGH");
        mask=0x1000000;tick(child.data(),nullptr);mask=0;assert(texts["code_value"]=="____-____");
        assert(!enabled_items["done"]);
        // Scope and precedence: keyboard X is a code symbol, pad MENU_OTHER clears.
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x2d]=128;online_menus::keyboard(keys.data());mask=0x1000000;tick(child.data(),nullptr);mask=0;assert(texts["cell0"]=="X");
        post(int(intent::clear));tick(child.data(),nullptr);assert(texts["code_value"]=="____-____");
        set_current(hub.data());mask=0x1000000;tick(hub.data(),nullptr);mask=0;assert(calls.back()=="request:text_entry");native_ready=false;deactivate(hub.data(),nullptr);set_current(hub.data());tick(hub.data(),nullptr);
        mask=0x400000;tick(hub.data(),nullptr);mask=0;assert(calls.back()=="request:lc_code");
        std::cout<<"round3 PASS name ownership/drain/31/32/initialized buffers/cancel/foreign tag; direct keys/repeat/tools/held Back; stale fixture/auto resolve/retry/local record/Rejoin\n";
    }
    std::cout<<"online menus "<<mode<<" PASS (guards, hooks, ABI, order, single init, no-controller wait, closed binding, escaping, fallback, reentry)\n";
}

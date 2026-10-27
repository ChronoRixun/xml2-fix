#include "online_menus.hpp"
#include "online_menus_rules.hpp"
#include "online_menus_guards.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "mod_loader.hpp"
#include "script_functions.hpp"
#include "script_function_overflow.hpp"
#include "patch_transaction.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <map>
#include <cstdio>

namespace online_menus {
namespace {
using namespace online_menus_rules;
template<class T> T at(std::uintptr_t p) { return reinterpret_cast<T>(p); }
using getter=void*(__cdecl*)();
using tick_fn=void(__thiscall*)(void*);
using close_fn=std::uint32_t(__thiscall*)(void*);
using request_fn=void(__thiscall*)(void*,const char*,const char*);
using select_fn=void(__thiscall*)(void*,int);
using init_fn=bool(__thiscall*)(void*);
using name_fn=void(__thiscall*)(void*,const char*);
using command_fn=bool(__thiscall*)(void*,const char*);
using set_text_fn=void(__cdecl*)(void*,const char*,const char*,...);
using hide_fn=void(__thiscall*)(void*,const char*,bool);
bool ready=false;
entry_latch entry;
DWORD default_clock() { return GetTickCount(); }
DWORD (*clock_source)()=&default_clock;
void (*request_fault)()=nullptr;
void (*tick_fault)()=nullptr;
DWORD now_ms() { return clock_source(); }
bool hide(void* menu,const char* item,bool hidden);
bool accept_hint(void* menu,bool hidden);
struct binding_cache {
    std::uintptr_t menu=0;
    std::map<std::string,std::string,std::less<>> text;
    std::map<std::string,bool,std::less<>> visibility,enabled;
    void clear() noexcept { menu=0;text.clear();visibility.clear();enabled.clear(); }
    void scope(void* source) {
        const auto id=reinterpret_cast<std::uintptr_t>(source);
        if(menu!=id) { clear();menu=id; }
    }
} bindings;
int accepted=-1;
DWORD accepted_at=0;
mailbox pending;
char local_name[32]{};
bool error_visible=false;
std::string feedback;
code_model code;
name_request name_edit;
DWORD name_requested_at=0;
key_edges keyboard_edges;
bool back_held=false;
std::optional<recent_game> recent;
bool recent_loaded=false;
std::uint64_t fixture_ticket=0;
std::string fixture_code;

struct clipboard_api {
    HANDLE handle=nullptr;
    bool open() { return OpenClipboard(nullptr)!=FALSE; }
    void close() { CloseClipboard(); }
    std::size_t size() { handle=GetClipboardData(CF_UNICODETEXT); return handle ? GlobalSize(handle) : 0; }
    const std::uint16_t* lock() { return static_cast<const std::uint16_t*>(GlobalLock(handle)); }
    void unlock() { GlobalUnlock(handle); }
};
// LocalAppData is deliberately outside game/mod folders and all shared state.
// Reject network drives. No identity, credential or address is written here.
std::filesystem::path recent_path() {
    wchar_t path[32768]{};
    const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
    if(!n || n>=32768) return {};
    if(path[0]==L'\\' && path[1]==L'\\') return {}; // no UNC/device namespace query or I/O
    const std::filesystem::path base(path);
    if(!base.is_absolute() || GetDriveTypeW(base.root_path().c_str())!=DRIVE_FIXED) return {};
    return base/L"XML2 Fix"/L"online"/L"last-game.txt";
}
void load_recent() {
    if(recent_loaded) return;
    recent_loaded=true;
    const auto path=recent_path(); if(path.empty()) return;
    std::ifstream file(path,std::ios::binary); std::array<char,129> data{};
    file.read(data.data(),data.size());
    if(file.gcount()>0 && file.gcount()<=128) recent=decode_recent({data.data(),std::size_t(file.gcount())});
}
void remember_fixture() {
    // This is explicitly an offline fixture record, never successful admission.
    recent=recent_game{code.text,"Offline fixture",std::uint64_t(std::time(nullptr))};
    const auto path=recent_path(); if(path.empty()) return;
    std::error_code ec; std::filesystem::create_directories(path.parent_path(),ec); if(ec) return;
    const auto bytes=encode_recent(*recent);
    std::ofstream file(path,std::ios::binary|std::ios::trunc); file.write(bytes.data(),bytes.size());
}
void message(const char* text) { feedback=text; error_visible=true; }


void* manager() { return at<getter>(0x5d8920)(); }
void* current() {
    __try {
        auto* m=manager();
        return m && *static_cast<std::uint32_t*>(m)==0x6a236c ?
            *reinterpret_cast<void**>(static_cast<char*>(m)+0x86090) : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
screen which(void* menu) {
    __try {
        if(!menu) return screen::none;
        char name[64]{};
        std::memcpy(name,static_cast<char*>(menu)+0xc,63);
        const auto s=identify(name);
        if(custom(s) && *static_cast<std::uint32_t*>(menu)!=0x69e41c) return screen::none;
        return s;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return screen::none; }
}
bool live(void* menu) {
    __try { return menu && current()==menu && (static_cast<unsigned char*>(menu)[4]&1) && custom(which(menu)); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Checked on real entry, after the mod index is ready. A capability marker is
// emitted ONLY by a builder that has selected the matching runtime commands.
bool assets_ready_impl() {
    if(!mod_loader::generation()) return false;
    const auto root=std::filesystem::path(mod_loader::game_root());
    std::ifstream marker(mod_loader::resolve_file((root/L"Data/lc_online_menus.cap").wstring()),std::ios::binary);
    std::array<char,sizeof(capability)> bytes{};
    marker.read(bytes.data(),bytes.size());
    if(marker.gcount()!=sizeof(capability)-1 || std::memcmp(bytes.data(),capability,sizeof(capability)-1)) return false;
    for(const auto* menu:{L"lc_online",L"lc_host",L"lc_code",L"lc_browse"}) {
        for(const auto* ext:{L".XMLB",L".engb",L".IGB"}) {
            const auto path=root/L"UI/menus"/(std::wstring(menu)+ext);
            std::error_code ec;
            if(!std::filesystem::is_regular_file(mod_loader::resolve_file(path.wstring()),ec)) return false;
        }
        const auto path=root/L"Packages/generated/maps/package/menus"/(std::wstring(menu)+L".PKGB");
        std::error_code ec;
        if(!std::filesystem::is_regular_file(mod_loader::resolve_file(path.wstring()),ec)) return false;
    }
    return true;
}

unsigned cached_asset_generation=0;
bool assets_ready() noexcept {
    try {
        const auto generation=mod_loader::generation();
        if(!generation) { cached_asset_generation=0;return false; }
        if(cached_asset_generation==generation) return true;
        if(!assets_ready_impl()) return false;
        cached_asset_generation=generation;return true;
    } catch(...) {
        cached_asset_generation=0;
        logger::write("online menus: asset check threw; native online entry retained");
        return false;
    }
}

struct native_engine {
    void *seats=nullptr,*network_object=nullptr,*session_object=nullptr,*campaign_object=nullptr,*profile=nullptr,*console=nullptr;
    bool preflight(int p) {
        __try {
            if(!controller_valid(p)) return false;
            auto* m=manager();
            if(!m || *static_cast<std::uint32_t*>(m)!=0x6a236c ||
                at<int(__thiscall*)(void*)>(0x5d8570)(m)!=p) return false;
            seats=at<getter>(0x48de40)();
            network_object=at<getter>(0x60b210)();
            session_object=at<getter>(0x612be0)();
            campaign_object=at<getter>(0x6097b0)();
            profile=at<getter>(0x48fed0)();
            console=at<getter>(0x55c890)();
            auto* input=at<getter>(0x551ed0)();
            if(!seats || *static_cast<std::uint32_t*>(seats)!=0x6898c4 ||
                network_object!=at<void*>(0xa3df68) || session_object!=at<void*>(0xa53058) ||
                *static_cast<std::uint32_t*>(session_object)!=0x6a481c ||
                campaign_object!=at<void*>(0xa3bc40) || !profile ||
                *static_cast<std::uint32_t*>(profile)!=0x689994 || !console ||
                *static_cast<std::uint32_t*>(console)!=0x69a81c || !input ||
                *static_cast<std::uint32_t*>(input)!=0x69a134) return false;
            const char* name=at<const char*(__thiscall*)(void*)>(0x48f490)(profile);
            std::size_t n=0;
            for(;n<32 && name[n];++n) local_name[n]=name[n];
            if(n>=32) return false;
            local_name[n]=0;
            if(!n) std::memcpy(local_name,"Player",7);
            return valid_name(local_name);
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool bind(int p) {
        __try { at<select_fn>(0x48d5e0)(seats,p); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool network() {
        __try { return at<init_fn>(0x615dc0)(network_object); }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool name() {
        if(!valid_name(local_name)) return false;
        __try { at<name_fn>(0x610d80)(session_object,local_name); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool session() {
        __try { return at<init_fn>(0x613ba0)(session_object); }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool campaign() {
        __try { return at<init_fn>(0x60a4f0)(campaign_object); }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool mode() {
        __try { at<select_fn>(0x60a770)(network_object,1); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool persist() {
        __try {
            at<name_fn>(0x48f3f0)(profile,local_name);
            // Stock Ready uses run-now (+18), then queues the next screen.
            return at<command_fn>(0x55beb0)(console,"runscript saveloadProcess(2)");
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
};

void abandon_name() {
    if(!name_edit.owned) return;
    auto* request=at<getter>(0x60c7e0)();
    if(at<int(__thiscall*)(void*)>(0x60b9a0)(request)==0) {
        if(at<bool(__thiscall*)(void*)>(0x60b960)(request))
            at<const char*(__thiscall*)(void*)>(0x60c780)(request);
        else at<select_fn>(0x60b990)(request,4);
    }
    name_edit.owned=false;
}

// EBX is the native MAIN_MENU input mask at the guarded call site; p is the
// controller returned with that input. Forward the native setter in all cases.
void __cdecl capture(void* m,int p,unsigned mask) {
    at<select_fn>(0x5d8560)(m,p);
    accepted=controller_valid(p) && (mask&0x10) && which(current())==screen::main ? p : -1;
    accepted_at=now_ms();
}
__declspec(naked) void capture_stub() {
    __asm {
        mov eax,[esp+4]
        push ebx
        push eax
        push ecx
        call capture
        add esp,12
        ret 4
    }
}

void request_impl(void* m,const char* name,const char* args) {
    pending.clear();
    if(name_edit.owned && name && std::strcmp(name,"text_entry") && std::strcmp(name,"lc_online")) abandon_name();
    auto* source=current();
    const auto source_id=reinterpret_cast<std::uintptr_t>(source);
    if(ready && name && std::strcmp(name,"online")==0 && which(source)==screen::main) {
        // Availability precedes the capture gate: missing/wrong assets always
        // retain native online, including console requests and delayed queues.
        if(!assets_ready()) {
            entry.clear();accept_hint(source,true);
            at<request_fn>(0x5d5db0)(m,name,args);return;
        }
        if(entry.waiting(source_id,now_ms())) return;
        if(!accept_fresh(accepted,accepted_at,now_ms())) {
            accepted=-1;
            // Builder owns this localized text. Without a usable prompt item,
            // fall back natively rather than create a silent main-menu action.
            if(accept_hint(source,false)) {
                logger::write("online menus: waiting for real Accept; localized continue prompt shown");return;
            }
            at<request_fn>(0x5d5db0)(m,name,args);return;
        }
        accept_hint(source,true);
        const int p=accepted; accepted=-1;
        native_engine engine;
        if(initialize(engine,p)) {
            entry.start(source_id,now_ms());error_visible=false;
            logger::write("online menus: entry initialized once, controller=%d; Hub requested; no broker operation",p);
            at<request_fn>(0x5d5db0)(m,"lc_online",nullptr);return;
        }
        logger::write("online menus: native initialization refused; keeping native online entry");
        entry.clear();
    } else if(name && entry.source==source_id && which(source)==screen::main && std::strcmp(name,"lc_online")) {
        entry.clear();accept_hint(source,true);
    }
    at<request_fn>(0x5d5db0)(m,name,args);
}
void __fastcall request(void* m,void*,const char* name,const char* args) {
    try {
        if(request_fault) request_fault();
        request_impl(m,name,args);
    } catch(...) {
        pending.clear();entry.clear();accepted=-1;
        logger::write("online menus: request threw; native request retained");
        try { at<request_fn>(0x5d5db0)(m,name,args); }
        catch(...) { logger::write("online menus: native request threw; frame contained"); }
    }
}

int argument(void* args) {
    __try {
        auto* value=at<void*(__thiscall*)(void*,int)>(0x4d5830)(args,0);
        // n(i) accepts an integer Value only. Do not dispatch arbitrary vtables.
        if(!value || *static_cast<std::uint32_t*>(value)!=0x6905fc) return 0;
        return at<int(__thiscall*)(void*)>(0x55d7e0)(value);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* __cdecl handler(void* args) {
    if(ready) {
        auto* menu=current();
        if(live(menu)) pending.post(argument(args),reinterpret_cast<std::uintptr_t>(menu),which(menu));
    }
    return nullptr;
}
const script_functions_rules::func_entry registration{reinterpret_cast<const void*>(&handler),function_name,"n","i"};

// Native usecmd queues its string for a later frame. Capture our exact authored
// constants at that boundary, while the accepting source menu still exists;
// otherwise Back/closure before the script executes could retarget an intent
// to a different menu. No display/model text is accepted as command source.
bool __fastcall queue(void* console,void*,const char* line) {
    // Only exact canonical integer commands are accepted at this boundary.
    // Model/name/clipboard text can never become a script or console command.
    if(ready && line) for(int value=2;value<=62;++value) {
        if(value>int(intent::invalid_key) && value<32) continue;
        char command[48]{};
        std::snprintf(command,sizeof(command),"runscript lcOnlineIntent(%d)",value);
        if(std::strcmp(command,line)) continue;
        auto* menu=current();
        if(live(menu)) pending.post(value,reinterpret_cast<std::uintptr_t>(menu),which(menu));
        return true;
    }
    return at<command_fn>(0x55c410)(console,line);
}
// Replaces the nonempty usecmd's call [edx+1c] / pop ebx / pop edi.
// Keep the shared console vtable retail for postgame/test-input guards.
__declspec(naked) void queue_stub() {
    __asm {
        push [esp+4]
        call queue
        pop eax
        add esp,4
        pop ebx
        pop edi
        jmp eax
    }
}

bool bind_text_native(void* menu,const char* item,const char* text) {
    __try {
        auto* found=at<void*(__thiscall*)(void*,const char*)>(0x5adc10)(menu,item);
        if(!found) return false;
        const auto* vt=*static_cast<const std::uint32_t**>(found);
        if(vt[0x78/4]!=0x5c6850 || *reinterpret_cast<const std::uint32_t*>(static_cast<char*>(found)+0xa0)) return false;
        at<set_text_fn>(0x5adcf0)(menu,item,"%s",text);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void bind_text(void* menu,const char* item,const char* text) {
    bindings.scope(menu);
    const auto old=bindings.text.find(item);
    if(old!=bindings.text.end() && old->second==text) return;
    if(bind_text_native(menu,item,text)) bindings.text.insert_or_assign(item,text);
}
bool hide_native(void* menu,const char* item,bool hidden) {
    __try {
        auto* found=at<void*(__thiscall*)(void*,const char*)>(0x5adc10)(menu,item);
        if(found && (*static_cast<const std::uint32_t**>(found))[8/4]==0x5bcdb0) {
            at<hide_fn>(0x5ade10)(menu,item,hidden);return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    return false;
}
bool hide(void* menu,const char* item,bool hidden) {
    bindings.scope(menu);
    const auto old=bindings.visibility.find(item);
    if(old!=bindings.visibility.end() && old->second==hidden) return true;
    if(!hide_native(menu,item,hidden)) return false;
    bindings.visibility.insert_or_assign(item,hidden);return true;
}
bool accept_hint(void* menu,bool hidden) {
    // Main has its own vtable/lifetime. Reacquire this prerequisite on each
    // request; a stale visibility cache must never certify a missing prompt.
    bindings.scope(menu);
    if(!hide_native(menu,"lc_online_continue",hidden)) {
        bindings.visibility.erase("lc_online_continue");return false;
    }
    bindings.visibility.insert_or_assign("lc_online_continue",hidden);return true;
}
bool enable_native(void* menu,const char* item,bool enabled) {
    __try {
        if(!at<void*(__thiscall*)(void*,const char*)>(0x5adc10)(menu,item)) return false;
        at<hide_fn>(0x5ade70)(menu,item,enabled);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void enable(void* menu,const char* item,bool enabled) {
    bindings.scope(menu);
    const auto old=bindings.enabled.find(item);
    if(old!=bindings.enabled.end() && old->second==enabled) return;
    if(enable_native(menu,item,enabled)) bindings.enabled.insert_or_assign(item,enabled);
}
void show_notice(void* menu) {
    // Builder-owned localized text. Only visibility, never English replacement.
    constexpr const char* notices[]{"lc_notice_unavailable","lc_notice_resolving","lc_notice_fixture",
        "lc_notice_paste_invalid","lc_notice_code_invalid","lc_notice_name_invalid",
        "lc_notice_profile_save","lc_notice_name_open"};
    bool found=false;
    for(const auto* item:notices) {
        const bool selected=error_visible && feedback==item;
        const bool usable=hide(menu,item,!selected);
        if(selected && usable) found=true;
    }
    hide(menu,"lc_inline_error",!error_visible || found);
}
bool begin_name() {
    if(!name_edit.begin(local_name)) return false;
    error_visible=false; feedback.clear(); name_requested_at=now_ms();
    auto* request=at<getter>(0x60c7e0)();
    if(at<bool(__thiscall*)(void*)>(0x60b960)(request))
        at<const char*(__thiscall*)(void*)>(0x60c780)(request); // stale, any tag
    at<select_fn>(0x60b990)(request,0);
    at<select_fn>(0x60b970)(request,31);
    at<name_fn>(0x60c6e0)(request,name_edit.seed.data());
    at<name_fn>(0x60c710)(request,name_edit.title.data());
    at<request_fn>(0x5d5db0)(manager(),"text_entry",nullptr);
    return true;
}
void poll_name() {
    if(!name_edit.owned) return;
    // Request is queued: an unchanged Hub before its close is NOT a cancelled
    // keyboard. The close hook records departure, even across address reuse.
    if(!name_edit.departed) {
        if(now_ms()-name_requested_at>5000) { abandon_name(); message("lc_notice_name_open"); }
        return;
    }
    // Hub return is the ownership boundary. Never steal another menu's result.
    auto* request=at<getter>(0x60c7e0)();
    const bool result=at<bool(__thiscall*)(void*)>(0x60b960)(request);
    const int tag=at<int(__thiscall*)(void*)>(0x60b9a0)(request);
    std::array<char,256> copy{};
    if(result && tag==0) {
        const auto* text=at<const char*(__thiscall*)(void*)>(0x60c780)(request);
        std::memcpy(copy.data(),text,copy.size());
    }
    if(result && tag==0) {
        const auto end=std::find(copy.begin(),copy.end(),char(0));
        if(end==copy.end() || !valid_name({copy.data(),std::size_t(end-copy.begin())}))
            message("lc_notice_name_invalid");
    }
    if(!result && tag==0) at<select_fn>(0x60b990)(request,4);
    const auto value=name_edit.finish(result ? tag : 4,copy.data(),copy.size());
    if(!value) return;
    // Same native side effects as ONLINE_MENU Ready. No 32-byte truncation.
    at<name_fn>(0x610d80)(at<getter>(0x612be0)(),value->c_str());
    at<name_fn>(0x48f3f0)(at<getter>(0x48fed0)(),value->c_str());
    const bool saved=at<command_fn>(0x55beb0)(at<getter>(0x55c890)(),"runscript saveloadProcess(2)");
    std::memcpy(local_name,value->c_str(),value->size()+1);
    if(!saved) message("lc_notice_profile_save");
}
// Base Back slot is scoped; native menus retain their original handler.
// All paths (pad, keyboard, clickable MENU_BACK) enter the same single mailbox.
void __fastcall on_back(void* menu,void*) {
    if(ready && live(menu)) {
        if(!back_held) {
            back_held=true;
            pending.post(int(intent::back),reinterpret_cast<std::uintptr_t>(menu),which(menu));
        }
        return;
    }
    at<tick_fn>(0x5ad7c0)(menu);
}
unsigned input_mask() {
    auto* m=manager();
    return at<unsigned(__thiscall*)(void*)>(0x5d4b00)(m);
}
void schedule_fixture(bool retry=false) {
    const auto ticket=code.resolve(retry);
    if(ticket) { fixture_ticket=ticket; fixture_code=code.text; message("lc_notice_resolving"); }
}
void tick_impl(void* menu,bool& native_called) {
    if(ready && live(menu)) {
        // The native mask is rising-edge input (controller +2f8 XOR +374).
        // Keyboard text already posted by DirectInput wins over X/Y bindings.
        const auto mask=input_mask();
        if(!(mask&0x200000)) back_held=false;
        const auto s=which(menu);
        if(!pending.action) {
            int shortcut=0;
            if(s==screen::code) {
                if(mask&0x400000) shortcut=int(intent::paste); // MENU_NEXT (Y)
                else if(mask&0x1000000) shortcut=int(intent::clear); // MENU_OTHER (X)
                else if(mask&0x100000) shortcut=int(intent::done); // MENU_OK (Start)
            } else if(s==screen::hub) {
                if(mask&0x1000000) shortcut=int(intent::edit_name);
                else if(mask&0x400000) shortcut=int(intent::rejoin);
            }
            if(shortcut) pending.post(shortcut,reinterpret_cast<std::uintptr_t>(menu),s);
        }
    }
    native_called=true;
    at<tick_fn>(0x5aaeb0)(menu);
    if(tick_fault) tick_fault();
    if(!ready || !live(menu)) { pending.clear(); return; }
    const auto s=which(menu);
    if(s==screen::hub) entry.clear();
    load_recent();
    if(s==screen::hub) poll_name();
    else abandon_name();
    if(s==screen::code && !code.active) code.open();
    if(s!=screen::code && code.active) { code.close(); fixture_ticket=0; }
    const auto completed_ticket=fixture_ticket;
    const auto completed_code=fixture_code;
    fixture_ticket=0;
    const auto action=static_cast<intent>(pending.take(reinterpret_cast<std::uintptr_t>(menu),s));
    const char* destination=nullptr;
    switch(action) {
    case intent::host: if(s==screen::hub) destination="lc_host"; break;
    case intent::code: if(s==screen::hub) destination="lc_code"; break;
    case intent::browse: if(s==screen::hub) destination="lc_browse"; break;
    case intent::other: if(s==screen::hub) destination="online"; break;
    case intent::back:
        if(s==screen::code && !code.text.empty()) { code.erase(); error_visible=false; }
        else destination=s==screen::hub ? "main" : "lc_online";
        break;
    case intent::edit_name: if(s==screen::hub && begin_name()) return; break;
    case intent::paste: if(s==screen::code) {
        clipboard_api api; const auto value=read_clipboard(api);
        if(value) { code.replace(*value); error_visible=false; }
        else message("lc_notice_paste_invalid");
    } break;
    case intent::erase: if(s==screen::code) { code.erase(); error_visible=false; } break;
    case intent::clear: if(s==screen::code) { code.replace(""); error_visible=false; } break;
    case intent::done: if(s==screen::code) schedule_fixture(true); break;
    case intent::rejoin: if(s==screen::hub && recent) {
        code.open(); code.replace(recent->code); destination="lc_code";
    } break;
    case intent::invalid_key: if(s==screen::code) message("lc_notice_code_invalid"); break;
    case intent::unavailable: message("lc_notice_unavailable"); break;
    case intent::dismiss: error_visible=false; break;
    default:
        if(s==screen::code && int(action)>=32 && int(action)<=62) {
            code.append(alphabet[int(action)-32]); error_visible=false;
        }
        break;
    }
    // Apply this tick's edit/cancel BEFORE accepting a prior completion.
    if(completed_ticket && code.complete(completed_ticket) && completed_code==code.text && s==screen::code && !destination) {
        remember_fixture(); message("lc_notice_fixture");
    }
    if(s==screen::code) schedule_fixture();
    if(destination) {
        if(s==screen::code) { code.close(); fixture_ticket=0; }
        name_edit.owned=false;
        pending.clear(); error_visible=false; accepted=-1;
        // Original manager request deliberately bypasses our Online interception:
        // Other ways opens the full native name/profile/Host/Join chain.
        at<request_fn>(0x5d5db0)(manager(),destination,nullptr);
        return; // never bind after requesting a transition
    }
    const auto name=display(local_name,31);
    bind_text(menu,s==screen::hub ? "local_name" : "self_name",name.c_str());
    if(recent) {
        bind_text(menu,"last_name",display(recent->name,31).c_str());
        bind_text(menu,"last_code",recent->code.c_str());
    }
    for(const auto* item:{"last_name","last_code","last_time"}) hide(menu,item,!recent);
    hide(menu,"lc_recent_empty",bool(recent));
    if(s==screen::hub) enable(menu,"rejoin",bool(recent));
    if(recent) bind_text(menu,"last_time",std::to_string(recent->time).c_str());
    if(s==screen::code) {
        bind_text(menu,"code_value",code.shown().c_str());
        for(unsigned i=0;i<8;++i) {
            const auto item="cell"+std::to_string(i);
            const char value[]{i<code.text.size() ? code.text[i] : char(0),0};
            bind_text(menu,item.c_str(),value);
        }
        bind_text(menu,"code_remaining",std::to_string(8-code.text.size()).c_str());
        hide(menu,"code_status",error_visible || code.text.size()==8);
        hide(menu,"lc_code_complete",error_visible || code.text.size()!=8);
        enable(menu,"done",code.text.size()==8);

    }
    show_notice(menu);
}
void __fastcall tick(void* menu,void*) {
    bool native_called=false;
    try { tick_impl(menu,native_called); }
    catch(...) {
        pending.clear();++code.generation;code.submitted=code.generation;fixture_ticket=0;bindings.clear();
        feedback.clear();error_visible=true;
        logger::write("online menus: UI update threw; frame contained");
        if(!native_called) {
            try { at<tick_fn>(0x5aaeb0)(menu); }
            catch(...) { logger::write("online menus: native tick threw; frame contained"); }
        }
    }
}
void deactivate_state(void* menu,bool departure) {
    if(bindings.menu==reinterpret_cast<std::uintptr_t>(menu)) bindings.clear();
    pending.clear();
    const auto s=which(menu);
    // +48 is resource cleanup and may run while constructing a noncurrent
    // cached menu. Only real +1c deactivation owns a requested Hub departure.
    if(s==screen::code && (departure || current()==menu)) { code.close(); fixture_ticket=0; }
    if(departure && s==screen::hub && name_edit.owned) name_edit.departed=true;
}
void __fastcall deactivate(void* menu,void*) {
    deactivate_state(menu,true);
    at<tick_fn>(0x5ace30)(menu); // preserve native commands/input restoration
}
std::uint32_t __fastcall close(void* menu,void*) {
    deactivate_state(menu,false);
    return at<close_fn>(0x5af5a0)(menu);
}
}

void keyboard(unsigned char* state) {
    if(!ready) return;
    if(!state) { keyboard_edges.reset(); return; }
    // Sample outside Code too: keys held through entry are ignored, while a
    // fresh first key after entry is not lost to a new-menu priming frame.
    const auto edges=keyboard_edges.sample(state);
    auto* menu=current();
    if(!live(menu) || which(menu)!=screen::code) return;
    const auto address=reinterpret_cast<std::uintptr_t>(menu);
    const bool control=(state[0x1d]|state[0x9d])&0x80;
    const bool shift=(state[0x2a]|state[0x36])&0x80;
    int action=0;
    if(control && edges[0x2f]) action=int(intent::paste); // Ctrl+V
    else if(edges[0xd3]) action=int(shift ? intent::clear : intent::erase);
    else if(edges[0x0e]) action=int(intent::erase); // Backspace
    else if(edges[0x1c] || edges[0x9c]) action=int(intent::done);
    else if(!control) {
        constexpr unsigned char scans[]{0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x24,0x25,0x32,0x31,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a};
        for(unsigned i=0;i<31;++i) if(edges[scans[i]]) { action=32+i; break; }
        for(unsigned i:{0x0bu,0x18u,0x02u,0x17u,0x26u}) if(edges[i]) action=int(intent::invalid_key);
    }
    if(action) pending.post(action,address,screen::code);
    // Enter is Done, never also accepts the focused grid cell. These keys
    // are reserved for Code tools only while the Code menu owns keyboard input.
    state[0x1c]=state[0x9c]=state[0x0e]=state[0xd3]=0;
}

bool requested() { return enabled(ini::text(L"Online",L"Menus")); }
bool installed() { return ready; }
patch_transaction::contribution contribution() {
    patch_transaction::contribution c;
    c.owner="online menus";
    for(const auto& g:online_menus_guards::guards)
        c.guards.push_back({g.va,patch_transaction::bytes_of(g.hex),g.what});
    c.writes.push_back(patch_transaction::relocation(0x5c971b,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&capture_stub)),1,"accept controller"));
    c.writes.push_back(patch_transaction::relocation(0x6a23d8,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&request)),0,"request online"));
    c.writes.push_back(patch_transaction::relocation(0x69e454,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tick)),0,"base menu UI tick"));
    c.writes.push_back(patch_transaction::relocation(0x69e464,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&close)),0,"base menu close"));
    c.writes.push_back(patch_transaction::relocation(0x69e438,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&deactivate)),0,"base menu deactivation"));
    c.writes.push_back(patch_transaction::relocation(0x69e440,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&on_back)),0,"scoped base Back"));
    c.writes.push_back(patch_transaction::relocation(0x5bc3d0,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&queue_stub)),0,"authored usecmd intent boundary"));
    return c;
}
void install(HMODULE game,const install_hooks* hooks) {
    if(ready || !requested()) return;
    if(reinterpret_cast<std::uintptr_t>(game)!=0x400000 ||
        !registration_fits(script_function_overflow::table(),script_functions::capacity())) {
        logger::write("online menus: registration/capacity refused; native online entry retained"); return;
    }
    if(hooks) {
        if(hooks->clock) clock_source=hooks->clock;
        request_fault=hooks->before_request;tick_fault=hooks->before_tick;
    }
    patch_transaction::transaction tx;
    tx.add(contribution());
    const auto result=tx.commit(hooks ? hooks->transaction : nullptr);
    if(result==patch_transaction::outcome::rollback_failed) game_version::refuse_startup();
    if(result!=patch_transaction::outcome::committed) {
        logger::write("online menus: %s; native online entry retained",tx.report().c_str()); return;
    }
    // Installation runs before game registration on a single thread; capacity
    // was reserved above. Publish the function only after every hook readback.
    if(hooks && hooks->before_registration) hooks->before_registration();
    const auto added=script_function_overflow::table().add(&registration);
    if(added!=script_functions_rules::added::added) {
        game_version::refuse_startup(); return;
    }
    ready=true;
    logger::write("online menus: dispatcher lcOnlineIntent n(i) registered; 7 hooks committed atomically; effective script capacity=%d; requires lc-online-menus/1 assets; input API=%s",script_functions::capacity(),input_capability);
}
}

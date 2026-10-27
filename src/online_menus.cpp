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
#include <intrin.h>
#include <atomic>
#include <vector>

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
input_inbox keyboard_input;
std::optional<std::string> rejoin_seed;
std::atomic<DWORD> game_thread{0},keyboard_thread{0};
bool back_held=false;
std::optional<recent_game> recent;
bool recent_loaded=false;
std::uint64_t fixture_ticket=0;
std::string fixture_code;
bool (*lobby_fields)(lobby_fixture&)=nullptr;
bool lobby_live(void* menu);

struct clipboard_api {
    HANDLE handle=nullptr;
    bool open() { return OpenClipboard(nullptr)!=FALSE; }
    void close() { CloseClipboard(); }
    std::size_t size() { handle=GetClipboardData(CF_UNICODETEXT); return handle ? GlobalSize(handle) : 0; }
    const std::uint16_t* lock() { return static_cast<const std::uint16_t*>(GlobalLock(handle)); }
    void unlock() { GlobalUnlock(handle); }
};
BOOL CALLBACK clipboard_owner_window(HWND window,LPARAM out) {
    DWORD process=0;GetWindowThreadProcessId(window,&process);
    char name[64]{};
    if(process==GetCurrentProcessId() && GetClassNameA(window,name,sizeof(name))>0 &&
        std::strcmp(name,"igWin32WindowClass")==0) {
        *reinterpret_cast<HWND*>(out)=window;return FALSE;
    }
    return TRUE;
}
HWND clipboard_owner() {
    HWND owner=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),clipboard_owner_window,reinterpret_cast<LPARAM>(&owner));
    return owner; // read only; no activation/focus or foreign-process window
}
// LocalAppData is deliberately outside game/mod folders and all shared state.
// Reject network drives. No identity, credential or address is written here.
std::filesystem::path recent_path() {
    std::vector<wchar_t> path(32768);
    const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",path.data(),DWORD(path.size()));
    if(!n || n>=32768) return {};
    if(path[0]==L'\\' && path[1]==L'\\') return {}; // no UNC/device namespace query or I/O
    const std::filesystem::path base(path.data());
    if(!base.is_absolute() || GetDriveTypeW(base.root_path().c_str())!=DRIVE_FIXED) return {};
    return base/L"XML2 Fix"/L"online"/L"last-game.txt";
}
void load_recent() {
    if(recent_loaded) return;
    recent_loaded=true;
    const auto path=recent_path(); if(path.empty()) return;
    std::ifstream file(path,std::ios::binary); std::array<char,129> data{};
    file.read(data.data(),data.size());
    if(!file.bad() && file.gcount()>0 && file.gcount()<=128) recent=decode_recent({data.data(),std::size_t(file.gcount())});
}
bool remember_fixture() {
    // This is explicitly an offline fixture record, never successful admission.
    recent_game next{code.text,"Offline fixture",std::uint64_t(std::time(nullptr))};
    const auto path=recent_path(); if(path.empty()) return false;
    std::error_code ec; std::filesystem::create_directories(path.parent_path(),ec); if(ec) return false;
    const auto bytes=encode_recent(next);if(bytes.empty()) return false;
    static unsigned serial=0;
    const auto temporary=path.wstring()+L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++serial);
    const auto file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0;
    bool saved=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size();
    if(!CloseHandle(file)) saved=false;
    if(saved) saved=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE;
    if(!saved) { DeleteFileW(temporary.c_str());return false; } // only our CREATE_NEW file
    recent=std::move(next);return true;
}
void message(const char* text) { feedback=text; error_visible=true; }


void* manager() { return at<getter>(0x5d8920)(); }
void* current_peek() {
    __try {
        auto* m=*reinterpret_cast<void**>(0x8aff18);
        if(!m || *static_cast<unsigned*>(m)!=0x6a236c) return nullptr;
        auto* menu=*reinterpret_cast<void**>(static_cast<char*>(m)+0x86090);
        return menu && (static_cast<unsigned char*>(menu)[4]&1) ? menu : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
bool input_thread(const char* where,bool keyboard_call) {
    const auto id=GetCurrentThreadId();
    if(keyboard_call) {
        DWORD empty=0;keyboard_thread.compare_exchange_strong(empty,id);
        logger::write_once("online:keyboard-thread","online menus: keyboard thread=%lu",id);
    } else {
        DWORD empty=0;game_thread.compare_exchange_strong(empty,id);
        logger::write_once("online:tick-thread","online menus: game tick thread=%lu",game_thread.load());
    }
    const auto owner=game_thread.load(),keys=keyboard_thread.load();
    if(owner && keys) logger::write_once("online:thread-agreement",
        "online menus: keyboard/game-thread assertion %s (keyboard=%lu game=%lu)",owner==keys ? "matched" : "FAILED",keys,owner);
    if(owner && owner!=id) {
        logger::write_once("online:thread-refused","online menus: %s refused outside game thread=%lu (caller=%lu)",where,owner,id);
        return false;
    }
    return true;
}
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
        if(s==screen::lobby_host && *static_cast<std::uint32_t*>(menu)!=0x69f274) return screen::none;
        if(s==screen::lobby_join && *static_cast<std::uint32_t*>(menu)!=0x69f33c) return screen::none;
        return s;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return screen::none; }
}
bool live(void* menu) {
    __try { return menu && current()==menu && (static_cast<unsigned char*>(menu)[4]&1) &&
        (custom(which(menu)) || lobby_live(menu)); }
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

enum class name_operation { abandon, begin, poll, apply };
struct name_io {
    int tag=4;
    bool result=false,saved=false;
    char copy[256]{};
    const char* accepted=nullptr;
};
// /EHsc does not catch access violations. Keep ALL native name-service calls
// and result copying in this POD-only SEH boundary, including save side effects.
bool name_native(name_operation operation,name_io& io) {
    __try {
        auto* request=at<getter>(0x60c7e0)();
        if(request!=at<void*>(0xa3e168) || *static_cast<unsigned*>(request)!=0x6a4730) return false;
        if(operation==name_operation::abandon) {
            if(at<int(__thiscall*)(void*)>(0x60b9a0)(request)==0) {
                if(at<bool(__thiscall*)(void*)>(0x60b960)(request))
                    at<const char*(__thiscall*)(void*)>(0x60c780)(request);
                else at<select_fn>(0x60b990)(request,4);
            }
        } else if(operation==name_operation::begin) {
            if(at<bool(__thiscall*)(void*)>(0x60b960)(request))
                at<const char*(__thiscall*)(void*)>(0x60c780)(request);
            at<select_fn>(0x60b990)(request,0);
            at<select_fn>(0x60b970)(request,31);
            at<name_fn>(0x60c6e0)(request,name_edit.seed.data());
            at<name_fn>(0x60c710)(request,name_edit.title.data());
            auto* m=*reinterpret_cast<void**>(0x8aff18);
            if(!m || *static_cast<unsigned*>(m)!=0x6a236c) return false;
            at<request_fn>(0x5d5db0)(m,"text_entry",nullptr);
        } else if(operation==name_operation::poll) {
            io.result=at<bool(__thiscall*)(void*)>(0x60b960)(request);
            io.tag=at<int(__thiscall*)(void*)>(0x60b9a0)(request);
            if(io.result && io.tag==0) {
                const auto* text=at<const char*(__thiscall*)(void*)>(0x60c780)(request);
                std::memcpy(io.copy,text,sizeof(io.copy));
            }
            if(!io.result && io.tag==0) at<select_fn>(0x60b990)(request,4);
        } else {
            auto* session=at<getter>(0x612be0)();
            auto* profile=at<getter>(0x48fed0)();
            auto* console=at<getter>(0x55c890)();
            if(session!=at<void*>(0xa53058) || *static_cast<unsigned*>(session)!=0x6a481c ||
                !profile || *static_cast<unsigned*>(profile)!=0x689994 ||
                !console || *static_cast<unsigned*>(console)!=0x69a81c) return false;
            at<name_fn>(0x610d80)(session,io.accepted);
            at<name_fn>(0x48f3f0)(profile,io.accepted);
            io.saved=at<command_fn>(0x55beb0)(console,"runscript saveloadProcess(2)");
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void name_fault() {
    name_edit.owned=false;name_edit.departed=false;++name_edit.generation;
    name_io ignored;name_native(name_operation::abandon,ignored); // one bounded best-effort drain
    message("lc_notice_name_open");
}
bool abandon_name() {
    if(!name_edit.owned) return true;
    name_edit.owned=false;name_edit.departed=false;
    name_io ignored;
    if(!name_native(name_operation::abandon,ignored)) { name_fault();return false; }
    return true;
}

// EBX is the native MAIN_MENU input mask at the guarded call site; p is the
// controller returned with that input. Forward the native setter in all cases.
void __cdecl capture(void* m,int p,unsigned mask) {
    at<select_fn>(0x5d8560)(m,p);
    accepted=controller_valid(p) && (mask&0x10) && which(current())==screen::main ? p : -1;
    if(which(current())==screen::main && (mask&0x10)) accept_hint(current(),true);
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
    pending.clear();keyboard_input.clear();
    if(name_edit.owned && name && std::strcmp(name,"text_entry") && std::strcmp(name,"lc_online") && !abandon_name()) return;
    auto* source=current();
    const auto source_id=reinterpret_cast<std::uintptr_t>(source);
    if(name && std::strcmp(name,"lc_code")) rejoin_seed.reset();
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
    } else if(name && which(source)==screen::main && std::strcmp(name,"lc_online")) {
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
        const auto value=argument(args);
        if(value!=int(intent::invalid_key) && live(menu)) pending.post(value,reinterpret_cast<std::uintptr_t>(menu),which(menu));
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
        if(value==int(intent::invalid_key)) continue; // internal keyboard feedback, never a published command
        if(value>int(intent::confirm_leave) && value<32) continue;
        char command[48]{};
        std::snprintf(command,sizeof(command),"runscript lcOnlineIntent(%d)",value);
        if(std::strcmp(command,line)) continue;
        auto* menu=current();
        if(live(menu)) {
            if(pending.action || pending.post(value,reinterpret_cast<std::uintptr_t>(menu),which(menu))) return true;
        }
        break; // no supported live source: preserve native queue result/behavior
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
// Native lobbies retain their complete controller/vtable and campaign callback.
// No additional pooled rows, participant writes, admission or direct Start leaf.
struct lobby_view {
    unsigned count=0,ready_count=0,local=0;
    bool local_known=false;
    int selected=-1;
    unsigned ids[4]{};
    bool ready[4]{};
    bool host[4]{};
    char names[4][32]{};
};
struct lobby_ui {
    void* owner=nullptr;
    std::uint64_t generation=0;
    bool confirm=false,error=false,copied=false,copy_held=false,role_held=false,focus_confirm=false,leave_authorized=false,copy_keyboard=false,copy_block=false;
    bool target_error=false,picker_safe=false;
    bool start_dimmed=false,start_was_enabled=false;
    void reset(void* menu=nullptr) { ++generation;owner=menu;confirm=error=copied=copy_held=role_held=focus_confirm=leave_authorized=copy_keyboard=copy_block=target_error=picker_safe=start_dimmed=start_was_enabled=false; }
} lobby_state;
thread_local bool lobby_ticking=false,base_ui_dispatch=false,native_mouse=false,back_consumed=false;
thread_local unsigned refreshing_names=0;
thread_local char native_display_name[32]{};
const char* __fastcall participant_display_name(void* participant,void*) {
    auto* raw=reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(participant)+0x40);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    // Only the two guarded roster formatting reads get display text. Even
    // nested protocol/other consumers retain the exact native field pointer.
    if(!refreshing_names || (caller!=0x5ba66e && caller!=0x5c829b)) return raw;
    __try {
        unsigned n=0;while(n<31 && raw[n]) { native_display_name[n]=display_byte(static_cast<unsigned char>(raw[n]));++n; }
        native_display_name[n]=0;return native_display_name;
    } __except(EXCEPTION_EXECUTE_HANDLER) { native_display_name[0]=0;return native_display_name; }
}
bool suppress_lobby_action=false;
bool role_accept=false;
std::uint64_t input_generation=0;
bool lobby_action_valid(void* menu,unsigned mask);
bool lobby_mask(void* mgr,unsigned& mask) {
    __try { mask=at<unsigned(__thiscall*)(void*)>(0x5d4b00)(mgr);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { mask=0;return false; }
}
unsigned __fastcall lobby_input(void* mgr,void*) {
    unsigned mask=0;lobby_mask(mgr,mask);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(lobby_ticking && input_generation==lobby_state.generation && current()==lobby_state.owner && (caller==0x5bad33 || caller==0x5c88d2)) {
        if(mask&0x200000) {
            if(back_consumed) return 0;
            // Back must remain available, but may not smuggle a coincident
            // Accept past modal/Copy or Kick-target suppression.
            if(suppress_lobby_action || lobby_state.copy_block || pending.action==int(intent::copy_code) ||
                !lobby_action_valid(lobby_state.owner,mask)) return mask&0x200000;
            return mask;
        }
        if(pending.action==int(intent::copy_code)) lobby_state.copy_block=true;
        if(suppress_lobby_action || lobby_state.copy_block) return 0;
        const auto action=role_accept ? mask|0x10 : mask;
        if(!lobby_action_valid(lobby_state.owner,action)) { lobby_state.error=lobby_state.target_error=true;return 0; }
        return action;
    }
    return mask;
}
void* item(void* menu,const char* name) {
    return at<void*(__thiscall*)(void*,const char*)>(0x5adc10)(menu,name);
}
bool lobby_live(void* menu) {
    __try { return lobby(which(menu)) && item(menu,"lc_lobby_v1"); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool lobby_lists(void* menu) {
    __try {
        auto* a=item(menu,"text_playername1");auto* b=item(menu,"text_readycheck1");
        return a && b && a!=b && *static_cast<unsigned*>(a)==0x6a062c &&
            *static_cast<unsigned*>(b)==0x6a062c &&
            *reinterpret_cast<void**>(static_cast<char*>(a)+0xbc) &&
            *reinterpret_cast<void**>(static_cast<char*>(b)+0xbc);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Raw native reads stay inside this small SEH boundary; never retain pointers.
bool read_lobby_impl(void* menu,lobby_view& out) {
    __try {
        if(!lobby_lists(menu)) return false;
        const bool host=which(menu)==screen::lobby_host;
        auto* session=at<getter>(0x612be0)();
        auto* table=*reinterpret_cast<void**>(static_cast<char*>(menu)+(host ? 0x196c : 0x1968));
        if(!table || table!=at<void*(__thiscall*)(void*)>(0x610ca0)(session)) return false;
        const auto participants=*reinterpret_cast<int*>(static_cast<char*>(table)+0xc);
        if(participants<0 || participants>4) return false;
        void* records[4]{};
        auto* node=*static_cast<void**>(table);
        void* previous=nullptr;
        // The retail allocator/insert build ordinary NULL-terminated doubly
        // linked nodes. Validate the bounded shape BEFORE invoking the native
        // lookups, which themselves assume every linked node has a payload.
        for(int row=0;row<participants;++row) {
            if(!node || *reinterpret_cast<void**>(static_cast<char*>(node)+0xc)!=previous) return false;
            records[row]=*reinterpret_cast<void**>(static_cast<char*>(node)+0x10);
            if(!records[row]) return false;
            previous=node;node=*reinterpret_cast<void**>(static_cast<char*>(node)+8);
        }
        if(node || *reinterpret_cast<void**>(static_cast<char*>(table)+4)!=previous) return false;
        auto* local=at<void*(__thiscall*)(void*)>(0x6111f0)(session);
        out.local_known=local!=nullptr;
        if(local) out.local=at<unsigned(__thiscall*)(void*)>(0x614210)(local);
        auto* names=item(menu,"text_playername1");auto* checks=item(menu,"text_readycheck1");
        const auto count=at<int(__thiscall*)(void*)>(0x5bf750)(names);
        const auto other=at<int(__thiscall*)(void*)>(0x5bf750)(checks);
        // Native refresh emits one disabled name placeholder and no ready row.
        if(!participants) return (count==0 || count==1) && other==0;
        const auto pool=*reinterpret_cast<unsigned*>(0x8abe20);
        if(count!=participants || count!=other || pool>128 || pool<unsigned(count*2)) return false;
        const auto* name_rows=*reinterpret_cast<unsigned char**>(static_cast<char*>(names)+0xbc);
        const auto* ready_rows=*reinterpret_cast<unsigned char**>(static_cast<char*>(checks)+0xbc);
        bool slots[128]{};
        for(int row=0;row<count;++row) for(auto index:{name_rows[row],ready_rows[row]}) {
            if(index>=128 || slots[index]) return false;
            const auto bit=1u<<(index&31);
            if(!(*reinterpret_cast<const unsigned*>(0x8a83f0+0x3800+4*(index/32))&bit) ||
               !(*reinterpret_cast<const unsigned*>(0x8a83f0+0x3a20+4*(index/32))&bit)) return false;
            slots[index]=true;
        }
        out.count=count;out.selected=at<int(__thiscall*)(void*)>(0x5bf5c0)(names);
        for(int row=0;row<count;++row) {
            auto* participant=records[row];
            if(host) {
                out.ids[row]=at<unsigned(__thiscall*)(void*,int)>(0x5bfc20)(names,row);
                participant=at<void*(__thiscall*)(void*,unsigned)>(0x6110c0)(session,out.ids[row]);
                if(!participant || at<unsigned(__thiscall*)(void*)>(0x614210)(participant)!=out.ids[row]) return false;
            } else {
                // JOIN's actual 0x5c8150 never sets row values. Read the same
                // first/next participant sequence without changing its cursor.
                out.ids[row]=at<unsigned(__thiscall*)(void*)>(0x614210)(participant);
            }
            for(int previous=0;previous<row;++previous) if(out.ids[previous]==out.ids[row]) return false;
            const auto* name=at<const char*(__thiscall*)(void*)>(0x6141e0)(participant);
            unsigned size=0;while(size<31 && name[size]) { out.names[row][size]=name[size];++size; }
            out.names[row][size]=0;
            out.ready[row]=at<bool(__thiscall*)(void*)>(0x614240)(participant);
            out.host[row]=at<bool(__thiscall*)(void*)>(0x6142a0)(participant);
            out.ready_count+=out.ready[row] ? 1 : 0;
        }
        return true; // exactly `participants` real payloads, then NULL
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool read_lobby(void* menu,lobby_view& out) {
    // Retail lookup helpers use the list's shared iterator cursor. A UI read
    // must return it exactly as the native callback left it, including faults.
    __try {
        auto* table=at<void*(__thiscall*)(void*)>(0x610ca0)(at<getter>(0x612be0)());
        if(!table) return false;
        auto** cursor=reinterpret_cast<void**>(static_cast<char*>(table)+8);
        auto* saved=*cursor;bool result=false;
        __try { result=read_lobby_impl(menu,out); }
        __finally { *cursor=saved; }
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct lobby_native_state { int phase=0;bool host=false,gate=false,transfer=false,authority=false;unsigned minimum=2; };
bool lobby_state_read(void* menu,lobby_native_state& state) {
    __try {
        state.host=which(menu)==screen::lobby_host;
        state.phase=state.host ? *reinterpret_cast<int*>(static_cast<char*>(menu)+0x1960) : 0;
        state.gate=static_cast<unsigned char*>(menu)[0x195c]!=0;
        state.transfer=static_cast<unsigned char*>(menu)[state.host ? 0x1970 : 0x196c]!=0;
        state.minimum=*reinterpret_cast<const unsigned char*>(0x5bae37);
        if(state.host) state.authority=at<bool(__thiscall*)(void*)>(0x610d10)(at<getter>(0x612be0)());
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool lobby_phase(void* menu,int value) {
    __try { *reinterpret_cast<int*>(static_cast<char*>(menu)+0x1960)=value;return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool lobby_focus(void* menu,const char* target) {
    __try { at<void(__thiscall*)(void*,const char*,bool)>(0x5adf40)(menu,target,true);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool lobby_original(void* menu,unsigned original) {
    __try { at<tick_fn>(original)(menu);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int lobby_native_back(void* menu,unsigned original) {
    __try { return at<int(__thiscall*)(void*)>(original)(menu); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool lobby_native_activate(void* menu,int argument,unsigned original) {
    const bool marked=lobby_live(menu);
    bool result=true;if(marked) ++refreshing_names;
    __try { at<void(__thiscall*)(void*,int)>(original)(menu,argument); }
    __except(EXCEPTION_EXECUTE_HANDLER) { result=false; }
    if(marked) --refreshing_names;return result;
}
bool lobby_exit(void* menu) {
    __try { at<request_fn>(0x5d5db0)(manager(),"online",nullptr);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        __try { at<void(__thiscall*)(void*,const char*,int)>(0x5db270)(manager(),static_cast<char*>(menu)+0xc,0);return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
}
bool native_item_flag(void* menu,const char* name,unsigned mask,bool& value) {
    __try { auto* found=item(menu,name);if(!found) return false;value=(static_cast<unsigned char*>(found)[0x54]&mask)!=0;return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void picker_visibility(void* menu,bool show) {
    bool old=false;
    if(show) { bool enabled=false;if(!native_item_flag(menu,"text_playername1",8,enabled) || !enabled) show=false; }
    if(native_item_flag(menu,"text_playername1",4,old) && old==show) hide_native(menu,"text_playername1",!show);
    if(native_item_flag(menu,"text_readycheck1",4,old) && !old) hide_native(menu,"text_readycheck1",true);
}
bool sanitize_picker(void* menu) {
    __try {
        lobby_view view{};if(!read_lobby(menu,view)) return false;
        for(unsigned i=0;i<view.count;++i) {
            auto* row=const_cast<char*>(at<const char*(__thiscall*)(void*,int)>(0x5bfcd0)(item(menu,"text_playername1"),int(i)));
            if(!row) return false;
            char text[32]{};unsigned n=0;
            while(n<31 && view.names[i][n]) { text[n]=display_byte(static_cast<unsigned char>(view.names[i][n]));++n; }
            std::memcpy(row,text,n+1);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void lobby_after_refresh(void* menu) {
    if(!lobby_live(menu)) return;
    const bool host=which(menu)==screen::lobby_host;
    const bool safe=host && sanitize_picker(menu);
    if(lobby_state.owner==menu) lobby_state.picker_safe=safe;
    lobby_native_state state{};
    picker_visibility(menu,safe && lobby_state_read(menu,state) && state.phase==1);
}
bool lobby_action_valid(void* menu,unsigned mask) {
    lobby_view view{};
    // Missing presentation data must never trap Back/Leave. Kick is different:
    // accepting the picker requires a positively known, nonlocal native target.
    bool picker=false;
    __try {
        if(which(menu)==screen::lobby_host && (mask&0x10)) {
            const auto* bytes=static_cast<const char*>(menu);
            picker=*reinterpret_cast<const int*>(bytes+0x1960)==1 &&
                *reinterpret_cast<void* const*>(bytes+0x1964) &&
                *reinterpret_cast<void* const*>(bytes+0x324)==*reinterpret_cast<void* const*>(bytes+0x1964);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if(picker) {
        if(!read_lobby(menu,view) || !view.local_known) return false;
        return view.selected>=0 && unsigned(view.selected)<view.count && view.ids[view.selected]!=view.local;
    }
    return true;
}
void restore_lobby_focus(void* menu) {
    lobby_native_state state{};
    if(lobby_state_read(menu,state)) lobby_focus(menu,state.host ? (state.phase==1 ? "text_playername1" : "text_startgame") : "text_readygame");
}
// Snapshot fields supplied by host tests only. The ordinary DLL has no fixture
// provider, so an unknown room/RTT/build stays unknown until round 5.
void bind_lobby(void* menu,const lobby_view& view,bool valid) {
    lobby_fixture extra{};
    const bool fixture=lobby_fields && lobby_fields(extra) && extra.count<=4;
    std::optional<std::string> code_value;
    if(fixture) {
        const auto end=std::find(std::begin(extra.code),std::end(extra.code),char(0));
        if(end!=std::end(extra.code)) code_value=parse_code({extra.code,std::size_t(end-extra.code)});
    }
    std::string shown=code_value ? code_value->substr(0,4)+"-"+code_value->substr(4) : "---- ----";
    const bool host=which(menu)==screen::lobby_host;
    lobby_native_state native{};const bool native_valid=lobby_state_read(menu,native);
    // Extra Copy never manufactures an Accept or invokes a native action.
    const auto action=static_cast<intent>(pending.take(reinterpret_cast<std::uintptr_t>(menu),which(menu)));
    if(action==intent::dismiss || action==intent::back) {
        lobby_state.confirm=lobby_state.error=lobby_state.copied=false;
        restore_lobby_focus(menu);
    } else if(action==intent::confirm_leave && host && lobby_state.confirm) {
        lobby_state.confirm=false;
        lobby_state.leave_authorized=true;
        restore_lobby_focus(menu);
        if(!lobby_phase(menu,2)) lobby_state.leave_authorized=false;
    } else if(action==intent::copy_code && code_value) {
        // Bounded Unicode payload. SetClipboardData takes ownership on success.
        bool copied=false;
        const auto owner=clipboard_owner();
        if(owner && OpenClipboard(owner)) {
            auto memory=GlobalAlloc(GMEM_MOVEABLE,(shown.size()+1)*sizeof(wchar_t));
            if(memory) {
                auto* target=static_cast<wchar_t*>(GlobalLock(memory));
                if(target) {
                    for(unsigned i=0;i<=shown.size();++i) target[i]=i<shown.size() ? wchar_t(shown[i]) : 0;
                    GlobalUnlock(memory);
                    copied=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory);
                }
                if(!copied) GlobalFree(memory);
            }
            CloseClipboard();
        }
        lobby_state.copied=copied;lobby_state.error=!copied;
    }
    bind_text(menu,"lc_room_code",shown.c_str());
    enable(menu,"lc_copy_code",bool(code_value));
    for(unsigned i=0;i<4;++i) {
        const auto prefix="lc_p"+std::to_string(i+1)+"_";
        const bool member=valid && i<view.count;
        hide(menu,(prefix+"card").c_str(),!member);
        for(const auto* field:{"empty","empty_title","empty_help","empty_plus"}) hide(menu,(prefix+field).c_str(),member);
        for(const auto* field:{"name","slot","hero_fallback"}) hide(menu,(prefix+field).c_str(),!member);
        if(!member) {
            for(const auto* field:{"ready","not_ready","host","you","ping","ms","measuring","same_build","checking_build","selected"})
                hide(menu,(prefix+field).c_str(),true);
            continue;
        }
        const auto safe=display(view.names[i],31);
        bind_text(menu,(prefix+"name").c_str(),safe.c_str());
        bind_text(menu,(prefix+"slot").c_str(),("P"+std::to_string(i+1)).c_str());
        hide(menu,(prefix+"ready").c_str(),!view.ready[i]);
        hide(menu,(prefix+"not_ready").c_str(),view.ready[i]);
        hide(menu,(prefix+"host").c_str(),!view.host[i]);
        hide(menu,(prefix+"hero_fallback").c_str(),false);
        hide(menu,(prefix+"you").c_str(),(!view.local_known || view.ids[i]!=view.local));
        const lobby_fixture::member* data=nullptr;
        if(fixture) for(unsigned j=0;j<extra.count;++j) if(extra.members[j].native_index==view.ids[i]) {
            if(data) { data=nullptr;break; }data=&extra.members[j];
        }
        const bool measured=data && data->ping_ms>=0 && data->ping_ms<=60000 && (!view.local_known || view.ids[i]!=view.local);
        bind_text(menu,(prefix+"ping").c_str(),measured ? std::to_string(data->ping_ms).c_str() : "");
        hide(menu,(prefix+"ping").c_str(),!measured);
        hide(menu,(prefix+"ms").c_str(),!measured);
        hide(menu,(prefix+"measuring").c_str(),measured || (view.local_known && view.ids[i]==view.local));
        hide(menu,(prefix+"same_build").c_str(),!data || !data->same_build);
        hide(menu,(prefix+"checking_build").c_str(),data && data->same_build);
        const bool picked=native_valid && host && native.phase==1 && int(i)==view.selected;
        hide(menu,(prefix+"selected").c_str(),!picked);
    }
    bind_text(menu,"lc_ready_count",std::to_string(valid ? view.ready_count : 0).c_str());
    bind_text(menu,"lc_member_count",std::to_string(valid ? view.count : 0).c_str());
    unsigned waiting=0;while(waiting<view.count && view.ready[waiting]) ++waiting;
    bind_text(menu,"lc_waiting_name",valid && waiting<view.count ? display(view.names[waiting],31).c_str() : "");
    hide(menu,"lc_ready_waiting",!valid || waiting==view.count);
    hide(menu,"lc_ready_all",!valid || waiting!=view.count || view.count<2);
    hide(menu,"lc_ready_solo",!valid || view.count!=1 || waiting!=view.count);
    hide(menu,"lc_leave_confirm",!lobby_state.confirm);
    hide(menu,"lc_lobby_error",!lobby_state.error);
    hide(menu,"lc_code_copied",!lobby_state.copied);
    for(const auto* button:{"lc_leave_yes","lc_leave_cancel","lc_lobby_dismiss"}) {
        const bool visible=std::strcmp(button,"lc_lobby_dismiss")==0 ?
            (lobby_state.error || lobby_state.copied) && !lobby_state.confirm : lobby_state.confirm;
        hide(menu,button,!visible);enable(menu,button,visible);
    }
    if(lobby_state.focus_confirm) {
        lobby_state.focus_confirm=false;
        lobby_focus(menu,"lc_leave_cancel");
    }
    const bool picker=native_valid && host && native.phase==1;
    picker_visibility(menu,picker && valid && lobby_state.picker_safe);
    if(host && valid && native_valid && !picker && !native.transfer) {
        const bool allowed=native.authority && view.count>=native.minimum && view.ready_count==view.count &&
            !lobby_state.confirm && native.gate && native.phase==0;
        bool enabled=false;
        if(native_item_flag(menu,"text_startgame",8,enabled)) {
            if(!allowed && enabled) { lobby_state.start_dimmed=true;lobby_state.start_was_enabled=enabled; }
            if(enabled!=allowed) enable_native(menu,"text_startgame",allowed);
            if(allowed) lobby_state.start_dimmed=false;
        }
    } else if(host && !valid && native_valid && native.phase==0 && native.gate && !native.transfer && lobby_state.start_dimmed) {
        // Undo only our own prior dim when the presentation snapshot fails.
        // Native Start still enforces role, admission, readiness and minimum.
        enable_native(menu,"text_startgame",lobby_state.start_was_enabled);
        lobby_state.start_dimmed=false;
    }
    const auto local=std::find(std::begin(view.ids),std::begin(view.ids)+view.count,view.local);
    const auto local_row=unsigned(local-std::begin(view.ids));
    const bool local_ready=valid && view.local_known && local_row<view.count && view.ready[local_row];
    const auto self=valid && view.local_known && local_row<view.count ? display(view.names[local_row],31) : std::string();
    bind_text(menu,"lc_self_name",self.c_str());
    bind_text(menu,"lc_self_initial",self.empty() ? "" : self.substr(0,1).c_str());
    hide(menu,"lc_self_hosting",!host);
    hide(menu,"lc_self_joined",host || local_ready);
    hide(menu,"lc_self_ready",host || !local_ready);
    hide(menu,"lc_guest_unready",host || !local_ready);
    hide(menu,"lc_guest_leave",host || local_ready);

}
bool lobby_current_mask(unsigned& mask) {
    __try { return lobby_mask(manager(),mask); }
    __except(EXCEPTION_EXECUTE_HANDLER) { mask=0;return false; }
}
void lobby_tick(void* menu,unsigned original,bool& called) {
    if(!lobby_live(menu) || current_peek()!=menu) { called=true;lobby_original(menu,original);return; }
    if(lobby_state.owner!=menu) { lobby_state.reset(menu);bindings.clear();pending.clear(); }
    lobby_view before{};const bool valid=read_lobby(menu,before);
    lobby_native_state state{};const bool state_valid=lobby_state_read(menu,state);
    const bool host=which(menu)==screen::lobby_host;
    if(!valid || !state_valid) {
        lobby_state.confirm=lobby_state.focus_confirm=false;
        if(lobby_state.target_error) lobby_state.error=lobby_state.target_error=false;
    }
    if(valid && state_valid && host && !lobby_state.leave_authorized && state.phase==2 && lobby_phase(menu,0))
        lobby_state.confirm=lobby_state.focus_confirm=true;
    const bool picker=state_valid && host && state.phase==1;
    const bool bad_target=valid && before.local_known && picker &&
        (before.selected<0 || unsigned(before.selected)>=before.count || before.ids[before.selected]==before.local);
    if(lobby_state.target_error && valid && !bad_target) lobby_state.error=lobby_state.target_error=false;
    const bool panel=lobby_state.confirm;
    // Consent permits only this next native dispatch, even if our state read
    // faults or native code already consumed/reset phase 2 between callbacks.
    const bool consuming_leave=host && lobby_state.leave_authorized;
    unsigned input=0;lobby_current_mask(input);
    if(!(input&0x400010) && !lobby_state.copy_keyboard) lobby_state.copy_block=false;
    if(input&0x400000) {
        if(!lobby_state.copy_held) pending.post(int(intent::copy_code),reinterpret_cast<std::uintptr_t>(menu),which(menu));
        lobby_state.copy_held=true;
    } else lobby_state.copy_held=false;
    const bool tool=pending.action!=0;
    if(pending.action==int(intent::copy_code) || lobby_state.copy_held || lobby_state.copy_keyboard) lobby_state.copy_block=true;
    role_accept=false;
    if(input&0x1000000) {
        const bool local_member=valid && before.local_known &&
            std::find(std::begin(before.ids),std::begin(before.ids)+before.count,before.local)!=std::begin(before.ids)+before.count;
        if(!lobby_state.role_held && local_member && state_valid && !picker && !panel && !tool && state.gate && !state.transfer) {
            bool local_ready=false;
            for(unsigned i=0;i<before.count;++i) if(before.ids[i]==before.local) local_ready=before.ready[i];
            const auto* target=host ? "text_kickplayer" : local_ready ? "text_leaveroom" : "text_readygame";
            bool enabled=false;
            role_accept=native_item_flag(menu,target,8,enabled) && enabled && lobby_focus(menu,target);
        }
        lobby_state.role_held=true;
    } else lobby_state.role_held=false;
    // Only our own modal/tool state can suppress input. An invalid snapshot
    // always leaves native action/readiness/admission/exit policy in charge.
    suppress_lobby_action=panel || tool;
    struct input_scope {
        std::uint64_t generation;
        ~input_scope() { lobby_ticking=suppress_lobby_action=role_accept=back_consumed=false; }
    } restore{lobby_state.generation};
    input_generation=lobby_state.generation;lobby_ticking=true;back_consumed=false;called=true;
    if(consuming_leave) lobby_state.leave_authorized=false; // one native dispatch, never a reusable permission
    lobby_original(menu,original);
    lobby_ticking=false;
    if(lobby_state.generation!=restore.generation) return;
    if(current_peek()!=menu) { lobby_state.reset();bindings.clear();pending.clear();return; }
    lobby_native_state after_state{};const bool state_after=lobby_state_read(menu,after_state);
    if(valid && state_after && host && !panel && !consuming_leave && after_state.phase==2 && lobby_phase(menu,0))
        lobby_state.confirm=lobby_state.focus_confirm=true;
    lobby_view after{};const bool usable=read_lobby(menu,after);
    // Direct activation/update refreshes and in-tick callbacks are also covered.
    lobby_after_refresh(menu);
    bind_lobby(menu,after,usable);
}
void lobby_frame(void* menu,unsigned original) noexcept {
    bool called=false;
    if(!input_thread("lobby tick",false)) { lobby_original(menu,original);return; }
    try { lobby_tick(menu,original,called); }
    catch(...) {
        pending.clear();bindings.clear();
        logger::write("online menus: lobby fields unavailable; native controls retained");
        if(!called) lobby_original(menu,original);
    }
}
void __fastcall host_tick(void* menu,void*) { lobby_frame(menu,0x5bacd0); }
void __fastcall join_tick(void* menu,void*) { lobby_frame(menu,0x5c8840); }
void __fastcall lobby_base_tick(void* menu,void*) {
    const bool previous=base_ui_dispatch;base_ui_dispatch=true;
    lobby_original(menu,0x5aaeb0);base_ui_dispatch=previous;
}
void lobby_refresh(void* menu,unsigned original) {
    const bool marked=lobby_live(menu);
    if(marked) { if(lobby_state.owner==menu) lobby_state.picker_safe=false;picker_visibility(menu,false); }
    if(marked) ++refreshing_names;
    lobby_original(menu,original);
    if(marked) --refreshing_names;
    if(marked) lobby_after_refresh(menu);
}
void __fastcall host_refresh(void* menu,void*) { lobby_refresh(menu,0x5ba930); }
void __fastcall host_refresh_other(void* menu,void*) { lobby_refresh(menu,0x5ba940); }
void __fastcall host_update(void* menu,void*) { lobby_refresh(menu,0x5babc0); }
void __fastcall join_refresh(void* menu,void*) { lobby_refresh(menu,0x5c8620); }
void __fastcall join_update(void* menu,void*) { lobby_refresh(menu,0x5c8480); }
void lobby_activate(void* menu,int argument,unsigned original) {
    if(!lobby_live(menu)) { lobby_native_activate(menu,argument,original);return; }
    lobby_state.reset(menu);bindings.clear();pending.clear();
    if(!lobby_lists(menu)) {
        logger::write("online menus: malformed lobby skin; returning to native online");
        lobby_state.reset();lobby_exit(menu);return;
    }
    picker_visibility(menu,false);
    if(!lobby_native_activate(menu,argument,original)) { lobby_state.reset();lobby_exit(menu);return; }
    lobby_after_refresh(menu);
    try { lobby_view view{};const bool valid=read_lobby(menu,view);bind_lobby(menu,view,valid); }
    catch(...) { pending.clear();bindings.clear();logger::write("online menus: activation fields unavailable; native controls retained"); }
}
void __fastcall host_activate(void* menu,void*,int arg) { lobby_activate(menu,arg,0x5ba870); }
void __fastcall join_activate(void* menu,void*,int arg) { lobby_activate(menu,arg,0x5c8590); }
int lobby_back(void* menu,unsigned original) {
    // A native Join tick/mouse Leave branch dispatches this same slot. It must
    // retain native Unready/Leave behavior even while Copy/error text is visible.
    const bool native_dispatch=(lobby_ticking && !base_ui_dispatch) || native_mouse;
    if(!native_dispatch && lobby_live(menu) && lobby_state.owner==menu &&
        (lobby_state.confirm || lobby_state.error || lobby_state.copied)) {
        lobby_state.confirm=lobby_state.error=lobby_state.copied=lobby_state.target_error=false;pending.clear();
        restore_lobby_focus(menu);
        if(lobby_ticking) suppress_lobby_action=back_consumed=true;
        return 2;
    }
    return lobby_native_back(menu,original);
}
int __fastcall host_back(void* menu,void*) { return lobby_back(menu,0x5aaff0); }
int __fastcall join_back(void* menu,void*) { return lobby_back(menu,0x5c8c60); }
void lobby_deactivate(void* menu,unsigned original) {
    if(lobby_state.owner==menu) { lobby_state.reset();bindings.clear();pending.clear(); }
    lobby_original(menu,original);
}
void __fastcall host_deactivate(void* menu,void*) { lobby_deactivate(menu,0x5ba280); }
void __fastcall join_deactivate(void* menu,void*) { lobby_deactivate(menu,0x5c8070); }
bool lobby_mouse_native(void* menu,unsigned original,int x,int y,int flags,int message,int extra) {
    __try { return at<bool(__thiscall*)(void*,int,int,int,int,int)>(original)(menu,x,y,flags,message,extra); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool lobby_mouse(void* menu,unsigned original,int x,int y,int flags,int message,int extra) {
    const bool previous=native_mouse;native_mouse=true;
    const bool result=lobby_mouse_native(menu,original,x,y,flags,message,extra);
    native_mouse=previous;
    if(game_thread.load()==GetCurrentThreadId() && lobby_live(menu) && current_peek()==menu) lobby_after_refresh(menu);
    return result;
}
bool __fastcall host_mouse(void* menu,void*,int x,int y,int flags,int message,int extra) { return lobby_mouse(menu,0x5ba2b0,x,y,flags,message,extra); }
bool __fastcall join_mouse(void* menu,void*,int x,int y,int flags,int message,int extra) { return lobby_mouse(menu,0x5c8da0,x,y,flags,message,extra); }
void show_notice(void* menu) {
    // Builder-owned localized text. Only visibility, never English replacement.
    constexpr const char* notices[]{"lc_notice_unavailable","lc_notice_resolving","lc_notice_fixture",
        "lc_notice_paste_invalid","lc_notice_code_invalid","lc_notice_name_invalid",
        "lc_notice_profile_save","lc_notice_name_open","lc_notice_recent_save"};
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
    error_visible=false;feedback.clear();name_requested_at=now_ms();
    name_io io;
    if(!name_native(name_operation::begin,io)) { name_fault();return false; }
    return true;
}
void poll_name() {
    if(!name_edit.owned) return;
    if(!name_edit.departed) {
        if(now_ms()-name_requested_at>5000) { abandon_name();message("lc_notice_name_open"); }
        return;
    }
    name_io io;
    if(!name_native(name_operation::poll,io)) { name_fault();return; }
    if(io.result && io.tag==0) {
        const auto end=std::find(std::begin(io.copy),std::end(io.copy),char(0));
        if(end==std::end(io.copy) || !valid_name({io.copy,std::size_t(end-io.copy)})) message("lc_notice_name_invalid");
    }
    const auto value=name_edit.finish(io.result ? io.tag : 4,io.copy,sizeof(io.copy));
    if(!value) return;
    io.accepted=value->c_str();
    if(!name_native(name_operation::apply,io)) { name_fault();return; }
    std::memcpy(local_name,value->c_str(),value->size()+1);
    if(!io.saved) message("lc_notice_profile_save");
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
    unsigned mask=0;lobby_current_mask(mask);return mask;
}
void schedule_fixture(bool retry=false) {
    const auto ticket=code.resolve(retry);
    if(ticket) { fixture_ticket=ticket; fixture_code=code.text; message("lc_notice_resolving"); }
}
void code_input(int value) {
    switch(static_cast<intent>(value)) {
    case intent::paste: {
        clipboard_api api;const auto text=read_clipboard(api);
        if(text) { code.replace(*text);error_visible=false; }
        else message("lc_notice_paste_invalid");
    } break;
    case intent::erase: code.erase();error_visible=false;break;
    case intent::clear: code.replace("");error_visible=false;break;
    case intent::done: schedule_fixture(true);break;
    case intent::invalid_key: message("lc_notice_code_invalid");break;
    default:
        if(value>=32 && value<=62) { code.append(alphabet[value-32]);error_visible=false; }
        break;
    }
}
void tick_impl(void* menu,bool& native_called) {
    if(ready && live(menu)) {
        // The native mask is rising-edge input (controller +2f8 XOR +374).
        // Keyboard text already posted by DirectInput wins over X/Y bindings.
        const auto mask=input_mask();
        if(!(mask&0x200000)) back_held=false;
        const auto s=which(menu);
        if(!pending.action && !keyboard_input.count) {
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
    if(!ready || !live(menu)) { pending.clear();keyboard_input.clear();return; }
    const auto s=which(menu);
    if(s==screen::hub) entry.clear();
    load_recent();
    if(s==screen::hub) poll_name();
    else abandon_name();
    if(s==screen::code && !code.active) {
        code.open();
        if(rejoin_seed) { code.replace(*rejoin_seed);rejoin_seed.reset(); }
    }
    if(s!=screen::code && code.active) { code.close(); fixture_ticket=0; }
    const auto completed_ticket=fixture_ticket;
    const auto completed_code=fixture_code;
    fixture_ticket=0;
    const auto action=static_cast<intent>(pending.take(reinterpret_cast<std::uintptr_t>(menu),s));
    if(s==screen::hub && int(action) && action!=intent::rejoin) rejoin_seed.reset();
    if(s==screen::code && action!=intent::back && keyboard_input.source==reinterpret_cast<std::uintptr_t>(menu))
        for(unsigned i=0;i<keyboard_input.count;++i) code_input(keyboard_input.values[i]);
    keyboard_input.clear();
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
    case intent::paste: case intent::erase: case intent::clear: case intent::done: case intent::invalid_key:
        if(s==screen::code) code_input(int(action));break;
    case intent::rejoin: if(s==screen::hub && recent) {
        rejoin_seed=recent->code;destination="lc_code";
    } break;
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
        message(remember_fixture() ? "lc_notice_fixture" : "lc_notice_recent_save");
    }
    if(s==screen::code) schedule_fixture();
    if(destination) {
        if(s==screen::code) { code.close(); fixture_ticket=0; }
        pending.clear();keyboard_input.clear();error_visible=false;accepted=-1;
        if(!abandon_name()) { show_notice(menu);return; }
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
    if(!input_thread("tick",false)) { at<tick_fn>(0x5aaeb0)(menu);return; }
    try { tick_impl(menu,native_called); }
    catch(...) {
        pending.clear();keyboard_input.clear();++code.generation;code.submitted=code.generation;fixture_ticket=0;bindings.clear();
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
    pending.clear();keyboard_input.clear();
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
    if(!ready || !input_thread("keyboard",true)) return;
    if(!game_thread.load()) return; // no frontend/input model work before the first game tick
    if(!state) { keyboard_edges.reset();keyboard_input.clear();lobby_state.copy_keyboard=false;return; }
    const auto edges=keyboard_edges.sample(state);
    // DirectInput can poll before frontend creation. Never invoke the lazy
    // 0x5d8920 getter here; even validation must use the already-published cell.
    auto* menu=current_peek();
    if(!menu) return;
    const auto s=which(menu);
    if(!custom(s) && !lobby_live(menu)) return;
    const auto address=reinterpret_cast<std::uintptr_t>(menu);
    const bool control=(state[0x1d]|state[0x9d])&0x80;
    const bool alt=(state[0x38]|state[0xb8])&0x80;
    if(lobby(s)) {
        lobby_state.copy_keyboard=!alt && control && (state[0x2e]&0x80);
        if(!alt && control && edges[0x2e]) pending.post(int(intent::copy_code),address,s);
        if(!alt && control) state[0x2e]=0;
        return;
    }
    if(s!=screen::code || alt) return;
    const bool shift=(state[0x2a]|state[0x36])&0x80;
    auto post=[&](int value) { keyboard_input.push(address,value); };
    if(control) {
        if(edges[0x2f]) post(int(intent::paste));
    } else if(edges[0xd3]) post(int(shift ? intent::clear : intent::erase));
    else if(edges[0x0e]) post(int(intent::erase));
    else {
        bool valid=false,invalid=false;
        constexpr unsigned char scans[]{0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x24,0x25,0x32,0x31,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a};
        constexpr unsigned char numpad[]{0x50,0x51,0x4b,0x4c,0x4d,0x47,0x48,0x49};
        for(unsigned scan=0;scan<256;++scan) if(edges[scan]) {
            for(unsigned i=0;i<31;++i) if(scans[i]==scan) { post(32+i);valid=true; }
            for(unsigned i=0;i<8;++i) if(numpad[i]==scan) { post(32+23+i);valid=true; }
            for(unsigned bad:{0x0bu,0x18u,0x02u,0x17u,0x26u,0x52u,0x4fu}) if(bad==scan) invalid=true;
        }
        const bool done=edges[0x1c] || edges[0x9c];
        if(done) post(int(intent::done));
        else if(invalid && !valid) post(int(intent::invalid_key));
    }
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
    const std::pair<unsigned,const void*> lobby_hooks[]{
        {0x69f284,reinterpret_cast<const void*>(&host_activate)},
        {0x69f350,reinterpret_cast<const void*>(&join_activate)},
        {0x69f290,reinterpret_cast<const void*>(&host_deactivate)},
        {0x69f358,reinterpret_cast<const void*>(&join_deactivate)},
        {0x69f2a0,reinterpret_cast<const void*>(&host_back)},
        {0x69f368,reinterpret_cast<const void*>(&join_back)},
        {0x69f2ac,reinterpret_cast<const void*>(&host_tick)},
        {0x69f374,reinterpret_cast<const void*>(&join_tick)},
        {0x6a24ac,reinterpret_cast<const void*>(&lobby_input)},
        {0x69f2a4,reinterpret_cast<const void*>(&host_update)},
        {0x69f320,reinterpret_cast<const void*>(&host_refresh)},
        {0x69f328,reinterpret_cast<const void*>(&host_refresh_other)},
        {0x69f3e8,reinterpret_cast<const void*>(&join_refresh)},
        {0x69f3f0,reinterpret_cast<const void*>(&join_refresh)},
        {0x69f314,reinterpret_cast<const void*>(&host_refresh)},
        {0x69f3dc,reinterpret_cast<const void*>(&join_refresh)},
        {0x69f36c,reinterpret_cast<const void*>(&join_update)},
        {0x69f2f4,reinterpret_cast<const void*>(&host_mouse)},
        {0x69f3bc,reinterpret_cast<const void*>(&join_mouse)}};
    for(const auto& hook:lobby_hooks) c.writes.push_back(patch_transaction::relocation(hook.first,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hook.second)),0,"native lobby adapter"));
    for(const auto site:{0x5bad1fu,0x5c88beu}) c.writes.push_back(patch_transaction::relocation(site,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&lobby_base_tick)),0,"native lobby base-input boundary"));
    c.writes.push_back(patch_transaction::relocation(0x6141e0,"e9",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&participant_display_name)),0,"refresh-scoped display name getter"));
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
        lobby_fields=hooks->lobby_fields;
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
    logger::write("online menus: dispatcher lcOnlineIntent n(i) registered; 29 hooks committed atomically; effective script capacity=%d; requires lc-online-menus/1 assets; input API=%s; lobby API=%s",script_functions::capacity(),input_capability,lobby_capability);
}
}
